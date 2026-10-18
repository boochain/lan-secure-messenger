//! Paths and persisted settings.
//!
//! Layout (Windows): `%APPDATA%\FREC86\lan-secure-messenger\`
//!   `config\`  identity.json, contacts.json, settings.json, groups.json, *.key
//!   `data\`    messages.enc (encrypted history)
//!   `logs\`    daemon.log.YYYY-MM-DD (rotated daily, 7 kept)
//! Linux: `~/.config/lan-secure-messenger/{config,data,logs}`.

use crate::util::{atomic_write, sanitize_display_name};
use crate::{APP_NAME, DEFAULT_DISCOVERY_PORT, DEFAULT_LISTEN_PORT};
use anyhow::{Context, Result};
use directories::ProjectDirs;
use serde::{Deserialize, Serialize};
use std::fs;
use std::path::{Path, PathBuf};

#[derive(Debug, Clone)]
pub struct ConfigPaths {
    pub base: PathBuf,
    pub root: PathBuf,
    pub data: PathBuf,
    pub logs: PathBuf,
    pub identity: PathBuf,
    pub contacts: PathBuf,
    pub legacy_peers: PathBuf,
    pub history: PathBuf,
    pub history_key: PathBuf,
    pub settings: PathBuf,
    pub groups: PathBuf,
}

impl ConfigPaths {
    /// Default per-user location. v0.3 stored identity in `<base>\config`, which we keep
    /// so upgrading preserves the identity/fingerprint.
    pub fn resolve() -> Result<Self> {
        let dirs = ProjectDirs::from("com", "FREC86", APP_NAME).context("cannot resolve config directory")?;
        let cfg = dirs.config_dir();
        let base = if cfg.file_name().map(|n| n == "config").unwrap_or(false) {
            cfg.parent().unwrap_or(cfg).to_path_buf()
        } else {
            cfg.to_path_buf()
        };
        Ok(Self::from_base(base))
    }

    /// Everything under one base directory (used by tests and `--data-dir`).
    pub fn from_base(base: impl AsRef<Path>) -> Self {
        let base = base.as_ref().to_path_buf();
        let root = base.join("config");
        let data = base.join("data");
        Self {
            identity: root.join("identity.json"),
            contacts: root.join("contacts.json"),
            legacy_peers: root.join("peers.json"),
            history: data.join("messages.enc"),
            history_key: root.join("history.key"),
            settings: root.join("settings.json"),
            groups: root.join("groups.json"),
            logs: base.join("logs"),
            base,
            root,
            data,
        }
    }

    pub fn ensure_dirs(&self) -> Result<()> {
        for d in [&self.root, &self.data, &self.logs] {
            fs::create_dir_all(d).with_context(|| format!("create {}", d.display()))?;
        }
        Ok(())
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(default)]
pub struct Settings {
    pub display_name: String,
    /// UDP discovery port (announces are sent to and received on this port).
    pub discovery_port: u16,
    /// Preferred TCP listen port (falls back to an ephemeral port if busy).
    pub listen_port: u16,
    pub discovery_enabled: bool,
    /// Receive announces (false simulates a firewall that blocks inbound UDP; tests).
    pub discovery_listen: bool,
    pub multicast_enabled: bool,
    /// Extra unicast announce targets `host[:port]` (e.g. peers on other subnets, tests).
    pub announce_targets: Vec<String>,
    /// Interface name substrings to force-include even if they look virtual.
    pub include_interfaces: Vec<String>,
    /// Interface name substrings to always exclude.
    pub exclude_interfaces: Vec<String>,
    /// Accept files from trusted peers automatically.
    pub auto_accept_files: bool,
    pub max_file_bytes: u64,
    pub download_dir: Option<String>,
    pub history_enabled: bool,
    pub first_run_done: bool,
    /// Auto-trust every new peer (tests / CLI convenience only).
    pub auto_trust: bool,
    pub check_updates: bool,
    pub announce_interval_secs: u64,
}

impl Default for Settings {
    fn default() -> Self {
        Self {
            display_name: default_name(),
            discovery_port: DEFAULT_DISCOVERY_PORT,
            listen_port: DEFAULT_LISTEN_PORT,
            discovery_enabled: true,
            discovery_listen: true,
            multicast_enabled: true,
            announce_targets: Vec::new(),
            include_interfaces: Vec::new(),
            exclude_interfaces: Vec::new(),
            auto_accept_files: true,
            max_file_bytes: 4 * 1024 * 1024 * 1024,
            download_dir: None,
            history_enabled: true,
            first_run_done: false,
            auto_trust: false,
            check_updates: true,
            announce_interval_secs: 3,
        }
    }
}

fn default_name() -> String {
    hostname::get()
        .ok()
        .and_then(|h| h.into_string().ok())
        .map(|s| sanitize_display_name(&s))
        .unwrap_or_else(|| "LAN 用户".into())
}

impl Settings {
    pub fn load(paths: &ConfigPaths) -> Result<Self> {
        if paths.settings.exists() {
            let raw = fs::read_to_string(&paths.settings).context("read settings")?;
            match serde_json::from_str::<Settings>(&raw) {
                Ok(mut s) => {
                    s.display_name = sanitize_display_name(&s.display_name);
                    return Ok(s);
                }
                Err(e) => tracing::warn!(error = %e, "settings.json unreadable, using defaults"),
            }
        }
        Ok(Self::default())
    }

    pub fn save(&self, paths: &ConfigPaths) -> Result<()> {
        atomic_write(&paths.settings, serde_json::to_string_pretty(self)?.as_bytes())
    }

    pub fn download_dir(&self, paths: &ConfigPaths) -> PathBuf {
        if let Some(d) = self.download_dir.as_ref().filter(|d| !d.trim().is_empty()) {
            return PathBuf::from(d);
        }
        directories::UserDirs::new()
            .and_then(|u| u.download_dir().map(|d| d.join("LanMessenger")))
            .unwrap_or_else(|| paths.base.join("downloads"))
    }
}
