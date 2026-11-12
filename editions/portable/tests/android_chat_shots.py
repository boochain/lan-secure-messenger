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
def tap(x, y): sh(f"input tap {x} {y}")  # one-shot tap; split DOWN/UP across input(1) startups was a long-press on ConvRow
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
def on_main_nav():
    return find(lambda n: n.get("text") == "聊天" or n.get("content-desc") == "聊天") is not None and find(lambda n: n.get("text") == "设置" or n.get("content-desc") == "设置") is not None
def on_scan_page():
    return find(lambda n: n.get("text") == "扫描二维码") is not None and find(lambda n: n.get("text") in ("从图片识别", "应用链接")) is not None
def back_to_main(tries=8):
    """Leave a QR dialog or the scan page. The first Back often only hides the IME."""
    sh("input keyevent 111"); time.sleep(0.3)  # KEYCODE_ESCAPE: dismiss IME if shown
    for _ in range(tries):
        if on_main_nav() and not on_scan_page():
            return
        sh("input keyevent 4"); time.sleep(1.0)
    raise RuntimeError("could not leave qr/scan to main nav")

IGN_PATH = None

def dialog_rect(png, th):
    """Dialog card = bbox of the palette card_bg fill (the page behind it is darkened by the scrim).
    Same helper as android_theme_shots.py (add-ip / pin / about)."""
    from PIL import Image
    import numpy as np, cv2
    card = (0xED, 0xED, 0xF4) if th == "light" else (0x28, 0x2A, 0x2F)  # M3 dialog = surfaceContainerHigh
    m = (np.array(Image.open(png).convert("RGB")) == card).all(-1).astype(np.uint8)
    n, lab, st, _ = cv2.connectedComponentsWithStats(m, connectivity=4)
    if n <= 1:
        raise RuntimeError(f"no dialog card_bg in {png}")
    k = 1 + int(np.argmax(st[1:, cv2.CC_STAT_AREA]))  # the dialog card is the largest card_bg blob
    x0, y0, w0, h0 = (int(st[k, j]) for j in range(4))
    xs = np.array([x0, x0 + w0 - 1]); ys = np.array([y0, y0 + h0 - 1])
    i = 2 * 28  # inset past the 28 dp rounded corners (scrim shows in them)
    rec = [int(xs.min()) + i, int(ys.min()) + i, int(xs.max() - xs.min()) - 2 * i, int(ys.max() - ys.min()) - 2 * i]
    if rec[2] < 100 or rec[3] < 100:
        raise RuntimeError(f"dialog card too small in {png}: {rec}")
    return rec

def qr_canvas_boxes(png):
    """White QR canvas(es): machine-readable pictures, not UI text. Threshold 252 keeps Android light
    page fill #F9F9FF from merging with the quiet zone (245 would cover the whole screen)."""
    from PIL import Image
    import numpy as np, cv2
    im = Image.open(png).convert("RGB")
    w, h = im.size
    a = np.asarray(im)
    white = ((a[..., 0] >= 252) & (a[..., 1] >= 252) & (a[..., 2] >= 252)).astype("uint8")
    k = cv2.getStructuringElement(cv2.MORPH_RECT, (3, 3))
    white = cv2.morphologyEx(white, cv2.MORPH_CLOSE, k, iterations=2)
    n, _, stt, _ = cv2.connectedComponentsWithStats(white, connectivity=8)
    boxes = []
    for i in range(1, n):
        x, y, bw, bh, area = map(int, stt[i])
        if area < 80 * 80:
            continue
        ratio = max(bw, bh) / max(1, min(bw, bh))
        if ratio > 1.25:
            continue
        roi = a[y:y + bh, x:x + bw]
        dark = int(((roi[..., 0] < 40) & (roi[..., 1] < 40) & (roi[..., 2] < 40)).sum())
        if dark < 200:
            continue
        pad = 4
        x0, y0 = max(0, x - pad), max(0, y - pad)
        x1, y1 = min(w, x + bw + pad), min(h, y + bh + pad)
        boxes.append([x0, y0, x1 - x0, y1 - y0])
    return boxes

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
    if "image-viewer" in name:
        from PIL import Image as _Im
        im = _Im.open(png)
        boxes = [[0, 0, im.size[0], im.size[1]]]
        ign["_reasons"][name + ".png"] = "fullscreen image viewer: user photo on a black scrim, plus the system status and gesture bars (they switch to dark while the viewer is up)"
    if name.endswith("-11-chat"):
        # tiled doodle wallpaper (Parts.kt wallpaper(), spec §6.4, ~4% opacity) sits beside incoming
        # bubbles and the stroke scanner reports it as faint text (same tile in light and dark).
        boxes.append([720, 176, 48, 80])
        ign["_reasons"][name + ".png"] = "user-sent photo/sticker thumbnails, colour-emoji glyphs, and a doodle-wallpaper tile (spec §6.4, ~4% opacity; not UI text)"
    modal_qr = bool(re.search(r"-(16-my-qr|19-server-qr|20-pair-qr)$", name))
    scan_qr = bool(re.search(r"-(17-scan|18-scan-image)$", name))
    if modal_qr:
        th = "dark" if "-dark-" in name else "light"
        entry = {"only": dialog_rect(png, th)}
        qrs = qr_canvas_boxes(png)
        if qrs:
            entry["ignore"] = qrs
        ign[name + ".png"] = entry
        ign["_reasons"][name + ".png"] = (
            "modal AlertDialog: the page behind the scrim is inactive (WCAG 1.4.3 exempts inactive UI); "
            "only the dialog card is audited. The QR code is a machine-readable graphic "
            "(black-on-white for scanners), not UI text"
        )
    elif scan_qr:
        qrs = qr_canvas_boxes(png)
        boxes.extend(qrs)
        ign["_reasons"][name + ".png"] = (
            "QR code in the camera preview or decoded image is a machine-readable graphic "
            "(black-on-white for scanners), not UI text"
        )
        if boxes:
            ign[name + ".png"] = boxes
    elif boxes:
        ign[name + ".png"] = boxes
    json.dump(ign, open(path, "w"), ensure_ascii=False, indent=1)

def _hit(label):
    return find(lambda n: n.get("text") == label or n.get("content-desc") == label
                or (n.get("content-desc") or "").startswith(label))
def dismiss_conv_menu():
    for _ in range(4):
        if find(lambda n: n.get("text") in ("置顶", "取消置顶", "标为已读", "查看指纹", "删除（仅本机）")):
            back(); time.sleep(0.45)
        else:
            break
def open_chat(name="王工"):
    dismiss_conv_menu()
    tap_text(name); time.sleep(1.2)
    if find(lambda n: n.get("text") == "输入消息" or n.get("content-desc") == "输入消息"):
        return True
    dismiss_conv_menu()
    tap_text(name); time.sleep(1.5)
    return find(lambda n: n.get("text") == "输入消息" or n.get("content-desc") == "输入消息") is not None
class App:
    def trust(s, name):
        row = ui_seed.wait(lambda: find(lambda n: n.get("text") == name), 90, 2)
        if not row: raise RuntimeError("not found: " + name)
        # Long-press the row: conv menu includes 信任 and does not leave the list.
        sh(f"input swipe {row[0]} {row[1]} {row[0]} {row[1]} 900"); time.sleep(1.5)
        r = _hit("信任")
        if r:
            tap(*r); time.sleep(1); return
        back(); time.sleep(0.6)
        tap(*row); time.sleep(1.5)
        r = _hit("信任")
        if r:
            tap(*r); time.sleep(1); back(); return
        r = _hit("菜单")
        if r:
            tap(*r); time.sleep(1.5)
            r = _hit("信任")
            if r: tap(*r); time.sleep(1)
            back(); return
        dump = [(n.get("text"), n.get("content-desc")) for n in nodes() if n.get("text") or n.get("content-desc")]
        raise RuntimeError("not found: 菜单 after " + name + " dump=" + repr(dump[:60]))
    def send_text(s, name, text):
        sh("am start -W -a android.intent.action.SEND -t text/plain --es android.intent.extra.TEXT " + shlex.quote(text) + f" -n {PKG}/.MainActivity")
        time.sleep(3); tap_text(name); time.sleep(1); back()

sh("svc power stayon true; input keyevent 224; wm dismiss-keyguard; settings put system screen_off_timeout 1800000")  # keep the screen on
sh("cmd overlay enable com.android.internal.systemui.navbar.gestural")  # gesture navigation, as on current phones (and in the mockup)
for k in ("window_animation_scale", "transition_animation_scale", "animator_duration_scale"): sh(f"settings put global {k} 0")
sh(f"pm clear {PKG}"); sh("cmd uimode night no"); sh(f"pm grant {PKG} android.permission.POST_NOTIFICATIONS")
sh(f"pm grant {PKG} android.permission.CAMERA")
sh("touch /data/local/tmp/lsm-ui-test")
adb("push", os.path.join(HERE, "assets", "lsm-qr-sample.png"), "/data/local/tmp/lsm-qr-sample.png")
sh(f"am start -W -n {PKG}/.MainActivity"); time.sleep(10)
# first-run overlay (设备模式 is the default radio)
if find(lambda n: n.get("text") == "开始使用"):
    shot("android-light-00-first-run")  # first-run LSMF_account_mode
    tap_text("开始使用"); time.sleep(1.5)
pdf = "/data/local/tmp/季度报告-Q3-最终版.pdf"; local_pdf = "/tmp/lsm-seed.pdf"
open(local_pdf, "wb").write(b"%PDF-1.4\n" + os.urandom(2_516_582))
adb("push", local_pdf, pdf)
sh("rm -rf /data/local/tmp/seed; mkdir -p /data/local/tmp/seed")
def mk(name, i):
    port = 42000 + 4 * i; tag = f"p{i}"
    p = ui_seed.Peer(name, [ADB, "shell", "-T", f"{args.cli} --data /data/local/tmp/seed/{tag} --name {shlex.quote(name)} --no-discovery --port {port} --disco-port {port + 1} --announce-ms 1000"], args.out)
    p.port = port; return p
photo = "/data/local/tmp/界面截图.jpg"; adb("push", os.path.join(HERE, "assets", "ui-photo.jpg"), photo)
sticker = "/data/local/tmp/lsm-sticker-thumb.png"; adb("push", os.path.join(HERE, "..", "stickers", "thumb.png"), sticker)
# App JNI sets bind_lan_ip=auto so TCP listen is on the emulator LAN IP (typically 10.0.2.15), not 127.0.0.1.
def emu_lan_ip():
    out = sh("ip -4 -o addr show scope global 2>/dev/null || true")
    for line in out.splitlines():
        m = re.search(r"inet (\d+\.\d+\.\d+\.\d+)", line)
        if m and not m.group(1).startswith("127."):
            return m.group(1)
    return "10.0.2.15"
app_addr = f"{emu_lan_ip()}:41235"; print("seed app_addr", app_addr, flush=True)
P = ui_seed.seed(mk, app_addr, App(), pdf, photo, sticker=sticker)
for th in ("light", "dark"):
    sh("cmd uimode night " + ("yes" if th == "dark" else "no")); time.sleep(6)
    shot(f"android-{th}-10-list")
    # LSMF_chat_list_menu: long-press a conversation row (王工)
    row = find(lambda n: n.get("text") == "王工")
    if row:
        sh(f"input swipe {row[0]} {row[1]} {row[0]} {row[1]} 900"); time.sleep(1.5)
        shot(f"android-{th}-14-conv-menu-longpress")  # conv-menu-longpress LSMF_chat_list_menu LSMF_chat_pin LSMF_chat_mark_read LSMF_chat_mark_unread LSMF_chat_mute LSMF_chat_delete
        if find(lambda n: n.get("text") in ("置顶", "取消置顶")) is None:
            print("WARN long-press conv menu missing 置顶", flush=True)
        back(); time.sleep(1)
        # LSMF_chat_list_swipe: swipe left from the right side of the row (Telegram style)
        def swipe_open():
            hit = find(lambda n: n.get("text") == "王工")
            if not hit:
                return False
            # start to the right of the name so the gesture crosses the row, duration 700ms
            x1, y = min(hit[0] + 320, 760), hit[1]
            x2 = max(hit[0] - 200, 40)
            sh(f"input swipe {x1} {y} {x2} {y} 700"); time.sleep(1.2)
            return find(lambda n: n.get("text") in ("置顶", "取消置顶")) is not None and find(lambda n: n.get("text") in ("已读", "删除", "免打扰")) is not None
        ok_swipe = swipe_open()
        if not ok_swipe:
            back(); time.sleep(0.4)
            ok_swipe = swipe_open()
        shot(f"android-{th}-15-conv-menu-swipe")  # conv-menu-swipe LSMF_chat_list_swipe inline 置顶/已读/免打扰/删除
        if not ok_swipe:
            raise RuntimeError("swipe conv menu missing inline 置顶/已读/免打扰/删除")
        back(); time.sleep(1)
    else:
        print("WARN no 王工 row for conv long-press/swipe", flush=True)
    # LSMF_nav_* bottom tabs + overflow pages
    # LSMF_nav_chats LSMF_nav_contacts LSMF_nav_groups LSMF_nav_transfers LSMF_nav_files LSMF_nav_settings LSMF_nav_profile LSMF_nav_diagnostics
    for label, hook in (("联系人", "LSMF_nav_contacts"), ("群组", "LSMF_nav_groups"),
                        ("传输", "LSMF_nav_transfers"), ("文件站", "LSMF_nav_files"),
                        ("设置", "LSMF_nav_settings"), ("聊天", "LSMF_nav_chats")):
        try:
            tap_text(label, timeout=20)
            shot(f"android-{th}-page-{hook.split('_')[-1]}")
        except Exception as e:
            print("WARN nav", label, e, flush=True)
    try:
        if find(lambda n: n.get("content-desc") == "个人资料"):
            tap_text("个人资料", timeout=15)
            shot(f"android-{th}-page-profile")
            back(); time.sleep(1)
        tap_text("菜单", timeout=15)
        tap_text("诊断信息", timeout=15)
        shot(f"android-{th}-page-diagnostics")
        back(); time.sleep(1)
    except Exception as e:
        print("WARN profile/diag pages", e, flush=True)
    # opened QR + scan view (fake camera) + scan-from-image + server/pair QR
    if find(lambda n: n.get("text") == "聊天" or n.get("content-desc") == "聊天"):
        try: tap_text("聊天", timeout=10)
        except Exception: pass
    def tap_row(label, timeout=15):
        r = ui_seed.wait(lambda: find(lambda n: n.get("text") == label or n.get("content-desc") == label), timeout, 1)
        if not r:
            sh("input swipe 540 1500 540 400 400"); time.sleep(0.7)
            r = find(lambda n: n.get("text") == label or n.get("content-desc") == label)
        if not r: raise RuntimeError("not found: " + label)
        tap(*r); time.sleep(1.2)
    try:
        tap_text("菜单", timeout=15)
        tap_text("我的二维码", timeout=15)
        shot(f"android-{th}-16-my-qr")  # my-qr LSMF_qr_profile
        back(); time.sleep(1)
        # settings QR first: system Back on the scan page used to toast 再按一次退出 / exit the app
        tap_text("设置", timeout=15)
        time.sleep(0.6)
        tap_row("服务器二维码")
        shot(f"android-{th}-19-server-qr")  # server-qr LSMF_qr_server
        back(); time.sleep(0.8)
        if not find(lambda n: n.get("text") == "配对新设备"):
            if find(lambda n: n.get("text") == "设置" or n.get("content-desc") == "设置"):
                try: tap_text("设置", timeout=8)
                except Exception: pass
            sh("input swipe 540 1500 540 400 400"); time.sleep(0.7)
        tap_row("配对新设备")
        shot(f"android-{th}-20-pair-qr")  # pair-qr LSMF_qr_pair
        back(); time.sleep(0.8)
        if find(lambda n: n.get("text") == "聊天" or n.get("content-desc") == "聊天"):
            try: tap_text("聊天", timeout=8)
            except Exception: pass
        tap_text("菜单", timeout=15)
        tap_text("扫描二维码", timeout=15)
        shot(f"android-{th}-17-scan")  # scan LSMF_qr_scan
        tap_row("从图片识别")
        time.sleep(1)
        shot(f"android-{th}-18-scan-image")  # scan-image LSMF_qr_scan
        # the toolbar 返回 button dismisses the scan overlay; system Back also should, after BackHandler
        if find(lambda n: n.get("text") == "返回" or n.get("content-desc") == "返回"):
            tap_text("返回", timeout=8)
        else:
            back_to_main()
        time.sleep(0.8)
    except Exception as e:
        raise RuntimeError("qr/scan shots failed: " + str(e))
    if find(lambda n: n.get("text") == "聊天" or n.get("content-desc") == "聊天"):
        try: tap_text("聊天", timeout=10)
        except Exception: pass
    open_chat("王工"); time.sleep(0.8); shot(f"android-{th}-11-chat")
    img = find(lambda n: re.search(r"\.(jpe?g|png|webp|gif)$", n.get("content-desc") or "", re.I))
    if img:
        tap(*img); time.sleep(1.5); shot(f"android-{th}-12-image-viewer")
        z = find(lambda n: n.get("text") == "+" or n.get("content-desc") == "放大")
        if z:
            for _ in range(4):
                tap(*z); time.sleep(0.25)
            time.sleep(0.6)
        shot(f"android-{th}-13-image-viewer-zoom")
        back(); time.sleep(1)
    else:
        print("WARN no image bubble to open", flush=True)
    back()
# ---- regression: system Back navigates (chat -> list), closes menus first, survives rotation, double-back exits
def has(t): return find(lambda n: n.get("text") == t or n.get("content-desc") == t) is not None
def resumed(): return PKG in sh("dumpsys activity activities | grep -E 'mResumedActivity|topResumedActivity'")
FAILS = []
def check(cond, what):
    print(("PASS " if cond else "FAIL ") + what, flush=True)
    if not cond: FAILS.append(what)
sh("cmd uimode night no"); time.sleep(4)
# delete (this device only): long-press a bubble -> 删除（仅本机） -> confirm -> gone
open_chat("王工")
r = ui_seed.wait(lambda: find(lambda n: (n.get("text") or "").startswith("这是最新的数据")), 30, 2)
if r: sh(f"input swipe {r[0]} {r[1]} {r[0]} {r[1]} 900"); time.sleep(2)
okm = has("删除（仅本机）")
if okm: tap_text("删除（仅本机）"); tap_text("删除")
time.sleep(2); check(okm and not has("这是最新的数据，第 4 页有改动。"), "long-press 删除（仅本机） with confirmation removes the message")
back()
check(open_chat("王工") and has("输入消息"), "open chat")
back(); check(has("局域网安全通讯") and not has("输入消息") and resumed(), "Back from chat returns to the list (app stays open)")
open_chat("王工"); tap_text("菜单"); check(has("查看指纹"), "chat menu open")
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
open_chat("王工"); sh("input keyevent 3"); time.sleep(2); pid0 = sh(f"pidof {PKG}").strip(); sh(f"am kill {PKG}"); time.sleep(3); pid1 = sh(f"pidof {PKG}").strip()
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
