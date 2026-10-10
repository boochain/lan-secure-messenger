//! LAN Secure Messenger core (v0.5): connection-manager engine, discovery, Noise links,
//! encrypted storage and the local JSON-RPC IPC server used by UIs and the CLI.

pub mod api;
pub mod backup;
pub mod config;
pub mod contacts;
pub mod crypto;
pub mod discovery;
pub mod engine;
pub mod firewall;
pub mod group;
pub mod history;
pub mod identity;
pub mod ipc;
pub mod link;
pub mod netif;
pub mod util;
pub mod wire;

pub use config::{ConfigPaths, Settings};
pub use engine::{EngineHandle, EngineOptions, RpcError};
pub use identity::{Fingerprint, Identity};

pub const APP_NAME: &str = "lan-secure-messenger";
pub const APP_VERSION: &str = env!("CARGO_PKG_VERSION");
pub const DEFAULT_DISCOVERY_PORT: u16 = 41234;
pub const DEFAULT_LISTEN_PORT: u16 = 41235;
