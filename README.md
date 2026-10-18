# 局域网加密即时通讯（LAN Secure Messenger）

无服务器、无账号的局域网聊天工具：同一局域网内的电脑自动互相发现，文本、图片、文件、群聊全部端到端加密。

> **v0.5 是一次完整重构**（Rust 后台服务 + WinUI 3 界面），修复了 v0.3.x 在真实 Windows 上
> “必须关闭防火墙才能用 / 界面乱码 / 消息发不出去 / 看不到对方 / 一方能看到另一方、另一方列表为空”等问题。
> 旧的 egui 界面已移除。v0.3.x 代码保留在 `main` 分支的历史中。

## 仓库结构（v0.6 起：所有版本在同一仓库）

从 v0.6 开始，Windows（WinUI）版、Windows XP 版、Linux 版、Android 版和中转服务器都在本仓库中，
每次发布在同一个 Release 里提供全部安装包。原 `lan-secure-messenger-xp` 仓库以 git subtree 方式并入
`editions/portable/`，**完整保留提交历史**（`git log -- editions/portable`）。

```
.                         Rust 工作区（Windows 版的核心与后台服务）
├── lan-core/             Rust 核心：发现、Noise XX 加密、存储、传输、续传、中转客户端
├── lan-daemon/  lan-cli/ 后台服务 lan-daemon.exe 与命令行 lan-msg
├── ui-winui/             WinUI 3 界面（Windows 10/11，x64 / arm64）
├── relay-server/         中转服务器（Rust）+ 部署脚本 deploy.sh / deploy.ps1 / deploy.bat
├── editions/
│   └── portable/         C 语言核心与轻量版本（原 lan-secure-messenger-xp）
│       ├── src/          共享 C 核心 core.c / relay.c / resume.c / xcrypto.c、CLI，以及 XP Win32 界面 gui.c
│       ├── linux/        Linux：GTK 4 + libadwaita 界面（adw_gui.c，AppImage）与 GTK 3 兼容界面（gtk_gui.c）
│       ├── android/      Android 应用（Kotlin / Compose，JNI 调用同一份 C 核心）
│       ├── reference/v0.5  互通测试用的 v0.5 Rust 后台服务源码快照
│       └── tests/  tools/  互通矩阵 matrix.py、截图与配色审计等
├── tools/                配色审计、sync-from-xp.sh、gen-portable-workflow.py
├── scripts/              Windows 冒烟测试
└── docs/                 协议（protocol.md、relay-protocol.md）、IPC、发布说明
```

为什么 C 版本整体放在 `editions/portable/` 而不是拆成 `core-c/`、`editions/xp/`、`editions/linux/`……：
XP、Linux、Android 三个版本共用同一份 C 核心和同一套 Makefile / 测试脚本，内部用相对路径互相引用；
整体并入能保持构建脚本不变，并且在迁移期间可以用 `tools/sync-from-xp.sh` 一条命令把 xp 仓库的新提交
（`git subtree pull`）同步过来。全部同步完成、旧仓库归档后，可以再按需细分目录。

### CI

`.github/workflows/ci.yml` 是唯一的入口（push / PR / 手动），依次调用：

| 工作流 | 内容 |
|------|------|
| `windows.yml` | Rust 核心测试、lan-daemon / lan-msg、WinUI 3 x64（浅色 / 深色界面冒烟 + 配色审计）、arm64 打包 |
| `portable.yml` | XP exe（i686 / x64，XP 导入检查，wine 互通）、Linux GTK 3 AppImage + tar 包（多发行版启动）、GTK 4 AppImage、Android APK（模拟器矩阵、截图审计） |
| `relay-server.yml` | 中转服务器：Linux musl x86_64 / aarch64 / armv7 / i686 / riscv64，Windows x64 / arm64 / x86，部署脚本测试 |
| `relay-client-matrix.yml` | Rust ↔ C ↔ Android 互通矩阵：局域网直连、经中转服务器、断点续传、传输中途切换线路 |

`portable.yml` 由 `tools/gen-portable-workflow.py` 根据 `editions/portable/.github/workflows/xp.yml` 生成，请勿手改。
只有手动推送的 `v*` 标签才会运行 `release` 任务，把以上所有安装包和 `SHA256SUMS.txt` 收进同一个 Release
（说明取自 `docs/release-notes-<标签>.md`；标签带 `-` 时为预发布）。

迁移说明：旧的 `lan-secure-messenger-xp` 仓库归档前，在其 README 顶部放入 `docs/xp-repo-readme-pointer.md` 中的说明。

## 下载即用（Windows 10 1809+ / 11，x64）

1. 到 [Releases](https://github.com/boochain/lan-secure-messenger/releases) 下载 `LanMessenger-<版本>-win-x64.zip`
2. **解压整个文件夹**，双击 `LanMessenger.exe`（或 `启动.bat`）。自带 .NET 8 与 Windows App SDK，无需安装任何运行库
3. 首次运行：填写显示名称 → 点击「一键配置防火墙」（弹出一次 UAC 管理员确认）
4. 其他电脑同样打开即可互相看到；看不到时在左上角输入对方 IP 直接添加，或查看「诊断」页

压缩包内容：

| 文件 | 说明 |
|------|------|
| `LanMessenger.exe` | WinUI 3 图形界面（Mica、导航：聊天 / 群组 / 传输 / 设置 / 诊断，托盘图标，系统通知） |
| `lan-daemon.exe` | 后台服务：网络、加密、存储；界面会自动启动它，关闭窗口后仍在托盘/后台接收消息 |
| `lan-msg.exe` | 命令行客户端（`lan-msg --help`） |
| `启动.bat` / `Start.bat` / `配置防火墙.bat` / `停止.bat` | 便捷脚本 |

## 功能

- 私聊、群聊（创建者签名成员列表；移除成员自动换群密钥）
- 文件 / 图片：进度、取消、重试、SHA-256 校验，图片在聊天中直接预览；支持拖拽发送
- 送达 / 已读回执；对方离线时消息排队，上线后自动补发（去重，保证只送达一次）
- TOFU 指纹信任 + PIN 配对；屏蔽；免打扰
- 本地加密聊天记录 + 搜索；密码加密的备份 / 恢复（Argon2id）
- 更新检查（GitHub Releases，可关闭）

## 架构（v0.5）

```
LanMessenger.exe (WinUI 3, C#)        lan-msg.exe (CLI)
        │  JSON-RPC over named pipe  \\.\pipe\lan-secure-messenger-<用户名>（仅当前用户可访问）
        ▼
lan-daemon.exe (Rust, lan-core)
  ├─ 连接管理 actor：每个联系人一条长期 TCP 连接（Noise XX 加密），文本/回执/文件块/群消息/在线状态全部多路复用
  │    心跳 5 s，20 s 无响应判定断线；指数退避自动重连；同时互拨时按指纹确定性保留一条
  ├─ 对称在线：A 通过任何途径发现 B → A 主动连 B → 握手成功后双方同时显示在线（在线 = 连接状态）
  ├─ 发现：每块真实网卡发送定向广播（跳过回环、169.254、Hyper-V/vEthernet、VMware、VirtualBox、WSL、Docker、TAP/VPN，可覆盖）
  │    + 可选组播 + 收到公告后单播回复 + 手动按 IP 添加（持久化）+ 启动时重连已信任联系人的上次地址
  ├─ 防火墙：`lan-daemon firewall-setup` 通过一次 UAC 调用 netsh 为本程序添加入站规则（专用/域网络，TCP+UDP）；检测“公用网络”
  ├─ 存储：%APPDATA%\FREC86\lan-secure-messenger\{config,data,logs}
  └─ 日志：logs\daemon.*.log 按天滚动保留 7 天；崩溃写 crash.log
```

- IPC 协议见 [docs/ipc.md](docs/ipc.md)（请求/响应 + 事件订阅 + 全量快照；Linux 上为 Unix socket）
- 界面只根据 daemon 的快照和事件渲染，不自己维护网络状态
- Rust 可执行文件静态链接 CRT（`+crt-static`），只依赖系统 DLL

## 威胁模型

### 保护目标

- 局域网窃听者无法阅读文本 / 文件内容（E2E）
- 伪造发现包难以冒充已签名身份
- 已核对指纹并信任后，握手静态密钥不匹配则失败（缓解 MITM）
- 发现包重放：发现包只触发一次拨号，真正的身份认证在 Noise 握手中完成，重放无法冒充
- 本地历史静态加密（防简单拷贝窥探；无法防本机恶意软件读密钥文件）

### 群聊威胁模型（诚实说明）

- 群共享密钥经 **Noise 1:1** 安全通道发给每位成员；创建者对「群 id + 名称 + 成员列表 + 密钥」签名
- 群消息由发送者用群密钥加密后，经 1:1 会话**扇出**给在线成员（非中心服务器）
- **无离线存转发**：成员离线则收不到该条；重新上线不会自动补历史（仅本地各自持久化已收消息）
- 移除成员后创建者自动轮换群密钥（rekey），被移除者无法解密之后的消息；但其离线期间截获的旧密文仍可被旧密钥解密
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
2. 不可信网络不要开启 `auto_trust`
3. 日志不输出私钥

## 密码学

| 用途 | 算法 / 库 |
|------|-----------|
| 握手与会话 | Noise XX（`snow`）X25519 + ChaCha20-Poly1305 + BLAKE2s |
| 发现签名 | Ed25519（`ed25519-dalek`） |
| 指纹 | SHA-256(X25519 公钥) 前 8 字节 → `XXXX-XXXX-XXXX-XXXX` |
| 历史静态加密 | ChaCha20-Poly1305 |
| 文件完整性 | SHA-256 |

不自行实现密码学原语。

## 构建

```bash
# Rust 核心 + CLI（Linux / Windows）
cargo build --release -p lan-daemon -p lan-cli
cargo test -p lan-core                 # 单元 + 集成测试（两个引擎走真实回环 TCP）
./scripts/smoke_test.sh                # 两个真实 daemon：单向发现、双向消息、文件、断线重连（PARTITION=1 再测静默断网）
./scripts/stress_test.sh               # 5 节点全互联，2000 条消息 + 并发大文件

# C# IPC 客户端测试（Linux 上即可运行，连接真实 Rust daemon）
cd ui-winui && dotnet test LanMessenger.Core.Tests

# WinUI 3 界面（仅 Windows）
dotnet publish ui-winui/LanMessenger/LanMessenger.csproj -c Release -r win-x64 -p:Platform=x64 -o publish
```

Windows 便携包由 `.github/workflows/windows.yml` 在 `windows-latest` 上构建：msvc 编译 + 导入表检查 + 防火墙规则测试 +
双 daemon 命名管道冒烟 + C# 测试 + WinUI 发布 + 界面冒烟截图；推送 `v*` 标签时自动创建预发布。

## CLI 用法（lan-msg）

```bash
lan-daemon                       # 前台运行后台服务（界面会自动启动它）
lan-msg status                   # 本机身份、端口
lan-msg peers                    # 联系人与在线状态
lan-msg add 192.168.1.23         # 按 IP[:端口] 添加（跨网段 / 广播被拦截时）
lan-msg send <名称|指纹> "你好"
lan-msg send-file <名称|指纹> ./a.zip
lan-msg trust <指纹>  |  lan-msg pin-offer  |  lan-msg pin-pair <指纹> <PIN>
lan-msg group-create "项目组" <成员...>  |  lan-msg send-group <群> "消息"
lan-msg history <对象>  |  lan-msg search 关键字
lan-msg diag                     # 网卡、防火墙、网络类型、每个联系人的连接状态（JSON）
lan-msg firewall-setup           # 配置 Windows 防火墙（UAC）
lan-msg watch                    # 实时事件流
```

`--ipc <管道或socket>` / 环境变量 `LSM_IPC` 可连接非默认实例；`lan-daemon --data-dir <目录> --set key=JSON` 可多实例测试。

## 端口

- UDP `41234`：发现广播
- TCP `41235`：加密连接（被占用时自动换端口，通过发现包告知对方）

## 已知限制

- 仅局域网：跨网段需要手动按 IP 添加或配置额外发现目标
- 群聊无离线补发（成员离线期间的群消息不会补发）
- 游戏 / 共享文件夹功能（v0.4 分支）尚未移植到新架构

## License

MIT — 见 [LICENSE](./LICENSE)
