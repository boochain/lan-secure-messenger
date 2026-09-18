//! Encrypted TCP sessions using Noise XX.

use crate::crypto::{
    build_initiator, build_responder, handshake_read, handshake_write, into_transport,
    transport_decrypt, transport_encrypt,
};
use crate::identity::{Fingerprint, Identity};
use crate::protocol::{encode_frame, WireMessage};
use anyhow::{anyhow, Context, Result};
use snow::TransportState;
use std::net::SocketAddr;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::TcpStream;
use tracing::debug;

const HANDSHAKE_TIMEOUT: std::time::Duration = std::time::Duration::from_secs(12);
const CONNECT_TIMEOUT: std::time::Duration = std::time::Duration::from_secs(8);

pub struct SecureSession {
    pub peer_fp: Fingerprint,
    pub peer_x25519: [u8; 32],
    transport: TransportState,
    stream: TcpStream,
    peer_addr: SocketAddr,
}

impl SecureSession {
    /// Dial peer and complete Noise XX as initiator.
    pub async fn connect(identity: &Identity, addr: SocketAddr) -> Result<Self> {
        let mut stream = tokio::time::timeout(CONNECT_TIMEOUT, TcpStream::connect(addr))
            .await
            .map_err(|_| anyhow!("connect timeout to {addr}"))?
            .with_context(|| format!("connect {addr}"))?;
        stream.set_nodelay(true)?;
        let kp = identity.snow_private();
        let mut hs = build_initiator(&kp)?;

        // -> e
        let msg1 = handshake_write(&mut hs, &[])?;
        write_frame(&mut stream, &msg1).await?;

        // <- e, ee, s, es
        let msg2 = read_frame_timeout(&mut stream, HANDSHAKE_TIMEOUT).await?;
        handshake_read(&mut hs, &msg2)?;

        // -> s, se
        let msg3 = handshake_write(&mut hs, &[])?;
        write_frame(&mut stream, &msg3).await?;

        let peer_static = hs
            .get_remote_static()
            .ok_or_else(|| anyhow!("no remote static"))?
            .to_vec();
        anyhow::ensure!(peer_static.len() == 32, "bad remote static len");
        let mut peer_x25519 = [0u8; 32];
        peer_x25519.copy_from_slice(&peer_static);
        let peer_fp = Fingerprint::from_x25519(&peer_x25519);

        let transport = into_transport(hs)?;
        debug!(peer = %peer_fp, %addr, "session established (initiator)");
        Ok(Self {
            peer_fp,
            peer_x25519,
            transport,
            stream,
            peer_addr: addr,
        })
    }

    /// Accept incoming TCP and complete Noise XX as responder.
    pub async fn accept(identity: &Identity, mut stream: TcpStream, peer_addr: SocketAddr) -> Result<Self> {
        stream.set_nodelay(true)?;
        let kp = identity.snow_private();
        let mut hs = build_responder(&kp)?;

        // <- e
        let msg1 = read_frame_timeout(&mut stream, HANDSHAKE_TIMEOUT).await?;
        handshake_read(&mut hs, &msg1)?;

        // -> e, ee, s, es
        let msg2 = handshake_write(&mut hs, &[])?;
        write_frame(&mut stream, &msg2).await?;

        // <- s, se
        let msg3 = read_frame_timeout(&mut stream, HANDSHAKE_TIMEOUT).await?;
        handshake_read(&mut hs, &msg3)?;

        let peer_static = hs
            .get_remote_static()
            .ok_or_else(|| anyhow!("no remote static"))?
            .to_vec();
        anyhow::ensure!(peer_static.len() == 32, "bad remote static len");
        let mut peer_x25519 = [0u8; 32];
        peer_x25519.copy_from_slice(&peer_static);
        let peer_fp = Fingerprint::from_x25519(&peer_x25519);

        let transport = into_transport(hs)?;
        debug!(peer = %peer_fp, %peer_addr, "session established (responder)");
        Ok(Self {
            peer_fp,
            peer_x25519,
            transport,
            stream,
            peer_addr,
        })
    }

    pub fn peer_addr(&self) -> SocketAddr {
        self.peer_addr
    }

    pub async fn send_msg(&mut self, msg: &WireMessage) -> Result<()> {
        let plain = bincode::serialize(msg)?;
        let ct = transport_encrypt(&mut self.transport, &plain)?;
        write_frame(&mut self.stream, &ct).await
    }

    pub async fn recv_msg(&mut self) -> Result<WireMessage> {
        let ct = read_frame(&mut self.stream).await?;
        let plain = transport_decrypt(&mut self.transport, &ct)?;
        let msg: WireMessage = bincode::deserialize(&plain)?;
        Ok(msg)
    }
}

async fn write_frame(stream: &mut TcpStream, payload: &[u8]) -> Result<()> {
    let frame = encode_frame(payload);
    stream.write_all(&frame).await?;
    Ok(())
}

async fn read_frame_timeout(stream: &mut TcpStream, timeout: std::time::Duration) -> Result<Vec<u8>> {
    tokio::time::timeout(timeout, read_frame(stream))
        .await
        .map_err(|_| anyhow!("frame read timeout"))?
}

async fn read_frame(stream: &mut TcpStream) -> Result<Vec<u8>> {
    let mut len_buf = [0u8; 4];
    stream.read_exact(&mut len_buf).await?;
    let len = u32::from_be_bytes(len_buf) as usize;
    anyhow::ensure!(len > 0 && len <= 256 * 1024, "frame too large: {len}");
    let mut buf = vec![0u8; len];
    stream.read_exact(&mut buf).await?;
    Ok(buf)
}
