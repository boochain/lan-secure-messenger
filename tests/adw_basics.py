#!/usr/bin/env python3
"""GTK 4 / libadwaita GUI: runtime basics (docs/ux-basics-checklist.md G-items) on Xvfb, driven with xdotool;
state read from the LSM_UI_TEST "UISTATE dialogs=N addpop=N size=WxH" lines.
usage: adw_basics.py <lan-messenger-adw> <lanmsg-cli> <outdir>"""
import os, subprocess, sys, threading, time, shutil, json
gui, cli, out = [os.path.abspath(a) for a in sys.argv[1:4]]; os.makedirs(out, exist_ok=True)
fails = []; W = f"/tmp/lsmadw-{os.getpid()}"; shutil.rmtree(W, True); os.makedirs(W)
def check(ok, what): print(("PASS " if ok else "FAIL ") + what, flush=True); ok or fails.append(what)
disp = ":%d" % (60 + os.getpid() % 30)
xv = subprocess.Popen(["Xvfb", disp, "-screen", "0", "1280x860x24", "-nolisten", "tcp"], stderr=subprocess.DEVNULL); time.sleep(1.5)
env = dict(os.environ, DISPLAY=disp, GDK_BACKEND="x11", GSK_RENDERER="cairo", NO_AT_BRIDGE="1", LANG="C.UTF-8", GTK_A11Y="none")
env.update(l.split("=", 1) for l in subprocess.check_output(["dbus-launch", "--sh-syntax"], env=env, text=True).replace(";", "\n").splitlines() if "=" in l and not l.startswith("export"))
env = {k: v.strip("'") for k, v in env.items()}
# a compositing manager, as on every real desktop: without one, GTK >= 4.14 popovers (own X surfaces with
# an ARGB shadow margin) are drawn on black by Xvfb
comp = shutil.which("xcompmgr")
compp = subprocess.Popen([comp, "-n"], env=env, stderr=subprocess.DEVNULL) if comp else None
print("compositor:", comp or "none", flush=True)
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
base = 49600 + os.getpid() % 100 * 8
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
    if r: ignore[os.path.basename(path)] = r; ignore["_reasons"][os.path.basename(path)] = "decorative icons only: trusted-identity shield (symbolic GtkImage) online dot (empty GtkBox; status is also spelled out as text) and the empty-state AdwStatusPage illustration (title + description carry the meaning); bounds reported by the app"
    print("shot", path, "windows", wid, flush=True); return path

def wait(f, t=20):
    end = time.time() + t
    while time.time() < end:
        v = f()
        if v: return v
        time.sleep(0.3)
def state(g):
    l = next((l for l in reversed(g.lines) if l.startswith("UISTATE ")), "")
    return dict(kv.split("=") for kv in l.split()[1:])
def fresh(g, k):   # wait for a UISTATE line newer than now
    n = len(g.lines); wait(lambda: any(l.startswith("UISTATE ") for l in g.lines[n:]), 3); return state(g).get(k)
def snap(name): subprocess.run(["import", "-window", "root", "-crop", "1024x760+0+0", "+repage", f"{out}/{name}.png"], env=env); print("shot", f"{out}/{name}.png", flush=True)
port = base; A = peer("a", "王工", port + 2)
def start(extra=None):
    e = dict(env, LSM_THEME="light", LSM_UI_TEST="1", LSM_INSTANCE=".basics"); e.update(extra or {})
    return Proc([gui, "--data", f"{W}/gui", "--name", "Bill", "--no-discovery", "--port", str(port), "--disco-port", str(port + 1), "--auto-trust"], e)
dropf = f"{W}/拖放测试.txt"; open(dropf, "w").write("drop test\n")
g = start({"LSM_WINDOW_SIZE": "1024x694", "LSM_UI_TEST_DROP": dropf}); fp = (g.wait_for("LSM_FP", 20) or " ? ").split()[1]; check(fp != "?", "GUI started")
A.cmd(f"add 127.0.0.1:{port}"); time.sleep(2); A.cmd(f"trust {fp[:12]}"); A.cmd(f"send {fp[:12]} 基础检查：请回复。"); time.sleep(3)
B = peer("b", "陈静", port + 4); B.cmd(f"add 127.0.0.1:{port}"); time.sleep(2); B.cmd(f"trust {fp[:12]}"); B.cmd(f"send {fp[:12]} 第二个会话。"); time.sleep(3)
xdo("mousemove", "130", "220", "click", "1"); time.sleep(1)   # second row = 王工 (陈静 wrote last)
# G8 drag & drop: the test hook emits "drop" on the chat's GtkDropTarget with a GdkFileList -> the open chat's peer gets the offer
off = wait(lambda: next((l for l in A.lines if "拖放测试.txt" in l), None), 20)
check(bool(off), f"G8 a dropped file (GtkDropTarget drop, GdkFileList) is offered to the open chat ({(off or '')[:60]!r})")
# G1 Esc closes: Ctrl+I details dialog, Ctrl+N add popover
xdo("key", "ctrl+i"); time.sleep(1.2); d = fresh(g, "dialogs"); xdo("key", "Escape"); time.sleep(1); d2 = fresh(g, "dialogs")
check(d == "1" and d2 == "0", f"G1 Ctrl+I opens the details dialog, Esc closes it (dialogs {d} -> {d2})")
xdo("key", "ctrl+n"); time.sleep(1.2); p = fresh(g, "addpop"); snap("adw-basics-add-popover"); xdo("key", "Escape"); time.sleep(1); p2 = fresh(g, "addpop")
check(p == "1" and p2 == "0", f"G1 Ctrl+N opens the add popover, Esc closes it ({p} -> {p2})")
# G2 Shift+Enter new line, Enter sends one message with both lines
xdo("mousemove", "600", "663", "click", "1"); time.sleep(0.5); xdo("type", "--delay", "30", "line-one"); xdo("key", "shift+Return"); xdo("type", "--delay", "30", "line-two"); time.sleep(1)
check(not any("line-one" in l for l in A.lines), "G2 Shift+Enter does not send")
xdo("key", "Return"); r2 = wait(lambda: next((l for l in A.lines if "\tin\t" in l and "line-two" in l), None), 10)
check(bool(r2), f"G2 Enter sends one message with both lines ({(r2 or '')[-30:]!r})")
# G9 right-click a bubble opens its menu; G10 复制 copies (pasted back and received by the peer)
xdo("mousemove", "428", "167", "click", "3"); time.sleep(1.2); snap("adw-basics-msg-menu"); xdo("key", "Return"); time.sleep(0.8)
xdo("mousemove", "600", "663", "click", "1"); time.sleep(0.4); xdo("key", "ctrl+v"); time.sleep(0.4); xdo("key", "Return")
check(bool(wait(lambda: any("\tin\t" in l and "基础检查：请回复。" in l for l in A.lines), 8)), "G10 bubble menu 复制 copies the text (pasted with Ctrl+V and received by the peer)")
# G13 destructive action confirmed: bubble menu -> 删除这条消息（仅本机）… opens an alert dialog, Esc cancels
xdo("mousemove", "428", "167", "click", "3"); time.sleep(1.2); xdo("key", "Down"); time.sleep(0.3); xdo("key", "Return"); time.sleep(1.2); d = fresh(g, "dialogs"); snap("adw-basics-delete-confirm")
xdo("key", "Escape"); time.sleep(1); d2 = fresh(g, "dialogs")
check(d == "1" and d2 == "0", f"G13 删除这条消息 asks for confirmation, Esc cancels (dialogs {d} -> {d2})")
# G11 tooltip on the send button
def tops(): return set(subprocess.run(["xdotool", "search", "--onlyvisible", "--maxdepth", "1", "--name", ""], env=env, capture_output=True, text=True).stdout.split())
tb = tops(); xdo("mousemove", "985", "663"); time.sleep(2.5); tn = tops() - tb; snap("adw-basics-tooltip")
check(bool(tn), f"G11 hovering the send button shows a tooltip surface ({len(tn)} new)")
# G5 close hides and keeps running; G12 a second launch exits and re-presents the one window
n = len(g.lines); xdo("key", "ctrl+w"); time.sleep(1.5); vis = subprocess.run(["xdotool", "search", "--onlyvisible", "--name", "局域网安全通讯"], env=env, capture_output=True, text=True).stdout.split()
check(not vis and g.p.poll() is None, f"G5 close hides the window and the app keeps running (visible={len(vis)}, running={g.p.poll() is None})")
# G17 a message notification's default action (app.open-conv, what the desktop calls on click) shows the window and opens that chat
conv0 = fresh(g, "conv"); target = "dm:" + B.fp   # DM conversation ids are dm:<fingerprint>
subprocess.run(["gdbus", "call", "--session", "--dest", "io.github.frec86.LanMessenger.basics", "--object-path", "/io/github/frec86/LanMessenger/basics",
                "--method", "org.freedesktop.Application.ActivateAction", "open-conv", f"[<'{target}'>]", "{}"], env=env, capture_output=True, timeout=10); time.sleep(1.5)
vis1, conv1 = fresh(g, "visible"), state(g).get("conv")
check(vis1 == "1" and conv1 == target and conv0 != target, f"G17 notification click (app.open-conv) shows the hidden window and opens that chat ({conv0[:8] if conv0 else conv0} -> {(conv1 or '')[:8]}, visible={vis1})")
snap("adw-basics-notification-open")
n = len(g.lines); xdo("key", "ctrl+w"); time.sleep(1.5)
p2 = subprocess.Popen([gui], env=dict(env, LSM_INSTANCE=".basics"), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL); ended = wait(lambda: p2.poll() is not None, 15); time.sleep(1.5)
vis = subprocess.run(["xdotool", "search", "--onlyvisible", "--name", "局域网安全通讯"], env=env, capture_output=True, text=True).stdout.split()
check(bool(ended) and len(vis) >= 1 and g.p.poll() is None, f"G12 second launch exits and re-presents the running window (exited={bool(ended)}, visible={len(vis)})")
# G6 window memory: resize, Ctrl+Q, start again without LSM_WINDOW_SIZE
wid = vis[-1] if vis else ""; xdo("windowsize", wid, "900", "640"); time.sleep(1.5); want = fresh(g, "size")
xdo("key", "ctrl+q"); ex = wait(lambda: g.p.poll() is not None, 10); check(bool(ex), "G6 Ctrl+Q quits")
g = start(); g.wait_for("LSM_FP", 20); time.sleep(2); have = fresh(g, "size")
check(bool(want) and want == have, f"G6 window size restored after restart ({want} -> {have})")
open(f"{out}/gui-basics.log", "w").write("\n".join(g.lines) + "\n--- peer\n" + "\n".join(A.lines))
for q in (g, A, B): q.p.kill()
xv.kill(); compp and compp.kill(); print("RESULT", "FAIL" if fails else "PASS", len(fails))
