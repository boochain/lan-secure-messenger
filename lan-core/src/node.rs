//! High-level node: discovery + sessions + messaging API.

use crate::config::{AppConfig, ConfigPaths};
use crate::discovery::{AnnounceValidator, DiscoveryService};
use crate::group::{invite_sign_bytes, GroupInfo, GroupStore};
use crate::history::HistoryStore;
use crate::identity::Identity;
use crate::peer::{DiscoveryChange, PeerInfo, PeerStore, TrustState};
use crate::protocol::{ChatScope, BroadcastResult, ChatMessage, FileMeta, MsgDirection, MsgKind, PeerStatusKind, WireMessage};
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
    GroupInviteReceived(GroupInfo),
    GroupUpdated(GroupInfo),
    GroupRemoved { group_id: Uuid, name: String },
    GroupText {
        group_id: Uuid,
        from_fp: String,
        from_name: String,
        body: String,
        id: Uuid,
    },
    GroupFileReceived {
        group_id: Uuid,
        from_fp: String,
        from_name: String,
        path: PathBuf,
        meta: FileMeta,
    },
    Error(String),
    Info(String),
}

enum Command {
    SendText {
        peer_fp: String,
        body: String,
        reply: oneshot::Sender<Result<(Uuid, bool)>>,
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
    CreateGroup {
        name: String,
        member_fps: Vec<String>,
        reply: oneshot::Sender<Result<GroupInfo>>,
    },
    ListGroups {
        reply: oneshot::Sender<Vec<GroupInfo>>,
    },
    SendGroupText {
        group_id: Uuid,
        body: String,
        reply: oneshot::Sender<Result<Uuid>>,
    },
    SendGroupFile {
        group_id: Uuid,
        path: PathBuf,
        reply: oneshot::Sender<Result<FileMeta>>,
    },
    LeaveGroup {
        group_id: Uuid,
        reply: oneshot::Sender<Result<()>>,
    },
    InviteToGroup {
        group_id: Uuid,
        peer_fp: String,
        reply: oneshot::Sender<Result<()>>,
    },
    GroupHistory {
        group_id: Uuid,
        limit: usize,
        reply: oneshot::Sender<Vec<ChatMessage>>,
    },
    DmHistory {
        peer_fp: String,
        limit: usize,
        reply: oneshot::Sender<Vec<ChatMessage>>,
    },
    RemoveGroupMember {
        group_id: Uuid,
        member_fp: String,
        reply: oneshot::Sender<Result<GroupInfo>>,
    },
    SearchHistory {
        query: String,
        limit: usize,
        reply: oneshot::Sender<Vec<ChatMessage>>,
    },
    BackupIdentity {
        path: PathBuf,
        password: String,
        reply: oneshot::Sender<Result<()>>,
    },
    RestoreIdentity {
        path: PathBuf,
        password: String,
        reply: oneshot::Sender<Result<()>>,
    },
    SetDownloadDir {
        path: Option<PathBuf>,
        reply: oneshot::Sender<Result<()>>,
    },
    GetDownloadDir {
        reply: oneshot::Sender<PathBuf>,
    },
    MutePeer {
        peer_fp: String,
        mute: bool,
        reply: oneshot::Sender<Result<()>>,
    },
    MuteGroup {
        group_id: Uuid,
        mute: bool,
        reply: oneshot::Sender<Result<()>>,
    },
    GetConfigSnapshot {
        reply: oneshot::Sender<AppConfig>,
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
    /// Returns `(message_id, delivered)` where `delivered` means TextAck received.
    pub async fn send_text(&self, peer_fp: &str, body: &str) -> Result<(Uuid, bool)> {
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

    pub async fn create_group(&self, name: &str, member_fps: Vec<String>) -> Result<GroupInfo> {
        let (reply, rx) = oneshot::channel();
        self.tx.send(Command::CreateGroup {
            name: name.into(),
            member_fps,
            reply,
        })?;
        rx.await?
    }

    pub async fn list_groups(&self) -> Result<Vec<GroupInfo>> {
        let (reply, rx) = oneshot::channel();
        self.tx.send(Command::ListGroups { reply })?;
        Ok(rx.await?)
    }

    pub async fn send_group_text(&self, group_id: Uuid, body: &str) -> Result<Uuid> {
        let (reply, rx) = oneshot::channel();
        self.tx.send(Command::SendGroupText {
            group_id,
            body: body.into(),
            reply,
        })?;
        rx.await?
    }

    pub async fn send_group_file(&self, group_id: Uuid, path: impl AsRef<Path>) -> Result<FileMeta> {
        let (reply, rx) = oneshot::channel();
        self.tx.send(Command::SendGroupFile {
            group_id,
            path: path.as_ref().to_path_buf(),
            reply,
        })?;
        rx.await?
    }

    pub async fn leave_group(&self, group_id: Uuid) -> Result<()> {
        let (reply, rx) = oneshot::channel();
        self.tx.send(Command::LeaveGroup { group_id, reply })?;
        rx.await?
    }

    pub async fn invite_to_group(&self, group_id: Uuid, peer_fp: &str) -> Result<()> {
        let (reply, rx) = oneshot::channel();
        self.tx.send(Command::InviteToGroup {
            group_id,
            peer_fp: peer_fp.into(),
            reply,
        })?;
        rx.await?
    }

    pub async fn group_history(&self, group_id: Uuid, limit: usize) -> Result<Vec<ChatMessage>> {
        let (reply, rx) = oneshot::channel();
        self.tx.send(Command::GroupHistory {
            group_id,
            limit,
            reply,
        })?;
        Ok(rx.await?)
    }

    pub async fn dm_history(&self, peer_fp: &str, limit: usize) -> Result<Vec<ChatMessage>> {
        let (reply, rx) = oneshot::channel();
        self.tx.send(Command::DmHistory {
            peer_fp: peer_fp.into(),
            limit,
            reply,
        })?;
        Ok(rx.await?)
    }

    pub async fn remove_group_member(&self, group_id: Uuid, member_fp: &str) -> Result<GroupInfo> {
        let (reply, rx) = oneshot::channel();
        self.tx.send(Command::RemoveGroupMember {
            group_id,
            member_fp: member_fp.into(),
            reply,
        })?;
        rx.await?
    }

    pub async fn search_history(&self, query: &str, limit: usize) -> Result<Vec<ChatMessage>> {
        let (reply, rx) = oneshot::channel();
        self.tx.send(Command::SearchHistory {
            query: query.into(),
            limit,
            reply,
        })?;
        Ok(rx.await?)
    }

    pub async fn backup_identity(&self, path: impl AsRef<Path>, password: &str) -> Result<()> {
        let (reply, rx) = oneshot::channel();
        self.tx.send(Command::BackupIdentity {
            path: path.as_ref().to_path_buf(),
            password: password.into(),
            reply,
        })?;
        rx.await?
    }

    pub async fn restore_identity(&self, path: impl AsRef<Path>, password: &str) -> Result<()> {
        let (reply, rx) = oneshot::channel();
        self.tx.send(Command::RestoreIdentity {
            path: path.as_ref().to_path_buf(),
            password: password.into(),
            reply,
        })?;
        rx.await?
    }

    pub async fn set_download_dir(&self, path: Option<PathBuf>) -> Result<()> {
        let (reply, rx) = oneshot::channel();
        self.tx.send(Command::SetDownloadDir { path, reply })?;
        rx.await?
    }

    pub async fn get_download_dir(&self) -> Result<PathBuf> {
        let (reply, rx) = oneshot::channel();
        self.tx.send(Command::GetDownloadDir { reply })?;
        Ok(rx.await?)
    }

    pub async fn mute_peer(&self, peer_fp: &str, mute: bool) -> Result<()> {
        let (reply, rx) = oneshot::channel();
        self.tx.send(Command::MutePeer {
            peer_fp: peer_fp.into(),
            mute,
            reply,
        })?;
        rx.await?
    }

    pub async fn mute_group(&self, group_id: Uuid, mute: bool) -> Result<()> {
        let (reply, rx) = oneshot::channel();
        self.tx.send(Command::MuteGroup {
            group_id,
            mute,
            reply,
        })?;
        rx.await?
    }

    pub async fn config_snapshot(&self) -> Result<AppConfig> {
        let (reply, rx) = oneshot::channel();
        self.tx.send(Command::GetConfigSnapshot { reply })?;
        Ok(rx.await?)
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
    groups: GroupStore,
    downloads: PathBuf,
}

impl Node {
    pub fn open(paths: ConfigPaths, config: AppConfig) -> Result<Self> {
        paths.ensure_dirs()?;
        let identity = Identity::load_or_create(&paths)?;
        let peers = PeerStore::load(&paths)?;
        let history = HistoryStore::load(&paths, config.history_enabled)?;
        let groups = GroupStore::load(&paths)?;
        let downloads = paths.root.join("downloads");
        std::fs::create_dir_all(&downloads)?;
        config.save(&paths)?;
        Ok(Self {
            paths,
            config,
            identity,
            peers,
            history,
            groups,
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
            groups: self.groups,
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
    groups: GroupStore,
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
                    Some(Command::CreateGroup { name, member_fps, reply }) => {
                        let result = do_create_group(&state, &ev_tx, &name, member_fps).await;
                        let _ = reply.send(result);
                    }
                    Some(Command::ListGroups { reply }) => {
                        let list = state.lock().groups.list();
                        let _ = reply.send(list);
                    }
                    Some(Command::SendGroupText { group_id, body, reply }) => {
                        let result = do_send_group_text(&state, &ev_tx, group_id, &body).await;
                        let _ = reply.send(result);
                    }
                    Some(Command::SendGroupFile { group_id, path, reply }) => {
                        let result = do_send_group_file(&state, &ev_tx, group_id, &path).await;
                        let _ = reply.send(result);
                    }
                    Some(Command::LeaveGroup { group_id, reply }) => {
                        let result = do_leave_group(&state, &ev_tx, group_id).await;
                        let _ = reply.send(result);
                    }
                    Some(Command::InviteToGroup { group_id, peer_fp, reply }) => {
                        let result = do_invite_to_group(&state, &ev_tx, group_id, &peer_fp).await;
                        let _ = reply.send(result);
                    }
                    Some(Command::GroupHistory { group_id, limit, reply }) => {
                        let msgs = state.lock().history.list_group(&group_id, limit);
                        let _ = reply.send(msgs);
                    }
                    Some(Command::DmHistory { peer_fp, limit, reply }) => {
                        let msgs = state.lock().history.list_dm(&peer_fp, limit);
                        let _ = reply.send(msgs);
                    }
                                        Some(Command::RemoveGroupMember { group_id, member_fp, reply }) => {
                        let result = do_remove_group_member(&state, &ev_tx, group_id, &member_fp).await;
                        let _ = reply.send(result);
                    }
                    Some(Command::SearchHistory { query, limit, reply }) => {
                        let msgs = state.lock().history.search(&query, limit);
                        let _ = reply.send(msgs);
                    }
                    Some(Command::BackupIdentity { path, password, reply }) => {
                        let paths = state.lock().paths.clone();
                        let result = crate::backup::export_backup(&paths, &password, &path);
                        let _ = reply.send(result);
                    }
                    Some(Command::RestoreIdentity { path, password, reply }) => {
                        let paths = state.lock().paths.clone();
                        let result = crate::backup::import_backup(&paths, &password, &path);
                        if result.is_ok() {
                            let _ = ev_tx.send(NodeEvent::Info(
                                "身份已恢复，请重启应用以加载新密钥".into(),
                            ));
                        }
                        let _ = reply.send(result);
                    }
                    Some(Command::SetDownloadDir { path, reply }) => {
                        let result = (|| {
                            let mut s = state.lock();
                            if let Some(ref p) = path {
                                std::fs::create_dir_all(p)?;
                                s.downloads = p.clone();
                                s.config.download_dir = Some(p.to_string_lossy().into_owned());
                            } else {
                                let d = s.paths.root.join("downloads");
                                std::fs::create_dir_all(&d)?;
                                s.downloads = d;
                                s.config.download_dir = None;
                            }
                            s.config.save(&s.paths)?;
                            Ok(())
                        })();
                        let _ = reply.send(result);
                    }
                    Some(Command::GetDownloadDir { reply }) => {
                        let d = state.lock().downloads.clone();
                        let _ = reply.send(d);
                    }
                    Some(Command::MutePeer { peer_fp, mute, reply }) => {
                        let result = (|| {
                            let mut s = state.lock();
                            if mute {
                                if !s.config.muted_peers.contains(&peer_fp) {
                                    s.config.muted_peers.push(peer_fp);
                                }
                            } else {
                                s.config.muted_peers.retain(|x| x != &peer_fp);
                            }
                            s.config.save(&s.paths)
                        })();
                        let _ = reply.send(result);
                    }
                    Some(Command::MuteGroup { group_id, mute, reply }) => {
                        let result = (|| {
                            let mut s = state.lock();
                            let id = group_id.to_string();
                            if mute {
                                if !s.config.muted_groups.contains(&id) {
                                    s.config.muted_groups.push(id);
                                }
                            } else {
                                s.config.muted_groups.retain(|x| x != &id);
                            }
                            s.config.save(&s.paths)
                        })();
                        let _ = reply.send(result);
                    }
                    Some(Command::GetConfigSnapshot { reply }) => {
                        let cfg = state.lock().config.clone();
                        let _ = reply.send(cfg);
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
                        let retries = state.lock().config.transfer_retries.max(1);
                        let mut result = Err(anyhow!("send_file: no attempt"));
                        for attempt in 1..=retries {
                            result = do_send_file(&state, &ev_tx, &peer_fp, &path).await;
                            if result.is_ok() {
                                break;
                            }
                            let err = result.as_ref().err().map(|e| e.to_string()).unwrap_or_default();
                            let _ = ev_tx.send(NodeEvent::Info(format!(
                                "文件发送失败({attempt}/{retries}): {err}"
                            )));
                            if attempt < retries {
                                tokio::time::sleep(Duration::from_millis(400 * attempt as u64)).await;
                            }
                        }
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
) -> Result<(Uuid, bool)> {
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

    // Wait for delivery receipt (non-broadcast). Loop so Ping/Pong/other
    // frames don't drop the TextAck (common race with pooled sessions).
    let mut delivered = false;
    if !is_broadcast {
        let deadline = tokio::time::Instant::now() + std::time::Duration::from_secs(5);
        while tokio::time::Instant::now() < deadline {
            let left = deadline.saturating_duration_since(tokio::time::Instant::now());
            match tokio::time::timeout(left, session.recv_msg()).await {
                Ok(Ok(WireMessage::TextAck { id: ack_id, .. })) if ack_id == id => {
                    delivered = true;
                    let _ = ev_tx.send(NodeEvent::TextDelivered {
                        id,
                        peer_fp: peer_fp.to_string(),
                    });
                    break;
                }
                Ok(Ok(WireMessage::Ping { ts_ms })) => {
                    let _ = session.send_msg(&WireMessage::Pong { ts_ms }).await;
                }
                Ok(Ok(WireMessage::Pong { .. })) => {}
                Ok(Ok(other)) => {
                    tracing::debug!(?other, "skipping non-ack while awaiting TextAck");
                }
                Ok(Err(e)) => {
                    tracing::debug!(error = %e, "recv error while awaiting TextAck");
                    break;
                }
                Err(_) => break, // timeout
            }
        }
        if !delivered {
            tracing::debug!(%id, peer = %peer_fp, "TextAck not received within timeout");
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
        delivered,
        group_id: None,
        chat_scope: if is_broadcast {
            crate::protocol::ChatScope::Broadcast
        } else {
            crate::protocol::ChatScope::Direct
        },
    };
    {
        let mut s = state.lock();
        let _ = s.history.append(chat);
        if delivered {
            let _ = s.history.mark_delivered(id);
        }
    }
    let _ = ev_tx.send(NodeEvent::Text {
        from_fp: my_fp,
        from_name: my_name,
        body: body.to_string(),
        id,
        broadcast: is_broadcast,
    });
    store_session(state, peer_fp, session);
    Ok((id, delivered))
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
        group_id: None,
        chat_scope: crate::protocol::ChatScope::Direct,
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
        group_id: None,
        chat_scope: crate::protocol::ChatScope::Direct,
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
                let _ = state.lock().history.mark_delivered(id);
                let _ = ev_tx.send(NodeEvent::TextDelivered {
                    id,
                    peer_fp: peer_fp.clone(),
                });
            }
            WireMessage::Ping { ts_ms } => {
                session.send_msg(&WireMessage::Pong { ts_ms }).await?;
            }
            
            WireMessage::GroupInvite {
                group_id,
                name,
                creator_fp,
                members,
                group_key,
                created_ms,
                join_code,
                creator_sig,
            } => {
                // Verify signature if we know creator's ed25519 key
                let verified = {
                    let s = state.lock();
                    if let Some(rec) = s.peers.get_record(&creator_fp) {
                        match ed25519_dalek::VerifyingKey::from_bytes(&rec.ed25519_pk) {
                            Ok(vk) => {
                                let bytes = invite_sign_bytes(
                                    &group_id,
                                    &name,
                                    &creator_fp,
                                    &members,
                                    &group_key,
                                    created_ms,
                                );
                                if creator_sig.len() == 64 {
                                    let mut sig = [0u8; 64];
                                    sig.copy_from_slice(&creator_sig);
                                    Identity::verify(&vk, &bytes, &sig)
                                } else {
                                    false
                                }
                            }
                            Err(_) => false,
                        }
                    } else {
                        // Unknown creator key — accept but mark in info (LAN MVP)
                        true
                    }
                };
                if !verified {
                    let _ = ev_tx.send(NodeEvent::Error(format!(
                        "群邀请签名校验失败: {name} from {creator_fp}"
                    )));
                } else {
                    let info = GroupInfo {
                        id: group_id,
                        name: name.clone(),
                        creator_fp: creator_fp.clone(),
                        members,
                        group_key_hex: hex::encode(group_key),
                        created_ms,
                        join_code,
                        key_epoch: 0,
                    };
                    let _ = state.lock().groups.upsert(info.clone());
                    let _ = ev_tx.send(NodeEvent::GroupInviteReceived(info));
                    let _ = ev_tx.send(NodeEvent::Info(format!("收到群邀请: {name}")));
                }
            }
            WireMessage::GroupInviteAck { group_id, accept } => {
                let _ = ev_tx.send(NodeEvent::Info(format!(
                    "群 {group_id} 邀请响应: {}",
                    if accept { "接受" } else { "拒绝" }
                )));
            }
            WireMessage::GroupText {
                group_id,
                id,
                body,
                ts_ms,
                sender_fp,
                sender_name,
            } => {
                let chat = ChatMessage {
                    id,
                    peer_fp: sender_fp.clone(),
                    peer_name: sender_name.clone(),
                    direction: MsgDirection::In,
                    kind: MsgKind::Text,
                    body: body.clone(),
                    ts_ms,
                    file_path: None,
                    file_size: None,
                    delivered: false,
                    group_id: Some(group_id),
                    chat_scope: ChatScope::Group,
                };
                let _ = state.lock().history.append(chat);
                let _ = ev_tx.send(NodeEvent::GroupText {
                    group_id,
                    from_fp: sender_fp,
                    from_name: sender_name,
                    body,
                    id,
                });
            }
            WireMessage::GroupFileOffer { .. } => {
                // Metadata only — actual file arrives as normal FileOffer on same/other session
                let _ = ev_tx.send(NodeEvent::Info("收到群文件通知".into()));
            }
            WireMessage::GroupLeave { group_id, sender_fp } => {
                if let Some(g) = state.lock().groups.get_mut(&group_id) {
                    g.members.retain(|m| m != &sender_fp);
                    let name = g.name.clone();
                    let _ = state.lock().groups.save();
                    let _ = ev_tx.send(NodeEvent::Info(format!(
                        "{sender_fp} 离开了群 {name}"
                    )));
                }
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
        group_id: None,
        chat_scope: crate::protocol::ChatScope::Broadcast,
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


async fn do_create_group(
    state: &Arc<Mutex<RuntimeState>>,
    ev_tx: &broadcast::Sender<NodeEvent>,
    name: &str,
    member_fps: Vec<String>,
) -> Result<GroupInfo> {
    let (my_fp, identity) = {
        let s = state.lock();
        (s.identity.fingerprint().as_str().to_string(), s.identity.clone())
    };
    // Only invite trusted peers
    let mut members = Vec::new();
    {
        let s = state.lock();
        for fp in &member_fps {
            if s.peers.is_trusted(fp) {
                members.push(fp.clone());
            }
        }
    }
    let info = {
        let mut s = state.lock();
        s.groups.create(name, &my_fp, members.clone(), &identity)?
    };
    let _ = ev_tx.send(NodeEvent::GroupUpdated(info.clone()));
    // Send invites
    for fp in &info.members {
        if fp == &my_fp {
            continue;
        }
        if let Err(e) = send_group_invite(state, &info, fp).await {
            let _ = ev_tx.send(NodeEvent::Error(format!("invite {fp} failed: {e}")));
        }
    }
    let _ = ev_tx.send(NodeEvent::Info(format!(
        "群组已创建: {} ({}) 邀请码 {}",
        info.name, info.id, info.join_code
    )));
    Ok(info)
}

async fn send_group_invite(
    state: &Arc<Mutex<RuntimeState>>,
    info: &GroupInfo,
    peer_fp: &str,
) -> Result<()> {
    let peer = state
        .lock()
        .peers
        .get_live(peer_fp)
        .cloned()
        .ok_or_else(|| anyhow!("peer offline: {peer_fp}"))?;
    let identity = state.lock().identity.clone();
    let key = info.group_key()?;
    let sign_bytes = invite_sign_bytes(
        &info.id,
        &info.name,
        &info.creator_fp,
        &info.members,
        &key,
        info.created_ms,
    );
    let sig = identity.sign(&sign_bytes).to_vec();
    let mut session = take_or_connect_session(state, peer_fp, peer.addr).await?;
    session
        .send_msg(&WireMessage::GroupInvite {
            group_id: info.id,
            name: info.name.clone(),
            creator_fp: info.creator_fp.clone(),
            members: info.members.clone(),
            group_key: key,
            created_ms: info.created_ms,
            join_code: info.join_code.clone(),
            creator_sig: sig,
        })
        .await?;
    store_session(state, peer_fp, session);
    Ok(())
}

async fn do_invite_to_group(
    state: &Arc<Mutex<RuntimeState>>,
    ev_tx: &broadcast::Sender<NodeEvent>,
    group_id: Uuid,
    peer_fp: &str,
) -> Result<()> {
    if !state.lock().peers.is_trusted(peer_fp) {
        anyhow::bail!("只能邀请已信任的对等端");
    }
    let info = {
        let mut s = state.lock();
        let g = s
            .groups
            .get_mut(&group_id)
            .ok_or_else(|| anyhow!("群组不存在"))?;
        if !g.members.iter().any(|m| m == peer_fp) {
            g.members.push(peer_fp.to_string());
            g.members.sort();
            g.members.dedup();
        }
        let info = g.clone();
        s.groups.upsert(info.clone())?;
        info
    };
    send_group_invite(state, &info, peer_fp).await?;
    let _ = ev_tx.send(NodeEvent::GroupUpdated(info));
    Ok(())
}

async fn do_send_group_text(
    state: &Arc<Mutex<RuntimeState>>,
    ev_tx: &broadcast::Sender<NodeEvent>,
    group_id: Uuid,
    body: &str,
) -> Result<Uuid> {
    let (info, my_fp, my_name) = {
        let s = state.lock();
        let info = s
            .groups
            .get(&group_id)
            .cloned()
            .ok_or_else(|| anyhow!("群组不存在"))?;
        let my_fp = s.identity.fingerprint().as_str().to_string();
        anyhow::ensure!(info.is_member(&my_fp), "你不在该群组中");
        (info, my_fp, s.config.display_name.clone())
    };
    let id = Uuid::new_v4();
    let ts_ms = chrono::Utc::now().timestamp_millis();
    let msg = WireMessage::GroupText {
        group_id,
        id,
        body: body.to_string(),
        ts_ms,
        sender_fp: my_fp.clone(),
        sender_name: my_name.clone(),
    };
    let mut ok = 0usize;
    for fp in &info.members {
        if fp == &my_fp {
            continue;
        }
        let Some(peer) = state.lock().peers.get_live(fp).cloned() else {
            continue;
        };
        if !peer.online {
            continue;
        }
        match take_or_connect_session(state, fp, peer.addr).await {
            Ok(mut session) => {
                if session.send_msg(&msg).await.is_ok() {
                    ok += 1;
                    store_session(state, fp, session);
                }
            }
            Err(e) => {
                let _ = ev_tx.send(NodeEvent::Error(format!("群消息发给 {fp} 失败: {e}")));
            }
        }
    }
    let chat = ChatMessage {
        id,
        peer_fp: my_fp.clone(),
        peer_name: my_name.clone(),
        direction: MsgDirection::Out,
        kind: MsgKind::Text,
        body: body.to_string(),
        ts_ms,
        file_path: None,
        file_size: None,
        delivered: false,
        group_id: Some(group_id),
        chat_scope: ChatScope::Group,
    };
    let _ = state.lock().history.append(chat);
    let _ = ev_tx.send(NodeEvent::GroupText {
        group_id,
        from_fp: my_fp,
        from_name: my_name,
        body: body.to_string(),
        id,
    });
    let _ = ev_tx.send(NodeEvent::Info(format!("群消息已送达 {ok} 个在线成员")));
    Ok(id)
}

async fn do_send_group_file(
    state: &Arc<Mutex<RuntimeState>>,
    ev_tx: &broadcast::Sender<NodeEvent>,
    group_id: Uuid,
    path: &Path,
) -> Result<FileMeta> {
    let (info, my_fp, my_name) = {
        let s = state.lock();
        let info = s
            .groups
            .get(&group_id)
            .cloned()
            .ok_or_else(|| anyhow!("群组不存在"))?;
        let my_fp = s.identity.fingerprint().as_str().to_string();
        anyhow::ensure!(info.is_member(&my_fp), "你不在该群组中");
        (info, my_fp, s.config.display_name.clone())
    };
    // Send as normal file to each online member (reuse transfer stack), plus GroupFileOffer notify
    let mut last_meta = None;
    for fp in &info.members {
        if fp == &my_fp {
            continue;
        }
        let Some(peer) = state.lock().peers.get_live(fp).cloned() else { continue };
        if !peer.online || peer.trust != TrustState::Trusted {
            continue;
        }
        match do_send_file(state, ev_tx, fp, path).await {
            Ok(meta) => {
                last_meta = Some(meta);
            }
            Err(e) => {
                let _ = ev_tx.send(NodeEvent::Error(format!("群文件发给 {fp} 失败: {e}")));
            }
        }
    }
    let meta = last_meta.ok_or_else(|| anyhow!("没有在线成员可接收文件"))?;
    let chat = ChatMessage {
        id: meta.id,
        peer_fp: my_fp,
        peer_name: my_name,
        direction: MsgDirection::Out,
        kind: if meta.mime.starts_with("image/") {
            MsgKind::Image
        } else {
            MsgKind::File
        },
        body: format!("[群文件] {}", meta.name),
        ts_ms: chrono::Utc::now().timestamp_millis(),
        file_path: Some(path.display().to_string()),
        file_size: Some(meta.size),
        delivered: false,
        group_id: Some(group_id),
        chat_scope: ChatScope::Group,
    };
    let _ = state.lock().history.append(chat);
    Ok(meta)
}

async fn do_leave_group(
    state: &Arc<Mutex<RuntimeState>>,
    ev_tx: &broadcast::Sender<NodeEvent>,
    group_id: Uuid,
) -> Result<()> {
    let (info, my_fp) = {
        let mut s = state.lock();
        let my_fp = s.identity.fingerprint().as_str().to_string();
        let info = s.groups.leave(&group_id, &my_fp)?;
        (info, my_fp)
    };
    let msg = WireMessage::GroupLeave {
        group_id,
        sender_fp: my_fp,
    };
    for fp in &info.members {
        let peer = state.lock().peers.get_live(fp).cloned();
        let Some(peer) = peer else { continue };
        if let Ok(mut session) = take_or_connect_session(state, fp, peer.addr).await {
            let _ = session.send_msg(&msg).await;
            store_session(state, fp, session);
        }
    }
    let _ = ev_tx.send(NodeEvent::GroupRemoved {
        group_id,
        name: info.name,
    });
    Ok(())
}

async fn do_remove_group_member(
    state: &Arc<Mutex<RuntimeState>>,
    ev_tx: &broadcast::Sender<NodeEvent>,
    group_id: Uuid,
    member_fp: &str,
) -> Result<GroupInfo> {
    let (my_fp, identity, info) = {
        let mut s = state.lock();
        let my_fp = s.identity.fingerprint().as_str().to_string();
        let identity = s.identity.clone();
        let info = s.groups.remove_and_rekey(&group_id, &my_fp, member_fp)?;
        (my_fp, identity, info)
    };
    let key = info.group_key()?;
    let sign = crate::group::rekey_sign_bytes(
        &info.id,
        &info.members,
        &key,
        info.key_epoch,
        member_fp,
    );
    let sig = identity.sign(&sign).to_vec();

    // Notify removed member (best-effort) — drop locks before await
    let removed_addr = {
        let s = state.lock();
        s.peers
            .get_live(member_fp)
            .filter(|p| p.online)
            .map(|p| p.addr)
    };
    if let Some(addr) = removed_addr {
        if let Ok(mut session) = take_or_connect_session(state, member_fp, addr).await {
            let _ = session
                .send_msg(&WireMessage::GroupMemberRemoved {
                    group_id,
                    removed_fp: member_fp.to_string(),
                    by_fp: my_fp.clone(),
                })
                .await;
            store_session(state, member_fp, session);
        }
    }

    // Distribute new key to remaining members
    let targets: Vec<(String, std::net::SocketAddr)> = {
        let s = state.lock();
        info.members
            .iter()
            .filter(|fp| fp.as_str() != my_fp.as_str())
            .filter_map(|fp| {
                s.peers
                    .get_live(fp)
                    .filter(|p| p.online)
                    .map(|p| (fp.clone(), p.addr))
            })
            .collect()
    };
    for (fp, addr) in targets {
        match take_or_connect_session(state, &fp, addr).await {
            Ok(mut session) => {
                if let Err(e) = session
                    .send_msg(&WireMessage::GroupRekey {
                        group_id,
                        members: info.members.clone(),
                        group_key: key,
                        key_epoch: info.key_epoch,
                        removed_fp: member_fp.to_string(),
                        creator_sig: sig.clone(),
                    })
                    .await
                {
                    let _ = ev_tx.send(NodeEvent::Error(format!("rekey to {fp} failed: {e}")));
                } else {
                    store_session(state, &fp, session);
                }
            }
            Err(e) => {
                let _ = ev_tx.send(NodeEvent::Error(format!("rekey connect {fp}: {e}")));
            }
        }
    }

    let _ = ev_tx.send(NodeEvent::GroupUpdated(info.clone()));
    let _ = ev_tx.send(NodeEvent::Info(format!(
        "已移除成员并轮换群密钥 epoch={}",
        info.key_epoch
    )));
    Ok(info)
}
