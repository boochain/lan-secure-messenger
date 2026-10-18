#!/usr/bin/env python3
"""XP Win32 GUI under wine next to two native C-edition peers (lanmsg-cli): chat shots in light + dark, plus UI checks.
usage: xp_gui_cli_shots.py <LanMessenger.exe> <lanmsg-cli> <outdir> [--only light|dark]   (Xvfb, xdotool, ImageMagick, wine32)"""
import os, subprocess, sys, threading, time, shutil
exe, cli, out = [os.path.abspath(a) for a in sys.argv[1:4]]; os.makedirs(out, exist_ok=True)
themes = [sys.argv[5]] if "--only" in sys.argv else ["light", "dark"]
fails = []; W = f"/tmp/lsmxp-{os.getpid()}"; shutil.rmtree(W, True); os.makedirs(W)
def check(ok, what): print(("PASS " if ok else "FAIL ") + what, flush=True); ok or fails.append(what)
disp = ":%d" % (30 + os.getpid() % 20)
xv = subprocess.Popen(["Xvfb", disp, "-screen", "0", "1100x760x24", "-nolisten", "tcp"], stderr=subprocess.DEVNULL); time.sleep(1.5)
env = dict(os.environ, DISPLAY=disp, WINEDEBUG="-all", LANG="zh_CN.UTF-8", WINEPREFIX=os.environ.get("WINEPREFIX", os.path.expanduser("~/wpfx32")))
if shutil.which("openbox"): subprocess.Popen(["openbox", "--config-file", os.path.join(os.path.dirname(os.path.abspath(__file__)), "openbox-nodecor.xml")], env=env, stderr=subprocess.DEVNULL); time.sleep(1)
if not os.path.isdir(env["WINEPREFIX"]): subprocess.run(["wineboot", "-i"], env=env)
for f in ["Tahoma", "MS Shell Dlg", "MS Shell Dlg 2", "Segoe UI", "SimSun", "Microsoft YaHei", "Microsoft YaHei UI"]:
    subprocess.run(["wine", "reg", "add", r"HKCU\Software\Wine\Fonts\Replacements", "/v", f, "/d", "Noto Sans CJK SC", "/f"], env=env, capture_output=True)
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
base = 47100 + os.getpid() % 100 * 10
def peer(tag, name, port):
    p = Proc([cli, "--data", f"{W}/{tag}", "--name", name, "--no-discovery", "--port", str(port), "--disco-port", str(port + 1)])
    p.fp = next(l for l in p.cmd("id", 30) if l.startswith("OK id")).split()[2]; return p
def shot(name):
    g = {k: int(v) for k, v in (l.split("=") for l in xdo("getwindowgeometry", "--shell", wid).split())}
    path = f"{out}/{name}.png"
    subprocess.run(["import", "-window", "root", "-crop", f"{g['WIDTH']}x{g['HEIGHT']}+{g['X']}+{g['Y']}", "+repage", path], env=env); print("shot", path, flush=True); return path
def click(x, y, b="1"):
    g = {k: int(v) for k, v in (l.split("=") for l in xdo("getwindowgeometry", "--shell", wid).split())}; xdo("mousemove", str(g["X"] + x), str(g["Y"] + y), "click", b)
img = f"{W}/季度报告-Q3-最终版.pdf"; open(img, "wb").write(os.urandom(2400 * 1024))
for i, theme in enumerate(themes):
    port = base + i * 6; data = f"C:\\lsmxp{os.getpid()}{theme}"
    A = peer(f"a{theme}", "王工", port + 2); B = peer(f"b{theme}", "Alice Chen", port + 4)
    g = Proc(["wine", exe, "--data", data, "--name", "Bill", "--port", str(port), "--disco-port", str(port + 1), "--theme", theme, "--auto-trust"])
    wid = ""
    for _ in range(80):
        wid = (xdo("search", "--name", "局域网安全通讯").split() or [""])[0]
        if wid: break
        time.sleep(0.5)
    check(bool(wid), f"[{theme}] XP window appeared")
    xdo("windowmove", wid, "20", "20"); xdo("windowsize", wid, "1024", "700"); time.sleep(2)
    shot(f"xp-{theme}-empty")
    for p in (A, B): p.cmd(f"add 127.0.0.1:{port}")
    xfp = None
    for _ in range(40):
        pl = [l for l in A.cmd("peers") if l.startswith("PEER") and "Bill" in l]
        if pl: xfp = pl[0].split()[1]; break
        time.sleep(0.5)
    check(bool(xfp), f"[{theme}] peers see the XP client")
    for p in (A, B): p.cmd(f"trust {xfp[:12]}")
    A.cmd(f"send {xfp[:12]} 早上好！下午三点的评审会改到 302 会议室。"); time.sleep(0.4)
    A.cmd(f"send {xfp[:12]} Slides are in the shared folder — please take a look before the meeting."); time.sleep(0.4)
    B.cmd(f"send {xfp[:12]} Can you send me the latest build?"); time.sleep(0.4)
    A.cmd(f"sendfile {xfp[:12]} {img}"); time.sleep(3)
    A.cmd(f"send {xfp[:12]} 这是最新的数据，第 4 页有改动。"); time.sleep(1.5)
    click(200, 52); time.sleep(1.5)   # first conversation (x clear of the tray balloon wine draws top-left)
    xdo("type", "--delay", "40", "OK, see you at 3 pm."); xdo("key", "Return"); time.sleep(2.5)
    shot(f"xp-{theme}-chat")
    click(600, 300, "3"); time.sleep(1); shot(f"xp-{theme}-chat-menu"); xdo("key", "Escape"); time.sleep(0.5)
    open(f"{out}/gui-{theme}.log", "w").write("\n".join(g.lines))
    subprocess.run(["wineserver", "-k"], env=env); time.sleep(1.5)
    for p in (A, B): p.p.kill()
xv.kill(); print("RESULT", "FAIL" if fails else "PASS", len(fails))
