//! Account-mode helpers: recovery key, lookup id, URI parse, pairing wrap.
//! The relay never sees RK or identity plaintext — see docs/account-mode.md.

use anyhow::{anyhow, bail, Result};
use rand::RngCore;
use sha2::{Digest, Sha256};

const LOOKUP_DOM: &[u8] = b"lsm-acct-lookup-v1";
const PAIR_MAGIC: &[u8] = b"LSMPAIR1";

/// 16 random bytes shown as `RK1-XXXX-...` (8 hex groups).
pub fn generate_recovery_key() -> String {
    let mut raw = [0u8; 16];
    rand::thread_rng().fill_bytes(&mut raw);
    format_recovery_key(&raw)
}

pub fn format_recovery_key(raw: &[u8; 16]) -> String {
    let h = hex::encode(raw).to_uppercase();
    let mut out = String::from("RK1");
    for chunk in h.as_bytes().chunks(4) {
        out.push('-');
        out.push_str(std::str::from_utf8(chunk).unwrap());
    }
    out
}

/// Accept grouped or raw hex; return canonical `RK1-...` form.
pub fn normalize_recovery_key(s: &str) -> Result<String> {
    let rest = s.trim();
    let rest = rest.strip_prefix("RK1-").or_else(|| rest.strip_prefix("RK1")).unwrap_or(rest);
    let hex: String = rest
        .chars()
        .filter(|c| c.is_ascii_hexdigit())
        .collect::<String>()
        .to_uppercase();
    if hex.len() != 32 {
        bail!("恢复密钥格式不正确");
    }
    let mut raw = [0u8; 16];
    raw.copy_from_slice(&hex::decode(&hex).map_err(|_| anyhow!("恢复密钥格式不正确"))?);
    Ok(format_recovery_key(&raw))
}

/// Server directory name derived from RK (not reversible).
pub fn account_lookup(rk: &str) -> Result<String> {
    let n = normalize_recovery_key(rk)?;
    let mut h = Sha256::new();
    h.update(LOOKUP_DOM);
    h.update(n.as_bytes());
    Ok(hex::encode(&h.finalize()[..16]))
}

pub fn relay_uri(host: &str, port: u16) -> String {
    format!("lsm://relay?h={}&p={}", host.trim(), if port == 0 { 9443 } else { port })
}

pub fn pair_uri(host: &str, port: u16, token: &[u8; 32], wrap_key: &[u8; 32]) -> String {
    format!(
        "lsm://pair?r={}:{}&t={}&k={}",
        host.trim(),
        if port == 0 { 9443 } else { port },
        hex::encode(token),
        hex::encode(wrap_key)
    )
}

pub fn wrap_recovery(rk: &str, key: &[u8; 32]) -> Result<Vec<u8>> {
    let ct = crate::crypto::seal_at_rest(key, rk.as_bytes())?;
    let mut out = PAIR_MAGIC.to_vec();
    out.extend_from_slice(&ct);
    Ok(out)
}

pub fn unwrap_recovery(blob: &[u8], key: &[u8; 32]) -> Result<String> {
    if blob.len() < PAIR_MAGIC.len() || &blob[..PAIR_MAGIC.len()] != PAIR_MAGIC {
        bail!("不是有效的配对数据");
    }
    let pt = crate::crypto::open_at_rest(key, &blob[PAIR_MAGIC.len()..])
        .map_err(|_| anyhow!("配对数据无法解密"))?;
    normalize_recovery_key(std::str::from_utf8(&pt).map_err(|_| anyhow!("配对数据损坏"))?)
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum LsmUri {
    Add { addr: String, fp: String, name: String },
    Relay { host: String, port: u16 },
    Pair { host: String, port: u16, token: [u8; 32], wrap_key: [u8; 32] },
}

pub fn uri_param(uri: &str, key: &str) -> String {
    let Some(q) = uri.split_once('?').map(|x| x.1) else { return String::new() };
    for part in q.split('&') {
        if let Some((k, v)) = part.split_once('=') {
            if k == key {
                return url_decode(v);
            }
        }
    }
    String::new()
}

fn url_decode(s: &str) -> String {
    let b = s.as_bytes();
    let mut i = 0;
    let mut out = Vec::new();
    while i < b.len() {
        if b[i] == b'%' && i + 2 < b.len() {
            if let Ok(v) = u8::from_str_radix(std::str::from_utf8(&b[i + 1..i + 3]).unwrap_or(""), 16) {
                out.push(v);
                i += 3;
                continue;
            }
        }
        out.push(if b[i] == b'+' { b' ' } else { b[i] });
        i += 1;
    }
    String::from_utf8_lossy(&out).into_owned()
}

pub fn parse_lsm_uri(uri: &str) -> Result<LsmUri> {
    let uri = uri.trim();
    if uri.starts_with("lsm://add?") {
        let addr = uri_param(uri, "a");
        if addr.is_empty() {
            bail!("链接缺少地址");
        }
        return Ok(LsmUri::Add { addr, fp: uri_param(uri, "fp"), name: uri_param(uri, "n") });
    }
    if uri.starts_with("lsm://relay?") {
        let host = uri_param(uri, "h");
        if host.is_empty() {
            bail!("链接缺少服务器地址");
        }
        let port = uri_param(uri, "p").parse::<u16>().unwrap_or(9443);
        return Ok(LsmUri::Relay { host, port: if port == 0 { 9443 } else { port } });
    }
    if uri.starts_with("lsm://pair?") {
        let r = uri_param(uri, "r");
        let (host, port) = if let Some((h, p)) = r.rsplit_once(':') {
            (h.to_string(), p.parse::<u16>().unwrap_or(9443))
        } else if !r.is_empty() {
            (r, 9443)
        } else {
            bail!("配对链接缺少服务器");
        };
        let token = decode32(&uri_param(uri, "t")).ok_or_else(|| anyhow!("配对令牌无效"))?;
        let wrap_key = decode32(&uri_param(uri, "k")).ok_or_else(|| anyhow!("配对密钥无效"))?;
        return Ok(LsmUri::Pair { host, port: if port == 0 { 9443 } else { port }, token, wrap_key });
    }
    bail!("不是有效的 lsm:// 链接");
}

fn decode32(h: &str) -> Option<[u8; 32]> {
    let v = hex::decode(h).ok()?;
    if v.len() != 32 {
        return None;
    }
    let mut a = [0u8; 32];
    a.copy_from_slice(&v);
    Some(a)
}


use crate::config::ConfigPaths;
use crate::util::atomic_write;
use std::path::PathBuf;

pub fn recovery_path(paths: &ConfigPaths) -> PathBuf {
    paths.root.join("recovery.dat")
}

pub fn save_recovery_key(paths: &ConfigPaths, rk: &str) -> Result<()> {
    let n = normalize_recovery_key(rk)?;
    let key = crate::history::load_or_create_key(&paths.history_key)?;
    atomic_write(&recovery_path(paths), &crate::crypto::seal_at_rest(&key, n.as_bytes())?)?;
    Ok(())
}

pub fn load_recovery_key(paths: &ConfigPaths) -> Option<String> {
    let d = std::fs::read(recovery_path(paths)).ok()?;
    let key = crate::history::load_or_create_key(&paths.history_key).ok()?;
    let pt = crate::crypto::open_at_rest(&key, &d).ok()?;
    normalize_recovery_key(std::str::from_utf8(&pt).ok()?).ok()
}

pub fn clear_recovery_key(paths: &ConfigPaths) {
    let _ = std::fs::remove_file(recovery_path(paths));
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn recovery_key_roundtrip() {
        let rk = generate_recovery_key();
        assert!(rk.starts_with("RK1-"), "{rk}");
        assert_eq!(rk.chars().filter(|c| *c != '-').count(), 3 + 32);
        let n = normalize_recovery_key(&rk.replace('-', "")).unwrap();
        assert_eq!(n, rk);
        let look = account_lookup(&rk).unwrap();
        assert_eq!(look.len(), 32);
        assert!(look.chars().all(|c| c.is_ascii_hexdigit()));
        assert_eq!(account_lookup(&n).unwrap(), look);
        assert!(normalize_recovery_key("short").is_err());
    }

    #[test]
    fn uris_parse() {
        let r = parse_lsm_uri("lsm://relay?h=example.com&p=9443").unwrap();
        assert_eq!(r, LsmUri::Relay { host: "example.com".into(), port: 9443 });
        let a = parse_lsm_uri("lsm://add?a=10.0.0.2:41235&fp=ABCD&n=Li").unwrap();
        match a {
            LsmUri::Add { addr, fp, name } => {
                assert_eq!(addr, "10.0.0.2:41235");
                assert_eq!(fp, "ABCD");
                assert_eq!(name, "Li");
            }
            _ => panic!("add"),
        }
        let mut tok = [0u8; 32];
        tok[0] = 0xab;
        let mut k = [0u8; 32];
        k[1] = 0xcd;
        let u = pair_uri("10.1.2.3", 9443, &tok, &k);
        let p = parse_lsm_uri(&u).unwrap();
        match p {
            LsmUri::Pair { host, port, token, wrap_key } => {
                assert_eq!(host, "10.1.2.3");
                assert_eq!(port, 9443);
                assert_eq!(token, tok);
                assert_eq!(wrap_key, k);
            }
            _ => panic!("pair"),
        }
    }

    #[test]
    fn pair_wrap_roundtrip() {
        let rk = generate_recovery_key();
        let mut key = [0u8; 32];
        rand::thread_rng().fill_bytes(&mut key);
        let blob = wrap_recovery(&rk, &key).unwrap();
        assert!(blob.starts_with(PAIR_MAGIC));
        assert_eq!(unwrap_recovery(&blob, &key).unwrap(), rk);
        let mut bad = key;
        bad[0] ^= 1;
        assert!(unwrap_recovery(&blob, &bad).is_err());
    }
}
