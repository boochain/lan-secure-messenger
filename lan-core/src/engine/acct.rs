//! v0.7.1 account-mode RPCs: setup / restore / pair / URI apply. See docs/account-mode.md.
use super::*;
use crate::account::{self, LsmUri};
use rand::RngCore;
use serde::Deserialize;
use serde_json::{json, Value};
use tokio::sync::oneshot;

fn p<T: for<'de> Deserialize<'de>>(v: Value) -> std::result::Result<T, RpcError> {
    serde_json::from_value(v).map_err(|e| RpcError::invalid(e.to_string()))
}

impl Engine {
    fn load_or_create_rk(&self) -> Result<String, RpcError> {
        if let Some(rk) = account::load_recovery_key(&self.paths) {
            return Ok(rk);
        }
        let rk = account::generate_recovery_key();
        account::save_recovery_key(&self.paths, &rk).map_err(|e| RpcError::new(-32021, format!("{e:#}")))?;
        Ok(rk)
    }

    fn set_account_local(&mut self, mode: &str, lookup: &str) -> Result<(), RpcError> {
        self.settings.account_mode = if mode == "account" { "account".into() } else { "device".into() };
        if !lookup.is_empty() {
            self.settings.account_user = lookup.to_string();
        }
        self.settings.save(&self.paths).map_err(|e| RpcError::new(-32000, e.to_string()))?;
        self.emit(Event::SettingsUpdated { settings: self.settings.for_ipc() });
        self.emit(Event::MeUpdated { me: self.me_view() });
        Ok(())
    }

    fn enable_relay(&mut self, host: &str, port: u16) -> Result<(), RpcError> {
        if host.trim().is_empty() {
            return Ok(());
        }
        self.settings.relay_enabled = true;
        self.settings.relay_host = host.trim().to_string();
        self.settings.relay_port = if port == 0 { 9443 } else { port };
        self.settings.save(&self.paths).map_err(|e| RpcError::new(-32000, e.to_string()))?;
        let _ = self.relay.send(relay::RelayCmd::Configure(relay_cfg(&self.settings)));
        self.emit(Event::SettingsUpdated { settings: self.settings.for_ipc() });
        Ok(())
    }

    pub(crate) fn rpc_acct(&mut self, method: &str, params: Value) -> Option<RpcResult> {
        Some(match method {
            "account_setup" => self.acct_setup(params),
            "account_set_mode" => self.acct_set_mode(params),
            "reveal_recovery_key" => self.acct_reveal(),
            "relay_uri" => {
                let host = self.settings.relay_host.clone();
                let port = self.settings.relay_port;
                Ok(json!({"uri": if host.is_empty() { String::new() } else { account::relay_uri(&host, port) }}))
            }
            _ => return None,
        })
    }

    fn acct_reveal(&self) -> RpcResult {
        match account::load_recovery_key(&self.paths) {
            Some(rk) => Ok(json!({"recovery_key": rk, "lookup": account::account_lookup(&rk).unwrap_or_default()})),
            None => Err(RpcError::invalid("还没有恢复密钥（请先开启账号模式）")),
        }
    }

    fn acct_set_mode(&mut self, params: Value) -> RpcResult {
        #[derive(Deserialize)]
        struct P { mode: String }
        let a: P = p(params)?;
        if a.mode == "account" {
            return self.acct_setup(json!({}));
        }
        self.set_account_local("device", "")?;
        Ok(json!({"ok": true, "account_mode": "device"}))
    }

    fn acct_setup(&mut self, params: Value) -> RpcResult {
        #[derive(Deserialize, Default)]
        struct P {
            #[serde(default)]
            relay_host: String,
            #[serde(default)]
            relay_port: u16,
        }
        let a: P = if params.is_null() || params.as_object().map(|o| o.is_empty()).unwrap_or(false) {
            P::default()
        } else {
            p(params).unwrap_or_default()
        };
        if !a.relay_host.is_empty() {
            self.enable_relay(&a.relay_host, a.relay_port)?;
        }
        let rk = self.load_or_create_rk()?;
        let lookup = account::account_lookup(&rk).map_err(|e| RpcError::invalid(e.to_string()))?;
        self.set_account_local("account", &lookup)?;
        // Best-effort upload if the relay is on; UI can retry via account_setup again.
        let uploaded = self.acct_try_upload(&lookup, &rk);
        Ok(json!({
            "ok": true,
            "account_mode": "account",
            "recovery_key": rk,
            "lookup": lookup,
            "uploaded": uploaded,
        }))
    }

    pub(crate) fn acct_maybe_reupload(&mut self) {
        if self.settings.account_mode != "account" {
            return;
        }
        let Some(rk) = account::load_recovery_key(&self.paths) else { return };
        let Ok(lookup) = account::account_lookup(&rk) else { return };
        let _ = self.acct_try_upload(&lookup, &rk);
    }

    fn acct_try_upload(&mut self, lookup: &str, rk: &str) -> bool {
        let _ = self.history.flush(true);
        let tmp = self.paths.root.join("account-backup.tmp");
        if crate::backup::export_backup(&self.paths, rk, &tmp).is_err() {
            return false;
        }
        let Ok(blob) = std::fs::read(&tmp) else { return false; };
        let _ = std::fs::remove_file(&tmp);
        let (tx1, rx1) = oneshot::channel();
        if self.relay.send(relay::RelayCmd::AccountRegister { user: lookup.to_string(), pass: String::new(), reply: tx1 }).is_err() {
            return false;
        }
        let (tx2, rx2) = oneshot::channel();
        if self.relay.send(relay::RelayCmd::AccountPutBackup { user: lookup.to_string(), blob, reply: tx2 }).is_err() {
            return false;
        }
        // Don't block the engine actor: fire-and-forget; callers that need certainty use account_restore tests
        // or wait on a follow-up. Engine tests that need upload wait via account_backup_put style RPC.
        tokio::spawn(async move {
            let _ = tokio::time::timeout(std::time::Duration::from_secs(45), rx1).await;
            let _ = tokio::time::timeout(std::time::Duration::from_secs(45), rx2).await;
        });
        true
    }

}

// The async RPCs need the engine's reply channel. They are started from engine.rs.
impl Engine {
    pub(crate) fn begin_account_restore(&mut self, params: Value, reply: tokio::sync::oneshot::Sender<RpcResult>) {
        #[derive(Deserialize)]
        struct P {
            recovery_key: String,
            #[serde(default)]
            relay_host: String,
            #[serde(default)]
            relay_port: u16,
        }
        let a: P = match p(params) {
            Ok(v) => v,
            Err(e) => { let _ = reply.send(Err(e)); return; }
        };
        let rk = match account::normalize_recovery_key(&a.recovery_key) {
            Ok(v) => v,
            Err(e) => { let _ = reply.send(Err(RpcError::invalid(e.to_string()))); return; }
        };
        if let Err(e) = account::save_recovery_key(&self.paths, &rk) {
            let _ = reply.send(Err(RpcError::new(-32021, format!("{e:#}"))));
            return;
        }
        let lookup = match account::account_lookup(&rk) {
            Ok(v) => v,
            Err(e) => { let _ = reply.send(Err(RpcError::invalid(e.to_string()))); return; }
        };
        if let Err(e) = self.enable_relay(&a.relay_host, a.relay_port) {
            let _ = reply.send(Err(e));
            return;
        }
        if let Err(e) = self.set_account_local("account", &lookup) {
            let _ = reply.send(Err(e));
            return;
        }
        let (tx, rx) = oneshot::channel();
        if self.relay.send(relay::RelayCmd::AccountGetBackup { user: lookup.clone(), reply: tx }).is_err() {
            let _ = reply.send(Err(RpcError::new(-32020, "中转未启用")));
            return;
        }
        let paths = self.paths.clone();
        tokio::spawn(async move {
            let r = tokio::time::timeout(std::time::Duration::from_secs(60), rx).await;
            let res = match r {
                Ok(Ok(Ok(blob))) => {
                    let tmp = paths.root.join("account-restore.tmp");
                    if let Err(e) = std::fs::write(&tmp, &blob) {
                        Err(RpcError::new(-32021, e.to_string()))
                    } else {
                        match crate::backup::import_backup(&paths, &rk, &tmp) {
                            Ok(()) => {
                                let _ = std::fs::remove_file(&tmp);
                                let _ = account::save_recovery_key(&paths, &rk);
                                Ok(json!({"ok": true, "restart_required": true, "lookup": lookup, "recovery_key": rk}))
                            }
                            Err(e) => {
                                let _ = std::fs::remove_file(&tmp);
                                Err(RpcError::new(-32021, format!("{e:#}")))
                            }
                        }
                    }
                }
                Ok(Ok(Err(e))) => Err(RpcError::new(-32020, e)),
                _ => Err(RpcError::new(-32020, "下载备份失败或超时")),
            };
            let _ = reply.send(res);
        });
    }

    pub(crate) fn begin_account_pair_start(&mut self, reply: tokio::sync::oneshot::Sender<RpcResult>) {
        let rk = match self.load_or_create_rk() {
            Ok(v) => v,
            Err(e) => { let _ = reply.send(Err(e)); return; }
        };
        let host = self.settings.relay_host.clone();
        let port = self.settings.relay_port;
        if host.is_empty() || !self.settings.relay_enabled {
            let _ = reply.send(Err(RpcError::invalid("请先连接中转服务器再配对新设备")));
            return;
        }
        let mut token = [0u8; 32];
        let mut wrap = [0u8; 32];
        rand::thread_rng().fill_bytes(&mut token);
        rand::thread_rng().fill_bytes(&mut wrap);
        let blob = match account::wrap_recovery(&rk, &wrap) {
            Ok(b) => b,
            Err(e) => { let _ = reply.send(Err(RpcError::new(-32021, format!("{e:#}")))); return; }
        };
        let uri = account::pair_uri(&host, port, &token, &wrap);
        let (tx, rx) = oneshot::channel();
        if self.relay.send(relay::RelayCmd::PairPut { token, ttl: 600, blob, reply: tx }).is_err() {
            let _ = reply.send(Err(RpcError::new(-32020, "中转未启用")));
            return;
        }
        tokio::spawn(async move {
            let r = tokio::time::timeout(std::time::Duration::from_secs(30), rx).await;
            let _ = reply.send(match r {
                Ok(Ok(Ok(()))) => Ok(json!({"ok": true, "uri": uri})),
                Ok(Ok(Err(e))) => Err(RpcError::new(-32020, e)),
                _ => Err(RpcError::new(-32020, "配对码上传失败或超时")),
            });
        });
    }

    pub(crate) fn begin_account_pair_accept(&mut self, params: Value, reply: tokio::sync::oneshot::Sender<RpcResult>) {
        #[derive(Deserialize)]
        struct P { uri: String }
        let a: P = match p(params) {
            Ok(v) => v,
            Err(e) => { let _ = reply.send(Err(e)); return; }
        };
        let parsed = match account::parse_lsm_uri(a.uri.trim()) {
            Ok(LsmUri::Pair { host, port, token, wrap_key }) => (host, port, token, wrap_key),
            Ok(_) => { let _ = reply.send(Err(RpcError::invalid("不是配对链接"))); return; }
            Err(e) => { let _ = reply.send(Err(RpcError::invalid(e.to_string()))); return; }
        };
        if let Err(e) = self.enable_relay(&parsed.0, parsed.1) {
            let _ = reply.send(Err(e));
            return;
        }
        let (tx, rx) = oneshot::channel();
        if self.relay.send(relay::RelayCmd::PairGet { token: parsed.2, reply: tx }).is_err() {
            let _ = reply.send(Err(RpcError::new(-32020, "中转未启用")));
            return;
        }
        let paths = self.paths.clone();
        let wrap = parsed.3;
        // After we unwrap RK, reuse restore against the same relay.
        let relay = self.relay.clone();
        tokio::spawn(async move {
            let r = tokio::time::timeout(std::time::Duration::from_secs(30), rx).await;
            let blob = match r {
                Ok(Ok(Ok(b))) => b,
                Ok(Ok(Err(e))) => { let _ = reply.send(Err(RpcError::new(-32020, e))); return; }
                _ => { let _ = reply.send(Err(RpcError::new(-32020, "配对数据获取失败或超时"))); return; }
            };
            let rk = match account::unwrap_recovery(&blob, &wrap) {
                Ok(v) => v,
                Err(e) => { let _ = reply.send(Err(RpcError::new(-32021, e.to_string()))); return; }
            };
            if let Err(e) = account::save_recovery_key(&paths, &rk) {
                let _ = reply.send(Err(RpcError::new(-32021, format!("{e:#}"))));
                return;
            }
            let lookup = match account::account_lookup(&rk) {
                Ok(v) => v,
                Err(e) => { let _ = reply.send(Err(RpcError::invalid(e.to_string()))); return; }
            };
            let (tx2, rx2) = oneshot::channel();
            if relay.send(relay::RelayCmd::AccountGetBackup { user: lookup.clone(), reply: tx2 }).is_err() {
                let _ = reply.send(Err(RpcError::new(-32020, "中转未启用")));
                return;
            }
            let r2 = tokio::time::timeout(std::time::Duration::from_secs(60), rx2).await;
            let res = match r2 {
                Ok(Ok(Ok(bak))) => {
                    let tmp = paths.root.join("account-restore.tmp");
                    if let Err(e) = std::fs::write(&tmp, &bak) {
                        Err(RpcError::new(-32021, e.to_string()))
                    } else {
                        match crate::backup::import_backup(&paths, &rk, &tmp) {
                            Ok(()) => {
                                let _ = std::fs::remove_file(&tmp);
                                Ok(json!({"ok": true, "restart_required": true, "lookup": lookup, "recovery_key": rk}))
                            }
                            Err(e) => {
                                let _ = std::fs::remove_file(&tmp);
                                Err(RpcError::new(-32021, format!("{e:#}")))
                            }
                        }
                    }
                }
                Ok(Ok(Err(e))) => Err(RpcError::new(-32020, e)),
                _ => Err(RpcError::new(-32020, "下载备份失败或超时")),
            };
            let _ = reply.send(res);
        });
    }
}
