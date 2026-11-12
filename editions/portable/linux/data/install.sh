#!/bin/sh
# 安装到当前用户（~/.local），无需 root。卸载：删除 ~/.local/bin/lan-messenger-gtk 等文件即可。
set -e; D="$(cd "$(dirname "$0")" && pwd)"; P="${PREFIX:-$HOME/.local}"
mkdir -p "$P/bin" "$P/share/applications" "$P/share/icons"
cp "$D/bin/lan-messenger-gtk" "$D/bin/lanmsg-cli" "$P/bin/"
cp -a "$D/share/icons/hicolor" "$P/share/icons/"
sed "s#^Exec=.*#Exec=$P/bin/lan-messenger-gtk#" "$D/share/applications/lan-messenger.desktop" > "$P/share/applications/lan-messenger.desktop"
echo "已安装到 $P/bin 。可在应用菜单中找到“局域网安全通讯”。"
