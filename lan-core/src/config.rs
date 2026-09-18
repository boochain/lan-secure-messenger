use crate::{APP_NAME, DEFAULT_DISCOVERY_PORT, DEFAULT_LISTEN_PORT};
use anyhow::{Context, Result};
use directories::ProjectDirs;
use serde::{Deserialize, Serialize};
use std::fs;
use std::net::Ipv4Addr;
use std::path::{Path, PathBuf};

#[derive(Debug, Clone)]
pub struct ConfigPaths {
    pub root: PathBuf,
    pub identity: PathBuf,
    pub peers: PathBuf,
    pub history: PathBuf,
    pub history_key: PathBuf,
    pub settings: PathBuf,
}

impl ConfigPaths {
    pub fn resolve() -> Result<Self> {
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
    /// Optional broadcast address override (e.g. 127.0.0.1 for local smoke test).
    pub broadcast_addr: Option<String>,
    /// Auto-trust new peers (CLI convenience; GUI should confirm).
    pub auto_trust: bool,
    /// Enable encrypted at-rest history.
    pub history_enabled: bool,
}

impl Default for AppConfig {
    fn default() -> Self {
        let hostname = hostname::get()
            .ok()
            .and_then(|h| h.into_string().ok())
            .unwrap_or_else(|| "unknown".into());
        Self {
            display_name: hostname,
            discovery_port: DEFAULT_DISCOVERY_PORT,
            listen_port: DEFAULT_LISTEN_PORT,
            bind_addr: Ipv4Addr::UNSPECIFIED.to_string(),
            broadcast_addr: None,
            auto_trust: false,
            history_enabled: true,
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
        fs::write(&paths.settings, data)?;
        Ok(())
    }
}
