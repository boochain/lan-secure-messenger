//! v0.6.1 UX: QR / link add (fingerprint-checked auto trust), one-click resend, file size + ETA preview,
//! delivery route (LAN / via server) and the relay "stored on server" mark.
use super::*;
use serde::Deserialize;
use serde_json::{json, Value};

fn p<T: for<'de> Deserialize<'de>>(v: Value) -> std::result::Result<T, RpcError> {
    serde_json::from_value(v).map_err(|e| RpcError::invalid(e.to_string()))
}

pub(crate) fn fmt_size(b: u64) -> String {
    match b {
        b if b < 1024 => format!("{b} B"),
        b if b < 1 << 20 => format!("{:.1} KB", b as f64 / 1024.0),
        b if b < 1 << 30 => format!("{:.1} MB", b as f64 / 1048576.0),
        b => format!("{:.2} GB", b as f64 / 1073741824.0),
    }
}
pub(crate) fn fmt_dur(s: u64) -> String {
    let s = s.max(1);
    if s < 60 { format!("{s} 秒") } else if s < 3600 { format!("{} 分 {} 秒", s / 60, s % 60) } else { format!("{} 小时 {} 分", s / 3600, s / 60 % 60) }
}
fn pct_enc(s: &str) -> String {
    s.bytes().map(|b| if b.is_ascii_alphanumeric() || b"-._".contains(&b) || b >= 0x80 { (b as char).to_string() } else { format!("%{b:02X}") })
        .collect::<String>()
        // bytes >= 0x80 were pushed one by one as Latin-1 chars above; rebuild them as UTF-8 below
}
fn enc_name(s: &str) -> String {
    let mut out = String::new();
    for ch in s.chars() {
        if ch.is_ascii_alphanumeric() || "-._".contains(ch) || !ch.is_ascii() { out.push(ch) } else { out.push_str(&pct_enc(&ch.to_string())) }
    }
    out
}
pub(crate) fn uri_param(uri: &str, key: &str) -> String {
    let Some(q) = uri.split_once('?').map(|x| x.1) else { return String::new() };
    for kv in q.split('&') {
        if let Some((k, v)) = kv.split_once('=') {
            if k == key {
                let b = v.as_bytes();
                let mut out = Vec::new();
                let mut i = 0;
                while i < b.len() {
                    if b[i] == b'%' && i + 2 < b.len() + 0 && i + 2 <= b.len() - 1 {
                        if let Ok(x) = u8::from_str_radix(&v[i + 1..i + 3], 16) { out.push(x); i += 3; continue; }
                    }
                    out.push(b[i]);
                    i += 1;
                }
                return String::from_utf8_lossy(&out).into_owned();
            }
        }
    }
    String::new()
}
/// QR matrix as a '0'/'1' string, row-major.
pub(crate) fn qr(text: &str) -> (usize, String) {
    match qrcodegen::QrCode::encode_text(text, qrcodegen::QrCodeEcc::Medium) {
        Ok(q) => {
            let n = q.size() as usize;
            let mut s = String::with_capacity(n * n);
            for y in 0..n as i32 { for x in 0..n as i32 { s.push(if q.get_module(x, y) { '1' } else { '0' }); } }
            (n, s)
        }
        Err(_) => (0, String::new()),
    }
}

impl Engine {
    pub(crate) fn ux_on_add(&mut self, m: &mut Message) {
        if m.direction != Direction::Out || m.ext.route != 0 { return; }
        if let Some(fp) = m.conversation.strip_prefix("dm:") {
            if let Some(c) = self.conns.get(fp) { m.ext.route = if c.via_relay { 2 } else { 1 }; }
        }
    }
    pub(crate) fn ux_tick(&mut self) {
        let now = Instant::now();
        self.ux_pending.retain(|(_, until)| *until > now);
        let hit: Vec<String> = self.ux_pending.iter().filter(|(fp, _)| self.conns.contains_key(fp)).map(|x| x.0.clone()).collect();
        for fp in hit {
            self.ux_pending.retain(|x| x.0 != fp);
            if self.contacts.trust(&fp) != Trust::Blocked { self.set_trust(&fp, Trust::Trusted); }
        }
    }
    pub(crate) fn ux_stored(&mut self, id: String) {
        self.update_msg(&id, |m| m.ext.server_stored = true);
        let _ = self.history.flush(false);
    }
    fn my_uri(&self) -> String {
        let ip = crate::netif::list(&self.settings.include_interfaces, &self.settings.exclude_interfaces)
            .into_iter().find(|i| i.up && i.used && !i.ip.is_loopback()).map(|i| i.ip.to_string()).unwrap_or_default();
        format!("lsm://add?a={}:{}&fp={}&n={}", ip, self.shared.listen_port.load(Ordering::Relaxed), self.me, enc_name(&self.shared.name.lock().unwrap()))
    }

    pub(crate) fn rpc_ux(&mut self, method: &str, params: Value) -> Option<RpcResult> {
        Some(match method {
            "my_uri" => {
                let uri = self.my_uri();
                let (n, m) = qr(&uri);
                Ok(json!({"uri": uri, "qr_size": n, "qr": m}))
            }
            "add_uri" => (|| {
                #[derive(Deserialize)]
                struct P { uri: String }
                let a: P = p(params)?;
                let uri = a.uri.trim();
                if !uri.starts_with("lsm://add?") { return Err(RpcError::invalid("不是有效的添加链接")); }
                let addr = uri_param(uri, "a");
                if addr.is_empty() { return Err(RpcError::invalid("链接缺少地址")); }
                let r = self.rpc_sync("add_peer", json!({"address": addr}))?;
                let fp = uri_param(uri, "fp");
                if !fp.is_empty() && fp != self.me {
                    self.ux_pending.retain(|x| x.0 != fp);
                    self.ux_pending.push((fp.clone(), Instant::now() + Duration::from_secs(600)));
                    self.ux_tick();
                }
                Ok(json!({"ok": true, "address": r.get("address"), "fingerprint": fp, "name": uri_param(uri, "n")}))
            })(),
            "resend" => (|| {
                #[derive(Deserialize)]
                struct P { id: String }
                let a: P = p(params)?;
                let m = self.history.get(&a.id).cloned().ok_or_else(|| RpcError::invalid("消息不存在"))?;
                let can = m.direction == Direction::Out && !m.ext.recalled && m.ext.poll.is_none()
                    && matches!(m.status, MsgStatus::Failed | MsgStatus::Rejected | MsgStatus::Queued);
                if !can { return Err(RpcError::invalid("此消息不能重发")); }
                let r = match (&m.kind, &m.file) {
                    (MsgKind::File, Some(f)) if f.path.is_some() => self.rpc_sync("send_file", json!({"conversation": m.conversation, "path": f.path}))?,
                    (MsgKind::Text, _) => self.rpc_sync("send_text", json!({"conversation": m.conversation, "body": m.body, "reply_to": m.ext.reply_to, "mentions": m.ext.mentions}))?,
                    _ => return Err(RpcError::invalid("此消息不能重发")),
                };
                let _ = self.rpc_sync("delete_message", json!({"id": a.id}));
                Ok(r)
            })(),
            "file_preview" => (|| {
                #[derive(Deserialize)]
                struct P { conversation: String, path: String }
                let a: P = p(params)?;
                let size = std::fs::metadata(&a.path).map(|m| m.len()).map_err(|_| RpcError::invalid("无法读取文件"))?;
                let name = std::path::Path::new(&a.path).file_name().map(|s| s.to_string_lossy().into_owned()).unwrap_or_default();
                let route = a.conversation.strip_prefix("dm:").and_then(|fp| self.conns.get(fp)).map(|c| if c.via_relay { 2 } else { 1 }).unwrap_or(0);
                let bps: u64 = match route { 1 => 8_000_000, 2 => 1_000_000, _ => 0 };
                let eta = if bps > 0 { size / bps + 1 } else { 0 };
                let text = if bps > 0 {
                    format!("{name} · {} · 预计约 {}（{}）", fmt_size(size), fmt_dur(eta), if route == 1 { "局域网直连" } else { "经服务器中转" })
                } else { format!("{name} · {} · 对方上线后发送", fmt_size(size)) };
                Ok(json!({"name": name, "size": size, "eta_secs": eta, "route": route, "text": text}))
            })(),
            _ => return None,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn uri_roundtrip() {
        let u = format!("lsm://add?a=10.0.0.2:41235&fp=ABCD&n={}", enc_name("李明 的&本"));
        assert_eq!(uri_param(&u, "a"), "10.0.0.2:41235");
        assert_eq!(uri_param(&u, "n"), "李明 的&本");
        let (n, m) = qr(&u);
        assert!(n >= 21 && m.len() == n * n);
        assert_eq!(fmt_size(1536), "1.5 KB");
        assert_eq!(fmt_dur(75), "1 分 15 秒");
    }
}
