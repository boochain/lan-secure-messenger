# 局域网加密即时通讯（LAN Secure Messenger）

面向 Windows 局域网的端到端加密即时通讯：自动发现同网段对等端，支持加密文本 / 图片 / 文件传输，提供 CLI 与 GUI。

- **仓库**: https://github.com/FREC86/lan-secure-messenger
- **作者**: Bill Ocean (FREC86)
- **许可**: MIT

## 功能

- UDP 组播发现 + 在线/离线（Ed25519 签名公告，防伪造 / 重放）
- 1:1 端到端加密文本（Noise XX：X25519 + ChaCha20-Poly1305）
- 分块文件 / 图片传输（进度、SHA-256 校验）
- TOFU 信任 + 人类可读指纹核对
- 向所有**已信任且在线**对等端安全广播
- 本地聊天历史（ChaCha20-Poly1305 静态加密）
- 显示名、对等端列表、传输历史
- CLI（`lan-msg`）+ GUI（`lan-gui`，egui，支持拖放发文件）

## 架构

```
lan-secure-messenger/
├── lan-core/   # 发现、密码学、协议、会话、传输、历史
├── lan-cli/    # clap CLI → lan-msg
└── lan-gui/    # egui/eframe → lan-gui
```

| 层次 | 说明 |
|------|------|
| 发现 | UDP 组播 `239.255.42.99` + 可选广播；公告含公钥、端口、时间戳、nonce、Ed25519 签名 |
| 会话 | TCP + Noise_XX_25519_ChaChaPoly_BLAKE2s；握手后核对静态公钥指纹 |
| 应用消息 | 文本 / 广播 / 文件报价 / 分块 / ACK |
| 身份 | 长期 X25519 + Ed25519，存于用户配置目录（Unix 权限 0600） |
| 信任 | TOFU：首次见面核对指纹后标记 Trusted |

## 威胁模型

### 保护目标

- 局域网窃听者无法阅读文本 / 文件内容（E2E）
- 伪造发现包难以冒充已签名身份
- 已核对指纹并信任后，握手静态密钥不匹配则失败（缓解 MITM）
- 发现重放：nonce 缓存 + 时间窗
- 本地历史静态加密（防简单拷贝窥探；无法防本机恶意软件读密钥文件）

### 不在范围内

- 已入侵本机的恶意软件
- 互联网中继 / NAT 穿透（仅局域网）
- 匿名性（发现包含显示名与公钥）
- 持有受害者长期私钥的攻击者

### 使用建议

1. 首次见面**当面核对指纹**再信任
2. 不可信网络不要开启 `--auto-trust`
3. 日志不输出私钥

## 构建

需要 **Rust stable**（推荐 1.85+；本仓库 `rust-toolchain.toml` 使用 stable）。

### Linux（开发 / 冒烟）

```bash
cargo build --release -p lan-cli
cargo build --release -p lan-gui   # 需要图形相关系统库
```

产物：`target/release/lan-msg`、`target/release/lan-gui`

### Windows x64

1. 安装 [Rust](https://rustup.rs/)（默认 `x86_64-pc-windows-msvc`）
2. 安装 Visual Studio Build Tools（C++ 桌面工作负载）
3. PowerShell：

```powershell
git clone https://github.com/FREC86/lan-secure-messenger.git
cd lan-secure-messenger
cargo build --release -p lan-cli -p lan-gui
```

产物：

- `target\release\lan-msg.exe`
- `target\release\lan-gui.exe`

可选（Linux 交叉编译，需自行配置链接器）：

```bash
rustup target add x86_64-pc-windows-msvc
# 或 gnu 工具链
rustup target add x86_64-pc-windows-gnu
cargo build --release -p lan-cli --target x86_64-pc-windows-gnu
```

## 配置目录

| 平台 | 默认路径 |
|------|----------|
| Windows | `%APPDATA%\lan-secure-messenger\`（`directories` crate：`com.FREC86`） |
| Linux | `~/.config/lan-secure-messenger/` |
| macOS | `~/Library/Application Support/com.FREC86.lan-secure-messenger/` |

内容：`identity.json`、`peers.json`、`history.enc`、`history.key`、`settings.json`、`downloads/`

同机多实例请用 `--config-dir` 或环境变量 `LAN_MSG_CONFIG` 隔离。

## CLI 用法

```bash
# 本机身份
lan-msg --name Alice id

# 守护进程（发现 + 收消息）
lan-msg --name Alice daemon

# 列出对等端
lan-msg peers --wait 3

# 信任 / 拉黑
lan-msg trust ABCD-EFGH-IJKL-MNOP
lan-msg block ABCD-EFGH-IJKL-MNOP

# 发文本 / 文件
lan-msg send ABCD-EFGH-IJKL-MNOP "你好"
lan-msg send-file ABCD-EFGH-IJKL-MNOP ./photo.png

# 向已信任在线节点广播
lan-msg broadcast "会议室集合"

# 历史 / 改名
lan-msg history --limit 20
lan-msg set-name "我的电脑"
```

### 常用参数 / 环境变量

| CLI | 环境变量 | 含义 |
|-----|----------|------|
| `--name` | `LAN_MSG_NAME` | 显示名 |
| `--config-dir` | `LAN_MSG_CONFIG` | 配置目录 |
| `--bind` | `LAN_MSG_BIND` | 绑定地址（默认 `0.0.0.0`） |
| `--discovery-port` | `LAN_MSG_DISCOVERY_PORT` | 发现端口（默认 41234） |
| `--listen-port` | `LAN_MSG_LISTEN_PORT` | TCP 端口（默认 41235） |
| `--broadcast` | `LAN_MSG_BROADCAST` | 发现发送目标覆盖（测试用） |
| `--auto-trust` | `LAN_MSG_AUTO_TRUST` | 自动信任（仅可信环境） |

### 同机双进程冒烟（Linux）

```bash
./scripts/smoke_test.sh
# 或手动：
cargo build -p lan-cli
# 终端 A
./target/debug/lan-msg --config-dir /tmp/lsm-a --name Alice \
  --bind 0.0.0.0 --discovery-port 41234 --listen-port 41235 --auto-trust daemon
# 终端 B
./target/debug/lan-msg --config-dir /tmp/lsm-b --name Bob \
  --bind 0.0.0.0 --discovery-port 41234 --listen-port 41236 --auto-trust daemon
# 另开终端从 A 发消息（先查 B 指纹）
./target/debug/lan-msg --config-dir /tmp/lsm-a --listen-port 41246 --auto-trust \
  send <B指纹> "hello"
```

发现使用 UDP **组播**（`239.255.42.99`），同机多进程可共用发现端口；TCP 监听端口必须不同。

## GUI 用法

```bash
lan-gui
LAN_MSG_NAME=Alice LAN_MSG_CONFIG=/tmp/lsm-gui lan-gui
```

- 左侧：对等端（在线 / 信任）
- 中间：会话；发送文本；「发文件」或拖放
- 新设备弹窗：核对指纹后信任或拉黑
- 「广播」：所有已信任在线对等端

## 密码学

| 用途 | 算法 / 库 |
|------|-----------|
| 握手与会话 | Noise XX（`snow`）X25519 + ChaCha20-Poly1305 + BLAKE2s |
| 发现签名 | Ed25519（`ed25519-dalek`） |
| 指纹 | SHA-256(X25519 公钥) 前 8 字节 → `XXXX-XXXX-XXXX-XXXX` |
| 历史静态加密 | ChaCha20-Poly1305 |
| 文件完整性 | SHA-256 |

不自行实现密码学原语。

## 防火墙（Windows）

允许专用网络入站：

- UDP `41234`（发现，可配置）
- TCP `41235`（会话，可配置）

## 已知限制（MVP）

- 每次发消息新建 TCP/Noise 会话（简单可靠，非长连接多路复用）
- 无群组加密房间（广播=分别向多对端发送）
- GUI 图片按文件传输，无内联大图预览
- 跨子网需组播路由 / 自行改发现地址

## License

MIT — 见 [LICENSE](./LICENSE)
