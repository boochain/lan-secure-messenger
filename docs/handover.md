# 接手说明

写给下一个 agent。从线协议往上读。不要从界面、不要从旧网站、不要从聊天记录开始改。

仓库：`boochain/lan-secure-messenger`。已发布版本看仓库根目录的 `VERSION`（写这份说明时是 **0.7.3**）。Android 安装包的开发者证书在提交 `bbe7ee3`。README 开头那句「v0.6」是过时的，以 `VERSION` 和本文件为准。`https://lan-messenger-kmzf.pages.grok.bot/` 是 v0.6 的手册，只能当历史，不能当事实。仓库里没有的文件、帧、数据目录，都还没实现。先搜索再写。

## 0. 接到任务先做这几步

1. 读 `VERSION`、本文件、你要改的那个文件，以及挡住它的测试。
2. CI 红了：先把失败 job 的日志和截图制品下下来，对着报错改。不要没读日志就改代码，也不要在没确认原因时重跑同一套改动。
3. 行为变了，先改拥有这个行为的那一层，再改界面。顺序是：线协议（两边一起）→ Rust `lan-core` 或 C `editions/portable/src` → 五个 UI。只改一个界面对端就不通。
4. 用户没明确说「发版」之前，不要打 `v*` 标签，不要 `gh release create`。标签会跑完整 CI，并把所有安装包收进同一个 Release。
5. 五个界面功能必须互通。外壳（导航、菜单、窗口装饰）可以按设备不同。

## 1. 最低一层：两套引擎，一条线，一个不参与加密的中转

这不是五个各自联网的 App。线上只有一条协议，两套实现必须字节级一致：

| 实现 | 谁在用 | 位置 |
|---|---|---|
| Rust | Windows 10/11。`lan-daemon` 做网络和存储，WinUI 只渲染 | `lan-core/`、`lan-daemon/`、`lan-cli/`、`ui-winui/` |
| C | Windows XP、Linux GTK 3、Linux GTK 4/libadwaita、Android（JNI） | `editions/portable/src`（`core.c`、`relay.c`、`xcrypto.c`），界面在 `src/gui.c`、`linux/gtk_gui.c`、`linux/adw_gui.c`、`android/` |
| 中转 | 可选的自建转发进程，不是会话的一方 | `relay-server/` |

WinUI 不自己持有连接。它用 JSON-RPC 过命名管道 `\\.\pipe\lan-secure-messenger-<用户名>` 问 daemon。Linux 上同一份 IPC 走 Unix socket。界面只根据快照和事件画，不自己维护在线状态。IPC 见 `docs/ipc.md`。

中转看不到明文。它转发 Noise 已经加密的帧，也可以暂存客户端再封了一层的密文。它不参与 Noise 握手。

`Cargo.toml` 的 workspace 只有 `lan-core`、`lan-daemon`、`lan-cli`。`relay-server` 是独立 crate，不在这个 workspace 的 members 里时以它自己的 `Cargo.toml` 为准，改依赖前先打开文件。

## 2. 字节怎么编码

权威说明是 `docs/protocol.md`。这里只放不看就会改坏互通的规则。

P2P 应用协议版本 `PROTO_VERSION = 5`。结构用 bincode 1.x 默认的 fixint、小端。解码限制 1 MiB，允许尾部多余字节。

| 类型 | 线上 |
|---|---|
| 整数 | 固定宽度，小端 |
| `String` / `Vec<u8>` | `u64` 长度 + 内容 |
| `Uuid` | `u64` = 16，然后 16 字节 |
| `Option` | 1 字节，0 无，1 有 |
| enum | **`u32` 变体序号**，然后按声明顺序的字段 |
| struct | 按声明顺序，没有字段名 |

enum 的序号就是协议。**禁止在中间插入变体。** 新能力加在枚举最后，并且只发给声明了对应 capability 的对端。旧实现对尾部多余字节是忽略的，所以 Hello 后面可以追加内容，不能改前面的布局。

TCP 帧外壳和 bincode 不是同一套字节序：每一帧是 **大端 `u32` 长度** + 载荷，长度 1..=65539。长度是大端，载荷里的整数是小端。混用会握手失败。

## 3. 身份

每个安装有一把 Noise 用的 X25519 静态密钥，和一把给发现包、群邀请、rekey 签名的 Ed25519 密钥。

指纹 = `SHA-256(X25519 公钥)` 的前 8 字节，大写十六进制，四位一组，例如 `1A2B-3C4D-5E6F-7081`。联系人、群成员、会话 id（`dm:<指纹>`、`group:<uuid>`）都用这个指纹，不用显示名当主键。显示名可以改，用 `Name` 帧推送。

指纹对上才是同一个人。重装而没有恢复备份，就会变成新指纹。

## 4. 发现不是在线

UDP，默认端口 **41234**。

包 = 4 字节 `LSM5` + bincode(`SignedAnnounce`)。签名是对 body 的 Ed25519，必须 64 字节。`Announce.proto` 必须是 5。不检查时间戳。自己的指纹丢掉。

发现包只是拨号提示：去 `来源 IP : tcp_port` 拨 TCP。**在线 = 已经完成 Noise 握手的 TCP 连接**，不是「刚才收到过广播」。

默认每 3 秒从每块可用 IPv4 网卡发：子网广播、`255.255.255.255`、组播 `239.255.42.99`（TTL 1），以及配置的单播目标。跳过回环、169.254、常见虚拟网卡。对方 `want_reply` 时，最多每 10 秒单播回一次。

TCP 默认听 **41235**。被占用就换端口，并通过发现包告诉对方。

## 5. 连接

Noise 模式：`Noise_XX_25519_ChaChaPoly_BLAKE2s`，空 prologue，无 PSK。拨号方是 initiator。三次握手载荷都是空的。

传输用 snow 的无状态模式：每个方向 nonce 从 0 递增。一条传输消息就是一个 bincode 值。握手加第一条 Hello 必须在 10 秒内完成。连到自己的指纹就丢掉。

双方握手后的第一条消息是 `Hello`：`proto`、应用版本、显示名、Ed25519 公钥、监听端口。`proto != 5` 就断开。远端指纹来自 Noise 静态密钥，不是来自 Hello 里的名字。

Hello 的 bincode 后面可以有可选的 `caps: u32` 小端。没有这段就是 0。旧对端忽略尾部。

| 位 | 名字 | 含义 |
|---|---|---|
| 1 | `CAP_RESUME` | 按字节偏移续传 |
| 2 | `CAP_PROFILE` | 资料 |
| 4 | `CAP_EXT` | 扩展 |
| 8 | `CAP_ACCOUNT` | 账号模式 URI / 配对。不向没声明这一位的对端发新帧 |

需要能力位的帧，只发给声明了该位的对端。

心跳：每 5 秒 `Ping`，对方 `Pong` 回同一个时间戳。20 秒没有任何帧就判定死亡，然后退避重连，上限 30 秒。两边同时拨号时，指纹字典序较小的一方发起的那条留下。`Bye` 是正常关闭。

后续帧的序号（`docs/protocol.md` 第 4 节有完整字段）：

| 序号 | 帧 | 干什么 |
|---|---|---|
| 0–1 | Ping / Pong | 心跳 |
| 2 | Name | 改显示名 |
| 3 | Msg | 一条消息，里面是 Payload |
| 4 | Ack | 收到了这个 id |
| 5–12 | FileOffer … FileCancel | 文件，见第 8 节 |
| 13–14 | PinPair / PinResult | PIN 配对 |
| 15 | Bye | 关闭 |
| 16 | FileResume | 只发给 `CAP_RESUME` |

`Msg` 里的 Payload 同样是序号：0 Text，1 GroupText，2 GroupInvite，3 GroupRekey，4 GroupLeave，5 Read。

## 6. 消息怎么保证送到一次

`Msg` 进每个对端一个发件箱。链路在就发，重连再发，直到 `Ack`。接收方对每个 id 都回 `Ack`，但同一个 id 只处理一次（约 2 万条的已见集合，加上历史）。状态是：排队 → 已发送 → 已送达（Ack）→ 已读（`Read`）。

已读是用户打开会话时另发一条 `Msg{Read}`，不进发件箱，丢了就算了。群消息按成员扇出，**同一条消息用同一个 id**。

## 7. 信任是发文件和进群的闸门

这是最常被 CI 和真机同时踩中的规则。

默认 `auto_trust` 关闭。信任只有两条路：当面核对指纹后手动信任，或 PIN。PIN 是 6 位，300 秒，最多错 5 次。A 出示 PIN，B 在已建立的链路上发 `PinPair`，A 回 `PinResult`。成功则双方互相信任。PIN 只证明两边知道同一个短码，不是指纹核对。

**没信任就不能收文件，也不能接受群邀请。** C 核心在发送侧的原文是：

> 对方尚未信任，无法发送图片/文件。请先在联系人菜单中信任对方（或完成 PIN 配对）。

接收侧对未信任的发送者回 `FileReject`。所以截图测试里如果先发文件、后点信任，图里看到的是拒收，不是传输进度。GTK 浅色「文件传输」审计曾经因此整页红掉：测试脚本用过时的菜单方向键，点进了恢复密钥复选框，而不是传输页。

现行测试约定（改 `linux_gui_shots.py` 之前先读它，不要恢复「向下按四次」）：

- 环境变量 `LSM_UI_TEST`。
- GTK：`Ctrl+Shift+X` 打开真正的传输窗口（`gtk_gui.c` 里 `show_transfers()`）。菜单在「更多 → 文件与系统」里，用方向键计数会点到恢复密钥。
- 信任走真实菜单项「信任当前联系人」，成功时 stderr 打 `UITEST trust ok <指纹>`。
- 配色审计会量像素。缺字（`.notdef`，例如用了 `·` 而字体没有）和浅色未选中复选框的浅灰边，都会被当成一块不合格的颜色。先看失败图的坐标，再改 CSS。

## 8. 文件

`FileOffer` 带整文件 SHA-256、mime、可选群 id，以及聊天消息 id。群里每个成员一条 offer，tid 不同，msg id 相同。

接收方：发送者未信任、超过大小限制、或群不存在 / 发送者不是成员 → `FileReject`。否则 `FileAccept`，写入 `.part`。

发送方：60 KiB 一块 `FileChunk`（`CHUNK = 60*1024`），连续偏移。未确认数据超过 16 MiB 就等 `FileProgress`，45 秒无进展就超时，然后发 `FileEnd`。接收方偏移不连续就失败；每 256 KiB 以及结束时报进度；`FileEnd` 时核对大小和 SHA-256，改名到下载目录，回 `FileComplete`。任一方 `FileCancel` 都中止。

`FileComplete` 丢了的时候，发送方在校验状态最多再发 4 次 `FileEnd`（间隔 8 秒），然后失败，原文是「对方未确认文件已收完」。接收方如果已经收完，再收到 `FileEnd` 就再回一次 `FileComplete`。C 核心在同一线程里先写入数据块再处理 `FileEnd`，字节没到齐就直接失败，不要把它改成异步等待。Rust 会记下 `end_seen`，等字节到齐再进入校验。两边都在 `lan-core/src/engine/files.rs` 和 `editions/portable/src/core.c`。

断线后续传（双方都有 `CAP_RESUME`）：

- 活动传输变成暂停，不直接判失败。接收方留着 `.part` 和已经算到一半的 SHA-256。
- 发送方用同一个 tid 再发 `FileOffer`。
- 已经完成 → 再回一次 `FileComplete`（上一份可能丢了）。
- 已有一部分 → `FileResume{offset, 前 offset 字节的 SHA-256}`。
- 发送方核对前缀，对得上就从 offset 继续，否则从 0 重来。
- 没有 `CAP_RESUME` 的旧对端：断线即失败，手动重试从 0 开始。

接收方的 `.part` 旁边有 `<part>.resume`，发送方在 `<data>/outgoing/<tid>.out`。格式在 `docs/protocol.md` 8.1。启动时重新哈希 `.part` 得到偏移，这不在网络线程上做。超过 30 天的残留删掉。源文件大小或 mtime 变了，发送失败，原文是「源文件已更改」。

文件传输如果停在「正在校验 0/大小」，先看是不是 `FileEnd` 比最后一块先到，或 `FileComplete` 丢了。上面的重发已经接在现有状态上。不要再新造一套传输状态。

## 9. 群

群 id 是 Uuid v4。创建者持有成员列表（含自己，排序去重）和 32 字节群密钥。群正文是 `nonce(12) ‖ ChaCha20-Poly1305(群密钥)`，附加数据为空。非成员的密文丢掉。

邀请和 rekey 由创建者的 Ed25519 签名。签名覆盖的字节布局在 `docs/protocol.md` 第 7 节，改一个字节两边都要改。邀请只在发送者已受信任、签名正确、自己在成员列表里时接受。

移除成员：创建者换新密钥，`key_epoch + 1`，给留下的人发 `GroupRekey`，也发给被移除的人，让他知道自己被移出。被移除者解不开新消息，但旧密钥仍能解开他离线期间截到的旧密文。这一点要诚实，不要在界面上写成「移除后历史也不可读」。

群**没有**离线补发。成员不在线就收不到那一条，上线也不会补历史。各自只持久化自己已经收到的。

## 10. 中转和账号

P2P 仍然是协议 5。中转是后加的转发，HELLO 版本仍是 1。细节以 `docs/account-mode.md` 为准，这里是不能改错的约束。

两种模式：

| | 设备模式（默认） | 账号模式 |
|---|---|---|
| 身份 | 只在本机 | 本机持有密钥，密文备份在中转上 |
| 换机 | 指纹会变，除非本机还有备份文件 | 恢复密钥或已有设备的配对二维码，指纹不变 |
| 局域网 | 照常发现、直连、端到端 | 相同 |

没有服务器口令。官方客户端不再创建带密码的中转账户。`ACCOUNT_REGISTER` / `ACCOUNT_LOGIN` 的口令长度为 0 表示密钥账户。目录里如果还留着旧的 `pass.hash`，登录仍按旧口令校验。

恢复密钥 RK 是 16 字节，显示成 `RK1-` 加 8 组十六进制。查找名是 `SHA-256("lsm-acct-lookup-v1" ‖ 规范化 RK)` 的前 16 字节的十六进制，只是目录名，推不回 RK。备份格式 `LSMBAK02`：`Argon2id(RK) → ChaCha20-Poly1305`。服务器只存这块密文。

跨设备能看到的历史，来自这份客户端加密的备份，以及用户明确选择「留在服务器」的密文，**不是**把 Noise 传输帧重放一遍。新设备没有旧会话的前向保密临时密钥。

配对二维码：`lsm://pair?r=host:port&t=<token>&k=<包装密钥>`，中转上 TTL 10 分钟，用过即删。另有 `lsm://add?...` 加联系人，`lsm://relay?h=&p=` 连接中转。五个 UI 都要能出示自己的「加我」二维码。Android 用 CameraX + ZXing，不用 ML Kit。桌面以粘贴 `lsm://` 为主，有 `zbarimg` 才从图片解码。

中转 `AUTH_OK` 在原有 38 字节后可以追加 `caps: u32`。`CAP_RELAY_ACCOUNT = 1`，`CAP_RELAY_PAIR = 2`。旧服务器没有这段，客户端要说「服务器不支持账号模式」，局域网设备模式不受影响。

账号相关帧：`PAIR_*` 是 `0x68`–`0x6C`。`ACCOUNT_DEVICES` 是 `0x6D`（用户名后一个字节：0 列设备，2 登出其他设备），应答 `ACCOUNT_DEVICE_LIST` `0x6E`（每行 `hex first_unix last_unix`）。设备号是这次连接的 X25519 公钥的 SHA-256。调用者必须已经在名单里。登出只删设备文件，不删 `identity.bak`。`ACCOUNT_GET_BACKUP` 在用户名后再加一个字节 `1` 表示要上一份备份，没有这个字节仍是当前这份。未知类型旧服务器忽略。不要占用已经用掉的类型号。五个界面的账号菜单都有「已登录设备」「登出其他设备」「用上一份备份覆盖本机」。细节在 `docs/account-mode.md` 第 7、8 节。

传输模式：`lan` 时优先直连，没有候选才走中转；`server_lan` 时两条路并行。每条消息可以选 ends-only（中转不落盘，默认）或 server-retain（存密文、有 TTL 和配额）。群消息默认不留在服务器。

## 11. 数据放哪，升级不许丢

Windows daemon 的文档路径是 `%APPDATA%\FREC86\lan-secure-messenger\{config,data,logs}`。日志按天滚动，留 7 天，不写私钥。

中转的数据目录在 `relay-server/src/datadir.rs`。配置里的 `data_dir` 非空就用那个路径。为空时：

1. 环境变量 `LSM_RELAY_DATA`（非空）。
2. Windows：`%ProgramData%\LsmRelay\data`，没有就 `%LOCALAPPDATA%\LsmRelay\data`。
3. 其他系统：能写就 `/var/lib/lsm-relay/data`，否则 `~/.lsm-relay/state/data`，再否则才是 `relay-data`。

`deploy.sh` 写入 `$STATE_DIR/data`（root 就是 `/var/lib/lsm-relay/data`）。`deploy.ps1` 管理员写入 `%ProgramData%\LsmRelay\data`，非管理员写入 `%LOCALAPPDATA%\LsmRelay\data`。两边都会在目标 `users/` 为空时复制旧的 `relay-data`，来源不删。`SCHEMA` 现在是 `1`，只表示这个目录已经按当前布局打开过；设备文件在 `users/<查找名>/devices/`，登录时才创建。每次上传备份先把活的 `identity.bak` 改名为 `identity.bak.prev`，再写入新文件。

改数据目录时的硬规则：

- 新版本要能读旧版本的目录。
- 迁移是拷贝或原地升级，不删除用户原来的目录。
- 先写新的，确认能读，再考虑清理。失败必须还能用旧目录启动。
- 不要把空的 `data_dir` 再解释成「进程当前目录下的 `./relay-data`」。部署脚本和 `datadir.rs` 要一起看：`relay-server/deploy/deploy.sh`、`deploy.ps1`。

## 12. 五个界面怎么对齐

平台 id：`winui`、`xp`、`gtk3`、`adw`、`android`。GTK 3 和 GTK 4/libadwaita 分开算，不能只改一个。

`docs/features.yml` 是对照表。CI 里 `python3 tools/check_feature_parity.py` 会查每个功能的 `LSMF_<id>`：

- `implemented`：钩子字符串出现在该平台的 UI 源码里。钩子必须放在真的控件或处理函数旁边，不许为了过检查写一行永远不会走到的字符串。
- `automated_test`：测试文件存在且包含指定片段。引擎测试可以算在这里。
- `gui_verified`：截图或冒烟驱动包含指定片段。`lan-core/tests` 和 `c_unit_test.c` **不能**算 GUI 已验证。

加一个用户可见的功能，就要五端都有入口（Android 专有的滑动除外，用 `platforms:` 限死），并补测试。`python3 tools/ui_menu_limits.py` 也在版本 job 里跑。菜单和导航的数量限制以这个工具和 `docs/design-research-v072.md` 为准，不要把所有命令铺进第一层。

各端源码根：

| 端 | 主要文件 |
|---|---|
| WinUI | `ui-winui/` |
| XP | `editions/portable/src/gui.c` 和 `xp_*.inc` |
| GTK 3 | `editions/portable/linux/gtk_gui.c` 和 `gtk_*.inc` |
| GTK 4 / libadwaita | `editions/portable/linux/adw_gui.c` 和 `adw_*.inc` |
| Android | `editions/portable/android/app/src/main/java/io/github/frec86/lanmessenger/` |

XP 保持系统控件和经典主题。不要用自绘卡片去对抗系统主题。Android 不用 Material You：页面、卡片、胶囊导航和气泡是不透明的 Liquid Glass（`Theme.kt` 的 `Pal`，色值在 `editions/portable/tools/palettes/android.json`）。Compose Material3 只剩对话框、菜单和文字，配色被 `Pal.scheme()` 盖掉，不要改回 SchemeTonalSpot，也不要换启动图标。

## 13. 界面改动听谁的

已发布的设计文件：

- `docs/design-research-v072.md`：导航数量、菜单、恢复密钥页。依据是 Material、Telegram、Signal、WhatsApp 的现成做法。
- `docs/design-m3-cards.md`：一度把页面收成卡片。用户后来认为整页卡片像没做完。不要再把列表、设置、导航都收成卡片墙。

卡片只用在有状态的少数地方：信任闸门、正在传输的文件、恢复密钥、必须二选一的模式。人要扫很多行的地方用列表。做法照 Material 3 的卡片指南（多数内容不要放进卡片）和 Signal 的恢复密钥页，不要自己发明一套格子。

动动画时只用 Material 3 已公布的曲线，不要线性，也不要自己写一条：

- 选择、悬停：standard，`cubic-bezier(0.2, 0, 0, 1)`，约 200 ms。
- 消息进入：emphasized decelerate，`cubic-bezier(0.05, 0.7, 0.1, 1)`，约 400 ms。
- 退出：emphasized accelerate，`cubic-bezier(0.3, 0, 0.8, 0.15)`，约 200 ms。

图标：启动图标是 Material 的圆角聊天气泡（气泡加三行字，尾巴在左下），放在自适应图标的安全圈里。底色是应用主色 `#3F5F90`，不是另一套蓝。矢量在 `res/drawable/ic_fg.xml`，传统 PNG 在 `res/mipmap-*`。通知图标是同一形状的白色剪影。不要换回那只没有字、尾巴戳出圆角的空心气泡。

## 14. CI 和发版

`.github/workflows/ci.yml` 是唯一入口。push 到 `main` 或 `unify/**`、PR、手动，都会跑：

| Job | 内容 |
|---|---|
| `version` | `tools/set-version.py --check`、功能对照、菜单数量 |
| `windows` | Rust 测试、daemon、WinUI 浅色/深色冒烟和配色 |
| `portable` | XP、GTK 3、GTK 4、Android APK 和模拟器截图 |
| `relay-server` | 中转各平台二进制和部署脚本 |
| `relay-matrix` | Rust ↔ C ↔ Android，直连、中转、续传、中途换路 |

`work/**` 不在 push 触发里（为了省配额）。要在这种分支上跑，用手动 `workflow_dispatch`，并用 `jobs` 输入限定 `windows,portable,relay-server,relay-matrix`。

只有 **手动推上去的 `v*` 标签** 才跑 `release`，把全部安装包和 `SHA256SUMS.txt` 收进同一个 GitHub Release。标签必须等于 `VERSION`。说明文件是 `docs/release-notes-<标签>.md`，没有就用一行标题。标签名里带 `-` 的是预发布。

`portable.yml` 有生成来源（`tools/gen-portable-workflow.py`，从便携版的 xp 工作流生成）。不要为了一处小改去重写整个文件。Android 签名不在工作流里生成钥匙，而在 Gradle 里，见下一节。

配色审计是像素级的（`tools/ui_color_audit.py`）。失败时先看图上标出的矩形，确认那是文字、图标还是复选框，再改。更新检查如果因为 GitHub API 403 报 `newer=-1`，先看是不是没带 token 或没重试，不要改版本比较逻辑。

## 15. Android 证书

症状：安装器说软件包无效、缺乏开发者证书。v0.7.3 最初的 APK 其实有签名块，但是 `CN=Android Debug`，而且那把钥匙是跑 CI 的机器当天自己生成的，下一版还会变，装过的手机也无法覆盖升级。

现在 `app/build.gradle.kts` 的 release 使用 `app/signing/lsm-release.p12`。主体、指纹和「为什么不能换钥匙」写在 `docs/signing.md`。release 构建缺这个文件就应该失败，不要静默退回 debug。

换图标或证书之后，已经发出去的那个 APK 不会自己变。要替换 Release 上的 `LanMessenger-v0.7.3-android.apk`，并改 `SHA256SUMS.txt` 里对应的一行。包名仍是 `io.github.frec86.lanmessenger`，`versionName` 仍是 `0.7.3`，除非用户要求新版本号。证书一换，如果有人曾经装上旧的 debug 包，必须先卸载。

## 16. 明确不要做的事

- 不要在 enum 或帧类型的中间插值。
- 不要只改 Rust 或只改 C。
- 不要为了过 `features.yml` 写假钩子。
- 不要手改生成出来的大段工作流来「修」一个签名问题。
- 不要在没读 CI 日志时改截图脚本。
- 不要把 `release` 签回 Android Debug。
- 不要在升级脚本里删除旧数据目录。
- 不要把恢复密钥、私钥、PIN 写进日志或 IPC 快照。快照里只允许 `account_mode`、`account_user`（查找名）、`has_recovery_key`。
- 不要打标签，除非用户说发版。
- 不要用网页原型或设计稿里的假数据当成已经接通的功能。

## 17. 本地可以先跑的检查

```bash
cargo test -p lan-core
python3 tools/check_feature_parity.py
python3 tools/set-version.py --check
python3 tools/ui_menu_limits.py
```

C 核心的语法检查在 `editions/portable` 的 Makefile / `tests/c_unit_test.c`。Android 发布包：

```bash
cd editions/portable/android && ./gradlew assembleRelease
keytool -printcert -jarfile app/build/outputs/apk/release/app-release.apk
```

指纹必须等于 `docs/signing.md`。

## 18. 还该打开的文件

| 要改什么 | 先打开 |
|---|---|
| 线上格式 | `docs/protocol.md` |
| 账号、二维码、备份 | `docs/account-mode.md` |
| 中转行为 | `docs/relay-protocol.md`（若与代码不一致，以代码为准，并改文档） |
| 功能有没有五端 | `docs/features.yml` |
| 导航和恢复密钥该长什么样 | `docs/design-research-v072.md` |
| Windows 怎么连 daemon | `docs/ipc.md` |
| 证书 | `docs/signing.md` |
