# 局域网加密即时通讯（LAN Secure Messenger）

面向 Windows 局域网的端到端加密即时通讯：自动发现同网段对等端，支持加密文本 / 图片 / 文件传输，提供 CLI 与 GUI。

- **仓库**: https://github.com/FREC86/lan-secure-messenger
- **作者**: Bill Ocean (FREC86)
- **许可**: MIT

## 功能

- UDP 组播发现 + 在线/离线（Ed25519 签名公告；仅在状态变化时提示，心跳不再刷屏）
- **私聊（DM）**：按对等端会话线程、未读、历史；文本 / 文件 / 图片均走既有加密传输栈
- **群聊**：局域网群房间（建群 / 邀请 / 退群）；群共享密钥经既有 Noise 1:1 分发；成员列表由创建者签名；群文本与群文件
- 1:1 端到端加密文本（Noise XX：X25519 + ChaCha20-Poly1305）
- **文本送达回执**（`TextAck`）
- 分块文件 / 图片传输（进度、SHA-256 校验、**可取消**、最大体积可配置）
- **会话复用**：文本连接可池化复用，降低握手开销
- TOFU 信任 + 人类可读指纹核对
- **可选 PIN 配对**（`pair-offer` / `pair`）快速互信
- 向所有**已信任且在线**对等端安全广播（带成功/失败明细）
- 本地聊天历史（ChaCha20-Poly1305 静态加密；含群元数据 `groups.json`）
- GUI：**首次运行**仅需显示名；**系统托盘**（关窗后台、显示/退出）；私聊 / 群聊页签
- CLI（`lan-msg`）含 **`doctor`**、群聊子命令；一键下载即用（见下）



## 下载即用（Windows，免编译免配置）

> **启用自动构建：** 将 `packaging/ci/release.yml` 复制为 `.github/workflows/release.yml` 后推送（需具备 `workflow` 权限的 token）。此后推 `main` / 打 `v*` 标签即产出 Windows / Linux 发布包。



1. 打开 [Releases](https://github.com/FREC86/lan-secure-messenger/releases) 下载 `lan-secure-messenger-windows-x64.zip`  
   （配置 Actions 后，推送到 `main` 或打 `v*` 标签会自动构建并上传产物）
2. 解压后双击 **`启动.bat`**（或 `Start.bat` / `lan-gui.exe`）
3. 首次运行：自动生成密钥，设置**显示名称**即可开始；发现立即启动
4. Windows 防火墙提示时勾选**专用网络**允许一次
5. 关闭窗口后程序在**系统托盘**继续运行；托盘菜单可显示窗口 / 退出

无需安装 Rust、无需命令行参数。同目录另有 `lan-msg.exe` 供高级 CLI 使用。

Linux 开发机也可 `cargo build --release`；CI 另产出 `lan-secure-messenger-linux-x64.tar.gz`。

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

### 群聊威胁模型（诚实说明）

- 群共享密钥经 **Noise 1:1** 安全通道发给每位成员；创建者对「群 id + 名称 + 成员列表 + 密钥」签名
- 群消息由发送者用群密钥加密后，经 1:1 会话**扇出**给在线成员（非中心服务器）
- **无离线存转发**：成员离线则收不到该条；重新上线不会自动补历史（仅本地各自持久化已收消息）
- 被踢/退群后，旧密钥仍可能被恶意前成员用于解密其离线期间截获的密文（MVP 不做前向保密轮转）
- 邀请码便于识别群，**不替代**信任：只能邀请已 TOFU/PIN 信任的对等端

### 不在范围内

- 已入侵本机的恶意软件
- 互联网中继 / NAT 穿透（仅局域网）
- 匿名性（发现包含显示名与公钥）
- 持有受害者长期私钥的攻击者
- 群聊消息的全局一致性排序 / 对抗恶意成员篡改成员列表（依赖创建者签名 + 本地校验）

### 使用建议

- PIN 配对仅证明双方知道同一短码；请在可信环境下当面或带外确认 PIN，勿在不可信聊天中传递


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

推荐：**推送 `main` 或打 tag 后下载 Actions / Release 产物**（见「下载即用」）。

可选本地交叉编译（Linux + mingw）：

```bash
rustup target add x86_64-pc-windows-gnu
# Debian/Ubuntu: sudo apt install gcc-mingw-w64-x86-64
cargo build --release -p lan-cli -p lan-gui --target x86_64-pc-windows-gnu
# 打包参考 packaging/windows/
```

## 配置目录

| 平台 | 默认路径 |
|------|----------|
| Windows | `%APPDATA%\FREC86\lan-secure-messenger\`（`directories`：`ProjectDirs::from("com", "FREC86", "lan-secure-messenger")` → Windows `%APPDATA%\FREC86\lan-secure-messenger\`） |
| Linux | `~/.config/lan-secure-messenger/` |
| macOS | `~/Library/Application Support/com.FREC86.lan-secure-messenger/` |

内容：`identity.json`、`peers.json`、`groups.json`、`history.enc`、`history.key`、`settings.json`、`downloads/`

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

# 历史 / 改名 / 诊断
lan-msg history --limit 20
lan-msg history <对端指纹> --limit 20   # 私聊线程
lan-msg set-name "我的电脑"
lan-msg doctor

# 群聊
lan-msg create-group "项目组" --members <指纹1>,<指纹2>
lan-msg groups
lan-msg group-send <群UUID> "大家好"
lan-msg group-send-file <群UUID> ./doc.pdf
lan-msg group-invite <群UUID> <指纹>
lan-msg group-leave <群UUID>
lan-msg group-history <群UUID>
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

- **首次运行**：自动生成身份，输入显示名即可；无需其它配置
- 页签：**私聊** / **群聊**
- 私聊：左侧好友列表（未读角标）→ 会话区发文本 / 文件；支持广播
- 群聊：新建群（勾选已信任成员）→ 发群消息 / 文件；可邀请更多已信任成员；邀请码展示
- 新设备弹窗：核对指纹后信任或拉黑
- **托盘**：关闭窗口进入后台；托盘点击或「显示窗口」恢复；「退出」彻底退出

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

- 会话可复用，但非完整多路复用长连接
- 群聊无离线补发、无 sender-keys 轮转（见威胁模型）
- GUI 图片按文件传输，无内联大图预览
- 跨子网需组播路由 / 自行改发现地址
- Linux 托盘依赖 AppIndicator；无托盘环境可用 `--no-default-features` 构建 GUI

## License

MIT — 见 [LICENSE](./LICENSE)
