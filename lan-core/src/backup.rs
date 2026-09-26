//! Encrypted backup / restore of identity, contacts, settings, groups and keys.
//!
//! v2 format: `LSMBAK02 | salt(16) | ChaCha20-Poly1305(JSON)`, key = Argon2id(password, salt).
//! v1 (`LSMBAK01`, key = BLAKE3(password), no salt) is still accepted for import.

use crate::config::ConfigPaths;
use crate::crypto::{open_at_rest, seal_at_rest};
use crate::util::atomic_write;
use anyhow::{anyhow, Context, Result};
use rand::RngCore;
use serde::{Deserialize, Serialize};
use std::fs;
use std::path::Path;

const MAGIC_V1: &[u8] = b"LSMBAK01";
const MAGIC_V2: &[u8] = b"LSMBAK02";

#[derive(Debug, Serialize, Deserialize, Default)]
#[serde(default)]
struct BackupPayload {
    version: u32,
    identity_json: String,
    #[serde(alias = "peers_json")]
    contacts_json: String,
    settings_json: Option<String>,
    history_key_hex: Option<String>,
    groups_key_hex: Option<String>,
    groups_file_hex: Option<String>,
}

fn kdf(password: &str, salt: &[u8]) -> Result<[u8; 32]> {
    let mut out = [0u8; 32];
    argon2::Argon2::default()
        .hash_password_into(password.as_bytes(), salt, &mut out)
        .map_err(|e| anyhow!("kdf: {e}"))?;
    Ok(out)
}

fn read_opt(p: &Path) -> Option<Vec<u8>> {
    fs::read(p).ok()
}

pub fn export_backup(paths: &ConfigPaths, password: &str, out: &Path) -> Result<()> {
    anyhow::ensure!(password.chars().count() >= 6, "备份密码至少 6 个字符");
    let payload = BackupPayload {
        version: 2,
        identity_json: fs::read_to_string(&paths.identity).context("read identity")?,
        contacts_json: fs::read_to_string(&paths.contacts).unwrap_or_else(|_| "{}".into()),
        settings_json: fs::read_to_string(&paths.settings).ok(),
        history_key_hex: read_opt(&paths.history_key).map(hex::encode),
        groups_key_hex: read_opt(&paths.root.join("groups.key")).map(hex::encode),
        groups_file_hex: read_opt(&paths.groups).map(hex::encode),
    };
    let mut salt = [0u8; 16];
    rand::thread_rng().fill_bytes(&mut salt);
    let key = kdf(password, &salt)?;
    let sealed = seal_at_rest(&key, &serde_json::to_vec(&payload)?)?;
    let mut bytes = MAGIC_V2.to_vec();
    bytes.extend_from_slice(&salt);
    bytes.extend_from_slice(&sealed);
    atomic_write(out, &bytes)
}

pub fn import_backup(paths: &ConfigPaths, password: &str, input: &Path) -> Result<()> {
    let data = fs::read(input).context("read backup")?;
    anyhow::ensure!(data.len() > 8 + 28, "backup too short");
    let plain = if &data[..8] == MAGIC_V2 {
        anyhow::ensure!(data.len() > 8 + 16 + 28, "backup too short");
        let key = kdf(password, &data[8..24])?;
        open_at_rest(&key, &data[24..])
    } else if &data[..8] == MAGIC_V1 {
        let key = *blake3::hash(password.as_bytes()).as_bytes();
        open_at_rest(&key, &data[8..])
    } else {
        anyhow::bail!("不是 LAN Secure Messenger 备份文件");
    }
    .map_err(|_| anyhow!("密码错误或备份文件已损坏"))?;
    let p: BackupPayload = serde_json::from_slice(&plain)?;
    paths.ensure_dirs()?;
    atomic_write(&paths.identity, p.identity_json.as_bytes())?;
    if p.version >= 2 {
        atomic_write(&paths.contacts, p.contacts_json.as_bytes())?;
    } else {
        atomic_write(&paths.legacy_peers, p.contacts_json.as_bytes())?;
        let _ = fs::remove_file(&paths.contacts);
    }
    if let Some(s) = p.settings_json {
        atomic_write(&paths.settings, s.as_bytes())?;
    }
    for (hexv, dst) in [(p.history_key_hex, paths.history_key.clone()), (p.groups_key_hex, paths.root.join("groups.key")), (p.groups_file_hex, paths.groups.clone())] {
        if let Some(h) = hexv {
            atomic_write(&dst, &hex::decode(h)?)?;
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn roundtrip() {
        let d = tempfile::tempdir().unwrap();
        let p = ConfigPaths::from_base(d.path().join("a"));
        p.ensure_dirs().unwrap();
        crate::identity::Identity::load_or_create(&p).unwrap();
        fs::write(&p.contacts, "{\"contacts\":[]}").unwrap();
        let f = d.path().join("b.lsmbak");
        export_backup(&p, "secret123", &f).unwrap();
        let q = ConfigPaths::from_base(d.path().join("b"));
        assert!(import_backup(&q, "wrong-pass", &f).is_err());
        import_backup(&q, "secret123", &f).unwrap();
        assert_eq!(fs::read(&p.identity).unwrap(), fs::read(&q.identity).unwrap());
    }
}
