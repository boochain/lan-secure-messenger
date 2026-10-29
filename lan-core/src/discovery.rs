//! UDP discovery. Announces are only *hints* for where to dial; presence is decided by
//! the authenticated TCP connection (see engine). Sources:
//!  * subnet-directed broadcast on every real IPv4 interface (+ 255.255.255.255 bound per interface)
//!  * multicast 239.255.42.99 joined/sent per interface
//!  * unicast replies to announces we receive, and configured unicast targets
//! No timestamp-skew rejection (v0.3 silently dropped peers whose clock differed > 60 s).

use crate::identity::{Fingerprint, Identity};
use crate::netif::{self, NetIf};
use crate::wire::{self, Announce, SignedAnnounce, ANNOUNCE_MAGIC, PROTO_VERSION};
use ed25519_dalek::VerifyingKey;
use serde::Serialize;
use socket2::{Domain, Protocol, Socket, Type};
use std::collections::HashMap;
use std::net::{Ipv4Addr, SocketAddr, SocketAddrV4, ToSocketAddrs};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};
use tokio::net::UdpSocket;
use tokio::sync::mpsc;

pub const MULTICAST_GROUP: Ipv4Addr = Ipv4Addr::new(239, 255, 42, 99);

#[derive(Debug, Clone)]
pub struct Seen {
    pub fp: Fingerprint,
    pub name: String,
    pub ed25519_pk: [u8; 32],
    /// Where to dial (source ip + advertised TCP port).
    pub dial: SocketAddr,
    pub src: SocketAddr,
}

#[derive(Debug, Clone)]
pub struct DiscoParams {
    pub port: u16,
    pub listen: bool,
    pub multicast: bool,
    pub interval: Duration,
    pub targets: Vec<String>,
    pub include: Vec<String>,
    pub exclude: Vec<String>,
}

#[derive(Debug, Clone, Default, Serialize, serde::Deserialize)]
pub struct DiscoStatus {
    pub enabled: bool,
    pub port: u16,
    pub listening: bool,
    pub bind_error: String,
    pub multicast: bool,
    pub announces_sent: u64,
    pub announces_received: u64,
    pub announces_rejected: u64,
    pub send_errors: u64,
    pub last_send_error: String,
    pub interfaces: Vec<NetIf>,
}

pub struct AnnounceInfo {
    pub name: String,
    pub tcp_port: u16,
}

pub struct Discovery {
    pub status: Arc<Mutex<DiscoStatus>>,
    pub info: Arc<Mutex<AnnounceInfo>>,
    reply_tx: mpsc::UnboundedSender<SocketAddr>,
}

impl Discovery {
    /// Ask discovery to unicast an announce to `addr` (a peer's discovery port).
    pub fn reply_to(&self, addr: SocketAddr) {
        let _ = self.reply_tx.send(addr);
    }

    pub fn start(identity: Identity, params: DiscoParams, info: AnnounceInfo, tx: mpsc::UnboundedSender<Seen>) -> Self {
        let status = Arc::new(Mutex::new(DiscoStatus { enabled: true, port: params.port, multicast: params.multicast, ..Default::default() }));
        let info = Arc::new(Mutex::new(info));
        let (reply_tx, reply_rx) = mpsc::unbounded_channel();
        let d = Discovery { status: status.clone(), info: info.clone(), reply_tx: reply_tx.clone() };
        tokio::spawn(run(identity, params, info, status, tx, reply_rx, reply_tx));
        d
    }

    pub fn disabled() -> Self {
        let (reply_tx, _rx) = mpsc::unbounded_channel();
        Discovery {
            status: Arc::new(Mutex::new(DiscoStatus::default())),
            info: Arc::new(Mutex::new(AnnounceInfo { name: String::new(), tcp_port: 0 })),
            reply_tx,
        }
    }
}

fn build_announce(identity: &Identity, info: &Arc<Mutex<AnnounceInfo>>, disco_port: u16) -> Vec<u8> {
    let (name, tcp_port) = {
        let i = info.lock().unwrap();
        (i.name.clone(), i.tcp_port)
    };
    let a = Announce {
        proto: PROTO_VERSION,
        x25519_pk: identity.x25519_public_bytes(),
        ed25519_pk: identity.ed25519_public_bytes(),
        name,
        tcp_port,
        disco_port,
        nonce: rand::random(),
        ts_ms: chrono::Utc::now().timestamp_millis(),
        want_reply: true,
    };
    let body = wire::encode(&a).unwrap_or_default();
    let sig = identity.sign(&body).to_vec();
    let mut out = ANNOUNCE_MAGIC.to_vec();
    out.extend(wire::encode(&SignedAnnounce { body, sig }).unwrap_or_default());
    out
}

pub fn parse_announce(buf: &[u8]) -> Option<Announce> {
    if buf.len() < 4 || buf[..4] != ANNOUNCE_MAGIC {
        return None;
    }
    let sa: SignedAnnounce = wire::decode(&buf[4..]).ok()?;
    let a: Announce = wire::decode(&sa.body).ok()?;
    if a.proto != PROTO_VERSION || sa.sig.len() != 64 {
        return None;
    }
    let vk = VerifyingKey::from_bytes(&a.ed25519_pk).ok()?;
    let mut sig = [0u8; 64];
    sig.copy_from_slice(&sa.sig);
    Identity::verify(&vk, &sa.body, &sig).then_some(a)
}

fn recv_socket(port: u16) -> std::io::Result<std::net::UdpSocket> {
    let s = Socket::new(Domain::IPV4, Type::DGRAM, Some(Protocol::UDP))?;
    #[cfg(unix)]
    s.set_reuse_address(true)?;
    s.set_broadcast(true)?;
    s.bind(&SocketAddrV4::new(Ipv4Addr::UNSPECIFIED, port).into())?;
    s.set_nonblocking(true)?;
    Ok(s.into())
}

fn send_socket(ip: Ipv4Addr) -> std::io::Result<std::net::UdpSocket> {
    let s = Socket::new(Domain::IPV4, Type::DGRAM, Some(Protocol::UDP))?;
    s.set_broadcast(true)?;
    if !ip.is_unspecified() {
        let _ = s.set_multicast_if_v4(&ip);
        s.set_multicast_ttl_v4(1)?;
    }
    s.set_multicast_loop_v4(true)?;
    s.bind(&SocketAddrV4::new(ip, 0).into())?;
    s.set_nonblocking(true)?;
    Ok(s.into())
}

fn resolve_targets(targets: &[String], default_port: u16) -> Vec<SocketAddr> {
    let mut out = Vec::new();
    for t in targets {
        let t = t.trim();
        if t.is_empty() {
            continue;
        }
        let with_port = if t.contains(':') { t.to_string() } else { format!("{t}:{default_port}") };
        if let Ok(it) = with_port.to_socket_addrs() {
            out.extend(it.filter(|a| a.is_ipv4()));
        }
    }
    out
}

async fn run(
    identity: Identity,
    p: DiscoParams,
    info: Arc<Mutex<AnnounceInfo>>,
    status: Arc<Mutex<DiscoStatus>>,
    tx: mpsc::UnboundedSender<Seen>,
    mut reply_rx: mpsc::UnboundedReceiver<SocketAddr>,
    reply_tx: mpsc::UnboundedSender<SocketAddr>,
) {
    let me = identity.fingerprint().clone();
    // Receiver.
    let rsock = if p.listen {
        match recv_socket(p.port).and_then(UdpSocket::from_std) {
            Ok(s) => {
                status.lock().unwrap().listening = true;
                Some(Arc::new(s))
            }
            Err(e) => {
                tracing::warn!(port = p.port, error = %e, "discovery bind failed; relying on manual add / redial");
                status.lock().unwrap().bind_error = format!("UDP {} 绑定失败: {e}", p.port);
                None
            }
        }
    } else {
        None
    };
    if let Some(rs) = rsock.clone() {
        let status = status.clone();
        let tx = tx.clone();
        let me = me.clone();
        tokio::spawn(async move {
            let mut buf = vec![0u8; 2048];
            let mut last_reply: HashMap<std::net::IpAddr, Instant> = HashMap::new();
            loop {
                let Ok((n, src)) = rs.recv_from(&mut buf).await else {
                    tokio::time::sleep(Duration::from_millis(200)).await;
                    continue;
                };
                let Some(a) = parse_announce(&buf[..n]) else {
                    status.lock().unwrap().announces_rejected += 1;
                    continue;
                };
                let fp = Fingerprint::from_x25519(&a.x25519_pk);
                if fp == me {
                    continue;
                }
                status.lock().unwrap().announces_received += 1;
                let reply_due = a.want_reply && last_reply.get(&src.ip()).map(|t| t.elapsed() > Duration::from_secs(10)).unwrap_or(true);
                if reply_due && a.disco_port != 0 {
                    last_reply.insert(src.ip(), Instant::now());
                    let _ = reply_tx.send(SocketAddr::new(src.ip(), a.disco_port));
                }
                let _ = tx.send(Seen {
                    fp,
                    name: crate::util::sanitize_display_name(&a.name),
                    ed25519_pk: a.ed25519_pk,
                    dial: SocketAddr::new(src.ip(), a.tcp_port),
                    src,
                });
            }
        });
    }

    let mut ifaces: Vec<NetIf> = Vec::new();
    let mut socks: HashMap<Ipv4Addr, UdpSocket> = HashMap::new();
    let mut joined: Vec<Ipv4Addr> = Vec::new();
    let mut last_enum = Instant::now() - Duration::from_secs(3600);
    let fallback = send_socket(Ipv4Addr::UNSPECIFIED).and_then(UdpSocket::from_std).ok();
    let mut tick = tokio::time::interval(p.interval);
    let targets_every = 1u64;
    let mut n_tick: u64 = 0;
    loop {
        tokio::select! {
            _ = tick.tick() => {}
            Some(addr) = reply_rx.recv() => {
                let pkt = build_announce(&identity, &info, p.port);
                if let Some(s) = &fallback {
                    if s.send_to(&pkt, addr).await.is_ok() {
                        status.lock().unwrap().announces_sent += 1;
                    }
                }
                continue;
            }
        }
        n_tick += 1;
        if last_enum.elapsed() > Duration::from_secs(20) {
            last_enum = Instant::now();
            let now_ifaces = netif::list(&p.include, &p.exclude);
            let used: Vec<Ipv4Addr> = now_ifaces.iter().filter(|i| i.used).map(|i| i.ip).collect();
            socks.retain(|ip, _| used.contains(ip));
            for ip in &used {
                if !socks.contains_key(ip) {
                    match send_socket(*ip).and_then(UdpSocket::from_std) {
                        Ok(s) => {
                            socks.insert(*ip, s);
                        }
                        Err(e) => tracing::warn!(%ip, error = %e, "cannot bind discovery sender"),
                    }
                }
                if p.multicast && !joined.contains(ip) {
                    if let Some(rs) = &rsock {
                        match rs.join_multicast_v4(MULTICAST_GROUP, *ip) {
                            Ok(()) => joined.push(*ip),
                            Err(e) => tracing::debug!(%ip, error = %e, "multicast join failed"),
                        }
                    }
                }
            }
            if used.is_empty() {
                tracing::warn!("no usable LAN interface found for discovery");
            }
            if format!("{:?}", now_ifaces.iter().map(|i| (&i.name, i.ip, i.used)).collect::<Vec<_>>())
                != format!("{:?}", ifaces.iter().map(|i| (&i.name, i.ip, i.used)).collect::<Vec<_>>())
            {
                for i in &now_ifaces {
                    tracing::info!(name = %i.name, friendly = %i.friendly_name, ip = %i.ip, bcast = %i.broadcast, used = i.used, reason = %i.reason, "interface");
                }
            }
            ifaces = now_ifaces;
            status.lock().unwrap().interfaces = ifaces.clone();
        }
        let pkt = build_announce(&identity, &info, p.port);
        let mut sent = 0u64;
        let mut errs = 0u64;
        let mut last_err = String::new();
        for itf in ifaces.iter().filter(|i| i.used) {
            let Some(s) = socks.get(&itf.ip) else { continue };
            let mut dests = vec![SocketAddr::from((itf.broadcast, p.port))];
            if itf.broadcast != Ipv4Addr::BROADCAST {
                dests.push(SocketAddr::from((Ipv4Addr::BROADCAST, p.port)));
            }
            if p.multicast {
                dests.push(SocketAddr::from((MULTICAST_GROUP, p.port)));
            }
            for d in dests {
                match s.send_to(&pkt, d).await {
                    Ok(_) => sent += 1,
                    Err(e) => {
                        errs += 1;
                        last_err = format!("{} -> {d}: {e}", itf.ip);
                    }
                }
            }
        }
        if n_tick % targets_every == 0 {
            if let Some(s) = &fallback {
                for t in resolve_targets(&p.targets, p.port) {
                    match s.send_to(&pkt, t).await {
                        Ok(_) => sent += 1,
                        Err(e) => {
                            errs += 1;
                            last_err = format!("-> {t}: {e}");
                        }
                    }
                }
            }
        }
        let mut st = status.lock().unwrap();
        st.announces_sent += sent;
        st.send_errors += errs;
        if !last_err.is_empty() {
            st.last_send_error = last_err;
        }
    }
}
