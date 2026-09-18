//! Encrypted identity + trust-store backup / restore.
//!
//! Backup blob = ChaCha20-Poly1305 seal of JSON containing identity.json + peers.json
//! (+ optional settings without secrets). Password → key via BLAKE3 keyed hash
//! (not a full KDF; document: use a strong password; LAN MVP).

use crate::config::ConfigPaths;
use crate::crypto::{open_at_rest, seal_at_rest};
use anyhow::{anyhow, Context, Result};
use serde::{Deserialize, Serialize};
use std::fs;
use std::path::Path;

const MAGIC: &[u8] = b"LSMBAK01";

#[derive(Debug, Serialize, Deserialize)]
struct BackupPayload {
    version: u32,
    identity_json: String,
    peers_json: String,
    settings_json: Option<String>,
    history_key_hex: Option<String>,
    groups_key_hex: Option<String>,
}

fn key_from_password(password: &str) -> [u8; 32] {
    *blake3::hash(password.as_bytes()).as_bytes()
}

pub fn export_backup(paths: &ConfigPaths, password: &str, out: &Path) -> Result<()> {
    anyhow::ensure!(!password.is_empty(), "password required");
    let identity_json = fs::read_to_string(&paths.identity).context("read identity")?;
    let peers_json = if paths.peers.exists() {
        fs::read_to_string(&paths.peers)?
    } else {
        "{\"peers\":[]}".into()
    };
    let settings_json = if paths.settings.exists() {
        Some(fs::read_to_string(&paths.settings)?)
    } else {
        None
    };
    let history_key_hex = if paths.history_key.exists() {
        Some(hex::encode(fs::read(&paths.history_key)?))
    } else {
        None
    };
    let groups_key = paths.root.join("groups.key");
    let groups_key_hex = if groups_key.exists() {
        Some(hex::encode(fs::read(&groups_key)?))
    } else {
        None
    };
    let payload = BackupPayload {
        version: 1,
        identity_json,
        peers_json,
        settings_json,
        history_key_hex,
        groups_key_hex,
    };
    let plain = serde_json::to_vec(&payload)?;
    let key = key_from_password(password);
    let sealed = seal_at_rest(&key, &plain)?;
    let mut out_bytes = MAGIC.to_vec();
    out_bytes.extend_from_slice(&sealed);
    if let Some(parent) = out.parent() {
        fs::create_dir_all(parent)?;
    }
    fs::write(out, out_bytes)?;
    Ok(())
}

pub fn import_backup(paths: &ConfigPaths, password: &str, input: &Path) -> Result<()> {
    anyhow::ensure!(!password.is_empty(), "password required");
    let data = fs::read(input).context("read backup")?;
    anyhow::ensure!(data.len() > MAGIC.len() + 28, "backup too short");
    anyhow::ensure!(&data[..MAGIC.len()] == MAGIC, "not a LAN messenger backup");
    let key = key_from_password(password);
    let plain = open_at_rest(&key, &data[MAGIC.len()..]).map_err(|_| anyhow!("wrong password or corrupt backup"))?;
    let payload: BackupPayload = serde_json::from_slice(&plain)?;
    paths.ensure_dirs()?;
    fs::write(&paths.identity, payload.identity_json)?;
    fs::write(&paths.peers, payload.peers_json)?;
    if let Some(s) = payload.settings_json {
        fs::write(&paths.settings, s)?;
    }
    if let Some(h) = payload.history_key_hex {
        let bytes = hex::decode(h)?;
        anyhow::ensure!(bytes.len() == 32, "bad history key in backup");
        fs::write(&paths.history_key, bytes)?;
    }
    if let Some(g) = payload.groups_key_hex {
        let bytes = hex::decode(g)?;
        anyhow::ensure!(bytes.len() == 32, "bad groups key in backup");
        fs::write(paths.root.join("groups.key"), bytes)?;
    }
    Ok(())
}
