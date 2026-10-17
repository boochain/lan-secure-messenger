//! The engine: a single actor that owns all state and a connection manager that keeps
//! exactly one long-lived Noise link per peer.
//!
//! Presence == "an authenticated link exists". Discovery (UDP), manual addresses and
//! remembered addresses only produce *dial candidates*. Whoever learns of the other first
//! dials; after the handshake both sides register the peer online, so one-way UDP
//! visibility (typical with Windows firewall / multi-NIC) still yields symmetric presence.
//! Simultaneous dials are resolved deterministically: the link initiated by the lower
//! fingerprint wins on both ends.

use crate::api::*;
use crate::config::{ConfigPaths, Settings};
use crate::contacts::{ContactStore, Trust};
use crate::crypto::{open_at_rest, seal_at_rest};
use crate::discovery::{AnnounceInfo, DiscoParams, Discovery, Seen};
use crate::group::{invite_sign_bytes, rekey_sign_bytes, GroupInfo, GroupStore};
use crate::history::HistoryStore;
use crate::identity::Identity;
use crate::link::{self, Link, LinkWriter};
mod files;
use files::{TransferEvt, TransferRt};
use crate::wire::{Frame, Hello, Payload, PROTO_VERSION};
use crate::APP_VERSION;
use anyhow::Result;
use ed25519_dalek::VerifyingKey;
use serde::Deserialize;
use serde_json::{json, Value};
use std::collections::{HashMap, HashSet, VecDeque};
use std::net::{SocketAddr, ToSocketAddrs};
use std::path::PathBuf;
use std::sync::atomic::{AtomicU16, Ordering};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};
use tokio::net::{TcpListener, TcpStream};
use tokio::sync::{broadcast, mpsc, oneshot, Notify};
use uuid::Uuid;

const PING_EVERY: Duration = Duration::from_secs(5);
const DEAD_AFTER: Duration = Duration::from_secs(20);
const STALE_FOR_TIEBREAK: Duration = Duration::from_secs(12);
const BULK_QUEUE: usize = 64;

#[derive(Debug, Clone)]
pub struct RpcError {
    pub code: i64,
    pub message: String,
}

impl RpcError {
    pub fn new(code: i64, m: impl Into<String>) -> Self {
        Self { code, message: m.into() }
    }
    pub fn invalid(m: impl Into<String>) -> Self {
        Self::new(-32602, m)
    }
}

impl From<anyhow::Error> for RpcError {
    fn from(e: anyhow::Error) -> Self {
        RpcError::new(-32000, format!("{e:#}"))
    }
}

type RpcResult = std::result::Result<Value, RpcError>;

pub struct EngineOptions {
    pub paths: ConfigPaths,
    /// Displayed in diagnostics / snapshot.
    pub ipc_endpoint: String,
    /// Apply on top of settings.json (tests / CLI flags); not persisted.
    pub overrides: Option<Value>,
}

#[derive(Clone)]
pub struct EngineHandle {
    tx: mpsc::UnboundedSender<EngineMsg>,
    events: broadcast::Sender<Event>,
    pub shutdown: Arc<Notify>,
    pub fingerprint: String,
}

impl EngineHandle {
    pub async fn call(&self, method: &str, params: Value) -> RpcResult {
        let (reply, rx) = oneshot::channel();
        self.tx
            .send(EngineMsg::Rpc { method: method.to_string(), params, reply })
            .map_err(|_| RpcError::new(-32001, "engine stopped"))?;
        rx.await.map_err(|_| RpcError::new(-32001, "engine stopped"))?
    }
    pub fn subscribe(&self) -> broadcast::Receiver<Event> {
        self.events.subscribe()
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub(crate) enum DialKey {
    Peer(String),
    Manual(String),
}

pub(crate) enum EngineMsg {
    Rpc { method: String, params: Value, reply: oneshot::Sender<RpcResult> },
    LinkUp { link: Box<Link>, initiator: bool, key: Option<DialKey> },
    DialFailed { key: DialKey, err: String },
    Frame { fp: String, conn_id: u64, frame: Frame },
    Closed { fp: String, conn_id: u64, err: String },
    Seen(Seen),
    Transfer(TransferEvt),
    Tick,
}

pub(crate) struct Conn {
    pub id: u64,
    pub initiator: bool,
    pub ctrl: mpsc::UnboundedSender<Frame>,
    pub bulk: mpsc::Sender<Frame>,
    addr: SocketAddr,
    since: Instant,
    last_rx: Instant,
    last_ping: Instant,
    rtt_ms: Option<u32>,
    reader: tokio::task::AbortHandle,
    app_version: String,
}

#[derive(Default)]
struct PeerRt {
    announce_addr: Option<SocketAddr>,
    last_announce: Option<Instant>,
    announce_name: String,
    sources: HashSet<String>,
    dialing: bool,
    failures: u32,
    next_attempt: Option<Instant>,
    last_error: String,
}

#[derive(Default)]
struct ManualRt {
    dialing: bool,
    failures: u32,
    next_attempt: Option<Instant>,
    last_error: String,
    fp: Option<String>,
}

struct Outgoing {
    fp: String,
    id: Uuid,
    payload: Payload,
    /// History message whose status tracks this delivery.
    msg_ref: Option<String>,
}

struct Shared {
    identity: Identity,
    name: Mutex<String>,
    listen_port: AtomicU16,
}

impl Shared {
    fn hello(&self) -> Hello {
        Hello {
            proto: PROTO_VERSION,
            app_version: APP_VERSION.to_string(),
            name: self.name.lock().unwrap().clone(),
            ed25519_pk: self.identity.ed25519_public_bytes(),
            listen_port: self.listen_port.load(Ordering::Relaxed),
        }
    }
}

pub struct Engine {
    shared: Arc<Shared>,
    me: String,
    paths: ConfigPaths,
    pub(crate) settings: Settings,
    ipc_endpoint: String,
    contacts: ContactStore,
    history: HistoryStore,
    groups: GroupStore,
    events: broadcast::Sender<Event>,
    tx: mpsc::UnboundedSender<EngineMsg>,
    conns: HashMap<String, Conn>,
    rt: HashMap<String, PeerRt>,
    manual_rt: HashMap<String, ManualRt>,
    outbox: Vec<Outgoing>,
    unread: HashMap<String, Vec<Uuid>>,
    pub(crate) transfers: HashMap<Uuid, TransferRt>,
    next_conn_id: u64,
    tcp_error: String,
    disco: Discovery,
    pending_pin: Option<(String, Instant, u8)>,
    pin_waiters: HashMap<String, oneshot::Sender<RpcResult>>,
    seen_ids: VecDeque<Uuid>,
    seen_set: HashSet<Uuid>,
    shutdown: Arc<Notify>,
}

pub async fn start(opts: EngineOptions) -> Result<EngineHandle> {
    let paths = opts.paths;
    paths.ensure_dirs()?;
    let identity = Identity::load_or_create(&paths)?;
    let mut settings = Settings::load(&paths)?;
    if let Some(ov) = opts.overrides {
        let mut v = serde_json::to_value(&settings)?;
        merge(&mut v, &ov);
        settings = serde_json::from_value(v)?;
    }
    let contacts = ContactStore::load(&paths)?;
    let history = HistoryStore::load(&paths, settings.history_enabled)?;
    let groups = GroupStore::load(&paths)?;
    let (tx, mut rx) = mpsc::unbounded_channel();
    let (events, _) = broadcast::channel(4096);
    let shutdown = Arc::new(Notify::new());
    let me = identity.fingerprint().to_string();

    // TCP listener: preferred port, else ephemeral.
    let mut tcp_error = String::new();
    let listener = match TcpListener::bind(("0.0.0.0", settings.listen_port)).await {
        Ok(l) => l,
        Err(e) => {
            tcp_error = format!("TCP {} 被占用 ({e})，改用随机端口", settings.listen_port);
            tracing::warn!("{tcp_error}");
            TcpListener::bind(("0.0.0.0", 0)).await?
        }
    };
    let port = listener.local_addr()?.port();
    let shared = Arc::new(Shared { identity: identity.clone(), name: Mutex::new(settings.display_name.clone()), listen_port: AtomicU16::new(port) });
    tracing::info!(fingerprint = %me, port, "engine starting");

    let (seen_tx, mut seen_rx) = mpsc::unbounded_channel::<Seen>();
    let disco = if settings.discovery_enabled {
        Discovery::start(
            identity.clone(),
            DiscoParams {
                port: settings.discovery_port,
                listen: settings_listen(&settings),
                multicast: settings.multicast_enabled,
                interval: Duration::from_secs(settings.announce_interval_secs.clamp(1, 60)),
                targets: settings.announce_targets.clone(),
                include: settings.include_interfaces.clone(),
                exclude: settings.exclude_interfaces.clone(),
            },
            AnnounceInfo { name: settings.display_name.clone(), tcp_port: port },
            seen_tx,
        )
    } else {
        drop(seen_tx);
        Discovery::disabled()
    };
    {
        let tx = tx.clone();
        tokio::spawn(async move {
            while let Some(s) = seen_rx.recv().await {
                if tx.send(EngineMsg::Seen(s)).is_err() {
                    break;
                }
            }
        });
    }
    // Accept loop.
    {
        let tx = tx.clone();
        let shared = shared.clone();
        tokio::spawn(async move {
            loop {
                match listener.accept().await {
                    Ok((stream, addr)) => {
                        let tx = tx.clone();
                        let shared = shared.clone();
                        tokio::spawn(async move {
                            match link::establish(stream, &shared.identity, shared.hello(), false).await {
                                Ok(l) => {
                                    let _ = tx.send(EngineMsg::LinkUp { link: Box::new(l), initiator: false, key: None });
                                }
                                Err(e) => tracing::debug!(%addr, error = %e, "inbound handshake failed"),
                            }
                        });
                    }
                    Err(e) => {
                        tracing::warn!(error = %e, "accept failed");
                        tokio::time::sleep(Duration::from_millis(200)).await;
                    }
                }
            }
        });
    }
    // Ticker.
    {
        let tx = tx.clone();
        tokio::spawn(async move {
            let mut t = tokio::time::interval(Duration::from_millis(500));
            loop {
                t.tick().await;
                if tx.send(EngineMsg::Tick).is_err() {
                    break;
                }
            }
        });
    }

    let mut eng = Engine {
        shared,
        me: me.clone(),
        paths,
        settings,
        ipc_endpoint: opts.ipc_endpoint,
        contacts,
        history,
        groups,
        events: events.clone(),
        tx: tx.clone(),
        conns: HashMap::new(),
        rt: HashMap::new(),
        manual_rt: HashMap::new(),
        outbox: Vec::new(),
        unread: HashMap::new(),
        transfers: HashMap::new(),
        next_conn_id: 1,
        tcp_error,
        disco,
        pending_pin: None,
        pin_waiters: HashMap::new(),
        seen_ids: VecDeque::new(),
        seen_set: HashSet::new(),
        shutdown: shutdown.clone(),
    };
    eng.rebuild_outbox();
    // Redial remembered trusted peers immediately.
    let fps: Vec<String> = eng.contacts.contacts.keys().cloned().collect();
    for fp in fps {
        eng.rt.entry(fp).or_default().next_attempt = Some(Instant::now());
    }
    let handle = EngineHandle { tx, events, shutdown, fingerprint: me };
    tokio::spawn(async move {
        while let Some(msg) = rx.recv().await {
            let stop = eng.handle(msg);
            if stop {
                break;
            }
        }
        eng.on_shutdown();
    });
    Ok(handle)
}

fn settings_listen(s: &Settings) -> bool {
    s.discovery_enabled && s.discovery_listen
}

/// Deep-merge JSON objects (used for settings overrides).
pub fn merge_pub(dst: &mut Value, src: &Value) {
    merge(dst, src)
}

pub(crate) fn merge(dst: &mut Value, src: &Value) {
    match (dst, src) {
        (Value::Object(d), Value::Object(s)) => {
            for (k, v) in s {
                merge(d.entry(k.clone()).or_insert(Value::Null), v);
            }
        }
        (d, s) => *d = s.clone(),
    }
}

fn now_ms() -> i64 {
    chrono::Utc::now().timestamp_millis()
}

fn jitter(ms: u64) -> Duration {
    Duration::from_millis(rand::random::<u64>() % ms.max(1))
}

fn backoff(failures: u32) -> Duration {
    let base = 500u64.saturating_mul(1u64 << failures.min(7)); // 0.5s .. 64s
    Duration::from_millis(base.min(30_000)) + jitter(400)
}

async fn writer_task(mut w: LinkWriter, mut ctrl: mpsc::UnboundedReceiver<Frame>, mut bulk: mpsc::Receiver<Frame>, tx: mpsc::UnboundedSender<EngineMsg>, fp: String, conn_id: u64) {
    loop {
        let f = tokio::select! {
            biased;
            f = ctrl.recv() => match f { Some(f) => f, None => break },
            Some(f) = bulk.recv() => f,
        };
        let bye = matches!(f, Frame::Bye);
        if let Err(e) = w.send(&f).await {
            let _ = tx.send(EngineMsg::Closed { fp, conn_id, err: format!("write: {e}") });
            return;
        }
        if bye {
            break;
        }
    }
    w.shutdown().await;
}

impl Engine {
    fn emit(&self, e: Event) {
        let _ = self.events.send(e);
    }

    fn notice(&self, level: &str, text: impl Into<String>) {
        let text = text.into();
        tracing::info!(level, "{text}");
        self.emit(Event::Notice { level: level.into(), text });
    }

    fn handle(&mut self, msg: EngineMsg) -> bool {
        match msg {
            EngineMsg::Rpc { method, params, reply } => {
                if method == "shutdown" {
                    let _ = reply.send(Ok(json!({"ok": true})));
                    return true;
                }
                self.rpc(&method, params, reply);
            }
            EngineMsg::LinkUp { link, initiator, key } => self.on_link_up(*link, initiator, key),
            EngineMsg::DialFailed { key, err } => self.on_dial_failed(key, err),
            EngineMsg::Frame { fp, conn_id, frame } => {
                let Some(c) = self.conns.get_mut(&fp) else { return false };
                if c.id != conn_id {
                    return false;
                }
                c.last_rx = Instant::now();
                self.on_frame(&fp, frame);
            }
            EngineMsg::Closed { fp, conn_id, err } => self.on_closed(&fp, conn_id, &err),
            EngineMsg::Seen(s) => self.on_seen(s),
            EngineMsg::Transfer(ev) => self.on_transfer_evt(ev),
            EngineMsg::Tick => self.on_tick(),
        }
        false
    }

    fn on_shutdown(&mut self) {
        for c in self.conns.values() {
            let _ = c.ctrl.send(Frame::Bye);
        }
        let _ = self.history.flush(true);
        let _ = self.contacts.save();
        tracing::info!("engine stopped");
        self.shutdown.notify_waiters();
        self.shutdown.notify_one();
    }

    // ---------------------------------------------------------------- connections

    fn on_link_up(&mut self, link: Link, initiator: bool, key: Option<DialKey>) {
        let fp = link.remote_fp.to_string();
        match &key {
            Some(DialKey::Peer(want)) => {
                let r = self.rt.entry(want.clone()).or_default();
                r.dialing = false;
                if *want != fp {
                    r.failures += 1;
                    r.last_error = format!("该地址现在属于另一个身份 {fp}");
                    r.next_attempt = Some(Instant::now() + backoff(r.failures));
                }
            }
            Some(DialKey::Manual(addr)) => {
                let m = self.manual_rt.entry(addr.clone()).or_default();
                m.dialing = false;
                m.failures = 0;
                m.last_error.clear();
                m.fp = Some(fp.clone());
            }
            None => {}
        }
        if self.contacts.trust(&fp) == Trust::Blocked {
            tracing::info!(%fp, "rejecting blocked peer");
            let mut w = link.writer;
            tokio::spawn(async move { w.shutdown().await });
            return;
        }
        // Tie-break.
        if let Some(existing) = self.conns.get(&fp) {
            let pref = |init_is_me: bool| if init_is_me { self.me < fp } else { fp < self.me };
            let keep_new = if existing.last_rx.elapsed() > STALE_FOR_TIEBREAK {
                true
            } else if pref(existing.initiator) == pref(initiator) {
                // Same direction twice: the peer reconnected; the old one is stale.
                true
            } else {
                pref(initiator)
            };
            if !keep_new {
                tracing::debug!(%fp, "tie-break: dropping duplicate link (initiator={initiator})");
                let mut w = link.writer;
                tokio::spawn(async move { w.shutdown().await });
                return;
            }
            let old = self.conns.remove(&fp).unwrap();
            old.reader.abort();
            self.fail_transfers_for(&fp, "连接被替换");
            tracing::debug!(%fp, "tie-break: replacing existing link");
        }
        let hello = link.remote_hello.clone();
        let addr = link.remote_addr;
        let dial_addr = if initiator { addr } else { SocketAddr::new(addr.ip(), hello.listen_port) };
        let conn_id = self.next_conn_id;
        self.next_conn_id += 1;
        let (ctrl_tx, ctrl_rx) = mpsc::unbounded_channel();
        let (bulk_tx, bulk_rx) = mpsc::channel(BULK_QUEUE);
        tokio::spawn(writer_task(link.writer, ctrl_rx, bulk_rx, self.tx.clone(), fp.clone(), conn_id));
        let mut reader = link.reader;
        let tx = self.tx.clone();
        let fp2 = fp.clone();
        let rd = tokio::spawn(async move {
            loop {
                match reader.recv().await {
                    Ok(Frame::Bye) => {
                        let _ = tx.send(EngineMsg::Closed { fp: fp2, conn_id, err: "peer closed".into() });
                        break;
                    }
                    Ok(frame) => {
                        if tx.send(EngineMsg::Frame { fp: fp2.clone(), conn_id, frame }).is_err() {
                            break;
                        }
                    }
                    Err(e) => {
                        let _ = tx.send(EngineMsg::Closed { fp: fp2, conn_id, err: format!("{e}") });
                        break;
                    }
                }
            }
        });
        let now = Instant::now();
        self.conns.insert(
            fp.clone(),
            Conn {
                id: conn_id,
                initiator,
                ctrl: ctrl_tx,
                bulk: bulk_tx,
                addr,
                since: now,
                last_rx: now,
                last_ping: now,
                rtt_ms: None,
                reader: rd.abort_handle(),
                app_version: hello.app_version.clone(),
            },
        );
        {
            let auto = self.settings.auto_trust;
            let c = self.contacts.entry(&fp);
            c.name = hello.name.clone();
            c.ed25519_pk = hex::encode(hello.ed25519_pk);
            c.last_seen_ms = now_ms();
            if auto && c.trust == Trust::Unknown {
                c.trust = Trust::Trusted;
            }
        }
        if hello.listen_port != 0 {
            self.contacts.note_addr(&fp, &dial_addr.to_string());
        }
        let _ = self.contacts.save();
        let r = self.rt.entry(fp.clone()).or_default();
        r.dialing = false;
        r.failures = 0;
        r.last_error.clear();
        r.next_attempt = None;
        r.sources.insert(if initiator { "outbound".into() } else { "inbound".into() });
        tracing::info!(%fp, name = %hello.name, %addr, initiator, "peer online");
        self.emit_peer(&fp);
        self.flush_outbox(&fp);
        self.resend_offers(&fp);
    }

    fn on_closed(&mut self, fp: &str, conn_id: u64, err: &str) {
        let Some(c) = self.conns.get(fp) else { return };
        if c.id != conn_id {
            return;
        }
        let c = self.conns.remove(fp).unwrap();
        c.reader.abort();
        tracing::info!(%fp, err, "peer offline");
        self.contacts.entry(fp).last_seen_ms = now_ms();
        let r = self.rt.entry(fp.to_string()).or_default();
        r.last_error = err.to_string();
        r.next_attempt = Some(Instant::now() + Duration::from_millis(300) + jitter(700));
        self.fail_transfers_for(fp, &format!("连接断开: {err}"));
        self.emit_peer(fp);
    }

    fn drop_conn(&mut self, fp: &str, why: &str) {
        if let Some(c) = self.conns.get(fp) {
            let id = c.id;
            let _ = c.ctrl.send(Frame::Bye);
            self.on_closed(fp, id, why);
        }
    }

    fn on_dial_failed(&mut self, key: DialKey, err: String) {
        match key {
            DialKey::Peer(fp) => {
                let r = self.rt.entry(fp.clone()).or_default();
                r.dialing = false;
                r.failures += 1;
                r.last_error = err.clone();
                r.next_attempt = Some(Instant::now() + backoff(r.failures));
                tracing::debug!(%fp, failures = r.failures, %err, "dial failed");
                self.emit_peer(&fp);
            }
            DialKey::Manual(addr) => {
                let m = self.manual_rt.entry(addr.clone()).or_default();
                m.dialing = false;
                m.failures += 1;
                m.last_error = err.clone();
                m.next_attempt = Some(Instant::now() + backoff(m.failures).max(Duration::from_secs(2)));
                tracing::debug!(%addr, %err, "manual dial failed");
            }
        }
    }

    fn spawn_dial(&self, key: DialKey, addrs: Vec<SocketAddr>) {
        let tx = self.tx.clone();
        let shared = self.shared.clone();
        tokio::spawn(async move {
            let mut last = String::from("no address");
            for a in addrs {
                match tokio::time::timeout(Duration::from_secs(3), TcpStream::connect(a)).await {
                    Ok(Ok(s)) => match link::establish(s, &shared.identity, shared.hello(), true).await {
                        Ok(l) => {
                            let _ = tx.send(EngineMsg::LinkUp { link: Box::new(l), initiator: true, key: Some(key) });
                            return;
                        }
                        Err(e) => last = format!("{a}: 握手失败 {e}"),
                    },
                    Ok(Err(e)) => last = format!("{a}: {e}"),
                    Err(_) => last = format!("{a}: 连接超时 (可能被防火墙拦截)"),
                }
            }
            let _ = tx.send(EngineMsg::DialFailed { key, err: last });
        });
    }

    fn candidates(&self, fp: &str) -> Vec<SocketAddr> {
        let mut v: Vec<SocketAddr> = Vec::new();
        if let Some(a) = self.rt.get(fp).and_then(|r| r.announce_addr) {
            v.push(a);
        }
        if let Some(c) = self.contacts.get(fp) {
            for a in &c.last_addrs {
                if let Ok(sa) = a.parse::<SocketAddr>() {
                    if !v.contains(&sa) {
                        v.push(sa);
                    }
                }
            }
        }
        v
    }

    fn on_tick(&mut self) {
        let now = Instant::now();
        // Heartbeats / dead links.
        let mut dead = vec![];
        for (fp, c) in self.conns.iter_mut() {
            if c.last_rx.elapsed() > DEAD_AFTER {
                dead.push(fp.clone());
            } else if c.last_ping.elapsed() >= PING_EVERY {
                c.last_ping = now;
                let _ = c.ctrl.send(Frame::Ping { ts_ms: now_ms() });
            }
        }
        for fp in dead {
            self.drop_conn(&fp, "心跳超时");
        }
        // Dial peers that should be online.
        let mut to_dial = vec![];
        let fps: HashSet<String> = self.rt.keys().chain(self.contacts.contacts.keys()).cloned().collect();
        for fp in fps {
            if self.conns.contains_key(&fp) || self.contacts.trust(&fp) == Trust::Blocked {
                continue;
            }
            let trusted = self.contacts.trust(&fp) == Trust::Trusted;
            let r = self.rt.entry(fp.clone()).or_default();
            if r.dialing {
                continue;
            }
            let recently_seen = r.last_announce.map(|t| t.elapsed() < Duration::from_secs(60)).unwrap_or(false);
            let has_outbox = self.outbox.iter().any(|o| o.fp == fp);
            if !(trusted || recently_seen || has_outbox) {
                continue;
            }
            if r.next_attempt.map(|t| t > now).unwrap_or(false) {
                continue;
            }
            if r.next_attempt.is_none() && !recently_seen {
                // Remembered peer without pending attempt: schedule with backoff.
                r.next_attempt = Some(now + backoff(r.failures));
                continue;
            }
            to_dial.push(fp);
        }
        for fp in to_dial {
            let addrs = self.candidates(&fp);
            let r = self.rt.entry(fp.clone()).or_default();
            if addrs.is_empty() {
                r.next_attempt = Some(now + Duration::from_secs(10));
                continue;
            }
            r.dialing = true;
            r.next_attempt = None;
            self.spawn_dial(DialKey::Peer(fp.clone()), addrs);
            self.emit_peer(&fp);
        }
        // Manual addresses.
        let manual = self.contacts.manual.clone();
        for addr in manual {
            let m = self.manual_rt.entry(addr.clone()).or_default();
            if m.dialing || m.next_attempt.map(|t| t > now).unwrap_or(false) {
                continue;
            }
            if let Some(fp) = &m.fp {
                if self.conns.contains_key(fp) {
                    m.next_attempt = Some(now + Duration::from_secs(5));
                    continue;
                }
            }
            match resolve_addr(&addr, self.settings.listen_port) {
                Some(sa) => {
                    m.dialing = true;
                    self.spawn_dial(DialKey::Manual(addr.clone()), vec![sa]);
                }
                None => {
                    m.last_error = "无法解析地址".into();
                    m.next_attempt = Some(now + Duration::from_secs(30));
                }
            }
        }
        let _ = self.history.flush(false);
    }

    fn on_seen(&mut self, s: Seen) {
        let fp = s.fp.to_string();
        if fp == self.me {
            return;
        }
        let online = self.conns.contains_key(&fp);
        let r = self.rt.entry(fp.clone()).or_default();
        let first = r.last_announce.is_none();
        r.announce_addr = Some(s.dial);
        r.last_announce = Some(Instant::now());
        r.announce_name = s.name.clone();
        r.sources.insert("discovery".into());
        if !online && !r.dialing {
            // Pull the next attempt forward (bounded so an unreachable peer is not hammered).
            let soon = Instant::now() + if r.failures == 0 { Duration::ZERO } else { Duration::from_secs(3).min(backoff(r.failures)) };
            if r.next_attempt.map(|t| t > soon).unwrap_or(true) {
                r.next_attempt = Some(soon);
            }
        }
        if first {
            tracing::info!(%fp, name = %s.name, addr = %s.dial, "discovered peer");
        }
        if first && !online {
            self.emit_peer(&fp);
        }
    }

    // ---------------------------------------------------------------- frames

    fn send_ctrl(&self, fp: &str, f: Frame) -> bool {
        self.conns.get(fp).map(|c| c.ctrl.send(f).is_ok()).unwrap_or(false)
    }

    fn peer_name(&self, fp: &str) -> String {
        if fp == self.me {
            return self.settings.display_name.clone();
        }
        self.contacts
            .get(fp)
            .map(|c| c.name.clone())
            .filter(|n| !n.is_empty())
            .or_else(|| self.rt.get(fp).map(|r| r.announce_name.clone()).filter(|n| !n.is_empty()))
            .unwrap_or_else(|| fp.chars().take(9).collect())
    }

    fn on_frame(&mut self, fp: &str, frame: Frame) {
        match frame {
            Frame::Ping { ts_ms } => {
                self.send_ctrl(fp, Frame::Pong { ts_ms });
            }
            Frame::Pong { ts_ms } => {
                if let Some(c) = self.conns.get_mut(fp) {
                    let rtt = (now_ms() - ts_ms).clamp(0, 60_000) as u32;
                    let changed = c.rtt_ms.map(|o| o.abs_diff(rtt) > 20).unwrap_or(true);
                    c.rtt_ms = Some(rtt);
                    if changed {
                        self.emit_peer(fp);
                    }
                }
            }
            Frame::Name { name } => {
                let n = crate::util::sanitize_display_name(&name);
                self.contacts.entry(fp).name = n;
                let _ = self.contacts.save();
                self.emit_peer(fp);
                self.emit_conv(&dm_conv(fp));
            }
            Frame::Msg { id, payload } => {
                let dup = self.seen_set.contains(&id) || self.history.contains(&id.to_string());
                if !dup {
                    self.remember_id(id);
                    self.on_payload(fp, id, payload);
                }
                self.send_ctrl(fp, Frame::Ack { id });
            }
            Frame::Ack { id } => self.on_ack(fp, id),
            Frame::PinPair { pin } => {
                let ok = match &mut self.pending_pin {
                    Some((p, t, _)) if t.elapsed() < Duration::from_secs(300) && *p == pin => true,
                    Some((_, _, tries)) => {
                        *tries += 1;
                        if *tries >= 5 {
                            self.pending_pin = None;
                        }
                        false
                    }
                    None => false,
                };
                self.send_ctrl(fp, Frame::PinResult { ok });
                if ok {
                    self.pending_pin = None;
                    self.set_trust(fp, Trust::Trusted);
                    self.notice("info", format!("已通过 PIN 与 {} 配对并信任", self.peer_name(fp)));
                }
            }
            Frame::PinResult { ok } => {
                if ok {
                    self.set_trust(fp, Trust::Trusted);
                }
                if let Some(w) = self.pin_waiters.remove(fp) {
                    let _ = w.send(Ok(json!({ "ok": ok })));
                }
            }
            Frame::Bye => self.drop_conn(fp, "peer closed"),
            f @ (Frame::FileOffer { .. }
            | Frame::FileAccept { .. }
            | Frame::FileReject { .. }
            | Frame::FileChunk { .. }
            | Frame::FileProgress { .. }
            | Frame::FileEnd { .. }
            | Frame::FileComplete { .. }
            | Frame::FileCancel { .. }) => self.on_file_frame(fp, f),
        }
    }

    fn remember_id(&mut self, id: Uuid) {
        self.seen_set.insert(id);
        self.seen_ids.push_back(id);
        if self.seen_ids.len() > 20_000 {
            if let Some(old) = self.seen_ids.pop_front() {
                self.seen_set.remove(&old);
            }
        }
    }

    fn add_message(&mut self, m: Message, notify: bool) {
        let conv = m.conversation.clone();
        if m.direction == Direction::In && m.kind != MsgKind::System {
            if let Ok(u) = Uuid::parse_str(&m.id) {
                self.unread.entry(conv.clone()).or_default().push(u);
            }
            if notify && !self.is_muted(&conv) {
                let title = if conv.starts_with("group:") {
                    format!("{} · {}", self.conv_title(&conv), m.sender_name)
                } else {
                    m.sender_name.clone()
                };
                let body = match m.kind {
                    MsgKind::File => format!("[文件] {}", m.file.as_ref().map(|f| f.name.clone()).unwrap_or_default()),
                    _ => m.body.chars().take(200).collect(),
                };
                self.emit(Event::Notify { conversation: conv.clone(), title, body });
            }
        }
        self.history.push(m.clone());
        self.emit(Event::MessageAdded { message: m });
        self.emit_conv(&conv);
    }

    fn system_msg(&mut self, conv: &str, text: String) {
        let m = Message {
            id: Uuid::new_v4().to_string(),
            conversation: conv.to_string(),
            direction: Direction::In,
            sender_fp: String::new(),
            sender_name: String::new(),
            kind: MsgKind::System,
            body: text,
            ts_ms: now_ms(),
            status: MsgStatus::Received,
            file: None,
        };
        self.add_message(m, false);
    }

    fn update_msg(&mut self, id: &str, f: impl FnOnce(&mut Message)) {
        if let Some(m) = self.history.get_mut(id) {
            f(m);
            let m = m.clone();
            let is_last = self.history.last_in(&m.conversation).map(|l| l.id == m.id).unwrap_or(false);
            let conv = m.conversation.clone();
            self.emit(Event::MessageUpdated { message: m });
            if is_last {
                self.emit_conv(&conv); // the chat-list row shows the last message's ticks
            }
        }
    }

    fn on_payload(&mut self, fp: &str, id: Uuid, payload: Payload) {
        let name = self.peer_name(fp);
        match payload {
            Payload::Text { body, ts_ms } => {
                let m = Message {
                    id: id.to_string(),
                    conversation: dm_conv(fp),
                    direction: Direction::In,
                    sender_fp: fp.to_string(),
                    sender_name: name,
                    kind: MsgKind::Text,
                    body: body.chars().take(64 * 1024).collect(),
                    ts_ms: if ts_ms > 0 { ts_ms } else { now_ms() },
                    status: MsgStatus::Received,
                    file: None,
                };
                self.add_message(m, true);
            }
            Payload::GroupText { group_id, key_epoch, sealed, ts_ms } => {
                let Some(g) = self.groups.get(&group_id).cloned() else {
                    tracing::debug!(%group_id, "group text for unknown group");
                    return;
                };
                if !g.is_member(fp) {
                    tracing::warn!(%fp, %group_id, "group text from non-member dropped");
                    return;
                }
                let body = match g.group_key().and_then(|k| open_at_rest(&k, &sealed)) {
                    Ok(b) => String::from_utf8_lossy(&b).to_string(),
                    Err(_) => {
                        self.system_msg(&group_conv(&group_id), format!("收到来自 {name} 的消息但无法解密 (密钥版本 {key_epoch} ≠ {})", g.key_epoch));
                        return;
                    }
                };
                let m = Message {
                    id: id.to_string(),
                    conversation: group_conv(&group_id),
                    direction: Direction::In,
                    sender_fp: fp.to_string(),
                    sender_name: name,
                    kind: MsgKind::Text,
                    body,
                    ts_ms: if ts_ms > 0 { ts_ms } else { now_ms() },
                    status: MsgStatus::Received,
                    file: None,
                };
                self.add_message(m, true);
            }
            Payload::GroupInvite { group_id, name: gname, creator_fp, members, group_key, created_ms, key_epoch, sig } => {
                if creator_fp != fp || !self.verify_sig(fp, &invite_sign_bytes(&group_id, &gname, &creator_fp, &members, &group_key, created_ms), &sig) {
                    tracing::warn!(%fp, "invalid group invite signature");
                    return;
                }
                if self.contacts.trust(fp) != Trust::Trusted {
                    self.notice("warn", format!("忽略来自未信任联系人 {name} 的群组邀请「{gname}」（请先信任对方）"));
                    return;
                }
                if !members.iter().any(|m| *m == self.me) {
                    return;
                }
                let existed = self.groups.get(&group_id).is_some();
                let info = GroupInfo {
                    id: group_id,
                    name: crate::util::sanitize_display_name(&gname),
                    creator_fp,
                    members,
                    group_key_hex: hex::encode(group_key),
                    created_ms,
                    join_code: String::new(),
                    key_epoch,
                };
                let _ = self.groups.upsert(info);
                self.emit_group(&group_id);
                if !existed {
                    self.system_msg(&group_conv(&group_id), format!("{name} 邀请你加入了群组「{gname}」"));
                }
            }
            Payload::GroupRekey { group_id, members, group_key, key_epoch, removed_fp, sig } => {
                let Some(g) = self.groups.get(&group_id).cloned() else { return };
                if g.creator_fp != fp || !self.verify_sig(fp, &rekey_sign_bytes(&group_id, &members, &group_key, key_epoch, &removed_fp), &sig) {
                    tracing::warn!(%fp, "invalid group rekey");
                    return;
                }
                if key_epoch < g.key_epoch {
                    return;
                }
                if !members.iter().any(|m| *m == self.me) {
                    let _ = self.groups.remove(&group_id);
                    self.emit(Event::GroupRemoved { id: group_id.to_string() });
                    self.notice("info", format!("你已被移出群组「{}」", g.name));
                    return;
                }
                if let Some(gm) = self.groups.get_mut(&group_id) {
                    gm.members = members;
                    gm.group_key_hex = hex::encode(group_key);
                    gm.key_epoch = key_epoch;
                }
                let _ = self.groups.save();
                self.emit_group(&group_id);
                if !removed_fp.is_empty() {
                    let rn = self.peer_name(&removed_fp);
                    self.system_msg(&group_conv(&group_id), format!("{rn} 已被移出群组，群密钥已更新"));
                }
            }
            Payload::GroupLeave { group_id } => {
                let Some(g) = self.groups.get(&group_id).cloned() else { return };
                if !g.is_member(fp) {
                    return;
                }
                if g.creator_fp == self.me {
                    // Creator rotates the key for remaining members.
                    let _ = self.remove_member(group_id, fp);
                } else if let Some(gm) = self.groups.get_mut(&group_id) {
                    gm.members.retain(|m| m != fp);
                    let _ = self.groups.save();
                    self.emit_group(&group_id);
                }
                self.system_msg(&group_conv(&group_id), format!("{name} 退出了群组"));
            }
            Payload::Read { ids } => {
                for i in ids {
                    let s = i.to_string();
                    let is_mine = self.history.get(&s).map(|m| m.direction == Direction::Out && m.sender_fp == self.me).unwrap_or(false);
                    if is_mine {
                        self.update_msg(&s, |m| m.status = MsgStatus::Read);
                    }
                }
            }
        }
    }

    fn verify_sig(&self, fp: &str, msg: &[u8], sig: &[u8]) -> bool {
        let Some(c) = self.contacts.get(fp) else { return false };
        let Ok(pk) = hex::decode(&c.ed25519_pk) else { return false };
        let (Ok(pk), true) = (<[u8; 32]>::try_from(pk.as_slice()), sig.len() == 64) else { return false };
        let Ok(vk) = VerifyingKey::from_bytes(&pk) else { return false };
        let mut s = [0u8; 64];
        s.copy_from_slice(sig);
        Identity::verify(&vk, msg, &s)
    }

    // ---------------------------------------------------------------- outbox

    fn send_reliable(&mut self, fp: &str, payload: Payload, id: Uuid, msg_ref: Option<String>) -> bool {
        let online = self.send_ctrl(fp, Frame::Msg { id, payload: payload.clone() });
        self.outbox.push(Outgoing { fp: fp.to_string(), id, payload, msg_ref });
        online
    }

    fn flush_outbox(&mut self, fp: &str) {
        let items: Vec<(Uuid, Payload, Option<String>)> =
            self.outbox.iter().filter(|o| o.fp == fp).map(|o| (o.id, o.payload.clone(), o.msg_ref.clone())).collect();
        for (id, p, r) in items {
            if self.send_ctrl(fp, Frame::Msg { id, payload: p }) {
                if let Some(r) = r {
                    let is_queued = self.history.get(&r).map(|m| m.status == MsgStatus::Queued).unwrap_or(false);
                    if is_queued {
                        self.update_msg(&r, |m| m.status = MsgStatus::Sent);
                    }
                }
            }
        }
    }

    fn on_ack(&mut self, fp: &str, id: Uuid) {
        let mut refs = vec![];
        self.outbox.retain(|o| {
            if o.fp == fp && o.id == id {
                refs.push(o.msg_ref.clone());
                false
            } else {
                true
            }
        });
        for r in refs.into_iter().flatten() {
            let remaining = self.outbox.iter().any(|o| o.msg_ref.as_deref() == Some(&r));
            if !remaining {
                let upd = self.history.get(&r).map(|m| matches!(m.status, MsgStatus::Queued | MsgStatus::Sent)).unwrap_or(false);
                if upd {
                    self.update_msg(&r, |m| m.status = MsgStatus::Delivered);
                }
            }
        }
    }

    /// After restart: re-queue our undelivered text messages (last 7 days).
    fn rebuild_outbox(&mut self) {
        let cutoff = now_ms() - 7 * 24 * 3600 * 1000;
        let pending: Vec<Message> = self
            .history
            .messages
            .iter()
            .filter(|m| m.direction == Direction::Out && m.kind == MsgKind::Text && matches!(m.status, MsgStatus::Queued | MsgStatus::Sent) && m.ts_ms > cutoff)
            .cloned()
            .collect();
        for m in pending {
            let Ok(id) = Uuid::parse_str(&m.id) else { continue };
            if let Some(fp) = m.conversation.strip_prefix("dm:") {
                self.outbox.push(Outgoing { fp: fp.to_string(), id, payload: Payload::Text { body: m.body.clone(), ts_ms: m.ts_ms }, msg_ref: Some(m.id.clone()) });
            } else if let Some(gid) = m.conversation.strip_prefix("group:").and_then(|g| Uuid::parse_str(g).ok()) {
                if let Some(g) = self.groups.get(&gid).cloned() {
                    if let Ok(k) = g.group_key() {
                        if let Ok(sealed) = seal_at_rest(&k, m.body.as_bytes()) {
                            for mem in g.members.iter().filter(|x| **x != self.me).cloned().collect::<Vec<_>>().iter() {
                                self.outbox.push(Outgoing {
                                    fp: mem.clone(),
                                    id,
                                    payload: Payload::GroupText { group_id: gid, key_epoch: g.key_epoch, sealed: sealed.clone(), ts_ms: m.ts_ms },
                                    msg_ref: Some(m.id.clone()),
                                });
                            }
                        }
                    }
                }
            }
        }
        if !self.outbox.is_empty() {
            tracing::info!(n = self.outbox.len(), "re-queued undelivered messages");
        }
    }

    // ---------------------------------------------------------------- trust / groups helpers

    fn set_trust(&mut self, fp: &str, t: Trust) {
        self.contacts.entry(fp).trust = t;
        let _ = self.contacts.save();
        if t == Trust::Blocked {
            self.drop_conn(fp, "已屏蔽");
            self.outbox.retain(|o| o.fp != fp);
        }
        self.emit_peer(fp);
    }

    fn group_payload_invite(&self, g: &GroupInfo) -> Result<Payload> {
        let key = g.group_key()?;
        let sig = self.shared.identity.sign(&invite_sign_bytes(&g.id, &g.name, &g.creator_fp, &g.members, &key, g.created_ms));
        Ok(Payload::GroupInvite {
            group_id: g.id,
            name: g.name.clone(),
            creator_fp: g.creator_fp.clone(),
            members: g.members.clone(),
            group_key: key,
            created_ms: g.created_ms,
            key_epoch: g.key_epoch,
            sig: sig.to_vec(),
        })
    }

    fn group_payload_rekey(&self, g: &GroupInfo, removed: &str) -> Result<Payload> {
        let key = g.group_key()?;
        let sig = self.shared.identity.sign(&rekey_sign_bytes(&g.id, &g.members, &key, g.key_epoch, removed));
        Ok(Payload::GroupRekey { group_id: g.id, members: g.members.clone(), group_key: key, key_epoch: g.key_epoch, removed_fp: removed.to_string(), sig: sig.to_vec() })
    }

    fn remove_member(&mut self, gid: Uuid, fp: &str) -> Result<()> {
        let g = self.groups.remove_and_rekey(&gid, &self.me.clone(), fp)?;
        let p = self.group_payload_rekey(&g, fp)?;
        // Tell the removed member too (so they drop the group), then the rest.
        self.send_reliable(fp, p.clone(), Uuid::new_v4(), None);
        for m in g.members.iter().filter(|m| **m != self.me).cloned().collect::<Vec<_>>().iter() {
            self.send_reliable(m, p.clone(), Uuid::new_v4(), None);
        }
        self.emit_group(&gid);
        Ok(())
    }

    fn is_muted(&self, conv: &str) -> bool {
        if let Some(fp) = conv.strip_prefix("dm:") {
            self.contacts.get(fp).map(|c| c.muted).unwrap_or(false)
        } else if let Some(g) = conv.strip_prefix("group:") {
            self.contacts.muted_groups.iter().any(|x| x == g)
        } else {
            false
        }
    }

    // ---------------------------------------------------------------- views

    pub(crate) fn peer_view(&self, fp: &str) -> Peer {
        let c = self.contacts.get(fp);
        let conn = self.conns.get(fp);
        let r = self.rt.get(fp);
        let state = if conn.is_some() {
            ConnState::Connected
        } else if r.map(|r| r.dialing).unwrap_or(false) {
            ConnState::Connecting
        } else {
            ConnState::Offline
        };
        Peer {
            fingerprint: fp.to_string(),
            name: self.peer_name(fp),
            trust: c.map(|c| c.trust).unwrap_or_default(),
            online: conn.is_some(),
            state,
            addr: conn.map(|c| c.addr.to_string()).or_else(|| r.and_then(|r| r.announce_addr.map(|a| a.to_string()))),
            direction: conn.map(|c| if c.initiator { "outbound".into() } else { "inbound".into() }),
            last_seen_ms: if conn.is_some() { now_ms() } else { c.map(|c| c.last_seen_ms).unwrap_or(0) },
            muted: c.map(|c| c.muted).unwrap_or(false),
            unread: self.unread.get(&dm_conv(fp)).map(|v| v.len() as u32).unwrap_or(0),
            rtt_ms: conn.and_then(|c| c.rtt_ms),
            app_version: conn.map(|c| c.app_version.clone()).unwrap_or_default(),
        }
    }

    fn emit_peer(&self, fp: &str) {
        self.emit(Event::PeerUpdated { peer: self.peer_view(fp) });
        self.emit(Event::ConversationUpdated { conversation: self.conv_view(&dm_conv(fp)) });
        // Group member online dots.
        for g in self.groups.list() {
            if g.is_member(fp) {
                self.emit_group(&g.id);
            }
        }
    }

    fn visible_peers(&self) -> Vec<String> {
        let mut v: Vec<String> = self.contacts.contacts.keys().cloned().collect();
        for (fp, r) in &self.rt {
            if !v.contains(fp) && r.last_announce.map(|t| t.elapsed() < Duration::from_secs(60)).unwrap_or(false) {
                v.push(fp.clone());
            }
        }
        v
    }

    fn group_view(&self, g: &GroupInfo) -> Group {
        let conv = group_conv(&g.id);
        Group {
            id: g.id.to_string(),
            name: g.name.clone(),
            creator_fp: g.creator_fp.clone(),
            is_creator: g.creator_fp == self.me,
            members: g
                .members
                .iter()
                .map(|m| GroupMember { fingerprint: m.clone(), name: self.peer_name(m), online: *m == self.me || self.conns.contains_key(m) })
                .collect(),
            key_epoch: g.key_epoch,
            muted: self.is_muted(&conv),
            unread: self.unread.get(&conv).map(|v| v.len() as u32).unwrap_or(0),
        }
    }

    fn emit_group(&self, id: &Uuid) {
        if let Some(g) = self.groups.get(id) {
            self.emit(Event::GroupUpdated { group: self.group_view(g) });
            self.emit(Event::ConversationUpdated { conversation: self.conv_view(&group_conv(id)) });
        }
    }

    fn conv_title(&self, conv: &str) -> String {
        if let Some(fp) = conv.strip_prefix("dm:") {
            self.peer_name(fp)
        } else if let Some(g) = conv.strip_prefix("group:").and_then(|g| Uuid::parse_str(g).ok()).and_then(|g| self.groups.get(&g)) {
            g.name.clone()
        } else {
            conv.to_string()
        }
    }

    fn conv_view(&self, conv: &str) -> Conversation {
        let last = self.history.last_in(conv);
        let is_group = conv.starts_with("group:");
        Conversation {
            id: conv.to_string(),
            kind: if is_group { "group".into() } else { "dm".into() },
            title: self.conv_title(conv),
            last_body: last
                .map(|m| match m.kind {
                    MsgKind::File => format!("[文件] {}", m.file.as_ref().map(|f| f.name.as_str()).unwrap_or("")),
                    _ => m.body.chars().take(80).collect(),
                })
                .unwrap_or_default(),
            last_ts_ms: last.map(|m| m.ts_ms).unwrap_or(0),
            unread: self.unread.get(conv).map(|v| v.len() as u32).unwrap_or(0),
            muted: self.is_muted(conv),
            online: if let Some(fp) = conv.strip_prefix("dm:") { self.conns.contains_key(fp) } else { true },
            last_out: last.map(|m| m.direction == Direction::Out).unwrap_or(false),
            last_status: last.map(|m| m.status),
            last_sender: last.map(|m| m.sender_name.clone()).unwrap_or_default(),
            last_kind: last.map(|m| m.kind),
            last_file: last.and_then(|m| m.file.as_ref()).map(|f| f.name.clone()).unwrap_or_default(),
        }
    }

    fn emit_conv(&self, conv: &str) {
        self.emit(Event::ConversationUpdated { conversation: self.conv_view(conv) });
    }

    fn me_view(&self) -> Me {
        Me {
            fingerprint: self.me.clone(),
            name: self.settings.display_name.clone(),
            version: APP_VERSION.to_string(),
            listen_port: self.shared.listen_port.load(Ordering::Relaxed),
            discovery_port: self.settings.discovery_port,
            ipc_endpoint: self.ipc_endpoint.clone(),
            data_dir: self.paths.base.display().to_string(),
            log_dir: self.paths.logs.display().to_string(),
            download_dir: self.settings.download_dir(&self.paths).display().to_string(),
            first_run_done: self.settings.first_run_done,
        }
    }

    fn snapshot(&self) -> Snapshot {
        let mut peers: Vec<Peer> = self.visible_peers().iter().map(|fp| self.peer_view(fp)).collect();
        peers.sort_by(|a, b| b.online.cmp(&a.online).then(a.name.cmp(&b.name)));
        let groups: Vec<Group> = self.groups.list().iter().map(|g| self.group_view(g)).collect();
        let mut convs: Vec<String> = self.visible_peers().iter().map(|f| dm_conv(f)).collect();
        convs.extend(self.groups.list().iter().map(|g| group_conv(&g.id)));
        let mut conversations: Vec<Conversation> = convs.iter().map(|c| self.conv_view(c)).collect();
        conversations.sort_by(|a, b| b.last_ts_ms.cmp(&a.last_ts_ms));
        let mut transfers: Vec<Transfer> = self.transfers.values().map(|t| t.view()).collect();
        transfers.sort_by(|a, b| b.started_ms.cmp(&a.started_ms));
        transfers.truncate(300);
        Snapshot { me: self.me_view(), peers, groups, conversations, transfers, settings: self.settings.clone() }
    }

    fn diag_peers(&self) -> Vec<PeerDiag> {
        let now = Instant::now();
        let mut fps: Vec<String> = self.visible_peers();
        for fp in self.rt.keys() {
            if !fps.contains(fp) {
                fps.push(fp.clone());
            }
        }
        fps.iter()
            .map(|fp| {
                let p = self.peer_view(fp);
                let r = self.rt.get(fp);
                let c = self.conns.get(fp);
                let mut sources: Vec<String> = r.map(|r| r.sources.iter().cloned().collect()).unwrap_or_default();
                sources.sort();
                PeerDiag {
                    fingerprint: fp.clone(),
                    name: p.name,
                    state: p.state,
                    addr: p.addr,
                    direction: p.direction,
                    connected_for_ms: c.map(|c| c.since.elapsed().as_millis() as i64),
                    last_rx_ms_ago: c.map(|c| c.last_rx.elapsed().as_millis() as i64),
                    rtt_ms: p.rtt_ms,
                    dial_failures: r.map(|r| r.failures).unwrap_or(0),
                    last_error: r.map(|r| r.last_error.clone()).unwrap_or_default(),
                    next_retry_in_ms: r.and_then(|r| r.next_attempt).map(|t| t.saturating_duration_since(now).as_millis() as i64),
                    candidates: self.candidates(fp).iter().map(|a| a.to_string()).collect(),
                    sources,
                }
            })
            .collect()
    }

    // ---------------------------------------------------------------- RPC

    fn rpc(&mut self, method: &str, params: Value, reply: oneshot::Sender<RpcResult>) {
        // Async-completing methods take ownership of `reply`.
        match method {
            "get_diagnostics" => {
                let disco = self.disco.status.lock().unwrap().clone();
                let mut d = Diagnostics {
                    version: APP_VERSION.into(),
                    os: format!("{} {}", std::env::consts::OS, std::env::consts::ARCH),
                    tcp_listen: format!("0.0.0.0:{}", self.shared.listen_port.load(Ordering::Relaxed)),
                    tcp_error: self.tcp_error.clone(),
                    discovery: disco,
                    firewall: Default::default(),
                    peers: self.diag_peers(),
                    manual_peers: self.contacts.manual.clone(),
                    log_dir: self.paths.logs.display().to_string(),
                    config_dir: self.paths.root.display().to_string(),
                    ipc_endpoint: self.ipc_endpoint.clone(),
                };
                if !self.settings.discovery_enabled {
                    d.discovery.enabled = false;
                }
                if d.discovery.interfaces.is_empty() {
                    d.discovery.interfaces = crate::netif::list(&self.settings.include_interfaces, &self.settings.exclude_interfaces);
                }
                let skip_fw = params.get("skip_firewall").and_then(|v| v.as_bool()).unwrap_or(false);
                tokio::spawn(async move {
                    if !skip_fw {
                        d.firewall = tokio::task::spawn_blocking(crate::firewall::status).await.unwrap_or_default();
                    }
                    let _ = reply.send(serde_json::to_value(d).map_err(|e| RpcError::new(-32000, e.to_string())));
                });
                return;
            }
            "firewall_status" => {
                tokio::spawn(async move {
                    let s = tokio::task::spawn_blocking(crate::firewall::status).await.unwrap_or_default();
                    let _ = reply.send(Ok(serde_json::to_value(s).unwrap_or_default()));
                });
                return;
            }
            "firewall_setup" => {
                let public = params.get("include_public").and_then(|v| v.as_bool()).unwrap_or(false);
                let events = self.events.clone();
                tokio::spawn(async move {
                    let r = tokio::task::spawn_blocking(move || crate::firewall::setup_elevated(public)).await;
                    let res = match r {
                        Ok(Ok(())) => {
                            let _ = events.send(Event::Notice { level: "info".into(), text: "防火墙规则已添加".into() });
                            Ok(json!({"ok": true}))
                        }
                        Ok(Err(e)) => Err(RpcError::new(-32010, format!("{e:#}"))),
                        Err(e) => Err(RpcError::new(-32010, e.to_string())),
                    };
                    let _ = reply.send(res);
                });
                return;
            }
            "pin_pair" => {
                #[derive(Deserialize)]
                struct P {
                    fingerprint: String,
                    pin: String,
                }
                let p: P = match serde_json::from_value(params) {
                    Ok(p) => p,
                    Err(e) => {
                        let _ = reply.send(Err(RpcError::invalid(e.to_string())));
                        return;
                    }
                };
                if !self.send_ctrl(&p.fingerprint, Frame::PinPair { pin: p.pin.trim().to_string() }) {
                    let _ = reply.send(Err(RpcError::new(-32004, "对方不在线")));
                    return;
                }
                self.pin_waiters.insert(p.fingerprint, reply);
                return;
            }
            _ => {}
        }
        let r = self.rpc_sync(method, params);
        if let Err(e) = &r {
            tracing::debug!(method, error = %e.message, "rpc error");
        }
        let _ = reply.send(r);
    }

    fn rpc_sync(&mut self, method: &str, params: Value) -> RpcResult {
        fn p<T: for<'de> Deserialize<'de>>(v: Value) -> std::result::Result<T, RpcError> {
            serde_json::from_value(v).map_err(|e| RpcError::invalid(e.to_string()))
        }
        fn ok<T: serde::Serialize>(v: T) -> RpcResult {
            serde_json::to_value(v).map_err(|e| RpcError::new(-32000, e.to_string()))
        }
        #[derive(Deserialize)]
        struct ConvP {
            conversation: String,
        }
        match method {
            "hello" => ok(json!({ "daemon_version": APP_VERSION, "protocol": PROTO_VERSION, "fingerprint": self.me, "ipc_version": 1 })),
            "get_snapshot" => ok(self.snapshot()),
            "get_messages" => {
                #[derive(Deserialize)]
                struct P {
                    conversation: String,
                    before_ts: Option<i64>,
                    limit: Option<usize>,
                }
                let a: P = p(params)?;
                ok(self.history.conversation(&a.conversation, a.before_ts, a.limit.unwrap_or(200).min(2000)))
            }
            "send_text" => {
                #[derive(Deserialize)]
                struct P {
                    conversation: String,
                    body: String,
                }
                let a: P = p(params)?;
                let body = a.body.trim_end().to_string();
                if body.trim().is_empty() {
                    return Err(RpcError::invalid("消息不能为空"));
                }
                if body.len() > 60_000 {
                    return Err(RpcError::invalid("消息过长 (最多约 60KB)"));
                }
                let id = Uuid::new_v4();
                let ts = now_ms();
                let mut m = Message {
                    id: id.to_string(),
                    conversation: a.conversation.clone(),
                    direction: Direction::Out,
                    sender_fp: self.me.clone(),
                    sender_name: self.settings.display_name.clone(),
                    kind: MsgKind::Text,
                    body: body.clone(),
                    ts_ms: ts,
                    status: MsgStatus::Queued,
                    file: None,
                };
                if let Some(fp) = a.conversation.strip_prefix("dm:") {
                    let fp = fp.to_string();
                    if fp == self.me {
                        return Err(RpcError::invalid("不能给自己发消息"));
                    }
                    if self.contacts.trust(&fp) == Trust::Blocked {
                        return Err(RpcError::invalid("该联系人已被屏蔽"));
                    }
                    if self.contacts.get(&fp).is_none() && !self.rt.contains_key(&fp) {
                        return Err(RpcError::invalid("未知联系人"));
                    }
                    let online = self.send_reliable(&fp, Payload::Text { body, ts_ms: ts }, id, Some(m.id.clone()));
                    if online {
                        m.status = MsgStatus::Sent;
                    }
                } else if let Some(gid) = a.conversation.strip_prefix("group:").and_then(|g| Uuid::parse_str(g).ok()) {
                    let g = self.groups.get(&gid).cloned().ok_or_else(|| RpcError::invalid("群组不存在"))?;
                    let sealed = seal_at_rest(&g.group_key()?, body.as_bytes())?;
                    let mut any = false;
                    for mem in g.members.iter().filter(|x| **x != self.me).cloned().collect::<Vec<_>>().iter() {
                        any |= self.send_reliable(mem, Payload::GroupText { group_id: gid, key_epoch: g.key_epoch, sealed: sealed.clone(), ts_ms: ts }, id, Some(m.id.clone()));
                    }
                    if any {
                        m.status = MsgStatus::Sent;
                    }
                    if g.members.len() <= 1 {
                        m.status = MsgStatus::Delivered;
                    }
                } else {
                    return Err(RpcError::invalid("conversation 必须是 dm:<指纹> 或 group:<id>"));
                }
                self.remember_id(id);
                self.add_message(m.clone(), false);
                ok(m)
            }
            "send_file" => {
                #[derive(Deserialize)]
                struct P {
                    conversation: String,
                    path: String,
                }
                let a: P = p(params)?;
                let t = self.send_file(&a.conversation, PathBuf::from(a.path), None)?;
                ok(t)
            }
            "cancel_transfer" | "retry_transfer" | "accept_transfer" | "reject_transfer" => {
                #[derive(Deserialize)]
                struct P {
                    id: String,
                }
                let a: P = p(params)?;
                let tid = Uuid::parse_str(&a.id).map_err(|_| RpcError::invalid("bad transfer id"))?;
                match method {
                    "cancel_transfer" => self.cancel_transfer(tid, "已取消")?,
                    "retry_transfer" => return ok(self.retry_transfer(tid)?),
                    "accept_transfer" => self.accept_transfer(tid)?,
                    _ => self.reject_transfer(tid, "对方拒绝接收")?,
                }
                ok(json!({"ok": true}))
            }
            "add_peer" => {
                #[derive(Deserialize)]
                struct P {
                    address: String,
                }
                let a: P = p(params)?;
                let addr = a.address.trim().to_string();
                if resolve_addr(&addr, self.settings.listen_port).is_none() {
                    return Err(RpcError::invalid("地址格式应为 IP 或 IP:端口，例如 192.168.1.23 或 192.168.1.23:41235"));
                }
                if !self.contacts.manual.contains(&addr) {
                    self.contacts.manual.push(addr.clone());
                    self.contacts.save()?;
                }
                let m = self.manual_rt.entry(addr.clone()).or_default();
                m.next_attempt = None;
                m.failures = 0;
                ok(json!({"ok": true, "address": addr}))
            }
            "remove_manual_peer" => {
                #[derive(Deserialize)]
                struct P {
                    address: String,
                }
                let a: P = p(params)?;
                self.contacts.manual.retain(|x| *x != a.address);
                self.manual_rt.remove(&a.address);
                self.contacts.save()?;
                ok(json!({"ok": true}))
            }
            "set_trust" => {
                #[derive(Deserialize)]
                struct P {
                    fingerprint: String,
                    trust: Trust,
                }
                let a: P = p(params)?;
                self.set_trust(&a.fingerprint, a.trust);
                ok(self.peer_view(&a.fingerprint))
            }
            "forget_peer" => {
                #[derive(Deserialize)]
                struct P {
                    fingerprint: String,
                }
                let a: P = p(params)?;
                self.drop_conn(&a.fingerprint, "已删除联系人");
                self.contacts.contacts.remove(&a.fingerprint);
                self.contacts.save()?;
                self.rt.remove(&a.fingerprint);
                self.outbox.retain(|o| o.fp != a.fingerprint);
                self.emit(Event::PeerRemoved { fingerprint: a.fingerprint });
                ok(json!({"ok": true}))
            }
            "set_display_name" | "complete_first_run" => {
                #[derive(Deserialize)]
                struct P {
                    name: Option<String>,
                }
                let a: P = p(params)?;
                if let Some(n) = a.name {
                    let n = crate::util::sanitize_display_name(&n);
                    self.settings.display_name = n.clone();
                    *self.shared.name.lock().unwrap() = n.clone();
                    self.disco.info.lock().unwrap().name = n.clone();
                    for c in self.conns.values() {
                        let _ = c.ctrl.send(Frame::Name { name: n.clone() });
                    }
                }
                if method == "complete_first_run" {
                    self.settings.first_run_done = true;
                }
                self.settings.save(&self.paths)?;
                self.emit(Event::MeUpdated { me: self.me_view() });
                ok(self.me_view())
            }
            "set_muted" => {
                #[derive(Deserialize)]
                struct P {
                    conversation: String,
                    muted: bool,
                }
                let a: P = p(params)?;
                if let Some(fp) = a.conversation.strip_prefix("dm:") {
                    self.contacts.entry(fp).muted = a.muted;
                    let fp = fp.to_string();
                    self.contacts.save()?;
                    self.emit_peer(&fp);
                } else if let Some(g) = a.conversation.strip_prefix("group:") {
                    self.contacts.muted_groups.retain(|x| x != g);
                    if a.muted {
                        self.contacts.muted_groups.push(g.to_string());
                    }
                    self.contacts.save()?;
                    if let Ok(id) = Uuid::parse_str(g) {
                        self.emit_group(&id);
                    }
                }
                ok(json!({"ok": true}))
            }
            "mark_read" => {
                let a: ConvP = p(params)?;
                let ids = self.unread.remove(&a.conversation).unwrap_or_default();
                if let Some(fp) = a.conversation.strip_prefix("dm:") {
                    if !ids.is_empty() {
                        self.send_ctrl(fp, Frame::Msg { id: Uuid::new_v4(), payload: Payload::Read { ids } });
                    }
                    let fp = fp.to_string();
                    self.emit_peer(&fp);
                } else if let Some(id) = a.conversation.strip_prefix("group:").and_then(|g| Uuid::parse_str(g).ok()) {
                    self.emit_group(&id);
                }
                self.emit_conv(&a.conversation);
                ok(json!({"ok": true}))
            }
            "create_group" => {
                #[derive(Deserialize)]
                struct P {
                    name: String,
                    members: Vec<String>,
                }
                let a: P = p(params)?;
                let name = crate::util::sanitize_display_name(&a.name);
                for m in &a.members {
                    if self.contacts.trust(m) != Trust::Trusted {
                        return Err(RpcError::invalid(format!("成员 {} 尚未信任，请先信任后再建群", self.peer_name(m))));
                    }
                }
                let me = self.me.clone();
                let g = self.groups.create(&name, &me, a.members, &self.shared.identity)?;
                let pl = self.group_payload_invite(&g)?;
                for m in g.members.iter().filter(|m| **m != self.me).cloned().collect::<Vec<_>>().iter() {
                    self.send_reliable(m, pl.clone(), Uuid::new_v4(), None);
                }
                self.emit_group(&g.id);
                self.system_msg(&group_conv(&g.id), format!("你创建了群组「{name}」"));
                ok(self.group_view(&g))
            }
            "invite_to_group" => {
                #[derive(Deserialize)]
                struct P {
                    group_id: Uuid,
                    members: Vec<String>,
                }
                let a: P = p(params)?;
                let mut g = self.groups.get(&a.group_id).cloned().ok_or_else(|| RpcError::invalid("群组不存在"))?;
                if g.creator_fp != self.me {
                    return Err(RpcError::invalid("只有群主可以邀请成员"));
                }
                for m in &a.members {
                    if self.contacts.trust(m) != Trust::Trusted {
                        return Err(RpcError::invalid(format!("成员 {} 尚未信任", self.peer_name(m))));
                    }
                    if !g.members.contains(m) {
                        g.members.push(m.clone());
                    }
                }
                g.members.sort();
                g.key_epoch += 1;
                self.groups.upsert(g.clone())?;
                let inv = self.group_payload_invite(&g)?;
                let rk = self.group_payload_rekey(&g, "")?;
                for m in g.members.iter().filter(|m| **m != self.me).cloned().collect::<Vec<_>>().iter() {
                    let pl = if a.members.contains(m) { inv.clone() } else { rk.clone() };
                    self.send_reliable(m, pl, Uuid::new_v4(), None);
                }
                self.emit_group(&g.id);
                ok(self.group_view(&g))
            }
            "remove_group_member" => {
                #[derive(Deserialize)]
                struct P {
                    group_id: Uuid,
                    fingerprint: String,
                }
                let a: P = p(params)?;
                self.remove_member(a.group_id, &a.fingerprint)?;
                let n = self.peer_name(&a.fingerprint);
                self.system_msg(&group_conv(&a.group_id), format!("已将 {n} 移出群组并更新群密钥"));
                ok(json!({"ok": true}))
            }
            "leave_group" => {
                #[derive(Deserialize)]
                struct P {
                    group_id: Uuid,
                }
                let a: P = p(params)?;
                let g = self.groups.get(&a.group_id).cloned().ok_or_else(|| RpcError::invalid("群组不存在"))?;
                for m in g.members.iter().filter(|m| **m != self.me).cloned().collect::<Vec<_>>().iter() {
                    self.send_reliable(m, Payload::GroupLeave { group_id: a.group_id }, Uuid::new_v4(), None);
                }
                self.groups.remove(&a.group_id)?;
                self.emit(Event::GroupRemoved { id: a.group_id.to_string() });
                ok(json!({"ok": true}))
            }
            "search" => {
                #[derive(Deserialize)]
                struct P {
                    query: String,
                    limit: Option<usize>,
                }
                let a: P = p(params)?;
                ok(self.history.search(&a.query, a.limit.unwrap_or(100).min(1000)))
            }
            "backup" | "restore" => {
                #[derive(Deserialize)]
                struct P {
                    path: String,
                    password: String,
                }
                let a: P = p(params)?;
                if method == "backup" {
                    self.history.flush(true)?;
                    self.contacts.save()?;
                    self.settings.save(&self.paths)?;
                    crate::backup::export_backup(&self.paths, &a.password, std::path::Path::new(&a.path))?;
                    ok(json!({"ok": true, "path": a.path}))
                } else {
                    crate::backup::import_backup(&self.paths, &a.password, std::path::Path::new(&a.path))?;
                    self.notice("warn", "备份已恢复，需要重启后台服务才能生效");
                    ok(json!({"ok": true, "restart_required": true}))
                }
            }
            "pin_offer" => {
                let pin = format!("{:06}", rand::random::<u32>() % 1_000_000);
                self.pending_pin = Some((pin.clone(), Instant::now(), 0));
                ok(json!({"pin": pin, "expires_in_secs": 300}))
            }
            "update_settings" => {
                let patch = params.get("settings").cloned().unwrap_or(params);
                let before = self.settings.clone();
                let mut v = serde_json::to_value(&self.settings).map_err(|e| RpcError::new(-32000, e.to_string()))?;
                merge(&mut v, &patch);
                let mut s: Settings = p(v)?;
                s.display_name = crate::util::sanitize_display_name(&s.display_name);
                let restart = s.discovery_port != before.discovery_port
                    || s.listen_port != before.listen_port
                    || s.discovery_enabled != before.discovery_enabled
                    || s.multicast_enabled != before.multicast_enabled
                    || s.announce_targets != before.announce_targets
                    || s.include_interfaces != before.include_interfaces
                    || s.exclude_interfaces != before.exclude_interfaces;
                self.history.set_enabled(s.history_enabled);
                if s.display_name != before.display_name {
                    *self.shared.name.lock().unwrap() = s.display_name.clone();
                    self.disco.info.lock().unwrap().name = s.display_name.clone();
                    for c in self.conns.values() {
                        let _ = c.ctrl.send(Frame::Name { name: s.display_name.clone() });
                    }
                }
                self.settings = s;
                self.settings.save(&self.paths)?;
                self.emit(Event::SettingsUpdated { settings: self.settings.clone() });
                self.emit(Event::MeUpdated { me: self.me_view() });
                ok(json!({"ok": true, "restart_required": restart}))
            }
            "debug_drop_connection" => {
                #[derive(Deserialize)]
                struct P {
                    fingerprint: String,
                }
                let a: P = p(params)?;
                // Abrupt: abort reader + drop writer without Bye (simulates a network cut).
                if let Some(c) = self.conns.get(&a.fingerprint) {
                    let id = c.id;
                    c.reader.abort();
                    self.on_closed(&a.fingerprint, id, "debug drop");
                    ok(json!({"ok": true}))
                } else {
                    Err(RpcError::new(-32004, "not connected"))
                }
            }
            _ => Err(RpcError::new(-32601, format!("unknown method '{method}'"))),
        }
    }
}

pub(crate) fn resolve_addr(s: &str, default_port: u16) -> Option<SocketAddr> {
    let s = s.trim();
    if s.is_empty() {
        return None;
    }
    let with = if s.parse::<SocketAddr>().is_ok() || (s.contains(':') && !s.contains("::")) { s.to_string() } else { format!("{s}:{default_port}") };
    with.to_socket_addrs().ok()?.find(|a| a.is_ipv4())
}

