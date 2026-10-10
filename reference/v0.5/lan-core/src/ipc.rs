//! Local IPC: newline-delimited JSON-RPC 2.0.
//!
//! Windows: named pipe `\\.\pipe\lan-secure-messenger-<user>` whose DACL grants access only
//! to the current user SID (+ SYSTEM); remote clients rejected; first-instance flag makes a
//! second daemon fail fast. Unix: socket `$XDG_RUNTIME_DIR/lan-secure-messenger-<user>.sock`
//! (mode 0600). Protocol documented in docs/ipc.md.

use crate::engine::EngineHandle;
use anyhow::{bail, Context, Result};
use serde_json::{json, Value};
use std::collections::VecDeque;
use tokio::io::{AsyncBufReadExt, AsyncRead, AsyncReadExt, AsyncWrite, AsyncWriteExt, BufReader};
use tokio::sync::{broadcast, mpsc};

pub const MAX_LINE: usize = 4 * 1024 * 1024;

fn user_tag() -> String {
    let u = std::env::var("USERNAME").or_else(|_| std::env::var("USER")).unwrap_or_else(|_| "user".into());
    let s: String = u.chars().map(|c| if c.is_ascii_alphanumeric() || c == '-' || c == '_' || c == '.' { c } else { '_' }).collect();
    if s.is_empty() {
        "user".into()
    } else {
        s
    }
}

pub fn default_endpoint() -> String {
    #[cfg(windows)]
    {
        format!(r"\\.\pipe\lan-secure-messenger-{}", user_tag())
    }
    #[cfg(not(windows))]
    {
        let dir = std::env::var("XDG_RUNTIME_DIR").ok().filter(|d| std::path::Path::new(d).is_dir()).unwrap_or_else(|| "/tmp".into());
        format!("{dir}/lan-secure-messenger-{}.sock", user_tag())
    }
}

pub trait Stream: AsyncRead + AsyncWrite + Unpin + Send {}
impl<T: AsyncRead + AsyncWrite + Unpin + Send> Stream for T {}

/// Serve forever on `endpoint`. Returns an error if another daemon already owns it.
pub async fn serve(endpoint: String, engine: EngineHandle) -> Result<()> {
    #[cfg(unix)]
    {
        use tokio::net::UnixListener;
        let path = std::path::PathBuf::from(&endpoint);
        if path.exists() {
            if tokio::net::UnixStream::connect(&path).await.is_ok() {
                bail!("another daemon is already listening on {endpoint}");
            }
            let _ = std::fs::remove_file(&path);
        }
        if let Some(parent) = path.parent() {
            let _ = std::fs::create_dir_all(parent);
        }
        let l = UnixListener::bind(&path).with_context(|| format!("bind {endpoint}"))?;
        {
            use std::os::unix::fs::PermissionsExt;
            let _ = std::fs::set_permissions(&path, std::fs::Permissions::from_mode(0o600));
        }
        tracing::info!(%endpoint, "IPC listening");
        loop {
            let (s, _) = l.accept().await?;
            tokio::spawn(handle_client(s, engine.clone()));
        }
    }
    #[cfg(windows)]
    {
        win::serve(endpoint, engine).await
    }
}

#[cfg(windows)]
mod win {
    use super::*;
    use std::ffi::c_void;
    use tokio::net::windows::named_pipe::{NamedPipeServer, ServerOptions};
    use windows_sys::Win32::Foundation::{CloseHandle, LocalFree, HANDLE};
    use windows_sys::Win32::Security::Authorization::{ConvertSidToStringSidW, ConvertStringSecurityDescriptorToSecurityDescriptorW, SDDL_REVISION_1};
    use windows_sys::Win32::Security::{GetTokenInformation, TokenUser, PSECURITY_DESCRIPTOR, SECURITY_ATTRIBUTES, TOKEN_QUERY, TOKEN_USER};
    use windows_sys::Win32::System::Threading::{GetCurrentProcess, OpenProcessToken};

    fn wide(s: &str) -> Vec<u16> {
        s.encode_utf16().chain(std::iter::once(0)).collect()
    }

    fn user_sid() -> Result<String> {
        unsafe {
            let mut tok: HANDLE = std::ptr::null_mut();
            if OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &mut tok) == 0 {
                bail!("OpenProcessToken failed");
            }
            let mut len = 0u32;
            GetTokenInformation(tok, TokenUser, std::ptr::null_mut(), 0, &mut len);
            let mut buf = vec![0u8; len as usize + 16];
            let ok = GetTokenInformation(tok, TokenUser, buf.as_mut_ptr() as *mut c_void, buf.len() as u32, &mut len);
            CloseHandle(tok);
            if ok == 0 {
                bail!("GetTokenInformation failed");
            }
            let tu = &*(buf.as_ptr() as *const TOKEN_USER);
            let mut p: *mut u16 = std::ptr::null_mut();
            if ConvertSidToStringSidW(tu.User.Sid, &mut p) == 0 {
                bail!("ConvertSidToStringSidW failed");
            }
            let mut n = 0;
            while *p.add(n) != 0 {
                n += 1;
            }
            let s = String::from_utf16_lossy(std::slice::from_raw_parts(p, n));
            LocalFree(p as *mut c_void);
            Ok(s)
        }
    }

    pub struct Sa {
        pub sa: SECURITY_ATTRIBUTES,
        _psd: PSECURITY_DESCRIPTOR,
    }
    unsafe impl Send for Sa {}

    pub fn security() -> Result<Sa> {
        let sid = user_sid()?;
        // Protected DACL: full access for the current user and SYSTEM only.
        let sddl = wide(&format!("D:P(A;;GA;;;{sid})(A;;GA;;;SY)"));
        let mut psd: PSECURITY_DESCRIPTOR = std::ptr::null_mut();
        let ok = unsafe { ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.as_ptr(), SDDL_REVISION_1, &mut psd, std::ptr::null_mut()) };
        if ok == 0 {
            bail!("ConvertStringSecurityDescriptorToSecurityDescriptorW failed");
        }
        Ok(Sa { sa: SECURITY_ATTRIBUTES { nLength: std::mem::size_of::<SECURITY_ATTRIBUTES>() as u32, lpSecurityDescriptor: psd, bInheritHandle: 0 }, _psd: psd })
    }

    fn create(endpoint: &str, sa: &mut Sa, first: bool) -> std::io::Result<NamedPipeServer> {
        let mut o = ServerOptions::new();
        o.first_pipe_instance(first).reject_remote_clients(true).max_instances(64);
        unsafe { o.create_with_security_attributes_raw(endpoint, &mut sa.sa as *mut SECURITY_ATTRIBUTES as *mut c_void) }
    }

    pub async fn serve(endpoint: String, engine: EngineHandle) -> Result<()> {
        let mut sa = security()?;
        let mut server = create(&endpoint, &mut sa, true).with_context(|| format!("create pipe {endpoint} (another daemon running?)"))?;
        tracing::info!(%endpoint, "IPC listening (named pipe, current-user ACL)");
        loop {
            server.connect().await?;
            let client = server;
            server = create(&endpoint, &mut sa, false)?;
            tokio::spawn(handle_client(client, engine.clone()));
        }
    }
}

fn line(v: &Value) -> String {
    let mut s = v.to_string();
    s.push('\n');
    s
}

async fn handle_client<S: Stream + 'static>(stream: S, engine: EngineHandle) {
    let (rd, mut wr) = tokio::io::split(stream);
    let (out_tx, mut out_rx) = mpsc::unbounded_channel::<String>();
    let writer = tokio::spawn(async move {
        while let Some(s) = out_rx.recv().await {
            if wr.write_all(s.as_bytes()).await.is_err() {
                break;
            }
            let _ = wr.flush().await;
        }
    });
    let mut rd = BufReader::new(rd);
    let mut buf = Vec::new();
    let mut sub: Option<tokio::task::JoinHandle<()>> = None;
    loop {
        buf.clear();
        match (&mut rd).take(MAX_LINE as u64 + 1).read_until(b'\n', &mut buf).await {
            Ok(0) | Err(_) => break,
            Ok(_) => {}
        }
        if buf.len() > MAX_LINE {
            let _ = out_tx.send(line(&json!({"jsonrpc":"2.0","id":null,"error":{"code":-32600,"message":"line too long"}})));
            break;
        }
        let text = String::from_utf8_lossy(&buf);
        let text = text.trim();
        if text.is_empty() {
            continue;
        }
        let req: Value = match serde_json::from_str(text) {
            Ok(v) => v,
            Err(e) => {
                let _ = out_tx.send(line(&json!({"jsonrpc":"2.0","id":null,"error":{"code":-32700,"message":format!("parse error: {e}")}})));
                continue;
            }
        };
        let id = req.get("id").cloned();
        let method = req.get("method").and_then(|m| m.as_str()).unwrap_or("").to_string();
        let params = req.get("params").cloned().filter(|p| !p.is_null()).unwrap_or_else(|| json!({}));
        if method == "subscribe" {
            if sub.is_none() {
                let mut rx = engine.subscribe();
                let tx = out_tx.clone();
                sub = Some(tokio::spawn(async move {
                    loop {
                        match rx.recv().await {
                            Ok(ev) => {
                                let v = json!({"jsonrpc":"2.0","method":"event","params": ev});
                                if tx.send(line(&v)).is_err() {
                                    break;
                                }
                            }
                            Err(broadcast::error::RecvError::Lagged(_)) => {
                                let _ = tx.send(line(&json!({"jsonrpc":"2.0","method":"event","params":{"type":"resync"}})));
                            }
                            Err(_) => break,
                        }
                    }
                }));
            }
            if let Some(id) = id {
                let _ = out_tx.send(line(&json!({"jsonrpc":"2.0","id":id,"result":{"ok":true}})));
            }
            continue;
        }
        let engine = engine.clone();
        let tx = out_tx.clone();
        tokio::spawn(async move {
            let r = engine.call(&method, params).await;
            if let Some(id) = id {
                let v = match r {
                    Ok(res) => json!({"jsonrpc":"2.0","id":id,"result":res}),
                    Err(e) => json!({"jsonrpc":"2.0","id":id,"error":{"code":e.code,"message":e.message}}),
                };
                let _ = tx.send(line(&v));
            }
        });
    }
    if let Some(s) = sub {
        s.abort();
    }
    drop(out_tx);
    let _ = writer.await;
}

/// Minimal async client (used by lan-msg and tests).
pub struct IpcClient {
    rd: BufReader<tokio::io::ReadHalf<Box<dyn Stream>>>,
    wr: tokio::io::WriteHalf<Box<dyn Stream>>,
    next_id: u64,
    events: VecDeque<Value>,
}

impl IpcClient {
    pub async fn connect(endpoint: &str) -> Result<Self> {
        let s: Box<dyn Stream> = {
            #[cfg(unix)]
            {
                Box::new(tokio::net::UnixStream::connect(endpoint).await.with_context(|| format!("connect {endpoint}"))?)
            }
            #[cfg(windows)]
            {
                use tokio::net::windows::named_pipe::ClientOptions;
                let mut tries = 0;
                loop {
                    match ClientOptions::new().open(endpoint) {
                        Ok(c) => break Box::new(c),
                        Err(e) if e.raw_os_error() == Some(231) && tries < 50 => {
                            tries += 1;
                            tokio::time::sleep(std::time::Duration::from_millis(50)).await;
                        }
                        Err(e) => return Err(anyhow::Error::from(e).context(format!("connect {endpoint}"))),
                    }
                }
            }
        };
        let (rd, wr) = tokio::io::split(s);
        Ok(Self { rd: BufReader::new(rd), wr, next_id: 1, events: VecDeque::new() })
    }

    async fn read_msg(&mut self) -> Result<Value> {
        let mut s = String::new();
        let n = self.rd.read_line(&mut s).await?;
        if n == 0 {
            bail!("daemon closed the connection");
        }
        Ok(serde_json::from_str(s.trim())?)
    }

    pub async fn call(&mut self, method: &str, params: Value) -> Result<Value> {
        let id = self.next_id;
        self.next_id += 1;
        let req = json!({"jsonrpc":"2.0","id":id,"method":method,"params":params});
        self.wr.write_all(line(&req).as_bytes()).await?;
        self.wr.flush().await?;
        loop {
            let v = self.read_msg().await?;
            if v.get("method").is_some() {
                self.events.push_back(v["params"].clone());
                continue;
            }
            if v.get("id").and_then(|x| x.as_u64()) != Some(id) {
                continue;
            }
            if let Some(e) = v.get("error") {
                bail!("{} (code {})", e["message"].as_str().unwrap_or("error"), e["code"]);
            }
            return Ok(v["result"].clone());
        }
    }

    pub async fn subscribe(&mut self) -> Result<()> {
        self.call("subscribe", json!({})).await.map(|_| ())
    }

    pub async fn next_event(&mut self) -> Result<Value> {
        if let Some(e) = self.events.pop_front() {
            return Ok(e);
        }
        loop {
            let v = self.read_msg().await?;
            if v.get("method").is_some() {
                return Ok(v["params"].clone());
            }
        }
    }
}
