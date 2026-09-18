//! LAN group chat: shared group key distributed over Noise 1:1 to members.
//!
//! Threat model (honest):
//! - Group key is created by the group creator and sent to each member inside
//!   an existing Noise XX session (so only that peer can read the invite).
//! - Membership list is Ed25519-signed by the creator; members should verify.
//! - Group messages are fan-out over 1:1 Noise sessions (not IP multicast crypto).
//! - Offline members do not receive store-and-forward (LAN MVP limitation).
//! - On member remove, creator rotates the group key and redistributes over Noise
//!   to remaining members. Offline catch-up of missed messages is still absent.
//! - Removed members may still decrypt *old* messages if they retained ciphertext.

use crate::config::ConfigPaths;
use crate::crypto::{open_at_rest, seal_at_rest};
use crate::identity::Identity;
use anyhow::{anyhow, Context, Result};
use rand::RngCore;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::fs;
use std::path::PathBuf;
use uuid::Uuid;
use zeroize::Zeroize;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct GroupInfo {
    pub id: Uuid,
    pub name: String,
    pub creator_fp: String,
    /// Member fingerprints (including creator).
    pub members: Vec<String>,
    /// Shared group key (32 bytes) — sealed at rest with history key material file.
    pub group_key_hex: String,
    pub created_ms: i64,
    /// Invite / join code (short), optional convenience for LAN.
    #[serde(default)]
    pub join_code: String,
    /// Incremented on each membership rekey.
    #[serde(default)]
    pub key_epoch: u64,
}

impl GroupInfo {
    pub fn group_key(&self) -> Result<[u8; 32]> {
        let bytes = hex::decode(&self.group_key_hex).context("decode group key")?;
        anyhow::ensure!(bytes.len() == 32, "bad group key len");
        let mut k = [0u8; 32];
        k.copy_from_slice(&bytes);
        Ok(k)
    }

    pub fn is_member(&self, fp: &str) -> bool {
        self.members.iter().any(|m| m == fp)
    }
}

#[derive(Debug, Default, Serialize, Deserialize)]
struct GroupFile {
    groups: Vec<GroupInfo>,
}

#[derive(Debug, Default)]
pub struct GroupStore {
    groups: HashMap<Uuid, GroupInfo>,
    path: PathBuf,
    /// Key used to seal group_key_hex at rest (reuse history key file bytes).
    seal_key: [u8; 32],
}

impl GroupStore {
    pub fn load(paths: &ConfigPaths) -> Result<Self> {
        paths.ensure_dirs()?;
        let seal_key = load_or_create_seal_key(paths)?;
        let mut store = Self {
            groups: HashMap::new(),
            path: paths.groups.clone(),
            seal_key,
        };
        if paths.groups.exists() {
            let raw = fs::read(&paths.groups).context("read groups")?;
            if raw.len() >= 12 + 16 {
                match open_at_rest(&store.seal_key, &raw) {
                    Ok(plain) => {
                        let file: GroupFile = serde_json::from_slice(&plain).unwrap_or_default();
                        for g in file.groups {
                            store.groups.insert(g.id, g);
                        }
                    }
                    Err(e) => {
                        tracing::warn!(error = %e, "groups decrypt failed; starting empty");
                    }
                }
            }
        }
        Ok(store)
    }

    pub fn save(&self) -> Result<()> {
        let file = GroupFile {
            groups: self.groups.values().cloned().collect(),
        };
        let plain = serde_json::to_vec_pretty(&file)?;
        let sealed = seal_at_rest(&self.seal_key, &plain)?;
        if let Some(parent) = self.path.parent() {
            fs::create_dir_all(parent)?;
        }
        fs::write(&self.path, sealed)?;
        Ok(())
    }

    pub fn create(
        &mut self,
        name: &str,
        creator_fp: &str,
        member_fps: Vec<String>,
        identity: &Identity,
    ) -> Result<GroupInfo> {
        let mut key = [0u8; 32];
        rand::thread_rng().fill_bytes(&mut key);
        let id = Uuid::new_v4();
        let mut members = member_fps;
        if !members.iter().any(|m| m == creator_fp) {
            members.push(creator_fp.to_string());
        }
        members.sort();
        members.dedup();
        let join_code = format!("{:06}", rand::thread_rng().gen_range(0..1_000_000u32));
        let info = GroupInfo {
            id,
            name: name.to_string(),
            creator_fp: creator_fp.to_string(),
            members,
            group_key_hex: hex::encode(key),
            created_ms: chrono::Utc::now().timestamp_millis(),
            join_code,
            key_epoch: 0,
        };
        key.zeroize();
        let _ = identity; // reserved for signed invites at call site
        self.groups.insert(id, info.clone());
        self.save()?;
        Ok(info)
    }

    pub fn upsert(&mut self, info: GroupInfo) -> Result<()> {
        self.groups.insert(info.id, info);
        self.save()
    }

    pub fn get(&self, id: &Uuid) -> Option<&GroupInfo> {
        self.groups.get(id)
    }

    pub fn get_mut(&mut self, id: &Uuid) -> Option<&mut GroupInfo> {
        self.groups.get_mut(id)
    }

    pub fn list(&self) -> Vec<GroupInfo> {
        let mut v: Vec<_> = self.groups.values().cloned().collect();
        v.sort_by(|a, b| a.name.cmp(&b.name));
        v
    }

    pub fn leave(&mut self, id: &Uuid, my_fp: &str) -> Result<GroupInfo> {
        let g = self
            .groups
            .get_mut(id)
            .ok_or_else(|| anyhow!("group not found"))?;
        g.members.retain(|m| m != my_fp);
        let info = g.clone();
        if !info.is_member(my_fp) {
            self.groups.remove(id);
        }
        self.save()?;
        Ok(info)
    }


    /// Creator removes a member and rotates the shared key (forward secrecy vs removed member for *new* messages).
    pub fn remove_and_rekey(&mut self, id: &Uuid, creator_fp: &str, member_fp: &str) -> Result<GroupInfo> {
        let g = self
            .groups
            .get_mut(id)
            .ok_or_else(|| anyhow!("group not found"))?;
        anyhow::ensure!(g.creator_fp == creator_fp, "only creator can remove members");
        anyhow::ensure!(member_fp != creator_fp, "creator cannot remove self; use leave");
        anyhow::ensure!(g.is_member(member_fp), "not a member");
        g.members.retain(|m| m != member_fp);
        let mut key = [0u8; 32];
        rand::thread_rng().fill_bytes(&mut key);
        g.group_key_hex = hex::encode(key);
        key.zeroize();
        g.key_epoch = g.key_epoch.saturating_add(1);
        let info = g.clone();
        self.save()?;
        Ok(info)
    }

    pub fn find_by_code(&self, code: &str) -> Option<&GroupInfo> {
        self.groups.values().find(|g| g.join_code == code)
    }

    pub fn remove(&mut self, id: &Uuid) -> Result<()> {
        self.groups.remove(id);
        self.save()
    }
}

fn load_or_create_seal_key(paths: &ConfigPaths) -> Result<[u8; 32]> {
    // Prefer dedicated groups.key; fall back to history.key
    let groups_key = paths.root.join("groups.key");
    if groups_key.exists() {
        let bytes = fs::read(&groups_key)?;
        anyhow::ensure!(bytes.len() == 32, "bad groups.key");
        let mut k = [0u8; 32];
        k.copy_from_slice(&bytes);
        return Ok(k);
    }
    if paths.history_key.exists() {
        let bytes = fs::read(&paths.history_key)?;
        if bytes.len() == 32 {
            let mut k = [0u8; 32];
            k.copy_from_slice(&bytes);
            let _ = fs::write(&groups_key, k);
            return Ok(k);
        }
    }
    let mut k = [0u8; 32];
    rand::thread_rng().fill_bytes(&mut k);
    #[cfg(unix)]
    {
        use std::io::Write;
        use std::os::unix::fs::OpenOptionsExt;
        let mut f = fs::OpenOptions::new()
            .write(true)
            .create(true)
            .truncate(true)
            .mode(0o600)
            .open(&groups_key)?;
        f.write_all(&k)?;
    }
    #[cfg(not(unix))]
    {
        fs::write(&groups_key, k)?;
    }
    Ok(k)
}

/// Canonical bytes for creator signature over a group invite.
pub fn invite_sign_bytes(
    group_id: &Uuid,
    name: &str,
    creator_fp: &str,
    members: &[String],
    group_key: &[u8; 32],
    created_ms: i64,
) -> Vec<u8> {
    let mut v = Vec::new();
    v.extend_from_slice(group_id.as_bytes());
    v.extend_from_slice(name.as_bytes());
    v.push(0);
    v.extend_from_slice(creator_fp.as_bytes());
    v.push(0);
    let mut ms = members.to_vec();
    ms.sort();
    for m in ms {
        v.extend_from_slice(m.as_bytes());
        v.push(0);
    }
    v.extend_from_slice(group_key);
    v.extend_from_slice(&created_ms.to_le_bytes());
    v
}

use rand::Rng;

/// Canonical bytes for creator signature over a group rekey.
pub fn rekey_sign_bytes(
    group_id: &uuid::Uuid,
    members: &[String],
    group_key: &[u8; 32],
    key_epoch: u64,
    removed_fp: &str,
) -> Vec<u8> {
    let mut v = Vec::new();
    v.extend_from_slice(b"REKEY1");
    v.extend_from_slice(group_id.as_bytes());
    let mut ms = members.to_vec();
    ms.sort();
    for m in ms {
        v.extend_from_slice(m.as_bytes());
        v.push(0);
    }
    v.extend_from_slice(group_key);
    v.extend_from_slice(&key_epoch.to_le_bytes());
    v.extend_from_slice(removed_fp.as_bytes());
    v
}
