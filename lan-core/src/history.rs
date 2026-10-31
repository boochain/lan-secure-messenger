//! Encrypted-at-rest message history (ChaCha20-Poly1305, key in config/history.key).

use crate::api::{Message, MsgKind};
use crate::config::ConfigPaths;
use crate::crypto::{generate_history_key, open_at_rest, seal_at_rest};
use crate::util::atomic_write;
use anyhow::{Context, Result};
use std::fs;
use std::time::{Duration, Instant};

const MAX: usize = 20_000;

/// Filters for `history_query` (all optional; empty = no constraint). Shared semantics with the C core
/// (`lsm_history_query`): see docs/ipc.md.
#[derive(Debug, Clone, Default, serde::Deserialize, serde::Serialize)]
#[serde(default)]
pub struct HistoryQuery {
    /// Full-text, case-insensitive substring over body, sender name and file name (CJK works: plain substring).
    pub query: String,
    /// Restrict to these conversations (`dm:<fp>` / `group:<id>`).
    pub conversations: Vec<String>,
    /// Inclusive lower / exclusive upper bound on `ts_ms` (0 = open).
    pub from_ms: i64,
    pub to_ms: i64,
    /// `text` | `image` | `file` | `sticker`
    pub types: Vec<String>,
    /// `in` | `out` (empty = both)
    pub direction: String,
    /// MsgStatus names: queued sent delivered read failed rejected received
    pub statuses: Vec<String>,
    /// Pagination over the newest-first result list.
    pub offset: usize,
    pub limit: usize,
}

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct HistoryPage {
    /// Newest first.
    pub items: Vec<Message>,
    pub total: usize,
    pub offset: usize,
    pub has_more: bool,
}

const STICKERS: [&str; 6] = ["thumb", "heart", "fire", "star", "party", "check"];

/// Shared rule (all editions): a sticker is a file named `lsm-sticker-*` or one of the built-in sticker
/// names (`thumb.png` ...).
pub fn is_sticker_name(name: &str) -> bool {
    let n = name.to_lowercase();
    let base = n.rsplit(['/', '\\']).next().unwrap_or(&n);
    if base.starts_with("lsm-sticker-") {
        return true;
    }
    base.strip_suffix(".png").map(|s| STICKERS.contains(&s)).unwrap_or(false)
}

pub fn msg_type(m: &Message) -> &'static str {
    match (&m.kind, &m.file) {
        (MsgKind::File, Some(f)) if is_sticker_name(&f.name) => "sticker",
        (MsgKind::File, Some(f)) if f.is_image => "image",
        (MsgKind::File, _) => "file",
        (MsgKind::System, _) => "system",
        _ => "text",
    }
}

fn status_name(s: crate::api::MsgStatus) -> &'static str {
    use crate::api::MsgStatus::*;
    match s { Queued => "queued", Sent => "sent", Delivered => "delivered", Read => "read", Failed => "failed", Rejected => "rejected", Received => "received" }
}

pub fn matches(m: &Message, q: &HistoryQuery, needle: &str) -> bool {
    if m.kind == MsgKind::System {
        return false;
    }
    if !q.conversations.is_empty() && !q.conversations.iter().any(|c| c == &m.conversation) {
        return false;
    }
    if q.from_ms > 0 && m.ts_ms < q.from_ms {
        return false;
    }
    if q.to_ms > 0 && m.ts_ms >= q.to_ms {
        return false;
    }
    if !q.types.is_empty() && !q.types.iter().any(|t| t == msg_type(m)) {
        return false;
    }
    match q.direction.as_str() {
        "in" if m.direction != crate::api::Direction::In => return false,
        "out" if m.direction != crate::api::Direction::Out => return false,
        _ => {}
    }
    if !q.statuses.is_empty() && !q.statuses.iter().any(|s| s == status_name(m.status)) {
        return false;
    }
    if !needle.is_empty() {
        let hit = m.body.to_lowercase().contains(needle)
            || m.sender_name.to_lowercase().contains(needle)
            || m.file.as_ref().map(|f| f.name.to_lowercase().contains(needle)).unwrap_or(false);
        if !hit {
            return false;
        }
    }
    true
}

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

    /// Filtered, paginated history across conversations (newest first).
    pub fn query(&self, q: &HistoryQuery) -> HistoryPage {
        let needle = q.query.trim().to_lowercase();
        let limit = if q.limit == 0 { 50 } else { q.limit.min(500) };
        let mut total = 0usize;
        let mut items = Vec::new();
        for m in self.messages.iter().rev() {
            if !matches(m, q, &needle) {
                continue;
            }
            if total >= q.offset && items.len() < limit {
                items.push(m.clone());
            }
            total += 1;
        }
        HistoryPage { has_more: q.offset + items.len() < total, items, total, offset: q.offset }
    }

    /// Messages around `id` in its conversation (for "jump to message"): returns (conversation, window).
    pub fn context(&self, id: &str, before: usize, after: usize) -> Option<(String, Vec<Message>)> {
        let conv = self.get(id)?.conversation.clone();
        let all: Vec<&Message> = self.messages.iter().filter(|m| m.conversation == conv).collect();
        let i = all.iter().position(|m| m.id == id)?;
        let lo = i.saturating_sub(before);
        let hi = (i + after + 1).min(all.len());
        Some((conv, all[lo..hi].iter().map(|m| (*m).clone()).collect()))
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
            kind: MsgKind::Text, body: "hi".into(), ts_ms: 1, status: MsgStatus::Received, file: None, ext: Default::default() }
    }

    fn mk(id: &str, conv: &str, dir: Direction, kind: MsgKind, body: &str, ts: i64, st: MsgStatus, file: Option<(&str, bool)>) -> Message {
        Message { id: id.into(), conversation: conv.into(), direction: dir, sender_fp: "X".into(), sender_name: "王工".into(), kind, body: body.into(), ts_ms: ts, status: st,
            file: file.map(|(n, img)| crate::api::FileRef { transfer_id: "t".into(), name: n.into(), size: 1, mime: "".into(), path: None, is_image: img }), ext: Default::default() }
    }

    fn store() -> (tempfile::TempDir, HistoryStore) {
        let dir = tempfile::tempdir().unwrap();
        let paths = crate::ConfigPaths::from_base(dir.path());
        let mut h = HistoryStore::load(&paths, true).unwrap();
        h.push(mk("1", "dm:A", Direction::In, MsgKind::Text, "下午三点评审会改到 302 会议室", 1000, MsgStatus::Received, None));
        h.push(mk("2", "dm:A", Direction::Out, MsgKind::Text, "OK, thanks!", 2000, MsgStatus::Read, None));
        h.push(mk("3", "group:G", Direction::In, MsgKind::File, "", 3000, MsgStatus::Received, Some(("会议白板.png", true))));
        h.push(mk("4", "dm:B", Direction::Out, MsgKind::File, "", 4000, MsgStatus::Rejected, Some(("季度报告.txt", false))));
        h.push(mk("5", "dm:B", Direction::Out, MsgKind::File, "", 5000, MsgStatus::Delivered, Some(("lsm-sticker-heart.png", true))));
        h.push(mk("6", "dm:A", Direction::In, MsgKind::System, "系统", 6000, MsgStatus::Received, None));
        (dir, h)
    }

    fn ids(p: &HistoryPage) -> Vec<&str> { p.items.iter().map(|m| m.id.as_str()).collect() }

    #[test]
    fn query_fulltext_cjk_and_case() {
        let (_d, h) = store();
        assert_eq!(ids(&h.query(&HistoryQuery { query: "评审".into(), ..Default::default() })), vec!["1"]);
        assert_eq!(ids(&h.query(&HistoryQuery { query: "THANKS".into(), ..Default::default() })), vec!["2"]);
        assert_eq!(ids(&h.query(&HistoryQuery { query: "白板".into(), ..Default::default() })), vec!["3"], "file names are searched");
        assert_eq!(h.query(&HistoryQuery::default()).total, 5, "system rows are never listed");
    }

    #[test]
    fn query_filters() {
        let (_d, h) = store();
        let q = |f: HistoryQuery| ids(&h.query(&f)).into_iter().map(String::from).collect::<Vec<_>>();
        assert_eq!(q(HistoryQuery { conversations: vec!["dm:A".into()], ..Default::default() }), vec!["2", "1"]);
        assert_eq!(q(HistoryQuery { types: vec!["image".into()], ..Default::default() }), vec!["3"]);
        assert_eq!(q(HistoryQuery { types: vec!["sticker".into()], ..Default::default() }), vec!["5"]);
        assert_eq!(q(HistoryQuery { types: vec!["file".into()], ..Default::default() }), vec!["4"]);
        assert_eq!(q(HistoryQuery { direction: "out".into(), ..Default::default() }), vec!["5", "4", "2"]);
        assert_eq!(q(HistoryQuery { statuses: vec!["rejected".into(), "read".into()], ..Default::default() }), vec!["4", "2"]);
        assert_eq!(q(HistoryQuery { from_ms: 2000, to_ms: 4000, ..Default::default() }), vec!["3", "2"]);
        assert_eq!(q(HistoryQuery { query: "会议".into(), types: vec!["text".into()], ..Default::default() }), vec!["1"]);
    }

    #[test]
    fn query_paginates_and_context_jumps() {
        let (_d, h) = store();
        let p1 = h.query(&HistoryQuery { limit: 2, ..Default::default() });
        assert_eq!((ids(&p1), p1.total, p1.has_more), (vec!["5", "4"], 5, true));
        let p3 = h.query(&HistoryQuery { limit: 2, offset: 4, ..Default::default() });
        assert_eq!((ids(&p3), p3.has_more), (vec!["1"], false));
        let (conv, win) = h.context("2", 5, 5).unwrap();
        assert_eq!(conv, "dm:A");
        assert_eq!(win.iter().map(|m| m.id.as_str()).collect::<Vec<_>>(), vec!["1", "2", "6"]);
        assert!(is_sticker_name("C:\\tmp\\heart.png") && !is_sticker_name("heartbeat.png"));
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
