#!/usr/bin/env python3
"""Drive the GTK GUI under Xvfb (openbox + stalonetray as tray host) against a real v0.5 lan-daemon and
screenshot every page in light and dark theme. Usage: python3 tests/linux_gui_shots.py --rust-bin DIR --gui BIN --out DIR"""
import argparse, json, os, shutil, subprocess, time
ap = argparse.ArgumentParser(); ap.add_argument("--rust-bin", required=True); ap.add_argument("--gui", required=True); ap.add_argument("--out", required=True)
ap.add_argument("--themes", default="light,dark"); a = ap.parse_args()
W = "/tmp/lsm-gui"; shutil.rmtree(W, ignore_errors=True); os.makedirs(W); os.makedirs(a.out, exist_ok=True)
DISP = ":57"; env = dict(os.environ, DISPLAY=DISP, NO_AT_BRIDGE="1", LANG="zh_CN.UTF-8", LC_ALL="zh_CN.UTF-8")
procs = []
def bg(cmd, **kw): p = subprocess.Popen(cmd, env=kw.pop("env", env), stdout=subprocess.DEVNULL, stderr=open(f"{W}/{os.path.basename(cmd[0])}.err", "a"), **kw); procs.append(p); return p
def x(*c): return subprocess.run(["xdotool"] + list(c), env=env, capture_output=True, text=True).stdout.strip()
CUR = {}
def shot(name):  # root grab cropped to the main window (dialogs/menus/tooltips sit on top of it); no desktop pixels
    g = {}
    if CUR.get("wid"):
        for l in x("getwindowgeometry", "--shell", CUR["wid"]).splitlines():
            k, _, v = l.partition("="); g[k] = int(v) if v.isdigit() else v
    crop = ["-crop", f'{g["WIDTH"]}x{g["HEIGHT"]}+{g["X"]}+{g["Y"]}', "+repage"] if g else []
    subprocess.run(["import", "-window", "root"] + crop + [f"{a.out}/{name}.png"], env=env); print("shot", name, flush=True)
def lm(*c): return subprocess.run([f"{a.rust_bin}/lan-msg", "--ipc", f"{W}/r.sock"] + list(c), capture_output=True, text=True, timeout=30).stdout
bg(["Xvfb", DISP, "-screen", "0", "1280x800x24", "-nolisten", "tcp"]); time.sleep(2)
dbus = subprocess.run(["dbus-launch", "--sh-syntax"], env=env, capture_output=True, text=True).stdout
for line in dbus.splitlines():
    if line.startswith("DBUS_SESSION_BUS_ADDRESS="): env["DBUS_SESSION_BUS_ADDRESS"] = line.split("=", 1)[1].strip("';")
bg(["openbox", "--config-file", os.path.join(os.path.dirname(os.path.abspath(__file__)), "openbox-nodecor.xml")]); time.sleep(1)
bg(["stalonetray", "--geometry", "6x1-0+0", "--icon-size", "24", "--kludges", "force_icons_size", "-bg", "#3c3c3c"]); time.sleep(1)
# real v0.5 daemon (Rust) as the peer
rd = bg([f"{a.rust_bin}/lan-daemon", "--data-dir", f"{W}/r", "--ipc", f"{W}/r.sock", "--set", 'display_name="王工（Windows v0.5）"', "--set", "listen_port=47320",
         "--set", "discovery_port=47321", "--set", "discovery_enabled=false", "--set", "auto_trust=true", "--set", f'download_dir="{W}/rdl"'], env=dict(os.environ, LSM_NO_DIALOG="1"))
for _ in range(50):
    if os.path.exists(f"{W}/r.sock"): break
    time.sleep(0.2)
time.sleep(1)
# image + document to send from the v0.5 side
from PIL import Image, ImageDraw
im = Image.new("RGB", (640, 400), (40, 110, 180)); d = ImageDraw.Draw(im)
for i in range(0, 640, 40): d.rectangle([i, 300 - i // 3, i + 30, 400], fill=(250, 200 - i // 4, 60))
d.ellipse([480, 40, 580, 140], fill=(255, 230, 120)); im.save(f"{W}/会议白板.png")
open(f"{W}/季度报告.txt", "w", encoding="utf-8").write("季度报告\n" * 2000)
for th in a.themes.split(","):
    tenv = dict(env, LSM_THEME=th, LSM_INSTANCE=th)
    gui = bg([a.gui, "--data", f"{W}/gui", "--name", "李明的笔记本", "--port", "47330", "--disco-port", "47331", "--no-discovery"], env=tenv)
    time.sleep(4)
    wid = x("search", "--onlyvisible", "--classname", "lan-messenger-gtk").split("\n")[-1]
    x("windowmove", wid, "60", "40"); x("windowsize", wid, "1100", "700"); x("windowactivate", wid); time.sleep(1); CUR["wid"] = wid
    gfp = None
    if th == a.themes.split(",")[0]:
        lm("add", "127.0.0.1:47330"); time.sleep(5)
        st = json.loads(lm("--json", "status")); gfp = [p for p in st["peers"]][0]["fingerprint"]; json.dump({"gfp": gfp}, open(f"{W}/ids.json", "w"))
    gfp = json.load(open(f"{W}/ids.json"))["gfp"]
    x("mousemove", "5", "790"); time.sleep(0.5); shot(f"{th}-01-welcome")
    # select the contact (first row of the list)
    x("mousemove", "--window", wid, "150", "128"); x("click", "1"); time.sleep(1.5)
    if th == a.themes.split(",")[0]:
        # trust from the GUI menu: open menu, pick 信任当前联系人 (3rd entry)
        x("key", "F10"); time.sleep(1)
        shot(f"{th}-03-menu"); x("key", "Escape"); time.sleep(0.5)
        x("key", "F10"); time.sleep(0.8); x("key", "Down", "Return"); time.sleep(1)
        lm("send", gfp, "李明你好，下午三点的评审会改到 302 会议室。"); time.sleep(1.5)
        x("mousemove", "--window", wid, "560", "640"); x("click", "1"); time.sleep(0.5); x("type", "--delay", "40", "OK, thanks!"); x("key", "Return"); time.sleep(1.5)
        lm("send", gfp, "顺便把白板照片和季度报告发你，请查收 📎"); lm("send-file", gfp, f"{W}/会议白板.png"); lm("send-file", gfp, f"{W}/季度报告.txt"); time.sleep(6)
    else:
        x("key", "F10"); time.sleep(1); shot(f"{th}-03-menu"); x("key", "Escape"); time.sleep(0.5)
    x("mousemove", "--window", wid, "150", "128"); x("click", "1"); time.sleep(2)
    if True:
        # GUI -> v0.5 file via the core (drag-drop path is the same lsm_send_file call), image preview on the sender side too
        from PIL import Image as _I; _I.new("RGB", (300, 200), (30, 160, 90)).save(f"{W}/截图.png")
        x("mousemove", "--window", wid, "1036", "645"); x("click", "1"); time.sleep(2); shot(f"{th}-09-file-chooser")
        x("key", "ctrl+l"); time.sleep(0.5); x("type", "--delay", "20", f"{W}/截图.png"); time.sleep(0.5); x("key", "Return"); time.sleep(5)
    shot(f"{th}-02-chat")
    # add-by-IP dialog (header + button)
    x("mousemove", "--window", wid, "22", "20"); x("click", "1"); time.sleep(1.2); x("type", "192.168.1.20"); time.sleep(0.5); shot(f"{th}-04-add-by-ip"); x("key", "Escape"); time.sleep(0.8)
    # error state: invalid address -> error dialog
    x("mousemove", "--window", wid, "22", "20"); x("click", "1"); time.sleep(1.2); x("type", "bad address"); x("key", "Return"); time.sleep(1.5); shot(f"{th}-10-error"); x("key", "Return"); time.sleep(0.5); x("key", "Escape"); time.sleep(0.8)
    # tooltip on the header-bar + button
    x("mousemove", "--window", wid, "20", "18"); time.sleep(0.3); x("mousemove", "--window", wid, "22", "20"); time.sleep(2.5); shot(f"{th}-11-tooltip"); x("mousemove", "--window", wid, "500", "400"); time.sleep(0.5)
    # transfers dialog: menu, last-but-6 entry -> use keyboard: F10 then Up x6
    def menu_up(n):
        x("key", "F10"); time.sleep(0.8)
        for _ in range(n): x("key", "Up"); time.sleep(0.05)
        x("key", "Return"); time.sleep(1.5)
    menu_up(6); shot(f"{th}-05-transfers"); x("key", "Escape"); time.sleep(0.8)
    menu_up(2); shot(f"{th}-06-about"); x("key", "Return"); time.sleep(0.8)
    # PIN offer dialog: F10, Down x5
    x("key", "F10"); time.sleep(0.8)
    for _ in range(4): x("key", "Down"); time.sleep(0.05)
    x("key", "Return"); time.sleep(1.5); shot(f"{th}-07-pin"); x("key", "Return"); time.sleep(0.8)
    # drag-drop hint is only visible during a drag; tray shown top-right in stalonetray
    subprocess.run(["import", "-window", "root", "-crop", "200x40+1080+0", "-scale", "300%", f"{a.out}/{th}-08-tray-crop.png"], env=env)
    gui.terminate(); gui.wait(5); time.sleep(1)
print("history on v0.5 side:"); print(lm("history", json.load(open(f"{W}/ids.json"))["gfp"]))
print("transfers on v0.5 side:"); print(lm("transfers"))
print("gui downloads:", os.listdir(f"{W}/gui/downloads") if os.path.exists(f"{W}/gui/downloads") else None)
lm("shutdown")
for p in procs[::-1]:
    try: p.terminate()
    except Exception: pass
