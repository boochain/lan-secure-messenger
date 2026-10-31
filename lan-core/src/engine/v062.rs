//! v0.6.2: polls (Ext "poll"/"vote"/"poll_close"), chat folders + archive (local), disappearing messages (Ext "ttl").
use super::*;
use serde::Deserialize;
use serde_json::{json, Value};

fn p<T: for<'de> Deserialize<'de>>(v: Value) -> std::result::Result<T, RpcError> {
    serde_json::from_value(v).map_err(|e| RpcError::invalid(e.to_string()))
}
fn ok<T: serde::Serialize>(v: T) -> RpcResult {
    serde_json::to_value(v).map_err(|e| RpcError::new(-32603, e.to_string()))
}

/// allowed disappearing timers (seconds); 0 = off
pub(crate) const TTL_CHOICES: [u32; 6] = [0, 30, 300, 3600, 86_400, 604_800];
pub(crate) const MAX_POLL_OPTIONS: usize = 10;

pub(crate) fn ttl_label(secs: u32) -> String {
    match secs {
        0 => "关闭".into(),
        s if s % 86_400 == 0 => format!("{} 天", s / 86_400),
        s if s % 3600 == 0 => format!("{} 小时", s / 3600),
        s if s % 60 == 0 => format!("{} 分钟", s / 60),
        s => format!("{s} 秒"),
    }
}

pub(crate) fn poll_fallback(q: &str, opts: &[String]) -> String {
    let mut s = format!("[投票] {q}");
    for (i, o) in opts.iter().enumerate() {
        s.push_str(&format!("\n{}. {o}", i + 1));
    }
    s.push_str("\n（对方版本较旧，请升级后参与投票）");
    s
}

fn clean_choices(c: &[u32], n: usize, multi: bool) -> Vec<u32> {
    let mut v: Vec<u32> = c.iter().copied().filter(|i| (*i as usize) < n).collect();
    v.sort_unstable();
    v.dedup();
    if !multi { v.truncate(1); }
    v
}

impl Engine {
    /// every new message: archived chats come back on new incoming mail (unless muted); disappearing timer applies
    pub(crate) fn v062_on_add(&mut self, m: &mut Message) {
        if m.kind == MsgKind::System { return; }
        let ttl = self.conv_prefs.ttl.get(&m.conversation).copied().unwrap_or(0);
        if ttl > 0 && m.ext.expires_ms == 0 {
            m.ext.expires_ms = m.ts_ms.max(now_ms()) + ttl as i64 * 1000;
        }
        if m.direction == Direction::In && !self.is_muted(&m.conversation) && self.conv_prefs.archived.remove(&m.conversation) {
            self.conv_prefs.save();
        }
    }

    pub(crate) fn v062_sweep(&mut self) {
        let now = now_ms();
        if now - self.conv_prefs.last_sweep < 1000 { return; }
        self.conv_prefs.last_sweep = now;
        let gone: Vec<String> = self.history.messages.iter().filter(|m| m.ext.expires_ms > 0 && m.ext.expires_ms <= now).map(|m| m.id.clone()).collect();
        if gone.is_empty() { return; }
        let mut convs = std::collections::BTreeSet::new();
        for id in gone {
            if let Some(conv) = self.history.remove(&id) {
                if let Some(u) = self.unread.get_mut(&conv) { u.retain(|x| x.to_string() != id); }
                self.emit(Event::MessageRemoved { id, conversation: conv.clone() });
                convs.insert(conv);
            }
        }
        let _ = self.history.flush(true);
        for c in convs { self.emit_conv(&c); }
    }

    pub(crate) fn on_ext_v062(&mut self, fp: &str, id: Uuid, conv: &str, k: &str, op: &Value, ts_ms: i64) {
        let target = op.get("target").and_then(|v| v.as_str()).unwrap_or("").to_string();
        match k {
            "poll" => {
                let q: String = op.get("q").and_then(|v| v.as_str()).unwrap_or("").chars().take(300).collect();
                let opts: Vec<String> = op.get("opts").and_then(|v| v.as_array()).map(|a| a.iter().filter_map(|x| x.as_str()).map(|s| s.chars().take(100).collect()).take(MAX_POLL_OPTIONS).collect()).unwrap_or_default();
                if q.trim().is_empty() || opts.len() < 2 { return; }
                let multi = op.get("multi").and_then(|v| v.as_bool()).unwrap_or(false);
                let m = Message {
                    id: id.to_string(),
                    conversation: conv.to_string(),
                    direction: Direction::In,
                    sender_fp: fp.to_string(),
                    sender_name: self.peer_name(fp),
                    kind: MsgKind::Text,
                    body: q.clone(),
                    ts_ms: if ts_ms > 0 { ts_ms } else { now_ms() },
                    status: MsgStatus::Received,
                    file: None,
                    ext: MsgExt { poll: Some(Poll { question: q, options: opts, multi, votes: vec![], closed: false }), ..Default::default() },
                };
                self.add_message(m, true);
            }
            "vote" => {
                let choices: Vec<u32> = op.get("choices").and_then(|v| v.as_array()).map(|a| a.iter().filter_map(|x| x.as_u64()).map(|x| x as u32).take(MAX_POLL_OPTIONS).collect()).unwrap_or_default();
                let okc = self.history.get(&target).map(|m| m.conversation == conv && m.ext.poll.as_ref().map(|p| !p.closed).unwrap_or(false)).unwrap_or(false);
                if !okc { return; }
                let fp_s = fp.to_string();
                self.update_msg(&target, |m| {
                    if let Some(p) = m.ext.poll.as_mut() {
                        let c = clean_choices(&choices, p.options.len(), p.multi);
                        p.votes.retain(|v| v.fp != fp_s);
                        if !c.is_empty() { p.votes.push(PollVote { fp: fp_s.clone(), choices: c }); }
                    }
                });
                let _ = self.history.flush(false);
            }
            "poll_close" => {
                let okc = self.history.get(&target).map(|m| m.conversation == conv && m.sender_fp == fp && m.direction == Direction::In && m.ext.poll.is_some()).unwrap_or(false);
                if !okc { return; }
                self.update_msg(&target, |m| { if let Some(p) = m.ext.poll.as_mut() { p.closed = true; } });
                let _ = self.history.flush(false);
            }
            "ttl" => {
                let secs = op.get("secs").and_then(|v| v.as_u64()).unwrap_or(0) as u32;
                if !TTL_CHOICES.contains(&secs) { return; }
                if self.conv_prefs.ttl.get(conv).copied().unwrap_or(0) == secs { return; }
                if secs == 0 { self.conv_prefs.ttl.remove(conv); } else { self.conv_prefs.ttl.insert(conv.to_string(), secs); }
                self.conv_prefs.save();
                let name = self.peer_name(fp);
                self.system_msg(conv, if secs == 0 { format!("{name} 关闭了消息定时删除") } else { format!("{name} 将消息定时删除设为 {}", ttl_label(secs)) });
                self.emit_conv(conv);
            }
            _ => {}
        }
    }

    pub(crate) fn rpc_v062(&mut self, method: &str, params: Value) -> Option<RpcResult> {
        Some(match method {
            "send_poll" => (|| {
                #[derive(Deserialize)]
                struct P { conversation: String, question: String, options: Vec<String>, #[serde(default)] multi: bool }
                let a: P = p(params)?;
                if !self.conv_exists(&a.conversation) { return Err(RpcError::invalid("会话不存在")); }
                let q: String = a.question.trim().chars().take(300).collect();
                let opts: Vec<String> = a.options.iter().map(|o| o.trim().chars().take(100).collect::<String>()).filter(|o| !o.is_empty()).collect();
                if q.is_empty() { return Err(RpcError::invalid("投票问题不能为空")); }
                if opts.len() < 2 || opts.len() > MAX_POLL_OPTIONS { return Err(RpcError::invalid("投票需要 2–10 个选项")); }
                let id = Uuid::new_v4();
                let mut m = Message {
                    id: id.to_string(),
                    conversation: a.conversation.clone(),
                    direction: Direction::Out,
                    sender_fp: self.me.clone(),
                    sender_name: self.settings.display_name.clone(),
                    kind: MsgKind::Text,
                    body: q.clone(),
                    ts_ms: now_ms(),
                    status: MsgStatus::Queued,
                    file: None,
                    ext: MsgExt { poll: Some(Poll { question: q.clone(), options: opts.clone(), multi: a.multi, votes: vec![], closed: false }), ..Default::default() },
                };
                let fb = poll_fallback(&q, &opts);
                if self.send_ext(&a.conversation, &json!({"k":"poll","q":q,"opts":opts,"multi":a.multi}), Some(&fb), id, Some(m.id.clone()))? {
                    m.status = MsgStatus::Sent;
                }
                self.remember_id(id);
                self.add_message(m.clone(), false);
                ok(self.history.get(&m.id).cloned().unwrap_or(m))
            })(),
            "vote_poll" => (|| {
                #[derive(Deserialize)]
                struct P { id: String, #[serde(default)] choices: Vec<u32> }
                let a: P = p(params)?;
                let m = self.history.get(&a.id).cloned().ok_or_else(|| RpcError::invalid("消息不存在"))?;
                let poll = m.ext.poll.clone().ok_or_else(|| RpcError::invalid("这不是投票"))?;
                if poll.closed { return Err(RpcError::invalid("投票已结束")); }
                let c = clean_choices(&a.choices, poll.options.len(), poll.multi);
                let me = self.me.clone();
                let c2 = c.clone();
                self.update_msg(&a.id, |m| {
                    if let Some(p) = m.ext.poll.as_mut() {
                        p.votes.retain(|v| v.fp != me);
                        if !c2.is_empty() { p.votes.push(PollVote { fp: me.clone(), choices: c2 }); }
                    }
                });
                let _ = self.history.flush(false);
                self.send_ext(&m.conversation, &json!({"k":"vote","target":a.id,"choices":c}), None, Uuid::new_v4(), None)?;
                ok(self.history.get(&a.id).cloned())
            })(),
            "close_poll" => (|| {
                #[derive(Deserialize)]
                struct P { id: String }
                let a: P = p(params)?;
                let m = self.history.get(&a.id).cloned().ok_or_else(|| RpcError::invalid("消息不存在"))?;
                if m.ext.poll.is_none() || m.direction != Direction::Out { return Err(RpcError::invalid("只能结束自己发起的投票")); }
                self.update_msg(&a.id, |m| { if let Some(p) = m.ext.poll.as_mut() { p.closed = true; } });
                let _ = self.history.flush(false);
                self.send_ext(&m.conversation, &json!({"k":"poll_close","target":a.id}), None, Uuid::new_v4(), None)?;
                ok(self.history.get(&a.id).cloned())
            })(),
            "set_conv_archived" => (|| {
                #[derive(Deserialize)]
                struct P { conversation: String, archived: bool }
                let a: P = p(params)?;
                if !self.conv_exists(&a.conversation) { return Err(RpcError::invalid("会话不存在")); }
                if a.archived { self.conv_prefs.archived.insert(a.conversation.clone()); } else { self.conv_prefs.archived.remove(&a.conversation); }
                self.conv_prefs.save();
                self.emit_conv(&a.conversation);
                ok(json!({"ok": true}))
            })(),
            "set_folder" => (|| {
                // replaces the folder's chat list; empty list deletes it; rename = new name + delete old
                #[derive(Deserialize)]
                struct P { name: String, #[serde(default)] conversations: Vec<String>, #[serde(default)] rename_from: Option<String> }
                let a: P = p(params)?;
                let name: String = a.name.trim().chars().take(32).collect();
                if name.is_empty() { return Err(RpcError::invalid("分组名不能为空")); }
                if !self.conv_prefs.folders.contains_key(&name) && self.conv_prefs.folders.len() >= 20 && a.rename_from.is_none() { return Err(RpcError::invalid("最多 20 个分组")); }
                let mut touched: std::collections::BTreeSet<String> = Default::default();
                if let Some(old) = a.rename_from.filter(|o| *o != name) {
                    if let Some(v) = self.conv_prefs.folders.remove(&old) { touched.extend(v); }
                }
                if let Some(v) = self.conv_prefs.folders.remove(&name) { touched.extend(v); }
                let set: std::collections::BTreeSet<String> = a.conversations.into_iter().filter(|c| self.conv_exists(c)).collect();
                touched.extend(set.iter().cloned());
                if !set.is_empty() { self.conv_prefs.folders.insert(name, set); }
                self.conv_prefs.save();
                for c in touched { self.emit_conv(&c); }
                ok(json!({"ok": true}))
            })(),
            "list_folders" => ok(self.conv_prefs.folders.iter().map(|(k, v)| Folder { name: k.clone(), conversations: v.iter().cloned().collect() }).collect::<Vec<_>>()),
            "set_disappearing" => (|| {
                #[derive(Deserialize)]
                struct P { conversation: String, secs: u32 }
                let a: P = p(params)?;
                if !self.conv_exists(&a.conversation) { return Err(RpcError::invalid("会话不存在")); }
                if !TTL_CHOICES.contains(&a.secs) { return Err(RpcError::invalid("不支持的定时（可选：关闭/30秒/5分钟/1小时/1天/7天）")); }
                if a.secs == 0 { self.conv_prefs.ttl.remove(&a.conversation); } else { self.conv_prefs.ttl.insert(a.conversation.clone(), a.secs); }
                self.conv_prefs.save();
                let fb = if a.secs == 0 { "（我关闭了消息定时删除）".to_string() } else { format!("（我将消息定时删除设为 {}，你的版本暂不支持，请升级）", ttl_label(a.secs)) };
                self.send_ext(&a.conversation, &json!({"k":"ttl","secs":a.secs}), Some(&fb), Uuid::new_v4(), None)?;
                self.system_msg(&a.conversation, if a.secs == 0 { "你关闭了消息定时删除".into() } else { format!("你将消息定时删除设为 {}（之后的新消息到时自动删除）", ttl_label(a.secs)) });
                self.emit_conv(&a.conversation);
                ok(json!({"ok": true}))
            })(),
            _ => return None,
        })
    }
}
