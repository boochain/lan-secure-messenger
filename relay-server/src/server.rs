//! Relay server core: auth, presence, forwarding, limits, status endpoint.
use crate::config::Config;
use crate::proto::{self, code, t, Auth, Frame, Hello, PeerId, ReadErr};
use anyhow::Result;
use rand::RngCore;
use serde::Serialize;
use std::collections::HashMap;
use std::net::{IpAddr, SocketAddr};
use std::sync::atomic::{AtomicU64, AtomicUsize, Ordering};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};
use tokio::io::{AsyncReadExt, AsyncWriteExt, BufReader, BufWriter};
use tokio::net::{TcpListener, TcpStream};
use tokio::sync::{mpsc, watch, Notify};
use tokio::task::JoinHandle;
use tracing::{debug, info, warn};

pub const VERSION: &str = env!("CARGO_PKG_VERSION");

struct PeerHandle {
    conn_id: u64,
    data: mpsc::Sender<Vec<u8>>,
    ctrl: mpsc::Sender<Vec<u8>>,
    kill: Arc<Notify>,
    info: Vec<u8>,
}

#[derive(Default)]
struct Stats {
    connections_total: AtomicU64,
    connections_now: AtomicUsize,
    auth_failures: AtomicU64,
    frames_forwarded: AtomicU64,
    bytes_forwarded: AtomicU64,
    send_failures: AtomicU64,
    protocol_errors: AtomicU64,
}

struct Hub {
    cfg: Config,
    key: [u8; 32],
    salt: [u8; 16],
    peers: Mutex<HashMap<PeerId, PeerHandle>>,
    per_ip: Mutex<HashMap<IpAddr, usize>>,
    auth_fail: Mutex<HashMap<IpAddr, (u32, Instant)>>,
    next_id: AtomicU64,
    stats: Stats,
    started: Instant,
    started_unix: u64,
    port: u16,
}

#[derive(Serialize)]
pub struct Status {
    pub name: &'static str,
    pub version: &'static str,
    pub protocol: u8,
    pub pid: u32,
    pub bind: String,
    pub port: u16,
    pub online: usize,
    pub connections: usize,
    pub connections_total: u64,
    pub auth_failures: u64,
    pub frames_forwarded: u64,
    pub bytes_forwarded: u64,
    pub send_failures: u64,
    pub protocol_errors: u64,
    pub uptime_secs: u64,
    pub started_unix: u64,
}

impl Hub {
    fn status(&self) -> Status {
        Status {
            name: "lsm-relay-server",
            version: VERSION,
            protocol: proto::PROTO_VERSION,
            pid: std::process::id(),
            bind: self.cfg.server.bind.clone(),
            port: self.port,
            online: self.peers.lock().unwrap().len(),
            connections: self.stats.connections_now.load(Ordering::Relaxed),
            connections_total: self.stats.connections_total.load(Ordering::Relaxed),
            auth_failures: self.stats.auth_failures.load(Ordering::Relaxed),
            frames_forwarded: self.stats.frames_forwarded.load(Ordering::Relaxed),
            bytes_forwarded: self.stats.bytes_forwarded.load(Ordering::Relaxed),
            send_failures: self.stats.send_failures.load(Ordering::Relaxed),
            protocol_errors: self.stats.protocol_errors.load(Ordering::Relaxed),
            uptime_secs: self.started.elapsed().as_secs(),
            started_unix: self.started_unix,
        }
    }

    fn too_many_failures(&self, ip: IpAddr) -> bool {
        let mut m = self.auth_fail.lock().unwrap();
        if let Some((n, t0)) = m.get(&ip) {
            if t0.elapsed() > Duration::from_secs(60) {
                m.remove(&ip);
                return false;
            }
            return *n >= self.cfg.limits.auth_failures_per_min;
        }
        false
    }
    fn note_failure(&self, ip: IpAddr) {
        self.stats.auth_failures.fetch_add(1, Ordering::Relaxed);
        let mut m = self.auth_fail.lock().unwrap();
        let e = m.entry(ip).or_insert((0, Instant::now()));
        if e.1.elapsed() > Duration::from_secs(60) {
            *e = (0, Instant::now());
        }
        e.0 += 1;
        if m.len() > 100_000 {
            m.retain(|_, v| v.1.elapsed() < Duration::from_secs(60));
        }
    }

    /// Broadcast a control frame to every peer except `except`.
    fn broadcast(&self, frame: &Frame, except: &PeerId) {
        let bytes = frame.encode();
        let peers = self.peers.lock().unwrap();
        for (id, p) in peers.iter() {
            if id == except {
                continue;
            }
            if p.ctrl.try_send(bytes.clone()).is_err() {
                // Control queue full = peer is hopelessly behind; disconnect it.
                p.kill.notify_one();
            }
        }
    }

    fn peer_list(&self, me: &PeerId) -> Frame {
        let peers = self.peers.lock().unwrap();
        let mut body = Vec::new();
        let mut n: u16 = 0;
        let mut entries = Vec::new();
        for (id, p) in peers.iter() {
            if id == me || n == u16::MAX {
                continue;
            }
            proto::peer_entry(id, &p.info, &mut entries);
            n += 1;
        }
        body.extend_from_slice(&n.to_be_bytes());
        body.extend_from_slice(&entries);
        Frame::new(t::PEER_LIST, body)
    }
}

/// Simple token bucket.
struct Bucket {
    rate: f64,
    cap: f64,
    tokens: f64,
    last: Instant,
}
impl Bucket {
    fn new(rate: u64) -> Self {
        let r = rate.max(1) as f64;
        Bucket { rate: r, cap: r, tokens: r, last: Instant::now() }
    }
    /// Returns how long to wait before `n` units are allowed (and consumes them).
    fn take(&mut self, n: f64) -> Duration {
        let now = Instant::now();
        self.tokens = (self.tokens + now.duration_since(self.last).as_secs_f64() * self.rate).min(self.cap);
        self.last = now;
        self.tokens -= n;
        if self.tokens >= 0.0 {
            Duration::ZERO
        } else {
            Duration::from_secs_f64(-self.tokens / self.rate)
        }
    }
}

pub struct Running {
    pub addr: SocketAddr,
    pub status_addr: Option<SocketAddr>,
    shutdown: watch::Sender<bool>,
    handle: JoinHandle<()>,
    status_handle: Option<JoinHandle<()>>,
}

impl Running {
    pub async fn stop(self) {
        let _ = self.shutdown.send(true);
        let _ = self.handle.await;
        if let Some(h) = self.status_handle {
            h.abort();
        }
    }
    pub async fn wait(self) {
        let _ = self.handle.await;
    }
    pub fn shutdown_sender(&self) -> watch::Sender<bool> {
        self.shutdown.clone()
    }
}

pub async fn start(cfg: Config) -> Result<Running> {
    // passphrase is optional (v0.6.0+): empty = open relay; clients also use empty by default
    let salt = cfg.salt_bytes()?;
    let pass = cfg.server.passphrase.clone();
    let key = tokio::task::spawn_blocking(move || proto::derive_key(&pass, &salt)).await?;
    let bind = format!("{}:{}", bracket(&cfg.server.bind), cfg.server.port);
    let listener = TcpListener::bind(&bind)
        .await
        .map_err(|e| anyhow::anyhow!("无法监听 {bind} (cannot listen): {e}"))?;
    let addr = listener.local_addr()?;
    let (tx, rx) = watch::channel(false);
    let hub = Arc::new(Hub {
        key,
        salt,
        peers: Mutex::new(HashMap::new()),
        per_ip: Mutex::new(HashMap::new()),
        auth_fail: Mutex::new(HashMap::new()),
        next_id: AtomicU64::new(1),
        stats: Stats::default(),
        started: Instant::now(),
        started_unix: SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or(0),
        port: addr.port(),
        cfg: cfg.clone(),
    });
    let mut status_addr = None;
    let mut status_handle = None;
    if let Some(sa) = cfg.status_addr() {
        // Status endpoint for a port-0 test server follows the real port.
        let sa = if cfg.server.port == 0 && cfg.server.status_addr.is_empty() { "127.0.0.1:0".to_string() } else { sa };
        match TcpListener::bind(&sa).await {
            Ok(l) => {
                status_addr = l.local_addr().ok();
                status_handle = Some(tokio::spawn(status_loop(l, hub.clone())));
            }
            Err(e) => warn!("状态接口无法监听 {sa} (status endpoint disabled): {e}"),
        }
    }
    info!(
        "中转服务器已启动 (relay started) v{VERSION} 监听 {addr} 状态接口 {:?} 最大帧 {} 最大连接 {}",
        status_addr, cfg.limits.max_frame, cfg.limits.max_connections
    );
    let handle = tokio::spawn(accept_loop(listener, hub, rx));
    Ok(Running { addr, status_addr, shutdown: tx, handle, status_handle })
}

fn bracket(h: &str) -> String {
    if h.contains(':') && !h.starts_with('[') { format!("[{h}]") } else { h.to_string() }
}

async fn status_loop(l: TcpListener, hub: Arc<Hub>) {
    loop {
        let Ok((mut s, _)) = l.accept().await else { continue };
        let hub = hub.clone();
        tokio::spawn(async move {
            let mut buf = [0u8; 1024];
            let _ = tokio::time::timeout(Duration::from_secs(2), s.read(&mut buf)).await;
            let body = serde_json::to_string_pretty(&hub.status()).unwrap_or_default();
            let resp = format!(
                "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}",
                body.len(),
                body
            );
            let _ = s.write_all(resp.as_bytes()).await;
            let _ = s.shutdown().await;
        });
    }
}

async fn accept_loop(listener: TcpListener, hub: Arc<Hub>, mut shutdown: watch::Receiver<bool>) {
    let mut tasks: Vec<JoinHandle<()>> = Vec::new();
    loop {
        tokio::select! {
            r = listener.accept() => {
                let (sock, peer) = match r {
                    Ok(x) => x,
                    Err(e) => { warn!("accept 失败: {e}"); tokio::time::sleep(Duration::from_millis(50)).await; continue; }
                };
                let hub2 = hub.clone();
                let sd = shutdown.clone();
                tasks.retain(|h| !h.is_finished());
                tasks.push(tokio::spawn(async move {
                    let _ = sock.set_nodelay(true);
                    conn_entry(sock, peer, hub2, sd).await;
                }));
            }
            _ = shutdown.changed() => break,
        }
    }
    info!("中转服务器正在停止 (shutting down), 断开 {} 个连接", tasks.len());
    drop(listener);
    // Each connection watches the shutdown channel too; give them a moment.
    let deadline = tokio::time::sleep(Duration::from_secs(3));
    tokio::pin!(deadline);
    for h in tasks {
        tokio::select! { _ = h => {}, _ = &mut deadline => break }
    }
}

async fn conn_entry(sock: TcpStream, peer: SocketAddr, hub: Arc<Hub>, shutdown: watch::Receiver<bool>) {
    let ip = peer.ip();
    hub.stats.connections_total.fetch_add(1, Ordering::Relaxed);
    let now = hub.stats.connections_now.fetch_add(1, Ordering::Relaxed) + 1;
    let ip_count = {
        let mut m = hub.per_ip.lock().unwrap();
        let c = m.entry(ip).or_insert(0);
        *c += 1;
        *c
    };
    let mut sock = sock;
    if now > hub.cfg.limits.max_connections || ip_count > hub.cfg.limits.max_connections_per_ip {
        warn!("{peer} 连接数超限 (connection limit) now={now} ip={ip_count}");
        let _ = proto::write_frame(&mut sock, &Frame::new(t::ERROR, proto::fail_body(code::SERVER_FULL, "server full"))).await;
    } else if hub.too_many_failures(ip) {
        let _ = proto::write_frame(&mut sock, &Frame::new(t::AUTH_FAIL, proto::fail_body(code::TOO_MANY_ATTEMPTS, "too many failed attempts"))).await;
    } else {
        match handle_conn(sock, peer, &hub, shutdown).await {
            Ok(()) => {}
            Err(e) => debug!("{peer} 连接结束 (closed): {e}"),
        }
    }
    hub.stats.connections_now.fetch_sub(1, Ordering::Relaxed);
    let mut m = hub.per_ip.lock().unwrap();
    if let Some(c) = m.get_mut(&ip) {
        *c -= 1;
        if *c == 0 {
            m.remove(&ip);
        }
    }
}

async fn handle_conn(sock: TcpStream, peer: SocketAddr, hub: &Arc<Hub>, mut shutdown: watch::Receiver<bool>) -> Result<()> {
    let lim = hub.cfg.limits.clone();
    let max_frame = lim.max_frame.min(proto::HARD_MAX_FRAME);
    let (rd, wr) = sock.into_split();
    let mut rd = BufReader::with_capacity(64 * 1024, rd);
    let mut wr = BufWriter::with_capacity(64 * 1024, wr);

    // ---- handshake ----
    let mut snonce = [0u8; 32];
    rand::thread_rng().fill_bytes(&mut snonce);
    let hello = Hello { version: proto::PROTO_VERSION, salt: hub.salt, m_kib: proto::ARGON_M_KIB, t_cost: proto::ARGON_T, nonce: snonce };
    proto::write_frame(&mut wr, &hello.encode()).await?;
    wr.flush().await?;
    // AUTH is small; cap it at 1 KiB regardless of max_frame.
    let f = match tokio::time::timeout(Duration::from_secs(lim.auth_timeout_secs), proto::read_frame(&mut rd, 1024)).await {
        Ok(Ok(f)) => f,
        Ok(Err(e)) => {
            hub.stats.protocol_errors.fetch_add(1, Ordering::Relaxed);
            anyhow::bail!("handshake read: {e}")
        }
        Err(_) => anyhow::bail!("auth timeout"),
    };
    let auth = match Auth::decode(&f) {
        Ok(a) => a,
        Err(e) => {
            hub.stats.protocol_errors.fetch_add(1, Ordering::Relaxed);
            let _ = proto::write_frame(&mut wr, &Frame::new(t::AUTH_FAIL, proto::fail_body(code::MALFORMED, "malformed AUTH"))).await;
            let _ = wr.flush().await;
            return Err(e);
        }
    };
    if auth.version != proto::PROTO_VERSION {
        proto::write_frame(&mut wr, &Frame::new(t::AUTH_FAIL, proto::fail_body(code::BAD_VERSION, "unsupported protocol version"))).await?;
        wr.flush().await?;
        anyhow::bail!("bad version {}", auth.version);
    }
    let expect = proto::auth_mac(&hub.key, proto::CTX_CLIENT, &snonce, &auth.client_nonce, &auth.id);
    if !proto::ct_eq(&expect, &auth.mac) {
        hub.note_failure(peer.ip());
        warn!("{peer} 口令错误 (bad passphrase) id={}", short(&auth.id));
        tokio::time::sleep(Duration::from_millis(300)).await;
        proto::write_frame(&mut wr, &Frame::new(t::AUTH_FAIL, proto::fail_body(code::BAD_PASSPHRASE, "bad passphrase"))).await?;
        wr.flush().await?;
        return Ok(());
    }
    let me = auth.id;
    let smac = proto::auth_mac(&hub.key, proto::CTX_SERVER, &auth.client_nonce, &snonce, &me);
    let mut okb = smac.to_vec();
    okb.extend_from_slice(&max_frame.to_be_bytes());
    okb.extend_from_slice(&lim.heartbeat_secs.to_be_bytes());
    proto::write_frame(&mut wr, &Frame::new(t::AUTH_OK, okb)).await?;

    // ---- register ----
    let conn_id = hub.next_id.fetch_add(1, Ordering::Relaxed);
    let (dtx, mut drx) = mpsc::channel::<Vec<u8>>(lim.queue_frames.max(4));
    let (ctx, mut crx) = mpsc::channel::<Vec<u8>>(4096);
    let kill = Arc::new(Notify::new());
    {
        let mut peers = hub.peers.lock().unwrap();
        if let Some(old) = peers.remove(&me) {
            let _ = old.ctrl.try_send(Frame::new(t::ERROR, proto::fail_body(code::REPLACED, "logged in elsewhere")).encode());
            old.kill.notify_one();
        }
        peers.insert(me, PeerHandle { conn_id, data: dtx, ctrl: ctx.clone(), kill: kill.clone(), info: auth.info.clone() });
    }
    // Snapshot to the new peer first, then announce it to everybody else.
    let _ = ctx.try_send(hub.peer_list(&me).encode());
    let mut jb = Vec::new();
    proto::peer_entry(&me, &auth.info, &mut jb);
    hub.broadcast(&Frame::new(t::PEER_JOIN, jb), &me);
    info!("{peer} 上线 (online) id={} conn={conn_id} 在线 {}", short(&me), hub.peers.lock().unwrap().len());

    // ---- writer ----
    let writer = tokio::spawn(async move {
        loop {
            let msg = tokio::select! {
                biased;
                m = crx.recv() => m,
                m = drx.recv() => m,
            };
            let Some(m) = msg else { break };
            if wr.write_all(&m).await.is_err() {
                break;
            }
            // Flush only when nothing else is immediately queued.
            if crx.is_empty() && drx.is_empty() && wr.flush().await.is_err() {
                break;
            }
            if m.len() >= 5 && m[4] == t::ERROR {
                let _ = wr.flush().await;
                break;
            }
        }
        let _ = wr.flush().await;
        let _ = wr.shutdown().await;
    });

    // ---- reader ----
    let mut bytes_b = Bucket::new(lim.rate_bytes_per_sec);
    let mut frames_b = Bucket::new(lim.rate_frames_per_sec);
    let idle = Duration::from_secs(lim.idle_timeout_secs.max(1));
    let send_to = Duration::from_millis(lim.send_timeout_ms);
    let result: Result<()> = loop {
        let f = tokio::select! {
            r = tokio::time::timeout(idle, proto::read_frame(&mut rd, max_frame)) => r,
            _ = kill.notified() => break Ok(()),
            _ = shutdown.changed() => {
                let _ = ctx.try_send(Frame::new(t::ERROR, proto::fail_body(code::SHUTDOWN, "server shutting down")).encode());
                break Ok(());
            }
        };
        let f = match f {
            Err(_) => {
                let _ = ctx.try_send(Frame::new(t::ERROR, proto::fail_body(code::IDLE_TIMEOUT, "idle timeout")).encode());
                break Err(anyhow::anyhow!("idle timeout"));
            }
            Ok(Err(ReadErr::Eof)) => break Ok(()),
            Ok(Err(e @ ReadErr::TooLarge(_))) | Ok(Err(e @ ReadErr::Empty)) => {
                hub.stats.protocol_errors.fetch_add(1, Ordering::Relaxed);
                let _ = ctx.try_send(Frame::new(t::ERROR, proto::fail_body(code::FRAME_TOO_LARGE, &e.to_string())).encode());
                break Err(e.into());
            }
            Ok(Err(e)) => break Err(e.into()),
            Ok(Ok(f)) => f,
        };
        let d = bytes_b.take(f.body.len() as f64 + 5.0).max(frames_b.take(1.0));
        if !d.is_zero() {
            tokio::time::sleep(d).await;
        }
        match f.ty {
            t::PING => {
                let _ = ctx.try_send(Frame::new(t::PONG, f.body).encode());
            }
            t::PONG => {}
            t::LIST => {
                let _ = ctx.try_send(hub.peer_list(&me).encode());
            }
            t::BYE => break Ok(()),
            t::SEND => {
                if f.body.len() < 32 {
                    hub.stats.protocol_errors.fetch_add(1, Ordering::Relaxed);
                    continue;
                }
                let dst: PeerId = f.body[..32].try_into().unwrap();
                let target = hub.peers.lock().unwrap().get(&dst).map(|p| p.data.clone());
                let fail = |c: u8| {
                    let mut b = dst.to_vec();
                    b.push(c);
                    Frame::new(t::SEND_FAIL, b).encode()
                };
                let Some(target) = target else {
                    hub.stats.send_failures.fetch_add(1, Ordering::Relaxed);
                    let _ = ctx.try_send(fail(code::PEER_OFFLINE));
                    continue;
                };
                let mut body = f.body;
                body[..32].copy_from_slice(&me);
                let n = body.len() as u64 - 32;
                let out = Frame::new(t::DELIVER, body).encode();
                match tokio::time::timeout(send_to, target.send(out)).await {
                    Ok(Ok(())) => {
                        hub.stats.frames_forwarded.fetch_add(1, Ordering::Relaxed);
                        hub.stats.bytes_forwarded.fetch_add(n, Ordering::Relaxed);
                    }
                    Ok(Err(_)) => {
                        hub.stats.send_failures.fetch_add(1, Ordering::Relaxed);
                        let _ = ctx.try_send(fail(code::PEER_OFFLINE));
                    }
                    Err(_) => {
                        hub.stats.send_failures.fetch_add(1, Ordering::Relaxed);
                        let _ = ctx.try_send(fail(code::PEER_BUSY));
                    }
                }
            }
            other => {
                // Unknown types are ignored for forward compatibility.
                debug!("{peer} 未知帧类型 (unknown frame type) 0x{other:02x}");
            }
        }
    };

    // ---- unregister ----
    let removed = {
        let mut peers = hub.peers.lock().unwrap();
        match peers.get(&me) {
            Some(p) if p.conn_id == conn_id => {
                peers.remove(&me);
                true
            }
            _ => false,
        }
    };
    if removed {
        hub.broadcast(&Frame::new(t::PEER_LEAVE, me.to_vec()), &me);
    }
    info!("{peer} 下线 (offline) id={} 在线 {}", short(&me), hub.peers.lock().unwrap().len());
    drop(ctx);
    let _ = tokio::time::timeout(Duration::from_secs(2), writer).await;
    result
}

pub fn short(id: &PeerId) -> String {
    hex::encode(&id[..6])
}

/// Query a running server's status endpoint; returns the JSON body.
pub async fn query_status(addr: &str) -> Result<String> {
    let mut s = tokio::time::timeout(Duration::from_secs(3), TcpStream::connect(addr)).await??;
    s.write_all(b"GET /status HTTP/1.0\r\nHost: localhost\r\n\r\n").await?;
    let mut v = Vec::new();
    tokio::time::timeout(Duration::from_secs(3), s.read_to_end(&mut v)).await??;
    let txt = String::from_utf8_lossy(&v).to_string();
    Ok(txt.split("\r\n\r\n").nth(1).unwrap_or("").to_string())
}
