//! Wire formats.
//!
//! * UDP announce: `MAGIC | bincode(SignedAnnounce)`; Ed25519-signed, never trusted for
//!   presence by itself — it only tells us *where to dial*.
//! * TCP link: `u32-be length | Noise(bincode(Frame))`, one long-lived connection per peer.

use serde::{Deserialize, Serialize};
use uuid::Uuid;

pub const ANNOUNCE_MAGIC: [u8; 4] = *b"LSM5";
pub const PROTO_VERSION: u16 = 5;
/// Capability: byte-offset file resume (`Frame::FileResume`), automatic re-offer after reconnect.
pub const CAP_RESUME: u32 = 1;
/// Capability: profile sync (`Frame::Profile`: signature + small avatar). Older peers never receive it.
pub const CAP_PROFILE: u32 = 2;
pub const MY_CAPS: u32 = CAP_RESUME | CAP_PROFILE;
/// Max avatar bytes carried in `Frame::Profile` (UIs downscale to ~96 px PNG/JPEG first).
pub const MAX_AVATAR: usize = 48 * 1024;
/// Max signature (个性签名) length in chars.
pub const MAX_SIGNATURE: usize = 120;
/// File chunk payload size (Noise messages are limited to 65535 bytes).
pub const CHUNK: usize = 32 * 1024;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Announce {
    pub proto: u16,
    pub x25519_pk: [u8; 32],
    pub ed25519_pk: [u8; 32],
    pub name: String,
    pub tcp_port: u16,
    /// Sender's UDP discovery port (target for unicast replies).
    pub disco_port: u16,
    pub nonce: u64,
    pub ts_ms: i64,
    /// Sender asks recipients to unicast an announce back (first contact).
    pub want_reply: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SignedAnnounce {
    pub body: Vec<u8>,
    pub sig: Vec<u8>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Hello {
    pub proto: u16,
    pub app_version: String,
    pub name: String,
    pub ed25519_pk: [u8; 32],
    /// Our TCP listen port, so the peer can redial us later.
    pub listen_port: u16,
}

/// Reliable payloads: always carried in `Frame::Msg`, acknowledged with `Frame::Ack`,
/// retried on reconnect, de-duplicated by id on receipt.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum Payload {
    Text { body: String, ts_ms: i64 },
    /// Body sealed with the current group key (ChaCha20-Poly1305), on top of the Noise link.
    GroupText { group_id: Uuid, key_epoch: u64, sealed: Vec<u8>, ts_ms: i64 },
    GroupInvite {
        group_id: Uuid,
        name: String,
        creator_fp: String,
        members: Vec<String>,
        group_key: [u8; 32],
        created_ms: i64,
        key_epoch: u64,
        sig: Vec<u8>,
    },
    GroupRekey {
        group_id: Uuid,
        members: Vec<String>,
        group_key: [u8; 32],
        key_epoch: u64,
        removed_fp: String,
        sig: Vec<u8>,
    },
    GroupLeave { group_id: Uuid },
    /// Read receipt for a batch of message ids.
    Read { ids: Vec<Uuid> },
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum Frame {
    Ping { ts_ms: i64 },
    Pong { ts_ms: i64 },
    Name { name: String },
    Msg { id: Uuid, payload: Payload },
    Ack { id: Uuid },
    FileOffer { tid: Uuid, msg_id: Uuid, name: String, size: u64, mime: String, sha256: [u8; 32], group_id: Option<Uuid>, ts_ms: i64 },
    FileAccept { tid: Uuid },
    FileReject { tid: Uuid, reason: String },
    FileChunk { tid: Uuid, offset: u64, data: Vec<u8> },
    /// Receiver -> sender flow-control credit: bytes durably written so far.
    FileProgress { tid: Uuid, received: u64 },
    FileEnd { tid: Uuid },
    FileComplete { tid: Uuid, ok: bool, error: String },
    FileCancel { tid: Uuid, reason: String },
    PinPair { pin: String },
    PinResult { ok: bool },
    Bye,
    /// Receiver -> sender, instead of FileAccept, for a re-offered transfer it already has
    /// `offset` bytes of; `prefix_sha256` = SHA-256 of those bytes. The sender continues at
    /// `offset` if its file has the same prefix, otherwise it restarts at 0 (first chunk offset 0).
    /// Only sent to peers announcing CAP_RESUME.
    FileResume { tid: Uuid, offset: u64, prefix_sha256: [u8; 32] },
    /// Profile (个人主页) fields, only sent to peers announcing CAP_PROFILE: on connect and on change.
    /// `avatar` empty = no avatar. Variant index 17 (C core: frame 17).
    Profile { signature: String, avatar: Vec<u8> },
}

pub fn encode<T: Serialize>(v: &T) -> anyhow::Result<Vec<u8>> {
    Ok(bincode::serialize(v)?)
}

pub fn decode<'a, T: Deserialize<'a>>(b: &'a [u8]) -> anyhow::Result<T> {
    use bincode::Options;
    Ok(bincode::DefaultOptions::new()
        .with_fixint_encoding()
        .allow_trailing_bytes()
        .with_limit(1 << 20)
        .deserialize(b)?)
}
