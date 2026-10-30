## LAN Secure Messenger v0.6.0-beta.2（预发布 / 测试版）

本版是 v0.6.0-beta.1 的修正版，**功能与 beta.1 相同**，主要修正版本号。

### 修正

- **版本号统一**：beta.1 中 XP、Linux、Android 版的文件名、「关于」和检查更新里仍显示旧版本号（如 0.5.0-xp.2、0.5.0-linux.1），
  Windows 版「关于」显示 0.5.0-beta.2，导致检查更新会误报「发现新版本 v0.6.0-beta.1」。
  现在所有版本（Windows、XP、Linux GTK 4 / GTK 3、Android、中转服务器）都显示 **0.6.0-beta.2**，文件名也一致
- 检查更新：当前版本与最新发布相同时显示「已是最新版本」；beta.2 正确识别为比 beta.1 新
- Android 版本号（versionCode）递增，可直接覆盖安装 beta.1
- 发布说明更正：Android 的免打扰在聊天页右上角 ⋮ 菜单中切换

### beta.1 的内容回顾

- 所有版本在同一个 Release 发布：Windows 10/11（x64 / arm64）、Windows XP、Linux（GTK 4 与 GTK 3 兼容版）、Android、中转服务器
- **中转服务器（可选）**：跨网段 / 跨地点时转发已加密的数据，端到端加密不变；直连优先，传输中途切换线路自动断点续传；
  附中文部署脚本，**不会自动修改防火墙**（云服务器请在安全组放行端口）
- 按联系人**免打扰**、**诊断信息**、**检查更新**
- XP / Linux / Android 与 Windows 版**加密备份互通**（Argon2id + ChaCha20-Poly1305）

### 文件

| 文件 | 适用 |
|------|------|
| `LanMessenger-v0.6.0-beta.2-win-x64.zip` / `-win-arm64.zip` | Windows 10 1809+ / 11 |
| `LanMessenger-XP-v0.6.0-beta.2.zip` | Windows XP SP3 及以上（32 / 64 位，绿色版） |
| `LanMessenger-adw-0.6.0-beta.2-x86_64.AppImage` | Linux，GTK 4 + libadwaita（推荐） |
| `LanMessenger-0.6.0-beta.2-x86_64.AppImage` / `lan-messenger-0.6.0-beta.2-linux-x86_64.tar.gz` | Linux，GTK 3 兼容版 |
| `LanMessenger-v0.6.0-beta.2-android.apk` | Android |
| `relay-server-linux-*.tar.gz` / `relay-server-win-*.zip` | 中转服务器 |
| `SHA256SUMS.txt` | 所有文件的 SHA-256 |
