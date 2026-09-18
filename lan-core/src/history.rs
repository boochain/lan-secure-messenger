//! Encrypted-at-rest local message history.

use crate::config::ConfigPaths;
use crate::crypto::{generate_history_key, open_at_rest, seal_at_rest};
use crate::protocol::ChatMessage;
use anyhow::{Context, Result};
use std::fs;
use tracing::warn;

pub struct HistoryStore {
    key: [u8; 32],
    messages: Vec<ChatMessage>,
    enabled: bool,
    path: std::path::PathBuf,
    key_path: std::path::PathBuf,
}

impl HistoryStore {
    pub fn load(paths: &ConfigPaths, enabled: bool) -> Result<Self> {
        paths.ensure_dirs()?;
        let key = if paths.history_key.exists() {
            let bytes = fs::read(&paths.history_key).context("read history key")?;
            anyhow::ensure!(bytes.len() == 32, "bad history key length");
            let mut k = [0u8; 32];
            k.copy_from_slice(&bytes);
            k
        } else {
            let k = generate_history_key();
            #[cfg(unix)]
            {
                use std::io::Write;
                use std::os::unix::fs::OpenOptionsExt;
                let mut f = fs::OpenOptions::new()
                    .write(true)
                    .create(true)
                    .truncate(true)
                    .mode(0o600)
                    .open(&paths.history_key)?;
                f.write_all(&k)?;
            }
            #[cfg(not(unix))]
            {
                fs::write(&paths.history_key, k)?;
            }
            k
        };

        let messages = if enabled && paths.history.exists() {
            match fs::read(&paths.history) {
                Ok(data) => match open_at_rest(&key, &data) {
                    Ok(plain) => serde_json::from_slice(&plain).unwrap_or_default(),
                    Err(e) => {
                        warn!(error = %e, "history decrypt failed; starting fresh");
                        Vec::new()
                    }
                },
                Err(_) => Vec::new(),
            }
        } else {
            Vec::new()
        };

        Ok(Self {
            key,
            messages,
            enabled,
            path: paths.history.clone(),
            key_path: paths.history_key.clone(),
        })
    }

    pub fn append(&mut self, msg: ChatMessage) -> Result<()> {
        self.messages.push(msg);
        // Cap history
        const MAX: usize = 5000;
        if self.messages.len() > MAX {
            self.messages.drain(0..self.messages.len() - MAX);
        }
        self.persist()
    }

    pub fn list(&self, peer_fp: Option<&str>, limit: usize) -> Vec<ChatMessage> {
        let iter = self.messages.iter().rev().filter(|m| {
            if m.group_id.is_some() {
                return peer_fp.is_none(); // only in unfiltered dumps
            }
            peer_fp.map(|fp| m.peer_fp == fp && m.group_id.is_none()).unwrap_or(m.group_id.is_none())
        });
        let mut out: Vec<_> = iter.take(limit).cloned().collect();
        out.reverse();
        out
    }

    pub fn list_dm(&self, peer_fp: &str, limit: usize) -> Vec<ChatMessage> {
        let iter = self.messages.iter().rev().filter(|m| {
            m.group_id.is_none() && m.peer_fp == peer_fp
        });
        let mut out: Vec<_> = iter.take(limit).cloned().collect();
        out.reverse();
        out
    }

    pub fn list_group(&self, group_id: &uuid::Uuid, limit: usize) -> Vec<ChatMessage> {
        let iter = self.messages.iter().rev().filter(|m| m.group_id.as_ref() == Some(group_id));
        let mut out: Vec<_> = iter.take(limit).cloned().collect();
        out.reverse();
        out
    }

    pub fn mark_delivered(&mut self, id: uuid::Uuid) -> Result<()> {
        if let Some(m) = self.messages.iter_mut().find(|m| m.id == id) {
            m.delivered = true;
            self.persist()?;
        }
        Ok(())
    }

    pub fn all(&self) -> &[ChatMessage] {
        &self.messages
    }

    /// Case-insensitive substring search over body/peer_name (DM + group).
    pub fn search(&self, query: &str, limit: usize) -> Vec<ChatMessage> {
        let q = query.trim().to_lowercase();
        if q.is_empty() {
            return Vec::new();
        }
        let iter = self.messages.iter().rev().filter(|m| {
            m.body.to_lowercase().contains(&q)
                || m.peer_name.to_lowercase().contains(&q)
                || m.peer_fp.to_lowercase().contains(&q)
        });
        let mut out: Vec<_> = iter.take(limit).cloned().collect();
        out.reverse();
        out
    }

    fn persist(&self) -> Result<()> {
        if !self.enabled {
            return Ok(());
        }
        let plain = serde_json::to_vec(&self.messages)?;
        let sealed = seal_at_rest(&self.key, &plain)?;
        fs::write(&self.path, sealed)?;
        let _ = &self.key_path; // kept for clarity
        Ok(())
    }
}
