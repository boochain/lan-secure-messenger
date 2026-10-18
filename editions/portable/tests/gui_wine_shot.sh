#!/usr/bin/env bash
# Run the XP GUI (x86) under wine on a virtual X display next to a real v0.5 daemon, drive it with
# xdotool, and capture screenshots. Usage: tests/gui_wine_shot.sh <rust-bin-dir> <outdir>
set -u
RB=$1; OUT=$2; mkdir -p "$OUT"
ROOT=$(cd "$(dirname "$0")/.." && pwd)
T=$(mktemp -d /tmp/lsm-gui.XXXX)
export WINEPREFIX=$HOME/wpfx32 WINEDEBUG=-all LANG=zh_CN.UTF-8 DISPLAY=:77
Xvfb :77 -screen 0 1100x720x24 >/dev/null 2>&1 & XV=$!
(openbox >/dev/null 2>&1 &)
sleep 2
# wine test env only: map the UI system fonts to a CJK font (a real Chinese XP has SimSun)
for f in Tahoma 'MS Shell Dlg' 'MS Shell Dlg 2' 'Segoe UI'; do wine reg add 'HKCU\Software\Wine\Fonts\Replacements' /v "$f" /d 'WenQuanYi Zen Hei' /f >/dev/null 2>&1; done
LSM_LOG=info "$RB/lan-daemon" --data-dir "$T/r" --ipc "$T/r.sock" --set 'display_name="v0.5 同事"' --set listen_port=0 --set discovery_port=47001 \
  --set announce_interval_secs=1 --set multicast_enabled=false --set auto_trust=true --set "download_dir=\"$T/dl\"" >"$T/r.log" 2>&1 & RD=$!
sleep 1
wine "$ROOT/build/x86/LanMessenger.exe" --data 'C:\lsmgui' --name 'XP 旧电脑' --port 0 --disco-port 47002 --targets 127.0.0.1:47001 >"$T/gui.log" 2>&1 &
M="$RB/lan-msg --ipc $T/r.sock"
for i in $(seq 40); do $M peers 2>/dev/null | grep -q "● XP\|XP 旧电脑.*trusted\|XP 旧电脑" && break; sleep 0.5; done
FX=$($M --json status | python3 -c 'import json,sys;d=json.load(sys.stdin);print([p["fingerprint"] for p in d["peers"] if "XP" in p["name"]][0])')
sleep 2
WID=$(xdotool search --name "局域网安全通讯" | head -1)
xdotool windowmove "$WID" 0 0; xdotool windowsize "$WID" 1000 640; xdotool windowactivate "$WID"; sleep 1
CL(){ eval $(xdotool getwindowgeometry --shell "$WID"); xdotool mousemove $((X+$1)) $((Y+$2)) click 1; }
CL 80 14; sleep 1; xdotool key alt+c; sleep 0.5; xdotool key t; sleep 1   # select contact, 联系人 > 信任
$M send "$FX" "你好！这是来自 v0.5 的消息。" >/dev/null
$M send "$FX" "Hello from v0.5 — 中文/English mixed text 测试" >/dev/null
head -c 200000 /dev/urandom > "$T/示例文件.bin"; $M send-file "$FX" "$T/示例文件.bin" >/dev/null
sleep 3
import -window root "$OUT/wine-01-main.png"
# click first contact in the list, type a reply and press Enter
CL 80 14; sleep 1.5
xdotool type --delay 30 "reply from XP GUI"; xdotool key Return; sleep 3
import -window root "$OUT/wine-02-chat.png"
$M history "$FX" > "$OUT/wine-rust-history.txt" 2>&1
# open the contacts menu
xdotool key alt+c; sleep 1; import -window root "$OUT/wine-03-menu.png"; xdotool key Escape Escape
kill $RD; wineserver -k; pkill openbox; kill $XV
echo "rust side history:"; cat "$OUT/wine-rust-history.txt"
