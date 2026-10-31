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
    /// Prefer this IPv4 for LAN listen/dial/discovery (empty = any). Helps bypass VPN hijack.
    pub bind_lan_ip: String,
    /// `lan` | `server_lan` — when relay is on, server_lan treats relay as equal parallel path.
    pub transport_mode: String,
    /// Optional relay account username (fingerprint restore). Empty = anonymous relay.
    pub account_user: String,
    /// Default retain policy for new messages: `ends_only` | `server_retain`.
    pub default_retain: String,
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
    /// Relay server fallback (off by default). LAN direct is always preferred.
    pub relay_enabled: bool,
    pub relay_host: String,
    pub relay_port: u16,
    /// Network passphrase (never sent; challenge-response). Write-only over IPC: reads (snapshot,
    /// settings_updated) leave it out and set `relay_has_passphrase` instead. In an
    /// `update_settings` patch: absent = keep, "" = clear, any other value = set.
    #[serde(skip_serializing_if = "String::is_empty")]
    pub relay_passphrase: String,
    /// IPC read-only view flag: a passphrase is configured. Ignored on write, never persisted.
    #[serde(skip_serializing_if = "is_false")]
    pub relay_has_passphrase: bool,
}

fn is_false(b: &bool) -> bool {
    !*b
}

impl Settings {
    /// The settings as shown over local IPC: the passphrase is replaced by `relay_has_passphrase`.
    pub fn for_ipc(&self) -> Settings {
        let mut s = self.clone();
        s.relay_has_passphrase = !s.relay_passphrase.is_empty();
        s.relay_passphrase.clear();
        s
    }
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
            bind_lan_ip: String::new(),
            transport_mode: "lan".into(),
            account_user: String::new(),
            default_retain: "ends_only".into(),
            auto_accept_files: true,
            max_file_bytes: 4 * 1024 * 1024 * 1024,
            download_dir: None,
            history_enabled: true,
            first_run_done: false,
            auto_trust: false,
            check_updates: true,
            announce_interval_secs: 2,
            relay_enabled: false,
            relay_host: String::new(),
            relay_port: 9443,
            relay_passphrase: String::new(),
            relay_has_passphrase: false,
        }
    }
}

fn relay_secret_path(paths: &ConfigPaths) -> PathBuf {
    paths.history.with_file_name("relay.dat")
}

fn load_relay_secret(paths: &ConfigPaths) -> Option<String> {
    let d = fs::read(relay_secret_path(paths)).ok()?;
    let key = crate::history::load_or_create_key(&paths.history_key).ok()?;
    match crate::crypto::open_at_rest(&key, &d) {
        Ok(p) => String::from_utf8(p).ok(),
        Err(e) => {
            tracing::warn!(error = %e, "relay.dat unreadable");
            None
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
                    s.relay_has_passphrase = false;
                    if let Some(p) = load_relay_secret(paths) {
                        s.relay_passphrase = p;
                    }
                    return Ok(s);
                }
                Err(e) => tracing::warn!(error = %e, "settings.json unreadable, using defaults"),
            }
        }
        Ok(Self::default())
    }

    /// The relay passphrase never lands in settings.json: it is sealed (ChaCha20-Poly1305,
    /// the at-rest history key) into relay.dat next to the history, like the C core's relay.dat.
    pub fn save(&self, paths: &ConfigPaths) -> Result<()> {
        let rf = relay_secret_path(paths);
        if self.relay_passphrase.is_empty() {
            let _ = fs::remove_file(&rf);
        } else {
            let key = crate::history::load_or_create_key(&paths.history_key)?;
            atomic_write(&rf, &crate::crypto::seal_at_rest(&key, self.relay_passphrase.as_bytes())?)?;
        }
        let mut s = self.clone();
        s.relay_passphrase.clear();
        atomic_write(&paths.settings, serde_json::to_string_pretty(&s)?.as_bytes())
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

#[cfg(test)]
mod relay_secret_tests {
    use super::*;
    #[test]
    fn relay_passphrase_write_only_over_ipc() {
        let mut s = Settings::default();
        s.relay_passphrase = "口令".into();
        let v = serde_json::to_value(s.for_ipc()).unwrap();
        assert!(v.get("relay_passphrase").is_none());
        assert_eq!(v["relay_has_passphrase"], true);
        let v = serde_json::to_value(Settings::default().for_ipc()).unwrap();
        assert!(v.get("relay_passphrase").is_none() && v.get("relay_has_passphrase").is_none());
    }

    #[test]
    fn relay_passphrase_sealed_not_in_settings_json() {
        let d = tempfile::tempdir().unwrap();
        let p = ConfigPaths::from_base(d.path());
        p.ensure_dirs().unwrap();
        let mut s = Settings::default();
        s.relay_enabled = true;
        s.relay_passphrase = "s3cret-网络口令".into();
        s.save(&p).unwrap();
        let raw = fs::read_to_string(&p.settings).unwrap();
        assert!(!raw.contains("s3cret"));
        let sealed = fs::read(p.history.with_file_name("relay.dat")).unwrap();
        assert!(!String::from_utf8_lossy(&sealed).contains("s3cret"));
        assert_eq!(Settings::load(&p).unwrap().relay_passphrase, "s3cret-网络口令");
        s.relay_passphrase.clear();
        s.save(&p).unwrap();
        assert!(!p.history.with_file_name("relay.dat").exists());
    }
}
