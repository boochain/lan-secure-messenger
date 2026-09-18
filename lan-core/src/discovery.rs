//! UDP multicast/broadcast discovery with signed announcements.

use crate::identity::{Fingerprint, Identity};
use crate::protocol::DiscoveryAnnounce;
use crate::{PROTOCOL_MAGIC, PROTOCOL_VERSION};
use anyhow::{anyhow, Context, Result};
use ed25519_dalek::VerifyingKey;
use rand::RngCore;
use std::net::{Ipv4Addr, SocketAddr, SocketAddrV4};
use std::time::{SystemTime, UNIX_EPOCH};
use tokio::net::UdpSocket;
use tracing::{debug, warn};

/// Default LAN multicast group for discovery.
pub const DEFAULT_MULTICAST: Ipv4Addr = Ipv4Addr::new(239, 255, 42, 99);

const MAX_SKEW_MS: i64 = 60_000;
const NONCE_CACHE_MAX: usize = 2048;

pub struct DiscoveryService {
    sock: UdpSocket,
    /// Where we send announces (multicast or override).
    destinations: Vec<SocketAddr>,
}

impl DiscoveryService {
    pub async fn bind(
        bind_addr: &str,
        discovery_port: u16,
        broadcast_override: Option<&str>,
    ) -> Result<Self> {
        let bind_ip: Ipv4Addr = bind_addr
            .parse()
            .with_context(|| format!("bind_addr must be IPv4: {bind_addr}"))?;
        let bind = SocketAddr::V4(SocketAddrV4::new(bind_ip, discovery_port));

        let std_sock = socket2::Socket::new(
            socket2::Domain::IPV4,
            socket2::Type::DGRAM,
            Some(socket2::Protocol::UDP),
        )?;
        // SO_REUSEADDR (not REUSEPORT): multiple processes can join the same
        // multicast group:port and each receives a copy of every datagram.
        std_sock.set_reuse_address(true)?;
        std_sock.set_broadcast(true)?;
        std_sock.bind(&bind.into())?;
        std_sock.set_nonblocking(true)?;

        let sock = UdpSocket::from_std(std_sock.into())?;

        let mut destinations = Vec::new();

        if let Some(addr) = broadcast_override {
            // Smoke-test / custom: send to override host:discovery_port
            // Also join multicast so real LAN still works when override is unset path.
            let dest: SocketAddr = format!("{addr}:{discovery_port}")
                .parse()
                .context("parse broadcast override")?;
            destinations.push(dest);
            // For loopback testing, also try sending to the same port on localhost
            // Multiple processes with REUSEPORT may not all get the packet, so we
            // additionally support env-style multi-dest via comma-separated override.
            for part in addr.split(',') {
                let part = part.trim();
                if part.is_empty() {
                    continue;
                }
                // host or host:port
                let dest: SocketAddr = if part.contains(':') {
                    part.parse().with_context(|| format!("parse dest {part}"))?
                } else {
                    format!("{part}:{discovery_port}")
                        .parse()
                        .with_context(|| format!("parse dest host {part}"))?
                };
                if !destinations.contains(&dest) {
                    destinations.push(dest);
                }
            }
        } else {
            // Production: multicast + limited broadcast
            let iface = if bind_ip.is_unspecified() {
                Ipv4Addr::UNSPECIFIED
            } else {
                bind_ip
            };
            sock.join_multicast_v4(DEFAULT_MULTICAST, iface)
                .context("join multicast")?;
            destinations.push(SocketAddr::V4(SocketAddrV4::new(
                DEFAULT_MULTICAST,
                discovery_port,
            )));
            destinations.push(SocketAddr::V4(SocketAddrV4::new(
                Ipv4Addr::BROADCAST,
                discovery_port,
            )));
        }

        // Always join multicast when binding unspecified/LAN so we receive peers
        if broadcast_override.is_none() || bind_ip.is_unspecified() {
            let iface = if bind_ip.is_unspecified() {
                Ipv4Addr::UNSPECIFIED
            } else {
                bind_ip
            };
            let _ = sock.join_multicast_v4(DEFAULT_MULTICAST, iface);
        }

        Ok(Self { sock, destinations })
    }

    pub fn local_addr(&self) -> Result<SocketAddr> {
        Ok(self.sock.local_addr()?)
    }

    pub async fn announce(
        &self,
        identity: &Identity,
        display_name: &str,
        listen_port: u16,
    ) -> Result<()> {
        let mut nonce = [0u8; 16];
        rand::thread_rng().fill_bytes(&mut nonce);
        let ts_ms = now_ms();
        let mut ann = DiscoveryAnnounce {
            magic: PROTOCOL_MAGIC,
            version: PROTOCOL_VERSION,
            display_name: display_name.to_string(),
            x25519_pk: identity.x25519_public_bytes(),
            ed25519_pk: identity.ed25519_public_bytes(),
            listen_port,
            ts_ms,
            nonce,
            sig: vec![0u8; 64],
        };
        let bytes = ann.sign_bytes();
        ann.sig = identity.sign(&bytes).to_vec();
        let payload = bincode::serialize(&ann)?;
        for dest in &self.destinations {
            match self.sock.send_to(&payload, dest).await {
                Ok(_) => debug!(to = %dest, "discovery announce sent"),
                Err(e) => warn!(to = %dest, error = %e, "announce send failed"),
            }
        }
        Ok(())
    }

    pub async fn recv(&self, buf: &mut [u8]) -> Result<(usize, SocketAddr)> {
        let (n, addr) = self.sock.recv_from(buf).await?;
        Ok((n, addr))
    }
}

pub struct AnnounceValidator {
    seen_nonces: std::collections::HashSet<[u8; 16]>,
    nonce_order: std::collections::VecDeque<[u8; 16]>,
}

impl Default for AnnounceValidator {
    fn default() -> Self {
        Self {
            seen_nonces: Default::default(),
            nonce_order: Default::default(),
        }
    }
}

impl AnnounceValidator {
    pub fn validate(
        &mut self,
        data: &[u8],
        from: SocketAddr,
        self_fp: &Fingerprint,
    ) -> Result<ValidatedPeer> {
        let ann: DiscoveryAnnounce = bincode::deserialize(data).context("decode announce")?;
        if ann.magic != PROTOCOL_MAGIC {
            return Err(anyhow!("bad magic"));
        }
        if ann.version != PROTOCOL_VERSION {
            return Err(anyhow!("unsupported version {}", ann.version));
        }
        let now = now_ms();
        if (now - ann.ts_ms).abs() > MAX_SKEW_MS {
            return Err(anyhow!("timestamp skew too large"));
        }
        if self.seen_nonces.contains(&ann.nonce) {
            return Err(anyhow!("replayed nonce"));
        }
        let vk = VerifyingKey::from_bytes(&ann.ed25519_pk)
            .map_err(|e| anyhow!("bad ed25519 pk: {e}"))?;
        let msg = ann.sign_bytes();
        anyhow::ensure!(ann.sig.len() == 64, "bad sig length");
        let mut sig = [0u8; 64];
        sig.copy_from_slice(&ann.sig);
        if !Identity::verify(&vk, &msg, &sig) {
            return Err(anyhow!("invalid discovery signature from {from}"));
        }
        let fp = Fingerprint::from_x25519(&ann.x25519_pk);
        if &fp == self_fp {
            return Err(anyhow!("self announce"));
        }

        self.seen_nonces.insert(ann.nonce);
        self.nonce_order.push_back(ann.nonce);
        while self.nonce_order.len() > NONCE_CACHE_MAX {
            if let Some(old) = self.nonce_order.pop_front() {
                self.seen_nonces.remove(&old);
            }
        }

        let peer_addr = SocketAddr::new(from.ip(), ann.listen_port);
        Ok(ValidatedPeer {
            fingerprint: fp,
            display_name: ann.display_name,
            x25519_pk: ann.x25519_pk,
            ed25519_pk: ann.ed25519_pk,
            addr: peer_addr,
            listen_port: ann.listen_port,
        })
    }
}

#[derive(Debug, Clone)]
pub struct ValidatedPeer {
    pub fingerprint: Fingerprint,
    pub display_name: String,
    pub x25519_pk: [u8; 32],
    pub ed25519_pk: [u8; 32],
    pub addr: SocketAddr,
    pub listen_port: u16,
}

fn now_ms() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_millis() as i64)
        .unwrap_or(0)
}

pub fn warn_invalid(err: anyhow::Error) {
    warn!(error = %err, "ignored discovery packet");
}
