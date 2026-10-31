//! v0.6.1 P2: pinned chats, drafts, pinned / starred messages (local), typing indicator, forward.
use super::*;
use serde::Deserialize;
use serde_json::{json, Value};
use std::collections::{BTreeMap, BTreeSet};
use std::path::{Path, PathBuf};

fn p<T: for<'de> Deserialize<'de>>(v: Value) -> std::result::Result<T, RpcError> {
    serde_json::from_value(v).map_err(|e| RpcError::invalid(e.to_string()))
}
fn ok<T: serde::Serialize>(v: T) -> RpcResult {
    serde_json::to_value(v).map_err(|e| RpcError::new(-32603, e.to_string()))
}

#[derive(Default, serde::Serialize, serde::Deserialize)]
pub(crate) struct ConvPrefs {
    #[serde(default)]
    pub pinned: BTreeSet<String>,
    #[serde(default)]
    pub drafts: BTreeMap<String, String>,
    #[serde(skip)]
    path: PathBuf,
    #[serde(skip)]
    key: [u8; 32],
    #[serde(skip)]
    last_typing: BTreeMap<String, i64>,
}

impl ConvPrefs {
    /// Sealed at rest with the history key (drafts are private text).
    pub fn load(base: &Path, key: [u8; 32]) -> Self {
        let path = base.join("conv_prefs.bin");
        let mut c: ConvPrefs = std::fs::read(&path).ok().and_then(|b| open_at_rest(&key, &b).ok()).and_then(|b| serde_json::from_slice(&b).ok()).unwrap_or_default();
        c.path = path;
        c.key = key;
        c
    }
    pub fn save(&self) {
        if let Some(b) = serde_json::to_vec(self).ok().and_then(|b| seal_at_rest(&self.key, &b).ok()) {
            let tmp = self.path.with_extension("tmp");
            if std::fs::write(&tmp, b).is_ok() {
                let _ = std::fs::rename(&tmp, &self.path);
            }
        }
    }
}

impl Engine {
    fn conv_exists(&self, conv: &str) -> bool {
        if let Some(fp) = conv.strip_prefix("dm:") {
            return self.contacts.get(fp).is_some() || self.rt.contains_key(fp);
        }
        conv.strip_prefix("group:").and_then(|g| Uuid::parse_str(g).ok()).map(|g| self.groups.get(&g).is_some()).unwrap_or(false)
    }

    pub(crate) fn rpc_p2(&mut self, method: &str, params: Value) -> Option<RpcResult> {
        Some(match method {
            "set_conv_pinned" => (|| {
                #[derive(Deserialize)]
                struct P { conversation: String, pinned: bool }
                let a: P = p(params)?;
                if !self.conv_exists(&a.conversation) {
                    return Err(RpcError::invalid("会话不存在"));
                }
                if a.pinned { self.conv_prefs.pinned.insert(a.conversation.clone()); } else { self.conv_prefs.pinned.remove(&a.conversation); }
                self.conv_prefs.save();
                self.emit_conv(&a.conversation);
                ok(json!({"ok": true}))
            })(),
            "set_draft" => (|| {
                #[derive(Deserialize)]
                struct P { conversation: String, #[serde(default)] text: String }
                let a: P = p(params)?;
                let t: String = a.text.chars().take(20_000).collect();
                let changed = if t.trim().is_empty() { self.conv_prefs.drafts.remove(&a.conversation).is_some() } else { self.conv_prefs.drafts.insert(a.conversation.clone(), t.clone()).as_deref() != Some(t.as_str()) };
                if changed {
                    self.conv_prefs.save();
                    self.emit_conv(&a.conversation);
                }
                ok(json!({"ok": true}))
            })(),
            "set_message_flags" => (|| {
                #[derive(Deserialize)]
                struct P { id: String, pinned: Option<bool>, starred: Option<bool> }
                let a: P = p(params)?;
                if self.history.get(&a.id).is_none() {
                    return Err(RpcError::invalid("消息不存在"));
                }
                self.update_msg(&a.id, |m| {
                    if let Some(v) = a.pinned { m.ext.pinned = v; }
                    if let Some(v) = a.starred { m.ext.starred = v; }
                });
                let _ = self.history.flush(false);
                ok(self.history.get(&a.id).cloned())
            })(),
            "list_flagged" => (|| {
                #[derive(Deserialize)]
                struct P { flag: String, #[serde(default)] conversation: Option<String> }
                let a: P = p(params)?;
                let star = match a.flag.as_str() { "starred" => true, "pinned" => false, _ => return Err(RpcError::invalid("flag 必须是 starred 或 pinned")) };
                let v: Vec<Message> = self.history.messages.iter().filter(|m| if star { m.ext.starred } else { m.ext.pinned })
                    .filter(|m| a.conversation.as_deref().map(|c| c == m.conversation).unwrap_or(true)).cloned().collect();
                ok(v)
            })(),
            "send_typing" => (|| {
                #[derive(Deserialize)]
                struct P { conversation: String }
                let a: P = p(params)?;
                let now = now_ms();
                if now - self.conv_prefs.last_typing.get(&a.conversation).copied().unwrap_or(0) < 3000 {
                    return ok(json!({"ok": true, "throttled": true}));
                }
                self.conv_prefs.last_typing.insert(a.conversation.clone(), now);
                self.send_typing_now(&a.conversation)?;
                ok(json!({"ok": true}))
            })(),
            "forward_message" => (|| {
                #[derive(Deserialize)]
                struct P { id: String, conversations: Vec<String> }
                let a: P = p(params)?;
                let m = self.history.get(&a.id).cloned().ok_or_else(|| RpcError::invalid("消息不存在"))?;
                if m.ext.recalled || m.kind == MsgKind::System {
                    return Err(RpcError::invalid("这条消息不能转发"));
                }
                let mut out = vec![];
                for conv in a.conversations.iter().take(20) {
                    match m.kind {
                        MsgKind::File => {
                            let path = m.file.as_ref().and_then(|f| f.path.clone()).filter(|p| std::path::Path::new(p).exists()).ok_or_else(|| RpcError::invalid("文件不在本机，无法转发"))?;
                            out.push(self.rpc_sync("send_file", json!({"conversation": conv, "path": path}))?);
                        }
                        _ => out.push(self.forward_text(conv, &m.body)?),
                    }
                }
                ok(out)
            })(),
            _ => return None,
        })
    }

    /// fire-and-forget: only to peers online right now with CAP_EXT (never queued)
    fn send_typing_now(&mut self, conv: &str) -> Result<(), RpcError> {
        let data = serde_json::to_vec(&json!({"k":"typing"})).unwrap_or_default();
        let ts = now_ms();
        let targets: Vec<(String, Payload)> = if let Some(fp) = conv.strip_prefix("dm:") {
            vec![(fp.to_string(), Payload::Ext { group_id: None, key_epoch: 0, data, ts_ms: ts })]
        } else {
            let gid = conv.strip_prefix("group:").and_then(|g| Uuid::parse_str(g).ok()).ok_or_else(|| RpcError::invalid("会话不存在"))?;
            let g = self.groups.get(&gid).cloned().ok_or_else(|| RpcError::invalid("群组不存在"))?;
            let sealed = seal_at_rest(&g.group_key()?, &data)?;
            g.members.iter().filter(|x| **x != self.me).map(|m| (m.clone(), Payload::Ext { group_id: Some(gid), key_epoch: g.key_epoch, data: sealed.clone(), ts_ms: ts })).collect()
        };
        for (fp, pl) in targets {
            if self.conns.contains_key(&fp) && self.peer_has_ext(&fp) {
                self.send_ctrl(&fp, Frame::Msg { id: Uuid::new_v4(), payload: pl });
            }
        }
        Ok(())
    }

    fn forward_text(&mut self, conv: &str, body: &str) -> RpcResult {
        if !self.conv_exists(conv) {
            return Err(RpcError::invalid("会话不存在"));
        }
        if let Some(fp) = conv.strip_prefix("dm:") {
            if self.contacts.trust(fp) == Trust::Blocked {
                return Err(RpcError::invalid("该联系人已被屏蔽"));
            }
        }
        let id = Uuid::new_v4();
        let mut m = Message {
            id: id.to_string(),
            conversation: conv.to_string(),
            direction: Direction::Out,
            sender_fp: self.me.clone(),
            sender_name: self.settings.display_name.clone(),
            kind: MsgKind::Text,
            body: body.to_string(),
            ts_ms: now_ms(),
            status: MsgStatus::Queued,
            file: None,
            ext: MsgExt { forwarded: true, ..Default::default() },
        };
        let fb = format!("[转发] {body}");
        if self.send_ext(conv, &json!({"k":"text","body":body,"fwd":true}), Some(&fb), id, Some(m.id.clone()))? {
            m.status = MsgStatus::Sent;
        }
        self.remember_id(id);
        self.add_message(m.clone(), false);
        ok(m)
    }
}
