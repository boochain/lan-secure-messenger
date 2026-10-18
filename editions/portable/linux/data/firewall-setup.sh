#!/bin/sh
# 为局域网安全通讯放行端口：TCP 41235（连接）、UDP 41234（自动发现）。支持 ufw 与 firewalld。
if command -v ufw >/dev/null 2>&1; then
  sudo ufw allow 41235/tcp comment 'LAN Messenger' && sudo ufw allow 41234/udp comment 'LAN Messenger' && echo "ufw 已放行。"
elif command -v firewall-cmd >/dev/null 2>&1; then
  sudo firewall-cmd --permanent --add-port=41235/tcp --add-port=41234/udp && sudo firewall-cmd --reload && echo "firewalld 已放行。"
else
  echo "未检测到 ufw 或 firewalld，无需设置（如使用其他防火墙，请手动放行 TCP 41235 和 UDP 41234）。"
fi
