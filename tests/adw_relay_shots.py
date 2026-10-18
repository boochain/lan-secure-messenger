#!/usr/bin/env python3
"""GTK 4 / libadwaita GUI: relay (中转服务器) UI against a real relay-server and a relay-only lanmsg-cli peer, light + dark.
usage: adw_relay_shots.py <lan-messenger-adw> <lanmsg-cli> <relay-server> <outdir>   (needs Xvfb, xdotool, ImageMagick import, dbus-launch)"""
import os, subprocess, sys, threading, time, shutil, json
gui, cli, rbin, out = [os.path.abspath(a) for a in sys.argv[1:5]]; os.makedirs(out, exist_ok=True)
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
img = f"{W}/设计稿-v3.png"
subprocess.run(["convert", "-size", "960x660", "gradient:#7aa7d8-#2b4f7e", "-fill", "#ffffff", "-draw", "roundrectangle 120,120 840,540 24,24", img])
RPASS = "lsm-test-relay-pass"
big = f"{W}/现场录像-完整版.mp4"; subprocess.run(["head", "-c", str(400 * 1024 * 1024), "/dev/urandom"], stdout=open(big, "wb"))   # test-only passphrase for the throwaway local relay
def wait(f, t=60):
    end = time.time() + t
    while time.time() < end:
        v = f()
        if v: return v
        time.sleep(0.5)
for i, theme in enumerate(["light", "dark"]):
    port = base + i * 10; rport = port + 8
    relay = subprocess.Popen([rbin, "--bind", "127.0.0.1", "--port", str(rport), "--password", RPASS], stdout=open(f"{out}/relay-server.log", "a"), stderr=subprocess.STDOUT)
    A = peer(f"a{theme}", "王工", port + 2); Cc = peer(f"c{theme}", "陈静（外地）", port + 4)
    Cc.cmd(f"relay 127.0.0.1 {rport} {RPASS}")
    g = Proc([gui, "--data", f"{W}/gui-{theme}", "--name", "Bill", "--no-discovery", "--port", str(port), "--disco-port", str(port + 1), "--auto-trust"],
             dict(env, LSM_THEME=theme, LSM_UI_TEST="1", LSM_QUIT_ON_CLOSE="1", LSM_WINDOW_SIZE="1024x694", LSM_UI_TEST_RELAY=f"127.0.0.1 {rport} {RPASS}"))
    fp = (g.wait_for("LSM_FP", 20) or " ? ").split()[1]; check(fp != "?", f"[{theme}] GUI started")
    A.cmd(f"add 127.0.0.1:{port}"); time.sleep(2); A.cmd(f"trust {fp[:12]}"); A.cmd(f"send {fp[:12]} 早上好！下午三点的评审会改到 302 会议室。")
    time.sleep(6); shot("relay-settings", theme, g=g)
    cfp = wait(lambda: next((l.split()[1] for l in Cc.cmd("peers") if l.startswith("PEER") and "Bill" in l and "via=relay" in l), None), 60)
    check(bool(cfp), f"[{theme}] relay-only peer reaches the GTK client via the relay")
    xdo("key", "Escape"); time.sleep(1)
    if cfp: Cc.cmd(f"trust {cfp[:12]}"); Cc.cmd(f"send {cfp[:12]} 我在外地出差，经服务器联系你。"); time.sleep(2)
    xdo("key", "ctrl+Tab"); time.sleep(1.5)
    shot("relay-chat", theme, g=g)
    # in-transit + paused file card (progress bar) over the relay
    if cfp:
        Cc.cmd(f"sendfile {cfp[:12]} {big}")
        act = wait(lambda: any("\tout\tactive\t" in l for l in Cc.lines) or None, 30); time.sleep(2.5)
        shot("relay-transfer", theme, g=g); check(bool(act), f"[{theme}] relay transfer active (progress bar shot)")
        relay.kill(); relay.wait()
        paused = wait(lambda: any("连接中断" in l for l in Cc.lines) or None, 40); time.sleep(2)
        shot("relay-paused", theme, g=g); check(bool(paused), f"[{theme}] transfer paused on relay loss")
    open(f"{out}/gui-relay-{theme}.log", "w").write("\n".join(g.lines))
    for p in (g, A, Cc): p.p.kill()
    relay.kill(); time.sleep(1)
json.dump(ignore, open(f"{out}/audit_ignore.json", "w"), ensure_ascii=False, indent=1)
xv.kill(); compp and compp.kill(); print("RESULT", "FAIL" if fails else "PASS", len(fails))
