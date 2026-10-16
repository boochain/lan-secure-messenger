#!/usr/bin/env python3
"""Drive the Android app on an emulator against two real v0.5 Rust daemons on the host and screenshot it
in light and dark mode. Also checks the app-level file path both ways (SHA-256).
usage: android_ui_shots.py --rust-bin DIR --apk APK --out DIR"""
import argparse, hashlib, json, os, re, subprocess, sys, time, xml.etree.ElementTree as ET
ap = argparse.ArgumentParser(); ap.add_argument("--rust-bin", required=True); ap.add_argument("--apk"); ap.add_argument("--out", required=True)
args = ap.parse_args(); ADB = os.environ.get("ADB", "adb"); PKG = "io.github.frec86.lanmessenger"; W = "/tmp/lsm-ashots"
os.makedirs(args.out, exist_ok=True); subprocess.run(["rm", "-rf", W]); os.makedirs(W)
def adb(*a, t=120): return subprocess.run([ADB] + list(a), capture_output=True, text=True, timeout=t).stdout
def sha(p): return hashlib.sha256(open(p, "rb").read()).hexdigest()
def wait(f, t, step=1.0):
    end = time.time() + t
    while time.time() < end:
        try:
            r = f()
            if r: return r
        except Exception: pass
        time.sleep(step)
    return None
results = {}
def check(name, ok): results[name] = bool(ok); print(("PASS " if ok else "FAIL ") + name, flush=True)

class Rust:
    def __init__(s, name, port):
        s.dir = f"{W}/{port}"; s.sock = f"{W}/{port}.sock"; os.makedirs(s.dir); s.dl = f"{s.dir}/dl"
        cmd = [f"{args.rust_bin}/lan-daemon", "--data-dir", s.dir, "--ipc", s.sock]
        for x in [f'display_name="{name}"', f'download_dir="{s.dl}"', f"listen_port={port}", f"discovery_port={port+1}", "discovery_enabled=false"]: cmd += ["--set", x]
        s.p = subprocess.Popen(cmd, stdout=open(s.dir + ".log", "w"), stderr=subprocess.STDOUT, env=dict(os.environ, LSM_NO_DIALOG="1"))
        wait(lambda: os.path.exists(s.sock), 15); s.fp = wait(lambda: s.j("status")["me"]["fingerprint"], 15)
    def m(s, *a): return subprocess.run([f"{args.rust_bin}/lan-msg", "--ipc", s.sock] + list(a), capture_output=True, text=True, timeout=60).stdout
    def j(s, *a): return json.loads(s.m("--json", *a))
    def peers(s): return s.j("status")["peers"]

def dump():
    adb("shell", "uiautomator", "dump", "/sdcard/ui.xml", t=180); x = adb("shell", "cat", "/sdcard/ui.xml")
    return ET.fromstring(x[x.index("<?xml"):]) if "<?xml" in x else None
def find(pred):
    r = dump()
    if r is None: return None
    for n in r.iter("node"):  # software-emulated (TCG) emulator: System UI ANR dialogs pop up; press "Wait"
        if n.get("text") == "Wait" and any("isn't responding" in (m.get("text") or "") for m in r.iter("node")):
            b = list(map(int, re.findall(r"\d+", n.get("bounds")))); tap((b[0]+b[2])//2, (b[1]+b[3])//2); time.sleep(2); return None
    for n in r.iter("node"):
        if pred(n): b = list(map(int, re.findall(r"\d+", n.get("bounds")))); return ((b[0]+b[2])//2, (b[1]+b[3])//2, n)
def tap_text(t, exact=True, timeout=120):
    r = wait(lambda: find(lambda n: (n.get("text") == t or n.get("content-desc") == t) if exact else (t in (n.get("text") or ""))), timeout, 2)
    if not r: raise RuntimeError("not found: " + t)
    tap(r[0], r[1]); time.sleep(2); return r
def tap(x, y):  # "input tap" gets lost on the software-emulated (TCG) emulator; explicit DOWN/UP works
    adb("shell", f"input touchscreen motionevent DOWN {x} {y}; input touchscreen motionevent UP {x} {y}")
def tap_edit(timeout=120):
    r = wait(lambda: find(lambda n: n.get("class") == "android.widget.EditText"), timeout, 2); tap(r[0], r[1]); time.sleep(2)
def shot(name):
    time.sleep(3); find(lambda n: False); subprocess.run(f"{ADB} exec-out screencap -p > {args.out}/{name}.png", shell=True); print("shot", name, flush=True)
def launch():
    adb("shell", "am", "start", "-W", "-n", f"{PKG}/.MainActivity"); time.sleep(10)

if args.apk: print(adb("install", "-r", args.apk, t=600).strip())
adb("shell", "pm", "clear", PKG); adb("shell", "cmd", "uimode", "night", "no")
adb("shell", "pm", "grant", PKG, "android.permission.POST_NOTIFICATIONS")
win = Rust("办公室-Windows", 47700); xp = Rust("XP旧电脑", 47710)
launch(); shot("light-01-empty")
for port in (47700, 47710):
    tap_text("按 IP 添加"); tap_edit(); adb("shell", "input", "text", f"10.0.2.2:{port}"); time.sleep(2); tap_text("确定")
amfp = wait(lambda: [p["fingerprint"] for p in win.peers() if p.get("online")], 90)
check("Android connected to v0.5 by IP", amfp); amfp = amfp[0]
wait(lambda: [p for p in xp.peers() if p.get("online")], 60)
shot("light-02-contacts")
# PIN pairing with the first v0.5 node: Android shows the PIN, v0.5 enters it
tap_text("菜单"); tap_text("生成 PIN（让对方输入）")
pin = wait(lambda: find(lambda n: re.fullmatch(r"\d{6}", n.get("text") or "")), 120, 2)[2].get("text")
shot("light-03-pin")
win.m("pin-pair", amfp, pin); tap_text("好")
check("PIN pairing Android<->v0.5 (mutual trust)", wait(lambda: any(p["fingerprint"] == amfp and p.get("trust") in ("trusted", "Trusted") for p in win.j("status")["peers"]) or "trusted" in win.m("peers").lower(), 60))
xp.m("trust", amfp)
# messages + an image from v0.5 into the app
win.m("send", amfp, "你好，这是 Windows v0.5 发来的消息 👋")
img = f"{W}/白板照片.png"; subprocess.run(["convert", "-size", "640x400", "gradient:#1565c0-#80deea", "-pointsize", "48", "-fill", "white", "-gravity", "center", "-annotate", "0", "LAN 白板", img])
win.m("send-file", amfp, img)
tap_text("办公室-Windows"); time.sleep(5)
# trust v0.5 side explicitly too (no-op if PIN already did it), then reply from the app
tap_text("菜单"); tap_text("信任")
tap_edit(); adb("shell", "input", "text", "Hello%sfrom%sAndroid"); tap_text("发送")
check("app -> v0.5 text delivered", wait(lambda: any(m["body"] == "Hello from Android" for m in win.j("history", amfp)), 60))
check("v0.5 -> app image received (SHA match)", wait(lambda: subprocess.run(f"{ADB} shell su 0 sha256sum /data/data/{PKG}/files/lsm/downloads/白板照片.png", shell=True, capture_output=True, text=True).stdout.split(" ")[0] == sha(img), 120, 3))
time.sleep(5); shot("light-04-chat-image")
# the app must trust a peer before sending it files (same rule as v0.5)
adb("shell", "input", "keyevent", "4"); time.sleep(3); adb("shell", "input", "keyevent", "4"); time.sleep(4); tap_text("XP旧电脑"); time.sleep(4); tap_text("菜单"); tap_text("信任"); adb("shell", "input", "keyevent", "4"); time.sleep(3)
# share intent: send a picture from the gallery into the app (the SAF/share path)
adb("push", img, "/sdcard/Pictures/share-test.png"); adb("shell", "am", "broadcast", "-a", "android.intent.action.MEDIA_SCANNER_SCAN_FILE", "-d", "file:///sdcard/Pictures/share-test.png"); time.sleep(8)
q = adb("shell", "content", "query", "--uri", "content://media/external/images/media", "--projection", "_id:_display_name")
mid = re.findall(r"_id=(\d+), _display_name=share-test.png", q)
if mid:
    adb("shell", "am", "start", "-a", "android.intent.action.SEND", "-t", "image/png", "--eu", "android.intent.extra.STREAM", f"content://media/external/images/media/{mid[-1]}", "-n", f"{PKG}/.MainActivity")
    time.sleep(8); shot("light-05-share-picker"); tap_text("XP旧电脑")
    check("app -> v0.5 file via share intent (SHA match)", wait(lambda: os.path.exists(f"{xp.dl}/share-test.png") and sha(f"{xp.dl}/share-test.png") == sha(img), 120, 2))
else: check("app -> v0.5 file via share intent (SHA match)", False)
# group created on v0.5 with the app as member
win.m("add", "127.0.0.1:47710"); wait(lambda: any(p["fingerprint"] == xp.fp and p.get("online") for p in win.peers()), 30)
xp.m("trust", win.fp); win.m("trust", xp.fp); time.sleep(3)
gid = win.m("group-create", "项目组", amfp, xp.fp)
print("group-create:", gid.strip(), flush=True); time.sleep(5); g = wait(lambda: win.j("groups")[0]["id"], 30)
check("v0.5 group with Android member created", g)
win.m("send-group", g, "群消息：周五下午三点开会"); xp.m("send-group", g, "收到 👍")
adb("shell", "input", "keyevent", "4"); time.sleep(4)
tap_text("项目组"); time.sleep(6); shot("light-06-group")
adb("shell", "input", "keyevent", "4"); time.sleep(3)
# dark mode
adb("shell", "cmd", "uimode", "night", "yes"); time.sleep(8); launch()
shot("dark-01-contacts"); tap_text("办公室-Windows"); time.sleep(6); shot("dark-02-chat-image")
adb("shell", "input", "keyevent", "4"); time.sleep(3); tap_text("项目组"); time.sleep(5); shot("dark-03-group")
adb("shell", "input", "keyevent", "4"); time.sleep(3); tap_text("菜单"); tap_text("关于"); shot("dark-04-about"); tap_text("好")
adb("shell", "cmd", "uimode", "night", "no")
for r in (win, xp): r.m("shutdown")
json.dump(results, open(f"{args.out}/results.json", "w"), ensure_ascii=False, indent=1)
print("RESULT", sum(results.values()), "/", len(results))
