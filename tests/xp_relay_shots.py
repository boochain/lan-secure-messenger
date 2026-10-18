#!/usr/bin/env python3
"""XP Win32 GUI: relay (中转服务器) UI against a real relay-server and a relay-only lanmsg-cli peer, light + dark.
usage: xp_relay_shots.py <LanMessenger.exe> <lanmsg-cli> <relay-server> <outdir> [--only light|dark]   (Xvfb, xdotool, ImageMagick, wine32)"""
import os, subprocess, sys, threading, time, shutil
exe, cli, rbin, out = [os.path.abspath(a) for a in sys.argv[1:5]]; os.makedirs(out, exist_ok=True)
themes = [sys.argv[6]] if "--only" in sys.argv else ["light", "dark"]
fails = []; W = f"/tmp/lsmxp-{os.getpid()}"; shutil.rmtree(W, True); os.makedirs(W)
def check(ok, what): print(("PASS " if ok else "FAIL ") + what, flush=True); ok or fails.append(what)
disp = ":%d" % (30 + os.getpid() % 20)
xv = subprocess.Popen(["Xvfb", disp, "-screen", "0", "1100x760x24", "-nolisten", "tcp"], stderr=subprocess.DEVNULL); time.sleep(1.5)
env = dict(os.environ, DISPLAY=disp, WINEDEBUG="-all", LANG="zh_CN.UTF-8", WINEPREFIX=os.environ.get("WINEPREFIX", os.path.expanduser("~/wpfx32")))
if shutil.which("openbox"): subprocess.Popen(["openbox", "--config-file", os.path.join(os.path.dirname(os.path.abspath(__file__)), "openbox-nodecor.xml")], env=env, stderr=subprocess.DEVNULL); time.sleep(1)
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
base = 48100 + os.getpid() % 100 * 10
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
RPASS = "lsm-test-relay-pass"   # test-only passphrase for the throwaway local relay
def relay_up(rport): return subprocess.Popen([rbin, "--bind", "127.0.0.1", "--port", str(rport), "--password", RPASS], stdout=open(f"{out}/relay-server.log", "a"), stderr=subprocess.STDOUT)
def wait(f, t=60, d=0.5):
    end = time.time() + t
    while time.time() < end:
        v = f()
        if v: return v
        time.sleep(d)
    return None
def meta_has(s): return any(s in l for l in Cc.lines)
big = f"{W}/现场录像-完整版.mp4"; subprocess.run(["head", "-c", str(400 * 1024 * 1024), "/dev/urandom"], stdout=open(big, "wb"))
for i, theme in enumerate(themes):
    port = base + i * 10; rport = port + 8; data = f"C:\\lsmxpr{os.getpid()}{theme}"
    relay = relay_up(rport)
    A = peer(f"a{theme}", "王工", port + 2)                    # LAN peer (direct)
    Cc = peer(f"c{theme}", "陈静（外地）", port + 4)            # relay-only peer: --no-discovery, never added by address
    Cc.cmd(f"relay 127.0.0.1 {rport} {RPASS}")
    icons_file = os.path.join(env["WINEPREFIX"], "drive_c", data[3:], "ui_icons.txt")
    g = Proc(["wine", exe, "--data", data, "--name", "Bill", "--port", str(port), "--disco-port", str(port + 1), "--theme", theme, "--auto-trust"], dict(env, LSM_UI_TEST="1"))
    wid = wait(lambda: (xdo("search", "--name", "局域网安全通讯").split() or [""])[0], 40)
    check(bool(wid), f"[{theme}] XP window appeared")
    xdo("windowmove", wid, "20", "20"); xdo("windowsize", wid, "1024", "700"); time.sleep(2)
    A.cmd(f"add 127.0.0.1:{port}")
    xfp = wait(lambda: next((l.split()[1] for l in A.cmd("peers") if l.startswith("PEER") and "Bill" in l), None), 30)
    check(bool(xfp), f"[{theme}] LAN peer sees the XP client"); A.cmd(f"trust {xfp[:12]}")
    A.cmd(f"send {xfp[:12]} 早上好！下午三点的评审会改到 302 会议室。"); time.sleep(0.5)
    # 设置 > 中转服务器(R)...  (menu bar click, then the R mnemonic)
    click(260, 46); time.sleep(1); xdo("key", "r"); time.sleep(1.5)
    dlg = wait(lambda: (xdo("search", "--name", "^中转服务器$").split() or [""])[0], 10)
    check(bool(dlg), f"[{theme}] relay settings dialog (captioned, modal) opens from 设置 > 中转服务器")
    shot(f"xp-{theme}-relay-settings-off")
    xdo("key", "space"); xdo("key", "Tab"); xdo("type", "127.0.0.1"); xdo("key", "Tab"); xdo("key", "ctrl+a"); xdo("type", str(rport)); xdo("key", "Tab"); xdo("type", RPASS); time.sleep(0.5)
    xdo("key", "Return"); time.sleep(4)
    shot(f"xp-{theme}-relay-settings")
    cfp = wait(lambda: next((l.split()[1] for l in Cc.cmd("peers") if l.startswith("PEER") and "Bill" in l and "via=relay" in l), None), 60)
    check(bool(cfp), f"[{theme}] relay-only peer reaches the XP client through the relay (via=relay)")
    xdo("key", "Escape"); time.sleep(0.8)
    if cfp:
        Cc.cmd(f"trust {cfp[:12]}")
        Cc.cmd(f"send {cfp[:12]} 我在外地出差，经服务器联系你。"); time.sleep(0.5)
        Cc.cmd(f"send {cfp[:12]} 现场录像有点大，我先传给你，断了会自动续传。"); time.sleep(1.5)
    click(200, 216); time.sleep(2)   # second row: 陈静 (relay-only peer)
    shot(f"xp-{theme}-relay-chat")
    # paused + resumed transfer over the relay: kill the relay mid-transfer, bring it back
    Cc.cmd(f"sendfile {cfp[:12]} {big}") if cfp else None
    act = wait(lambda: any("\tout\tactive\t" in l for l in Cc.lines) or None, 30)
    time.sleep(1.5); relay.kill(); relay.wait()
    paused = wait(lambda: any("连接中断" in l for l in Cc.lines) or None, 40)
    check(bool(act and paused), f"[{theme}] relay transfer paused on link loss")
    time.sleep(2); shot(f"xp-{theme}-relay-paused")
    k0 = len(Cc.lines); relay = relay_up(rport)
    res = wait(lambda: any("\tout\tactive\t" in l and "连接中断" not in l for l in Cc.lines[k0:]) or None, 60)
    time.sleep(1.5); shot(f"xp-{theme}-relay-resumed")
    check(bool(res), f"[{theme}] transfer resumed after the relay came back")
    # live path switch: the same peer becomes reachable on the LAN -> 局域网直连, no badge
    Cc.cmd(f"add 127.0.0.1:{port}")
    sw = wait(lambda: any("\tpath\tdirect" in l for l in Cc.lines) or None, 40)
    time.sleep(2); shot(f"xp-{theme}-relay-direct")
    check(bool(sw), f"[{theme}] header switches live to 局域网直连")
    open(f"{out}/gui-relay-{theme}.log", "w").write("\n".join(g.lines))
    subprocess.run(["wineserver", "-k"], env=env); time.sleep(1.5)
    for p in (A, Cc): p.p.kill()
    relay.kill()
json.dump(ignore, open(f"{out}/audit_ignore.json", "w"), ensure_ascii=False, indent=1)
xv.kill(); print("RESULT", "FAIL" if fails else "PASS", len(fails))
