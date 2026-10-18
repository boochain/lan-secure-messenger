# relay-server — LSM 中转服务器 (optional relay)

可选的中转服务器：帮助不在同一网络的客户端互相连接。它**只转发端到端 Noise 加密的数据**，看不到明文。
Optional relay that only forwards end-to-end Noise-encrypted blobs. Protocol: [`docs/relay-protocol.md`](../docs/relay-protocol.md).

## 快速开始 (quick start)

| 系统 | 命令 |
|---|---|
| Linux | `sudo ./deploy.sh` (菜单) / `sudo ./deploy.sh install --port 9443 --gen-password` |
| Windows / Windows Server (含 Server Core) | 管理员运行 `deploy.bat` / `deploy.bat install -Port 9443 -GenPassword` |
| 直接运行 (bare) | `relay-server --port 9443 --password '…'` / `relay-server --config relay.toml` |

Menu: 1 临时运行 · 2 持久运行 · 3 安装开机自启服务 · 4 删除开机自启 · 5 停止 · 6 修改端口/口令 · 7 查看状态 · 8 卸载 · 0 退出.
Non-interactive commands: `run start install upgrade remove-autostart stop config status uninstall version`;
flags `--port --password --gen-password --bind --open-firewall --yes --dry-run --no-start` (PowerShell: `-Port -Password -GenPassword -Bind -OpenFirewall -Yes -DryRun -NoStart -NoService`).

* 防火墙绝不自动修改：未启用则不碰；已启用则询问（默认否），非交互需 `--open-firewall`；卸载只删除脚本自己建的规则。
  云服务器还需在控制台安全组放行端口。
* 升级：重新运行脚本即可，会自动检测旧版本/残留（旧二进制、旧服务/计划任务、过期 PID、运行中的旧进程），停止、备份配置、保留端口和口令、替换并重启，并显示 旧版本 → 新版本。
* CPU 自动识别: Linux x86_64 / aarch64 / armv7 / i686 / riscv64 (static musl); Windows x64 / arm64 / x86.

## CLI

```
relay-server [--config FILE] [--port N] [--password P] [--bind ADDR] [--pid-file F] [--log-level L]
relay-server --status [--config FILE | --port N]     # JSON: online, connections, version, …
relay-server --version | --check-config
```
Config: see [`relay.example.toml`](relay.example.toml). Passphrase may also come from `RELAY_PASSWORD`.

## 开发与测试 (dev & tests)

```
cargo test --release -- --nocapture          # unit + integration (incl. C interop, 200 MB stream, 50 senders, fuzz)
RELAY_STRESS_SECS=180 cargo test --release stress -- --nocapture
make -C c-client && c-client/relay_client --selftest
bash tests/deploy_test.sh target/release/relay-server target/release/relay-test-client
powershell -File tests/deploy_test.ps1 -NewBin … -Cli …   # elevated
```
