#!/usr/bin/env python3
"""Redesign screenshots with real data: lanmsg-cli peers on the emulator (tests/ui_seed.py) talk to the app,
then the chat list and the 王工 chat are captured in light and dark (compare with design/redesign/mockups/android-*).
usage: android_chat_shots.py --cli /data/local/tmp/lanmsg-cli --out DIR"""
import argparse, os, re, shlex, subprocess, sys, time, xml.etree.ElementTree as ET
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import ui_seed
ap = argparse.ArgumentParser(); ap.add_argument("--cli", default="/data/local/tmp/lanmsg-cli"); ap.add_argument("--out", required=True)
args = ap.parse_args(); os.makedirs(args.out, exist_ok=True)
ADB = os.environ.get("ADB", "adb"); PKG = "io.github.frec86.lanmessenger"; HERE = os.path.dirname(os.path.abspath(__file__))
def adb(*a, t=180): return subprocess.run([ADB] + list(a), capture_output=True, text=True, timeout=t).stdout
def sh(c, t=180): return adb("shell", c, t=t)
def tap(x, y): sh(f"input touchscreen motionevent DOWN {x} {y}; input touchscreen motionevent UP {x} {y}")
def nodes():
    sh("uiautomator dump /sdcard/ui.xml"); x = sh("cat /sdcard/ui.xml")
    return list(ET.fromstring(x[x.index("<?xml"):]).iter("node")) if "<?xml" in x else []
def find(pred):
    for n in nodes():
        if n.get("text") == "Wait" or n.get("text") == "等待":  # TCG emulator ANR dialog
            b = list(map(int, re.findall(r"\d+", n.get("bounds")))); tap((b[0]+b[2])//2, (b[1]+b[3])//2); time.sleep(2); return None
        if pred(n): b = list(map(int, re.findall(r"\d+", n.get("bounds")))); return ((b[0]+b[2])//2, (b[1]+b[3])//2)
def tap_text(t, timeout=90):
    r = ui_seed.wait(lambda: find(lambda n: n.get("text") == t or n.get("content-desc") == t), timeout, 2)
    if not r: raise RuntimeError("not found: " + t)
    tap(*r); time.sleep(2)
def back(): sh("input keyevent 4"); time.sleep(2)
def shot(name):
    time.sleep(4); subprocess.run(f"{ADB} exec-out screencap -p > {args.out}/{name}.png", shell=True); print("shot", name, flush=True)

class App:
    def trust(s, name):
        tap_text(name); tap_text("菜单"); tap_text("信任"); back()
    def send_text(s, name, text):
        sh("am start -W -a android.intent.action.SEND -t text/plain --es android.intent.extra.TEXT " + shlex.quote(text) + f" -n {PKG}/.MainActivity")
        time.sleep(3); tap_text(name); time.sleep(1); back()
    def send_image(s, name, path):
        dst = "/sdcard/Download/ui-photo.jpg"; adb("push", path, dst)
        sh(f"am broadcast -a android.intent.action.MEDIA_SCANNER_SCAN_FILE -d file://{dst}"); time.sleep(3)
        q = sh("content query --uri content://media/external/images/media --projection _id --where \"_display_name='ui-photo.jpg'\"")
        mid = re.findall(r"_id=(\d+)", q)[-1]
        sh(f"am start -W -a android.intent.action.SEND -t image/jpeg --eu android.intent.extra.STREAM content://media/external/images/media/{mid} --grant-read-uri-permission -n {PKG}/.MainActivity")
        time.sleep(3); tap_text(name); time.sleep(3); back()

for k in ("window_animation_scale", "transition_animation_scale", "animator_duration_scale"): sh(f"settings put global {k} 0")
sh(f"pm clear {PKG}"); sh("cmd uimode night no"); sh(f"pm grant {PKG} android.permission.POST_NOTIFICATIONS")
sh(f"am start -W -n {PKG}/.MainActivity"); time.sleep(10)
pdf = "/data/local/tmp/季度报告-Q3-最终版.pdf"; local_pdf = "/tmp/lsm-seed.pdf"
open(local_pdf, "wb").write(b"%PDF-1.4\n" + os.urandom(2_516_582))
adb("push", local_pdf, pdf)
sh("rm -rf /data/local/tmp/seed; mkdir -p /data/local/tmp/seed")
def mk(name, i):
    port = 42000 + 4 * i; tag = f"p{i}"
    p = ui_seed.Peer(name, [ADB, "shell", "-T", f"{args.cli} --data /data/local/tmp/seed/{tag} --name {shlex.quote(name)} --no-discovery --port {port} --disco-port {port + 1} --announce-ms 1000"], args.out)
    p.port = port; return p
P = ui_seed.seed(mk, "127.0.0.1:41235", App(), pdf, os.path.join(HERE, "assets", "ui-photo.jpg"))
for th in ("light", "dark"):
    sh("cmd uimode night " + ("yes" if th == "dark" else "no")); time.sleep(6)
    shot(f"android-{th}-10-list")
    tap_text("王工"); time.sleep(2); shot(f"android-{th}-11-chat"); back()
for p in P.values(): p.stop()
sh("cmd uimode night no"); sh(f"am force-stop {PKG}")
