局域网安全通讯 XP 版 (LAN Secure Messenger, legacy edition) v0.6.0
====================================================================

给老机器（Windows XP SP3 32 位，及 XP x64 / Server 2003）用的版本，所有版本互通（协议 v5）：
自动发现、按 IP 添加、Noise 加密连接、消息与送达/已读回执、文件传输、群组（含移除成员换密钥）、PIN 配对。
协议细节见源码仓库 docs/protocol.md（协议版本 5）。

文件说明
--------
  LanMessenger.exe      图形界面（32 位，XP SP3 起）
  lanmsg-cli.exe        命令行版（32 位），输入 help 查看命令
  x64\                  64 位版本（XP x64 / Server 2003 x64 及以上），可选
  start.bat             启动图形界面
  cli.bat               启动命令行版
  firewall-setup.bat    添加防火墙例外（自动识别 XP 用 netsh firewall，Vista+ 用 advfirewall）
  data\                 首次运行后生成：身份密钥、联系人、群组、加密聊天记录、downloads\ 接收的文件

使用
----
1. 解压到任意目录（不需要安装，不写注册表）。
2. 以管理员身份运行 firewall-setup.bat 一次（或在“文件 > 设置防火墙例外”中操作）。
   需要放行：UDP 41234（发现）和 TCP 41235（连接）。
3. 运行 start.bat。同一网段的 v0.5 或 XP 版会自动出现在左侧列表；跨网段请用“联系人 > 按 IP 添加”。
4. 信任对方：选中联系人后“联系人 > 信任”，或用 PIN 配对（一方“生成 PIN”，另一方“输入 PIN 配对”）。
   只有已信任的联系人才能给你发文件、拉你进群（与 v0.5 规则一致）。
5. 关闭窗口时程序最小化到托盘；托盘右键“退出”才真正退出。

注意
----
* 中文显示需要系统已安装东亚语言支持（中文版 XP 默认已有；英文版 XP 需在“区域和语言选项”中安装）。
* 界面只用系统颜色（GetSysColor），在高对比度或深色主题下文字也可读。
* 数据目录 data\ 内的 state.key 是本机加密密钥，请与 data\ 一起备份、勿外传。
* 不依赖 .NET、VC 运行库或 GDI+；仅使用 XP SP3 自带的系统 DLL。

命令行示例
----------
  cli.bat --name 张三
  > peers
  > add 192.168.1.20
  > pin-offer
  > send 张三 你好
  > sendfile 张三 C:\报告.doc
  > group-create 项目组 张三,李四
  > quit
