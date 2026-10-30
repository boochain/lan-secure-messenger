#!/usr/bin/env python3
"""XP Win32 GUI basics under wine (docs/ux-basics-checklist.md X1-X13): real key presses / clicks via xdotool, results asserted on a native lanmsg-cli peer, the window list and window.txt.
usage: xp_basics.py <LanMessenger.exe> <lanmsg-cli> <outdir>   (Xvfb, xdotool, ImageMagick, wine32)"""
import os, subprocess, sys, threading, time, shutil
exe, cli, out = [os.path.abspath(a) for a in sys.argv[1:4]]; os.makedirs(out, exist_ok=True)
themes = [sys.argv[5]] if "--only" in sys.argv else ["light", "dark"]
fails = []; W = f"/tmp/lsmxp-{os.getpid()}"; shutil.rmtree(W, True); os.makedirs(W)
def check(ok, what): print(("PASS " if ok else "FAIL ") + what, flush=True); ok or fails.append(what)
disp = ":%d" % (30 + os.getpid() % 20)
xv = subprocess.Popen(["Xvfb", disp, "-screen", "0", "1100x760x24", "-nolisten", "tcp"], stderr=subprocess.DEVNULL); time.sleep(1.5)
env = dict(os.environ, DISPLAY=disp, WINEDEBUG="-all", LANG="zh_CN.UTF-8", WINEPREFIX=os.environ.get("WINEPREFIX", os.path.expanduser("~/wpfx32")))
if shutil.which("openbox"): subprocess.Popen(["openbox", "--config-file", os.path.join(os.path.dirname(os.path.abspath(__file__)), "openbox-nodecor.xml")], env=env, stderr=subprocess.DEVNULL); time.sleep(1)
subprocess.run(["wineserver", "-k"], env=env, stderr=subprocess.DEVNULL); time.sleep(1)   # no stale explorer bound to an old display
# an XEmbed system tray (stalonetray) so wine can dock the notification icon; LSM_TRAY_BIN overrides the lookup
tray_bin = os.environ.get("LSM_TRAY_BIN") or shutil.which("stalonetray") or ("/workspace/tools-stalone/usr/bin/stalonetray" if os.path.exists("/workspace/tools-stalone/usr/bin/stalonetray") else None)
trayp = tray_bin and subprocess.Popen([tray_bin, "--geometry", "4x1-0+0", "--icon-size", "16"], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL); tray_ok = bool(trayp); time.sleep(0.8)
print("tray:", tray_bin or "none", flush=True)
if not os.path.isdir(env["WINEPREFIX"]): subprocess.run(["wineboot", "-i"], env=env)
for f in ["Tahoma", "MS Shell Dlg", "MS Shell Dlg 2", "Segoe UI", "SimSun", "Microsoft YaHei", "Microsoft YaHei UI"]:
    subprocess.run(["wine", "reg", "add", r"HKCU\Software\Wine\Fonts\Replacements", "/v", f, "/d", "Noto Sans CJK SC", "/f"], env=env, capture_output=True)
# real XP look without a Microsoft msstyles: wine draws its own non-client frame (X11 "Decorated"=N) in Luna colours
for k, v in {"ActiveTitle": "0 84 227", "GradientActiveTitle": "61 149 255", "TitleText": "255 255 255", "InactiveTitle": "70 96 180", "GradientInactiveTitle": "110 140 210",
             "InactiveTitleText": "255 255 255", "ActiveBorder": "0 84 227", "InactiveBorder": "122 150 223", "ButtonFace": "236 233 216", "Menu": "255 255 255"}.items():
    subprocess.run(["wine", "reg", "add", r"HKCU\Control Panel\Colors", "/v", k, "/d", v, "/f"], env=env, capture_output=True)
subprocess.run(["wine", "reg", "add", r"HKCU\Software\Wine\X11 Driver", "/v", "Decorated", "/d", "N", "/f"], env=env, capture_output=True)
import json
ignore = {"_reasons": {}}
class Proc:
    def __init__(n, argv, e=None):
        n.lines = []; n.p = subprocess.Popen(argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=e or env)
        threading.Thread(target=lambda: [n.lines.append(l.decode("utf-8", "replace").rstrip()) for l in n.p.stdout], daemon=True).start()
    def cmd(n, line, t=30):
        k = len(n.lines); n.p.stdin.write((line + "\n").encode()); n.p.stdin.flush(); end = time.time() + t
        while time.time() < end:
            if any(l.startswith(("OK", "ERR")) for l in n.lines[k:]): return n.lines[k:]
            time.sleep(0.1)
        return n.lines[k:]
def xdo(*a): return subprocess.run(["xdotool", *a], env=env, capture_output=True, text=True).stdout
base = 46100 + os.getpid() % 100 * 10
def peer(tag, name, port):
    p = Proc([cli, "--data", f"{W}/{tag}", "--name", name, "--no-discovery", "--port", str(port), "--disco-port", str(port + 1)])
    p.fp = next(l for l in p.cmd("id", 30) if l.startswith("OK id")).split()[2]; return p
def shot(name, icons=None):
    xdo("mousemove", "1095", "755"); time.sleep(0.7)   # pointer off the window: no hover state / tooltip in the shot
    g = {k: int(v) for k, v in (l.split("=") for l in xdo("getwindowgeometry", "--shell", wid).split())}
    path = f"{out}/{name}.png"
    subprocess.run(["import", "-window", "root", "-crop", f"{g['WIDTH']}x{g['HEIGHT']}+{g['X']}+{g['Y']}", "+repage", path], env=env); print("shot", path, flush=True)
    try: r = [[int(v) for v in l.split()[2].split(",")] for l in open(icons_file, encoding="utf-8") if l.startswith("ICON ")]
    except OSError: r = []
    if r: ignore[os.path.basename(path)] = r; ignore["_reasons"][os.path.basename(path)] = "decorative icons only: avatar monogram tiles with their online dot (OCR merges the two; white-on-avatar colours come from the shared palette), photo thumbnails (user content), system scrollbars and the window caption/frame (OS theme colours), online presence ( is also spelled out as 在线 / N 人在线) and the trusted-identity shield (also spelled out as 已验证身份); bounds reported by the app (LSM_UI_TEST)"
    return path
def click(x, y, b="1"):
    g = {k: int(v) for k, v in (l.split("=") for l in xdo("getwindowgeometry", "--shell", wid).split())}; xdo("mousemove", str(g["X"] + x), str(g["Y"] + y), "click", b)
def wins(name): return [w for w in xdo("search", "--onlyvisible", "--name", name).split() if w]
def wait(f, t=10):
    end = time.time() + t
    while time.time() < end:
        v = f()
        if v: return v
        time.sleep(0.3)
    return None
def geom(w): return {k: int(v) for k, v in (l.split("=") for l in xdo("getwindowgeometry", "--shell", w).split())}
def received(p, text, t=10): return wait(lambda: any(text in l for l in p.lines), t)
theme = "light"; port = base; data = f"C:\\lsmxpb{os.getpid()}"
A = peer("a", "王工", port + 2)
exe_dir_txt = os.path.join(os.path.dirname(exe), "window.txt")
if os.path.exists(exe_dir_txt): os.remove(exe_dir_txt)
icons_file = os.path.join(env["WINEPREFIX"], "drive_c", data[3:], "ui_icons.txt")
def start(extra=None):
    g = Proc(["wine", exe, "--data", data, "--name", "Bill", "--port", str(port), "--disco-port", str(port + 1), "--theme", theme, "--auto-trust"], dict(env, LSM_UI_TEST="1", **(extra or {})))
    w = wait(lambda: (wins("局域网安全通讯 XP") or [None])[0], 40); return g, w
dropf = "/tmp/xp-drop-测试文件.txt"; open(dropf, "w").write("drop test\n")
g, wid = start({"LSM_UI_TEST_DROP": "Z:" + dropf.replace("/", "\\")}); check(bool(wid), "XP window appeared")
xdo("windowmove", wid, "20", "20"); xdo("windowsize", wid, "1024", "700"); time.sleep(2)
A.cmd(f"add 127.0.0.1:{port}")
xfp = wait(lambda: next((l.split()[1] for l in A.cmd("peers") if l.startswith("PEER") and "Bill" in l), None), 30); A.cmd(f"trust {xfp[:12]}")
A.cmd(f"send {xfp[:12]} 基础检查：请回复。"); time.sleep(2)
click(200, 160); time.sleep(1.5)
# X8 drag & drop: the test hook hands the path to the real WM_DROPFILES handler once a chat is open -> the peer gets the file offer
off = wait(lambda: next((l for l in A.lines if "xp-drop-测试文件.txt" in l), None), 20)
check(bool(off), f"X8 a dropped file (WM_DROPFILES, shell HDROP) is offered to the open chat ({(off or '')[:70]!r})")
# X1 Esc closes dialogs: Ctrl+N prompt, 设置 > 中转服务器 dialog
xdo("key", "ctrl+n"); d = wait(lambda: wins("按 IP 添加"), 5); xdo("key", "Escape"); gone = wait(lambda: not wins("按 IP 添加"), 5)
check(bool(d) and bool(gone), "X1 Esc closes the 按 IP 添加 dialog (Ctrl+N)")
click(260, 46); time.sleep(1); xdo("key", "r"); d = wait(lambda: wins("^中转服务器$"), 5); xdo("key", "Escape"); gone = wait(lambda: not wins("^中转服务器$"), 5)
check(bool(d) and bool(gone), "X1 Esc closes the 中转服务器 dialog")
# X2 Enter sends, Shift+Enter new line (the peer receives one message with two lines)
click(600, 640); time.sleep(0.5); xdo("type", "--delay", "30", "line-one"); xdo("key", "shift+Return"); xdo("type", "--delay", "30", "line-two")
time.sleep(0.5); check(not received(A, "line-one", 2), "X2 Shift+Enter does not send")
xdo("key", "Return"); r2 = wait(lambda: next((l for l in A.lines if "\tin\t" in l and "line-two" in l), None), 10)
check(bool(r2), f"X2 Enter sends one message with both lines ({(r2 or '')[-40:]!r})")
# X4 Ctrl+F focuses search: typed text filters the list (王工 hidden for 'zzz')
xdo("key", "ctrl+f"); xdo("type", "zzz"); time.sleep(1); shot("xp-basics-search-filter"); xdo("key", "Escape"); time.sleep(0.5)
# X9 context menu on a message + Esc closes it; X10 Ctrl+C copies the focused bubble (pasted back with Ctrl+V and sent)
# wine maps menus/tooltips as unnamed override-redirect top-levels: detect them as new visible top-level windows of a plausible size
def tops(): return set(xdo("search", "--onlyvisible", "--maxdepth", "1", "--name", "").split())
def popups(base, minw, minh):
    out_ = []
    for w in tops() - base:
        try: gw = geom(w)
        except Exception: continue
        if gw.get("WIDTH", 0) >= minw and gw.get("HEIGHT", 0) >= minh: out_.append((w, gw))
    return out_
base = tops(); click(400, 210, "3"); m = wait(lambda: popups(base, 120, 60), 4); time.sleep(0.5); shot("xp-basics-msg-menu"); xdo("key", "Escape"); time.sleep(0.6)
mg = popups(base, 120, 60)
check(bool(m) and not mg, "X9 right-click on a message opens the menu" + (f" ({m[0][1]['WIDTH']}x{m[0][1]['HEIGHT']} px)" if m else "") + ", Esc closes it")
click(400, 210); time.sleep(0.4); xdo("key", "ctrl+c"); time.sleep(0.4); click(600, 640); xdo("key", "ctrl+v"); time.sleep(0.4); xdo("key", "Return")
check(bool(wait(lambda: any("\tin\t" in l and "基础检查：请回复。" in l for l in A.lines), 8)), "X10 Ctrl+C on a bubble copies its text (pasted with Ctrl+V and received by the peer)")
# X11 tooltip on hover over 发送
gg = geom(wid); base = tops(); xdo("mousemove", str(gg["X"] + 965), str(gg["Y"] + 640)); tt = wait(lambda: popups(base, 60, 12), 4); shot_tt = f"{out}/xp-basics-tooltip.png"
subprocess.run(["import", "-window", "root", "-crop", f"{gg['WIDTH']}x{gg['HEIGHT']}+{gg['X']}+{gg['Y']}", "+repage", shot_tt], env=env)
check(bool(tt), "X11 hovering 发送 shows a tooltip")
# X13 destructive action confirmed: bubble menu 删除这条消息（仅本机）… asks first, Esc cancels
click(400, 210, "3"); time.sleep(0.8); xdo("key", "d"); pr = wait(lambda: wins("^删除消息$"), 5); shot("xp-basics-delete-confirm"); xdo("key", "Escape"); prg = wait(lambda: not wins("^删除消息$"), 5)
check(bool(pr) and bool(prg), "X13 删除这条消息 asks for confirmation (删除消息 prompt), Esc cancels")
# X5 close: hides to the notification area when there is one (else exits); X12 a second start re-opens the running window
# Alt+F4 -> WM_SYSCOMMAND SC_CLOSE -> WM_CLOSE, i.e. the real close path. (xdotool windowclose destroys the X11
# window behind wine's back, so the hidden Win32 window could never be mapped again and X12 always failed.)
xdo("windowfocus", "--sync", wid); time.sleep(0.3); xdo("key", "alt+F4"); time.sleep(2); hidden = not wins("局域网安全通讯 XP"); alive = g.p.poll() is None
check(hidden and (alive or not tray_ok), f"X5 close hides the window and keeps running (hidden={hidden}, running={alive})" if alive else f"X5 close exits (no notification area; tray={tray_ok})")
if alive:
    p2 = subprocess.Popen(["wine", exe], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL); back = wait(lambda: wins("局域网安全通讯 XP"), 15)
    ended = wait(lambda: p2.poll() is not None, 15)
    check(bool(back) and bool(ended) and len(wins("局域网安全通讯 XP")) == 1, f"X12 second start exits and re-opens the running window (single instance; shown={bool(back)} exited={bool(ended)} windows={len(wins('局域网安全通讯 XP'))})")
    if not back:
        g.p.kill(); subprocess.run(["wineserver", "-k"], env=env); g, _w = start(); wid = _w
    wid = (wins("局域网安全通讯 XP") or [wid])[0]
if not alive: g, wid = start(); time.sleep(2)
# X6 window memory: move/resize, 文件 > 退出, restart at the same place
xdo("windowmove", wid, "60", "50"); xdo("windowsize", wid, "900", "640"); time.sleep(1.5); want = geom(wid)
click(30, 46); time.sleep(0.8); xdo("key", "x"); ex = wait(lambda: g.p.poll() is not None, 15); check(bool(ex), "文件 > 退出 exits")
try: subprocess.run(["wineserver", "-w"], env=env, timeout=20)
except subprocess.TimeoutExpired: pass
g, wid = start(); time.sleep(2); have = geom(wid) if wid else {}
check(bool(wid) and abs(have.get("X", 0) - want["X"]) <= 4 and abs(have.get("Y", 0) - want["Y"]) <= 4 and abs(have.get("WIDTH", 0) - want["WIDTH"]) <= 4 and abs(have.get("HEIGHT", 0) - want["HEIGHT"]) <= 4,
      f"X6 window placement restored ({want['X']},{want['Y']} {want['WIDTH']}x{want['HEIGHT']} -> {have.get('X')},{have.get('Y')} {have.get('WIDTH')}x{have.get('HEIGHT')})")
open(f"{out}/gui-basics.log", "w").write("\n".join(g.lines) + "\n--- peer\n" + "\n".join(A.lines))
subprocess.run(["wineserver", "-k"], env=env); A.p.kill()
json.dump(ignore, open(f"{out}/audit_ignore.json", "w"), ensure_ascii=False, indent=1)
trayp and trayp.kill(); xv.kill(); print("RESULT", "FAIL" if fails else "PASS", len(fails))
sys.exit(1 if fails else 0)  # failures used to be printed only; the step must go red
