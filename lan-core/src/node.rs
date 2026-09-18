//! High-level node: discovery + sessions + messaging API.

use crate::config::{AppConfig, ConfigPaths};
use crate::discovery::{AnnounceValidator, DiscoveryService};
use crate::history::HistoryStore;
use crate::identity::Identity;
use crate::peer::{PeerInfo, PeerStore, TrustState};
use crate::protocol::{ChatMessage, FileMeta, MsgDirection, MsgKind, WireMessage};
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
                                let (info, is_new) = {
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
                                let _ = ev_tx.send(NodeEvent::PeerOnline(info));
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
                        s.peers.set_trust(&fp, trust);
                        let res = s.peers.save(&s.paths);
                        if let Some(info) = s.peers.get_live(&fp).cloned() {
                            let _ = ev_tx.send(NodeEvent::PeerUpdated(info));
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

    let mut session = SecureSession::connect(&identity, peer.addr).await?;
    anyhow::ensure!(
        session.peer_fp.as_str() == peer_fp,
        "fingerprint mismatch during handshake: got {} expected {}",
        session.peer_fp,
        peer_fp
    );
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
    session.send_msg(&msg).await?;

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
    };
    let _ = state.lock().history.append(chat);
    let _ = ev_tx.send(NodeEvent::Text {
        from_fp: my_fp,
        from_name: my_name,
        body: body.to_string(),
        id,
        broadcast: is_broadcast,
    });
    Ok(id)
}

async fn do_send_file(
    state: &Arc<Mutex<RuntimeState>>,
    ev_tx: &broadcast::Sender<NodeEvent>,
    peer_fp: &str,
    path: &Path,
) -> Result<FileMeta> {
    let peer = ensure_trusted(state, peer_fp).await?;
    let identity = state.lock().identity.clone();
    let mut session = SecureSession::connect(&identity, peer.addr).await?;
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
    let meta = transfer::send_file(&mut session, path, &mut progress).await?;
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
