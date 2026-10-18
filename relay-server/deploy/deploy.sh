#!/usr/bin/env bash
# LSM 中转服务器 部署脚本 (LAN Secure Messenger relay-server deploy script) — Linux
# 交互菜单: ./deploy.sh        非交互: ./deploy.sh install --port 9443 --password xxx [--open-firewall]
# 命令 (commands): menu run start install remove-autostart stop config status uninstall version
# 选项 (options): --port N --password P --gen-password --bind ADDR --open-firewall --yes --dry-run --no-start
set -u
SCRIPT_VERSION="0.1.0"
SVC=lsm-relay
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ---------------- 路径 (paths) ----------------
if [ -n "${RELAY_HOME:-}" ]; then
  INSTALL_DIR="$RELAY_HOME/bin"; CONF_DIR="$RELAY_HOME/etc"; STATE_DIR="$RELAY_HOME/state"; LOG_DIR="$RELAY_HOME/log"
elif [ "$(id -u)" = 0 ]; then
  INSTALL_DIR=/opt/lsm-relay; CONF_DIR=/etc/lsm-relay; STATE_DIR=/var/lib/lsm-relay; LOG_DIR=/var/log/lsm-relay
else
  INSTALL_DIR="$HOME/.lsm-relay/bin"; CONF_DIR="$HOME/.lsm-relay/etc"; STATE_DIR="$HOME/.lsm-relay/state"; LOG_DIR="$HOME/.lsm-relay/log"
fi
BIN="$INSTALL_DIR/relay-server"
CONF="$CONF_DIR/relay.toml"
PIDF="$STATE_DIR/relay.pid"
FWSTATE="$STATE_DIR/firewall-rules"
MODEF="$STATE_DIR/mode"
UNIT="${RELAY_SYSTEMD_DIR:-/etc/systemd/system}/$SVC.service"
INITD="/etc/init.d/$SVC"
CRON_MARK="# lsm-relay-autostart"
# 旧版本残留位置 (legacy locations from older/hand installs)
LEGACY_BINS="${RELAY_LEGACY_BINS:-/usr/local/bin/relay-server /usr/local/bin/lsm-relay /usr/bin/lsm-relay-server $HOME/relay-server}"
LEGACY_UNITS="relay-server.service lsm-relay-server.service"
LEGACY_PIDS="${RELAY_LEGACY_PIDS:-/run/lsm-relay.pid /var/run/lsm-relay.pid /tmp/lsm-relay.pid /run/relay-server.pid}"
LEGACY_CONFS="/etc/relay-server/config.toml /etc/lsm-relay/config.toml"

PORT=""; PASS=""; BIND=""; GEN_PASS=0; OPEN_FW=0; YES=0; DRY=0; NO_START=0; INTERACTIVE=0

c_ok=$'\e[32m'; c_warn=$'\e[33m'; c_err=$'\e[31m'; c_b=$'\e[1m'; c_0=$'\e[0m'
[ -t 1 ] || { c_ok=""; c_warn=""; c_err=""; c_b=""; c_0=""; }
say()  { printf '%s\n' "$*"; }
ok()   { printf '%s[完成]%s %s\n' "$c_ok" "$c_0" "$*"; }
warn() { printf '%s[注意]%s %s\n' "$c_warn" "$c_0" "$*"; }
err()  { printf '%s[错误]%s %s\n' "$c_err" "$c_0" "$*" >&2; }
die()  { err "$*"; exit 1; }
# 所有改变系统的命令都经过这里 (every system-changing command goes through run)
run()  { if [ "$DRY" = 1 ]; then say "[dry-run] $*"; else "$@"; fi; }
have() { command -v "$1" >/dev/null 2>&1; }
has_systemd() { [ -z "${RELAY_NO_SYSTEMD:-}" ] && [ -d /run/systemd/system ] && have systemctl; }
has_openrc() { [ -z "${RELAY_NO_OPENRC:-}" ] && have rc-update && [ -d /etc/init.d ]; }

# ---------------- 架构与二进制 (arch & binary) ----------------
detect_arch() {
  case "$(uname -m)" in
    x86_64|amd64) echo x86_64 ;;
    aarch64|arm64) echo aarch64 ;;
    armv7*|armv8l|armhf) echo armv7 ;;
    i386|i486|i586|i686) echo i686 ;;
    riscv64) echo riscv64 ;;
    *) uname -m ;;
  esac
}
find_new_bin() {
  local a; a="$(detect_arch)"
  for c in "${RELAY_BIN:-}" "$SCRIPT_DIR/bin/linux-$a/relay-server" "$SCRIPT_DIR/relay-server" \
           "$SCRIPT_DIR/../target/x86_64-unknown-linux-musl/release/relay-server" "$SCRIPT_DIR/../target/release/relay-server"; do
    [ -n "$c" ] && [ -f "$c" ] && { echo "$c"; return 0; }
  done
  return 1
}
bin_version() { # 输出版本号或 unknown
  [ -x "$1" ] || { echo none; return; }
  local v; v="$("$1" --version 2>/dev/null | head -1 | awk '{print $2}')"
  [ -n "$v" ] && echo "$v" || echo unknown
}

# ---------------- 配置读写 (config) ----------------
toml_get() { # file section key -> value (strings unquoted/unescaped)
  [ -f "$1" ] || return 0
  awk -v sec="[$2]" -v key="$3" '
    /^\[/ { cur=$0; gsub(/[ \t]/,"",cur) }
    cur==sec && $0 ~ "^[ \t]*"key"[ \t]*=" {
      v=$0; sub(/^[^=]*=[ \t]*/,"",v); sub(/[ \t]+#.*$/,"",v)
      if (v ~ /^"/) { v=substr(v,2,length(v)-2); gsub(/\\"/,"\"",v); gsub(/\\\\/,"\\",v) }
      print v; exit }' "$1"
}
toml_str() { local s="${1//\\/\\\\}"; s="${s//\"/\\\"}"; printf '"%s"' "$s"; }
gen_pass() { head -c 24 /dev/urandom | base64 | tr -d '+/=\n' | cut -c1-24; }
gen_salt() { head -c 16 /dev/urandom | od -An -tx1 | tr -d ' \n'; }
load_existing() { # 读取现有或旧配置 (current or legacy config)
  local f
  for f in "$CONF" $LEGACY_CONFS; do
    [ -f "$f" ] || continue
    [ -z "$PORT" ] && PORT="$(toml_get "$f" server port)"
    [ -z "$PASS" ] && PASS="$(toml_get "$f" server passphrase)"
    [ -z "$BIND" ] && BIND="$(toml_get "$f" server bind)"
    [ -z "${SALT:-}" ] && SALT="$(toml_get "$f" server salt)"
    [ -n "$PORT" ] && [ -n "$PASS" ] && break
  done
  return 0
}
write_config() {
  run mkdir -p "$CONF_DIR" "$STATE_DIR" "$LOG_DIR"
  [ -z "${SALT:-}" ] && SALT="$(gen_salt)"
  if [ "$DRY" = 1 ]; then say "[dry-run] write $CONF (port=$PORT bind=$BIND)"; return; fi
  local tmp="$CONF.tmp.$$"
  ( umask 077; cat > "$tmp" <<TOML
# LSM 中转服务器配置 (relay-server config) — 由 deploy.sh 生成
[server]
bind = $(toml_str "$BIND")
port = $PORT
# 网络口令：客户端设置中需填写相同口令 (clients must use the same passphrase)
passphrase = $(toml_str "$PASS")
salt = "$SALT"
# 本机状态接口 (local status endpoint); "" = 127.0.0.1:<port+1>, "off" 关闭
status_addr = ""

[limits]
max_frame = 1048640
max_connections = 1024
max_connections_per_ip = 64
rate_bytes_per_sec = 67108864
rate_frames_per_sec = 5000
idle_timeout_secs = 90

[log]
level = "info"
file = $(toml_str "$LOG_DIR/relay.log")
TOML
  ) && chmod 600 "$tmp" && mv -f "$tmp" "$CONF"
  ok "配置已写入 (config written): $CONF"
}

# ---------------- 端口与口令 (port & passphrase) ----------------
valid_port() { [[ "$1" =~ ^[0-9]+$ ]] && [ "$1" -ge 1 ] && [ "$1" -le 65535 ]; }
port_in_use() {
  if have ss; then ss -Hltn "sport = :$1" 2>/dev/null | grep -q . && return 0; return 1; fi
  if have netstat; then netstat -ltn 2>/dev/null | awk '{print $4}' | grep -Eq "[:.]$1\$" && return 0; return 1; fi
  (exec 3<>"/dev/tcp/127.0.0.1/$1") 2>/dev/null && return 0; return 1
}
our_port() { [ "$(toml_get "$CONF" server port)" = "$1" ] && is_running; }
ask_port() {
  local def="${PORT:-9443}" p
  while true; do
    read -r -p "端口 (port) [1-65535，默认 $def]: " p; p="${p:-$def}"
    valid_port "$p" || { err "端口必须是 1-65535 的数字 (invalid port)"; continue; }
    if port_in_use "$p" && ! our_port "$p"; then err "端口 $p 已被占用 (port in use)，请换一个"; continue; fi
    [ "$p" -lt 1024 ] && [ "$(id -u)" != 0 ] && { err "1024 以下端口需要 root (privileged port)"; continue; }
    PORT="$p"; return
  done
}
ask_pass() {
  local a b
  if [ -n "$PASS" ]; then
    read -r -p "保留现有口令？(keep current passphrase) [Y/n]: " a
    case "$a" in n|N) ;; *) return ;; esac
  fi
  while true; do
    read -r -p "口令：1) 手动输入 (type)  2) 随机生成 (generate) [2]: " a
    if [ "${a:-2}" = 2 ]; then PASS="$(gen_pass)"; say "已生成口令 (generated): ${c_b}$PASS${c_0}"; say "请记下并填入各客户端 (write it down for the clients)"; return; fi
    read -r -s -p "输入口令 (passphrase, 至少 8 位): " a; echo
    [ "${#a}" -ge 8 ] || { err "口令太短 (min 8 chars)"; continue; }
    read -r -s -p "再次输入确认 (confirm): " b; echo
    [ "$a" = "$b" ] || { err "两次输入不一致 (mismatch)"; continue; }
    PASS="$a"; return
  done
}
ask_bind() {
  local def="${BIND:-0.0.0.0}" b
  read -r -p "监听地址 (bind) [0.0.0.0=全部 IPv4, ::=IPv4+IPv6，默认 $def]: " b
  BIND="${b:-$def}"
}

# ---------------- 防火墙 (firewall; never automatic) ----------------
FW_KIND=""
detect_firewall() { # sets FW_KIND to ufw|firewalld|nftables|iptables or ""
  FW_KIND=""
  if have ufw && ufw status 2>/dev/null | grep -qi "Status: active"; then FW_KIND=ufw; return; fi
  if have firewall-cmd && [ "$(firewall-cmd --state 2>/dev/null)" = running ]; then FW_KIND=firewalld; return; fi
  if have nft && nft list ruleset 2>/dev/null | grep -Eq 'hook input.*policy drop|^\s*(drop|reject)|\s(drop|reject)$'; then FW_KIND=nftables; return; fi
  if have iptables && iptables -S INPUT 2>/dev/null | grep -Eq '^-P INPUT DROP|-j (DROP|REJECT)'; then FW_KIND=iptables; return; fi
}
fw_add_cmd() { case "$FW_KIND" in
  ufw) echo "ufw allow $1/tcp comment lsm-relay" ;;
  firewalld) echo "firewall-cmd --permanent --add-port=$1/tcp && firewall-cmd --reload" ;;
  iptables) echo "iptables -I INPUT -p tcp --dport $1 -j ACCEPT" ;;
  *) echo "" ;; esac; }
fw_del_cmd() { case "$1" in
  ufw) echo "ufw delete allow $2/tcp" ;;
  firewalld) echo "firewall-cmd --permanent --remove-port=$2/tcp && firewall-cmd --reload" ;;
  iptables) echo "iptables -D INPUT -p tcp --dport $2 -j ACCEPT" ;; esac; }
cloud_note() { warn "云服务器还需在服务商控制台的安全组/防火墙中放行 TCP $PORT (cloud: also open the port in the provider's security group)"; }
firewall_step() {
  detect_firewall
  if [ -z "$FW_KIND" ]; then say "本机防火墙未启用，不做修改 (host firewall inactive, not touching it)"; cloud_note; return; fi
  if grep -qx "$FW_KIND|$PORT" "$FWSTATE" 2>/dev/null; then ok "防火墙规则已存在 (rule exists): $FW_KIND TCP $PORT"; cloud_note; return; fi
  local cmd; cmd="$(fw_add_cmd "$PORT")"
  if [ -z "$cmd" ]; then
    warn "检测到 $FW_KIND 规则，脚本不自动修改 (detected $FW_KIND ruleset; not modified automatically)"
    say "如需放行请手动添加，例如: nft add rule inet filter input tcp dport $PORT accept"; cloud_note; return
  fi
  local doit=0
  if [ "$INTERACTIVE" = 1 ]; then
    say "检测到防火墙已启用: $FW_KIND (firewall active)"
    say "将执行的命令 (command to run): $cmd"
    local a; read -r -p "是否添加放行规则？(add allow rule) [y/N]: " a
    case "$a" in y|Y|yes|YES) doit=1 ;; esac
  elif [ "$OPEN_FW" = 1 ]; then doit=1; say "执行 (running): $cmd"
  else say "防火墙已启用 ($FW_KIND)，未传 --open-firewall，不做修改。手动放行: $cmd"; fi
  if [ "$doit" = 1 ]; then
    if run bash -c "$cmd"; then
      [ "$DRY" = 1 ] || { mkdir -p "$STATE_DIR"; echo "$FW_KIND|$PORT" >> "$FWSTATE"; }
      ok "已添加防火墙规则 (rule added)"
    else err "防火墙命令失败 (firewall command failed)"; fi
  else say "未修改防火墙 (firewall unchanged)"; fi
  cloud_note
}
firewall_remove_ours() { # 只删除本脚本创建的规则 (only rules we created)
  [ -f "$FWSTATE" ] || return 0
  local line kind p cmd
  while IFS='|' read -r kind p; do
    [ -n "$kind" ] || continue
    [ -n "${1:-}" ] && [ "$p" != "$1" ] && continue
    cmd="$(fw_del_cmd "$kind" "$p")"; say "删除防火墙规则 (removing our rule): $cmd"
    run bash -c "$cmd" || warn "删除失败 (failed): $cmd"
  done < "$FWSTATE"
  if [ "$DRY" != 1 ]; then
    if [ -n "${1:-}" ]; then grep -v "|$1\$" "$FWSTATE" > "$FWSTATE.tmp" || true; mv -f "$FWSTATE.tmp" "$FWSTATE"; else rm -f "$FWSTATE"; fi
  fi
}

# ---------------- 进程 (processes) ----------------
read_pid() { [ -f "$1" ] && tr -dc 0-9 < "$1"; }
pid_alive() { [ -n "$1" ] && kill -0 "$1" 2>/dev/null; }
our_pids() { # 运行中的 relay-server 进程 (ours and legacy), matched on the executable name only
  local p a0 a1
  for p in $(pgrep -f 'relay-server|lsm-relay' 2>/dev/null); do
    [ "$p" = "$$" ] && continue
    [ -r "/proc/$p/cmdline" ] || continue
    a0="$(tr '\0' '\n' < "/proc/$p/cmdline" 2>/dev/null | sed -n 1p)"; a1="$(tr '\0' '\n' < "/proc/$p/cmdline" 2>/dev/null | sed -n 2p)"
    case "$(basename -- "$a0" 2>/dev/null)" in
      relay-server|lsm-relay|lsm-relay-server) echo "$p" ;;
      bash|sh|dash) case "$(basename -- "$a1" 2>/dev/null)" in relay-server|lsm-relay|lsm-relay-server) echo "$p" ;; esac ;;
    esac
  done | sort -u
}
svc_active() { has_systemd && systemctl is-active --quiet "$SVC" 2>/dev/null; }
svc_enabled() {
  if has_systemd && [ -f "$UNIT" ]; then return 0; fi
  [ -f "$INITD" ] && return 0
  have crontab && crontab -l 2>/dev/null | grep -q "$CRON_MARK" && return 0
  return 1
}
is_running() { svc_active && return 0; pid_alive "$(read_pid "$PIDF")" && return 0; [ -n "$(our_pids)" ]; }
stop_all() {
  local stopped=0 p
  if has_systemd; then
    for u in "$SVC.service" $LEGACY_UNITS; do
      systemctl is-active --quiet "$u" 2>/dev/null && { run systemctl stop "$u"; stopped=1; }
    done
  fi
  [ -f "$INITD" ] && { run "$INITD" stop >/dev/null 2>&1 || true; }
  for f in "$PIDF" $LEGACY_PIDS; do
    p="$(read_pid "$f")"
    if pid_alive "$p"; then run kill "$p"; stopped=1; fi
    [ -f "$f" ] && run rm -f "$f"
  done
  for p in $(our_pids); do run kill "$p" 2>/dev/null; stopped=1; done
  if [ "$DRY" != 1 ]; then
    for _ in $(seq 1 30); do [ -z "$(our_pids)" ] && break; sleep 0.2; done
    for p in $(our_pids); do warn "强制结束 (kill -9) $p"; kill -9 "$p" 2>/dev/null; done
  fi
  [ "$stopped" = 1 ] && ok "服务器已停止 (stopped)" || say "没有正在运行的服务器 (nothing running)"
}
require_installed() { [ -x "$BIN" ] && [ -f "$CONF" ] || die "尚未安装，请先选择 安装/持久运行 (not installed; run install first)"; }
ensure_installed() { # 安装/升级 二进制+配置 (install or upgrade binary + config)
  local nb; nb="$(find_new_bin)" || die "找不到 relay-server 二进制 (binary not found for $(detect_arch)); 设置 RELAY_BIN=路径"
  local newv oldv found="" lb
  newv="$(bin_version "$nb")"; oldv="$(bin_version "$BIN")"
  for lb in $LEGACY_BINS; do [ -f "$lb" ] && found="$found $lb"; done
  [ -f "$UNIT" ] && found="$found $UNIT"
  for u in $LEGACY_UNITS; do [ -f "/etc/systemd/system/$u" ] && found="$found /etc/systemd/system/$u"; done
  for f in $LEGACY_PIDS "$PIDF"; do [ -f "$f" ] && ! pid_alive "$(read_pid "$f")" && found="$found $f(stale)"; done
  local running; running="$(our_pids | tr '\n' ' ')"
  [ "$oldv" = none ] && [ -n "$found$running" ] && oldv="$(for lb in $LEGACY_BINS; do [ -f "$lb" ] && bin_version "$lb" && break; done)"
  [ -z "$oldv" ] && oldv=none
  if [ "$oldv" != none ] || [ -n "$found$running" ]; then
    say "检测到已有安装或残留 (existing install / leftovers):${found:- }${running:+ 运行中进程 pids: $running}"
    if [ "$oldv" = "$newv" ]; then say "版本相同 (same version): $newv — 重新应用 (re-applying)"; else say "${c_b}升级 (upgrade): $oldv → $newv${c_0}"; fi
    WAS_SVC=0; svc_enabled && WAS_SVC=1
    stop_all
    [ -f "$CONF" ] && { run cp -p "$CONF" "$CONF.bak.$(date +%Y%m%d%H%M%S)"; ok "配置已备份 (config backed up)"; }
    load_existing
    # 清理旧残留 (remove legacy leftovers)
    for lb in $LEGACY_BINS; do [ -f "$lb" ] && { run rm -f "$lb"; say "已删除旧文件 (removed) $lb"; }; done
    if has_systemd; then for u in $LEGACY_UNITS; do
      [ -f "/etc/systemd/system/$u" ] && { run systemctl disable "$u" >/dev/null 2>&1; run rm -f "/etc/systemd/system/$u"; say "已删除旧服务 (removed old unit) $u"; }
    done; fi
    for lc in $LEGACY_CONFS; do [ -f "$lc" ] && [ "$lc" != "$CONF" ] && run mv -f "$lc" "$lc.migrated"; done
  fi
  run mkdir -p "$INSTALL_DIR"
  if [ "$(readlink -f "$nb")" != "$(readlink -f "$BIN" 2>/dev/null)" ]; then
    run cp -f "$nb" "$BIN.new" && run chmod 755 "$BIN.new" && run mv -f "$BIN.new" "$BIN"
  fi
  [ "$DRY" = 1 ] || cp -f "${BASH_SOURCE[0]}" "$INSTALL_DIR/deploy.sh" 2>/dev/null || true
  [ "$DRY" = 1 ] || { mkdir -p "$STATE_DIR" "$LOG_DIR"; echo "$newv" > "$STATE_DIR/version"; }
  ok "二进制已就绪 (binary ready): $BIN ($newv, $(detect_arch))"
}
params_step() { # 引导或校验参数 (guided input or validation)
  load_existing
  if [ "$INTERACTIVE" = 1 ]; then ask_port; ask_pass; ask_bind; else
    PORT="${PORT:-9443}"; BIND="${BIND:-0.0.0.0}"
    valid_port "$PORT" || die "无效端口 (invalid port): $PORT"
    if port_in_use "$PORT" && ! our_port "$PORT"; then die "端口 $PORT 已被占用 (port in use)"; fi
    [ "$GEN_PASS" = 1 ] && PASS="$(gen_pass)" && say "已生成口令 (generated passphrase): $PASS"
    [ -n "$PASS" ] || die "需要 --password 或 --gen-password (passphrase required)"
  fi
}
start_bg() {
  require_installed
  if svc_enabled && has_systemd; then run systemctl restart "$SVC"; ok "已通过 systemd 启动 (started via systemd)"; echo service > "$MODEF" 2>/dev/null; return; fi
  if is_running; then stop_all; fi
  if [ "$DRY" = 1 ]; then say "[dry-run] nohup setsid $BIN --config $CONF --pid-file $PIDF &"; return; fi
  mkdir -p "$LOG_DIR" "$STATE_DIR"
  if have setsid; then setsid nohup "$BIN" --config "$CONF" --pid-file "$PIDF" >>"$LOG_DIR/console.log" 2>&1 < /dev/null &
  else nohup "$BIN" --config "$CONF" --pid-file "$PIDF" >>"$LOG_DIR/console.log" 2>&1 < /dev/null & fi
  disown 2>/dev/null || true
  echo background > "$MODEF"
  for _ in $(seq 1 30); do sleep 0.2; pid_alive "$(read_pid "$PIDF")" && break; done
  if pid_alive "$(read_pid "$PIDF")"; then ok "已在后台运行 (running in background) PID $(read_pid "$PIDF")，关闭终端后继续运行"
  else err "启动失败，查看日志 (start failed, see) $LOG_DIR/console.log"; tail -5 "$LOG_DIR/console.log" 2>/dev/null; return 1; fi
}

# ---------------- 自启动 (autostart) ----------------
install_autostart() {
  require_installed
  if has_systemd && [ "$(id -u)" = 0 -o -n "${RELAY_SYSTEMD_DIR:-}" ]; then
    if [ "$DRY" = 1 ]; then say "[dry-run] write $UNIT; systemctl enable --now $SVC"; return; fi
    cat > "$UNIT" <<UNITEOF
[Unit]
Description=LSM Relay Server (LAN Secure Messenger 中转服务器)
After=network-online.target
Wants=network-online.target

[Service]
ExecStart=$BIN --config $CONF
Restart=on-failure
RestartSec=3
LimitNOFILE=65536
NoNewPrivileges=yes
ProtectSystem=full
ProtectHome=read-only
ReadWritePaths=$STATE_DIR $LOG_DIR

[Install]
WantedBy=multi-user.target
UNITEOF
    stop_all >/dev/null
    systemctl daemon-reload && systemctl enable --now "$SVC" && ok "已安装 systemd 服务 (systemd unit installed): $SVC" && echo service > "$MODEF"
  elif has_openrc && [ "$(id -u)" = 0 ]; then
    if [ "$DRY" = 1 ]; then say "[dry-run] write $INITD; rc-update add $SVC default"; return; fi
    cat > "$INITD" <<RCEOF
#!/sbin/openrc-run
description="LSM Relay Server"
command="$BIN"
command_args="--config $CONF --pid-file $PIDF"
command_background=true
pidfile="$PIDF"
depend() { need net; }
RCEOF
    chmod 755 "$INITD"; stop_all >/dev/null; rc-update add "$SVC" default && "$INITD" start && ok "已安装 OpenRC 服务 (OpenRC service installed)"
  elif have crontab; then
    local line="@reboot $BIN --config $CONF --pid-file $PIDF >>$LOG_DIR/console.log 2>&1 $CRON_MARK"
    if [ "$DRY" = 1 ]; then say "[dry-run] crontab += $line"; return; fi
    (crontab -l 2>/dev/null | grep -v "$CRON_MARK"; echo "$line") | crontab - && ok "无 systemd，已添加 crontab @reboot 自启 (no systemd; crontab @reboot added)"
    say "提示：其他 init 系统 (SysV/runit) 可将下面命令加入启动脚本: $BIN --config $CONF"
    is_running || start_bg
  else
    warn "未找到 systemd/OpenRC/crontab，请手动把下列命令加入开机启动 (add to boot manually): $BIN --config $CONF"
    say "现在以后台方式启动 (starting in background now)"; is_running || start_bg
  fi
}
remove_autostart() {
  local any=0
  if [ -f "$UNIT" ]; then
    has_systemd && { run systemctl disable --now "$SVC" >/dev/null 2>&1 || true; }
    run rm -f "$UNIT"; has_systemd && run systemctl daemon-reload; any=1
  fi
  if [ -f "$INITD" ]; then have rc-update && run rc-update del "$SVC" default >/dev/null 2>&1; run "$INITD" stop >/dev/null 2>&1; run rm -f "$INITD"; any=1; fi
  if have crontab && crontab -l 2>/dev/null | grep -q "$CRON_MARK"; then
    if [ "$DRY" = 1 ]; then say "[dry-run] remove crontab line"; else crontab -l 2>/dev/null | grep -v "$CRON_MARK" | crontab -; fi; any=1
  fi
  [ "$any" = 1 ] && ok "已删除开机自启 (autostart removed)" || say "未安装开机自启 (no autostart installed)"
  [ -f "$MODEF" ] && [ "$(cat "$MODEF")" = service ] && rm -f "$MODEF"
  return 0
}

# ---------------- 状态 (status) ----------------
json_field() { sed -n "s/^ *\"$1\": *\"\{0,1\}\([^\",]*\)\"\{0,1\},\{0,1\}\$/\1/p" | head -1; }
show_status() {
  say "${c_b}==== LSM 中转服务器状态 (status) ====${c_0}"
  say "安装目录 (install dir): $INSTALL_DIR  配置 (config): $CONF"
  say "已安装版本 (installed): $(bin_version "$BIN")   本包版本 (bundled): $(nb="$(find_new_bin)" && bin_version "$nb" || echo -)   CPU: $(detect_arch)"
  local port; port="$(toml_get "$CONF" server port)"
  say "端口 (port): ${port:--}   监听 (bind): $(toml_get "$CONF" server bind)"
  if svc_enabled; then say "开机自启 (autostart): 已安装 (installed)"; else say "开机自启 (autostart): 未安装 (not installed)"; fi
  if is_running; then
    say "运行状态 (running): ${c_ok}运行中 (running)${c_0} pids: $(our_pids | tr '\n' ' ')"
    local j; j="$("$BIN" --status --config "$CONF" 2>/dev/null)"
    if [ -n "$j" ]; then
      say "在线人数 (online): $(echo "$j" | json_field online)   连接数 (connections): $(echo "$j" | json_field connections)"
      say "运行版本 (running version): $(echo "$j" | json_field version)   已运行 (uptime): $(echo "$j" | json_field uptime_secs)s   已转发 (forwarded): $(echo "$j" | json_field bytes_forwarded) 字节"
    else warn "状态接口无响应 (status endpoint not reachable)"; fi
  else say "运行状态 (running): ${c_warn}未运行 (stopped)${c_0}"; fi
  [ -f "$FWSTATE" ] && say "本脚本添加的防火墙规则 (our firewall rules): $(tr '\n' ' ' < "$FWSTATE")"
  return 0
}

# ---------------- 动作 (actions) ----------------
do_install() { ensure_installed; params_step; write_config; firewall_step; [ "$NO_START" = 1 ] && return 0
  if [ "${WAS_SVC:-0}" = 1 ] || [ "${ACTION:-}" = install ] || [ "${ACTION:-}" = upgrade ]; then install_autostart; else start_bg; fi
  sleep 1; show_status; }
do_run() { ensure_installed; params_step; write_config; firewall_step; stop_all >/dev/null
  say "前台运行，按 Ctrl+C 停止 (foreground; Ctrl+C to stop)"; [ "$DRY" = 1 ] && { say "[dry-run] $BIN --config $CONF"; return; }
  "$BIN" --config "$CONF"; }
do_bg() { ensure_installed; params_step; write_config; firewall_step; start_bg; }
do_config() {
  require_installed; local oldport; oldport="$(toml_get "$CONF" server port)"
  load_existing; [ "$INTERACTIVE" = 1 ] && { ask_port; ask_pass; ask_bind; }
  valid_port "$PORT" || die "无效端口 (invalid port)"
  if [ "$PORT" != "$oldport" ] && port_in_use "$PORT"; then die "端口 $PORT 已被占用 (port in use)"; fi
  local wasrun=0; is_running && wasrun=1
  write_config
  if [ "$PORT" != "$oldport" ]; then say "端口变更 (port changed): $oldport → $PORT"; firewall_remove_ours "$oldport"; firewall_step; fi
  if [ "$wasrun" = 1 ]; then stop_all >/dev/null; if svc_enabled && has_systemd; then run systemctl restart "$SVC"; else start_bg; fi; ok "已用新参数重启 (restarted with new settings)"; fi
}
do_uninstall() {
  if [ "$INTERACTIVE" = 1 ] && [ "$YES" != 1 ]; then local a; read -r -p "确定卸载？配置与日志也将删除 (really uninstall?) [y/N]: " a; case "$a" in y|Y) ;; *) say "已取消"; return ;; esac; fi
  stop_all; remove_autostart; firewall_remove_ours
  run rm -rf "$INSTALL_DIR" "$CONF_DIR" "$STATE_DIR" "$LOG_DIR"
  for lb in $LEGACY_BINS; do [ -f "$lb" ] && run rm -f "$lb"; done
  ok "已卸载 (uninstalled)"
}

menu() {
  INTERACTIVE=1
  while true; do
    say ""
    say "${c_b}===== LSM 中转服务器 部署工具 v$SCRIPT_VERSION (relay-server deploy) =====${c_0}"
    say "中转仅转发端到端加密数据，看不到明文 (relay only forwards end-to-end encrypted data)"
    say "  1. 临时运行 (run in foreground, Ctrl+C 停止)"
    say "  2. 持久运行 (run in background, 关闭终端后继续)"
    say "  3. 安装开机自启服务 (install autostart service)"
    say "  4. 删除开机自启 (remove autostart)"
    say "  5. 停止正在运行的服务器 (stop server)"
    say "  6. 修改端口/口令等参数 (change port/passphrase)"
    say "  7. 查看状态 (status)"
    say "  8. 卸载 (uninstall)"
    say "  0. 退出 (exit)"
    local c; read -r -p "请选择 (choose) [0-8]: " c || exit 0
    case "$c" in
      1) ( do_run ) ;; 2) ( do_bg ) ;; 3) ( ACTION=install; do_install ) ;; 4) ( remove_autostart ) ;;
      5) ( stop_all ) ;; 6) ( do_config ) ;; 7) ( show_status ) ;; 8) ( do_uninstall ) ;;
      0|q|Q) say "再见 (bye)"; exit 0 ;; *) err "无效选择 (invalid choice)" ;;
    esac
  done
}

ACTION="${1:-menu}"; [ $# -gt 0 ] && shift
while [ $# -gt 0 ]; do case "$1" in
  --port) PORT="${2:-}"; shift 2 ;; --password) PASS="${2:-}"; shift 2 ;; --bind) BIND="${2:-}"; shift 2 ;;
  --gen-password) GEN_PASS=1; shift ;; --open-firewall) OPEN_FW=1; shift ;; --yes|-y) YES=1; shift ;;
  --dry-run) DRY=1; shift ;; --no-start) NO_START=1; shift ;;
  -h|--help) sed -n '2,6p' "$0"; exit 0 ;; *) die "未知参数 (unknown option): $1" ;; esac; done
case "$ACTION" in
  menu) menu ;; run) do_run ;; start|background) do_bg ;; install|upgrade) do_install ;;
  remove-autostart) remove_autostart ;; stop) stop_all ;; config) do_config ;; status) show_status ;;
  uninstall) do_uninstall ;; version) say "deploy.sh $SCRIPT_VERSION; relay-server $(bin_version "$BIN")" ;;
  -h|--help|help) sed -n '2,6p' "$0" ;; *) die "未知命令 (unknown command): $ACTION" ;;
esac
