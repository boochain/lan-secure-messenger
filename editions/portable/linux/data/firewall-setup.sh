#!/bin/sh
# 局域网安全通讯：为 TCP 41235（连接）、UDP 41234（自动发现）放行。
# 从不自动修改防火墙：未启用时只提示；已启用时显示将执行的命令并询问（默认：否）。
CLOUD="提醒：云服务器 / 虚拟机还需在服务商控制台的安全组中放行 TCP 41235 和 UDP 41234。"
if command -v firewall-cmd >/dev/null 2>&1 && firewall-cmd --state >/dev/null 2>&1; then
  CMD="sudo firewall-cmd --permanent --add-port=41235/tcp --add-port=41234/udp && sudo firewall-cmd --reload"; KIND=firewalld
elif command -v ufw >/dev/null 2>&1 && grep -qs '^ENABLED=yes' /etc/ufw/ufw.conf; then
  CMD="sudo ufw allow 41235/tcp comment 'LAN Messenger' && sudo ufw allow 41234/udp comment 'LAN Messenger'"; KIND=ufw
else
  echo "本机防火墙未启用，不做修改"; echo "（其他防火墙请自行放行 TCP 41235、UDP 41234）"; echo "$CLOUD"; exit 0
fi
echo "检测到已启用的防火墙：$KIND"; echo "将执行：$CMD"
if [ ! -t 0 ]; then echo "非交互模式，不做修改。"; echo "$CLOUD"; exit 0; fi
printf "是否添加放行规则？[y/N]: "; read -r a
case "$a" in y|Y|yes|YES) sh -c "$CMD" && echo "已放行。" ;; *) echo "未修改防火墙。" ;; esac
echo "$CLOUD"
