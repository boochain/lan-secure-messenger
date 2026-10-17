//! Noise_XX over TCP, split into independent read / write halves so one connection can
//! carry traffic in both directions concurrently. Uses snow's stateless transport with
//! explicit per-direction nonce counters (TCP guarantees ordering).

use crate::crypto::{build_initiator, build_responder};
use crate::identity::{Fingerprint, Identity};
use crate::wire::{self, Frame, Hello};
use anyhow::{anyhow, bail, Context, Result};
use snow::StatelessTransportState;
use std::sync::Arc;
use std::time::Duration;
use tokio::io::{AsyncRead, AsyncReadExt, AsyncWrite, AsyncWriteExt};
use tokio::net::TcpStream;

/// Read / write halves of a link transport: a LAN TCP stream, or a relay session
/// (an in-memory duplex pumped by `relay.rs`, carrying exactly the same bytes).
pub type RdHalf = Box<dyn AsyncRead + Send + Unpin>;
pub type WrHalf = Box<dyn AsyncWrite + Send + Unpin>;

const MAX_WIRE: usize = 65535 + 4;
pub const HANDSHAKE_TIMEOUT: Duration = Duration::from_secs(10);

pub struct LinkReader {
    rd: RdHalf,
    st: Arc<StatelessTransportState>,
    nonce: u64,
    buf: Vec<u8>,
    out: Vec<u8>,
}

pub struct LinkWriter {
    wr: WrHalf,
    st: Arc<StatelessTransportState>,
    nonce: u64,
    buf: Vec<u8>,
}

pub struct Link {
    pub reader: LinkReader,
    pub writer: LinkWriter,
    pub remote_fp: Fingerprint,
    pub remote_hello: Hello,
    pub remote_addr: std::net::SocketAddr,
    /// Carried by the relay server (UI badge 经服务器) instead of a direct LAN connection.
    pub via_relay: bool,
}

async fn write_raw(wr: &mut (impl AsyncWriteExt + Unpin), data: &[u8]) -> Result<()> {
    let mut v = Vec::with_capacity(4 + data.len());
    v.extend_from_slice(&(data.len() as u32).to_be_bytes());
    v.extend_from_slice(data);
    wr.write_all(&v).await?;
    Ok(())
}

async fn read_raw(rd: &mut (impl AsyncReadExt + Unpin), buf: &mut Vec<u8>) -> Result<usize> {
    let mut len = [0u8; 4];
    rd.read_exact(&mut len).await?;
    let n = u32::from_be_bytes(len) as usize;
    if n == 0 || n > MAX_WIRE {
        bail!("bad frame length {n}");
    }
    buf.resize(n, 0);
    rd.read_exact(&mut buf[..n]).await?;
    Ok(n)
}

impl LinkWriter {
    pub async fn send(&mut self, frame: &Frame) -> Result<()> {
        self.send_bytes(&wire::encode(frame)?).await
    }

    async fn send_bytes(&mut self, plain: &[u8]) -> Result<()> {
        if plain.len() > 65535 - 16 {
            bail!("frame too large ({} bytes)", plain.len());
        }
        self.buf.resize(plain.len() + 16, 0);
        let n = self
            .st
            .write_message(self.nonce, plain, &mut self.buf)
            .map_err(|e| anyhow!("encrypt: {e}"))?;
        self.nonce += 1;
        let (wr, buf) = (&mut self.wr, &self.buf[..n]);
        write_raw(wr, buf).await
    }

    pub async fn shutdown(&mut self) {
        let _ = self.wr.shutdown().await;
    }
}

impl LinkReader {
    pub async fn recv(&mut self) -> Result<Frame> {
        let b = self.recv_bytes().await?;
        wire::decode(&b)
    }

    async fn recv_bytes(&mut self) -> Result<Vec<u8>> {
        let n = read_raw(&mut self.rd, &mut self.buf).await?;
        self.out.resize(n, 0);
        let m = self
            .st
            .read_message(self.nonce, &self.buf[..n], &mut self.out)
            .map_err(|e| anyhow!("decrypt: {e}"))?;
        self.nonce += 1;
        Ok(self.out[..m].to_vec())
    }
}

/// Run the Noise XX handshake plus Hello exchange.
pub async fn establish(stream: TcpStream, identity: &Identity, hello: Hello, initiator: bool) -> Result<Link> {
    stream.set_nodelay(true).ok();
    let remote_addr = stream.peer_addr()?;
    establish_over(stream, remote_addr, false, identity, hello, initiator).await
}

/// Same handshake over any byte stream (used for relay sessions).
pub async fn establish_over<S>(stream: S, remote_addr: std::net::SocketAddr, via_relay: bool, identity: &Identity, hello: Hello, initiator: bool) -> Result<Link>
where
    S: AsyncRead + AsyncWrite + Send + Unpin + 'static,
{
    tokio::time::timeout(HANDSHAKE_TIMEOUT, establish_inner(stream, remote_addr, via_relay, identity, hello, initiator))
        .await
        .map_err(|_| anyhow!("handshake timeout"))?
}

async fn establish_inner<S>(mut stream: S, remote_addr: std::net::SocketAddr, via_relay: bool, identity: &Identity, hello: Hello, initiator: bool) -> Result<Link>
where
    S: AsyncRead + AsyncWrite + Send + Unpin + 'static,
{
    let kp = identity.snow_private();
    let mut buf = vec![0u8; 65535];
    let mut raw = Vec::new();
    let hs = if initiator {
        let mut hs = build_initiator(&kp)?;
        let n = hs.write_message(&[], &mut buf).map_err(|e| anyhow!("hs1: {e}"))?;
        write_raw(&mut stream, &buf[..n]).await?;
        let m = read_raw(&mut stream, &mut raw).await?;
        hs.read_message(&raw[..m], &mut buf).map_err(|e| anyhow!("hs2: {e}"))?;
        let n = hs.write_message(&[], &mut buf).map_err(|e| anyhow!("hs3: {e}"))?;
        write_raw(&mut stream, &buf[..n]).await?;
        hs
    } else {
        let mut hs = build_responder(&kp)?;
        let m = read_raw(&mut stream, &mut raw).await?;
        hs.read_message(&raw[..m], &mut buf).map_err(|e| anyhow!("hs1: {e}"))?;
        let n = hs.write_message(&[], &mut buf).map_err(|e| anyhow!("hs2: {e}"))?;
        write_raw(&mut stream, &buf[..n]).await?;
        let m = read_raw(&mut stream, &mut raw).await?;
        hs.read_message(&raw[..m], &mut buf).map_err(|e| anyhow!("hs3: {e}"))?;
        hs
    };
    let rs = hs.get_remote_static().context("no remote static key")?;
    let mut pk = [0u8; 32];
    pk.copy_from_slice(rs);
    let remote_fp = Fingerprint::from_x25519(&pk);
    if remote_fp == *identity.fingerprint() {
        bail!("connected to self");
    }
    let st = Arc::new(hs.into_stateless_transport_mode().map_err(|e| anyhow!("transport: {e}"))?);
    let (rd, wr) = tokio::io::split(stream);
    let (rd, wr): (RdHalf, WrHalf) = (Box::new(rd), Box::new(wr));
    let mut writer = LinkWriter { wr, st: st.clone(), nonce: 0, buf: Vec::new() };
    let mut reader = LinkReader { rd, st, nonce: 0, buf: Vec::new(), out: Vec::new() };
    writer.send_bytes(&wire::encode(&hello)?).await?;
    let remote_hello: Hello = wire::decode(&reader.recv_bytes().await?)?;
    if remote_hello.proto != wire::PROTO_VERSION {
        bail!("peer speaks protocol v{}, we speak v{}", remote_hello.proto, wire::PROTO_VERSION);
    }
    Ok(Link { reader, writer, remote_fp, remote_hello: clean_hello(remote_hello), remote_addr, via_relay })
}

/// Remote display name sanitised.
pub fn clean_hello(mut h: Hello) -> Hello {
    h.name = crate::util::sanitize_display_name(&h.name);
    h
}
