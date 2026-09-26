LAN Secure Messenger / 局域网安全通讯  v0.5.0-beta.1
=====================================================

使用方法（Windows 10 1809+ / Windows 11，64 位）
1. 把整个压缩包解压到任意文件夹（例如 D:\LanMessenger），不要在压缩包里直接运行。
2. 双击 LanMessenger.exe（或 启动.bat / Start.bat）。无需安装 .NET 或其他运行库。
3. 首次运行时输入显示名称，并点击「一键配置防火墙」，在弹出的管理员确认中点「是」。
   （也可以双击 配置防火墙.bat）
4. 同一局域网内其他打开了本软件的电脑会自动出现在左侧列表中；
   如果看不到，在左上角输入对方的 IP 地址直接添加。

文件说明
  LanMessenger.exe   图形界面（WinUI 3）
  lan-daemon.exe     后台服务：负责网络、加密、存储。关闭窗口后仍在托盘/后台接收消息。
  lan-msg.exe        命令行客户端（lan-msg --help），例如 lan-msg diag 查看诊断信息
  配置防火墙.bat      添加防火墙入站规则（TCP 41235 / UDP 41234，专用网络）
  停止.bat            停止后台服务并退出

常见问题
- 看不到对方 / 只有一方能看到另一方：
  打开「诊断」页，检查“防火墙规则”是否已就绪、网络类型是否为“公用网络”。
  公用网络会阻止入站连接，请在 Windows 设置 → 网络和 Internet → 属性 中改为“专用”。
  v0.5 起，只要任意一方能发现/连上另一方，双方都会显示在线。
- 电脑装有 Hyper-V / VMware / VirtualBox / WSL / VPN：这些虚拟网卡会被自动跳过，
  如仍有问题可在「设置 → 网络与发现」中指定使用的网卡。
- 数据与日志位置：%APPDATA%\FREC86\lan-secure-messenger\
     config\  身份密钥、联系人、设置
     data\    聊天记录（加密）、群组
     logs\    daemon.*.log（按天滚动，保留 7 天）、ui.log、crash.log
  反馈问题时请附上 logs 文件夹里的最新日志，或「诊断」页的“复制诊断信息”。

端口：TCP 41235（加密连接）、UDP 41234（发现广播）。
项目主页：https://github.com/FREC86/lan-secure-messenger
