//! relay-server — LSM 中继服务器 (optional relay for LAN Secure Messenger).
use anyhow::Result;
use clap::Parser;
use relay::config::Config;
use relay::server;
use std::path::PathBuf;

#[derive(Parser, Debug, Clone)]
#[command(name = "relay-server", disable_version_flag = true,
    about = "LSM 中继服务器：仅转发端到端 Noise 加密数据 (relay for end-to-end encrypted traffic only)")]
struct Cli {
    /// 监听端口 (TCP port)
    #[arg(long)]
    port: Option<u16>,
    /// 网络口令 (network passphrase); 建议放在配置文件中 (prefer the config file)
    #[arg(long)]
    password: Option<String>,
    /// 监听地址 (bind address), 例如 0.0.0.0 / ::
    #[arg(long)]
    bind: Option<String>,
    /// 配置文件 (TOML config file)
    #[arg(long)]
    config: Option<PathBuf>,
    /// 查询运行中服务器的状态 (query a running server) 并退出
    #[arg(long)]
    status: bool,
    /// 显示版本 (print version)
    #[arg(long, short = 'V')]
    version: bool,
    /// 校验配置后退出 (validate config and exit)
    #[arg(long)]
    check_config: bool,
    /// 由 Windows 服务控制管理器启动 (run under the Windows SCM)
    #[arg(long, hide = true)]
    service: bool,
    /// 写 PID 文件 (write a PID file)
    #[arg(long)]
    pid_file: Option<PathBuf>,
    /// 日志级别 (log level)
    #[arg(long)]
    log_level: Option<String>,
}

fn build_config(cli: &Cli) -> Result<Config> {
    let mut cfg = match &cli.config {
        Some(p) => Config::load(p)?,
        None => Config::default(),
    };
    if let Some(p) = cli.port {
        cfg.server.port = p;
    }
    if let Some(p) = &cli.password {
        cfg.server.passphrase = p.clone();
    }
    if let Some(b) = &cli.bind {
        cfg.server.bind = b.clone();
    }
    if let Some(l) = &cli.log_level {
        cfg.log.level = l.clone();
    }
    if cfg.server.passphrase.is_empty() {
        if let Ok(p) = std::env::var("RELAY_PASSWORD") {
            cfg.server.passphrase = p;
        }
    }
    Ok(cfg)
}

fn init_log(cfg: &Config) -> Option<tracing_appender::non_blocking::WorkerGuard> {
    use tracing_subscriber::EnvFilter;
    let filter = EnvFilter::try_new(&cfg.log.level).unwrap_or_else(|_| EnvFilter::new("info"));
    if !cfg.log.file.is_empty() {
        let p = PathBuf::from(&cfg.log.file);
        if let Some(d) = p.parent() {
            let _ = std::fs::create_dir_all(d);
        }
        if let Ok(f) = std::fs::OpenOptions::new().create(true).append(true).open(&p) {
            let (nb, guard) = tracing_appender::non_blocking(f);
            tracing_subscriber::fmt().with_env_filter(filter).with_ansi(false).with_writer(nb).init();
            return Some(guard);
        }
    }
    tracing_subscriber::fmt().with_env_filter(filter).with_writer(std::io::stderr).init();
    None
}

pub async fn run_server(cfg: Config, pid_file: Option<PathBuf>, stop: tokio::sync::oneshot::Receiver<()>) -> Result<()> {
    let r = server::start(cfg).await?;
    if let Some(p) = &pid_file {
        let _ = std::fs::write(p, std::process::id().to_string());
    }
    let sd = r.shutdown_sender();
    tokio::spawn(async move {
        let ctrl_c = tokio::signal::ctrl_c();
        #[cfg(unix)]
        {
            let mut term = tokio::signal::unix::signal(tokio::signal::unix::SignalKind::terminate()).unwrap();
            tokio::select! { _ = ctrl_c => {}, _ = term.recv() => {}, _ = stop => {} }
        }
        #[cfg(not(unix))]
        {
            tokio::select! { _ = ctrl_c => {}, _ = stop => {} }
        }
        tracing::info!("收到停止信号 (stop signal)");
        let _ = sd.send(true);
    });
    r.wait().await;
    if let Some(p) = &pid_file {
        let _ = std::fs::remove_file(p);
    }
    Ok(())
}

fn main() {
    let cli = Cli::parse();
    if cli.version {
        println!("relay-server {} (protocol v{}, {}-{})", server::VERSION, relay::proto::PROTO_VERSION, std::env::consts::OS, std::env::consts::ARCH);
        return;
    }
    let cfg = match build_config(&cli) {
        Ok(c) => c,
        Err(e) => {
            eprintln!("错误 (error): {e:#}");
            std::process::exit(2);
        }
    };
    if cli.check_config {
        if cfg.server.passphrase.is_empty() {
            eprintln!("错误：未设置口令 (no passphrase)");
            std::process::exit(2);
        }
        if let Err(e) = cfg.salt_bytes() {
            eprintln!("错误 (error): {e:#}");
            std::process::exit(2);
        }
        println!("配置有效 (config OK): {}:{}", cfg.server.bind, cfg.server.port);
        return;
    }
    if cli.status {
        let Some(addr) = cfg.status_addr() else {
            eprintln!("状态接口已关闭 (status endpoint disabled)");
            std::process::exit(3);
        };
        let rt = tokio::runtime::Builder::new_current_thread().enable_all().build().unwrap();
        match rt.block_on(server::query_status(&addr)) {
            Ok(j) => {
                println!("{j}");
            }
            Err(e) => {
                eprintln!("未运行或无法连接状态接口 {addr} (not running): {e}");
                std::process::exit(3);
            }
        }
        return;
    }
    #[cfg(windows)]
    if cli.service {
        if let Err(e) = winsvc::run(cfg, cli.pid_file.clone()) {
            eprintln!("服务启动失败 (service failed): {e:#}");
            std::process::exit(1);
        }
        return;
    }
    let _g = init_log(&cfg);
    let rt = tokio::runtime::Builder::new_multi_thread().enable_all().build().unwrap();
    let (_tx, rx) = tokio::sync::oneshot::channel();
    if let Err(e) = rt.block_on(run_server(cfg, cli.pid_file.clone(), rx)) {
        tracing::error!("{e:#}");
        eprintln!("错误 (error): {e:#}");
        std::process::exit(1);
    }
}

#[cfg(windows)]
mod winsvc {
    //! Proper Windows service: registers a control handler with the SCM.
    use super::*;
    use std::ffi::OsString;
    use std::sync::Mutex;
    use std::time::Duration;
    use windows_service::service::{ServiceControl, ServiceControlAccept, ServiceExitCode, ServiceState, ServiceStatus, ServiceType};
    use windows_service::service_control_handler::{self, ServiceControlHandlerResult};
    use windows_service::{define_windows_service, service_dispatcher};

    pub const SERVICE_NAME: &str = "LsmRelay";
    static ARGS: Mutex<Option<(Config, Option<PathBuf>)>> = Mutex::new(None);

    define_windows_service!(ffi_service_main, service_main);

    pub fn run(cfg: Config, pid: Option<PathBuf>) -> Result<()> {
        *ARGS.lock().unwrap() = Some((cfg, pid));
        service_dispatcher::start(SERVICE_NAME, ffi_service_main)?;
        Ok(())
    }

    fn service_main(_args: Vec<OsString>) {
        let _ = run_service();
    }

    fn run_service() -> Result<()> {
        let (cfg, pid) = ARGS.lock().unwrap().take().unwrap();
        let _g = init_log(&cfg);
        let (tx, rx) = tokio::sync::oneshot::channel::<()>();
        let tx = Mutex::new(Some(tx));
        let handler = move |c| match c {
            ServiceControl::Stop | ServiceControl::Shutdown => {
                if let Some(t) = tx.lock().unwrap().take() {
                    let _ = t.send(());
                }
                ServiceControlHandlerResult::NoError
            }
            ServiceControl::Interrogate => ServiceControlHandlerResult::NoError,
            _ => ServiceControlHandlerResult::NotImplemented,
        };
        let h = service_control_handler::register(SERVICE_NAME, handler)?;
        let st = |s: ServiceState, code: u32| ServiceStatus {
            service_type: ServiceType::OWN_PROCESS,
            current_state: s,
            controls_accepted: if s == ServiceState::Running { ServiceControlAccept::STOP | ServiceControlAccept::SHUTDOWN } else { ServiceControlAccept::empty() },
            exit_code: ServiceExitCode::Win32(code),
            checkpoint: 0,
            wait_hint: Duration::from_secs(5),
            process_id: None,
        };
        h.set_service_status(st(ServiceState::Running, 0))?;
        let rt = tokio::runtime::Builder::new_multi_thread().enable_all().build()?;
        let r = rt.block_on(run_server(cfg, pid, rx));
        if let Err(e) = &r {
            tracing::error!("{e:#}");
        }
        h.set_service_status(st(ServiceState::Stopped, if r.is_ok() { 0 } else { 1 }))?;
        Ok(())
    }
}
