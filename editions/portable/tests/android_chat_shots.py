#!/usr/bin/env python3
"""Redesign screenshots with real data: lanmsg-cli peers on the emulator (tests/ui_seed.py) talk to the app,
then the chat list and the 王工 chat are captured in light and dark (compare with design/redesign/mockups/android-*).
usage: android_chat_shots.py --cli /data/local/tmp/lanmsg-cli --out DIR"""
import json, argparse, os, re, shlex, subprocess, sys, time, xml.etree.ElementTree as ET
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
IGN_PATH = None
def emoji_boxes(png):
    """Colour-emoji glyphs (Noto Color Emoji, e.g. the yellow thumbs-up) are pictures, not UI text: OCR reads them as
    low-contrast 'text'. Locate the saturated yellow/orange glyph blobs and exclude them (with a reason) from the audit."""
    from PIL import Image; import numpy as np, cv2
    a = np.asarray(Image.open(png).convert("RGB")).astype(int); r, g, b = a[..., 0], a[..., 1], a[..., 2]
    m = ((r > 200) & (g > 110) & (g < 215) & (b < 90)).astype(np.uint8)
    n, _, st, _ = cv2.connectedComponentsWithStats(m, connectivity=8)
    return [[int(st[i, 0]) - 4, int(st[i, 1]) - 4, int(st[i, 2]) + 8, int(st[i, 3]) + 8] for i in range(1, n) if 40 <= st[i, cv2.CC_STAT_AREA] <= 4000]
def shot(name):
    time.sleep(4); png = f"{args.out}/{name}.png"; subprocess.run(f"{ADB} exec-out screencap -p > {png}", shell=True); print("shot", name, flush=True)
    path = f"{args.out}/audit_ignore.json"
    ign = json.load(open(path)) if os.path.exists(path) else {}
    ign.setdefault("_reasons", {})["android-*-11-chat emoji"] = "colour emoji glyph (Noto Color Emoji) inside a message is a picture, not UI text"
    boxes = emoji_boxes(png)
    ign["_reasons"]["android-*-11-chat photo"] = "the photo inside an image bubble is user content, not UI (its dark shades also occur in dark surfaces, so colour masking cannot isolate it)"
    for n in nodes():  # Image(contentDescription = file name) in the image bubble
        if re.search(r"\.(jpe?g|png|webp|gif)$", n.get("content-desc") or "", re.I):
            b = list(map(int, re.findall(r"\d+", n.get("bounds")))); boxes.append([b[0], b[1], b[2] - b[0], b[3] - b[1]])
    if boxes: ign[name + ".png"] = boxes
    json.dump(ign, open(path, "w"), ensure_ascii=False, indent=1)

class App:
    def trust(s, name):
        tap_text(name); tap_text("菜单"); tap_text("信任"); back()
    def send_text(s, name, text):
        sh("am start -W -a android.intent.action.SEND -t text/plain --es android.intent.extra.TEXT " + shlex.quote(text) + f" -n {PKG}/.MainActivity")
        time.sleep(3); tap_text(name); time.sleep(1); back()

sh("svc power stayon true; input keyevent 224; wm dismiss-keyguard; settings put system screen_off_timeout 1800000")  # keep the screen on
sh("cmd overlay enable com.android.internal.systemui.navbar.gestural")  # gesture navigation, as on current phones (and in the mockup)
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
photo = "/data/local/tmp/界面截图.jpg"; adb("push", os.path.join(HERE, "assets", "ui-photo.jpg"), photo)
P = ui_seed.seed(mk, "127.0.0.1:41235", App(), pdf, photo)
for th in ("light", "dark"):
    sh("cmd uimode night " + ("yes" if th == "dark" else "no")); time.sleep(6)
    shot(f"android-{th}-10-list")
    tap_text("王工"); time.sleep(2); shot(f"android-{th}-11-chat"); back()
# ---- regression: system Back navigates (chat -> list), closes menus first, survives rotation, double-back exits
def has(t): return find(lambda n: n.get("text") == t or n.get("content-desc") == t) is not None
def resumed(): return PKG in sh("dumpsys activity activities | grep -E 'mResumedActivity|topResumedActivity'")
FAILS = []
def check(cond, what):
    print(("PASS " if cond else "FAIL ") + what, flush=True)
    if not cond: FAILS.append(what)
sh("cmd uimode night no"); time.sleep(4)
# delete (this device only): long-press a bubble -> 删除（仅本机） -> confirm -> gone
tap_text("王工")
r = ui_seed.wait(lambda: find(lambda n: (n.get("text") or "").startswith("这是最新的数据")), 30, 2)
if r: sh(f"input swipe {r[0]} {r[1]} {r[0]} {r[1]} 900"); time.sleep(2)
okm = has("删除（仅本机）")
if okm: tap_text("删除（仅本机）"); tap_text("删除")
time.sleep(2); check(okm and not has("这是最新的数据，第 4 页有改动。"), "long-press 删除（仅本机） with confirmation removes the message")
back()
tap_text("王工"); check(has("输入消息"), "open chat")
back(); check(has("局域网安全通讯") and not has("输入消息") and resumed(), "Back from chat returns to the list (app stays open)")
tap_text("王工"); tap_text("菜单"); check(has("查看指纹"), "chat menu open")
back(); check(not has("查看指纹") and has("输入消息"), "Back closes the menu, stays in the chat")
sh("settings put system accelerometer_rotation 0"); sh("settings put system user_rotation 1"); time.sleep(4)
check(has("输入消息"), "rotation keeps the open chat")
sh("settings put system user_rotation 0"); time.sleep(4)
back(); check(has("局域网安全通讯") and resumed(), "Back after rotation returns to the list")
tap_text("群组"); back(); check(has("局域网安全通讯") and resumed(), "Back on another tab returns to 聊天, app stays open")
# A3 Back closes the list search (the first Back may only hide the IME, as everywhere on Android)
tap_text("搜索"); check(has("关闭搜索"), "search opens")
back()
if has("关闭搜索"): back()
check(not has("关闭搜索") and has("局域网安全通讯") and resumed(), "Back closes the list search, app stays open")
# A8 process death: open chat, Home, kill the process, relaunch from the launcher -> the same chat is restored
tap_text("王工"); sh("input keyevent 3"); time.sleep(2); pid0 = sh(f"pidof {PKG}").strip(); sh(f"am kill {PKG}"); time.sleep(3); pid1 = sh(f"pidof {PKG}").strip()
if pid1 and pid1 == pid0:   # am kill spares a process with a foreground service (CoreService): kill it the way the low-memory killer does
    sh(f"su 0 kill -9 {pid0} || run-as {PKG} kill -9 {pid0}"); time.sleep(3); pid1 = sh(f"pidof {PKG}").strip()
print(f"process death: pid {pid0!r} -> {pid1!r}", flush=True)
sh(f"monkey -p {PKG} -c android.intent.category.LAUNCHER 1"); time.sleep(8)
check(pid0 != pid1 and has("输入消息"), f"process death (am kill in background) restores the open chat (pid {pid0} -> {pid1 or 'killed'})")
back(); check(has("局域网安全通讯") and resumed(), "Back after the restore returns to the list")
sh("input keyevent 4"); time.sleep(0.8); check(resumed(), "first Back at the root does not exit ('再按一次退出' toast)")
sh("input keyevent 4"); time.sleep(3); check(not resumed(), "second Back within 2 s exits")
for p in P.values(): p.stop()
if FAILS: sys.exit("back-navigation regression: " + "; ".join(FAILS))
sh("cmd uimode night no"); sh(f"am force-stop {PKG}")
