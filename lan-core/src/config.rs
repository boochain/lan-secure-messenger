use crate::{APP_NAME, DEFAULT_DISCOVERY_PORT, DEFAULT_LISTEN_PORT};
use anyhow::{Context, Result};
use directories::ProjectDirs;
use serde::{Deserialize, Serialize};
use std::fs;
use std::net::Ipv4Addr;
use std::path::{Path, PathBuf};
use crate::util::{atomic_write, sanitize_display_name};

#[derive(Debug, Clone)]
pub struct ConfigPaths {
    pub root: PathBuf,
    pub identity: PathBuf,
    pub peers: PathBuf,
    pub history: PathBuf,
    pub history_key: PathBuf,
    pub settings: PathBuf,
    pub groups: PathBuf,
}

impl ConfigPaths {
    pub fn resolve() -> Result<Self> {
        // Qualifier / Organization / Application — cross-platform via `directories`
        // Windows: %APPDATA%\FREC86\lan-secure-messenger\
        // Linux:   ~/.config/lan-secure-messenger/
        // macOS:   ~/Library/Application Support/com.FREC86.lan-secure-messenger/
        let dirs = ProjectDirs::from("com", "FREC86", APP_NAME)
            .context("cannot resolve config directory")?;
        Ok(Self::from_dir(dirs.config_dir()))
    }

    pub fn from_dir(dir: impl AsRef<Path>) -> Self {
        let root = dir.as_ref().to_path_buf();
        Self {
            identity: root.join("identity.json"),
            peers: root.join("peers.json"),
            history: root.join("history.enc"),
            history_key: root.join("history.key"),
            settings: root.join("settings.json"),
            groups: root.join("groups.json"),
            root,
        }
    }

    pub fn ensure_dirs(&self) -> Result<()> {
        fs::create_dir_all(&self.root).context("create config dir")?;
        Ok(())
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AppConfig {
    pub display_name: String,
    pub discovery_port: u16,
    pub listen_port: u16,
    /// Bind address for TCP/UDP. Use 0.0.0.0 for all interfaces.
    pub bind_addr: String,
    /// Optional broadcast/multicast send override (e.g. 127.0.0.1 for local smoke test).
    pub broadcast_addr: Option<String>,
    /// Auto-trust new peers (CLI convenience; GUI should confirm).
    pub auto_trust: bool,
    /// Enable encrypted at-rest history.
    pub history_enabled: bool,
    /// Max outbound/inbound file size in bytes (default 256 MiB).
    #[serde(default = "default_max_file_bytes")]
    pub max_file_bytes: u64,
    /// Keep idle TCP/Noise sessions for reuse (seconds).
    #[serde(default = "default_session_idle_secs")]
    pub session_idle_secs: u64,
    /// First-run wizard completed.
    #[serde(default)]
    pub first_run_done: bool,
    /// Start GUI minimized to tray.
    #[serde(default)]
    pub start_in_tray: bool,
    /// Custom download directory (None = config_dir/downloads).
    #[serde(default)]
    pub download_dir: Option<String>,
    /// Auto-retry failed file transfers (attempts, default 3).
    #[serde(default = "default_transfer_retries")]
    pub transfer_retries: u32,
    /// Muted peer fingerprints (suppress notifications).
    #[serde(default)]
    pub muted_peers: Vec<String>,
    /// Muted group ids (as strings).
    #[serde(default)]
    pub muted_groups: Vec<String>,
    /// Soft-check GitHub Releases for updates.
    #[serde(default = "default_true")]
    pub check_updates: bool,
    /// Opt-in: expose a local folder to trusted peers.
    #[serde(default)]
    pub share_enabled: bool,
    /// Absolute path of shared folder (ignored unless share_enabled).
    #[serde(default)]
    pub share_root: Option<String>,
}

fn default_transfer_retries() -> u32 { 3 }
fn default_true() -> bool { true }


fn default_max_file_bytes() -> u64 {
    256 * 1024 * 1024
}
fn default_session_idle_secs() -> u64 {
    120
}

impl Default for AppConfig {
    fn default() -> Self {
        let hostname = hostname::get()
            .ok()
            .and_then(|h| h.into_string().ok())
            .unwrap_or_else(|| "unknown".into());
        Self {
            display_name: sanitize_display_name(&hostname),
            discovery_port: DEFAULT_DISCOVERY_PORT,
            listen_port: DEFAULT_LISTEN_PORT,
            bind_addr: Ipv4Addr::UNSPECIFIED.to_string(),
            broadcast_addr: None,
            auto_trust: false,
            history_enabled: true,
            max_file_bytes: default_max_file_bytes(),
            session_idle_secs: default_session_idle_secs(),
            first_run_done: false,
            start_in_tray: false,
            download_dir: None,
            transfer_retries: default_transfer_retries(),
            muted_peers: Vec::new(),
            muted_groups: Vec::new(),
            check_updates: true,
            share_enabled: false,
            share_root: None,
        }
    }
}

impl AppConfig {
    pub fn load(paths: &ConfigPaths) -> Result<Self> {
        paths.ensure_dirs()?;
        if paths.settings.exists() {
            let data = fs::read_to_string(&paths.settings)?;
            Ok(serde_json::from_str(&data)?)
        } else {
            let cfg = Self::default();
            cfg.save(paths)?;
            Ok(cfg)
        }
    }

    pub fn save(&self, paths: &ConfigPaths) -> Result<()> {
        paths.ensure_dirs()?;
        let data = serde_json::to_string_pretty(self)?;
        atomic_write(&paths.settings, data.as_bytes())?;
        Ok(())
    }
}

/// Snapshot for `lan-msg doctor` / status.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DoctorReport {
    pub display_name: String,
    pub fingerprint: String,
    pub config_dir: String,
    pub bind_addr: String,
    pub discovery_port: u16,
    pub listen_port: u16,
    pub broadcast_addr: Option<String>,
    pub auto_trust: bool,
    pub history_enabled: bool,
    pub max_file_bytes: u64,
    pub session_idle_secs: u64,
    pub peer_total: usize,
    pub peer_online: usize,
    pub peer_trusted: usize,
}
