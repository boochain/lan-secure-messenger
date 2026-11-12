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
# real XP look without a Microsoft msstyles: wine draws its own non-client frame (X11 "Decorated"=N) in Luna colours
for kv in "ActiveTitle=0 84 227" "GradientActiveTitle=61 149 255" "TitleText=255 255 255" "InactiveTitle=70 96 180" "GradientInactiveTitle=110 140 210" "InactiveTitleText=255 255 255" "ActiveBorder=0 84 227" "InactiveBorder=122 150 223" "ButtonFace=236 233 216" "Menu=255 255 255"; do
  wine reg add 'HKCU\Control Panel\Colors' /v "${kv%%=*}" /d "${kv#*=}" /f >/dev/null 2>&1; done
wine reg add 'HKCU\Software\Wine\X11 Driver' /v Decorated /d N /f >/dev/null 2>&1
"$RB/lan-daemon" --data-dir "$T/r" --ipc "$T/r.sock" --set 'display_name="王工（v0.5）"' --set listen_port=0 --set discovery_port=47011 \
  --set announce_interval_secs=1 --set multicast_enabled=false --set auto_trust=true --set "download_dir=\"$T/dl\"" >"$T/r.log" 2>&1 & RD=$!
sleep 1; M="$RB/lan-msg --ipc $T/r.sock"
ICONS="$WINEPREFIX/drive_c/lsmshots/ui_icons.txt"
shot(){ local tries=0 mean
  xdotool mousemove 1095 755; sleep 0.7  # pointer off the window: no hover state / tooltip in the shot
  while true; do
    [ -f "$ICONS" ] && cp "$ICONS" "$OUT/$1.icons"
    local G; G=$(xwininfo -id "$WID" 2>/dev/null || true)
    local X=$(echo "$G"|awk '/Absolute upper-left X/{print $4}') Y=$(echo "$G"|awk '/Absolute upper-left Y/{print $4}') W=$(echo "$G"|awk '/Width:/{print $2}') H=$(echo "$G"|awk '/Height:/{print $2}')
    if [ -z "$W" ] || [ "$W" -lt 50 ]; then
      ensure_main; tries=$((tries+1)); [ "$tries" -ge 4 ] && { echo "shot $1: no geometry" >&2; return 1; }; continue
    fi
    import -window root -crop ${W}x${H}+${X}+${Y} +repage "$OUT/$1.png"
    mean=$(identify -format '%[fx:mean]' "$OUT/$1.png" 2>/dev/null || echo 0)
    # nearly black 1000x660 crop (mean≈0.006) means the main window was unmapped
    if awk -v m="$mean" 'BEGIN{exit !(m+0<0.04)}'; then
      if [ "$tries" -lt 5 ]; then
        echo "shot $1 too dark (mean=$mean), remapping main window" >&2
        ensure_main; tries=$((tries+1)); continue
      fi
      echo "FATAL shot $1 still too dark after $tries remaps (mean=$mean) — not saving a blank grab" >&2
      return 1
    fi
    echo "shot $1 ${W}x${H}+${X}+${Y} mean=$mean"; break
  done; }
shot_or_die(){ shot "$1" || exit 1; }
# crop to a named popup (first-run / QR) if it is visible, else fall back to the main window
shot_named(){ local NAME=$2 ID G X Y W H
  ID=$(xdotool search --name "$NAME" 2>/dev/null | tail -1)
  if [ -n "$ID" ]; then
    G=$(xwininfo -id "$ID")
    X=$(echo "$G"|awk '/Absolute upper-left X/{print $4}') Y=$(echo "$G"|awk '/Absolute upper-left Y/{print $4}') W=$(echo "$G"|awk '/Width:/{print $2}') H=$(echo "$G"|awk '/Height:/{print $2}')
    if [ -n "$W" ] && [ "$W" -gt 50 ]; then
      xdotool mousemove 1095 755; sleep 0.5
      import -window root -crop ${W}x${H}+${X}+${Y} +repage "$OUT/$1.png"
      mean=$(identify -format '%[fx:mean]' "$OUT/$1.png" 2>/dev/null || echo 0)
      if awk -v m="$mean" 'BEGIN{exit !(m+0<0.04)}'; then
        echo "shot $1 ($NAME) too dark (mean=$mean), falling back to main window" >&2
        shot_or_die "$1"; return
      fi
      echo "shot $1 ${W}x${H}+${X}+${Y} ($NAME) mean=$mean"; return
    fi
  fi
  shot_or_die "$1"
}
CL(){ eval $(xdotool getwindowgeometry --shell "$WID"); xdotool mousemove $((X+$1)) $((Y+$2)) click 1; }
menu(){ xdotool key "alt+$1"; sleep 1.2; }
# Menubar popups cover the left search box. An empty box owner-draws
# "搜索联系人和群组 (Ctrl+F)"; OCR then samples mixed menu/hint pixels
# (xp-light-05-menu-groups: "(Ctrl" as #FEEFCB on #7F9DA6). Type into
# search first so the hint is not painted, then restore after the shot.
fill_search(){ xdotool key ctrl+f; sleep 0.25; xdotool type --delay 15 "x"; sleep 0.25; }
# After the owned image-viewer popup is destroyed, Wine may leave the main
# window unmapped. Re-find it, map it, and wait until X says it is viewable
# so later crops are not a 1000x660 black rectangle.
ensure_main(){
  local i G
  for i in 1 2 3 4 5 6 8 10; do
    WID=$(xdotool search --name "局域网安全通讯" 2>/dev/null | head -1)
    [ -n "$WID" ] || { sleep 0.3; continue; }
    xdotool windowmap "$WID" 2>/dev/null || true
    xdotool windowmove "$WID" 20 20 2>/dev/null || true
    xdotool windowsize "$WID" 1000 660 2>/dev/null || true
    xdotool windowactivate --sync "$WID" 2>/dev/null || true
    sleep 0.3
    G=$(xwininfo -id "$WID" 2>/dev/null || true)
    echo "$G" | grep -q "Map State: IsViewable" && return 0
  done
  echo "ensure_main: window not viewable" >&2
}
clear_search(){ xdotool key ctrl+f; sleep 0.15; xdotool key ctrl+a BackSpace; sleep 0.2; }
rm -rf "$WINEPREFIX/drive_c/lsmshots"
mkdir -p "$WINEPREFIX/drive_c/lsmshots"
cp "$ROOT/tests/assets/lsm-qr-sample.png" "$WINEPREFIX/drive_c/lsmshots/lsm-qr-sample.png"
for TH in light dark; do
  LSM_UI_TEST=1 LSM_SHOT_QR_FILE="C:\\lsmshots\\lsm-qr-sample.png" wine "$EXE" --data "C:\\lsmshots" --name "XP 旧电脑" --port 0 --disco-port 47012 --targets 127.0.0.1:47011 --theme $TH >"$T/gui-$TH.log" 2>&1 &
  for i in $(seq 60); do WID=$(xdotool search --name "局域网安全通讯" 2>/dev/null | head -1); [ -n "$WID" ] && break; sleep 0.5; done
  xdotool windowmove "$WID" 20 20; xdotool windowsize "$WID" 1000 660; xdotool windowactivate "$WID"; sleep 2
  shot_or_die "xp-$TH-01-welcome-empty"  # welcome after auto-completed first-run
  for i in $(seq 40); do $M peers 2>/dev/null | grep -q "XP" && break; sleep 0.5; done
  FX=$($M --json status | python3 -c 'import json,sys;d=json.load(sys.stdin);print([p["fingerprint"] for p in d["peers"] if "XP" in p["name"]][0])')
  CL 150 160; sleep 1
  if [ $TH = light ]; then menu c; xdotool key t; sleep 1
    $M send "$FX" "你好！这是来自 v0.5 的消息。下午三点评审会改到 302 会议室。" >/dev/null
    $M send "$FX" "Hello from v0.5 — 中文/English mixed text 测试" >/dev/null
    head -c 200000 /dev/urandom > "$T/示例文件.bin"; $M send-file "$FX" "$T/示例文件.bin" >/dev/null; sleep 3
    cp "$ROOT/stickers/thumb.png" "$T/thumb.png"
    convert -size 560x380 gradient:#3b4a7a-#d98a5b -fill "#f6d27a" -draw "circle 380,170 380,215" "$T/photo.png"
    cp "$T/photo.png" "$OUT/content-photo.png"
    $M send-file "$FX" "$T/photo.png" >/dev/null
    $M send-file "$FX" "$T/thumb.png" >/dev/null; sleep 4
    CL 150 160; sleep 1; xdotool type --delay 30 "OK, 收到 thanks"; xdotool key Return; sleep 2
  fi
  CL 150 160; sleep 1.5; shot_or_die "xp-$TH-02-chat" || exit 1
  # LSMF_chat_list_menu: right-click the selected conversation row
  CL 150 160; sleep 0.4
  eval $(xdotool getwindowgeometry --shell "$WID")
  xdotool mousemove $((X+150)) $((Y+160)) click 3
  sleep 1
  shot_or_die "xp-$TH-12-conv-menu"  # conv-menu LSMF_chat_list_menu LSMF_chat_pin LSMF_chat_mark_read LSMF_chat_mark_unread LSMF_chat_mute LSMF_chat_delete
  xdotool key Escape; sleep 0.5
  # LSMF_nav_* pages via LSM_UI_TEST Ctrl+Shift+1..8 (no mnemonic clash with 生成 PIN)
  # LSMF_nav_chats LSMF_nav_contacts LSMF_nav_groups LSMF_nav_transfers LSMF_nav_files LSMF_nav_settings LSMF_nav_profile LSMF_nav_diagnostics
  ensure_main
  xdotool key ctrl+shift+1; sleep 0.7; shot_or_die "xp-$TH-page-chats"
  xdotool key ctrl+shift+2; sleep 0.7; shot_or_die "xp-$TH-page-contacts"
  xdotool key ctrl+shift+3; sleep 0.7; shot_or_die "xp-$TH-page-groups"
  xdotool key ctrl+shift+4; sleep 0.7; shot_or_die "xp-$TH-page-transfers"
  xdotool key ctrl+shift+5; sleep 0.7; shot_or_die "xp-$TH-page-files"
  xdotool key ctrl+shift+6; sleep 0.7; shot_or_die "xp-$TH-page-settings"
  xdotool key ctrl+shift+7; sleep 0.7; shot_or_die "xp-$TH-page-profile"
  xdotool key ctrl+shift+8; sleep 0.7; shot_or_die "xp-$TH-page-diagnostics"
  xdotool key ctrl+shift+1; sleep 0.5
  fill_search; menu f; shot_or_die "xp-$TH-03-menu-file"; xdotool key Escape Escape; clear_search
  fill_search; menu c; shot_or_die "xp-$TH-04-menu-contacts"; xdotool key Escape Escape; clear_search
  fill_search; menu g; shot_or_die "xp-$TH-05-menu-groups"; xdotool key Escape Escape; clear_search
  fill_search; menu s; shot_or_die "xp-$TH-06-menu-settings"; xdotool key Escape Escape; clear_search
  menu c; xdotool key a; sleep 0.4; xdotool key a; sleep 1.5; xdotool type "192.168.1.20"; sleep 0.5; shot_or_die "xp-$TH-07-dialog-add-ip"
  xdotool key ctrl+a; xdotool type "bad address"; xdotool key Return; sleep 1.5; shot_or_die "xp-$TH-08-error"; xdotool key Return; sleep 0.8
  menu c; xdotool key p; sleep 0.4; xdotool key p; sleep 1.5; shot_or_die "xp-$TH-09-pin"; xdotool key Return; sleep 0.8
  menu g; xdotool key c; sleep 1.5; shot_or_die "xp-$TH-10-dialog-group-picker"; xdotool key Escape; sleep 0.8
  menu h; xdotool key a; sleep 1.5; shot_or_die "xp-$TH-11-about"; xdotool key Return; sleep 0.8
  xdotool windowactivate "$WID"; sleep 0.2
  close_named() {
    local title="$1" i rid
    for i in 1 2 3 4 5 6; do
      rid=$(xdotool search --onlyvisible --name "$title" 2>/dev/null | tail -1 || true)
      if [ -z "$rid" ]; then break; fi
      xdotool windowactivate --sync "$rid" key Escape || true
      sleep 0.25
    done
    xdotool windowactivate "$WID" >/dev/null 2>&1 || true
    sleep 0.15
  }
  xdotool key F7; sleep 1.2; shot_named "xp-$TH-13-first-run" "欢迎使用局域网安全通讯"  # first-run LSMF_account_mode
  xdotool key Return; sleep 0.6
  close_named "欢迎使用局域网安全通讯"
  xdotool key shift+F7; sleep 1.2; shot_named "xp-$TH-13b-recovery" "恢复密钥"  # recovery LSMF_recovery_key
  close_named "恢复密钥"
  xdotool key F8; sleep 1.2; shot_named "xp-$TH-14-my-qr" "我的二维码"  # my-qr LSMF_qr_profile
  close_named "我的二维码"
  xdotool key shift+F8; sleep 1.2; shot_named "xp-$TH-15-server-qr" "服务器二维码"  # server-qr LSMF_qr_server
  close_named "服务器二维码"
  xdotool key ctrl+F8; sleep 1.2; shot_named "xp-$TH-16-pair-qr" "配对新设备"  # pair-qr LSMF_qr_pair
  close_named "配对新设备"
  xdotool key F9; sleep 1.2; shot_named "xp-$TH-17-scan-paste" "扫描或粘贴链接"  # scan LSMF_qr_scan
  close_named "扫描或粘贴链接"
  xdotool key shift+F9; sleep 1.2; shot_named "xp-$TH-18-scan-image" "从图片识别"  # scan-image LSMF_qr_scan
  close_named "从图片识别"
  # Image viewer last: under Wine, DestroyWindow on the owned viewer can leave the
  # main window unmapped (1000x660 black crop). ensure_main often sees IsViewable
  # while the client area stays black, so do not take any later shots after this.
  ensure_main
  CL 150 160; sleep 1
  xdotool key F6; sleep 1.5; shot_or_die "xp-$TH-image-viewer" || exit 1
  for _ in 1 2 3 4 5; do xdotool key plus; sleep 0.12; done
  sleep 0.6; shot_or_die "xp-$TH-image-viewer-zoom" || exit 1
  xdotool key Escape; sleep 0.5
  wineserver -k; sleep 2
done
# app-reported decorative icon rects (online dots, shields) -> ignore file for the colour audit
python3 - "$OUT" <<'PY'
import glob, json, os, sys, statistics
from PIL import Image
out = sys.argv[1]; ig = {"_reasons": {}}
def hint_bar_ignore(png):
    im = Image.open(png).convert("RGB"); w, h = im.size; pix = im.load()
    def sig(y, x0, x1):
        y = max(0, min(h - 1, y)); xs = list(range(x0, x1, 4)) or [min(x0, w - 1)]
        cs = [pix[x, y] for x in xs]
        return tuple(int(statistics.median([c[i] for c in cs])) for i in range(3))
    def dist(a, b):
        return ((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2) ** 0.5
    # Center column, not the side margins: a zoomed picture is letterboxed in
    # black, so side samples never see it and a wide reserved strip (h-40)
    # swallows the bottom of the photo (Twemoji yellow flagged as stray).
    cx0, cx1 = max(0, w // 4), min(w, (3 * w) // 4)
    if cx1 <= cx0: cx0, cx1 = 0, w
    base = sig(h - 6, cx0, cx1)
    cut = max(1, h - 24)  # painted hint bar is scale_px(22) plus the OS border
    for y in range(h - 8, max(0, h - 100), -1):
        if dist(sig(y, cx0, cx1), base) > 18:
            cut = min(y + 2, h - 24); break
    return [[0, 0, w, max(1, cut)]]
for f in glob.glob(f"{out}/*.icons"):
    r = [[int(v) for v in l.split()[2].split(",")] for l in open(f, encoding="utf-8") if l.startswith("ICON ")]
    n = os.path.basename(f)[:-6] + ".png"
    if r: ig[n] = r; ig["_reasons"][n] = "decorative icons only: avatar monogram tiles with their online dot (OCR merges the two; white-on-avatar colours come from the shared palette), photo thumbnails (user content), system scrollbars and the window caption/frame (OS theme colours), online presence ( also spelled out as 在线 / N 人在线) and the trusted-identity shield (also 已验证身份); bounds reported by the app (LSM_UI_TEST)"
    os.remove(f)
for png in glob.glob(f"{out}/*image-viewer*.png"):
    n = os.path.basename(png)
    ig[n] = hint_bar_ignore(png)
    ig["_reasons"][n] = "user-sent image in the viewer (fit/zoom interpolates colours); the hint bar under the picture is still audited"
json.dump(ig, open(f"{out}/audit_ignore.json", "w"), ensure_ascii=False, indent=1)
PY
kill $RD; pkill -f "openbox" ; kill $XV
ls "$OUT"
