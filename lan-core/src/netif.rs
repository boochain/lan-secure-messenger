//! Network interface enumeration + classification for discovery.
//!
//! v0.3 sent announces only via the OS default route (INADDR_ANY multicast +
//! 255.255.255.255). On Windows machines with Hyper-V / WSL / VPN adapters that route
//! is frequently a virtual adapter, so announces never reached the real LAN. We now
//! enumerate every IPv4 adapter and send a subnet-directed broadcast on each real one.

use serde::{Deserialize, Serialize};
use std::net::Ipv4Addr;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct NetIf {
    pub name: String,
    pub friendly_name: String,
    pub description: String,
    pub ip: Ipv4Addr,
    pub netmask: Ipv4Addr,
    pub broadcast: Ipv4Addr,
    pub up: bool,
    /// Used for discovery?
    pub used: bool,
    /// Why it was skipped (empty if used).
    pub reason: String,
    pub is_default: bool,
}

const VIRTUAL_MARKERS: &[&str] = &[
    "vethernet", "hyper-v", "virtualbox", "vbox", "vmware", "vmnet", "wsl", "docker", "tap-", "tap ",
    "tap-windows", "wintun", "wireguard", "tailscale", "zerotier", "openvpn", "vpn", "npcap loopback",
    "loopback", "pseudo", "teredo", "isatap", "bluetooth", "virbr", "veth", "br-", "cni", "flannel",
    "utun", "tun", "wg", "zt", "anyconnect", "fortinet", "hamachi", "radmin",
];

fn matches_any(hay: &str, pats: &[String]) -> bool {
    let h = hay.to_lowercase();
    pats.iter().any(|p| !p.trim().is_empty() && h.contains(&p.trim().to_lowercase()))
}

pub fn classify(name: &str, friendly: &str, desc: &str, ip: Ipv4Addr, up: bool, include: &[String], exclude: &[String]) -> (bool, String) {
    let hay = format!("{name} | {friendly} | {desc}");
    if matches_any(&hay, exclude) {
        return (false, "被设置排除".into());
    }
    if matches_any(&hay, include) {
        return (true, String::new());
    }
    if !up {
        return (false, "网卡未连接".into());
    }
    if ip.is_loopback() {
        return (false, "回环地址".into());
    }
    if ip.is_link_local() {
        return (false, "169.254 自动地址 (无 DHCP)".into());
    }
    if ip.is_unspecified() {
        return (false, "无地址".into());
    }
    let lower = hay.to_lowercase();
    for m in VIRTUAL_MARKERS {
        // Short markers must match a whole token start to avoid false positives ("tun" in "tuning").
        let hit = if m.len() <= 3 {
            name.to_lowercase().starts_with(m)
        } else {
            lower.contains(m)
        };
        if hit {
            return (false, format!("虚拟网卡 ({})", m.trim()));
        }
    }
    (true, String::new())
}

pub fn list(include: &[String], exclude: &[String]) -> Vec<NetIf> {
    let mut out = Vec::new();
    for itf in netdev::get_interfaces() {
        let friendly = itf.friendly_name.clone().unwrap_or_default();
        let desc = itf.description.clone().unwrap_or_default();
        let up = itf.is_up() && !matches!(itf.oper_state, netdev::interface::state::OperState::Down);
        for net in &itf.ipv4 {
            let ip = net.addr();
            let (mut used, mut reason) = classify(&itf.name, &friendly, &desc, ip, up, include, exclude);
            if used && itf.is_loopback() {
                used = false;
                reason = "回环网卡".into();
            }
            out.push(NetIf {
                name: itf.name.clone(),
                friendly_name: friendly.clone(),
                description: desc.clone(),
                ip,
                netmask: net.netmask(),
                broadcast: if net.prefix_len() >= 31 { Ipv4Addr::BROADCAST } else { net.broadcast() },
                up,
                used,
                reason,
                is_default: itf.default,
            });
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn classify_virtual() {
        let ip = Ipv4Addr::new(192, 168, 1, 5);
        assert!(!classify("eth1", "vEthernet (WSL)", "Hyper-V Virtual Ethernet Adapter", ip, true, &[], &[]).0);
        assert!(!classify("{GUID}", "VirtualBox Host-Only Network", "", ip, true, &[], &[]).0);
        assert!(!classify("docker0", "", "", ip, true, &[], &[]).0);
        assert!(classify("{GUID}", "以太网", "Intel(R) Ethernet Connection I219-V", ip, true, &[], &[]).0);
        assert!(classify("{GUID}", "WLAN", "Intel(R) Wi-Fi 6 AX201 160MHz", ip, true, &[], &[]).0);
        assert!(!classify("eth0", "", "", Ipv4Addr::new(169, 254, 3, 4), true, &[], &[]).0);
        assert!(classify("{GUID}", "vEthernet (Default Switch)", "", ip, true, &["Default Switch".into()], &[]).0);
        assert!(!classify("{GUID}", "WLAN", "", ip, true, &[], &["wlan".into()]).0);
        assert!(classify("enp3s0", "", "", ip, true, &[], &[]).0);
    }
}
