//! Optional relay accounts: username + passphrase hash + opaque identity backup + retain blobs.
use crate::proto::{self, t, Frame};
use argon2::{Algorithm, Argon2, Params, Version};
use rand::RngCore;
use std::fs;
use std::path::PathBuf;

pub struct AccountStore {
    root: PathBuf,
}

impl AccountStore {
    pub fn new(root: impl Into<PathBuf>) -> Self {
        let root = root.into();
        let _ = fs::create_dir_all(root.join("users"));
        let _ = fs::create_dir_all(root.join("retain"));
        Self { root }
    }

    fn user_dir(&self, user: &str) -> Option<PathBuf> {
        let u = sanitize(user)?;
        Some(self.root.join("users").join(u))
    }

    pub fn register(&self, user: &str, pass: &str, device_fp: &[u8]) -> Result<(), &'static str> {
        let dir = self.user_dir(user).ok_or("bad user")?;
        if dir.exists() {
            return Err("user exists");
        }
        fs::create_dir_all(&dir).map_err(|_| "io")?;
        let mut salt = [0u8; 16];
        rand::thread_rng().fill_bytes(&mut salt);
        let hash = hash_pass(pass, &salt);
        fs::write(dir.join("salt"), salt).map_err(|_| "io")?;
        fs::write(dir.join("pass.hash"), hash).map_err(|_| "io")?;
        if device_fp.len() == 32 {
            let _ = fs::write(dir.join("device.fp"), device_fp);
        }
        Ok(())
    }

    pub fn login(&self, user: &str, pass: &str, device_fp: &[u8]) -> Result<(), &'static str> {
        let dir = self.user_dir(user).ok_or("bad user")?;
        let salt = fs::read(dir.join("salt")).map_err(|_| "no user")?;
        let want = fs::read(dir.join("pass.hash")).map_err(|_| "no user")?;
        if salt.len() != 16 {
            return Err("corrupt");
        }
        let mut s = [0u8; 16];
        s.copy_from_slice(&salt);
        let got = hash_pass(pass, &s);
        if !proto::ct_eq(&got, &want) {
            return Err("bad pass");
        }
        if device_fp.len() == 32 {
            let _ = fs::write(dir.join("device.fp"), device_fp);
        }
        Ok(())
    }

    pub fn put_backup(&self, user: &str, blob: &[u8]) -> Result<(), &'static str> {
        let dir = self.user_dir(user).ok_or("bad user")?;
        if !dir.exists() {
            return Err("no user");
        }
        if blob.len() > 2 * 1024 * 1024 {
            return Err("too large");
        }
        fs::write(dir.join("identity.bak"), blob).map_err(|_| "io")
    }

    pub fn get_backup(&self, user: &str) -> Result<Vec<u8>, &'static str> {
        let dir = self.user_dir(user).ok_or("bad user")?;
        fs::read(dir.join("identity.bak")).map_err(|_| "no backup")
    }

    pub fn store_blob(&self, owner_fp: &[u8], id: &[u8], blob: &[u8], ttl_secs: u32) -> Result<(), &'static str> {
        if owner_fp.len() != 32 || id.is_empty() || id.len() > 64 || blob.len() > 1024 * 1024 {
            return Err("bad args");
        }
        let key = hex(owner_fp) + "_" + &hex(id);
        let path = self.root.join("retain").join(key);
        let exp = now() + ttl_secs as u64;
        let mut out = exp.to_be_bytes().to_vec();
        out.extend_from_slice(blob);
        fs::write(path, out).map_err(|_| "io")
    }

    pub fn fetch_blob(&self, owner_fp: &[u8], id: &[u8]) -> Result<Vec<u8>, &'static str> {
        let key = hex(owner_fp) + "_" + &hex(id);
        let path = self.root.join("retain").join(key);
        let raw = fs::read(&path).map_err(|_| "miss")?;
        if raw.len() < 8 {
            return Err("corrupt");
        }
        let mut expb = [0u8; 8];
        expb.copy_from_slice(&raw[..8]);
        let exp = u64::from_be_bytes(expb);
        if now() > exp {
            let _ = fs::remove_file(path);
            return Err("expired");
        }
        Ok(raw[8..].to_vec())
    }

    pub fn del_blob(&self, owner_fp: &[u8], id: &[u8]) {
        let key = hex(owner_fp) + "_" + &hex(id);
        let _ = fs::remove_file(self.root.join("retain").join(key));
    }
}

fn sanitize(user: &str) -> Option<String> {
    let u = user.trim().to_lowercase();
    if u.is_empty() || u.len() > 64 {
        return None;
    }
    if !u.chars().all(|c| c.is_ascii_alphanumeric() || c == '_' || c == '-' || c == '.') {
        return None;
    }
    Some(u)
}

fn hash_pass(pass: &str, salt: &[u8; 16]) -> [u8; 32] {
    let params = Params::new(19456, 2, 1, Some(32)).expect("argon");
    let a = Argon2::new(Algorithm::Argon2id, Version::V0x13, params);
    let mut out = [0u8; 32];
    a.hash_password_into(pass.as_bytes(), salt, &mut out).expect("hash");
    out
}

fn hex(b: &[u8]) -> String {
    b.iter().map(|x| format!("{x:02x}")).collect()
}

fn now() -> u64 {
    std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or(0)
}

/// Build ACCOUNT_OK / FAIL frames.
pub fn account_ok() -> Frame {
    Frame::new(t::ACCOUNT_OK, Vec::new())
}
pub fn account_fail(msg: &str) -> Frame {
    Frame::new(t::ACCOUNT_FAIL, msg.as_bytes().to_vec())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn register_login_backup_retain() {
        let dir = std::env::temp_dir().join(format!("lsm-acct-{}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        let store = AccountStore::new(&dir);
        let me = [9u8; 32];
        store.register("alice", "secret12", &me).unwrap();
        assert!(store.register("alice", "secret12", &me).is_err());
        store.login("alice", "secret12", &me).unwrap();
        assert!(store.login("alice", "wrong", &me).is_err());
        store.put_backup("alice", b"LSMBAK02demo").unwrap();
        assert_eq!(store.get_backup("alice").unwrap(), b"LSMBAK02demo");
        let dest = [7u8; 32];
        store.store_blob(&dest, b"msgid1", b"cipher", 60).unwrap();
        assert_eq!(store.fetch_blob(&dest, b"msgid1").unwrap(), b"cipher");
        store.del_blob(&dest, b"msgid1");
        assert!(store.fetch_blob(&dest, b"msgid1").is_err());
        let _ = fs::remove_dir_all(&dir);
    }
}
