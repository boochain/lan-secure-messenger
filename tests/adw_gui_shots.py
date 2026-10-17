#!/usr/bin/env python3
"""GTK 4 / libadwaita GUI: real screenshots (light + dark) and keyboard-shortcut runtime checks under Xvfb.
usage: adw_gui_shots.py <lan-messenger-adw> <lanmsg-cli> <outdir>   (needs Xvfb, xdotool, ImageMagick import, dbus-launch)"""
import os, subprocess, sys, threading, time, shutil
gui, cli, out = [os.path.abspath(a) for a in sys.argv[1:4]]; os.makedirs(out, exist_ok=True)
fails = []; W = f"/tmp/lsmadw-{os.getpid()}"; shutil.rmtree(W, True); os.makedirs(W)
def check(ok, what): print(("PASS " if ok else "FAIL ") + what, flush=True); ok or fails.append(what)
disp = ":%d" % (60 + os.getpid() % 30)
xv = subprocess.Popen(["Xvfb", disp, "-screen", "0", "1280x860x24", "-nolisten", "tcp"], stderr=subprocess.DEVNULL); time.sleep(1.5)
env = dict(os.environ, DISPLAY=disp, GDK_BACKEND="x11", GSK_RENDERER="cairo", NO_AT_BRIDGE="1", LANG="C.UTF-8", GTK_A11Y="none")
env.update(l.split("=", 1) for l in subprocess.check_output(["dbus-launch", "--sh-syntax"], env=env, text=True).replace(";", "\n").splitlines() if "=" in l and not l.startswith("export"))
env = {k: v.strip("'") for k, v in env.items()}
class Proc:
    def __init__(n, argv, e=None):
        n.lines = []; n.p = subprocess.Popen(argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=e or env)
        threading.Thread(target=lambda: [n.lines.append(l.decode("utf-8", "replace").rstrip()) for l in n.p.stdout], daemon=True).start()
    def wait_for(n, pat, t=20, start=0):
        end = time.time() + t
        while time.time() < end:
            for l in n.lines[start:]:
                if pat in l: return l
            time.sleep(0.1)
        return None
    def cmd(n, line, t=30):
        k = len(n.lines); n.p.stdin.write((line + "\n").encode()); n.p.stdin.flush()
        end = time.time() + t
        while time.time() < end:
            if any(l.startswith(("OK", "ERR")) for l in n.lines[k:]): return n.lines[k:]
            time.sleep(0.1)
        return n.lines[k:]
base = 47600 + os.getpid() % 100 * 8
def peer(tag, name, port):
    p = Proc([cli, "--data", f"{W}/{tag}", "--name", name, "--no-discovery", "--port", str(port), "--disco-port", str(port + 1)])
    p.fp = next(l for l in p.cmd("id", 30) if l.startswith("OK id")).split()[2]; return p
def xdo(*a): subprocess.run(["xdotool", *a], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
ignore = {"_reasons": {}}
def icon_rects(lines):
    """ICON lines printed by the GUI right before its latest 'UI ' line: decorative GtkImage icons (no text)."""
    k = max((i for i, l in enumerate(lines) if l.startswith("UI ")), default=-1); out = []
    i = k - 1
    while i >= 0 and lines[i].startswith("ICON "): out.append([int(v) for v in lines[i].split()[2].split(",")]); i -= 1
    return out
def shot(name, theme, size="1024x694", g=None):
    wid = subprocess.run(["xdotool", "search", "--onlyvisible", "--name", "局域网安全通讯"], env=env, capture_output=True, text=True).stdout.split()
    path = f"{out}/{name}-{theme}.png"
    subprocess.run(["import", "-window", "root", "-crop", size + "+0+0", "+repage", path], env=env)
    r = icon_rects(g.lines) if g else []
    if r: ignore[os.path.basename(path)] = r; ignore["_reasons"][os.path.basename(path)] = "decorative icons only: trusted-identity shield (symbolic GtkImage) and online dot (empty GtkBox; status is also spelled out as text); bounds reported by the app"
    print("shot", path, "windows", wid, flush=True); return path
img = f"{W}/设计稿-v3.png"
subprocess.run(["convert", "-size", "960x660", "gradient:#7aa7d8-#2b4f7e", "-fill", "#ffffff", "-draw", "roundrectangle 120,120 840,540 24,24", img])
pdf = f"{W}/需求说明-v2.pdf"; open(pdf, "wb").write(os.urandom(1024 * 1300))
for i, theme in enumerate(["light", "dark"]):
    port = base + i * 6 + 400
    A = peer(f"a{theme}", "王工", port + 2); B = peer(f"b{theme}", "Alice Chen", port + 4)
    g = Proc([gui, "--data", f"{W}/gui-{theme}", "--name", "Bill", "--no-discovery", "--port", str(port), "--disco-port", str(port + 1), "--auto-trust"],
             dict(env, LSM_THEME=theme, LSM_UI_TEST="1", LSM_QUIT_ON_CLOSE="1", LSM_WINDOW_SIZE="1024x694"))
    fp = (g.wait_for("LSM_FP", 20) or " ? ").split()[1]
    check(fp != "?", f"[{theme}] GUI started and reported its fingerprint")
    for p in (A, B): p.cmd(f"add 127.0.0.1:{port}")
    time.sleep(2); A.cmd(f"trust {fp[:12]}"); B.cmd(f"trust {fp[:12]}")
    A.cmd(f"send {fp[:12]} 早上好，新版界面的设计稿我放到共享盘了。"); time.sleep(0.4)
    A.cmd(f"send {fp[:12]} 你看下聊天列表的间距，有问题告诉我。"); time.sleep(0.4)
    A.cmd(f"sendfile {fp[:12]} {img}", 60); time.sleep(0.6)
    A.cmd(f"sendfile {fp[:12]} {pdf}", 60); time.sleep(0.4)
    B.cmd(f"send {fp[:12]} Lunch at 12:30?"); time.sleep(3)
    gl = len(g.lines)
    xdo("key", "ctrl+Tab"); l = g.wait_for("UI next-conv", 5, gl)
    check(l is not None, f"[{theme}] shortcut Ctrl+Tab switches to the next conversation ({l})")
    gl = len(g.lines); xdo("key", "ctrl+shift+Tab"); l = g.wait_for("UI prev-conv", 5, gl)
    check(l is not None, f"[{theme}] shortcut Ctrl+Shift+Tab switches back ({l})")
    # make sure 王工's chat is the open one for the main shot
    for _ in range(3):
        if g.lines and A.fp[:8] in " ".join(x for x in g.lines if x.startswith("UI"))[-200:]: break
        xdo("key", "ctrl+Tab"); time.sleep(0.6)
    xdo("type", "--delay", "20", "OK, see you at 3 pm."); xdo("key", "Return"); time.sleep(1.5)
    check(A.wait_for("see you at 3 pm", 8) is not None, f"[{theme}] Enter in the composer sends (peer received it)")
    time.sleep(1); shot("page-chat-dm", theme, g=g)
    gl = len(g.lines); xdo("key", "ctrl+f"); l = g.wait_for("UI search", 5, gl)
    check(l is not None and "Text" in l, f"[{theme}] shortcut Ctrl+F focuses the conversation search ({l})"); xdo("key", "Escape"); time.sleep(0.4)
    gl = len(g.lines); xdo("key", "ctrl+n"); l = g.wait_for("UI add-contact", 5, gl)
    check(l is not None and "Text" in l, f"[{theme}] shortcut Ctrl+N opens the add-contact popover and focuses its box ({l})")
    time.sleep(0.5); shot("popup-add-peer", theme, g=g); xdo("key", "Escape"); time.sleep(0.4)
    gl = len(g.lines); xdo("key", "ctrl+i"); l = g.wait_for("UI details", 5, gl)
    check(l is not None, f"[{theme}] shortcut Ctrl+I opens contact details"); time.sleep(0.8); shot("dialog-details", theme, g=g); xdo("key", "Escape"); time.sleep(0.4)
    gl = len(g.lines); xdo("key", "F10"); l = g.wait_for("UI menu", 5, gl)
    check(l is not None, f"[{theme}] shortcut F10 opens the main menu"); time.sleep(0.5); shot("popup-main-menu", theme, g=g); xdo("key", "Escape"); time.sleep(0.4)
    gl = len(g.lines); xdo("mousemove", "420", "485", "click", "3"); l = g.wait_for("UI msg-menu", 5, gl)
    check(l is not None, f"[{theme}] right-click on a message (file card) opens its menu"); time.sleep(0.5); shot("popup-message-menu", theme, g=g); xdo("key", "Escape")
    xdo("key", "ctrl+q"); g.p.wait(10) if g.p.poll() is None else None
    check(g.p.poll() is not None, f"[{theme}] shortcut Ctrl+Q quits")
    if g.p.poll() is None: g.p.kill()
    for p in (A, B): p.cmd("quit", 10); p.p.kill()
    open(f"{out}/gui-{theme}.log", "w").write("\n".join(g.lines))
# narrow (collapsed split view, content page) - light only, matches gnome-light-narrow mockup
A = peer("an", "王工", base + 500); g = Proc([gui, "--data", f"{W}/gui-narrow", "--name", "Bill", "--no-discovery", "--port", str(base + 498), "--disco-port", str(base + 499), "--auto-trust"],
    dict(env, LSM_THEME="light", LSM_UI_TEST="1", LSM_QUIT_ON_CLOSE="1", LSM_WINDOW_SIZE="560x720"))
fp = (g.wait_for("LSM_FP", 20) or " ? ").split()[1]; A.cmd(f"add 127.0.0.1:{base + 498}"); time.sleep(2); A.cmd(f"trust {fp[:12]}")
A.cmd(f"send {fp[:12]} 早上好，新版界面的设计稿我放到共享盘了。"); A.cmd(f"sendfile {fp[:12]} {img}", 60); time.sleep(2.5)
xdo("key", "ctrl+Tab"); time.sleep(1.2)
check(g.wait_for("UI next-conv", 5) is not None, "[narrow] Ctrl+Tab opens the conversation page in the collapsed layout")
shot("page-chat-narrow", "light", "560x720", g)
gl = len(g.lines); xdo("key", "ctrl+f"); check(g.wait_for("UI search", 5, gl) is not None, "[narrow] Ctrl+F goes back to the list and focuses search")
time.sleep(0.8); shot("page-list-narrow", "light", "560x720", g)
xdo("key", "ctrl+q"); time.sleep(1); g.p.poll() is None and g.p.kill(); A.cmd("quit", 10); A.p.kill()
import json; json.dump(ignore, open(f"{out}/audit_ignore.json", "w"), ensure_ascii=False, indent=1)
xv.kill(); subprocess.run(["kill", env.get("DBUS_SESSION_BUS_PID", "0")])
print("RESULT", "FAIL" if fails else "PASS", len(fails)); sys.exit(1 if fails else 0)
