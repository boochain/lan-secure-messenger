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
    /// 诊断：端口、指纹、对等端数量、配置路径
    Doctor,
    /// 生成本地 PIN 并等待对方配对（默认随机 6 位）
    PairOffer {
        /// 自定义 PIN（4-12 位数字）；省略则随机生成
        #[arg(long)]
        pin: Option<String>,
        /// 保持守护并等待配对的秒数
        #[arg(long, default_value = "120")]
        wait: u64,
    },
    /// 使用 PIN 与指定对等端配对（互相信任）
    Pair {
        fingerprint: String,
        pin: String,
        #[arg(long, default_value = "5")]
        wait: u64,
    },
    /// 创建群聊（成员为已信任指纹，逗号分隔）
    CreateGroup {
        name: String,
        /// 成员指纹，逗号分隔
        #[arg(long)]
        members: String,
    },
    /// 列出本地群组
    Groups,
    /// 发送群文本
    GroupSend {
        group_id: String,
        message: String,
        #[arg(long, default_value = "3")]
        wait: u64,
    },
    /// 发送群文件
    GroupSendFile {
        group_id: String,
        path: PathBuf,
        #[arg(long, default_value = "3")]
        wait: u64,
    },
    /// 邀请已信任对等端入群
    GroupInvite {
        group_id: String,
        fingerprint: String,
        #[arg(long, default_value = "3")]
        wait: u64,
    },
    /// 退出群组
    GroupLeave {
        group_id: String,
    },
    /// 查看群历史
    GroupHistory {
        group_id: String,
        #[arg(long, default_value = "50")]
        limit: usize,
    },

    /// 群主移除成员并轮换群密钥
    GroupRemoveMember {
        group_id: String,
        fingerprint: String,
        #[arg(long, default_value = "3")]
        wait: u64,
    },
    /// 搜索本地历史（私聊+群聊）
    Search {
        query: String,
        #[arg(long, default_value = "50")]
        limit: usize,
    },
    /// 加密备份身份与信任库
    Backup {
        path: PathBuf,
        #[arg(long)]
        password: String,
    },
    /// 从加密备份恢复（需重启守护进程）
    Restore {
        path: PathBuf,
        #[arg(long)]
        password: String,
    },
    /// 设置下载目录
    SetDownloadDir {
        path: PathBuf,
    },
    /// 显示当前下载目录
    DownloadDir,
    /// 静音/取消静音对等端通知
    Mute {
        fingerprint: String,
        #[arg(long)]
        off: bool,
    },
    /// 静音/取消静音群通知
    MuteGroup {
        group_id: String,
        #[arg(long)]
        off: bool,
    },
    /// 检查 GitHub Releases 是否有新版本（仅提示）
    CheckUpdate,
    /// 请求取消进行中的文件传输（传 transfer uuid）
    CancelTransfer {
        id: String,
    },
    /// 邀请对等端玩井字棋
    GameInvite { fingerprint: String },
    /// 接受/拒绝井字棋邀请
    GameAccept {
        id: String,
        #[arg(long)]
        reject: bool,
    },
    /// 井字棋落子（格子 0-8）
    GameMove { id: String, cell: u8 },
    /// 认输
    GameResign { id: String },
    /// 显示进行中的棋局
    GameShow {
        id: Option<String>,
    },
    /// 启用/关闭本地共享目录（需已信任对等端才能被浏览）
    ShareSet {
        path: Option<PathBuf>,
        #[arg(long)]
        off: bool,
    },
    /// 列出对等端共享目录
    ShareList {
        fingerprint: String,
        #[arg(default_value = "")]
        path: String,
        #[arg(long, default_value = "3")]
        wait: u64,
    },
    /// 从对等端共享下载文件
    ShareGet {
        fingerprint: String,
        path: String,
        #[arg(long, default_value = "3")]
        wait: u64,
    },
    /// 上传本地文件到对等端共享目录
    SharePut {
        fingerprint: String,
        file: PathBuf,
        #[arg(long, default_value = "")]
        rel_dir: String,
        #[arg(long, default_value = "3")]
        wait: u64,
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
        Commands::Doctor => {
            let handle = start_node(paths, config).await?;
            tokio::time::sleep(Duration::from_millis(300)).await;
            let d = handle.doctor().await?;
            println!("=== lan-msg 诊断 ===");
            println!("显示名:     {}", d.display_name);
            println!("指纹:       {}", d.fingerprint);
            println!("配置目录:   {}", d.config_dir);
            println!("绑定地址:   {}", d.bind_addr);
            println!("发现端口:   {}", d.discovery_port);
            println!("TCP 端口:   {}", d.listen_port);
            println!("广播覆盖:   {:?}", d.broadcast_addr);
            println!("自动信任:   {}", d.auto_trust);
            println!("历史加密:   {}", d.history_enabled);
            println!("最大文件:   {} bytes", d.max_file_bytes);
            println!("会话空闲:   {} s", d.session_idle_secs);
            println!(
                "对等端:     {} 总计 / {} 在线 / {} 已信任",
                d.peer_total, d.peer_online, d.peer_trusted
            );
            handle.shutdown().await;
            Ok(())
        }
        Commands::PairOffer { pin, wait } => {
            let pin = pin.unwrap_or_else(|| {
                use rand::Rng;
                format!("{:06}", rand::thread_rng().gen_range(0..1_000_000u32))
            });
            let handle = start_node(paths, config).await?;
            handle.offer_pin(&pin).await?;
            let (fp, name) = handle.fingerprint().await?;
            println!("配对 PIN: {pin}");
            println!("本机: {name} ({fp})");
            println!("请对方运行: lan-msg pair {fp} {pin}");
            println!("等待最多 {wait}s…");
            let mut ev = handle.subscribe();
            let deadline = tokio::time::Instant::now() + Duration::from_secs(wait);
            loop {
                tokio::select! {
                    _ = tokio::signal::ctrl_c() => break,
                    _ = tokio::time::sleep_until(deadline) => break,
                    Ok(ev) = ev.recv() => {
                        match ev {
                            NodeEvent::PeerUpdated(p) if p.trust == TrustState::Trusted => {
                                println!("[配对成功] {} ({})", p.display_name, p.fingerprint);
                            }
                            NodeEvent::GameInviteReceived { id, from_name, .. } => {
                        println!("[井字棋] 邀请来自 {from_name} id={id}");
                    }
                    NodeEvent::GameUpdated { id, board, status, my_turn, .. } => {
                        println!("[井字棋] {id} {status} turn={}\n{board}", if my_turn {"me"} else {"peer"});
                    }
                    NodeEvent::ShareListed { peer_fp, path, entries } => {
                        println!("[共享] {peer_fp}:{path}");
                        for e in entries {
                            println!("  {} {} {}", if e.is_dir {"DIR"} else {"FILE"}, e.size, e.name);
                        }
                    }
                    NodeEvent::Info(i) => println!("[信息] {i}"),
                            NodeEvent::Error(e) => eprintln!("[错误] {e}"),
                            _ => {}
                        }
                    }
                }
            }
            handle.shutdown().await;
            Ok(())
        }
        Commands::Pair { fingerprint, pin, wait } => {
            let handle = start_node(paths, config).await?;
            wait_for_peer(&handle, &fingerprint, wait).await?;
            handle.pair_with_pin(&fingerprint, &pin).await?;
            println!("已与 {fingerprint} 完成 PIN 配对并信任");
            handle.shutdown().await;
            Ok(())
        }
        
        Commands::CreateGroup { name, members } => {
            let handle = start_node(paths, config).await?;
            tokio::time::sleep(Duration::from_secs(2)).await;
            let fps: Vec<String> = members
                .split(',')
                .map(|s| s.trim().to_string())
                .filter(|s| !s.is_empty())
                .collect();
            let g = handle.create_group(&name, fps).await?;
            println!("已创建群组: {} id={} 邀请码={}", g.name, g.id, g.join_code);
            handle.shutdown().await;
            Ok(())
        }
        Commands::Groups => {
            let handle = start_node(paths, config).await?;
            let list = handle.list_groups().await?;
            if list.is_empty() {
                println!("(暂无群组)");
            } else {
                for g in list {
                    println!(
                        "{}  {}  成员{}  邀请码{}",
                        g.id,
                        g.name,
                        g.members.len(),
                        g.join_code
                    );
                }
            }
            handle.shutdown().await;
            Ok(())
        }
        Commands::GroupSend { group_id, message, wait } => {
            let handle = start_node(paths, config).await?;
            tokio::time::sleep(Duration::from_secs(wait)).await;
            let gid = uuid::Uuid::parse_str(&group_id)?;
            let id = handle.send_group_text(gid, &message).await?;
            println!("群消息已发送 id={id}");
            handle.shutdown().await;
            Ok(())
        }
        Commands::GroupSendFile { group_id, path, wait } => {
            let handle = start_node(paths, config).await?;
            tokio::time::sleep(Duration::from_secs(wait)).await;
            let gid = uuid::Uuid::parse_str(&group_id)?;
            let meta = handle.send_group_file(gid, &path).await?;
            println!("群文件已发送: {} ({} bytes)", meta.name, meta.size);
            handle.shutdown().await;
            Ok(())
        }
        Commands::GroupInvite { group_id, fingerprint, wait } => {
            let handle = start_node(paths, config).await?;
            wait_for_peer(&handle, &fingerprint, wait).await?;
            let gid = uuid::Uuid::parse_str(&group_id)?;
            handle.invite_to_group(gid, &fingerprint).await?;
            println!("已邀请 {fingerprint} 加入群 {group_id}");
            handle.shutdown().await;
            Ok(())
        }
        Commands::GroupLeave { group_id } => {
            let handle = start_node(paths, config).await?;
            let gid = uuid::Uuid::parse_str(&group_id)?;
            handle.leave_group(gid).await?;
            println!("已退出群 {group_id}");
            handle.shutdown().await;
            Ok(())
        }
        Commands::GroupHistory { group_id, limit } => {
            let handle = start_node(paths, config).await?;
            let gid = uuid::Uuid::parse_str(&group_id)?;
            let msgs = handle.group_history(gid, limit).await?;
            for m in msgs {
                println!("{} {} {}: {}", m.ts_ms, m.peer_name, m.peer_fp, m.body);
            }
            handle.shutdown().await;
            Ok(())
        }

        
        Commands::GroupRemoveMember { group_id, fingerprint, wait } => {
            let handle = start_node(paths, config).await?;
            tokio::time::sleep(Duration::from_secs(wait)).await;
            let gid = uuid::Uuid::parse_str(&group_id)?;
            let g = handle.remove_group_member(gid, &fingerprint).await?;
            println!(
                "已移除 {}，群 {} 新 epoch={}",
                fingerprint, g.name, g.key_epoch
            );
            handle.shutdown().await;
            Ok(())
        }
        Commands::Search { query, limit } => {
            let handle = start_node(paths, config).await?;
            let msgs = handle.search_history(&query, limit).await?;
            if msgs.is_empty() {
                println!("(无匹配)");
            } else {
                for m in msgs {
                    let scope = if m.group_id.is_some() { "群" } else { "私" };
                    println!(
                        "[{scope}] {} {} {}: {}",
                        m.ts_ms, m.peer_name, m.peer_fp, m.body
                    );
                }
            }
            handle.shutdown().await;
            Ok(())
        }
        Commands::Backup { path, password } => {
            let handle = start_node(paths, config).await?;
            handle.backup_identity(&path, &password).await?;
            println!("备份已写入 {}", path.display());
            handle.shutdown().await;
            Ok(())
        }
        Commands::Restore { path, password } => {
            // restore does not need running sessions; still go through node API
            let handle = start_node(paths, config).await?;
            handle.restore_identity(&path, &password).await?;
            println!("已恢复备份。请重启 lan-msg/lan-gui 以加载新身份。");
            handle.shutdown().await;
            Ok(())
        }
        Commands::SetDownloadDir { path } => {
            let handle = start_node(paths, config).await?;
            handle.set_download_dir(Some(path.clone())).await?;
            println!("下载目录已设为 {}", path.display());
            handle.shutdown().await;
            Ok(())
        }
        Commands::DownloadDir => {
            let handle = start_node(paths, config).await?;
            let d = handle.get_download_dir().await?;
            println!("{}", d.display());
            handle.shutdown().await;
            Ok(())
        }
        Commands::Mute { fingerprint, off } => {
            let handle = start_node(paths, config).await?;
            handle.mute_peer(&fingerprint, !off).await?;
            println!("{}", if off { "已取消静音" } else { "已静音" });
            handle.shutdown().await;
            Ok(())
        }
        Commands::MuteGroup { group_id, off } => {
            let handle = start_node(paths, config).await?;
            let gid = uuid::Uuid::parse_str(&group_id)?;
            handle.mute_group(gid, !off).await?;
            println!("{}", if off { "已取消群静音" } else { "已静音群" });
            handle.shutdown().await;
            Ok(())
        }

        Commands::GameInvite { fingerprint } => {
            let handle = start_node(paths, config).await?;
            tokio::time::sleep(Duration::from_secs(3)).await;
            let id = handle.game_invite(&fingerprint).await?;
            println!("已邀请井字棋 id={id}");
            handle.shutdown().await;
            Ok(())
        }
        Commands::GameAccept { id, reject } => {
            let handle = start_node(paths, config).await?;
            let gid = uuid::Uuid::parse_str(&id)?;
            handle.game_accept(gid, !reject).await?;
            println!("{}", if reject { "已拒绝" } else { "已接受" });
            handle.shutdown().await;
            Ok(())
        }
        Commands::GameMove { id, cell } => {
            let handle = start_node(paths, config).await?;
            let gid = uuid::Uuid::parse_str(&id)?;
            handle.game_move(gid, cell).await?;
            for (i, board) in handle.game_show(Some(gid)).await? {
                println!("{i}\n{board}");
            }
            handle.shutdown().await;
            Ok(())
        }
        Commands::GameResign { id } => {
            let handle = start_node(paths, config).await?;
            let gid = uuid::Uuid::parse_str(&id)?;
            handle.game_resign(gid).await?;
            println!("已认输");
            handle.shutdown().await;
            Ok(())
        }
        Commands::GameShow { id } => {
            let handle = start_node(paths, config).await?;
            let oid = id.as_ref().map(|s| uuid::Uuid::parse_str(s)).transpose()?;
            for (i, board) in handle.game_show(oid).await? {
                println!("=== {i} ===\n{board}");
            }
            handle.shutdown().await;
            Ok(())
        }
        Commands::ShareSet { path, off } => {
            let handle = start_node(paths, config).await?;
            if off {
                handle.set_share_root(None, false).await?;
                println!("已关闭共享");
            } else {
                let p = path.ok_or_else(|| anyhow::anyhow!("需要 -- 或 path"))?;
                handle.set_share_root(Some(p.clone()), true).await?;
                println!("已启用共享: {}", p.display());
            }
            handle.shutdown().await;
            Ok(())
        }
        Commands::ShareList { fingerprint, path, wait } => {
            let handle = start_node(paths, config).await?;
            tokio::time::sleep(Duration::from_secs(wait)).await;
            let entries = handle.share_list(&fingerprint, &path).await?;
            for e in entries {
                let tag = if e.is_dir { "DIR " } else { "FILE" };
                println!("{tag} {:>10} {}", e.size, e.name);
            }
            handle.shutdown().await;
            Ok(())
        }
        Commands::ShareGet { fingerprint, path, wait } => {
            let handle = start_node(paths, config).await?;
            tokio::time::sleep(Duration::from_secs(wait)).await;
            let p = handle.share_get(&fingerprint, &path).await?;
            println!("已下载到 {}", p.display());
            handle.shutdown().await;
            Ok(())
        }
        Commands::SharePut { fingerprint, file, rel_dir, wait } => {
            let handle = start_node(paths, config).await?;
            tokio::time::sleep(Duration::from_secs(wait)).await;
            handle.share_put(&fingerprint, &file, &rel_dir).await?;
            println!("已上传 {}", file.display());
            handle.shutdown().await;
            Ok(())
        }

        Commands::CheckUpdate => {
            match check_github_update() {
                Ok(Some(tag)) => println!("发现新版本: {tag}（当前 {}）\n下载: https://github.com/FREC86/lan-secure-messenger/releases", env!("CARGO_PKG_VERSION")),
                Ok(None) => println!("已是最新（或无法比较）。当前 {}", env!("CARGO_PKG_VERSION")),
                Err(e) => println!("检查更新失败（可忽略）: {e}"),
            }
            Ok(())
        }

        Commands::CancelTransfer { id } => {
            let handle = start_node(paths, config).await?;
            let uuid = uuid::Uuid::parse_str(&id)?;
            handle.cancel_transfer(uuid).await?;
            println!("已请求取消传输 {id}");
            handle.shutdown().await;
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
            let (id, delivered) = handle.send_text(&fingerprint, &message).await?;
            println!("已发送 id={id}");
            if delivered {
                println!("[回执] 已送达 {fingerprint} id={id}");
            } else {
                println!("[回执] 未确认（对端可能离线或超时） id={id}");
            }
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
            let result = handle.broadcast_text_detailed(&message).await?;
            println!(
                "广播完成: 成功 {} / 失败 {}",
                result.ok.len(),
                result.failed.len()
            );
            for fp in &result.ok {
                println!("  ✓ {fp}");
            }
            for (fp, err) in &result.failed {
                println!("  ✗ {fp}: {err}");
            }
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
                    NodeEvent::GroupInviteReceived(g) => {
                        println!("[群邀请] {} id={} 成员{:?}", g.name, g.id, g.members);
                    }
                    NodeEvent::GroupUpdated(g) => {
                        println!("[群更新] {} ({})", g.name, g.id);
                    }
                    NodeEvent::GroupRemoved { name, .. } => {
                        println!("[退群] {name}");
                    }
                    NodeEvent::GroupText { from_name, body, group_id, .. } => {
                        println!("[群消息:{group_id}] {from_name}: {body}");
                    }
                    NodeEvent::GroupFileReceived { from_name, meta, group_id, path, .. } => {
                        println!("[群文件:{group_id}] {from_name}: {} -> {}", meta.name, path.display());
                    }
                    NodeEvent::TextDelivered { id, peer_fp } => {
                        println!("[回执] 已送达 {peer_fp} id={id}");
                    }
                    NodeEvent::PeerStatus { peer_fp, kind, detail } => {
                        println!("[状态] {peer_fp}: {kind:?} {detail}");
                    }
                    NodeEvent::Error(e) => eprintln!("[错误] {e}"),
                    NodeEvent::Info(i) => println!("[信息] {i}"),
                    NodeEvent::PeerUpdated(p) => {
                        // Trust/name changes only (discovery heartbeats no longer spam PeerOnline)
                        println!("[更新] {} {:?}", p.display_name, p.trust);
                    }
                    NodeEvent::GameInviteReceived { id, from_name, .. } => {
                        println!("[井字棋] 邀请来自 {from_name} id={id}");
                    }
                    NodeEvent::GameUpdated { id, board, status, my_turn, .. } => {
                        let who = if my_turn { "me" } else { "peer" };
                        println!("[井字棋] {id} {status} turn={who}\n{board}");
                    }
                    NodeEvent::ShareListed { peer_fp, path, entries } => {
                        println!("[共享] {peer_fp}:{path}");
                        for e in entries {
                            println!(
                                "  {} {} {}",
                                if e.is_dir { "DIR" } else { "FILE" },
                                e.size,
                                e.name
                            );
                        }
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


fn check_github_update() -> anyhow::Result<Option<String>> {
    // Soft check — never fail the app hard.
    let url = "https://api.github.com/repos/FREC86/lan-secure-messenger/releases/latest";
    let body = {
            let out = std::process::Command::new("curl")
                .args(["-fsSL", "-H", "User-Agent: lan-secure-messenger", url])
                .output()?;
            if !out.status.success() {
                anyhow::bail!("curl failed");
            }
            String::from_utf8_lossy(&out.stdout).to_string()
    };
    let v: serde_json::Value = serde_json::from_str(&body)?;
    let tag = v.get("tag_name").and_then(|x| x.as_str()).unwrap_or("").to_string();
    if tag.is_empty() {
        return Ok(None);
    }
    let current = env!("CARGO_PKG_VERSION");
    // Compare tag like v0.3.0 vs 0.1.0 loosely
    let norm = |s: &str| s.trim().trim_start_matches('v').to_string();
    if norm(&tag) != norm(current) && !tag.is_empty() {
        Ok(Some(tag))
    } else {
        Ok(None)
    }
}
