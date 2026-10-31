//! lan-daemon: network engine + local IPC API. UIs (WinUI app, lan-msg CLI) are clients.

use anyhow::{Context, Result};
use clap::{Parser, Subcommand};
use lan_core::{engine, firewall, ipc, ConfigPaths};
use serde_json::{json, Value};
use tracing_subscriber::{fmt, prelude::*, EnvFilter};

#[derive(Parser)]
#[command(name = "lan-daemon", version, about = "LAN Secure Messenger background service")]
struct Cli {
    /// Data directory (default: %APPDATA%\FREC86\lan-secure-messenger or ~/.config/lan-secure-messenger)
    #[arg(long, env = "LSM_DATA_DIR", global = true)]
    data_dir: Option<std::path::PathBuf>,
    /// IPC endpoint (named pipe on Windows, unix socket path elsewhere)
    #[arg(long, env = "LSM_IPC", global = true)]
    ipc: Option<String>,
    /// Temporary settings override, e.g. --set listen_port=45000 --set 'announce_targets=["10.0.0.5"]'
    #[arg(long = "set", value_name = "KEY=JSON", global = true)]
    set: Vec<String>,
    #[command(subcommand)]
    cmd: Option<Cmd>,
}

#[derive(Subcommand)]
enum Cmd {
    /// Run the daemon (default)
    Run,
    /// Add Windows Firewall inbound rules for this exe (asks for admin once via UAC)
    FirewallSetup {
        #[arg(long)]
        elevated: bool,
        /// Also allow on networks Windows classifies as "Public"
        #[arg(long)]
        public: bool,
    },
    /// Show firewall rule / network profile status as JSON
    FirewallStatus,
    /// List network interfaces and how discovery classifies them
    Interfaces,
    /// Print the default IPC endpoint and data paths
    Paths,
}

fn paths(cli: &Cli) -> Result<ConfigPaths> {
    Ok(match &cli.data_dir {
        Some(d) => ConfigPaths::from_base(d),
        None => ConfigPaths::resolve()?,
    })
}

fn overrides(set: &[String]) -> Result<Option<Value>> {
    if set.is_empty() {
        return Ok(None);
    }
    let mut m = serde_json::Map::new();
    for s in set {
        let (k, v) = s.split_once('=').context("--set expects KEY=VALUE")?;
        let val = serde_json::from_str(v).unwrap_or_else(|_| Value::String(v.to_string()));
        m.insert(k.trim().to_string(), val);
    }
    Ok(Some(Value::Object(m)))
}

fn init_logging(p: &ConfigPaths) -> Option<tracing_appender::non_blocking::WorkerGuard> {
    let _ = std::fs::create_dir_all(&p.logs);
    let filter = || EnvFilter::try_from_env("LSM_LOG").unwrap_or_else(|_| EnvFilter::new("info,lan_core=info"));
    let appender = tracing_appender::rolling::Builder::new()
        .rotation(tracing_appender::rolling::Rotation::DAILY)
        .filename_prefix("daemon")
        .filename_suffix("log")
        .max_log_files(7)
        .build(&p.logs);
    match appender {
        Ok(a) => {
            let (nb, guard) = tracing_appender::non_blocking(a);
            tracing_subscriber::registry()
                .with(fmt::layer().with_ansi(false).with_writer(nb).with_filter(filter()))
                .with(fmt::layer().with_writer(std::io::stderr).with_filter(filter()))
                .init();
            Some(guard)
        }
        Err(e) => {
            tracing_subscriber::registry().with(fmt::layer().with_writer(std::io::stderr).with_filter(filter())).init();
            tracing::warn!(error = %e, "file logging unavailable");
            None
        }
    }
}

fn install_panic_hook(p: &ConfigPaths) {
    let crash = p.logs.join("crash.log");
    let default = std::panic::take_hook();
    std::panic::set_hook(Box::new(move |info| {
        let bt = std::backtrace::Backtrace::force_capture();
        let msg = format!("[{}] lan-daemon {} panicked: {info}\n{bt}\n", chrono_now(), lan_core::APP_VERSION);
        tracing::error!("{msg}");
        use std::io::Write;
        if let Ok(mut f) = std::fs::OpenOptions::new().create(true).append(true).open(&crash) {
            let _ = f.write_all(msg.as_bytes());
        }
        #[cfg(windows)]
        if std::env::var("LSM_NO_DIALOG").is_err() {
            use windows_sys::Win32::UI::WindowsAndMessaging::{MessageBoxW, MB_ICONERROR, MB_OK};
            let text: Vec<u16> = format!("LAN Secure Messenger 后台服务崩溃：\n{info}\n\n详细信息已写入:\n{}", crash.display()).encode_utf16().chain([0]).collect();
            let title: Vec<u16> = "lan-daemon".encode_utf16().chain([0]).collect();
            unsafe { MessageBoxW(std::ptr::null_mut(), text.as_ptr(), title.as_ptr(), MB_OK | MB_ICONERROR) };
        }
        default(info);
    }));
}

fn chrono_now() -> String {
    let d = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap_or_default();
    format!("unix {}", d.as_secs())
}

fn main() -> Result<()> {
    let cli = Cli::parse();
    match &cli.cmd {
        Some(Cmd::FirewallSetup { elevated, public }) => {
            if firewall::host_firewall_enabled() == Some(false) {
                println!("{}", firewall::FW_DISABLED_MSG);
                return Ok(());
            }
            let r = if *elevated { firewall::apply_rules(&firewall::exe_path(), *public) } else { firewall::setup_elevated(*public) };
            match r {
                Ok(()) => {
                    println!("firewall rules added for {}", firewall::exe_path());
                    return Ok(());
                }
                Err(e) => {
                    eprintln!("firewall setup failed: {e:#}");
                    std::process::exit(2);
                }
            }
        }
        Some(Cmd::FirewallStatus) => {
            println!("{}", serde_json::to_string_pretty(&firewall::status())?);
            return Ok(());
        }
        Some(Cmd::Interfaces) => {
            let s = lan_core::Settings::load(&paths(&cli)?).unwrap_or_default();
            println!("{}", serde_json::to_string_pretty(&lan_core::netif::list(&s.include_interfaces, &s.exclude_interfaces))?);
            return Ok(());
        }
        Some(Cmd::Paths) => {
            let p = paths(&cli)?;
            println!("{}", serde_json::to_string_pretty(&json!({
                "ipc": cli.ipc.clone().unwrap_or_else(ipc::default_endpoint),
                "base": p.base, "config": p.root, "data": p.data, "logs": p.logs,
            }))?);
            return Ok(());
        }
        _ => {}
    }
    let p = paths(&cli)?;
    p.ensure_dirs()?;
    let _guard = init_logging(&p);
    install_panic_hook(&p);
    let endpoint = cli.ipc.clone().unwrap_or_else(ipc::default_endpoint);
    let rt = tokio::runtime::Builder::new_multi_thread().enable_all().build()?;
    rt.block_on(async move {
        // Single instance: if the endpoint answers, another daemon is running.
        if let Ok(mut c) = ipc::IpcClient::connect(&endpoint).await {
            if c.call("hello", json!({})).await.is_ok() {
                eprintln!("lan-daemon is already running on {endpoint}");
                std::process::exit(3);
            }
        }
        tracing::info!(version = lan_core::APP_VERSION, data = %p.base.display(), ipc = %endpoint, "lan-daemon starting");
        let h = engine::start(engine::EngineOptions { paths: p, ipc_endpoint: endpoint.clone(), overrides: overrides(&cli.set)? }).await?;
        let server = tokio::spawn(ipc::serve(endpoint.clone(), h.clone()));
        let stop = h.shutdown.clone();
        tokio::select! {
            r = server => {
                match r {
                    Ok(Err(e)) => { tracing::error!(error = %e, "IPC server failed"); let _ = h.call("shutdown", json!({})).await; stop.notified().await; return Err(e) }
                    _ => {}
                }
            }
            _ = stop.notified() => { tracing::info!("shutdown requested via IPC"); }
            _ = tokio::signal::ctrl_c() => {
                tracing::info!("ctrl-c");
                let _ = h.call("shutdown", json!({})).await;
                let _ = tokio::time::timeout(std::time::Duration::from_secs(3), stop.notified()).await;
            }
        }
        #[cfg(unix)]
        let _ = std::fs::remove_file(&endpoint);
        Ok::<(), anyhow::Error>(())
    })?;
    // Give the log writer a moment.
    std::thread::sleep(std::time::Duration::from_millis(100));
    Ok(())
}
