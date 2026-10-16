#!/usr/bin/env python3
"""Peer-less Android UI screens in light AND dark (empty state, add-by-IP dialog, error toast, menu, PIN, about)
for tools/ui_color_audit.py. Reuses the TCG-safe helpers of android_ui_shots.py. usage: --out DIR"""
import os, sys, time, subprocess, re, xml.etree.ElementTree as ET
OUT = sys.argv[sys.argv.index("--out") + 1]; os.makedirs(OUT, exist_ok=True)
ADB = os.environ.get("ADB", "adb"); PKG = "io.github.frec86.lanmessenger"
def adb(*a, t=180): return subprocess.run([ADB] + list(a), capture_output=True, text=True, timeout=t).stdout
def tap(x, y): adb("shell", f"input touchscreen motionevent DOWN {x} {y}; input touchscreen motionevent UP {x} {y}")
def find(pred):
    adb("shell", "uiautomator", "dump", "/sdcard/ui.xml"); x = adb("shell", "cat", "/sdcard/ui.xml")
    if "<?xml" not in x: return None
    for n in ET.fromstring(x[x.index("<?xml"):]).iter("node"):
        if pred(n): b = list(map(int, re.findall(r"\d+", n.get("bounds")))); return ((b[0] + b[2]) // 2, (b[1] + b[3]) // 2)
def tap_text(t, timeout=90):
    end = time.time() + timeout
    while time.time() < end:
        r = find(lambda n: n.get("text") == t or n.get("content-desc") == t)
        if r: tap(*r); time.sleep(3); return
        time.sleep(2)
    raise RuntimeError("not found: " + t)
import json
IGN = {"_reasons": {"android-*-dialog/pin/about": "modal AlertDialog: the page behind the scrim is inactive (WCAG 1.4.3 exempts inactive UI); only the dialog window is audited"}}
def dialog_rect(png, th):
    """the dialog card = bounding box of the palette card_bg fill (the page behind it is darkened by the scrim)"""
    from PIL import Image; import numpy as np
    card = (0xFB, 0xFB, 0xFB) if th == "light" else (0x2B, 0x2B, 0x2B)
    import cv2
    m = (np.array(Image.open(png).convert("RGB")) == card).all(-1).astype(np.uint8)
    n, lab, st, _ = cv2.connectedComponentsWithStats(m, connectivity=4)
    k = 1 + int(np.argmax(st[1:, cv2.CC_STAT_AREA]))  # the dialog card is the largest card_bg blob
    x0, y0, w0, h0 = (int(st[k, j]) for j in range(4)); xs = np.array([x0, x0 + w0 - 1]); ys = np.array([y0, y0 + h0 - 1])
    i = 16  # inset past the 8 dp rounded corners (scrim shows in them)
    return [int(xs.min()) + i, int(ys.min()) + i, int(xs.max() - xs.min()) - 2 * i, int(ys.max() - ys.min()) - 2 * i]
def shot(n, modal=False):
    time.sleep(3); subprocess.run(f"{ADB} exec-out screencap -p > {OUT}/{n}.png", shell=True); print("shot", n, flush=True)
    if modal:
        IGN[n + ".png"] = {"only": dialog_rect(f"{OUT}/{n}.png", "dark" if "-dark-" in n else "light")}; json.dump(IGN, open(f"{OUT}/audit_ignore.json", "w"), ensure_ascii=False, indent=1)
for k in ("window_animation_scale", "transition_animation_scale", "animator_duration_scale"): adb("shell", "settings", "put", "global", k, "0")
for th in ("light", "dark"):
    adb("shell", "cmd", "uimode", "night", "yes" if th == "dark" else "no"); adb("shell", "am", "force-stop", PKG); time.sleep(3)
    adb("shell", "am", "start", "-W", "-n", f"{PKG}/.MainActivity"); time.sleep(12)
    shot(f"android-{th}-01-empty")
    tap_text("按 IP 添加"); shot(f"android-{th}-02-add-ip-dialog", True)
    r = find(lambda n: n.get("class") == "android.widget.EditText"); tap(*r); time.sleep(1); adb("shell", "input", "text", "bad"); time.sleep(1)
    tap_text("确定"); time.sleep(1); shot(f"android-{th}-03-error"); time.sleep(4)
    tap_text("菜单"); shot(f"android-{th}-04-menu")
    tap_text("生成 PIN（让对方输入）"); shot(f"android-{th}-05-pin", True); tap_text("好")
    tap_text("菜单"); tap_text("关于"); shot(f"android-{th}-06-about", True); tap_text("好")
adb("shell", "cmd", "uimode", "night", "no")
