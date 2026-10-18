#!/usr/bin/env bash
# XP Win32 GUI under wine (x86) on Xvfb next to a real v0.5 daemon: every screen in light AND dark
# (--theme light|dark, same table as the 外观 menu). Screenshots are root grabs cropped to the main window,
# so popup menus and dialogs on top of it are included and no desktop pixels are.
# Usage: tests/xp_ui_shots.sh <rust-bin-dir> <outdir> [exe]
set -u
RB=$1; OUT=$2; mkdir -p "$OUT"
ROOT=$(cd "$(dirname "$0")/.." && pwd); EXE=${3:-$ROOT/build/x86/LanMessenger.exe}
T=$(mktemp -d /tmp/lsm-xpshots.XXXX)
export WINEPREFIX=${WINEPREFIX:-$HOME/wpfx32} WINEDEBUG=-all LANG=zh_CN.UTF-8 DISPLAY=:78
Xvfb :78 -screen 0 1100x760x24 -nolisten tcp >/dev/null 2>&1 & XV=$!
sleep 2; (openbox --config-file "$ROOT/tests/openbox-nodecor.xml" >/dev/null 2>&1 &); sleep 1
[ -d "$WINEPREFIX" ] || wineboot -i >/dev/null 2>&1
# wine test box only: no SimSun/YaHei here, map UI faces to Noto Sans CJK SC (the app itself prefers YaHei UI / SimSun)
for f in Tahoma 'MS Shell Dlg' 'MS Shell Dlg 2' 'Segoe UI' 'MS Sans Serif' 'Microsoft Sans Serif' SimSun 'Microsoft YaHei' 'Microsoft YaHei UI'; do wine reg add 'HKCU\Software\Wine\Fonts\Replacements' /v "$f" /d 'Noto Sans CJK SC' /f >/dev/null 2>&1; done
"$RB/lan-daemon" --data-dir "$T/r" --ipc "$T/r.sock" --set 'display_name="王工（v0.5）"' --set listen_port=0 --set discovery_port=47011 \
  --set announce_interval_secs=1 --set multicast_enabled=false --set auto_trust=true --set "download_dir=\"$T/dl\"" >"$T/r.log" 2>&1 & RD=$!
sleep 1; M="$RB/lan-msg --ipc $T/r.sock"
shot(){ local G; G=$(xwininfo -id "$WID"); local X=$(echo "$G"|awk '/Absolute upper-left X/{print $4}') Y=$(echo "$G"|awk '/Absolute upper-left Y/{print $4}') W=$(echo "$G"|awk '/Width:/{print $2}') H=$(echo "$G"|awk '/Height:/{print $2}')
  import -window root -crop ${W}x${H}+${X}+${Y} +repage "$OUT/$1.png"; echo "shot $1 ${W}x${H}+${X}+${Y}"; }
CL(){ eval $(xdotool getwindowgeometry --shell "$WID"); xdotool mousemove $((X+$1)) $((Y+$2)) click 1; }
menu(){ xdotool key "alt+$1"; sleep 1.2; }
rm -rf "$WINEPREFIX/drive_c/lsmshots"
for TH in light dark; do
  wine "$EXE" --data "C:\\lsmshots" --name "XP 旧电脑" --port 0 --disco-port 47012 --targets 127.0.0.1:47011 --theme $TH >"$T/gui-$TH.log" 2>&1 &
  for i in $(seq 60); do WID=$(xdotool search --name "局域网安全通讯" 2>/dev/null | head -1); [ -n "$WID" ] && break; sleep 0.5; done
  xdotool windowmove "$WID" 20 20; xdotool windowsize "$WID" 1000 660; xdotool windowactivate "$WID"; sleep 2
  shot "xp-$TH-01-welcome-empty"
  for i in $(seq 40); do $M peers 2>/dev/null | grep -q "XP" && break; sleep 0.5; done
  FX=$($M --json status | python3 -c 'import json,sys;d=json.load(sys.stdin);print([p["fingerprint"] for p in d["peers"] if "XP" in p["name"]][0])')
  CL 150 96; sleep 1
  if [ $TH = light ]; then menu c; xdotool key t; sleep 1
    $M send "$FX" "你好！这是来自 v0.5 的消息。下午三点评审会改到 302 会议室。" >/dev/null
    $M send "$FX" "Hello from v0.5 — 中文/English mixed text 测试" >/dev/null
    head -c 200000 /dev/urandom > "$T/示例文件.bin"; $M send-file "$FX" "$T/示例文件.bin" >/dev/null; sleep 3
    CL 150 96; sleep 1; xdotool type --delay 30 "OK, 收到 thanks"; xdotool key Return; sleep 2
  fi
  CL 150 96; sleep 1.5; shot "xp-$TH-02-chat"
  menu f; shot "xp-$TH-03-menu-file"; xdotool key Escape Escape; sleep 0.5
  menu c; shot "xp-$TH-04-menu-contacts"; xdotool key Escape Escape; sleep 0.5
  menu g; shot "xp-$TH-05-menu-groups"; xdotool key Escape Escape; sleep 0.5
  menu s; shot "xp-$TH-06-menu-settings"; xdotool key Escape Escape; sleep 0.5
  menu c; xdotool key a; sleep 1.5; xdotool type "192.168.1.20"; sleep 0.5; shot "xp-$TH-07-dialog-add-ip"
  xdotool key ctrl+a; xdotool type "bad address"; xdotool key Return; sleep 1.5; shot "xp-$TH-08-error"; xdotool key Return; sleep 0.8
  menu c; xdotool key p; sleep 1.5; shot "xp-$TH-09-pin"; xdotool key Return; sleep 0.8
  menu g; xdotool key c; sleep 1.5; shot "xp-$TH-10-dialog-group-picker"; xdotool key Escape; sleep 0.8
  menu h; xdotool key a; sleep 1.5; shot "xp-$TH-11-about"; xdotool key Return; sleep 0.8
  wineserver -k; sleep 2
done
kill $RD; pkill -f "openbox" ; kill $XV
ls "$OUT"
