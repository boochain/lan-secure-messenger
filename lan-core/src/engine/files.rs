//! File / image transfers over the peer's long-lived link.
//!
//! Sender: hash (sha256) → `FileOffer` (queued until the peer is online) → on `FileAccept`
//! stream `FileChunk`s through the low-priority bulk queue with a 4 MiB credit window driven
//! by the receiver's `FileProgress` → `FileEnd`. Receiver writes `<dir>/.<name>.<tid>.part`,
//! verifies sha256, renames to a unique final name and answers `FileComplete`.

use super::*;
use sha2::{Digest, Sha256};
use std::sync::atomic::AtomicBool;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::sync::watch;

const WINDOW: u64 = 4 * 1024 * 1024;
const PROGRESS_EVERY: u64 = 256 * 1024;

/// Partial download kept across a link loss (CAP_RESUME peers).
pub(crate) struct ResumePart {
    pub part: PathBuf,
    pub written: u64,
    pub hasher: Sha256,
}

pub(crate) enum RecvCmd {
    Chunk { offset: u64, data: Vec<u8> },
    End,
}

pub(crate) enum TransferEvt {
    Hashed { tid: Uuid, result: std::result::Result<[u8; 32], String> },
    SendDone { tid: Uuid, result: std::result::Result<(), String> },
    RecvProgress { tid: Uuid, written: u64 },
    RecvDone { tid: Uuid, result: std::result::Result<PathBuf, String> },
    /// recv channel dropped mid-transfer; the partial file is kept.
    RecvInterrupted { tid: Uuid, part: ResumePart },
    /// Sender actually (re)started at this offset.
    SendStart { tid: Uuid, offset: u64 },
}

pub(crate) struct TransferRt {
    pub id: Uuid,
    pub msg_id: String,
    pub conversation: String,
    pub peer_fp: String,
    pub peer_name: String,
    pub direction: Direction,
    pub name: String,
    pub size: u64,
    pub mime: String,
    pub path: Option<PathBuf>,
    pub sha256: Option<[u8; 32]>,
    pub group_id: Option<Uuid>,
    pub transferred: u64,
    pub state: TransferState,
    pub error: String,
    pub started_ms: i64,
    pub cancel: Arc<AtomicBool>,
    pub credit: Option<watch::Sender<u64>>,
    pub chunk_tx: Option<mpsc::UnboundedSender<RecvCmd>>,
    pub offer_ts: i64,
    /// Peer supports CAP_RESUME: on link loss wait for reconnect and resume instead of failing.
    pub resumable: bool,
    /// Receiver: transfer was accepted once (a re-offer is auto-accepted / resumed).
    pub accepted: bool,
    pub resume_part: Option<ResumePart>,
    pub resumed_from: u64,
    rate_mark: (Instant, u64),
    bps: u64,
    last_emit: Instant,
}

impl TransferRt {
    pub fn view(&self) -> Transfer {
        Transfer {
            id: self.id.to_string(),
            message_id: self.msg_id.clone(),
            conversation: self.conversation.clone(),
            peer_fp: self.peer_fp.clone(),
            peer_name: self.peer_name.clone(),
            direction: self.direction,
            name: self.name.clone(),
            size: self.size,
            transferred: self.transferred,
            state: self.state,
            mime: self.mime.clone(),
            path: self.path.as_ref().map(|p| p.display().to_string()),
            error: self.error.clone(),
            bytes_per_sec: if self.state == TransferState::Active { self.bps } else { 0 },
            started_ms: self.started_ms,
            resumed_from: self.resumed_from,
            retryable: self.direction == Direction::Out && matches!(self.state, TransferState::Failed | TransferState::Cancelled | TransferState::Rejected),
        }
    }

    fn set_progress(&mut self, n: u64) {
        self.transferred = n.min(self.size);
        let (t0, b0) = self.rate_mark;
        let dt = t0.elapsed().as_secs_f64();
        if dt >= 0.5 {
            let inst = ((self.transferred.saturating_sub(b0)) as f64 / dt) as u64;
            self.bps = if self.bps == 0 { inst } else { (self.bps * 2 + inst) / 3 };
            self.rate_mark = (Instant::now(), self.transferred);
        }
    }

    fn finished(&self) -> bool {
        matches!(self.state, TransferState::Done | TransferState::Failed | TransferState::Cancelled | TransferState::Rejected)
    }
}

pub fn mime_guess(name: &str) -> String {
    let ext = name.rsplit('.').next().unwrap_or("").to_lowercase();
    match ext.as_str() {
        "png" => "image/png",
        "jpg" | "jpeg" => "image/jpeg",
        "gif" => "image/gif",
        "webp" => "image/webp",
        "bmp" => "image/bmp",
        "txt" | "log" | "md" => "text/plain",
        "pdf" => "application/pdf",
        "zip" => "application/zip",
        "mp4" => "video/mp4",
        "mp3" => "audio/mpeg",
        "doc" | "docx" => "application/msword",
        "xls" | "xlsx" => "application/vnd.ms-excel",
        "ppt" | "pptx" => "application/vnd.ms-powerpoint",
        _ => "application/octet-stream",
    }
    .to_string()
}

pub fn sanitize_filename(name: &str) -> String {
    let base = name.rsplit(['/', '\\']).next().unwrap_or("");
    let mut s: String = base
        .chars()
        .map(|c| if c.is_control() || "<>:\"/\\|?*".contains(c) { '_' } else { c })
        .collect();
    s = s.trim().trim_matches('.').trim().to_string();
    if s.chars().count() > 180 {
        let ext: String = s.rsplit_once('.').map(|(_, e)| e.chars().take(10).collect()).unwrap_or_default();
        let stem: String = s.chars().take(160).collect();
        s = if ext.is_empty() { stem } else { format!("{stem}.{ext}") };
    }
    let stem_upper = s.split('.').next().unwrap_or("").to_uppercase();
    let reserved = ["CON", "PRN", "AUX", "NUL"].contains(&stem_upper.as_str())
        || ((stem_upper.starts_with("COM") || stem_upper.starts_with("LPT")) && stem_upper.len() == 4 && stem_upper.as_bytes()[3].is_ascii_digit());
    if reserved {
        s = format!("_{s}");
    }
    if s.is_empty() {
        "file".into()
    } else {
        s
    }
}

fn unique_path(dir: &std::path::Path, name: &str) -> PathBuf {
    let p = dir.join(name);
    if !p.exists() {
        return p;
    }
    let (stem, ext) = match name.rsplit_once('.') {
        Some((s, e)) if !s.is_empty() => (s.to_string(), format!(".{e}")),
        _ => (name.to_string(), String::new()),
    };
    for i in 1..10_000 {
        let c = dir.join(format!("{stem} ({i}){ext}"));
        if !c.exists() {
            return c;
        }
    }
    dir.join(format!("{stem}-{}{ext}", Uuid::new_v4()))
}

async fn send_task(
    tid: Uuid,
    path: PathBuf,
    size: u64,
    bulk: mpsc::Sender<Frame>,
    mut credit: watch::Receiver<u64>,
    cancel: Arc<AtomicBool>,
    resume: Option<(u64, [u8; 32])>,
    tx: mpsc::UnboundedSender<EngineMsg>,
) -> std::result::Result<(), String> {
    let mut f = tokio::fs::File::open(&path).await.map_err(|e| format!("打开文件失败: {e}"))?;
    let mut off: u64 = 0;
    let mut buf = vec![0u8; crate::wire::CHUNK];
    if let Some((want, prefix)) = resume.filter(|(o, _)| *o > 0 && *o <= size) {
        // Continue only if our file has exactly the receiver's prefix.
        let mut h = Sha256::new();
        let mut left = want;
        while left > 0 {
            let n = f.read(&mut buf[..(left as usize).min(crate::wire::CHUNK)]).await.map_err(|e| format!("读取文件失败: {e}"))?;
            if n == 0 {
                break;
            }
            h.update(&buf[..n]);
            left -= n as u64;
        }
        let got: [u8; 32] = h.finalize().into();
        if left == 0 && got == prefix {
            off = want;
        } else {
            use tokio::io::AsyncSeekExt;
            f.seek(std::io::SeekFrom::Start(0)).await.map_err(|e| format!("读取文件失败: {e}"))?;
        }
    }
    let _ = tx.send(EngineMsg::Transfer(TransferEvt::SendStart { tid, offset: off }));
    while off < size {
        if cancel.load(Ordering::Relaxed) {
            return Err("已取消".into());
        }
        while off.saturating_sub(*credit.borrow()) > WINDOW {
            match tokio::time::timeout(Duration::from_secs(30), credit.changed()).await {
                Ok(Ok(())) => {}
                Ok(Err(_)) => return Err("已取消".into()),
                Err(_) => return Err("对方长时间无响应".into()),
            }
            if cancel.load(Ordering::Relaxed) {
                return Err("已取消".into());
            }
        }
        let want = ((size - off) as usize).min(buf.len());
        let n = f.read(&mut buf[..want]).await.map_err(|e| format!("读取文件失败: {e}"))?;
        if n == 0 {
            return Err("文件在发送过程中被修改或截断".into());
        }
        bulk.send(Frame::FileChunk { tid, offset: off, data: buf[..n].to_vec() }).await.map_err(|_| "连接断开".to_string())?;
        off += n as u64;
    }
    bulk.send(Frame::FileEnd { tid }).await.map_err(|_| "连接断开".to_string())?;
    Ok(())
}

#[allow(clippy::too_many_arguments)]
async fn recv_task(
    tid: Uuid,
    part: PathBuf,
    dir: PathBuf,
    name: String,
    size: u64,
    sha: [u8; 32],
    mut rx: mpsc::UnboundedReceiver<RecvCmd>,
    ctrl: mpsc::UnboundedSender<Frame>,
    tx: mpsc::UnboundedSender<EngineMsg>,
    resume: Option<(u64, Sha256)>,
) {
    let fail = |e: String, part: &PathBuf| {
        let _ = std::fs::remove_file(part);
        let _ = ctrl.send(Frame::FileComplete { tid, ok: false, error: e.clone() });
        let _ = tx.send(EngineMsg::Transfer(TransferEvt::RecvDone { tid, result: Err(e) }));
    };
    let (mut written, mut h) = resume.unwrap_or((0, Sha256::new()));
    let opened = if written > 0 {
        match tokio::fs::OpenOptions::new().write(true).open(&part).await {
            Ok(f) => f.set_len(written).await.map(|_| f),
            Err(e) => Err(e),
        }
    } else {
        tokio::fs::File::create(&part).await
    };
    let mut f = match opened {
        Ok(mut f) => {
            use tokio::io::AsyncSeekExt;
            let _ = f.seek(std::io::SeekFrom::Start(written)).await;
            f
        }
        Err(e) => return fail(format!("无法创建文件 {}: {e}", part.display()), &part),
    };
    let mut last: u64 = written;
    while let Some(cmd) = rx.recv().await {
        match cmd {
            RecvCmd::Chunk { offset, data } => {
                if offset == 0 && written > 0 {
                    // Sender could not continue (prefix differs): restart from 0.
                    use tokio::io::AsyncSeekExt;
                    if f.set_len(0).await.is_err() || f.seek(std::io::SeekFrom::Start(0)).await.is_err() {
                        return fail("无法重置部分文件".into(), &part);
                    }
                    written = 0;
                    last = 0;
                    h = Sha256::new();
                }
                if offset != written || written + data.len() as u64 > size {
                    return fail("数据块顺序错误".into(), &part);
                }
                if let Err(e) = f.write_all(&data).await {
                    return fail(format!("写入失败 (磁盘已满?): {e}"), &part);
                }
                h.update(&data);
                written += data.len() as u64;
                if written - last >= PROGRESS_EVERY || written == size {
                    last = written;
                    let _ = ctrl.send(Frame::FileProgress { tid, received: written });
                    let _ = tx.send(EngineMsg::Transfer(TransferEvt::RecvProgress { tid, written }));
                }
            }
            RecvCmd::End => {
                let _ = f.flush().await;
                let _ = f.sync_all().await;
                drop(f);
                if written != size {
                    return fail(format!("文件不完整 ({written}/{size})"), &part);
                }
                let got: [u8; 32] = h.finalize().into();
                if got != sha {
                    return fail("SHA-256 校验失败".into(), &part);
                }
                let fin = unique_path(&dir, &name);
                if let Err(e) = std::fs::rename(&part, &fin) {
                    return fail(format!("重命名失败: {e}"), &part);
                }
                let _ = ctrl.send(Frame::FileComplete { tid, ok: true, error: String::new() });
                let _ = tx.send(EngineMsg::Transfer(TransferEvt::RecvDone { tid, result: Ok(fin) }));
                return;
            }
        }
    }
    // Channel dropped: cancelled or link lost. Keep the partial file; the engine decides
    // (resume on reconnect, or delete).
    let _ = f.flush().await;
    let _ = f.sync_all().await;
    drop(f);
    let _ = tx.send(EngineMsg::Transfer(TransferEvt::RecvInterrupted { tid, part: ResumePart { part, written, hasher: h } }));
}

impl Engine {
    fn emit_transfer(&mut self, tid: Uuid, force: bool) {
        if let Some(t) = self.transfers.get_mut(&tid) {
            if force || t.last_emit.elapsed() >= Duration::from_millis(200) || t.finished() {
                t.last_emit = Instant::now();
                let v = t.view();
                self.emit(Event::TransferUpdated { transfer: v });
            }
        }
    }

    pub(crate) fn send_file(&mut self, conv: &str, path: PathBuf, reuse_msg: Option<(String, String)>) -> std::result::Result<Transfer, RpcError> {
        let meta = std::fs::metadata(&path).map_err(|e| RpcError::invalid(format!("无法读取文件 {}: {e}", path.display())))?;
        if !meta.is_file() {
            return Err(RpcError::invalid("只能发送文件（不支持文件夹）"));
        }
        let size = meta.len();
        if size > self.settings.max_file_bytes {
            return Err(RpcError::invalid(format!("文件过大 (上限 {} MB)", self.settings.max_file_bytes / 1024 / 1024)));
        }
        let name = sanitize_filename(&path.file_name().map(|n| n.to_string_lossy().to_string()).unwrap_or_default());
        let mime = mime_guess(&name);
        let (recipients, group_id): (Vec<String>, Option<Uuid>) = if let Some((_, only)) = &reuse_msg {
            (vec![only.clone()], conv.strip_prefix("group:").and_then(|g| Uuid::parse_str(g).ok()))
        } else if let Some(fp) = conv.strip_prefix("dm:") {
            if self.contacts.trust(fp) == Trust::Blocked {
                return Err(RpcError::invalid("该联系人已被屏蔽"));
            }
            (vec![fp.to_string()], None)
        } else if let Some(gid) = conv.strip_prefix("group:").and_then(|g| Uuid::parse_str(g).ok()) {
            let g = self.groups.get(&gid).ok_or_else(|| RpcError::invalid("群组不存在"))?;
            (g.members.iter().filter(|m| **m != self.me).cloned().collect(), Some(gid))
        } else {
            return Err(RpcError::invalid("conversation 必须是 dm:<指纹> 或 group:<id>"));
        };
        if recipients.is_empty() {
            return Err(RpcError::invalid("群组中没有其他成员"));
        }
        let msg_id = reuse_msg.as_ref().map(|(m, _)| m.clone()).unwrap_or_else(|| Uuid::new_v4().to_string());
        let ts = now_ms();
        let mut first: Option<Transfer> = None;
        let mut first_tid = String::new();
        for fp in recipients {
            let tid = Uuid::new_v4();
            let t = TransferRt {
                id: tid,
                msg_id: msg_id.clone(),
                conversation: conv.to_string(),
                peer_name: self.peer_name(&fp),
                peer_fp: fp,
                direction: Direction::Out,
                name: name.clone(),
                size,
                mime: mime.clone(),
                path: Some(path.clone()),
                sha256: None,
                group_id,
                transferred: 0,
                state: TransferState::Offered,
                error: String::new(),
                started_ms: ts,
                cancel: Arc::new(AtomicBool::new(false)),
                credit: None,
                chunk_tx: None,
                offer_ts: ts,
                resumable: false,
                accepted: false,
                resume_part: None,
                resumed_from: 0,
                rate_mark: (Instant::now(), 0),
                bps: 0,
                last_emit: Instant::now(),
            };
            if first.is_none() {
                first = Some(t.view());
                first_tid = tid.to_string();
            }
            self.transfers.insert(tid, t);
            self.emit_transfer(tid, true);
            let p2 = path.clone();
            let tx = self.tx.clone();
            tokio::task::spawn_blocking(move || {
                let r = (|| -> std::io::Result<[u8; 32]> {
                    let mut f = std::fs::File::open(&p2)?;
                    let mut h = Sha256::new();
                    std::io::copy(&mut f, &mut h)?;
                    Ok(h.finalize().into())
                })()
                .map_err(|e| format!("读取文件失败: {e}"));
                let _ = tx.send(EngineMsg::Transfer(TransferEvt::Hashed { tid, result: r }));
            });
        }
        let file = FileRef { transfer_id: first_tid, name: name.clone(), size, mime: mime.clone(), path: Some(path.display().to_string()), is_image: mime.starts_with("image/") };
        if reuse_msg.is_some() {
            let f2 = file.clone();
            self.update_msg(&msg_id, move |m| {
                m.status = MsgStatus::Queued;
                m.file = Some(f2);
            });
        } else {
            let m = Message {
                id: msg_id.clone(),
                conversation: conv.to_string(),
                direction: Direction::Out,
                sender_fp: self.me.clone(),
                sender_name: self.settings.display_name.clone(),
                kind: MsgKind::File,
                body: String::new(),
                ts_ms: ts,
                status: MsgStatus::Queued,
                file: Some(file),
            };
            self.add_message(m, false);
        }
        Ok(first.unwrap())
    }

    fn send_offer(&mut self, tid: Uuid) {
        let Some(t) = self.transfers.get(&tid) else { return };
        let (Some(sha), TransferState::Offered, Direction::Out) = (t.sha256, t.state, t.direction) else { return };
        let Ok(msg_uuid) = Uuid::parse_str(&t.msg_id) else { return };
        let f = Frame::FileOffer { tid, msg_id: msg_uuid, name: t.name.clone(), size: t.size, mime: t.mime.clone(), sha256: sha, group_id: t.group_id, ts_ms: t.offer_ts };
        let fp = t.peer_fp.clone();
        let msg = t.msg_id.clone();
        if self.send_ctrl(&fp, f) {
            let queued = self.history.get(&msg).map(|m| m.status == MsgStatus::Queued).unwrap_or(false);
            if queued {
                self.update_msg(&msg, |m| m.status = MsgStatus::Sent);
            }
        }
    }

    pub(crate) fn resend_offers(&mut self, fp: &str) {
        let tids: Vec<Uuid> = self.transfers.values().filter(|t| t.peer_fp == fp && t.direction == Direction::Out && t.state == TransferState::Offered).map(|t| t.id).collect();
        for t in tids {
            self.send_offer(t);
        }
    }

    pub(crate) fn fail_transfers_for(&mut self, fp: &str, why: &str) {
        // CAP_RESUME peers: interrupted transfers wait for the next link (LAN or relay) and resume.
        let waiting: Vec<Uuid> = self
            .transfers
            .values()
            .filter(|t| t.peer_fp == fp && t.resumable && (t.state == TransferState::Active || (t.direction == Direction::Out && t.state == TransferState::Verifying)))
            .map(|t| t.id)
            .collect();
        for tid in &waiting {
            if let Some(t) = self.transfers.get_mut(tid) {
                t.state = TransferState::Offered;
                t.error = "连接中断，重连后自动续传".into();
                t.credit = None;
                t.chunk_tx = None; // receiver: recv_task reports RecvInterrupted with the partial data
                if t.direction == Direction::Out {
                    t.cancel.store(true, Ordering::Relaxed);
                    t.cancel = Arc::new(AtomicBool::new(false));
                }
            }
            self.emit_transfer(*tid, true);
        }
        let tids: Vec<Uuid> = self
            .transfers
            .values()
            .filter(|t| t.peer_fp == fp && !waiting.contains(&t.id) && !(t.resumable && t.direction == Direction::In && t.state == TransferState::Offered) && (matches!(t.state, TransferState::Active | TransferState::Verifying) || (t.direction == Direction::In && t.state == TransferState::Offered)))
            .map(|t| t.id)
            .collect();
        for tid in tids {
            if let Some(t) = self.transfers.get_mut(&tid) {
                t.state = TransferState::Failed;
                t.error = why.to_string();
                t.cancel.store(true, Ordering::Relaxed);
                t.credit = None;
                t.chunk_tx = None;
            }
            self.emit_transfer(tid, true);
            self.mark_file_msg_failed(tid);
        }
    }

    fn mark_file_msg_failed(&mut self, tid: Uuid) {
        let Some(t) = self.transfers.get(&tid) else { return };
        if t.direction == Direction::Out && t.group_id.is_none() {
            let id = t.msg_id.clone();
            self.update_msg(&id, |m| m.status = MsgStatus::Failed);
        }
    }

    pub(crate) fn cancel_transfer(&mut self, tid: Uuid, why: &str) -> std::result::Result<(), RpcError> {
        let t = self.transfers.get_mut(&tid).ok_or_else(|| RpcError::invalid("传输不存在"))?;
        if t.finished() {
            return Ok(());
        }
        t.state = TransferState::Cancelled;
        t.error = why.to_string();
        t.cancel.store(true, Ordering::Relaxed);
        t.credit = None;
        t.chunk_tx = None;
        let fp = t.peer_fp.clone();
        self.send_ctrl(&fp, Frame::FileCancel { tid, reason: why.to_string() });
        self.emit_transfer(tid, true);
        self.mark_file_msg_failed(tid);
        Ok(())
    }

    pub(crate) fn retry_transfer(&mut self, tid: Uuid) -> std::result::Result<Transfer, RpcError> {
        let t = self.transfers.get(&tid).ok_or_else(|| RpcError::invalid("传输不存在"))?;
        if t.direction != Direction::Out || !t.view().retryable {
            return Err(RpcError::invalid("只能重试发送失败/取消/被拒绝的传输"));
        }
        let (conv, path, msg, fp) = (t.conversation.clone(), t.path.clone().unwrap_or_default(), t.msg_id.clone(), t.peer_fp.clone());
        self.send_file(&conv, path, Some((msg, fp)))
    }

    pub(crate) fn accept_transfer(&mut self, tid: Uuid) -> std::result::Result<(), RpcError> {
        let dir = self.settings.download_dir(&self.paths);
        std::fs::create_dir_all(&dir).map_err(|e| RpcError::new(-32000, format!("无法创建下载目录 {}: {e}", dir.display())))?;
        let t = self.transfers.get_mut(&tid).ok_or_else(|| RpcError::invalid("传输不存在"))?;
        if t.direction != Direction::In || t.state != TransferState::Offered {
            return Err(RpcError::invalid("该传输不在等待接收状态"));
        }
        let Some(conn) = self.conns.get(&t.peer_fp) else { return Err(RpcError::new(-32004, "对方不在线")) };
        let can_resume = conn.caps & crate::wire::CAP_RESUME != 0;
        t.resumable = can_resume;
        t.accepted = true;
        t.error.clear();
        let rp = t.resume_part.take();
        let (part, resume) = match rp {
            Some(r) if can_resume && r.written > 0 && r.written < t.size => {
                let prefix: [u8; 32] = r.hasher.clone().finalize().into();
                let _ = conn.ctrl.send(Frame::FileResume { tid, offset: r.written, prefix_sha256: prefix });
                t.resumed_from = r.written;
                (r.part, Some((r.written, r.hasher)))
            }
            other => {
                if let Some(r) = other {
                    let _ = std::fs::remove_file(&r.part);
                }
                let _ = conn.ctrl.send(Frame::FileAccept { tid });
                t.resumed_from = 0;
                (dir.join(format!(".{}.{}.part", t.name, &tid.to_string()[..8])), None)
            }
        };
        let (ctx, crx) = mpsc::unbounded_channel();
        t.chunk_tx = Some(ctx);
        t.state = TransferState::Active;
        t.rate_mark = (Instant::now(), t.resumed_from);
        t.transferred = t.resumed_from;
        tokio::spawn(recv_task(tid, part, dir, t.name.clone(), t.size, t.sha256.unwrap_or_default(), crx, conn.ctrl.clone(), self.tx.clone(), resume));
        self.emit_transfer(tid, true);
        Ok(())
    }

    pub(crate) fn reject_transfer(&mut self, tid: Uuid, why: &str) -> std::result::Result<(), RpcError> {
        let t = self.transfers.get_mut(&tid).ok_or_else(|| RpcError::invalid("传输不存在"))?;
        if t.direction != Direction::In || t.state != TransferState::Offered {
            return Err(RpcError::invalid("该传输不在等待接收状态"));
        }
        t.state = TransferState::Rejected;
        t.error = why.into();
        let fp = t.peer_fp.clone();
        self.send_ctrl(&fp, Frame::FileReject { tid, reason: why.into() });
        self.emit_transfer(tid, true);
        Ok(())
    }

    pub(crate) fn on_file_frame(&mut self, fp: &str, f: Frame) {
        let own = |s: &Self, tid: &Uuid, dir: Direction| s.transfers.get(tid).map(|t| t.peer_fp == fp && t.direction == dir).unwrap_or(false);
        match f {
            Frame::FileOffer { tid, msg_id, name, size, mime, sha256, group_id, ts_ms } => {
                if let Some(t) = self.transfers.get(&tid) {
                    // Re-offer after reconnect of a transfer we already know.
                    if t.peer_fp != fp || t.direction != Direction::In {
                        return;
                    }
                    match t.state {
                        TransferState::Done => self.send_ctrl(fp, Frame::FileComplete { tid, ok: true, error: String::new() }),
                        TransferState::Offered if t.accepted => {
                            if let Err(e) = self.accept_transfer(tid) {
                                tracing::debug!(%tid, error = %e.message, "re-accept failed");
                            }
                            true
                        }
                        _ => true,
                    };
                    return;
                }
                let name = sanitize_filename(&name);
                let conv = match group_id {
                    Some(g) => {
                        if !self.groups.get(&g).map(|x| x.is_member(fp)).unwrap_or(false) {
                            self.send_ctrl(fp, Frame::FileReject { tid, reason: "不是群成员".into() });
                            return;
                        }
                        group_conv(&g)
                    }
                    None => dm_conv(fp),
                };
                if self.history.is_deleted(&msg_id.to_string()) {
                    // deleted on this device: a re-offer must not resurrect it
                    self.send_ctrl(fp, Frame::FileReject { tid, reason: "对方已删除这条消息".into() });
                    return;
                }
                let trust = self.contacts.trust(fp);
                let t = TransferRt {
                    id: tid,
                    msg_id: msg_id.to_string(),
                    conversation: conv.clone(),
                    peer_fp: fp.to_string(),
                    peer_name: self.peer_name(fp),
                    direction: Direction::In,
                    name: name.clone(),
                    size,
                    mime: mime.clone(),
                    path: None,
                    sha256: Some(sha256),
                    group_id,
                    transferred: 0,
                    state: TransferState::Offered,
                    error: String::new(),
                    started_ms: now_ms(),
                    cancel: Arc::new(AtomicBool::new(false)),
                    credit: None,
                    chunk_tx: None,
                    offer_ts: ts_ms,
                    resumable: false,
                    accepted: false,
                    resume_part: None,
                    resumed_from: 0,
                    rate_mark: (Instant::now(), 0),
                    bps: 0,
                    last_emit: Instant::now(),
                };
                self.transfers.insert(tid, t);
                let file = FileRef { transfer_id: tid.to_string(), name: name.clone(), size, mime: mime.clone(), path: None, is_image: mime.starts_with("image/") };
                if self.history.contains(&msg_id.to_string()) {
                    let f2 = file.clone();
                    self.update_msg(&msg_id.to_string(), move |m| m.file = Some(f2));
                } else {
                    let m = Message {
                        id: msg_id.to_string(),
                        conversation: conv,
                        direction: Direction::In,
                        sender_fp: fp.to_string(),
                        sender_name: self.peer_name(fp),
                        kind: MsgKind::File,
                        body: String::new(),
                        ts_ms: if ts_ms > 0 { ts_ms } else { now_ms() },
                        status: MsgStatus::Received,
                        file: Some(file),
                    };
                    self.remember_id(msg_id);
                    self.add_message(m, true);
                }
                if trust != Trust::Trusted {
                    let _ = self.reject_transfer(tid, "对方尚未信任你，文件被拒收（请先互相信任）");
                    self.notice("warn", format!("已拒收来自未信任联系人 {} 的文件 {name}", self.peer_name(fp)));
                } else if size > self.settings.max_file_bytes {
                    let _ = self.reject_transfer(tid, "文件超过对方设置的大小上限");
                } else if self.settings.auto_accept_files {
                    if let Err(e) = self.accept_transfer(tid) {
                        let _ = self.reject_transfer(tid, &e.message);
                    }
                } else {
                    self.emit_transfer(tid, true);
                }
            }
            Frame::FileAccept { tid } | Frame::FileResume { tid, .. } => {
                if !own(self, &tid, Direction::Out) {
                    return;
                }
                let resume = match f {
                    Frame::FileResume { offset, prefix_sha256, .. } => Some((offset, prefix_sha256)),
                    _ => None,
                };
                let Some(conn) = self.conns.get(fp) else { return };
                let bulk = conn.bulk.clone();
                let can_resume = conn.caps & crate::wire::CAP_RESUME != 0;
                let t = self.transfers.get_mut(&tid).unwrap();
                if t.state != TransferState::Offered {
                    return;
                }
                t.state = TransferState::Active;
                t.resumable = can_resume;
                t.error.clear();
                t.rate_mark = (Instant::now(), 0);
                let (ctx, crx) = watch::channel(resume.map(|r| r.0).unwrap_or(0));
                t.credit = Some(ctx);
                let (path, size, cancel) = (t.path.clone().unwrap_or_default(), t.size, t.cancel.clone());
                let tx = self.tx.clone();
                let tx2 = self.tx.clone();
                tokio::spawn(async move {
                    let r = send_task(tid, path, size, bulk, crx, cancel, resume, tx2).await;
                    let _ = tx.send(EngineMsg::Transfer(TransferEvt::SendDone { tid, result: r }));
                });
                self.emit_transfer(tid, true);
            }
            Frame::FileReject { tid, reason } => {
                if !own(self, &tid, Direction::Out) {
                    return;
                }
                let t = self.transfers.get_mut(&tid).unwrap();
                t.state = TransferState::Rejected;
                t.error = reason;
                self.emit_transfer(tid, true);
                self.mark_file_msg_failed(tid);
            }
            Frame::FileProgress { tid, received } => {
                if !own(self, &tid, Direction::Out) {
                    return;
                }
                let t = self.transfers.get_mut(&tid).unwrap();
                t.set_progress(received);
                if let Some(c) = &t.credit {
                    let _ = c.send(received);
                }
                self.emit_transfer(tid, false);
            }
            Frame::FileChunk { tid, offset, data } => {
                if let Some(t) = self.transfers.get(&tid).filter(|t| t.peer_fp == fp && t.direction == Direction::In) {
                    if let Some(c) = &t.chunk_tx {
                        let _ = c.send(RecvCmd::Chunk { offset, data });
                    }
                }
            }
            Frame::FileEnd { tid } => {
                if let Some(t) = self.transfers.get_mut(&tid).filter(|t| t.peer_fp == fp && t.direction == Direction::In) {
                    if let Some(c) = &t.chunk_tx {
                        let _ = c.send(RecvCmd::End);
                    }
                    t.state = TransferState::Verifying;
                    self.emit_transfer(tid, true);
                }
            }
            Frame::FileComplete { tid, ok, error } => {
                if !own(self, &tid, Direction::Out) {
                    return;
                }
                let t = self.transfers.get_mut(&tid).unwrap();
                if ok {
                    t.state = TransferState::Done;
                    t.transferred = t.size;
                } else {
                    t.state = TransferState::Failed;
                    t.error = format!("对方校验失败: {error}");
                }
                t.credit = None;
                let (msg, group) = (t.msg_id.clone(), t.group_id);
                // persist first, then announce (parity with the C core): the message status reaches history
                // (forced, synced write) before the transfer's terminal event goes out
                if ok {
                    let all_done = self.transfers.values().filter(|x| x.msg_id == msg && x.direction == Direction::Out).all(|x| x.state == TransferState::Done);
                    if all_done || group.is_none() {
                        self.update_msg(&msg, |m| m.status = MsgStatus::Delivered);
                    }
                } else {
                    self.mark_file_msg_failed(tid);
                }
                let _ = self.history.flush(true);
                self.emit_transfer(tid, true);
            }
            Frame::FileCancel { tid, reason } => {
                if let Some(t) = self.transfers.get_mut(&tid).filter(|t| t.peer_fp == fp) {
                    if t.finished() {
                        return;
                    }
                    t.state = TransferState::Cancelled;
                    t.error = format!("对方取消: {reason}");
                    t.cancel.store(true, Ordering::Relaxed);
                    t.credit = None;
                    t.chunk_tx = None;
                    self.emit_transfer(tid, true);
                    self.mark_file_msg_failed(tid);
                }
            }
            _ => {}
        }
    }

    pub(crate) fn on_transfer_evt(&mut self, ev: TransferEvt) {
        match ev {
            TransferEvt::Hashed { tid, result } => {
                let Some(t) = self.transfers.get_mut(&tid) else { return };
                match result {
                    Ok(h) => {
                        t.sha256 = Some(h);
                        self.send_offer(tid);
                    }
                    Err(e) => {
                        t.state = TransferState::Failed;
                        t.error = e;
                        self.emit_transfer(tid, true);
                        self.mark_file_msg_failed(tid);
                    }
                }
            }
            TransferEvt::SendDone { tid, result } => {
                let Some(t) = self.transfers.get_mut(&tid) else { return };
                if t.finished() || t.state == TransferState::Offered {
                    return; // finished, or interrupted and waiting to resume
                }
                match result {
                    Ok(()) => t.state = TransferState::Verifying,
                    Err(e) if t.resumable && e == "连接断开" => {
                        t.state = TransferState::Offered;
                        t.error = "连接中断，重连后自动续传".into();
                        t.credit = None;
                    }
                    Err(e) => {
                        t.state = TransferState::Failed;
                        t.error = e.clone();
                        let fp = t.peer_fp.clone();
                        self.send_ctrl(&fp, Frame::FileCancel { tid, reason: e });
                        self.mark_file_msg_failed(tid);
                    }
                }
                self.emit_transfer(tid, true);
            }
            TransferEvt::SendStart { tid, offset } => {
                if let Some(t) = self.transfers.get_mut(&tid) {
                    t.resumed_from = offset;
                    t.transferred = offset;
                    t.rate_mark = (Instant::now(), offset);
                    if offset > 0 {
                        tracing::info!(%tid, offset, "file transfer resumed");
                    }
                    self.emit_transfer(tid, true);
                }
            }
            TransferEvt::RecvInterrupted { tid, part } => {
                let keep = self.transfers.get(&tid).map(|t| t.state == TransferState::Offered && t.resumable && t.direction == Direction::In).unwrap_or(false);
                if keep {
                    tracing::info!(%tid, written = part.written, "partial download kept for resume");
                    if let Some(t) = self.transfers.get_mut(&tid) {
                        t.resume_part = Some(part);
                    }
                } else {
                    let _ = std::fs::remove_file(&part.part);
                }
            }
            TransferEvt::RecvProgress { tid, written } => {
                if let Some(t) = self.transfers.get_mut(&tid) {
                    t.set_progress(written);
                    self.emit_transfer(tid, false);
                }
            }
            TransferEvt::RecvDone { tid, result } => {
                let Some(t) = self.transfers.get_mut(&tid) else { return };
                t.chunk_tx = None;
                match result {
                    Ok(p) => {
                        t.state = TransferState::Done;
                        t.transferred = t.size;
                        t.path = Some(p.clone());
                        let (msg, name, conv) = (t.msg_id.clone(), t.name.clone(), t.conversation.clone());
                        let ps = p.display().to_string();
                        self.update_msg(&msg, move |m| {
                            if let Some(f) = m.file.as_mut() {
                                f.path = Some(ps);
                            }
                        });
                        tracing::info!(%tid, path = %p.display(), "file received");
                        if !self.is_muted(&conv) {
                            self.emit(Event::Notify { conversation: conv.clone(), title: "文件已接收".into(), body: name });
                        }
                    }
                    Err(e) => {
                        if !t.finished() {
                            t.state = TransferState::Failed;
                            t.error = e;
                        }
                    }
                }
                // the received file's path is in history (forced write) before 'done' is reported
                let _ = self.history.flush(true);
                self.emit_transfer(tid, true);
            }
        }
    }
}
