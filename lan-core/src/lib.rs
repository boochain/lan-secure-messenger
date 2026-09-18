//! LAN Secure Messenger core library.
//!
//! Provides peer discovery, Noise-based E2E encryption, text/file transfer,
//! TOFU trust, and encrypted local history.

pub mod backup;
pub mod config;
pub mod crypto;
pub mod discovery;
pub mod group;
pub mod history;
pub mod identity;
pub mod node;
pub mod peer;
pub mod protocol;
pub mod session;
pub mod transfer;
pub mod util;

pub use config::{AppConfig, ConfigPaths};
pub use identity::{Fingerprint, Identity};
pub use node::{Node, NodeEvent, NodeHandle};
pub use peer::{PeerInfo, PeerStore, TrustState};
pub use group::{GroupInfo, GroupStore};
pub use protocol::{ChatMessage, ChatScope, FileMeta, WireMessage};

pub const APP_NAME: &str = "lan-secure-messenger";
pub const APP_VERSION: &str = env!("CARGO_PKG_VERSION");
pub const DEFAULT_DISCOVERY_PORT: u16 = 41234;
pub const DEFAULT_LISTEN_PORT: u16 = 41235;
pub const PROTOCOL_MAGIC: [u8; 4] = *b"LSM1";
pub const PROTOCOL_VERSION: u16 = 1;
pub const CHUNK_SIZE: usize = 48 * 1024;
pub const DISCOVERY_INTERVAL_MS: u64 = 2000;
pub const PEER_TIMEOUT_SECS: u64 = 8;
