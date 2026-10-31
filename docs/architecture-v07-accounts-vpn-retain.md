# v0.7 架构设计：VPN 旁路 · 账号/双传输 · 服务器保留

状态：v0.6.1 已落地核心代码（绑网卡 / 账号+备份 / retain STORE_FOR / transport_mode）；细节与联调续进  
仓库：lan-secure-messenger  
原则：免费自建优先；默认可关；不破坏现有 LAN-only E2E。

## #8 VPN/代理开启时局域网仍可用

### 问题
发现层已排除 TAP/Wintun/WireGuard 等虚拟网卡，但全局 VPN 劫持路由时，UDP 广播与直连 TCP 仍可能走隧道失败。

### 方案（分层）
1. **文档兜底（立即）**：设置页说明如何在 WireGuard/系统分流中排除本应用或局域网网段。
2. **手动指定网卡/源 IP（v0.6.x 小步）**：设置 `bind_ipv4`；discovery announce、TCP listen/dial 绑定该地址（Win `IP_UNICAST_IF` / bind；Linux `SO_BINDTODEVICE` 或源地址；Android `NetworkCallback` 选 Wi‑Fi）。
3. **自动旁路（v0.7）**：枚举物理网卡，优先非虚拟且有 RFC1918 地址的 iface；失败回退现有逻辑。

### 非目标
不捆绑商业 VPN SDK；不修改系统路由表（需管理员且风险高）。

## #9 账号 + 指纹保留 + LAN/服务器对等传输

### 现状
身份在本地 `identity`；重装即新指纹。中转是在线转发后备，非对等并列，无账号。

### 方案
1. **本地加密身份备份**（已有 Argon2 + ChaCha）：设置页强化「导出/恢复」；可选定期提醒。
2. **自建 relay 账号（v0.7）**：
   - 注册：用户名 + 口令 → 服务端仅存 Argon2 哈希 + 设备 Noise 静态公钥指纹列表。
   - 登录：签发短期设备令牌；多设备列表可吊销。
   - **威胁模型**：服务端可见元数据（谁在线、何时连）；不可见消息明文（仍 E2E）。
3. **传输模式**：`LAN-only` | `Server+LAN`（双路径并行，按可达性选路；UI 同等展示「局域网 / 经服务器」）。

### 免费路径
沿用现有 `relay-server` 自建；不强制商业云。

## #10 每条消息/文件可选：留在服务器 vs 仅两端

### 依赖
硬依赖 #9 账号与 relay 存储层。

### 方案
- 默认 **`ends-only`**（现状：中转不落盘）。
- 可选 **`server-retain`**：上传 E2E 密文 blob + TTL + 配额；拉取/已读删除；服务端不可读明文。
- UI：发送前 per-item 开关；群消息默认 ends-only。

### 合规
自建部署自负；默认关闭保留。

## v0.6.1 已落地的相关小步
- 设置文案：中转「可选」、备份恢复入口（WinUI）。
- 存储管理与半成品清理（本分支 #7）。
- 心跳 4s / 死链 30s（#13）减轻后台误判离线。

## v0.6.1 已编码
- [x] `bind_lan_ip`（空/`auto`/IPv4）贯通 discovery+listen+dial（Win/Linux/Android/C）
- [x] relay ACCOUNT_* + 本地 AccountStore；客户端 IPC 注册/登录/备份上下传
- [x] `transport_mode=server_lan` 时中转与 LAN 并行 Open；`lan` 时无候选才走中转
- [x] STORE flags&1 按目标 peer 落盘；send_text/file `retain` + default_retain；DH seal_retain

## 残留（联调/增强）
- [x] 吊销设备指纹 UI + LIST 时 GC 过期 blob
- [x] 收端自动 FETCH（relay online + 45s 轮询 LIST_RETAIN）
- [x] 群已读回执：`mark_read` 对群会话向在线成员扇出 `Payload::Read`（协议本就支持）
- [x] 大文件 >100MB 发送确认 + Android/桌面通知路径
- [ ] 群消息 retain（默认仍 ends-only）
