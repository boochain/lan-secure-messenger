//! lan-msg: command-line client for lan-daemon (JSON-RPC over local IPC).

use anyhow::{bail, Context, Result};
use clap::{Parser, Subcommand};
use lan_core::ipc::{default_endpoint, IpcClient};
use serde_json::{json, Value};
use std::time::{Duration, Instant};

#[derive(Parser)]
#[command(name = "lan-msg", version, about = "LAN Secure Messenger CLI (needs lan-daemon running)")]
struct Cli {
    #[arg(long, env = "LSM_IPC", global = true)]
    ipc: Option<String>,
    /// Print raw JSON results
    #[arg(long, global = true)]
    json: bool,
    #[command(subcommand)]
    cmd: Cmd,
}

#[derive(Subcommand)]
enum Cmd {
    /// Who am I + peers overview
    Status,
    /// List peers
    Peers,
    /// Send a direct message: lan-msg send <peer> <text...>
    Send { peer: String, text: Vec<String> },
    /// Send to a group
    SendGroup { group: String, text: Vec<String> },
    /// Send a file to a peer or group (prefix group with g:)
    SendFile { target: String, path: String },
    /// Show conversation history (peer, or g:<group>)
    History {
        target: String,
        #[arg(long, default_value_t = 50)]
        limit: usize,
    },
    /// Add a peer manually by IP[:port] (persisted, redialed automatically)
    Add { address: String },
    Trust { peer: String },
    Untrust { peer: String },
    Block { peer: String },
    Forget { peer: String },
    Mute { target: String, #[arg(long)] off: bool },
    Groups,
    GroupCreate { name: String, members: Vec<String> },
    GroupInvite { group: String, members: Vec<String> },
    GroupRemove { group: String, peer: String },
    GroupLeave { group: String },
    Transfers,
    Cancel { id: String },
    Retry { id: String },
    Search { query: Vec<String> },
    Backup { path: String, #[arg(long)] password: String },
    Restore { path: String, #[arg(long)] password: String },
    /// Show a 6-digit PIN for the other side to enter
    PinOffer,
    /// Enter the PIN shown by <peer>
    PinPair { peer: String, pin: String },
    /// Set display name
    Name { name: Vec<String> },
    /// Relay server fallback: `relay on <host> <port> <passphrase>` | `relay off` | `relay status`
    Relay { action: String, host: Option<String>, port: Option<u16>, passphrase: Option<String> },
    /// Network diagnostics
    Diag,
    /// Add Windows Firewall rules (UAC prompt)
    FirewallSetup { #[arg(long)] public: bool },
    /// Stream events
    Watch,
    /// Wait until a peer is online (exit 1 on timeout)
    WaitPeer { peer: String, #[arg(long, default_value_t = 30)] timeout: u64 },
    /// Wait until a message is delivered (by id)
    WaitDelivered { id: String, #[arg(long, default_value_t = 30)] timeout: u64 },
    /// Raw JSON-RPC call
    Raw { method: String, params: Option<String> },
    /// Stop the daemon
    Shutdown,
}

struct Ctx {
    c: IpcClient,
    json: bool,
}

impl Ctx {
    async fn call(&mut self, m: &str, p: Value) -> Result<Value> {
        self.c.call(m, p).await
    }
    fn out(&self, v: &Value) {
        println!("{}", serde_json::to_string_pretty(v).unwrap_or_default());
    }

    async fn snapshot(&mut self) -> Result<Value> {
        self.call("get_snapshot", json!({})).await
    }

    async fn resolve_peer(&mut self, q: &str) -> Result<String> {
        let s = self.snapshot().await?;
        let ql = q.to_lowercase().replace('-', "");
        let peers = s["peers"].as_array().cloned().unwrap_or_default();
        let hits: Vec<&Value> = peers
            .iter()
            .filter(|p| {
                let fp = p["fingerprint"].as_str().unwrap_or("").to_lowercase().replace('-', "");
                fp.starts_with(&ql) || p["name"].as_str().unwrap_or("").eq_ignore_ascii_case(q)
            })
            .collect();
        match hits.len() {
            1 => hits[0]["fingerprint"].as_str().map(str::to_string).ok_or_else(|| anyhow::anyhow!("peer entry without fingerprint")),
            0 => bail!("no peer matches '{q}'"),
            _ => bail!("'{q}' is ambiguous ({} matches)", hits.len()),
        }
    }

    async fn resolve_group(&mut self, q: &str) -> Result<String> {
        let q = q.strip_prefix("g:").unwrap_or(q);
        let s = self.snapshot().await?;
        let gs = s["groups"].as_array().cloned().unwrap_or_default();
        let hits: Vec<&Value> = gs.iter().filter(|g| g["id"].as_str().unwrap_or("").starts_with(q) || g["name"].as_str() == Some(q)).collect();
        match hits.len() {
            1 => hits[0]["id"].as_str().map(str::to_string).ok_or_else(|| anyhow::anyhow!("group entry without id")),
            0 => bail!("no group matches '{q}'"),
            _ => bail!("'{q}' is ambiguous"),
        }
    }

    async fn conv(&mut self, target: &str) -> Result<String> {
        if target.starts_with("g:") {
            Ok(format!("group:{}", self.resolve_group(target).await?))
        } else {
            Ok(format!("dm:{}", self.resolve_peer(target).await?))
        }
    }
}

fn ts(ms: i64) -> String {
    chrono::DateTime::from_timestamp_millis(ms).map(|d| d.with_timezone(&chrono::Local).format("%m-%d %H:%M:%S").to_string()).unwrap_or_default()
}

#[tokio::main(flavor = "current_thread")]
async fn main() -> Result<()> {
    let cli = Cli::parse();
    let ep = cli.ipc.clone().unwrap_or_else(default_endpoint);
    let c = IpcClient::connect(&ep).await.with_context(|| format!("无法连接后台服务 lan-daemon ({ep})。请先启动 lan-daemon。"))?;
    let mut x = Ctx { c, json: cli.json };
    match cli.cmd {
        Cmd::Status | Cmd::Peers => {
            let s = x.snapshot().await?;
            if x.json {
                x.out(&s);
                return Ok(());
            }
            let me = &s["me"];
            println!("我: {}  指纹 {}  TCP {}  发现端口 {}", me["name"].as_str().unwrap_or(""), me["fingerprint"].as_str().unwrap_or(""), me["listen_port"], me["discovery_port"]);
            for p in s["peers"].as_array().into_iter().flatten() {
                println!(
                    "  {} {:<20} {}  {:<8} {} {}",
                    if p["online"].as_bool() == Some(true) { "●" } else { "○" },
                    p["name"].as_str().unwrap_or(""),
                    p["fingerprint"].as_str().unwrap_or(""),
                    p["trust"].as_str().unwrap_or(""),
                    if p["via_relay"].as_bool() == Some(true) { "经服务器" } else { p["addr"].as_str().unwrap_or("-") },
                    p["direction"].as_str().unwrap_or("")
                );
            }
            let r = &s["relay"];
            if r["enabled"].as_bool() == Some(true) {
                println!("中转服务器 {}: {} {}  在线 {}", r["server"].as_str().unwrap_or(""), r["state"].as_str().unwrap_or(""), r["detail"].as_str().unwrap_or(""), r["peers"]);
            }
        }
        Cmd::Send { peer, text } => {
            let fp = x.resolve_peer(&peer).await?;
            let r = x.call("send_text", json!({"conversation": format!("dm:{fp}"), "body": text.join(" ")})).await?;
            if x.json { x.out(&r) } else { println!("{} {}", r["id"].as_str().unwrap_or(""), r["status"].as_str().unwrap_or("")) }
        }
        Cmd::SendGroup { group, text } => {
            let g = x.resolve_group(&group).await?;
            let r = x.call("send_text", json!({"conversation": format!("group:{g}"), "body": text.join(" ")})).await?;
            if x.json { x.out(&r) } else { println!("{} {}", r["id"].as_str().unwrap_or(""), r["status"].as_str().unwrap_or("")) }
        }
        Cmd::SendFile { target, path } => {
            let conv = x.conv(&target).await?;
            let abs = std::fs::canonicalize(&path).with_context(|| format!("file not found: {path}"))?;
            let r = x.call("send_file", json!({"conversation": conv, "path": abs.display().to_string()})).await?;
            if x.json { x.out(&r) } else { println!("transfer {}", r["id"].as_str().unwrap_or("")) }
        }
        Cmd::History { target, limit } => {
            let conv = x.conv(&target).await?;
            let r = x.call("get_messages", json!({"conversation": conv, "limit": limit})).await?;
            if x.json {
                x.out(&r);
            } else {
                for m in r.as_array().into_iter().flatten() {
                    let body = if m["kind"] == "file" { format!("[文件] {}", m["file"]["name"].as_str().unwrap_or("")) } else { m["body"].as_str().unwrap_or("").to_string() };
                    println!("{} {:<12} {} [{}]", ts(m["ts_ms"].as_i64().unwrap_or(0)), m["sender_name"].as_str().unwrap_or("*"), body, m["status"].as_str().unwrap_or(""));
                }
            }
        }
        Cmd::Add { address } => {
            let r = x.call("add_peer", json!({"address": address})).await?;
            x.out(&r)
        }
        Cmd::Trust { peer } => { let fp = x.resolve_peer(&peer).await?; let r = x.call("set_trust", json!({"fingerprint": fp, "trust": "trusted"})).await?; x.out(&r) }
        Cmd::Untrust { peer } => { let fp = x.resolve_peer(&peer).await?; let r = x.call("set_trust", json!({"fingerprint": fp, "trust": "unknown"})).await?; x.out(&r) }
        Cmd::Block { peer } => { let fp = x.resolve_peer(&peer).await?; let r = x.call("set_trust", json!({"fingerprint": fp, "trust": "blocked"})).await?; x.out(&r) }
        Cmd::Forget { peer } => { let fp = x.resolve_peer(&peer).await?; let r = x.call("forget_peer", json!({"fingerprint": fp})).await?; x.out(&r) }
        Cmd::Mute { target, off } => { let conv = x.conv(&target).await?; let r = x.call("set_muted", json!({"conversation": conv, "muted": !off})).await?; x.out(&r) }
        Cmd::Groups => { let s = x.snapshot().await?; x.out(&s["groups"]) }
        Cmd::GroupCreate { name, members } => {
            let mut fps = vec![];
            for m in members { fps.push(x.resolve_peer(&m).await?); }
            let r = x.call("create_group", json!({"name": name, "members": fps})).await?;
            x.out(&r)
        }
        Cmd::GroupInvite { group, members } => {
            let g = x.resolve_group(&group).await?;
            let mut fps = vec![];
            for m in members { fps.push(x.resolve_peer(&m).await?); }
            let r = x.call("invite_to_group", json!({"group_id": g, "members": fps})).await?;
            x.out(&r)
        }
        Cmd::GroupRemove { group, peer } => {
            let g = x.resolve_group(&group).await?;
            let fp = x.resolve_peer(&peer).await?;
            let r = x.call("remove_group_member", json!({"group_id": g, "fingerprint": fp})).await?;
            x.out(&r)
        }
        Cmd::GroupLeave { group } => { let g = x.resolve_group(&group).await?; let r = x.call("leave_group", json!({"group_id": g})).await?; x.out(&r) }
        Cmd::Transfers => {
            let s = x.snapshot().await?;
            if x.json { x.out(&s["transfers"]); return Ok(()); }
            for t in s["transfers"].as_array().into_iter().flatten() {
                println!("{} {:<4} {:<10} {}/{} {} {} {}", t["id"].as_str().unwrap_or(""), t["direction"].as_str().unwrap_or(""), t["state"].as_str().unwrap_or(""), t["transferred"], t["size"], t["name"].as_str().unwrap_or(""), t["path"].as_str().unwrap_or(""), t["error"].as_str().unwrap_or(""));
            }
        }
        Cmd::Cancel { id } => { let r = x.call("cancel_transfer", json!({"id": id})).await?; x.out(&r) }
        Cmd::Retry { id } => { let r = x.call("retry_transfer", json!({"id": id})).await?; x.out(&r) }
        Cmd::Search { query } => { let r = x.call("search", json!({"query": query.join(" ")})).await?; x.out(&r) }
        Cmd::Backup { path, password } => { let r = x.call("backup", json!({"path": path, "password": password})).await?; x.out(&r) }
        Cmd::Restore { path, password } => { let r = x.call("restore", json!({"path": path, "password": password})).await?; x.out(&r) }
        Cmd::PinOffer => { let r = x.call("pin_offer", json!({})).await?; x.out(&r) }
        Cmd::PinPair { peer, pin } => { let fp = x.resolve_peer(&peer).await?; let r = x.call("pin_pair", json!({"fingerprint": fp, "pin": pin})).await?; x.out(&r) }
        Cmd::Name { name } => { let r = x.call("set_display_name", json!({"name": name.join(" ")})).await?; x.out(&r) }
        Cmd::Relay { action, host, port, passphrase } => {
            let r = match action.as_str() {
                "on" => {
                    let mut patch = json!({"relay_enabled": true});
                    if let Some(h) = host { patch["relay_host"] = json!(h); }
                    if let Some(p) = port { patch["relay_port"] = json!(p); }
                    if let Some(p) = passphrase { patch["relay_passphrase"] = json!(p); }
                    x.call("update_settings", json!({"settings": patch})).await?;
                    x.call("relay_status", json!({})).await?
                }
                "off" => {
                    x.call("update_settings", json!({"settings": {"relay_enabled": false}})).await?;
                    x.call("relay_status", json!({})).await?
                }
                _ => x.call("relay_status", json!({})).await?,
            };
            x.out(&r)
        }
        Cmd::Diag => { let r = x.call("get_diagnostics", json!({})).await?; x.out(&r) }
        Cmd::FirewallSetup { public } => { let r = x.call("firewall_setup", json!({"include_public": public})).await?; x.out(&r) }
        Cmd::Watch => {
            x.c.subscribe().await?;
            loop {
                let e = x.c.next_event().await?;
                println!("{}", e);
            }
        }
        Cmd::WaitPeer { peer, timeout } => {
            let deadline = Instant::now() + Duration::from_secs(timeout);
            loop {
                if let Ok(fp) = x.resolve_peer(&peer).await {
                    let s = x.snapshot().await?;
                    if s["peers"].as_array().into_iter().flatten().any(|p| p["fingerprint"] == fp.as_str() && p["online"] == true) {
                        println!("online {fp}");
                        return Ok(());
                    }
                }
                if Instant::now() > deadline {
                    eprintln!("timeout waiting for {peer}");
                    std::process::exit(1);
                }
                tokio::time::sleep(Duration::from_millis(200)).await;
            }
        }
        Cmd::WaitDelivered { id, timeout } => {
            let deadline = Instant::now() + Duration::from_secs(timeout);
            loop {
                let s = x.snapshot().await?;
                let convs: Vec<String> = s["conversations"].as_array().into_iter().flatten().filter_map(|c| c["id"].as_str().map(String::from)).collect();
                for c in convs {
                    let ms = x.call("get_messages", json!({"conversation": c, "limit": 500})).await?;
                    if let Some(m) = ms.as_array().into_iter().flatten().find(|m| m["id"] == id.as_str()) {
                        if matches!(m["status"].as_str(), Some("delivered") | Some("read")) {
                            println!("delivered {id}");
                            return Ok(());
                        }
                    }
                }
                if Instant::now() > deadline {
                    eprintln!("timeout waiting for delivery of {id}");
                    std::process::exit(1);
                }
                tokio::time::sleep(Duration::from_millis(250)).await;
            }
        }
        Cmd::Raw { method, params } => {
            let p: Value = params.map(|p| serde_json::from_str(&p)).transpose()?.unwrap_or(json!({}));
            let r = x.call(&method, p).await?;
            x.out(&r)
        }
        Cmd::Shutdown => { let r = x.call("shutdown", json!({})).await?; x.out(&r) }
    }
    Ok(())
}
