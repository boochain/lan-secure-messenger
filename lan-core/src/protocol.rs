use serde::{Deserialize, Serialize};
use uuid::Uuid;

/// Application-level messages sent over Noise transport (after handshake).
#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum WireMessage {
    /// Text chat
    Text {
        id: Uuid,
        body: String,
        ts_ms: i64,
    },
    /// Broadcast text (sender already delivered to multiple peers)
    BroadcastText {
        id: Uuid,
        body: String,
        ts_ms: i64,
    },
    /// Offer a file / image
    FileOffer {
        id: Uuid,
        name: String,
        size: u64,
        mime: String,
        sha256: String,
        ts_ms: i64,
    },
    /// Accept / reject file
    FileAccept { id: Uuid, accept: bool },
    /// Chunk of file data
    FileChunk {
        id: Uuid,
        offset: u64,
        data: Vec<u8>,
    },
    /// Receiver ACK of bytes received
    FileAck { id: Uuid, received: u64 },
    /// Transfer complete
    FileDone { id: Uuid, ok: bool },
    /// Keepalive
    Ping { ts_ms: i64 },
    Pong { ts_ms: i64 },
    /// Peer display name update
    Hello {
        display_name: String,
        fingerprint: String,
    },
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ChatMessage {
    pub id: Uuid,
    pub peer_fp: String,
    pub peer_name: String,
    pub direction: MsgDirection,
    pub kind: MsgKind,
    pub body: String,
    pub ts_ms: i64,
    pub file_path: Option<String>,
    pub file_size: Option<u64>,
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq)]
pub enum MsgDirection {
    In,
    Out,
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq)]
pub enum MsgKind {
    Text,
    Broadcast,
    File,
    Image,
    System,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct FileMeta {
    pub id: Uuid,
    pub name: String,
    pub size: u64,
    pub mime: String,
    pub sha256: String,
}

/// UDP discovery announcement (signed with Ed25519).
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DiscoveryAnnounce {
    pub magic: [u8; 4],
    pub version: u16,
    pub display_name: String,
    pub x25519_pk: [u8; 32],
    pub ed25519_pk: [u8; 32],
    pub listen_port: u16,
    pub ts_ms: i64,
    pub nonce: [u8; 16],
    /// Signature over canonical bytes (everything except sig itself).
    pub sig: Vec<u8>,  // 64 bytes
}

impl DiscoveryAnnounce {
    pub fn sign_bytes(&self) -> Vec<u8> {
        let mut v = Vec::new();
        v.extend_from_slice(&self.magic);
        v.extend_from_slice(&self.version.to_le_bytes());
        v.extend_from_slice(self.display_name.as_bytes());
        v.push(0);
        v.extend_from_slice(&self.x25519_pk);
        v.extend_from_slice(&self.ed25519_pk);
        v.extend_from_slice(&self.listen_port.to_le_bytes());
        v.extend_from_slice(&self.ts_ms.to_le_bytes());
        v.extend_from_slice(&self.nonce);
        v
    }
}

/// Length-prefixed frame helpers for TCP.
pub fn encode_frame(payload: &[u8]) -> Vec<u8> {
    let mut out = Vec::with_capacity(4 + payload.len());
    out.extend_from_slice(&(payload.len() as u32).to_be_bytes());
    out.extend_from_slice(payload);
    out
}
