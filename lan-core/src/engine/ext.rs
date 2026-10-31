//! v0.6.1 P1: reply/quote, reactions, edit, recall over `Payload::Ext` (CAP_EXT).
//! Peers without CAP_EXT get a readable plain-text fallback (reactions are not sent to them).
use super::*;

/// Edits / recalls are allowed for this long after sending.
fn p<T: for<'de> Deserialize<'de>>(v: Value) -> std::result::Result<T, RpcError> {
    serde_json::from_value(v).map_err(|e| RpcError::invalid(e.to_string()))
}
fn ok<T: serde::Serialize>(v: T) -> RpcResult {
    serde_json::to_value(v).map_err(|e| RpcError::new(-32000, e.to_string()))
}

pub(crate) const EDIT_WINDOW_MS: i64 = 24 * 3600 * 1000;

impl Engine {
    pub(crate) fn peer_has_ext(&self, fp: &str) -> bool {
        if let Some(c) = self.conns.get(fp) {
            return c.caps & crate::wire::CAP_EXT != 0;
        }
        self.contacts.get(fp).map(|c| c.caps & crate::wire::CAP_EXT != 0).unwrap_or(false)
    }

    pub(crate) fn quote_of(&self, id: &str) -> String {
        let Some(m) = self.history.get(id) else { return String::new() };
        let who = if m.direction == Direction::Out { "我".to_string() } else { m.sender_name.clone() };
        let what = if m.ext.recalled { "（已撤回）".to_string() } else if m.kind == MsgKind::File { format!("[文件] {}", m.file.as_ref().map(|f| f.name.as_str()).unwrap_or("")) } else { m.body.clone() };
        let what: String = what.replace('\n', " ").chars().take(80).collect();
        format!("{who}：{what}")
    }

    /// Send one Ext op to a conversation. `fallback` = plain text for peers without CAP_EXT (None = skip them).
    /// Returns true if at least one recipient was online.
    pub(crate) fn send_ext(&mut self, conv: &str, op: &Value, fallback: Option<&str>, id: Uuid, msg_ref: Option<String>) -> Result<bool, RpcError> {
        let data = serde_json::to_vec(op).map_err(|e| RpcError::invalid(e.to_string()))?;
        let ts = now_ms();
        if let Some(fp) = conv.strip_prefix("dm:") {
            let fp = fp.to_string();
            if self.peer_has_ext(&fp) {
                return Ok(self.send_reliable(&fp, Payload::Ext { group_id: None, key_epoch: 0, data, ts_ms: ts }, id, msg_ref));
            }
            if let Some(t) = fallback {
                return Ok(self.send_reliable(&fp, Payload::Text { body: t.to_string(), ts_ms: ts }, id, msg_ref));
            }
            return Ok(false);
        }
        let gid = conv.strip_prefix("group:").and_then(|g| Uuid::parse_str(g).ok()).ok_or_else(|| RpcError::invalid("conversation 必须是 dm:<指纹> 或 group:<id>"))?;
        let g = self.groups.get(&gid).cloned().ok_or_else(|| RpcError::invalid("群组不存在"))?;
        let key = g.group_key()?;
        let sealed = seal_at_rest(&key, &data)?;
        let fb = match fallback { Some(t) => Some(seal_at_rest(&key, t.as_bytes())?), None => None };
        let mut any = false;
        for mem in g.members.iter().filter(|x| **x != self.me).cloned().collect::<Vec<_>>() {
            if self.peer_has_ext(&mem) {
                any |= self.send_reliable(&mem, Payload::Ext { group_id: Some(gid), key_epoch: g.key_epoch, data: sealed.clone(), ts_ms: ts }, id, msg_ref.clone());
            } else if let Some(f) = &fb {
                any |= self.send_reliable(&mem, Payload::GroupText { group_id: gid, key_epoch: g.key_epoch, sealed: f.clone(), ts_ms: ts }, id, msg_ref.clone());
            }
        }
        Ok(any)
    }

    pub(crate) fn on_ext(&mut self, fp: &str, id: Uuid, group_id: Option<Uuid>, data: Vec<u8>, ts_ms: i64) {
        let name = self.peer_name(fp);
        let (conv, plain) = match group_id {
            None => (dm_conv(fp), data),
            Some(gid) => {
                let Some(g) = self.groups.get(&gid).cloned() else { return };
                if !g.is_member(fp) {
                    return;
                }
                match g.group_key().and_then(|k| open_at_rest(&k, &data)) {
                    Ok(p) => (group_conv(&gid), p),
                    Err(_) => {
                        self.system_msg(&group_conv(&gid), format!("收到来自 {name} 的扩展消息但无法解密"));
                        return;
                    }
                }
            }
        };
        let Ok(op) = serde_json::from_slice::<Value>(&plain) else { return };
        let k = op.get("k").and_then(|v| v.as_str()).unwrap_or("");
        let target = op.get("target").and_then(|v| v.as_str()).unwrap_or("").to_string();
        let body = op.get("body").and_then(|v| v.as_str()).unwrap_or("").chars().take(64 * 1024).collect::<String>();
        match k {
            "text" => {
                let reply_to = op.get("reply_to").and_then(|v| v.as_str()).map(|s| s.to_string()).filter(|s| !s.is_empty());
                let quote = op.get("quote").and_then(|v| v.as_str()).unwrap_or("").chars().take(200).collect::<String>();
                let mentions: Vec<String> = op.get("mentions").and_then(|v| v.as_array()).map(|a| a.iter().filter_map(|x| x.as_str().map(|s| s.to_string())).take(64).collect()).unwrap_or_default();
                let quote = if quote.is_empty() { reply_to.as_deref().map(|r| self.quote_of(r)).unwrap_or_default() } else { quote };
                let m = Message {
                    id: id.to_string(),
                    conversation: conv,
                    direction: Direction::In,
                    sender_fp: fp.to_string(),
                    sender_name: name,
                    kind: MsgKind::Text,
                    body,
                    ts_ms: if ts_ms > 0 { ts_ms } else { now_ms() },
                    status: MsgStatus::Received,
                    file: None,
                    ext: MsgExt { reply_to, quote, mentions_me: mentions.iter().any(|m| *m == self.me), mentions, forwarded: op.get("fwd").and_then(|v| v.as_bool()).unwrap_or(false), ..Default::default() },
                };
                self.add_message(m, true);
            }
            "react" => {
                let emoji: String = op.get("emoji").and_then(|v| v.as_str()).unwrap_or("").chars().take(8).collect();
                let fp_s = fp.to_string();
                if self.history.get(&target).map(|m| m.conversation == conv).unwrap_or(false) {
                    self.update_msg(&target, |m| {
                        m.ext.reactions.retain(|r| r.fp != fp_s);
                        if !emoji.is_empty() {
                            m.ext.reactions.push(Reaction { fp: fp_s.clone(), emoji });
                        }
                    });
                    let _ = self.history.flush(false);
                }
            }
            "edit" | "recall" => {
                let ok = self.history.get(&target).map(|m| m.conversation == conv && m.sender_fp == fp && m.direction == Direction::In).unwrap_or(false);
                if !ok {
                    tracing::warn!(%fp, %target, "ext {k} for a message the peer did not send: ignored");
                    return;
                }
                let recall = k == "recall";
                self.update_msg(&target, |m| {
                    if recall {
                        m.ext.recalled = true;
                        m.body.clear();
                    } else if !m.ext.recalled {
                        m.body = body;
                        m.ext.edited = true;
                    }
                });
                let _ = self.history.flush(false);
            }
            "typing" => {
                let name = self.peer_name(fp);
                self.emit(Event::Typing { conversation: conv, fingerprint: fp.to_string(), name });
            }
            _ => tracing::debug!(%fp, k, "unknown ext op ignored"),
        }
    }

    pub(crate) fn rpc_ext(&mut self, method: &str, params: Value) -> Option<RpcResult> {
        Some(match method {
            "react" => (|| {
                #[derive(Deserialize)]
                struct P { id: String, emoji: String }
                let a: P = p(params)?;
                let emoji: String = a.emoji.chars().take(8).collect();
                let conv = self.history.get(&a.id).map(|m| m.conversation.clone()).ok_or_else(|| RpcError::invalid("消息不存在"))?;
                let me = self.me.clone();
                // toggle: same emoji again removes it
                let cur = self.history.get(&a.id).and_then(|m| m.ext.reactions.iter().find(|r| r.fp == me).map(|r| r.emoji.clone()));
                let emoji = if cur.as_deref() == Some(emoji.as_str()) { String::new() } else { emoji };
                let e2 = emoji.clone();
                self.update_msg(&a.id, |m| {
                    m.ext.reactions.retain(|r| r.fp != me);
                    if !e2.is_empty() {
                        m.ext.reactions.push(Reaction { fp: me.clone(), emoji: e2 });
                    }
                });
                let _ = self.history.flush(false);
                self.send_ext(&conv, &json!({"k":"react","target":a.id,"emoji":emoji}), None, Uuid::new_v4(), None)?;
                ok(self.history.get(&a.id).cloned())
            })(),
            "edit_message" | "recall_message" => (|| {
                #[derive(Deserialize)]
                struct P { id: String, #[serde(default)] body: String }
                let a: P = p(params)?;
                let m = self.history.get(&a.id).cloned().ok_or_else(|| RpcError::invalid("消息不存在"))?;
                if m.direction != Direction::Out || m.kind == MsgKind::System {
                    return Err(RpcError::invalid("只能修改自己发送的消息"));
                }
                if now_ms() - m.ts_ms > EDIT_WINDOW_MS {
                    return Err(RpcError::invalid("超过 24 小时的消息不能再编辑或撤回"));
                }
                if m.ext.recalled {
                    return Err(RpcError::invalid("消息已撤回"));
                }
                let recall = method == "recall_message";
                let body = a.body.trim_end().to_string();
                if !recall && (body.trim().is_empty() || m.kind != MsgKind::Text) {
                    return Err(RpcError::invalid("只能编辑文字消息，且内容不能为空"));
                }
                let (op, fb) = if recall {
                    (json!({"k":"recall","target":a.id}), "（撤回了一条消息）".to_string())
                } else {
                    (json!({"k":"edit","target":a.id,"body":body}), format!("（已编辑）{body}"))
                };
                let b2 = body.clone();
                self.update_msg(&a.id, |m| {
                    if recall { m.ext.recalled = true; m.body.clear(); } else { m.body = b2; m.ext.edited = true; }
                });
                let _ = self.history.flush(false);
                self.send_ext(&m.conversation, &op, Some(&fb), Uuid::new_v4(), None)?;
                ok(self.history.get(&a.id).cloned())
            })(),
            _ => return None,
        })
    }
}
