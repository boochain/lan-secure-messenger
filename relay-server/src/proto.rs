//! LSM relay wire protocol v1 (see docs/relay-protocol.md).
//!
//! Frame = u32 BE length (of type+body) | u8 type | body.

use anyhow::{bail, Result};
use argon2::{Algorithm, Argon2, Params, Version};
use blake2::digest::{consts::U32, Mac};
use blake2::Blake2bMac;
use tokio::io::{AsyncRead, AsyncReadExt, AsyncWrite, AsyncWriteExt};

pub const MAGIC: &[u8; 4] = b"LSMR";
pub const PROTO_VERSION: u8 = 1;
pub const ID_LEN: usize = 32;
pub const NONCE_LEN: usize = 32;
pub const SALT_LEN: usize = 16;
pub const MAC_LEN: usize = 32;
pub const MAX_INFO: usize = 512;
/// Hard ceiling regardless of config (16 MiB).
pub const HARD_MAX_FRAME: u32 = 16 * 1024 * 1024;
pub const DEFAULT_SALT: [u8; SALT_LEN] = *b"LSMR-default-slt";
pub const ARGON_M_KIB: u32 = 19456;
pub const ARGON_T: u32 = 2;

pub type PeerId = [u8; ID_LEN];

pub mod t {
    // server -> client
    pub const HELLO: u8 = 0x00;
    pub const AUTH_OK: u8 = 0x02;
    pub const AUTH_FAIL: u8 = 0x03;
    pub const DELIVER: u8 = 0x11;
    pub const SEND_FAIL: u8 = 0x12;
    pub const PEER_LIST: u8 = 0x40;
    pub const PEER_JOIN: u8 = 0x41;
    pub const PEER_LEAVE: u8 = 0x42;
    pub const ERROR: u8 = 0x50;
    // client -> server
    pub const AUTH: u8 = 0x01;
    pub const SEND: u8 = 0x10;
    pub const LIST: u8 = 0x30;
    pub const BYE: u8 = 0x7F;
    // account / fingerprint restore (optional)
    pub const ACCOUNT_REGISTER: u8 = 0x60;
    pub const ACCOUNT_LOGIN: u8 = 0x61;
    pub const ACCOUNT_OK: u8 = 0x62;
    pub const ACCOUNT_FAIL: u8 = 0x63;
    pub const ACCOUNT_PUT_BACKUP: u8 = 0x64;
    pub const ACCOUNT_GET_BACKUP: u8 = 0x65;
    pub const ACCOUNT_BACKUP: u8 = 0x66;
    // server-retain encrypted blobs (optional)
    pub const STORE: u8 = 0x70;
    pub const STORE_OK: u8 = 0x71;
    pub const FETCH: u8 = 0x72;
    pub const FETCH_DATA: u8 = 0x73;
    pub const FETCH_MISS: u8 = 0x74;
    pub const STORE_DEL: u8 = 0x75;
    pub const LIST_RETAIN: u8 = 0x76;
    pub const LIST_RETAIN_DATA: u8 = 0x77;
    pub const ACCOUNT_REVOKE: u8 = 0x67;
    // both directions
    pub const PING: u8 = 0x20;
    pub const PONG: u8 = 0x21;
}

/// AUTH_FAIL / SEND_FAIL / ERROR codes.
pub mod code {
    pub const BAD_PASSPHRASE: u8 = 1;
    pub const BAD_VERSION: u8 = 2;
    pub const MALFORMED: u8 = 3;
    pub const SERVER_FULL: u8 = 4;
    pub const TOO_MANY_ATTEMPTS: u8 = 5;
    pub const REPLACED: u8 = 6; // same peer id logged in elsewhere
    pub const PEER_OFFLINE: u8 = 10;
    pub const PEER_BUSY: u8 = 11; // receiver queue full / too slow
    pub const FRAME_TOO_LARGE: u8 = 12;
    pub const IDLE_TIMEOUT: u8 = 13;
    pub const SHUTDOWN: u8 = 14;
}

pub const CTX_CLIENT: &[u8] = b"LSMR-v1-client";
pub const CTX_SERVER: &[u8] = b"LSMR-v1-server";

/// key = Argon2id(passphrase, salt, m=19456 KiB, t=2, p=1, out=32).
pub fn derive_key(passphrase: &str, salt: &[u8; SALT_LEN]) -> [u8; 32] {
    let params = Params::new(ARGON_M_KIB, ARGON_T, 1, Some(32)).expect("argon params");
    let a = Argon2::new(Algorithm::Argon2id, Version::V0x13, params);
    let mut out = [0u8; 32];
    a.hash_password_into(passphrase.as_bytes(), salt, &mut out).expect("argon2");
    out
}

/// mac = BLAKE2b-256(key=K, ctx || n1 || n2 || id)
pub fn auth_mac(key: &[u8; 32], ctx: &[u8], n1: &[u8], n2: &[u8], id: &PeerId) -> [u8; MAC_LEN] {
    let mut m = <Blake2bMac<U32> as Mac>::new_from_slice(key).expect("key");
    m.update(ctx);
    m.update(n1);
    m.update(n2);
    m.update(id);
    m.finalize().into_bytes().into()
}

pub fn ct_eq(a: &[u8], b: &[u8]) -> bool {
    use subtle::ConstantTimeEq;
    a.len() == b.len() && bool::from(a.ct_eq(b))
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Frame {
    pub ty: u8,
    pub body: Vec<u8>,
}

impl Frame {
    pub fn new(ty: u8, body: Vec<u8>) -> Self {
        Frame { ty, body }
    }
    pub fn encode(&self) -> Vec<u8> {
        let mut v = Vec::with_capacity(5 + self.body.len());
        v.extend_from_slice(&((self.body.len() as u32 + 1).to_be_bytes()));
        v.push(self.ty);
        v.extend_from_slice(&self.body);
        v
    }
}

#[derive(Debug)]
pub enum ReadErr {
    Eof,
    TooLarge(u32),
    Empty,
    Io(std::io::Error),
}

impl std::fmt::Display for ReadErr {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            ReadErr::Eof => write!(f, "eof"),
            ReadErr::TooLarge(n) => write!(f, "frame too large ({n} bytes)"),
            ReadErr::Empty => write!(f, "zero-length frame"),
            ReadErr::Io(e) => write!(f, "io: {e}"),
        }
    }
}
impl std::error::Error for ReadErr {}

pub async fn read_frame<R: AsyncRead + Unpin>(r: &mut R, max: u32) -> std::result::Result<Frame, ReadErr> {
    let mut len = [0u8; 4];
    if let Err(e) = r.read_exact(&mut len).await {
        return Err(if e.kind() == std::io::ErrorKind::UnexpectedEof { ReadErr::Eof } else { ReadErr::Io(e) });
    }
    let n = u32::from_be_bytes(len);
    if n == 0 {
        return Err(ReadErr::Empty);
    }
    if n > max.min(HARD_MAX_FRAME) {
        return Err(ReadErr::TooLarge(n));
    }
    let mut buf = vec![0u8; n as usize];
    r.read_exact(&mut buf).await.map_err(|e| {
        if e.kind() == std::io::ErrorKind::UnexpectedEof { ReadErr::Eof } else { ReadErr::Io(e) }
    })?;
    let ty = buf[0];
    buf.remove(0);
    Ok(Frame { ty, body: buf })
}

pub async fn write_frame<W: AsyncWrite + Unpin>(w: &mut W, f: &Frame) -> std::io::Result<()> {
    w.write_all(&f.encode()).await
}

// ---------- typed bodies ----------

pub struct Hello {
    pub version: u8,
    pub salt: [u8; SALT_LEN],
    pub m_kib: u32,
    pub t_cost: u32,
    pub nonce: [u8; NONCE_LEN],
}

impl Hello {
    pub fn encode(&self) -> Frame {
        let mut b = Vec::with_capacity(4 + 1 + 16 + 8 + 32);
        b.extend_from_slice(MAGIC);
        b.push(self.version);
        b.extend_from_slice(&self.salt);
        b.extend_from_slice(&self.m_kib.to_be_bytes());
        b.extend_from_slice(&self.t_cost.to_be_bytes());
        b.extend_from_slice(&self.nonce);
        Frame::new(t::HELLO, b)
    }
    pub fn decode(f: &Frame) -> Result<Self> {
        let b = &f.body;
        if f.ty != t::HELLO || b.len() < 61 || &b[0..4] != MAGIC {
            bail!("not an LSMR relay (bad HELLO)");
        }
        Ok(Hello {
            version: b[4],
            salt: b[5..21].try_into()?,
            m_kib: u32::from_be_bytes(b[21..25].try_into()?),
            t_cost: u32::from_be_bytes(b[25..29].try_into()?),
            nonce: b[29..61].try_into()?,
        })
    }
}

pub struct Auth {
    pub version: u8,
    pub client_nonce: [u8; NONCE_LEN],
    pub id: PeerId,
    pub mac: [u8; MAC_LEN],
    pub info: Vec<u8>,
}

impl Auth {
    pub fn encode(&self) -> Frame {
        let mut b = Vec::new();
        b.push(self.version);
        b.extend_from_slice(&self.client_nonce);
        b.extend_from_slice(&self.id);
        b.extend_from_slice(&self.mac);
        b.extend_from_slice(&(self.info.len() as u16).to_be_bytes());
        b.extend_from_slice(&self.info);
        Frame::new(t::AUTH, b)
    }
    pub fn decode(f: &Frame) -> Result<Self> {
        let b = &f.body;
        if f.ty != t::AUTH || b.len() < 99 {
            bail!("malformed AUTH");
        }
        let il = u16::from_be_bytes([b[97], b[98]]) as usize;
        if il > MAX_INFO || b.len() != 99 + il {
            bail!("malformed AUTH info");
        }
        Ok(Auth {
            version: b[0],
            client_nonce: b[1..33].try_into()?,
            id: b[33..65].try_into()?,
            mac: b[65..97].try_into()?,
            info: b[99..].to_vec(),
        })
    }
}

pub fn peer_entry(id: &PeerId, info: &[u8], out: &mut Vec<u8>) {
    out.extend_from_slice(id);
    out.extend_from_slice(&(info.len() as u16).to_be_bytes());
    out.extend_from_slice(info);
}

/// Parse PEER_LIST body: u16 count, entries.  PEER_JOIN body = one entry.
pub fn parse_entries(b: &[u8], with_count: bool) -> Result<Vec<(PeerId, Vec<u8>)>> {
    let mut p = 0usize;
    let n = if with_count {
        if b.len() < 2 { bail!("short list"); }
        p = 2;
        u16::from_be_bytes([b[0], b[1]]) as usize
    } else { 1 };
    let mut v = Vec::with_capacity(n);
    for _ in 0..n {
        if b.len() < p + 34 { bail!("short entry"); }
        let id: PeerId = b[p..p + 32].try_into()?;
        let il = u16::from_be_bytes([b[p + 32], b[p + 33]]) as usize;
        p += 34;
        if b.len() < p + il { bail!("short info"); }
        v.push((id, b[p..p + il].to_vec()));
        p += il;
    }
    Ok(v)
}

pub fn send_body(dst: &PeerId, payload: &[u8]) -> Vec<u8> {
    let mut b = Vec::with_capacity(32 + payload.len());
    b.extend_from_slice(dst);
    b.extend_from_slice(payload);
    b
}

pub fn fail_body(code: u8, msg: &str) -> Vec<u8> {
    let mut b = vec![code];
    b.extend_from_slice(msg.as_bytes());
    b
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn frame_roundtrip() {
        let f = Frame::new(t::SEND, vec![1, 2, 3]);
        let e = f.encode();
        assert_eq!(&e[..5], &[0, 0, 0, 4, t::SEND]);
        let rt = tokio_test_block(async move {
            let mut c = std::io::Cursor::new(e);
            read_frame(&mut c, 1024).await.unwrap()
        });
        assert_eq!(rt, f);
    }

    #[test]
    fn oversize_and_empty_rejected() {
        let r = tokio_test_block(async {
            let mut c = std::io::Cursor::new(vec![0, 0, 0x10, 0, 1]);
            read_frame(&mut c, 1024).await
        });
        assert!(matches!(r, Err(ReadErr::TooLarge(_))));
        let r = tokio_test_block(async {
            let mut c = std::io::Cursor::new(vec![0, 0, 0, 0]);
            read_frame(&mut c, 1024).await
        });
        assert!(matches!(r, Err(ReadErr::Empty)));
    }

    #[test]
    fn hello_auth_roundtrip() {
        let h = Hello { version: 1, salt: [7; 16], m_kib: 8, t_cost: 1, nonce: [9; 32] };
        let d = Hello::decode(&h.encode()).unwrap();
        assert_eq!(d.salt, [7; 16]);
        assert_eq!(d.nonce, [9; 32]);
        let a = Auth { version: 1, client_nonce: [1; 32], id: [2; 32], mac: [3; 32], info: b"bill".to_vec() };
        let d = Auth::decode(&a.encode()).unwrap();
        assert_eq!(d.info, b"bill");
        assert_eq!(d.id, [2; 32]);
        // truncated
        let mut f = a.encode();
        f.body.pop();
        assert!(Auth::decode(&f).is_err());
    }

    #[test]
    fn mac_known_vector() {
        // Fixed vector shared with the C client test (c-client/relay_client.c --selftest)
        let k = derive_key("test-pass", &DEFAULT_SALT);
        let m = auth_mac(&k, CTX_CLIENT, &[1; 32], &[2; 32], &[3; 32]);
        // Produced independently by Monocypher 4 (c-client --selftest).
        assert_eq!(hex::encode(k), "63d2d3a326f89cb06409e204bbe49c08d46a56b4725318c66b4f97ac6d637d5d");
        assert_eq!(hex::encode(m), "84e6b26fd79d428db2267c1360133ff87c720f067cf59b2becaa38eddb24a7b1");
        assert!(ct_eq(&m, &auth_mac(&k, CTX_CLIENT, &[1; 32], &[2; 32], &[3; 32])));
        assert!(!ct_eq(&m, &auth_mac(&k, CTX_SERVER, &[1; 32], &[2; 32], &[3; 32])));
    }

    #[test]
    fn entries_parse() {
        let mut b = 2u16.to_be_bytes().to_vec();
        peer_entry(&[1; 32], b"a", &mut b);
        peer_entry(&[2; 32], b"", &mut b);
        let v = parse_entries(&b, true).unwrap();
        assert_eq!(v.len(), 2);
        assert_eq!(v[0].1, b"a");
        assert!(parse_entries(&b[..b.len() - 1], true).is_err());
    }

    fn tokio_test_block<F: std::future::Future>(f: F) -> F::Output {
        tokio::runtime::Builder::new_current_thread().build().unwrap().block_on(f)
    }
}
