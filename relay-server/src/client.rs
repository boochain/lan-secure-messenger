//! Minimal async reference client (used by tests and `relay-test-client`).
//! Real clients (lan-daemon, C core) implement the same protocol; payloads are
//! already Noise-encrypted before they reach this layer.
use crate::proto::{self, code, t, Auth, Frame, Hello, PeerId};
use anyhow::{bail, Context, Result};
use rand::RngCore;
use std::collections::HashMap;
use std::sync::Mutex;
use std::time::Duration;
use tokio::io::{AsyncWriteExt, BufReader, BufWriter};
use tokio::net::TcpStream;
use tokio::sync::mpsc;

#[derive(Debug, Clone)]
pub enum Event {
    PeerList(Vec<(PeerId, Vec<u8>)>),
    Join(PeerId, Vec<u8>),
    Leave(PeerId),
    Deliver(PeerId, Vec<u8>),
    SendFail(PeerId, u8),
    Error(u8, String),
    Pong(Vec<u8>),
    Closed,
}

#[derive(Debug)]
pub struct AuthError {
    pub code: u8,
    pub msg: String,
}
impl std::fmt::Display for AuthError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "认证失败 (auth failed) code={} {}", self.code, self.msg)
    }
}
impl std::error::Error for AuthError {}

static KEY_CACHE: Mutex<Option<HashMap<(String, [u8; 16]), [u8; 32]>>> = Mutex::new(None);

async fn key_for(pass: &str, salt: [u8; 16]) -> Result<[u8; 32]> {
    if let Some(k) = KEY_CACHE.lock().unwrap().get_or_insert_with(HashMap::new).get(&(pass.to_string(), salt)) {
        return Ok(*k);
    }
    let p = pass.to_string();
    let k = tokio::task::spawn_blocking(move || proto::derive_key(&p, &salt)).await?;
    KEY_CACHE.lock().unwrap().get_or_insert_with(HashMap::new).insert((pass.to_string(), salt), k);
    Ok(k)
}

pub struct Client {
    pub id: PeerId,
    pub max_frame: u32,
    pub heartbeat: u16,
    tx: mpsc::Sender<Vec<u8>>,
    pub events: mpsc::Receiver<Event>,
}

impl Client {
    pub async fn connect(addr: &str, passphrase: &str, id: PeerId, info: &[u8]) -> Result<Client> {
        let sock = tokio::time::timeout(Duration::from_secs(5), TcpStream::connect(addr))
            .await
            .context("连接超时 (connect timeout)")??;
        sock.set_nodelay(true)?;
        let (rd, wr) = sock.into_split();
        let mut rd = BufReader::with_capacity(256 * 1024, rd);
        let mut wr = BufWriter::with_capacity(256 * 1024, wr);
        let f = tokio::time::timeout(Duration::from_secs(10), proto::read_frame(&mut rd, 4096)).await??;
        if f.ty == t::ERROR || f.ty == t::AUTH_FAIL {
            bail!(AuthError { code: f.body.first().copied().unwrap_or(0), msg: String::from_utf8_lossy(f.body.get(1..).unwrap_or(&[])).into() });
        }
        let hello = Hello::decode(&f)?;
        if hello.version != proto::PROTO_VERSION {
            bail!("服务器协议版本不兼容 (protocol version) {}", hello.version);
        }
        let key = key_for(passphrase, hello.salt).await?;
        let mut cn = [0u8; 32];
        rand::thread_rng().fill_bytes(&mut cn);
        let mac = proto::auth_mac(&key, proto::CTX_CLIENT, &hello.nonce, &cn, &id);
        let a = Auth { version: proto::PROTO_VERSION, client_nonce: cn, id, mac, info: info.to_vec() };
        proto::write_frame(&mut wr, &a.encode()).await?;
        wr.flush().await?;
        let f = tokio::time::timeout(Duration::from_secs(10), proto::read_frame(&mut rd, 4096)).await??;
        match f.ty {
            t::AUTH_OK if f.body.len() >= 38 => {}
            t::AUTH_FAIL | t::ERROR => bail!(AuthError {
                code: f.body.first().copied().unwrap_or(0),
                msg: String::from_utf8_lossy(f.body.get(1..).unwrap_or(&[])).into()
            }),
            _ => bail!("意外的应答 (unexpected reply) 0x{:02x}", f.ty),
        }
        let smac = proto::auth_mac(&key, proto::CTX_SERVER, &cn, &hello.nonce, &id);
        if !proto::ct_eq(&smac, &f.body[..32]) {
            bail!("服务器未通过口令验证 (server failed mutual auth)");
        }
        let max_frame = u32::from_be_bytes(f.body[32..36].try_into()?);
        let heartbeat = u16::from_be_bytes(f.body[36..38].try_into()?);

        let (tx, mut wrx) = mpsc::channel::<Vec<u8>>(64);
        let (etx, events) = mpsc::channel::<Event>(4096);
        tokio::spawn(async move {
            while let Some(m) = wrx.recv().await {
                if wr.write_all(&m).await.is_err() {
                    break;
                }
                if wrx.is_empty() && wr.flush().await.is_err() {
                    break;
                }
            }
            let _ = wr.shutdown().await;
        });
        let ptx = tx.clone();
        let hb = heartbeat.max(1) as u64;
        tokio::spawn(async move {
            loop {
                tokio::time::sleep(Duration::from_secs(hb)).await;
                if ptx.send(Frame::new(t::PING, b"hb".to_vec()).encode()).await.is_err() {
                    break;
                }
            }
        });
        let mf = max_frame + 64;
        tokio::spawn(async move {
            loop {
                let f = match proto::read_frame(&mut rd, mf).await {
                    Ok(f) => f,
                    Err(_) => break,
                };
                let ev = match f.ty {
                    t::PEER_LIST => Event::PeerList(proto::parse_entries(&f.body, true).unwrap_or_default()),
                    t::PEER_JOIN => match proto::parse_entries(&f.body, false) {
                        Ok(mut v) if !v.is_empty() => {
                            let (i, inf) = v.remove(0);
                            Event::Join(i, inf)
                        }
                        _ => continue,
                    },
                    t::PEER_LEAVE if f.body.len() >= 32 => Event::Leave(f.body[..32].try_into().unwrap()),
                    t::DELIVER if f.body.len() >= 32 => {
                        let src: PeerId = f.body[..32].try_into().unwrap();
                        let mut b = f.body;
                        b.drain(..32);
                        Event::Deliver(src, b)
                    }
                    t::SEND_FAIL if f.body.len() >= 33 => Event::SendFail(f.body[..32].try_into().unwrap(), f.body[32]),
                    t::ERROR => Event::Error(f.body.first().copied().unwrap_or(0), String::from_utf8_lossy(f.body.get(1..).unwrap_or(&[])).into()),
                    t::PONG => Event::Pong(f.body),
                    t::PING => continue,
                    _ => continue,
                };
                if etx.send(ev).await.is_err() {
                    break;
                }
            }
            let _ = etx.send(Event::Closed).await;
        });
        Ok(Client { id, max_frame, heartbeat, tx, events })
    }

    /// Send an opaque (already end-to-end encrypted) blob to `dst`.
    pub async fn send(&self, dst: &PeerId, blob: &[u8]) -> Result<()> {
        if blob.len() + 33 > self.max_frame as usize {
            bail!("帧过大 (frame too large) {} > {}", blob.len() + 33, self.max_frame);
        }
        self.tx.send(Frame::new(t::SEND, proto::send_body(dst, blob)).encode()).await.map_err(|_| anyhow::anyhow!("连接已关闭 (closed)"))
    }
    pub async fn raw(&self, bytes: Vec<u8>) -> Result<()> {
        self.tx.send(bytes).await.map_err(|_| anyhow::anyhow!("closed"))
    }
    pub async fn ping(&self, token: &[u8]) -> Result<()> {
        self.raw(Frame::new(t::PING, token.to_vec()).encode()).await
    }
    pub async fn list(&self) -> Result<()> {
        self.raw(Frame::new(t::LIST, vec![]).encode()).await
    }
    pub async fn bye(&self) {
        let _ = self.raw(Frame::new(t::BYE, vec![]).encode()).await;
    }
    /// Max payload bytes per SEND.
    pub fn max_payload(&self) -> usize {
        self.max_frame as usize - 33
    }
    /// Wait for the next event matching `pred` (with timeout).
    pub async fn wait_for<F: FnMut(&Event) -> bool>(&mut self, dur: Duration, mut pred: F) -> Result<Event> {
        let deadline = tokio::time::Instant::now() + dur;
        loop {
            let ev = tokio::time::timeout_at(deadline, self.events.recv()).await.context("等待事件超时 (timeout)")?;
            let ev = ev.ok_or_else(|| anyhow::anyhow!("closed"))?;
            if pred(&ev) {
                return Ok(ev);
            }
            if matches!(ev, Event::Closed) {
                bail!("connection closed");
            }
        }
    }
}

pub fn is_auth_fail(e: &anyhow::Error, c: u8) -> bool {
    e.downcast_ref::<AuthError>().map(|a| a.code == c).unwrap_or(false)
}
pub const BAD_PASS: u8 = code::BAD_PASSPHRASE;

/// Peer id helper: 32 bytes from a label (tests) — real clients use SHA-256 of their static key.
pub fn test_id(label: &str) -> PeerId {
    use blake2::{Blake2s256, Digest};
    Blake2s256::digest(label.as_bytes()).into()
}
