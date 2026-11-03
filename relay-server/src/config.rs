//! TOML configuration.
use anyhow::{Context, Result};
use serde::{Deserialize, Serialize};
use std::path::Path;

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(default)]
pub struct Config {
    pub server: ServerCfg,
    pub limits: Limits,
    pub log: LogCfg,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(default)]
pub struct ServerCfg {
    /// Bind address (监听地址), e.g. "0.0.0.0" or "::".
    pub bind: String,
    /// TCP port (端口).
    pub port: u16,
    /// Network passphrase (网络口令). Never sent on the wire.
    pub passphrase: String,
    /// 16-byte KDF salt as 32 hex chars. Empty = built-in default salt.
    pub salt: String,
    /// Local status endpoint (状态接口), "" disables. Default 127.0.0.1:<port+1>.
    pub status_addr: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(default)]
pub struct Limits {
    pub max_frame: u32,
    pub max_connections: usize,
    pub max_connections_per_ip: usize,
    pub rate_bytes_per_sec: u64,
    pub rate_frames_per_sec: u64,
    pub idle_timeout_secs: u64,
    pub auth_timeout_secs: u64,
    pub heartbeat_secs: u16,
    pub queue_frames: usize,
    pub send_timeout_ms: u64,
    pub auth_failures_per_min: u32,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(default)]
pub struct LogCfg {
    /// error|warn|info|debug|trace
    pub level: String,
    /// Log file path; "" = stderr only.
    pub file: String,
}

impl Default for Config {
    fn default() -> Self {
        Config { server: ServerCfg::default(), limits: Limits::default(), log: LogCfg::default() }
    }
}
impl Default for ServerCfg {
    fn default() -> Self {
        ServerCfg { bind: "0.0.0.0".into(), port: 9443, passphrase: String::new(), salt: String::new(), status_addr: String::new() }
    }
}
impl Default for Limits {
    fn default() -> Self {
        Limits {
            max_frame: 1024 * 1024 + 64,
            max_connections: 1024,
            max_connections_per_ip: 64,
            rate_bytes_per_sec: 64 * 1024 * 1024,
            rate_frames_per_sec: 5000,
            idle_timeout_secs: 90,
            auth_timeout_secs: 10,
            heartbeat_secs: 25,
            queue_frames: 256,
            send_timeout_ms: 15000,
            auth_failures_per_min: 10,
        }
    }
}
impl Default for LogCfg {
    fn default() -> Self {
        LogCfg { level: "info".into(), file: String::new() }
    }
}

impl Config {
    pub fn load(path: &Path) -> Result<Self> {
        let s = std::fs::read_to_string(path).with_context(|| format!("读取配置失败 (read config) {}", path.display()))?;
        let c: Config = toml::from_str(&s).with_context(|| format!("配置格式错误 (bad TOML) {}", path.display()))?;
        Ok(c)
    }
    pub fn status_addr(&self) -> Option<String> {
        match self.server.status_addr.as_str() {
            "off" | "none" | "disabled" => None,
            "" => Some(format!("127.0.0.1:{}", self.server.port.wrapping_add(1))),
            s => Some(s.to_string()),
        }
    }
    pub fn salt_bytes(&self) -> Result<[u8; 16]> {
        if self.server.salt.is_empty() {
            return Ok(crate::proto::DEFAULT_SALT);
        }
        let v = hex::decode(self.server.salt.trim()).context("salt 必须是 32 位十六进制 (salt must be 32 hex chars)")?;
        v.try_into().map_err(|_| anyhow::anyhow!("salt 必须是 16 字节 (salt must be 16 bytes)"))
    }
}
