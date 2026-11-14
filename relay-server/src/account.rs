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
        let _ = fs::create_dir_all(root.join("pairs"));
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
        if pass.is_empty() {
            // v0.7.1 key-based account: no server-side password. Ownership = recovery key
            // (client-side) or a pairing token. See docs/account-mode.md.
            fs::write(dir.join("auth.v"), b"2").map_err(|_| "io")?;
        } else {
            let mut salt = [0u8; 16];
            rand::thread_rng().fill_bytes(&mut salt);
            let hash = hash_pass(pass, &salt);
            fs::write(dir.join("salt"), salt).map_err(|_| "io")?;
            fs::write(dir.join("pass.hash"), hash).map_err(|_| "io")?;
        }
        if device_fp.len() == 32 {
            self.record_device(&dir, device_fp);
        }
        Ok(())
    }

    pub fn login(&self, user: &str, pass: &str, device_fp: &[u8]) -> Result<(), &'static str> {
        let dir = self.user_dir(user).ok_or("bad user")?;
        if !dir.exists() {
            return Err("no user");
        }
        if dir.join("pass.hash").exists() {
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
        } else if !dir.join("auth.v").exists() {
            return Err("no user");
        }
        // passwordless (auth.v): presence of the directory is enough; ciphertext backup
        // is decrypted client-side with the recovery key.
        if device_fp.len() == 32 {
            self.record_device(&dir, device_fp);
        }
        Ok(())
    }

    pub fn put_backup(&self, user: &str, blob: &[u8]) -> Result<(), &'static str> {
        let dir = self.user_dir(user).ok_or("bad user")?;
        if !dir.exists() {
            return Err("no user");
        }
        if blob.len() > 12 * 1024 * 1024 {
            return Err("too large");
        }
        // Write beside the live backup, keep one previous generation, then rename.
        // A crash cannot leave a truncated identity.bak as the only copy.
        let dest = dir.join("identity.bak");
        let tmp = dir.join("identity.bak.tmp");
        let prev = dir.join("identity.bak.prev");
        fs::write(&tmp, blob).map_err(|_| "io")?;
        if dest.exists() {
            let _ = fs::rename(&dest, &prev);
        }
        fs::rename(&tmp, &dest).map_err(|_| "io")?;
        Ok(())
    }

    pub fn get_backup(&self, user: &str) -> Result<Vec<u8>, &'static str> {
        self.get_backup_gen(user, 0)
    }

    /// `which` 0 = live backup, 1 = the previous generation kept across an upload.
    pub fn get_backup_gen(&self, user: &str, which: u8) -> Result<Vec<u8>, &'static str> {
        let dir = self.user_dir(user).ok_or("bad user")?;
        let name = match which {
            0 => "identity.bak",
            1 => "identity.bak.prev",
            _ => return Err("bad gen"),
        };
        fs::read(dir.join(name)).map_err(|_| if which == 1 { "no previous backup" } else { "no backup" })
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

    /// List retain blob ids owned by this peer (filename: {ownerhex}_{idhex}).
    pub fn list_blobs(&self, owner_fp: &[u8]) -> Vec<Vec<u8>> {
        let prefix = hex(owner_fp) + "_";
        let Ok(rd) = fs::read_dir(self.root.join("retain")) else { return vec![] };
        let mut out = Vec::new();
        for e in rd.flatten() {
            let name = e.file_name().to_string_lossy().to_string();
            if !name.starts_with(&prefix) { continue; }
            let idhex = &name[prefix.len()..];
            // skip expired
            if let Ok(raw) = fs::read(e.path()) {
                if raw.len() >= 8 {
                    let mut expb = [0u8; 8];
                    expb.copy_from_slice(&raw[..8]);
                    if now() > u64::from_be_bytes(expb) {
                        let _ = fs::remove_file(e.path());
                        continue;
                    }
                }
            }
            if let Ok(id) = decode_hex_loose(idhex) {
                if !id.is_empty() { out.push(id); }
            }
        }
        out
    }

    pub fn gc_expired(&self) -> u32 {
        let Ok(rd) = fs::read_dir(self.root.join("retain")) else { return 0 };
        let mut n = 0u32;
        for e in rd.flatten() {
            let Ok(raw) = fs::read(e.path()) else { continue };
            if raw.len() < 8 { let _ = fs::remove_file(e.path()); n += 1; continue; }
            let mut expb = [0u8; 8];
            expb.copy_from_slice(&raw[..8]);
            if now() > u64::from_be_bytes(expb) {
                let _ = fs::remove_file(e.path());
                n += 1;
            }
        }
        n
    }

    /// Clear stored device fingerprint for an account (re-login required on that device).
    pub fn revoke_devices(&self, user: &str, pass: &str) -> Result<(), &'static str> {
        let dir = self.user_dir(user).ok_or("bad user")?;
        if dir.join("pass.hash").exists() {
            let salt = fs::read(dir.join("salt")).map_err(|_| "no user")?;
            let want = fs::read(dir.join("pass.hash")).map_err(|_| "no user")?;
            if salt.len() != 16 { return Err("corrupt"); }
            let mut s = [0u8; 16];
            s.copy_from_slice(&salt);
            let got = hash_pass(pass, &s);
            if !proto::ct_eq(&got, &want) { return Err("bad pass"); }
        } else if !dir.exists() {
            return Err("no user");
        }
        let _ = fs::remove_file(dir.join("device.fp"));
        if let Ok(rd) = fs::read_dir(dir.join("devices")) {
            for e in rd.flatten() {
                let _ = fs::remove_file(e.path());
            }
        }
        Ok(())
    }

    /// Signal-style linked device: one file per fingerprint, first-seen preserved.
    fn record_device(&self, dir: &std::path::Path, fp: &[u8]) {
        if fp.len() != 32 {
            return;
        }
        let d = dir.join("devices");
        let _ = fs::create_dir_all(&d);
        let path = d.join(hex(fp));
        let now = now();
        let first = fs::read_to_string(&path).ok().and_then(|s| s.split_whitespace().next().and_then(|x| x.parse().ok())).unwrap_or(now);
        let _ = fs::write(&path, format!("{first} {now}\n"));
        let _ = fs::write(dir.join("device.fp"), fp);
    }

    fn caller_is_device(&self, dir: &std::path::Path, me: &[u8]) -> bool {
        me.len() == 32 && dir.join("devices").join(hex(me)).is_file()
    }

    /// `op` 0 = list, 2 = unlink every device except the caller (this connection).
    pub fn devices(&self, user: &str, me: &[u8], op: u8) -> Result<String, &'static str> {
        let dir = self.user_dir(user).ok_or("bad user")?;
        if !dir.is_dir() {
            return Err("no user");
        }
        if !self.caller_is_device(&dir, me) {
            return Err("not a device");
        }
        if op == 2 {
            if let Ok(rd) = fs::read_dir(dir.join("devices")) {
                let keep = hex(me);
                for e in rd.flatten() {
                    if e.file_name().to_string_lossy() != keep {
                        let _ = fs::remove_file(e.path());
                    }
                }
            }
        } else if op != 0 {
            return Err("bad op");
        }
        let mut lines = Vec::new();
        if let Ok(rd) = fs::read_dir(dir.join("devices")) {
            for e in rd.flatten() {
                let name = e.file_name().to_string_lossy().to_string();
                let body = fs::read_to_string(e.path()).unwrap_or_default();
                let mut it = body.split_whitespace();
                let first = it.next().unwrap_or("0");
                let last = it.next().unwrap_or(first);
                lines.push(format!("{name} {first} {last}"));
            }
        }
        lines.sort();
        Ok(lines.join("\n"))
    }

    fn pair_path(&self, token: &[u8]) -> Option<PathBuf> {
        if token.len() != 32 { return None; }
        Some(self.root.join("pairs").join(hex(token)))
    }

    /// One-time pairing blob (encrypted recovery key). Deleted after a successful get or when expired.
    pub fn pair_put(&self, token: &[u8], ttl_secs: u32, blob: &[u8]) -> Result<(), &'static str> {
        if token.len() != 32 || blob.is_empty() || blob.len() > 2 * 1024 * 1024 {
            return Err("bad args");
        }
        let ttl = ttl_secs.clamp(30, 3600);
        fs::create_dir_all(self.root.join("pairs")).map_err(|_| "io")?;
        let path = self.pair_path(token).ok_or("bad args")?;
        let exp = now() + ttl as u64;
        let mut out = exp.to_be_bytes().to_vec();
        out.extend_from_slice(blob);
        fs::write(path, out).map_err(|_| "io")
    }

    pub fn pair_get(&self, token: &[u8]) -> Result<Vec<u8>, &'static str> {
        let path = self.pair_path(token).ok_or("bad args")?;
        let raw = fs::read(&path).map_err(|_| "miss")?;
        let _ = fs::remove_file(&path);
        if raw.len() < 8 { return Err("corrupt"); }
        let mut expb = [0u8; 8];
        expb.copy_from_slice(&raw[..8]);
        if now() > u64::from_be_bytes(expb) { return Err("expired"); }
        Ok(raw[8..].to_vec())
    }

}

fn decode_hex_loose(s: &str) -> Result<Vec<u8>, ()> {
    if s.len() % 2 != 0 { return Err(()); }
    (0..s.len()/2).map(|i| u8::from_str_radix(&s[i*2..i*2+2], 16).map_err(|_| ())).collect()
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
        store.register("bob-key", "", &me).unwrap();
        store.login("bob-key", "", &me).unwrap();
        let tok = [3u8; 32];
        store.pair_put(&tok, 60, b"LSMPAIR1blob").unwrap();
        assert_eq!(store.pair_get(&tok).unwrap(), b"LSMPAIR1blob");
        assert!(store.pair_get(&tok).is_err());
        store.put_backup("alice", b"LSMBAK02demo").unwrap();
        assert_eq!(store.get_backup("alice").unwrap(), b"LSMBAK02demo");
        let other = [4u8; 32];
        store.login("alice", "secret12", &other).unwrap();
        let list = store.devices("alice", &me, 0).unwrap();
        assert!(list.contains(&hex(&me)));
        assert!(list.contains(&hex(&other)));
        assert!(store.devices("alice", &[1u8; 32], 0).is_err());
        let left = store.devices("alice", &me, 2).unwrap();
        assert!(left.contains(&hex(&me)));
        assert!(!left.contains(&hex(&other)));
        store.put_backup("alice", b"LSMBAK02next").unwrap();
        assert_eq!(store.get_backup("alice").unwrap(), b"LSMBAK02next");
        assert_eq!(fs::read(dir.join("users/alice/identity.bak.prev")).unwrap(), b"LSMBAK02demo");
        assert_eq!(store.get_backup_gen("alice", 1).unwrap(), b"LSMBAK02demo");
        assert!(store.get_backup_gen("alice", 2).is_err());
        let dest = [7u8; 32];
        store.store_blob(&dest, b"msgid1", b"cipher", 60).unwrap();
        assert_eq!(store.fetch_blob(&dest, b"msgid1").unwrap(), b"cipher");
        store.store_blob(&dest, b"msgid2", b"cipher2", 60).unwrap();
        let listed = store.list_blobs(&dest);
        assert!(listed.iter().any(|i| i == b"msgid2"));
        assert_eq!(store.gc_expired(), 0);
        store.revoke_devices("alice", "secret12").unwrap();
        assert!(!dir.join("users/alice/devices").join(hex(&me)).exists());
        assert!(!dir.join("users/alice/devices").join(hex(&other)).exists());
        store.del_blob(&dest, b"msgid1");
        store.del_blob(&dest, b"msgid2");
        assert!(store.fetch_blob(&dest, b"msgid1").is_err());
        let _ = fs::remove_dir_all(&dir);
    }
}
