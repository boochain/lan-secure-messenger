//! Encrypted backup / restore of identity, contacts, settings, groups, history and files.
//!
//! v2 format: `LSMBAK02 | salt(16) | ChaCha20-Poly1305(JSON)`, key = Argon2id(password, salt).
//! v1 (`LSMBAK01`, key = BLAKE3(password), no salt) is still accepted for import.
//!
//! The JSON payload includes `history_enc_hex` (the sealed `messages.enc`) and `files`
//! (local attachments as hex). The outer LSMBAK02 layer is what the relay stores;
//! the server never sees plaintext messages, file bytes, or keys. A new device
//! decrypts this blob with the recovery key (or after QR pairing unwraps that key).

use crate::api::Message;
use crate::config::ConfigPaths;
use crate::crypto::{open_at_rest, seal_at_rest};
use crate::util::atomic_write;
use anyhow::{anyhow, Context, Result};
use rand::RngCore;
use serde::{Deserialize, Serialize};
use std::collections::HashSet;
use std::fs;
use std::path::{Path, PathBuf};

const MAGIC_V1: &[u8] = b"LSMBAK01";
const MAGIC_V2: &[u8] = b"LSMBAK02";
/// Skip any single attachment larger than this (the sealed history itself is always included).
const MAX_FILE_BYTES: u64 = 4 * 1024 * 1024;

#[derive(Debug, Serialize, Deserialize, Default)]
#[serde(default)]
struct BackupFile {
    name: String,
    orig_path: String,
    data_hex: String,
}

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
    /// Sealed `messages.enc` (already encrypted at rest; hex-encoded for JSON).
    history_enc_hex: Option<String>,
    files: Vec<BackupFile>,
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

fn key32(hexv: &str) -> Option<[u8; 32]> {
    let v = hex::decode(hexv).ok()?;
    if v.len() != 32 {
        return None;
    }
    let mut k = [0u8; 32];
    k.copy_from_slice(&v);
    Some(k)
}

fn file_leaf(name: &str) -> String {
    Path::new(name)
        .file_name()
        .and_then(|s| s.to_str())
        .unwrap_or("file")
        .replace(['/', '\\'], "_")
}

fn unique_dest(dir: &Path, name: &str) -> PathBuf {
    let base = file_leaf(name);
    let p = dir.join(&base);
    if !p.exists() {
        return p;
    }
    let stem = Path::new(&base).file_stem().and_then(|s| s.to_str()).unwrap_or("file");
    let ext = Path::new(&base).extension().and_then(|s| s.to_str()).unwrap_or("");
    for i in 1..10_000 {
        let cand = if ext.is_empty() { format!("{stem}_{i}") } else { format!("{stem}_{i}.{ext}") };
        let p = dir.join(cand);
        if !p.exists() {
            return p;
        }
    }
    dir.join(base)
}

fn collect_files(history_enc: &[u8], history_key_hex: &Option<String>) -> Vec<BackupFile> {
    let Some(key) = history_key_hex.as_deref().and_then(key32) else {
        return vec![];
    };
    let Ok(plain) = open_at_rest(&key, history_enc) else {
        return vec![];
    };
    let msgs: Vec<Message> = serde_json::from_slice(&plain).unwrap_or_default();
    let mut out = Vec::new();
    let mut seen = HashSet::new();
    for m in msgs {
        let Some(f) = m.file else { continue };
        let Some(path) = f.path.filter(|p| !p.is_empty()) else { continue };
        if !seen.insert(path.clone()) {
            continue;
        }
        let p = Path::new(&path);
        let Ok(meta) = fs::metadata(p) else { continue };
        if !meta.is_file() || meta.len() > MAX_FILE_BYTES {
            continue;
        }
        let Ok(data) = fs::read(p) else { continue };
        let name = if f.name.is_empty() { file_leaf(&path) } else { f.name };
        out.push(BackupFile { name, orig_path: path, data_hex: hex::encode(data) });
    }
    out
}

fn rewrite_history_paths(paths: &ConfigPaths, map: &[(String, String)]) -> Result<()> {
    if map.is_empty() {
        return Ok(());
    }
    let key_bytes = match fs::read(&paths.history_key) {
        Ok(v) if v.len() == 32 => v,
        _ => return Ok(()),
    };
    let mut key = [0u8; 32];
    key.copy_from_slice(&key_bytes);
    let enc = match fs::read(&paths.history) {
        Ok(v) => v,
        Err(_) => return Ok(()),
    };
    let plain = match open_at_rest(&key, &enc) {
        Ok(v) => v,
        Err(_) => return Ok(()),
    };
    let mut msgs: Vec<Message> = match serde_json::from_slice(&plain) {
        Ok(v) => v,
        Err(_) => return Ok(()),
    };
    let mut changed = false;
    for m in &mut msgs {
        if let Some(f) = m.file.as_mut() {
            if let Some(p) = f.path.as_mut() {
                if let Some((_, newp)) = map.iter().find(|(old, _)| old == p) {
                    *p = newp.clone();
                    changed = true;
                }
            }
        }
    }
    if changed {
        atomic_write(&paths.history, &seal_at_rest(&key, &serde_json::to_vec(&msgs)?)?)?;
    }
    Ok(())
}

pub fn export_backup(paths: &ConfigPaths, password: &str, out: &Path) -> Result<()> {
    anyhow::ensure!(password.chars().count() >= 6, "备份密码至少 6 个字符");
    let history_enc = read_opt(&paths.history);
    let history_key_hex = read_opt(&paths.history_key).map(hex::encode);
    let files = history_enc.as_deref().map(|h| collect_files(h, &history_key_hex)).unwrap_or_default();
    let payload = BackupPayload {
        version: 2,
        identity_json: fs::read_to_string(&paths.identity).context("read identity")?,
        contacts_json: fs::read_to_string(&paths.contacts).unwrap_or_else(|_| "{}".into()),
        settings_json: fs::read_to_string(&paths.settings).ok(),
        history_key_hex,
        groups_key_hex: read_opt(&paths.root.join("groups.key")).map(hex::encode),
        groups_file_hex: read_opt(&paths.groups).map(hex::encode),
        history_enc_hex: history_enc.map(hex::encode),
        files,
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
    for (hexv, dst) in [
        (p.history_key_hex, paths.history_key.clone()),
        (p.groups_key_hex, paths.root.join("groups.key")),
        (p.groups_file_hex, paths.groups.clone()),
    ] {
        if let Some(h) = hexv {
            atomic_write(&dst, &hex::decode(h)?)?;
        }
    }
    if let Some(h) = p.history_enc_hex {
        atomic_write(&paths.history, &hex::decode(h)?)?;
    }
    let dl = paths.base.join("downloads");
    fs::create_dir_all(&dl)?;
    let mut rewrites = Vec::new();
    for f in p.files {
        if f.data_hex.is_empty() {
            continue;
        }
        let data = hex::decode(&f.data_hex)?;
        let dest = unique_dest(&dl, if f.name.is_empty() { "file" } else { &f.name });
        atomic_write(&dest, &data)?;
        if !f.orig_path.is_empty() {
            rewrites.push((f.orig_path, dest.to_string_lossy().into_owned()));
        }
    }
    rewrite_history_paths(paths, &rewrites)?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::api::{Direction, FileRef, MsgKind, MsgStatus};
    use crate::crypto::generate_history_key;
    use crate::history::load_or_create_key;

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

    #[test]
    fn history_and_file_roundtrip_no_plaintext() {
        let d = tempfile::tempdir().unwrap();
        let p = ConfigPaths::from_base(d.path().join("a"));
        p.ensure_dirs().unwrap();
        crate::identity::Identity::load_or_create(&p).unwrap();
        fs::write(&p.contacts, "{\"contacts\":[]}").unwrap();
        let key = generate_history_key();
        atomic_write(&p.history_key, &key).unwrap();
        let secret = "LSM-V071-SECRET-MSG-账号历史同步";
        let file_bytes = b"LSM-V071-FILE-PAYLOAD-NO-PLAINTEXT-ON-SERVER";
        let src = d.path().join("a-src.bin");
        fs::write(&src, file_bytes).unwrap();
        let msgs = vec![Message {
            id: "m1".into(),
            conversation: "dm:PEER".into(),
            direction: Direction::Out,
            sender_fp: "AA".into(),
            sender_name: "Ada".into(),
            kind: MsgKind::Text,
            body: secret.into(),
            ts_ms: 1,
            status: MsgStatus::Delivered,
            file: None,
            ext: Default::default(),
        }, Message {
            id: "m2".into(),
            conversation: "dm:PEER".into(),
            direction: Direction::Out,
            sender_fp: "AA".into(),
            sender_name: "Ada".into(),
            kind: MsgKind::File,
            body: String::new(),
            ts_ms: 2,
            status: MsgStatus::Delivered,
            file: Some(FileRef {
                transfer_id: "t1".into(),
                name: "secret.bin".into(),
                size: file_bytes.len() as u64,
                mime: "application/octet-stream".into(),
                path: Some(src.to_string_lossy().into_owned()),
                is_image: false,
            }),
            ext: Default::default(),
        }];
        atomic_write(&p.history, &seal_at_rest(&key, &serde_json::to_vec(&msgs).unwrap()).unwrap()).unwrap();
        let bak = d.path().join("hist.lsmbak");
        export_backup(&p, "RK1-AAAA-BBBB-CCCC-DDDD-EEEE-FFFF-0000-1111", &bak).unwrap();
        let blob = fs::read(&bak).unwrap();
        assert!(blob.starts_with(b"LSMBAK02"));
        assert!(!blob.windows(secret.as_bytes().len()).any(|w| w == secret.as_bytes()), "plaintext message leaked into backup blob");
        assert!(!blob.windows(file_bytes.len()).any(|w| w == file_bytes), "plaintext file leaked into backup blob");

        let q = ConfigPaths::from_base(d.path().join("b"));
        import_backup(&q, "RK1-AAAA-BBBB-CCCC-DDDD-EEEE-FFFF-0000-1111", &bak).unwrap();
        let qk = load_or_create_key(&q.history_key).unwrap();
        let restored: Vec<Message> = serde_json::from_slice(&open_at_rest(&qk, &fs::read(&q.history).unwrap()).unwrap()).unwrap();
        assert!(restored.iter().any(|m| m.body == secret), "text history missing after import");
        let fm = restored.iter().find(|m| m.kind == MsgKind::File).expect("file msg");
        let rpath = fm.file.as_ref().and_then(|f| f.path.clone()).expect("rewritten path");
        assert_ne!(rpath, src.to_string_lossy().as_ref());
        assert_eq!(fs::read(&rpath).unwrap(), file_bytes);
    }
}
