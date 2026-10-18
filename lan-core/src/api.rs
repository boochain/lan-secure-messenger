//! Data model exposed over the local IPC API (JSON, snake_case). See docs/ipc.md.

use crate::config::Settings;
use crate::contacts::Trust;
use crate::discovery::DiscoStatus;
use crate::firewall::FirewallStatus;
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Direction {
    In,
    Out,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum MsgStatus {
    /// Waiting for the peer to come online (will be sent automatically).
    Queued,
    /// Written to the peer's connection, not yet acknowledged.
    Sent,
    /// Peer acknowledged receipt.
    Delivered,
    /// Peer has shown the message.
    Read,
    Failed,
    /// Incoming message.
    Received,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum MsgKind {
    Text,
    File,
    System,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct FileRef {
    pub transfer_id: String,
    pub name: String,
    pub size: u64,
    pub mime: String,
    /// Local path (sender: source file; receiver: downloaded file once complete).
    pub path: Option<String>,
    pub is_image: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Message {
    pub id: String,
    /// `dm:<fingerprint>` or `group:<uuid>`
    pub conversation: String,
    pub direction: Direction,
    pub sender_fp: String,
    pub sender_name: String,
    pub kind: MsgKind,
    pub body: String,
    pub ts_ms: i64,
    pub status: MsgStatus,
    #[serde(default)]
    pub file: Option<FileRef>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ConnState {
    Connected,
    Connecting,
    Offline,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Peer {
    pub fingerprint: String,
    pub name: String,
    pub trust: Trust,
    pub online: bool,
    pub state: ConnState,
    pub addr: Option<String>,
    /// "outbound" (we dialed) / "inbound" (they dialed)
    pub direction: Option<String>,
    pub last_seen_ms: i64,
    pub muted: bool,
    pub unread: u32,
    pub rtt_ms: Option<u32>,
    pub app_version: String,
    /// Connected through the relay server (UI badge 经服务器 / tooltip 经服务器中转).
    #[serde(default)]
    pub via_relay: bool,
}

/// Relay client state. state: off | connecting | online | auth_failed | error
#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct RelayStatus {
    pub enabled: bool,
    pub state: String,
    pub detail: String,
    pub server: String,
    /// Peers currently present on the relay server.
    pub peers: u32,
    /// Live links carried by the relay.
    pub relayed_links: u32,
    /// A passphrase is configured (the passphrase itself is never returned over IPC).
    #[serde(default)]
    pub has_passphrase: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct GroupMember {
    pub fingerprint: String,
    pub name: String,
    pub online: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Group {
    pub id: String,
    pub name: String,
    pub creator_fp: String,
    pub is_creator: bool,
    pub members: Vec<GroupMember>,
    pub key_epoch: u64,
    pub muted: bool,
    pub unread: u32,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum TransferState {
    /// Waiting for the receiver to accept.
    Offered,
    Active,
    /// Waiting for receiver's hash verification.
    Verifying,
    Done,
    Failed,
    Cancelled,
    Rejected,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Transfer {
    pub id: String,
    pub message_id: String,
    pub conversation: String,
    pub peer_fp: String,
    pub peer_name: String,
    pub direction: Direction,
    pub name: String,
    pub size: u64,
    pub transferred: u64,
    pub state: TransferState,
    pub mime: String,
    pub path: Option<String>,
    pub error: String,
    pub bytes_per_sec: u64,
    pub started_ms: i64,
    pub retryable: bool,
    /// Byte offset the last (re)start continued from (0 = from the beginning). Resume = > 0.
    #[serde(default)]
    pub resumed_from: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Conversation {
    pub id: String,
    /// "dm" | "group"
    pub kind: String,
    pub title: String,
    pub last_body: String,
    pub last_ts_ms: i64,
    pub unread: u32,
    pub muted: bool,
    pub online: bool,
    /// Last-message details for the chat-list row (receipt ticks, group sender prefix, file/image icon).
    /// Additive: older UIs ignore them, older daemons omit them.
    #[serde(default)]
    pub last_out: bool,
    #[serde(default)]
    pub last_status: Option<MsgStatus>,
    #[serde(default)]
    pub last_sender: String,
    #[serde(default)]
    pub last_kind: Option<MsgKind>,
    #[serde(default)]
    pub last_file: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Me {
    pub fingerprint: String,
    pub name: String,
    pub version: String,
    pub listen_port: u16,
    pub discovery_port: u16,
    pub ipc_endpoint: String,
    pub data_dir: String,
    pub log_dir: String,
    pub download_dir: String,
    pub first_run_done: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Snapshot {
    pub me: Me,
    pub peers: Vec<Peer>,
    pub groups: Vec<Group>,
    pub conversations: Vec<Conversation>,
    pub transfers: Vec<Transfer>,
    pub settings: Settings,
    #[serde(default)]
    pub relay: RelayStatus,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PeerDiag {
    pub fingerprint: String,
    pub name: String,
    pub state: ConnState,
    pub addr: Option<String>,
    pub direction: Option<String>,
    pub connected_for_ms: Option<i64>,
    pub last_rx_ms_ago: Option<i64>,
    pub rtt_ms: Option<u32>,
    pub dial_failures: u32,
    pub last_error: String,
    pub next_retry_in_ms: Option<i64>,
    pub candidates: Vec<String>,
    pub sources: Vec<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Diagnostics {
    pub version: String,
    pub os: String,
    pub tcp_listen: String,
    pub tcp_error: String,
    pub discovery: DiscoStatus,
    pub firewall: FirewallStatus,
    pub peers: Vec<PeerDiag>,
    pub manual_peers: Vec<String>,
    pub log_dir: String,
    pub config_dir: String,
    pub ipc_endpoint: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(tag = "type", rename_all = "snake_case")]
pub enum Event {
    PeerUpdated { peer: Peer },
    PeerRemoved { fingerprint: String },
    MessageAdded { message: Message },
    MessageUpdated { message: Message },
    TransferUpdated { transfer: Transfer },
    GroupUpdated { group: Group },
    GroupRemoved { id: String },
    ConversationUpdated { conversation: Conversation },
    MeUpdated { me: Me },
    SettingsUpdated { settings: Settings },
    RelayUpdated { relay: RelayStatus },
    /// Human-readable notice for toasts / status bar. level: info|warn|error
    Notice { level: String, text: String },
    /// Incoming message worth a toast (not muted, not own).
    Notify { conversation: String, title: String, body: String },
}

pub fn dm_conv(fp: &str) -> String {
    format!("dm:{fp}")
}
pub fn group_conv(id: &uuid::Uuid) -> String {
    format!("group:{id}")
}
