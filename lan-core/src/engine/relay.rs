//! Optional relay-server client (LSM relay protocol v1, see docs/relay-protocol.md).
//!
//! The relay is a *fallback* transport: LAN direct links are always preferred by the
//! engine. Every relay session carries exactly the bytes a TCP link would carry
//! (Noise XX handshake + frames) through an in-memory duplex, so all features
//! (messages, receipts, files with resume, groups, PIN pairing) work unchanged.
//!
//! Stream layer inside every relay blob (client convention, opaque to the server):
//!   u8 kind (0 DATA, 1 OPEN, 2 CLOSE) | u32 BE session id | bytes
//! The peer with the numerically smaller relay id opens the session.
//! Client info announced to the server: "LSM5\t<fingerprint>\t<name>\t<version>".

use super::{EngineMsg, Shared};
use crate::link::{self, Link};
use blake2::digest::{consts::U32, Mac};
use sha2::{Digest, Sha256};
use std::collections::HashMap;
use std::net::SocketAddr;
use std::sync::Arc;
use std::time::{Duration, Instant};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::TcpStream;
use tokio::sync::mpsc;

type Blake2bMac256 = blake2::Blake2bMac<U32>;

const VERSION: u8 = 1;
const T_HELLO: u8 = 0x00;
const T_AUTH: u8 = 0x01;
const T_AUTH_OK: u8 = 0x02;
const T_AUTH_FAIL: u8 = 0x03;
const T_SEND: u8 = 0x10;
const T_DELIVER: u8 = 0x11;
const T_SEND_FAIL: u8 = 0x12;
const T_PING: u8 = 0x20;
const T_PONG: u8 = 0x21;
const T_PEER_LIST: u8 = 0x40;
const T_PEER_JOIN: u8 = 0x41;
const T_PEER_LEAVE: u8 = 0x42;
const T_ERROR: u8 = 0x50;
const T_BYE: u8 = 0x7F;

const K_DATA: u8 = 0;
const K_OPEN: u8 = 1;
const K_CLOSE: u8 = 2;

const CHUNK: usize = 60_000;
pub const DEFAULT_PORT: u16 = 9443;

#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct RelayConfig {
    pub enabled: bool,
    pub host: String,
    pub port: u16,
    pub passphrase: String,
}

pub enum RelayCmd {
    Configure(RelayConfig),
    /// Ask for a relay session to this fingerprint (ignored unless we are the opener).
    Open(String),
    Rename,
}

pub enum RelayEvt {
    /// state: off | connecting | online | auth_failed | error
    Status { state: String, detail: String },
    PeerJoin { fp: String, name: String },
    PeerLeave { fp: String },
    /// All relay peers gone (relay connection lost).
    PeersCleared,
    Link { link: Box<Link>, initiator: bool },
}

pub fn spawn(shared: Arc<Shared>, tx: mpsc::UnboundedSender<EngineMsg>) -> mpsc::UnboundedSender<RelayCmd> {
    let (ctx, crx) = mpsc::unbounded_channel();
    tokio::spawn(run(shared, crx, tx));
    ctx
}

fn evt(tx: &mpsc::UnboundedSender<EngineMsg>, e: RelayEvt) {
    let _ = tx.send(EngineMsg::Relay(e));
}

fn status(tx: &mpsc::UnboundedSender<EngineMsg>, state: &str, detail: impl Into<String>) {
    evt(tx, RelayEvt::Status { state: state.into(), detail: detail.into() });
}

enum End {
    Reconfigure(RelayConfig),
    Stop,
    Fail { state: &'static str, detail: String },
}

async fn run(shared: Arc<Shared>, mut cmd: mpsc::UnboundedReceiver<RelayCmd>, tx: mpsc::UnboundedSender<EngineMsg>) {
    let mut cfg = RelayConfig::default();
    let mut key_cache: Option<(String, [u8; 16], u32, u32, [u8; 32])> = None;
    let mut fails: u32 = 0;
    loop {
        if !cfg.enabled || cfg.host.trim().is_empty() {
            status(&tx, "off", "");
            loop {
                match cmd.recv().await {
                    None => return,
                    Some(RelayCmd::Configure(c)) => {
                        cfg = c;
                        break;
                    }
                    Some(_) => {}
                }
            }
            fails = 0;
            continue;
        }
        status(&tx, "connecting", format!("{}:{}", cfg.host, cfg.port));
        let end = session(&shared, &cfg, &mut key_cache, &mut cmd, &tx).await;
        evt(&tx, RelayEvt::PeersCleared);
        let wait = match end {
            End::Stop => return,
            End::Reconfigure(c) => {
                cfg = c;
                fails = 0;
                continue;
            }
            End::Fail { state, detail } => {
                tracing::info!(state, %detail, "relay disconnected");
                status(&tx, state, detail);
                fails += 1;
                if state == "auth_failed" {
                    Duration::from_secs(60)
                } else {
                    Duration::from_millis((1000u64 << fails.min(5)).min(30_000))
                }
            }
        };
        let deadline = tokio::time::sleep(wait);
        tokio::pin!(deadline);
        loop {
            tokio::select! {
                _ = &mut deadline => break,
                c = cmd.recv() => match c {
                    None => return,
                    Some(RelayCmd::Configure(c)) => {
                        if c != cfg { fails = 0; }
                        cfg = c;
                        break;
                    }
                    Some(_) => {}
                }
            }
        }
    }
}

fn frame(t: u8, body: &[u8]) -> Vec<u8> {
    let mut v = Vec::with_capacity(5 + body.len());
    v.extend_from_slice(&((body.len() as u32 + 1).to_be_bytes()));
    v.push(t);
    v.extend_from_slice(body);
    v
}

async fn read_frame<R: AsyncReadExt + Unpin>(r: &mut R, max: usize) -> std::io::Result<(u8, Vec<u8>)> {
    let mut h = [0u8; 4];
    r.read_exact(&mut h).await?;
    let n = u32::from_be_bytes(h) as usize;
    if n == 0 || n > max + 64 {
        return Err(std::io::Error::new(std::io::ErrorKind::InvalidData, "bad frame length"));
    }
    let mut b = vec![0u8; n];
    r.read_exact(&mut b).await?;
    let t = b[0];
    b.remove(0);
    Ok((t, b))
}

pub fn derive_key(pass: &str, salt: &[u8], m_kib: u32, t: u32) -> anyhow::Result<[u8; 32]> {
    let params = argon2::Params::new(m_kib, t, 1, Some(32)).map_err(|e| anyhow::anyhow!("{e}"))?;
    let a = argon2::Argon2::new(argon2::Algorithm::Argon2id, argon2::Version::V0x13, params);
    let mut out = [0u8; 32];
    a.hash_password_into(pass.as_bytes(), salt, &mut out).map_err(|e| anyhow::anyhow!("{e}"))?;
    Ok(out)
}

pub fn auth_mac(key: &[u8; 32], label: &[u8], n1: &[u8], n2: &[u8], id: &[u8]) -> [u8; 32] {
    let mut m = <Blake2bMac256 as Mac>::new_from_slice(key).expect("key len");
    m.update(label);
    m.update(n1);
    m.update(n2);
    m.update(id);
    let out = m.finalize().into_bytes();
    let mut r = [0u8; 32];
    r.copy_from_slice(&out);
    r
}

fn parse_info(info: &[u8]) -> Option<(String, String)> {
    let s = std::str::from_utf8(info).ok()?;
    let mut it = s.split('\t');
    if it.next()? != "LSM5" {
        return None;
    }
    let fp = it.next()?.to_string();
    let name = it.next().unwrap_or("").to_string();
    if fp.is_empty() {
        return None;
    }
    Some((fp, name))
}

struct Sess {
    sid: u32,
    to_link: mpsc::UnboundedSender<Vec<u8>>,
    tasks: Vec<tokio::task::AbortHandle>,
}

impl Drop for Sess {
    fn drop(&mut self) {
        for t in &self.tasks {
            t.abort();
        }
    }
}

fn relay_addr() -> SocketAddr {
    SocketAddr::from(([0, 0, 0, 0], 0))
}

/// Create a relay session: an in-memory duplex whose far end is pumped through the relay.
fn new_sess(
    shared: &Arc<Shared>,
    tx: &mpsc::UnboundedSender<EngineMsg>,
    out: &mpsc::UnboundedSender<([u8; 32], u32, Option<Vec<u8>>)>,
    peer: [u8; 32],
    sid: u32,
    initiator: bool,
) -> Sess {
    let (a, b) = tokio::io::duplex(256 * 1024);
    let (mut br, mut bw) = tokio::io::split(b);
    let (to_link, mut from_relay) = mpsc::unbounded_channel::<Vec<u8>>();
    let out2 = out.clone();
    let pump_out = tokio::spawn(async move {
        let mut buf = vec![0u8; CHUNK];
        loop {
            match br.read(&mut buf).await {
                Ok(0) | Err(_) => break,
                Ok(n) => {
                    if out2.send((peer, sid, Some(buf[..n].to_vec()))).is_err() {
                        return;
                    }
                }
            }
        }
        let _ = out2.send((peer, sid, None));
    });
    let pump_in = tokio::spawn(async move {
        while let Some(d) = from_relay.recv().await {
            if bw.write_all(&d).await.is_err() {
                break;
            }
        }
        let _ = bw.shutdown().await;
    });
    let shared2 = shared.clone();
    let tx2 = tx.clone();
    let hs = tokio::spawn(async move {
        match link::establish_over(a, relay_addr(), true, &shared2.identity, shared2.hello(), initiator).await {
            Ok(l) => evt(&tx2, RelayEvt::Link { link: Box::new(l), initiator }),
            Err(e) => tracing::debug!(error = %e, "relay session handshake failed"),
        }
    });
    Sess { sid, to_link, tasks: vec![pump_out.abort_handle(), pump_in.abort_handle(), hs.abort_handle()] }
}

async fn session(
    shared: &Arc<Shared>,
    cfg: &RelayConfig,
    key_cache: &mut Option<(String, [u8; 16], u32, u32, [u8; 32])>,
    cmd: &mut mpsc::UnboundedReceiver<RelayCmd>,
    tx: &mpsc::UnboundedSender<EngineMsg>,
) -> End {
    let fail = |state: &'static str, d: String| End::Fail { state, detail: d };
    let port = if cfg.port == 0 { DEFAULT_PORT } else { cfg.port };
    let target = format!("{}:{}", cfg.host.trim(), port);
    let mut sock = match tokio::time::timeout(Duration::from_secs(10), TcpStream::connect(&target)).await {
        Ok(Ok(s)) => s,
        Ok(Err(e)) => return fail("error", format!("连接 {target} 失败: {e}")),
        Err(_) => return fail("error", format!("连接 {target} 超时")),
    };
    sock.set_nodelay(true).ok();
    // HELLO
    let hello = match tokio::time::timeout(Duration::from_secs(10), read_frame(&mut sock, 4096)).await {
        Ok(Ok((T_HELLO, b))) if b.len() >= 61 && &b[0..4] == b"LSMR" => b,
        Ok(Ok(_)) => return fail("error", "不是中转服务器 (bad HELLO)".into()),
        Ok(Err(e)) => return fail("error", format!("读取失败: {e}")),
        Err(_) => return fail("error", "服务器无响应".into()),
    };
    if hello[4] != VERSION {
        return fail("error", format!("服务器协议版本 {} 不支持", hello[4]));
    }
    let mut salt = [0u8; 16];
    salt.copy_from_slice(&hello[5..21]);
    let m_kib = u32::from_be_bytes(hello[21..25].try_into().unwrap());
    let t = u32::from_be_bytes(hello[25..29].try_into().unwrap());
    let ns = hello[29..61].to_vec();
    if !(8..=1_048_576).contains(&m_kib) || !(1..=16).contains(&t) {
        return fail("error", "服务器参数异常".into());
    }
    let key = match key_cache {
        Some((p, s, m, tt, k)) if *p == cfg.passphrase && *s == salt && *m == m_kib && *tt == t => *k,
        _ => {
            let pass = cfg.passphrase.clone();
            let k = match tokio::task::spawn_blocking(move || derive_key(&pass, &salt, m_kib, t)).await {
                Ok(Ok(k)) => k,
                _ => return fail("error", "密钥派生失败".into()),
            };
            *key_cache = Some((cfg.passphrase.clone(), salt, m_kib, t, k));
            k
        }
    };
    let myid: [u8; 32] = Sha256::digest(shared.identity.x25519_public_bytes()).into();
    let nc: [u8; 32] = rand::random();
    let mac = auth_mac(&key, b"LSMR-v1-client", &ns, &nc, &myid);
    let name = shared.name.lock().unwrap().clone();
    let mut info = format!("LSM5\t{}\t{}\t{}", shared.identity.fingerprint(), name.replace('\t', " "), crate::APP_VERSION).into_bytes();
    info.truncate(512);
    let mut body = vec![VERSION];
    body.extend_from_slice(&nc);
    body.extend_from_slice(&myid);
    body.extend_from_slice(&mac);
    body.extend_from_slice(&(info.len() as u16).to_be_bytes());
    body.extend_from_slice(&info);
    if let Err(e) = sock.write_all(&frame(T_AUTH, &body)).await {
        return fail("error", format!("发送失败: {e}"));
    }
    let (max_frame, hb) = match tokio::time::timeout(Duration::from_secs(15), read_frame(&mut sock, 4096)).await {
        Ok(Ok((T_AUTH_OK, b))) if b.len() >= 38 => {
            let want = auth_mac(&key, b"LSMR-v1-server", &nc, &ns, &myid);
            if !bool::from(subtle_eq(&b[0..32], &want)) {
                return fail("error", "服务器身份校验失败 (server MAC mismatch)".into());
            }
            let mf = u32::from_be_bytes(b[32..36].try_into().unwrap()) as usize;
            let hb = u16::from_be_bytes(b[36..38].try_into().unwrap()).max(5) as u64;
            (mf.max(1024), hb)
        }
        Ok(Ok((T_AUTH_FAIL, b))) => {
            let code = b.first().copied().unwrap_or(0);
            return match code {
                1 => fail("auth_failed", "口令错误 (wrong passphrase)".into()),
                5 => fail("auth_failed", "尝试次数过多，请稍后再试".into()),
                4 => fail("error", "服务器已满".into()),
                2 => fail("error", "协议版本不兼容".into()),
                c => fail("error", format!("认证失败 (code {c})")),
            };
        }
        Ok(Ok((t, _))) => return fail("error", format!("意外的响应 {t:#x}")),
        Ok(Err(e)) => return fail("error", format!("连接断开: {e}")),
        Err(_) => return fail("error", "认证超时".into()),
    };
    let chunk = CHUNK.min(max_frame.saturating_sub(64)).max(512);
    tracing::info!(%target, "relay online");
    status(tx, "online", target.clone());

    let (mut rd, mut wr) = sock.into_split();
    let (in_tx, mut in_rx) = mpsc::channel::<std::io::Result<(u8, Vec<u8>)>>(64);
    let reader = tokio::spawn(async move {
        loop {
            let r = read_frame(&mut rd, max_frame).await;
            let stop = r.is_err();
            if in_tx.send(r).await.is_err() || stop {
                break;
            }
        }
    });
    let (wtx, mut wrx) = mpsc::channel::<Vec<u8>>(256);
    let writer = tokio::spawn(async move {
        while let Some(f) = wrx.recv().await {
            if wr.write_all(&f).await.is_err() {
                break;
            }
        }
        let _ = wr.shutdown().await;
    });
    struct Guard(Vec<tokio::task::AbortHandle>);
    impl Drop for Guard {
        fn drop(&mut self) {
            for h in &self.0 {
                h.abort();
            }
        }
    }
    let _guard = Guard(vec![reader.abort_handle(), writer.abort_handle()]);

    let (out_tx, mut out_rx) = mpsc::unbounded_channel::<([u8; 32], u32, Option<Vec<u8>>)>();
    let mut peers: HashMap<[u8; 32], String> = HashMap::new();
    let mut by_fp: HashMap<String, [u8; 32]> = HashMap::new();
    let mut sess: HashMap<[u8; 32], Sess> = HashMap::new();
    let mut last_rx = Instant::now();
    let mut ping = tokio::time::interval(Duration::from_secs(hb));
    ping.tick().await;

    macro_rules! send {
        ($f:expr) => {
            if wtx.send($f).await.is_err() {
                return fail("error", "连接断开".into());
            }
        };
    }
    let blob = |dst: &[u8; 32], kind: u8, sid: u32, data: &[u8]| {
        let mut b = Vec::with_capacity(37 + data.len());
        b.extend_from_slice(dst);
        b.push(kind);
        b.extend_from_slice(&sid.to_be_bytes());
        b.extend_from_slice(data);
        frame(T_SEND, &b)
    };

    loop {
        tokio::select! {
            r = in_rx.recv() => {
                let (t, b) = match r {
                    Some(Ok(x)) => x,
                    Some(Err(e)) => return fail("error", format!("连接断开: {e}")),
                    None => return fail("error", "连接断开".into()),
                };
                last_rx = Instant::now();
                match t {
                    T_PING => send!(frame(T_PONG, &[])),
                    T_PONG => {}
                    T_PEER_LIST | T_PEER_JOIN => {
                        let mut off = if t == T_PEER_LIST { 2 } else { 0 };
                        while b.len() >= off + 34 {
                            let mut id = [0u8; 32];
                            id.copy_from_slice(&b[off..off + 32]);
                            let il = u16::from_be_bytes([b[off + 32], b[off + 33]]) as usize;
                            let end = (off + 34 + il).min(b.len());
                            if id != myid {
                                if let Some((fp, name)) = parse_info(&b[off + 34..end]) {
                                    if let Some(old) = peers.insert(id, fp.clone()) {
                                        by_fp.remove(&old);
                                    }
                                    by_fp.insert(fp.clone(), id);
                                    evt(tx, RelayEvt::PeerJoin { fp, name });
                                }
                            }
                            off = end;
                            if t == T_PEER_JOIN { break; }
                        }
                    }
                    T_PEER_LEAVE if b.len() >= 32 => {
                        let mut id = [0u8; 32];
                        id.copy_from_slice(&b[..32]);
                        sess.remove(&id);
                        if let Some(fp) = peers.remove(&id) {
                            by_fp.remove(&fp);
                            evt(tx, RelayEvt::PeerLeave { fp });
                        }
                    }
                    T_DELIVER if b.len() >= 37 => {
                        let mut src = [0u8; 32];
                        src.copy_from_slice(&b[..32]);
                        let kind = b[32];
                        let sid = u32::from_be_bytes(b[33..37].try_into().unwrap());
                        let data = &b[37..];
                        match kind {
                            K_OPEN => {
                                // Responder side; replaces any older session with this peer.
                                let s = new_sess(shared, tx, &out_tx, src, sid, false);
                                if !data.is_empty() { let _ = s.to_link.send(data.to_vec()); }
                                sess.insert(src, s);
                            }
                            K_DATA => {
                                match sess.get(&src) {
                                    Some(s) if s.sid == sid => { let _ = s.to_link.send(data.to_vec()); }
                                    _ => send!(blob(&src, K_CLOSE, sid, &[])),
                                }
                            }
                            K_CLOSE => {
                                if sess.get(&src).map(|s| s.sid == sid).unwrap_or(false) {
                                    sess.remove(&src);
                                }
                            }
                            _ => {}
                        }
                    }
                    T_SEND_FAIL if b.len() >= 32 => {
                        let mut id = [0u8; 32];
                        id.copy_from_slice(&b[..32]);
                        sess.remove(&id);
                    }
                    T_ERROR => {
                        let code = b.first().copied().unwrap_or(0);
                        let d = match code {
                            6 => "同一身份在别处登录 (replaced)".to_string(),
                            13 => "空闲超时".to_string(),
                            14 => "服务器关闭".to_string(),
                            c => format!("服务器错误 code {c}"),
                        };
                        return fail("error", d);
                    }
                    T_BYE => return fail("error", "服务器关闭连接".into()),
                    _ => {}
                }
            }
            o = out_rx.recv() => {
                let Some((dst, sid, data)) = o else { continue };
                // Only forward for the current session of that peer.
                if !sess.get(&dst).map(|s| s.sid == sid).unwrap_or(false) { continue; }
                match data {
                    Some(d) => {
                        for c in d.chunks(chunk) {
                            send!(blob(&dst, K_DATA, sid, c));
                        }
                    }
                    None => {
                        sess.remove(&dst);
                        send!(blob(&dst, K_CLOSE, sid, &[]));
                    }
                }
            }
            c = cmd.recv() => match c {
                None => return End::Stop,
                Some(RelayCmd::Configure(c)) => {
                    if c != *cfg {
                        let _ = wtx.send(frame(T_BYE, &[])).await;
                        return End::Reconfigure(c);
                    }
                }
                Some(RelayCmd::Rename) => {
                    // Info is fixed per relay connection; reconnect to publish the new name.
                    let _ = wtx.send(frame(T_BYE, &[])).await;
                    return End::Reconfigure(cfg.clone());
                }
                Some(RelayCmd::Open(fp)) => {
                    let Some(id) = by_fp.get(&fp).copied() else { continue };
                    if myid >= id || sess.contains_key(&id) { continue; }
                    let sid: u32 = rand::random::<u32>() | 1;
                    let s = new_sess(shared, tx, &out_tx, id, sid, true);
                    sess.insert(id, s);
                    send!(blob(&id, K_OPEN, sid, &[]));
                }
            },
            _ = ping.tick() => {
                if last_rx.elapsed() > Duration::from_secs(hb * 3) {
                    return fail("error", "服务器心跳超时".into());
                }
                send!(frame(T_PING, &[]));
            }
        }
    }
}

fn subtle_eq(a: &[u8], b: &[u8]) -> bool {
    a.len() == b.len() && a.iter().zip(b).fold(0u8, |acc, (x, y)| acc | (x ^ y)) == 0
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn vector_matches_server() {
        let k = derive_key("test-pass", b"LSMR-default-slt", 19456, 2).unwrap();
        assert_eq!(&hex::encode(k)[..8], "63d2d3a3");
    }
}
