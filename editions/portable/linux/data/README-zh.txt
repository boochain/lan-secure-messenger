局域网安全通讯 Linux 版 0.6.0
=====================================

所有版本互通（协议 v5，Noise_XX_25519_ChaChaPoly_BLAKE2s 端到端加密）。

运行方式
  AppImage：chmod +x LanMessenger-*.AppImage && ./LanMessenger-*.AppImage
            命令行版：./LanMessenger-*.AppImage cli --help
  压缩包：  ./bin/lan-messenger-gtk   （图形界面）
            ./bin/lanmsg-cli          （命令行，静态编译，任何 Linux 可用）
            ./install.sh              （安装到 ~/.local，加入应用菜单）

依赖：图形界面需要系统自带的 GTK 3（所有主流桌面发行版均已预装）。
托盘图标：有 AppIndicator（Ubuntu/KDE/Xfce 等）时显示；GNOME 需安装 “AppIndicator 支持” 扩展，否则没有托盘图标，但关闭窗口即退出，不影响使用。
深色模式：自动跟随系统（XDG 门户 / GNOME 设置）；也可用 LSM_THEME=dark 或 LSM_THEME=light 强制。
防火墙：如启用了 ufw 或 firewalld，请运行 ./firewall-setup.sh，或在菜单“设置防火墙”中操作。
数据目录：~/.local/share/lan-messenger（接收的文件在其中的 downloads 目录）。
