//! Encrypted-at-rest message history (ChaCha20-Poly1305, key in config/history.key).

use crate::api::{Message, MsgKind};
use crate::config::ConfigPaths;
use crate::crypto::{generate_history_key, open_at_rest, seal_at_rest};
use crate::util::atomic_write;
use anyhow::{Context, Result};
use std::fs;
use std::time::{Duration, Instant};

const MAX: usize = 20_000;

pub struct HistoryStore {
    key: [u8; 32],
    pub messages: Vec<Message>,
    enabled: bool,
    path: std::path::PathBuf,
    dirty: bool,
    last_save: Instant,
    /// ids deleted on this device ("tombstones"): a resend from the peer must not bring them back
    deleted: std::collections::HashSet<String>,
    deleted_order: Vec<String>,
}

const MAX_TOMBSTONES: usize = 20_000;

pub fn load_or_create_key(path: &std::path::Path) -> Result<[u8; 32]> {
    if path.exists() {
        let b = fs::read(path).context("read key")?;
        anyhow::ensure!(b.len() == 32, "bad key length in {}", path.display());
        let mut k = [0u8; 32];
        k.copy_from_slice(&b);
        return Ok(k);
    }
    let k = generate_history_key();
    atomic_write(path, &k)?;
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let _ = fs::set_permissions(path, fs::Permissions::from_mode(0o600));
    }
    Ok(k)
}

impl HistoryStore {
    pub fn load(paths: &ConfigPaths, enabled: bool) -> Result<Self> {
        let key = load_or_create_key(&paths.history_key)?;
        let messages = if paths.history.exists() {
            match fs::read(&paths.history).map_err(anyhow::Error::from).and_then(|d| open_at_rest(&key, &d)) {
                Ok(plain) => serde_json::from_slice(&plain).unwrap_or_default(),
                Err(e) => {
                    tracing::warn!(error = %e, "history unreadable; starting fresh (old file kept as .bad)");
                    let _ = fs::rename(&paths.history, paths.history.with_extension("bad"));
                    Vec::new()
                }
            }
        } else {
            Vec::new()
        };
        let tomb_path = paths.history.with_extension("deleted");
        let deleted_order: Vec<String> = fs::read(&tomb_path).ok().and_then(|d| open_at_rest(&key, &d).ok()).and_then(|p| serde_json::from_slice(&p).ok()).unwrap_or_default();
        let deleted = deleted_order.iter().cloned().collect();
        Ok(Self { key, messages, enabled, path: paths.history.clone(), dirty: false, last_save: Instant::now(), deleted, deleted_order })
    }

    pub fn push(&mut self, m: Message) {
        self.messages.push(m);
        if self.messages.len() > MAX {
            let n = self.messages.len() - MAX;
            self.messages.drain(0..n);
        }
        self.dirty = true;
    }

    pub fn get_mut(&mut self, id: &str) -> Option<&mut Message> {
        self.dirty = true;
        self.messages.iter_mut().rev().find(|m| m.id == id)
    }

    /// Delete one message locally (this device only; the peer keeps its copy). Returns its conversation.
    pub fn remove(&mut self, id: &str) -> Option<String> {
        let i = self.messages.iter().rposition(|m| m.id == id)?;
        self.dirty = true;
        if self.deleted.insert(id.to_string()) {
            self.deleted_order.push(id.to_string());
            if self.deleted_order.len() > MAX_TOMBSTONES {
                let n = self.deleted_order.len() - MAX_TOMBSTONES;
                for old in self.deleted_order.drain(0..n) {
                    self.deleted.remove(&old);
                }
            }
            // tombstones are written immediately (synced) and even with history disabled
            if let Ok(plain) = serde_json::to_vec(&self.deleted_order) {
                if let Ok(sealed) = seal_at_rest(&self.key, &plain) {
                    let _ = atomic_write(&self.path.with_extension("deleted"), &sealed);
                }
            }
        }
        Some(self.messages.remove(i).conversation)
    }

    /// True when this message id was deleted on this device.
    pub fn is_deleted(&self, id: &str) -> bool {
        self.deleted.contains(id)
    }

    pub fn get(&self, id: &str) -> Option<&Message> {
        self.messages.iter().rev().find(|m| m.id == id)
    }

    pub fn contains(&self, id: &str) -> bool {
        self.deleted.contains(id) || self.messages.iter().rev().take(5000).any(|m| m.id == id)
    }

    pub fn conversation(&self, conv: &str, before_ts: Option<i64>, limit: usize) -> Vec<Message> {
        let mut v: Vec<Message> = self
            .messages
            .iter()
            .rev()
            .filter(|m| m.conversation == conv && before_ts.map(|b| m.ts_ms < b).unwrap_or(true))
            .take(limit)
            .cloned()
            .collect();
        v.reverse();
        v
    }

    pub fn last_in(&self, conv: &str) -> Option<&Message> {
        self.messages.iter().rev().find(|m| m.conversation == conv)
    }

    pub fn search(&self, q: &str, limit: usize) -> Vec<Message> {
        let q = q.trim().to_lowercase();
        if q.is_empty() {
            return vec![];
        }
        let mut v: Vec<Message> = self
            .messages
            .iter()
            .rev()
            .filter(|m| {
                m.kind != MsgKind::System
                    && (m.body.to_lowercase().contains(&q)
                        || m.sender_name.to_lowercase().contains(&q)
                        || m.file.as_ref().map(|f| f.name.to_lowercase().contains(&q)).unwrap_or(false))
            })
            .take(limit)
            .cloned()
            .collect();
        v.reverse();
        v
    }

    /// Debounced persistence; call periodically and on shutdown (`force`).
    pub fn flush(&mut self, force: bool) -> Result<()> {
        if !self.enabled || !self.dirty {
            return Ok(());
        }
        if !force && self.last_save.elapsed() < Duration::from_millis(1500) {
            return Ok(());
        }
        let plain = serde_json::to_vec(&self.messages)?;
        atomic_write(&self.path, &seal_at_rest(&self.key, &plain)?)?;
        self.dirty = false;
        self.last_save = Instant::now();
        Ok(())
    }

    pub fn set_enabled(&mut self, on: bool) {
        self.enabled = on;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::api::{Direction, Message, MsgKind, MsgStatus};

    fn msg(id: &str) -> Message {
        Message { id: id.into(), conversation: "dm:X".into(), direction: Direction::In, sender_fp: "X".into(), sender_name: "x".into(),
            kind: MsgKind::Text, body: "hi".into(), ts_ms: 1, status: MsgStatus::Received, file: None }
    }

    #[test]
    fn delete_leaves_a_tombstone_that_survives_restart() {
        let dir = tempfile::tempdir().unwrap();
        let paths = crate::ConfigPaths::from_base(dir.path());
        let mut h = HistoryStore::load(&paths, true).unwrap();
        h.push(msg("a"));
        h.push(msg("b"));
        assert_eq!(h.remove("a").as_deref(), Some("dm:X"));
        assert!(h.contains("a"), "a deleted id still counts as seen, so a resend is dropped");
        assert!(h.get("a").is_none());
        h.flush(true).unwrap();
        let h2 = HistoryStore::load(&paths, true).unwrap();
        assert!(h2.is_deleted("a") && h2.contains("a"));
        assert!(h2.get("a").is_none() && h2.get("b").is_some());
        assert!(!h2.is_deleted("b"));
    }
}
