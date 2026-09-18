//! High-level node: discovery + sessions + messaging API.

use crate::config::{AppConfig, ConfigPaths};
use crate::discovery::{AnnounceValidator, DiscoveryService};
use crate::history::HistoryStore;
use crate::identity::Identity;
use crate::peer::{DiscoveryChange, PeerInfo, PeerStore, TrustState};
use crate::protocol::{BroadcastResult, ChatMessage, FileMeta, MsgDirection, MsgKind, PeerStatusKind, WireMessage};
use crate::session::SecureSession;
use crate::transfer::{self, is_image_mime};
use crate::{DISCOVERY_INTERVAL_MS, PEER_TIMEOUT_SECS};
use anyhow::{anyhow, Context, Result};
use parking_lot::Mutex;
use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::time::Duration;
use tokio::net::TcpListener;
use tokio::sync::{broadcast, mpsc, oneshot};
use tracing::{info, warn};
use uuid::Uuid;

#[derive(Debug, Clone)]
pub enum NodeEvent {
    PeerOnline(PeerInfo),
    PeerOffline { fingerprint: String, name: String },
    PeerUpdated(PeerInfo),
    NewPeerNeedsTrust(PeerInfo),
    Text {
        from_fp: String,
        from_name: String,
        body: String,
        id: Uuid,
        broadcast: bool,
    },
    /// Delivery receipt for an outbound text we sent.
    TextDelivered { id: Uuid, peer_fp: String },
    FileReceived {
        from_fp: String,
        from_name: String,
        path: PathBuf,
        meta: FileMeta,
    },
    FileProgress {
        id: Uuid,
        direction: MsgDirection,
        received: u64,
        total: u64,
    },
    TransferComplete {
        id: Uuid,
        name: String,
        ok: bool,
    },
    PeerStatus {
        peer_fp: String,
        kind: PeerStatusKind,
        detail: String,
    },
    Error(String),
    Info(String),
}

enum Command {
    SendText {
        peer_fp: String,
        body: String,
        reply: oneshot::Sender<Result<Uuid>>,
    },
    BroadcastText {
        body: String,
        reply: oneshot::Sender<Result<usize>>,
    },
    SendFile {
        peer_fp: String,
        path: PathBuf,
        reply: oneshot::Sender<Result<FileMeta>>,
    },
    TrustPeer {
        fp: String,
        trust: TrustState,
        reply: oneshot::Sender<Result<()>>,
    },
    ListPeers {
        reply: oneshot::Sender<Vec<PeerInfo>>,
    },
    History {
        peer_fp: Option<String>,
        limit: usize,
        reply: oneshot::Sender<Vec<ChatMessage>>,
    },
    Fingerprint {
        reply: oneshot::Sender<(String, String)>,
    },
    SetDisplayName {
        name: String,
        reply: oneshot::Sender<Result<()>>,
    },
    OfferPin {
        pin: String,
        reply: oneshot::Sender<Result<()>>,
    },
    PairWithPin {
        peer_fp: String,
        pin: String,
        reply: oneshot::Sender<Result<()>>,
    },
    CancelTransfer {
        id: Uuid,
        reply: oneshot::Sender<Result<()>>,
    },
    Doctor {
        reply: oneshot::Sender<crate::config::DoctorReport>,
    },
    BroadcastTextDetailed {
        body: String,
        reply: oneshot::Sender<Result<BroadcastResult>>,
    },
    Shutdown {
        reply: oneshot::Sender<()>,
    },
}

#[derive(Clone)]
pub struct NodeHandle {
    tx: mpsc::UnboundedSender<Command>,
    pub events: broadcast::Sender<NodeEvent>,
}

impl NodeHandle {
    pub async fn send_text(&self, peer_fp: &str, body: &str) -> Result<Uuid> {
        let (reply, rx) = oneshot::channel();
        self.tx.send(Command::SendText {
            peer_fp: peer_fp.into(),
            body: body.into(),
            reply,
        })?;
        rx.await?
    }

    pub async fn broadcast_text(&self, body: &str) -> Result<usize> {
        let (reply, rx) = oneshot::channel();
        self.tx.send(Command::BroadcastText {
            body: body.into(),
            reply,
        })?;
        rx.await?
    }

    pub async fn send_file(&self, peer_fp: &str, path: impl AsRef<Path>) -> Result<FileMeta> {
        let (reply, rx) = oneshot::channel();
        self.tx.send(Command::SendFile {
            peer_fp: peer_fp.into(),
            path: path.as_ref().to_path_buf(),
            reply,
        })?;
        rx.await?
    }

    pub async fn trust_peer(&self, fp: &str, trust: TrustState) -> Result<()> {
        let (reply, rx) = oneshot::channel();
        self.tx.send(Command::TrustPeer {
            fp: fp.into(),
            trust,
            reply,
        })?;
        rx.await?
    }

    pub async fn list_peers(&self) -> Result<Vec<PeerInfo>> {
        let (reply, rx) = oneshot::channel();
        self.tx.send(Command::ListPeers { reply })?;
        Ok(rx.await?)
    }

    pub async fn history(&self, peer_fp: Option<&str>, limit: usize) -> Result<Vec<ChatMessage>> {
        let (reply, rx) = oneshot::channel();
        self.tx.send(Command::History {
            peer_fp: peer_fp.map(|s| s.to_string()),
            limit,
            reply,
        })?;
        Ok(rx.await?)
    }

    pub async fn fingerprint(&self) -> Result<(String, String)> {
        let (reply, rx) = oneshot::channel();
        self.tx.send(Command::Fingerprint { reply })?;
        Ok(rx.await?)
    }

    pub async fn set_display_name(&self, name: &str) -> Result<()> {
        let (reply, rx) = oneshot::channel();
        self.tx.send(Command::SetDisplayName {
            name: name.into(),
            reply,
        })?;
        rx.await?
    }

    pub async fn shutdown(&self) {
        let (reply, rx) = oneshot::channel();
        let _ = self.tx.send(Command::Shutdown { reply });
        let _ = rx.await;
    }


    pub async fn offer_pin(&self, pin: &str) -> Result<()> {
        let (reply, rx) = oneshot::channel();
        self.tx.send(Command::OfferPin {
            pin: pin.into(),
            reply,
        })?;
        rx.await?
    }

    pub async fn pair_with_pin(&self, peer_fp: &str, pin: &str) -> Result<()> {
        let (reply, rx) = oneshot::channel();
        self.tx.send(Command::PairWithPin {
            peer_fp: peer_fp.into(),
            pin: pin.into(),
            reply,
        })?;
        rx.await?
    }

    pub async fn cancel_transfer(&self, id: Uuid) -> Result<()> {
        let (reply, rx) = oneshot::channel();
        self.tx.send(Command::CancelTransfer { id, reply })?;
        rx.await?
    }

    pub async fn doctor(&self) -> Result<crate::config::DoctorReport> {
        let (reply, rx) = oneshot::channel();
        self.tx.send(Command::Doctor { reply })?;
        Ok(rx.await?)
    }

    pub async fn broadcast_text_detailed(&self, body: &str) -> Result<BroadcastResult> {
        let (reply, rx) = oneshot::channel();
        self.tx.send(Command::BroadcastTextDetailed {
            body: body.into(),
            reply,
        })?;
        rx.await?
    }

    pub fn subscribe(&self) -> broadcast::Receiver<NodeEvent> {
        self.events.subscribe()
    }
}

pub struct Node {
    paths: ConfigPaths,
    config: AppConfig,
    identity: Identity,
    peers: PeerStore,
    history: HistoryStore,
    downloads: PathBuf,
}

impl Node {
    pub fn open(paths: ConfigPaths, config: AppConfig) -> Result<Self> {
        paths.ensure_dirs()?;
        let identity = Identity::load_or_create(&paths)?;
        let peers = PeerStore::load(&paths)?;
        let history = HistoryStore::load(&paths, config.history_enabled)?;
        let downloads = paths.root.join("downloads");
        std::fs::create_dir_all(&downloads)?;
        config.save(&paths)?;
        Ok(Self {
            paths,
            config,
            identity,
            peers,
            history,
            downloads,
        })
    }

    pub fn identity_fingerprint(&self) -> String {
        self.identity.fingerprint().as_str().to_string()
    }

    pub fn display_name(&self) -> &str {
        &self.config.display_name
    }

    pub async fn start(self) -> Result<NodeHandle> {
        let (cmd_tx, cmd_rx) = mpsc::unbounded_channel();
        let (ev_tx, _) = broadcast::channel(256);
        let handle = NodeHandle {
            tx: cmd_tx,
            events: ev_tx.clone(),
        };

        let discovery = DiscoveryService::bind(
            &self.config.bind_addr,
            self.config.discovery_port,
            self.config.broadcast_addr.as_deref(),
        )
        .await?;
        info!(addr = %discovery.local_addr()?, "discovery listening");

        let listen_addr = format!("{}:{}", self.config.bind_addr, self.config.listen_port);
        let listener = TcpListener::bind(&listen_addr)
            .await
            .with_context(|| format!("bind TCP {listen_addr}"))?;
        info!(addr = %listener.local_addr()?, "TCP listening");

        let state = Arc::new(Mutex::new(RuntimeState {
            config: self.config,
            identity: self.identity,
            peers: self.peers,
            history: self.history,
            paths: self.paths,
            downloads: self.downloads,
            sessions: std::collections::HashMap::new(),
            pending_pin: None,
            cancel_transfers: std::collections::HashSet::new(),
        }));

        tokio::spawn(run_loop(state, discovery, listener, cmd_rx, ev_tx));
        Ok(handle)
    }
}

struct RuntimeState {
    config: AppConfig,
    identity: Identity,
    peers: PeerStore,
    history: HistoryStore,
    paths: ConfigPaths,
    downloads: PathBuf,
    /// Reusable outbound Noise sessions keyed by peer fingerprint.
    sessions: std::collections::HashMap<String, SecureSession>,
    /// Optional PIN offered locally for pairing (expires).
    pending_pin: Option<(String, std::time::Instant)>,
    /// Transfer IDs requested to cancel.
    cancel_transfers: std::collections::HashSet<Uuid>,
}

async fn run_loop(
    state: Arc<Mutex<RuntimeState>>,
    discovery: DiscoveryService,
    listener: TcpListener,
    mut cmd_rx: mpsc::UnboundedReceiver<Command>,
    ev_tx: broadcast::Sender<NodeEvent>,
) {
    let mut validator = AnnounceValidator::default();
    let mut announce_tick = tokio::time::interval(Duration::from_millis(DISCOVERY_INTERVAL_MS));
    let mut stale_tick = tokio::time::interval(Duration::from_secs(2));
    let mut buf = vec![0u8; 65535];

    loop {
        tokio::select! {
            _ = announce_tick.tick() => {
                let (identity, name, port) = {
                    let s = state.lock();
                    (
                        s.identity.clone(),
                        s.config.display_name.clone(),
                        s.config.listen_port,
                    )
                };
                if let Err(e) = discovery.announce(&identity, &name, port).await {
                    warn!(error = %e, "announce failed");
                }
            }
            recv = discovery.recv(&mut buf) => {
                match recv {
                    Ok((n, from)) => {
                        let self_fp = state.lock().identity.fingerprint().clone();
                        match validator.validate(&buf[..n], from, &self_fp) {
                            Ok(vp) => {
                                let auto = state.lock().config.auto_trust;
                                let (info, is_new, change) = {
                                    let mut s = state.lock();
                                    let r = s.peers.upsert_discovery(
                                        vp.fingerprint.clone(),
                                        vp.display_name.clone(),
                                        vp.x25519_pk,
                                        vp.ed25519_pk,
                                        vp.addr,
                                        vp.listen_port,
                                        auto,
                                    );
                                    let _ = s.peers.save(&s.paths);
                                    r
                                };
                                if is_new && info.trust == TrustState::Unknown {
                                    let _ = ev_tx.send(NodeEvent::NewPeerNeedsTrust(info.clone()));
                                }
                                match change {
                                    DiscoveryChange::CameOnline => {
                                        let _ = ev_tx.send(NodeEvent::PeerOnline(info));
                                    }
                                    DiscoveryChange::Updated => {
                                        let _ = ev_tx.send(NodeEvent::PeerUpdated(info));
                                    }
                                    DiscoveryChange::Refreshed => {
                                        // Heartbeat only — do not spam PeerOnline.
                                    }
                                }
                            }
                            Err(e) => {
                                let msg = e.to_string();
                                if !msg.contains("self announce") && !msg.contains("replayed") {
                                    crate::discovery::warn_invalid(e);
                                }
                            }
                        }
                    }
                    Err(e) => warn!(error = %e, "discovery recv error"),
                }
            }
            accept = listener.accept() => {
                match accept {
                    Ok((stream, addr)) => {
                        let state = state.clone();
                        let ev_tx = ev_tx.clone();
                        tokio::spawn(async move {
                            if let Err(e) = handle_incoming(state, stream, addr, ev_tx).await {
                                warn!(error = %e, %addr, "incoming session error");
                            }
                        });
                    }
                    Err(e) => warn!(error = %e, "accept error"),
                }
            }
            _ = stale_tick.tick() => {
                let gone = state
                    .lock()
                    .peers
                    .mark_offline_stale(Duration::from_secs(PEER_TIMEOUT_SECS));
                for fp in gone {
                    let name = state
                        .lock()
                        .peers
                        .get_live(&fp)
                        .map(|p| p.display_name.clone())
                        .unwrap_or_default();
                    let _ = ev_tx.send(NodeEvent::PeerOffline {
                        fingerprint: fp,
                        name,
                    });
                }
            }
            cmd = cmd_rx.recv() => {
                match cmd {
                    None => break,
                    Some(Command::OfferPin { pin, reply }) => {
                        if pin.len() < 4 || pin.len() > 12 || !pin.chars().all(|c| c.is_ascii_digit()) {
                            let _ = reply.send(Err(anyhow!("PIN must be 4-12 digits")));
                        } else {
                            let mut s = state.lock();
                            s.pending_pin = Some((pin, std::time::Instant::now()));
                            let _ = reply.send(Ok(()));
                            let _ = ev_tx.send(NodeEvent::Info(
                                "PIN pairing armed for 5 minutes — tell the peer your PIN".into(),
                            ));
                        }
                    }
                    Some(Command::PairWithPin { peer_fp, pin, reply }) => {
                        let result = do_pair_with_pin(&state, &ev_tx, &peer_fp, &pin).await;
                        let _ = reply.send(result);
                    }
                    Some(Command::CancelTransfer { id, reply }) => {
                        state.lock().cancel_transfers.insert(id);
                        let _ = reply.send(Ok(()));
                        let _ = ev_tx.send(NodeEvent::Info(format!("cancel requested for {id}")));
                    }
                    Some(Command::Doctor { reply }) => {
                        let s = state.lock();
                        let peers = s.peers.list_all();
                        let report = crate::config::DoctorReport {
                            display_name: s.config.display_name.clone(),
                            fingerprint: s.identity.fingerprint().as_str().to_string(),
                            config_dir: s.paths.root.display().to_string(),
                            bind_addr: s.config.bind_addr.clone(),
                            discovery_port: s.config.discovery_port,
                            listen_port: s.config.listen_port,
                            broadcast_addr: s.config.broadcast_addr.clone(),
                            auto_trust: s.config.auto_trust,
                            history_enabled: s.config.history_enabled,
                            max_file_bytes: s.config.max_file_bytes,
                            session_idle_secs: s.config.session_idle_secs,
                            peer_total: peers.len(),
                            peer_online: peers.iter().filter(|p| p.online).count(),
                            peer_trusted: peers
                                .iter()
                                .filter(|p| p.trust == TrustState::Trusted)
                                .count(),
                        };
                        let _ = reply.send(report);
                    }
                    Some(Command::BroadcastTextDetailed { body, reply }) => {
                        let peers: Vec<_> = state
                            .lock()
                            .peers
                            .list_online()
                            .into_iter()
                            .filter(|p| p.trust == TrustState::Trusted && p.online)
                            .collect();
                        let mut result = BroadcastResult {
                            ok: Vec::new(),
                            failed: Vec::new(),
                        };
                        for p in &peers {
                            match do_send_text(&state, &ev_tx, p.fingerprint.as_str(), &body, true)
                                .await
                            {
                                Ok(_) => result.ok.push(p.fingerprint.as_str().to_string()),
                                Err(e) => result
                                    .failed
                                    .push((p.fingerprint.as_str().to_string(), e.to_string())),
                            }
                        }
                        let _ = reply.send(Ok(result));
                    }
                    Some(Command::Shutdown { reply }) => {

                        let _ = reply.send(());
                        break;
                    }
                    Some(Command::Fingerprint { reply }) => {
                        let s = state.lock();
                        let _ = reply.send((
                            s.identity.fingerprint().as_str().to_string(),
                            s.config.display_name.clone(),
                        ));
                    }
                    Some(Command::ListPeers { reply }) => {
                        let peers = state.lock().peers.list_all();
                        let _ = reply.send(peers);
                    }
                    Some(Command::History { peer_fp, limit, reply }) => {
                        let msgs = state.lock().history.list(peer_fp.as_deref(), limit);
                        let _ = reply.send(msgs);
                    }
                    Some(Command::TrustPeer { fp, trust, reply }) => {
                        let mut s = state.lock();
                        let changed = s.peers.set_trust(&fp, trust);
                        let res = s.peers.save(&s.paths);
                        if changed {
                            if let Some(info) = s.peers.get_live(&fp).cloned() {
                                let _ = ev_tx.send(NodeEvent::PeerUpdated(info));
                            }
                        }
                        let _ = reply.send(res);
                    }
                    Some(Command::SetDisplayName { name, reply }) => {
                        let mut s = state.lock();
                        s.config.display_name = name;
                        let res = s.config.save(&s.paths);
                        let _ = reply.send(res);
                    }
                    Some(Command::SendText { peer_fp, body, reply }) => {
                        let result = do_send_text(&state, &ev_tx, &peer_fp, &body, false).await;
                        let _ = reply.send(result);
                    }
                    Some(Command::BroadcastText { body, reply }) => {
                        let peers: Vec<_> = state
                            .lock()
                            .peers
                            .list_online()
                            .into_iter()
                            .filter(|p| p.trust == TrustState::Trusted && p.online)
                            .collect();
                        let mut ok = 0usize;
                        for p in &peers {
                            if do_send_text(&state, &ev_tx, p.fingerprint.as_str(), &body, true)
                                .await
                                .is_ok()
                            {
                                ok += 1;
                            }
                        }
                        let _ = reply.send(Ok(ok));
                    }
                    Some(Command::SendFile { peer_fp, path, reply }) => {
                        let result = do_send_file(&state, &ev_tx, &peer_fp, &path).await;
                        let _ = reply.send(result);
                    }
                }
            }
        }
    }
}

async fn ensure_trusted(state: &Arc<Mutex<RuntimeState>>, fp: &str) -> Result<PeerInfo> {
    let auto = state.lock().config.auto_trust;
    let peer = state
        .lock()
        .peers
        .get_live(fp)
        .cloned()
        .ok_or_else(|| anyhow!("peer not found / offline: {fp}"))?;
    if peer.trust == TrustState::Blocked {
        return Err(anyhow!("peer blocked"));
    }
    if peer.trust != TrustState::Trusted {
        if auto {
            let mut s = state.lock();
            s.peers.set_trust(fp, TrustState::Trusted);
            s.peers.save(&s.paths)?;
            return s
                .peers
                .get_live(fp)
                .cloned()
                .ok_or_else(|| anyhow!("peer vanished"));
        }
        return Err(anyhow!(
            "peer not trusted (fingerprint {}). Use trust command first.",
            peer.fingerprint
        ));
    }
    Ok(peer)
}

async fn take_or_connect_session(
    state: &Arc<Mutex<RuntimeState>>,
    peer_fp: &str,
    addr: std::net::SocketAddr,
) -> Result<SecureSession> {
    if let Some(sess) = state.lock().sessions.remove(peer_fp) {
        return Ok(sess);
    }
    let identity = state.lock().identity.clone();
    let session = SecureSession::connect(&identity, addr).await?;
    anyhow::ensure!(
        session.peer_fp.as_str() == peer_fp,
        "fingerprint mismatch during handshake: got {} expected {}",
        session.peer_fp,
        peer_fp
    );
    Ok(session)
}

fn store_session(state: &Arc<Mutex<RuntimeState>>, peer_fp: &str, session: SecureSession) {
    state.lock().sessions.insert(peer_fp.to_string(), session);
}

async fn do_send_text(
    state: &Arc<Mutex<RuntimeState>>,
    ev_tx: &broadcast::Sender<NodeEvent>,
    peer_fp: &str,
    body: &str,
    is_broadcast: bool,
) -> Result<Uuid> {
    let peer = ensure_trusted(state, peer_fp).await?;
    let identity = state.lock().identity.clone();
    let my_fp = identity.fingerprint().as_str().to_string();
    let my_name = state.lock().config.display_name.clone();

    let mut session = match take_or_connect_session(state, peer_fp, peer.addr).await {
        Ok(s) => s,
        Err(_) => {
            // stale pooled session — force fresh connect
            state.lock().sessions.remove(peer_fp);
            take_or_connect_session(state, peer_fp, peer.addr).await?
        }
    };
    let id = Uuid::new_v4();
    let ts_ms = chrono::Utc::now().timestamp_millis();
    let msg = if is_broadcast {
        WireMessage::BroadcastText {
            id,
            body: body.to_string(),
            ts_ms,
        }
    } else {
        WireMessage::Text {
            id,
            body: body.to_string(),
            ts_ms,
        }
    };
    if let Err(e) = session.send_msg(&msg).await {
        // reconnect once
        state.lock().sessions.remove(peer_fp);
        session = take_or_connect_session(state, peer_fp, peer.addr).await?;
        session.send_msg(&msg).await.map_err(|_| e)?;
    }

    // Wait briefly for delivery receipt (non-broadcast).
    if !is_broadcast {
        match tokio::time::timeout(std::time::Duration::from_secs(2), session.recv_msg()).await {
            Ok(Ok(WireMessage::TextAck { id: ack_id, .. })) if ack_id == id => {
                let _ = ev_tx.send(NodeEvent::TextDelivered {
                    id,
                    peer_fp: peer_fp.to_string(),
                });
            }
            Ok(Ok(other)) => {
                tracing::debug!(?other, "unexpected msg while awaiting TextAck");
            }
            _ => {
                // No ack — still OK; message may have been delivered without ack support.
            }
        }
    }

    let chat = ChatMessage {
        id,
        peer_fp: peer_fp.to_string(),
        peer_name: peer.display_name.clone(),
        direction: MsgDirection::Out,
        kind: if is_broadcast {
            MsgKind::Broadcast
        } else {
            MsgKind::Text
        },
        body: body.to_string(),
        ts_ms,
        file_path: None,
        file_size: None,
        delivered: false,
    };
    let _ = state.lock().history.append(chat);
    let _ = ev_tx.send(NodeEvent::Text {
        from_fp: my_fp,
        from_name: my_name,
        body: body.to_string(),
        id,
        broadcast: is_broadcast,
    });
    store_session(state, peer_fp, session);
    Ok(id)
}

async fn do_pair_with_pin(
    state: &Arc<Mutex<RuntimeState>>,
    ev_tx: &broadcast::Sender<NodeEvent>,
    peer_fp: &str,
    pin: &str,
) -> Result<()> {
    let peer = state
        .lock()
        .peers
        .get_live(peer_fp)
        .cloned()
        .ok_or_else(|| anyhow!("peer not found / offline: {peer_fp}"))?;
    let mut session = take_or_connect_session(state, peer_fp, peer.addr).await?;
    session
        .send_msg(&WireMessage::PinPair {
            pin: pin.to_string(),
        })
        .await?;
    let resp = tokio::time::timeout(std::time::Duration::from_secs(5), session.recv_msg()).await??;
    match resp {
        WireMessage::PinPairAck { ok: true, message } => {
            let mut s = state.lock();
            s.peers.set_trust(peer_fp, TrustState::Trusted);
            s.peers.save(&s.paths)?;
            if let Some(info) = s.peers.get_live(peer_fp).cloned() {
                let _ = ev_tx.send(NodeEvent::PeerUpdated(info));
            }
            let _ = ev_tx.send(NodeEvent::Info(format!("PIN pairing OK: {message}")));
            store_session(state, peer_fp, session);
            Ok(())
        }
        WireMessage::PinPairAck { ok: false, message } => {
            Err(anyhow!("PIN pairing rejected: {message}"))
        }
        other => Err(anyhow!("unexpected pairing response: {other:?}")),
    }
}

async fn do_send_file(
    state: &Arc<Mutex<RuntimeState>>,
    ev_tx: &broadcast::Sender<NodeEvent>,
    peer_fp: &str,
    path: &Path,
) -> Result<FileMeta> {
    let peer = ensure_trusted(state, peer_fp).await?;
    // File transfer uses a dedicated session (not the text pool).
    state.lock().sessions.remove(peer_fp);
    let max_bytes = state.lock().config.max_file_bytes;
    let meta_len = tokio::fs::metadata(path).await?.len();
    anyhow::ensure!(
        meta_len <= max_bytes,
        "file too large: {meta_len} bytes (max {max_bytes})"
    );
    // Notify peer we are about to send
    let mut session = take_or_connect_session(state, peer_fp, peer.addr).await?;
    let _ = session
        .send_msg(&WireMessage::Status {
            kind: PeerStatusKind::ReceivingFile,
            detail: path
                .file_name()
                .and_then(|s| s.to_str())
                .unwrap_or("file")
                .to_string(),
        })
        .await;
    anyhow::ensure!(session.peer_fp.as_str() == peer_fp, "fingerprint mismatch");

    let ev = ev_tx.clone();
    let meta_id = Arc::new(Mutex::new(Uuid::nil()));
    let meta_id2 = meta_id.clone();
    let mut progress = move |received: u64, total: u64| {
        let id = *meta_id2.lock();
        let _ = ev.send(NodeEvent::FileProgress {
            id,
            direction: MsgDirection::Out,
            received,
            total,
        });
    };
    let st_c = state.clone();
    let meta = transfer::send_file(&mut session, path, &mut progress, &mut || {
        let s = st_c.lock();
        !s.cancel_transfers.is_empty()
    })
    .await?;
    *meta_id.lock() = meta.id;

    let chat = ChatMessage {
        id: meta.id,
        peer_fp: peer_fp.to_string(),
        peer_name: peer.display_name,
        direction: MsgDirection::Out,
        kind: if is_image_mime(&meta.mime) {
            MsgKind::Image
        } else {
            MsgKind::File
        },
        body: format!("[文件] {}", meta.name),
        ts_ms: chrono::Utc::now().timestamp_millis(),
        file_path: Some(path.display().to_string()),
        file_size: Some(meta.size),
        delivered: false,
    };
    let _ = state.lock().history.append(chat);
    let _ = ev_tx.send(NodeEvent::TransferComplete {
        id: meta.id,
        name: meta.name.clone(),
        ok: true,
    });
    Ok(meta)
}

async fn handle_incoming(
    state: Arc<Mutex<RuntimeState>>,
    stream: tokio::net::TcpStream,
    addr: std::net::SocketAddr,
    ev_tx: broadcast::Sender<NodeEvent>,
) -> Result<()> {
    let identity = state.lock().identity.clone();
    let mut session = SecureSession::accept(&identity, stream, addr).await?;
    let peer_fp = session.peer_fp.as_str().to_string();

    {
        let mut s = state.lock();
        if s.peers.get_record(&peer_fp).map(|r| r.trust) == Some(TrustState::Blocked) {
            return Err(anyhow!("blocked peer"));
        }
        let trusted = s.peers.is_trusted(&peer_fp) || s.config.auto_trust;
        if !trusted {
            return Err(anyhow!("untrusted peer {peer_fp} attempted connection"));
        }
        if s.config.auto_trust && !s.peers.is_trusted(&peer_fp) {
            s.peers.set_trust(&peer_fp, TrustState::Trusted);
            let _ = s.peers.save(&s.paths);
        }
    }

    let peer_name = state
        .lock()
        .peers
        .get_live(&peer_fp)
        .map(|p| p.display_name.clone())
        .unwrap_or_else(|| peer_fp.clone());

    loop {
        let msg = match session.recv_msg().await {
            Ok(m) => m,
            Err(e) => {
                debug_close(&e);
                break;
            }
        };
        match msg {
            WireMessage::Text { id, body, ts_ms } => {
                record_text(&state, &ev_tx, &peer_fp, &peer_name, id, body, ts_ms, false);
                let _ = session
                    .send_msg(&WireMessage::TextAck {
                        id,
                        ts_ms: chrono::Utc::now().timestamp_millis(),
                    })
                    .await;
            }
            WireMessage::BroadcastText { id, body, ts_ms } => {
                record_text(&state, &ev_tx, &peer_fp, &peer_name, id, body, ts_ms, true);
            }
            WireMessage::FileOffer {
                id,
                name,
                size,
                mime,
                sha256,
                ..
            } => {
                let meta = FileMeta {
                    id,
                    name,
                    size,
                    mime,
                    sha256,
                };
                let downloads = state.lock().downloads.clone();
                let ev = ev_tx.clone();
                let mut progress = move |received: u64, total: u64| {
                    let _ = ev.send(NodeEvent::FileProgress {
                        id,
                        direction: MsgDirection::In,
                        received,
                        total,
                    });
                };
                match transfer::receive_file(
                    &mut session,
                    meta.clone(),
                    &downloads,
                    true,
                    &mut progress,
                )
                .await
                {
                    Ok(path) => {
                        let kind = if is_image_mime(&meta.mime) {
                            MsgKind::Image
                        } else {
                            MsgKind::File
                        };
                        let chat = ChatMessage {
                            id: meta.id,
                            peer_fp: peer_fp.clone(),
                            peer_name: peer_name.clone(),
                            direction: MsgDirection::In,
                            kind,
                            body: format!("[文件] {}", meta.name),
                            ts_ms: chrono::Utc::now().timestamp_millis(),
                            file_path: Some(path.display().to_string()),
                            file_size: Some(meta.size),
        delivered: false,
                        };
                        let _ = state.lock().history.append(chat);
                        let _ = ev_tx.send(NodeEvent::FileReceived {
                            from_fp: peer_fp.clone(),
                            from_name: peer_name.clone(),
                            path,
                            meta,
                        });
                    }
                    Err(e) => {
                        let _ = ev_tx.send(NodeEvent::Error(format!("file recv failed: {e}")));
                        break;
                    }
                }
            }
            WireMessage::PinPair { pin } => {
                let (ok, info_opt) = {
                    let mut s = state.lock();
                    let ok = match &s.pending_pin {
                        Some((expected, at))
                            if at.elapsed() < std::time::Duration::from_secs(300) =>
                        {
                            expected == &pin
                        }
                        _ => false,
                    };
                    let mut info_opt = None;
                    if ok {
                        s.pending_pin = None;
                        s.peers.set_trust(&peer_fp, TrustState::Trusted);
                        let _ = s.peers.save(&s.paths);
                        info_opt = s.peers.get_live(&peer_fp).cloned();
                    }
                    (ok, info_opt)
                };
                if let Some(info) = info_opt {
                    let _ = ev_tx.send(NodeEvent::PeerUpdated(info));
                }
                session
                    .send_msg(&WireMessage::PinPairAck {
                        ok,
                        message: if ok {
                            "trusted via PIN".into()
                        } else {
                            "PIN mismatch or expired".into()
                        },
                    })
                    .await?;
            }
            WireMessage::Status { kind, detail } => {
                let _ = ev_tx.send(NodeEvent::PeerStatus {
                    peer_fp: peer_fp.clone(),
                    kind,
                    detail,
                });
            }
            WireMessage::TextAck { id, .. } => {
                let _ = ev_tx.send(NodeEvent::TextDelivered {
                    id,
                    peer_fp: peer_fp.clone(),
                });
            }
            WireMessage::Ping { ts_ms } => {
                session.send_msg(&WireMessage::Pong { ts_ms }).await?;
            }
            WireMessage::Hello { .. } => {}
            other => {
                tracing::debug!(?other, "unhandled inbound");
            }
        }
    }
    Ok(())
}

fn record_text(
    state: &Arc<Mutex<RuntimeState>>,
    ev_tx: &broadcast::Sender<NodeEvent>,
    peer_fp: &str,
    peer_name: &str,
    id: Uuid,
    body: String,
    ts_ms: i64,
    broadcast: bool,
) {
    let chat = ChatMessage {
        id,
        peer_fp: peer_fp.to_string(),
        peer_name: peer_name.to_string(),
        direction: MsgDirection::In,
        kind: if broadcast {
            MsgKind::Broadcast
        } else {
            MsgKind::Text
        },
        body: body.clone(),
        ts_ms,
        file_path: None,
        file_size: None,
        delivered: false,
    };
    let _ = state.lock().history.append(chat);
    let _ = ev_tx.send(NodeEvent::Text {
        from_fp: peer_fp.to_string(),
        from_name: peer_name.to_string(),
        body,
        id,
        broadcast,
    });
}

fn debug_close(e: &anyhow::Error) {
    let s = e.to_string();
    if !s.contains("early eof")
        && !s.contains("Connection reset")
        && !s.contains("broken pipe")
        && !s.contains("UnexpectedEof")
    {
        warn!(error = %e, "session closed with error");
    }
}
