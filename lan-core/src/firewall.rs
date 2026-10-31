//! Windows Firewall integration.
//!
//! v0.3 never created a firewall rule, so on Windows inbound UDP 41234 / TCP 41235 were
//! blocked unless the user answered the one-off "allow access" prompt correctly (and on a
//! *Public* network that prompt defaults to deny). Result: users had to disable the firewall.
//! We now add explicit inbound allow rules for the daemon executable via elevated netsh.

use serde::{Deserialize, Serialize};

/// Printed instead of changing anything when the host firewall is off.
pub const FW_DISABLED_MSG: &str = "本机防火墙未启用，不做修改";
pub const RULE_TCP: &str = "LAN Secure Messenger (TCP-In)";
pub const RULE_UDP: &str = "LAN Secure Messenger (UDP-In)";

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct NetProfile {
    pub name: String,
    pub interface: String,
    /// "Public" | "Private" | "DomainAuthenticated"
    pub category: String,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct FirewallStatus {
    pub supported: bool,
    pub tcp_rule: bool,
    pub udp_rule: bool,
    /// Rule exists but points at a different exe path (e.g. the folder was moved).
    pub rule_path_mismatch: bool,
    pub rule_profiles: String,
    pub exe: String,
    pub profiles: Vec<NetProfile>,
    pub public_network: bool,
    pub advice: String,
}

#[cfg(windows)]
mod imp {
    use super::*;
    use std::os::windows::process::CommandExt;
    use std::process::Command;
    const CREATE_NO_WINDOW: u32 = 0x0800_0000;

    fn run(cmd: &str, args: &[&str]) -> (bool, String) {
        match Command::new(cmd).args(args).creation_flags(CREATE_NO_WINDOW).output() {
            Ok(o) => {
                let mut s = String::from_utf8_lossy(&o.stdout).to_string();
                s.push_str(&String::from_utf8_lossy(&o.stderr));
                (o.status.success(), s)
            }
            Err(e) => (false, e.to_string()),
        }
    }

    pub fn exe_path() -> String {
        std::env::current_exe().map(|p| p.display().to_string()).unwrap_or_default()
    }

    fn rule(name: &str, exe: &str) -> (bool, bool, String) {
        let (ok, out) = run("netsh", &["advfirewall", "firewall", "show", "rule", &format!("name={name}"), "verbose"]);
        // Real netsh exits 1 when no rule matches; also require the (ASCII) rule name in the output.
        if !ok || !out.contains(name) {
            return (false, false, String::new());
        }
        // netsh prints in the OEM code page, so only compare ASCII paths reliably.
        let mismatch = exe.is_ascii() && !out.to_lowercase().contains(&exe.to_lowercase());
        // "Profiles:  Domain,Private" / "配置文件: 域,专用" — localized; report raw line.
        let prof = out
            .lines()
            .find(|l| {
                let l = l.to_lowercase();
                l.starts_with("profiles") || l.starts_with("配置文件")
            })
            .map(|l| l.split(':').nth(1).unwrap_or("").trim().to_string())
            .unwrap_or_default();
        (true, mismatch, prof)
    }

    pub fn profiles() -> Vec<NetProfile> {
        let ps = "Get-NetConnectionProfile | ForEach-Object { [pscustomobject]@{ name=[string]$_.Name; interface=[string]$_.InterfaceAlias; category=[string]$_.NetworkCategory } } | ConvertTo-Json -Compress";
        let (ok, out) = run("powershell", &["-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-Command", ps]);
        if !ok {
            return vec![];
        }
        let t = out.trim();
        if t.starts_with('[') {
            serde_json::from_str(t).unwrap_or_default()
        } else {
            serde_json::from_str::<NetProfile>(t).map(|p| vec![p]).unwrap_or_default()
        }
    }

    pub fn status() -> FirewallStatus {
        let exe = exe_path();
        let (tcp, m1, prof) = rule(RULE_TCP, &exe);
        let (udp, m2, _) = rule(RULE_UDP, &exe);
        let profiles = profiles();
        let public_network = profiles.iter().any(|p| p.category.eq_ignore_ascii_case("Public"));
        let mut advice = String::new();
        if !(tcp && udp) {
            advice.push_str("未找到防火墙放行规则：请在“设置/诊断”中点击“一键配置防火墙”（需要管理员确认一次）。");
        } else if m1 || m2 {
            advice.push_str("防火墙规则指向的程序路径与当前不同（程序被移动过），请重新配置防火墙。");
        }
        if public_network {
            advice.push_str("当前网络被 Windows 识别为“公用网络”，默认规则只对专用/域网络生效。建议在 Windows 设置 → 网络和 Internet 中把此网络改为“专用网络”，或在配置防火墙时勾选“也允许公用网络”。");
        }
        FirewallStatus { supported: true, tcp_rule: tcp, udp_rule: udp, rule_path_mismatch: m1 || m2, rule_profiles: prof, exe, profiles, public_network, advice }
    }

    /// Some(false) when every Windows Firewall profile is off (then we never touch it), None if unknown.
    /// Uses Get-NetFirewallProfile (Enabled is a boolean, not localized like netsh output).
    pub fn host_firewall_enabled() -> Option<bool> {
        let (ok, out) = run("powershell", &["-NoProfile", "-NonInteractive", "-Command", "@(Get-NetFirewallProfile | Where-Object { $_.Enabled }).Count"]);
        if !ok { return None; }
        out.trim().parse::<u32>().ok().map(|n| n > 0)
    }

    pub fn is_elevated() -> bool {
        unsafe { windows_sys::Win32::UI::Shell::IsUserAnAdmin() != 0 }
    }

    /// Add rules directly (must already be elevated).
    pub fn apply_rules(exe: &str, include_public: bool) -> anyhow::Result<()> {
        let profile = if include_public { "any" } else { "private,domain" };
        for (name, proto) in [(RULE_TCP, "TCP"), (RULE_UDP, "UDP")] {
            let _ = run("netsh", &["advfirewall", "firewall", "delete", "rule", &format!("name={name}")]);
            let (ok, out) = run(
                "netsh",
                &[
                    "advfirewall", "firewall", "add", "rule",
                    &format!("name={name}"), "dir=in", "action=allow",
                    &format!("program={exe}"), "enable=yes",
                    &format!("profile={profile}"), &format!("protocol={proto}"),
                    "description=LAN Secure Messenger peer-to-peer (discovery + encrypted chat)",
                ],
            );
            anyhow::ensure!(ok, "netsh add rule {proto} failed: {}", out.trim());
        }
        Ok(())
    }

    /// Re-launch ourselves elevated (one UAC prompt) to run `firewall-setup --elevated`.
    pub fn setup_elevated(include_public: bool) -> anyhow::Result<()> {
        use windows_sys::Win32::Foundation::{CloseHandle, WAIT_OBJECT_0};
        use windows_sys::Win32::System::Threading::{GetExitCodeProcess, WaitForSingleObject};
        use windows_sys::Win32::UI::Shell::{ShellExecuteExW, SEE_MASK_NOCLOSEPROCESS, SHELLEXECUTEINFOW};
        let exe = exe_path();
        if is_elevated() {
            return apply_rules(&exe, include_public);
        }
        let wide = |s: &str| s.encode_utf16().chain(std::iter::once(0)).collect::<Vec<u16>>();
        let verb = wide("runas");
        let file = wide(&exe);
        let params = wide(&format!("firewall-setup --elevated{}", if include_public { " --public" } else { "" }));
        let mut info: SHELLEXECUTEINFOW = unsafe { std::mem::zeroed() };
        info.cbSize = std::mem::size_of::<SHELLEXECUTEINFOW>() as u32;
        info.fMask = SEE_MASK_NOCLOSEPROCESS;
        info.lpVerb = verb.as_ptr();
        info.lpFile = file.as_ptr();
        info.lpParameters = params.as_ptr();
        info.nShow = 0; // SW_HIDE
        let ok = unsafe { ShellExecuteExW(&mut info) };
        anyhow::ensure!(ok != 0, "用户取消了管理员授权 (UAC) 或启动失败");
        let h = info.hProcess;
        anyhow::ensure!(!h.is_null(), "no process handle");
        let mut code: u32 = 1;
        unsafe {
            if WaitForSingleObject(h, 120_000) == WAIT_OBJECT_0 {
                GetExitCodeProcess(h, &mut code);
            }
            CloseHandle(h);
        }
        anyhow::ensure!(code == 0, "firewall-setup exited with code {code}");
        Ok(())
    }
}

#[cfg(not(windows))]
mod imp {
    use super::*;
    pub fn status() -> FirewallStatus {
        FirewallStatus {
            supported: false,
            advice: "非 Windows 系统：请确保本机防火墙放行 UDP 发现端口与 TCP 监听端口（如 ufw allow 41234/udp; ufw allow 41235/tcp）。".into(),
            exe: std::env::current_exe().map(|p| p.display().to_string()).unwrap_or_default(),
            ..Default::default()
        }
    }
    pub fn host_firewall_enabled() -> Option<bool> {
        None
    }
    pub fn is_elevated() -> bool {
        false
    }
    pub fn apply_rules(_exe: &str, _p: bool) -> anyhow::Result<()> {
        anyhow::bail!("firewall setup is only implemented on Windows")
    }
    pub fn setup_elevated(_p: bool) -> anyhow::Result<()> {
        anyhow::bail!("firewall setup is only implemented on Windows")
    }
    pub fn exe_path() -> String {
        std::env::current_exe().map(|p| p.display().to_string()).unwrap_or_default()
    }
}

pub use imp::*;
