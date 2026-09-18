use anyhow::Result;
use clap::{Parser, Subcommand};
use lan_core::config::{AppConfig, ConfigPaths};
use lan_core::node::{Node, NodeEvent};
use lan_core::peer::TrustState;
use std::path::PathBuf;
use std::time::Duration;
use tracing_subscriber::EnvFilter;

#[derive(Parser, Debug)]
#[command(name = "lan-msg", about = "局域网加密即时通讯 CLI", version)]
struct Cli {
    /// 显示名称
    #[arg(long, env = "LAN_MSG_NAME")]
    name: Option<String>,

    /// 配置目录覆盖（用于同机多实例测试）
    #[arg(long, env = "LAN_MSG_CONFIG")]
    config_dir: Option<PathBuf>,

    /// 绑定地址
    #[arg(long, default_value = "0.0.0.0", env = "LAN_MSG_BIND")]
    bind: String,

    /// 发现端口
    #[arg(long, env = "LAN_MSG_DISCOVERY_PORT")]
    discovery_port: Option<u16>,

    /// TCP 监听端口
    #[arg(long, env = "LAN_MSG_LISTEN_PORT")]
    listen_port: Option<u16>,

    /// 广播目标（本机环回测试用 127.0.0.1）
    #[arg(long, env = "LAN_MSG_BROADCAST")]
    broadcast: Option<String>,

    /// 自动信任新对等端（仅测试/可信局域网）
    #[arg(long, env = "LAN_MSG_AUTO_TRUST")]
    auto_trust: bool,

    #[command(subcommand)]
    command: Commands,
}

#[derive(Subcommand, Debug)]
enum Commands {
    /// 启动守护进程：发现 + 接收消息
    Daemon,
    /// 打印本机指纹与显示名
    Id,
    /// 列出当前发现的对等端
    Peers {
        /// 等待发现的秒数
        #[arg(long, default_value = "3")]
        wait: u64,
    },
    /// 信任对等端（TOFU）
    Trust {
        fingerprint: String,
    },
    /// 取消信任 / 拉黑
    Block {
        fingerprint: String,
    },
    /// 发送文本
    Send {
        fingerprint: String,
        message: String,
        #[arg(long, default_value = "3")]
        wait: u64,
    },
    /// 向所有已信任在线对等端广播
    Broadcast {
        message: String,
        #[arg(long, default_value = "3")]
        wait: u64,
    },
    /// 发送文件
    SendFile {
        fingerprint: String,
        path: PathBuf,
        #[arg(long, default_value = "3")]
        wait: u64,
    },
    /// 查看本地历史
    History {
        fingerprint: Option<String>,
        #[arg(long, default_value = "50")]
        limit: usize,
        #[arg(long, default_value = "2")]
        wait: u64,
    },
    /// 设置显示名称
    SetName {
        name: String,
    },
}

#[tokio::main]
async fn main() -> Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(EnvFilter::from_default_env().add_directive("lan_core=info".parse()?))
        .with_target(false)
        .init();

    let cli = Cli::parse();
    let paths = resolve_paths(cli.config_dir.as_ref())?;
    let mut config = AppConfig::load(&paths)?;
    if let Some(name) = &cli.name {
        config.display_name = name.clone();
    }
    config.bind_addr = cli.bind.clone();
    if let Some(p) = cli.discovery_port {
        config.discovery_port = p;
    }
    if let Some(p) = cli.listen_port {
        config.listen_port = p;
    }
    if let Some(b) = &cli.broadcast {
        config.broadcast_addr = Some(b.clone());
    }
    if cli.auto_trust {
        config.auto_trust = true;
    }
    config.save(&paths)?;

    match cli.command {
        Commands::Id => {
            let cfg_path = paths.root.display().to_string();
            let node = Node::open(paths, config)?;
            println!("显示名: {}", node.display_name());
            println!("指纹:   {}", node.identity_fingerprint());
            println!("配置:   {}", cfg_path);
            Ok(())
        }
        Commands::SetName { name } => {
            config.display_name = name.clone();
            config.save(&paths)?;
            println!("显示名已设为: {name}");
            Ok(())
        }
        Commands::Daemon => run_daemon(paths, config).await,
        Commands::Peers { wait } => {
            let handle = start_node(paths, config).await?;
            tokio::time::sleep(Duration::from_secs(wait)).await;
            let peers = handle.list_peers().await?;
            if peers.is_empty() {
                println!("(暂无对等端)");
            } else {
                println!(
                    "{:<22} {:<16} {:<8} {}",
                    "指纹", "名称", "状态", "地址"
                );
                for p in peers {
                    println!(
                        "{:<22} {:<16} {:<8} {} (port {}) {:?}",
                        p.fingerprint,
                        truncate(&p.display_name, 16),
                        if p.online { "在线" } else { "离线" },
                        p.addr.ip(),
                        p.listen_port,
                        p.trust
                    );
                }
            }
            handle.shutdown().await;
            Ok(())
        }
        Commands::Trust { fingerprint } => {
            let handle = start_node(paths, config).await?;
            tokio::time::sleep(Duration::from_secs(2)).await;
            handle.trust_peer(&fingerprint, TrustState::Trusted).await?;
            println!("已信任: {fingerprint}");
            handle.shutdown().await;
            Ok(())
        }
        Commands::Block { fingerprint } => {
            let handle = start_node(paths, config).await?;
            handle.trust_peer(&fingerprint, TrustState::Blocked).await?;
            println!("已拉黑: {fingerprint}");
            handle.shutdown().await;
            Ok(())
        }
        Commands::Send {
            fingerprint,
            message,
            wait,
        } => {
            let handle = start_node(paths, config).await?;
            wait_for_peer(&handle, &fingerprint, wait).await?;
            // auto trust for convenience when sending after discovery if already trusted path fails
            let _ = handle.trust_peer(&fingerprint, TrustState::Trusted).await;
            let id = handle.send_text(&fingerprint, &message).await?;
            println!("已发送 id={id}");
            handle.shutdown().await;
            Ok(())
        }
        Commands::Broadcast { message, wait } => {
            let handle = start_node(paths, config).await?;
            tokio::time::sleep(Duration::from_secs(wait)).await;
            // trust all online
            for p in handle.list_peers().await? {
                if p.online {
                    let _ = handle
                        .trust_peer(p.fingerprint.as_str(), TrustState::Trusted)
                        .await;
                }
            }
            let n = handle.broadcast_text(&message).await?;
            println!("已广播给 {n} 个对等端");
            handle.shutdown().await;
            Ok(())
        }
        Commands::SendFile {
            fingerprint,
            path,
            wait,
        } => {
            let handle = start_node(paths, config).await?;
            wait_for_peer(&handle, &fingerprint, wait).await?;
            let _ = handle.trust_peer(&fingerprint, TrustState::Trusted).await;
            let meta = handle.send_file(&fingerprint, &path).await?;
            println!(
                "文件已发送: {} ({} bytes, sha256={})",
                meta.name, meta.size, meta.sha256
            );
            handle.shutdown().await;
            Ok(())
        }
        Commands::History {
            fingerprint,
            limit,
            wait,
        } => {
            let handle = start_node(paths, config).await?;
            tokio::time::sleep(Duration::from_secs(wait)).await;
            let msgs = handle.history(fingerprint.as_deref(), limit).await?;
            for m in msgs {
                let dir = match m.direction {
                    lan_core::protocol::MsgDirection::In => "←",
                    lan_core::protocol::MsgDirection::Out => "→",
                };
                println!(
                    "{} [{}] {} {}: {}",
                    dir,
                    m.peer_fp,
                    m.peer_name,
                    m.ts_ms,
                    m.body
                );
            }
            handle.shutdown().await;
            Ok(())
        }
    }
}

fn resolve_paths(override_dir: Option<&PathBuf>) -> Result<ConfigPaths> {
    if let Some(dir) = override_dir {
        std::fs::create_dir_all(dir)?;
        Ok(ConfigPaths::from_dir(dir))
    } else {
        ConfigPaths::resolve()
    }
}


async fn start_node(paths: ConfigPaths, config: AppConfig) -> Result<lan_core::NodeHandle> {
    let node = Node::open(paths, config)?;
    node.start().await
}

async fn run_daemon(paths: ConfigPaths, config: AppConfig) -> Result<()> {
    let handle = start_node(paths, config).await?;
    let (fp, name) = handle.fingerprint().await?;
    println!("lan-msg 守护进程已启动");
    println!("  显示名: {name}");
    println!("  指纹:   {fp}");
    println!("按 Ctrl+C 退出");

    let mut ev = handle.subscribe();
    loop {
        tokio::select! {
            _ = tokio::signal::ctrl_c() => {
                println!("正在退出…");
                handle.shutdown().await;
                break;
            }
            Ok(event) = ev.recv() => {
                match event {
                    NodeEvent::PeerOnline(p) => {
                        println!("[在线] {} ({}) {:?}", p.display_name, p.fingerprint, p.trust);
                    }
                    NodeEvent::PeerOffline { fingerprint, name } => {
                        println!("[离线] {name} ({fingerprint})");
                    }
                    NodeEvent::NewPeerNeedsTrust(p) => {
                        println!(
                            "[待信任] {} 指纹={}  — 运行: lan-msg trust {}",
                            p.display_name, p.fingerprint, p.fingerprint
                        );
                    }
                    NodeEvent::Text { from_name, from_fp, body, broadcast, .. } => {
                        let tag = if broadcast { "广播" } else { "消息" };
                        println!("[{tag}] {from_name} ({from_fp}): {body}");
                    }
                    NodeEvent::FileReceived { from_name, path, meta, .. } => {
                        println!(
                            "[文件] 来自 {from_name}: {} -> {} ({} bytes)",
                            meta.name,
                            path.display(),
                            meta.size
                        );
                    }
                    NodeEvent::FileProgress { received, total, .. } => {
                        if total > 0 {
                            let pct = received * 100 / total;
                            print!("\r[进度] {received}/{total} ({pct}%)");
                            let _ = std::io::Write::flush(&mut std::io::stdout());
                            if received >= total {
                                println!();
                            }
                        }
                    }
                    NodeEvent::TransferComplete { name, ok, .. } => {
                        println!("[传输] {name} {}", if ok { "完成" } else { "失败" });
                    }
                    NodeEvent::Error(e) => eprintln!("[错误] {e}"),
                    NodeEvent::Info(i) => println!("[信息] {i}"),
                    NodeEvent::PeerUpdated(p) => {
                        println!("[更新] {} {:?} ", p.display_name, p.trust);
                    }
                }
            }
        }
    }
    Ok(())
}

async fn wait_for_peer(
    handle: &lan_core::NodeHandle,
    fp: &str,
    wait: u64,
) -> Result<()> {
    let deadline = tokio::time::Instant::now() + Duration::from_secs(wait);
    loop {
        let peers = handle.list_peers().await?;
        if peers.iter().any(|p| p.fingerprint.as_str() == fp && p.online) {
            return Ok(());
        }
        if tokio::time::Instant::now() >= deadline {
            // also try partial match
            if let Some(p) = peers.iter().find(|p| p.fingerprint.as_str().starts_with(fp) && p.online) {
                anyhow::bail!("请使用完整指纹: {}", p.fingerprint);
            }
            anyhow::bail!("等待对等端超时: {fp}");
        }
        tokio::time::sleep(Duration::from_millis(400)).await;
    }
}

fn truncate(s: &str, max: usize) -> String {
    if s.chars().count() <= max {
        s.to_string()
    } else {
        let t: String = s.chars().take(max.saturating_sub(1)).collect();
        format!("{t}…")
    }
}
