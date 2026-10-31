## LAN Secure Messenger v0.6.0（正式版）

v0.6.0 是第一个把**所有版本放在同一个仓库、同一个 Release** 的正式版：Windows 10/11（x64 / arm64）、
Windows XP、Linux（GTK 4 与 GTK 3 兼容版）、Android，以及可选的**中转服务器**。所有版本互通（协议 v5）。

### 主要功能

- **中转服务器（可选）**：跨网段 / 跨地点时转发已加密数据，端到端加密不变，服务器看不到内容、不保存记录；
  直连优先，传输中途切换线路或断线后自动断点续传（SHA-256 校验）；附中文部署脚本（安装 / 升级 / 卸载 / 状态）
- **免打扰**（按联系人）、**诊断信息**、**检查更新**
- XP / Linux / Android 与 Windows 版**加密备份互通**（Argon2id + ChaCha20-Poly1305）

### 自 beta.2 以来的修正

- **中转服务器无需口令**：客户端不再要求网络口令，默认开放连接（端到端加密不变；部署脚本口令改为可选）
- **发送文件/图片前检查信任**：对方未信任时直接提示，不再先显示发送成功再失败
- **已拒收**：对方拒收的文件在发送方聊天中保留，并标明「已拒收」

- **防火墙从不自动修改**：Windows、XP、Linux 的"配置防火墙"和中转部署脚本都会先检测；防火墙未启用时只提示
  「本机防火墙未启用，不做修改」；已启用时显示将执行的命令并询问（默认「否」）。云服务器请在安全组中放行端口
- 所有版本的"关于"统一为"所有版本互通（协议 v5）"；Windows 窗口标题改为中文；XP 图标增加 48px
- 版本号全部来自同一个 VERSION 文件（含 Windows 说明文件和中转部署脚本）；Android versionCode 递增，可覆盖安装
- 中转部署脚本：iptables 规则添加后提示如何持久化

### 文件

| 文件 | 适用 |
|------|------|
| `LanMessenger-v0.6.0-win-x64.zip` / `-win-arm64.zip` | Windows 10 1809+ / 11 |
| `LanMessenger-XP-v0.6.0.zip` | Windows XP SP3 及以上（32 / 64 位，绿色版） |
| `LanMessenger-adw-0.6.0-x86_64.AppImage` | Linux，GTK 4 + libadwaita（推荐） |
| `LanMessenger-0.6.0-x86_64.AppImage` / `lan-messenger-0.6.0-linux-x86_64.tar.gz` | Linux，GTK 3 兼容版 |
| `LanMessenger-v0.6.0-android.apk` | Android |
| `relay-server-linux-*.tar.gz` / `relay-server-win-*.zip` | 中转服务器 |
| `SHA256SUMS.txt` | 所有文件的 SHA-256 |
