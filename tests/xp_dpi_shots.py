#!/usr/bin/env python3
"""XP Win32 GUI at 144 DPI (150 %) under wine (checklist X7): LogPixels=144, a chat with messages, a file card and the
relay/add dialogs, light + dark shots to look at for clipping; LogPixels is put back to 96 afterwards.
usage: xp_dpi_shots.py <LanMessenger.exe> <lanmsg-cli> <outdir>"""
import os, subprocess, sys, threading, time, shutil
exe, cli, out = [os.path.abspath(a) for a in sys.argv[1:4]]; os.makedirs(out, exist_ok=True)
themes = [sys.argv[5]] if "--only" in sys.argv else ["light", "dark"]
fails = []; W = f"/tmp/lsmxp-{os.getpid()}"; shutil.rmtree(W, True); os.makedirs(W)
def check(ok, what): print(("PASS " if ok else "FAIL ") + what, flush=True); ok or fails.append(what)
disp = ":%d" % (30 + os.getpid() % 20)
xv = subprocess.Popen(["Xvfb", disp, "-screen", "0", "1700x1150x24", "-nolisten", "tcp"], stderr=subprocess.DEVNULL); time.sleep(1.5)
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

def wins(name): return [w for w in xdo("search", "--onlyvisible", "--name", name).split() if w]
def wait(f, t=20):
    end = time.time() + t
    while time.time() < end:
        v = f()
        if v: return v
        time.sleep(0.4)
def setdpi(v):
    for k in (r"HKCU\Control Panel\Desktop", r"HKLM\System\CurrentControlSet\Hardware Profiles\Current\Software\Fonts"):
        subprocess.run(["wine", "reg", "add", k, "/v", "LogPixels", "/t", "REG_DWORD", "/d", str(v), "/f"], env=env, capture_output=True)
    subprocess.run(["wineserver", "-k"], env=env, capture_output=True); time.sleep(1)
def snap(name):
    xdo("mousemove", "1695", "1145"); time.sleep(0.7); path = f"{out}/{name}.png"
    subprocess.run(["import", "-window", "root", "-crop", "1600x1080+0+0", "+repage", path], env=env); print("shot", path, flush=True)
setdpi(144)
try:
    for i, theme in enumerate(themes):
        port = base + i * 5; data = f"C:\\lsm-dpi-{os.getpid()}-{theme}"
        A = peer(f"a{theme}", "王工", port + 2)
        g = Proc(["wine", exe, "--data", data, "--name", "Bill", "--port", str(port), "--disco-port", str(port + 1), "--theme", theme, "--auto-trust"], dict(env, LSM_UI_TEST="1"))
        wid = wait(lambda: (wins("局域网安全通讯 XP") or [None])[0], 40); check(bool(wid), f"[{theme}] window at 144 DPI")
        xdo("windowmove", wid, "0", "0"); xdo("windowsize", wid, "1536", "1050"); time.sleep(2)
        A.cmd(f"add 127.0.0.1:{port}")
        fp = wait(lambda: next((l.split()[1] for l in A.cmd("peers") if l.startswith("PEER") and "Bill" in l), None), 30); A.cmd(f"trust {fp[:12]}")
        A.cmd(f"send {fp[:12]} 下午三点的评审会改到 302 会议室，请带上第 4 页的修改稿。"); fpath = f"{W}/季度报告-Q3-最终版.pdf"; open(fpath, "wb").write(os.urandom(300000))
        A.cmd(f"sendfile {fp[:12]} {fpath}"); time.sleep(3)
        xdo("mousemove", "250", "240", "click", "1"); time.sleep(2); snap(f"xp-{theme}-144dpi-chat")
        xdo("key", "ctrl+n"); wait(lambda: wins("按 IP 添加"), 5); time.sleep(0.8); snap(f"xp-{theme}-144dpi-add"); xdo("key", "Escape"); time.sleep(0.8)
        subprocess.run(["wineserver", "-k"], env=env); A.p.kill(); time.sleep(1)
finally:
    setdpi(96)
xv.kill(); print("RESULT", "FAIL" if fails else "PASS", len(fails))
