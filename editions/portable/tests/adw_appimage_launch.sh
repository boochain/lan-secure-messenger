#!/bin/sh
# Launch the GTK 4 AppImage on a stock distro (CI: debian:12) under Xvfb and take a screenshot.
# usage: adw_appimage_launch.sh <AppImage> <outdir>
set -eu
APP=$(readlink -f "$1"); OUT=$2; mkdir -p "$OUT"; chmod +x "$APP"
export APPIMAGE_EXTRACT_AND_RUN=1 NO_CLEANUP=1 DISPLAY=:77 GDK_BACKEND=x11 GSK_RENDERER=cairo NO_AT_BRIDGE=1 LANG=C.UTF-8
Xvfb :77 -screen 0 1280x860x24 -nolisten tcp >/dev/null 2>&1 & XV=$!; sleep 2
eval "$(dbus-launch --sh-syntax)"
D=$(mktemp -d)
LSM_UI_TEST=1 LSM_QUIT_ON_CLOSE=1 "$APP" --data "$D/gui" --name Bill --no-discovery --port 47901 --disco-port 47902 > "$OUT/launch.log" 2>&1 & G=$!
i=0; while [ $i -lt 60 ] && ! grep -q LSM_FP "$OUT/launch.log"; do sleep 0.5; i=$((i+1)); done
FP=$(awk '/^LSM_FP/{print $2}' "$OUT/launch.log")
"$APP" cli --data "$D/peer" --name 王工 --no-discovery --port 47903 --disco-port 47904 > "$OUT/peer.log" 2>&1 <<CMDS &
add 127.0.0.1:47901
sleep 2000
send ${FP%%-*} 你好，这是 Debian 12 上的 AppImage 测试消息。
sleep 6000
quit
CMDS
sleep 7
import -window root -crop 1024x694+0+0 +repage "$OUT/appimage-debian12-light.png"
cat "$OUT/launch.log"; grep -E "LSM_TOOLKIT gtk 4\.(1[2-9]|[2-9][0-9])\.[0-9]+ adwaita 1\.([4-9]|[1-9][0-9])\." "$OUT/launch.log"
kill $G 2>/dev/null || true; kill $XV 2>/dev/null || true
echo "PASS AppImage launched on $(. /etc/os-release; echo $PRETTY_NAME) with bundled GTK/libadwaita"
