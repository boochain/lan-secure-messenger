#!/usr/bin/env python3
"""Relay client UI on Android (emulator, adb): turn the relay on in 设置 › 中转服务器（可选） with the test relay,
see a relay-only peer get the 经服务器 badge, and capture light + dark shots of the settings screen, the list with the
经服务器 filter chip and the chat header via the server. Also checks Back navigation and that the passphrase is write-only.
The relay runs on the host (127.0.0.1); the emulator reaches it at 10.0.2.2. There is no LAN path, so the relay is the only route.
usage: android_relay_shots.py --relay-bin relay-server --native-cli build/linux/lanmsg-cli --out DIR"""
import argparse, json, os, re, subprocess, sys, tempfile, time, xml.etree.ElementTree as ET
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import ui_seed
ap = argparse.ArgumentParser(); ap.add_argument("--relay-bin", required=True); ap.add_argument("--native-cli", required=True); ap.add_argument("--out", required=True)
args = ap.parse_args(); os.makedirs(args.out, exist_ok=True)
ADB = os.environ.get("ADB", "adb"); PKG = "io.github.frec86.lanmessenger"
RPORT, RPASS, PEER = 29443, "ci-relay-pass", "王工-中转"
def adb(*a, t=180): return subprocess.run([ADB] + list(a), capture_output=True, text=True, timeout=t).stdout
def sh(c, t=180): return adb("shell", c, t=t)
def tap(x, y): sh(f"input touchscreen motionevent DOWN {x} {y}; input touchscreen motionevent UP {x} {y}")
def nodes():
    sh("uiautomator dump /sdcard/ui.xml"); x = sh("cat /sdcard/ui.xml")
    return list(ET.fromstring(x[x.index("<?xml"):]).iter("node")) if "<?xml" in x else []
def centre(n): b = list(map(int, re.findall(r"\d+", n.get("bounds")))); return ((b[0]+b[2])//2, (b[1]+b[3])//2), b
def find_all(pred):
    out = []
    for n in nodes():
        if n.get("text") in ("Wait", "等待"): tap(*centre(n)[0]); time.sleep(2); return []   # TCG emulator ANR dialog
        if pred(n): out.append(n)
    return out
def is_(t): return lambda n: n.get("text") == t or n.get("content-desc") == t
def has(t): return bool(find_all(is_(t)))
def tap_text(t, timeout=60, idx=0):
    # note: an ElementTree node without children is falsy, so wait on its centre tuple
    r = ui_seed.wait(lambda: (lambda l: centre(l[idx])[0] if len(l) > idx else None)(find_all(is_(t))), timeout, 2)
    if r is None: raise RuntimeError("not found: " + t)
    tap(*r); time.sleep(2)
def field(label):
    """tap the EditText whose label is `label` (OutlinedTextField: the label Text is a child of the EditText node)"""
    for n in nodes():
        if n.get("class") == "android.widget.EditText" and any(c.get("text") == label for c in n.iter("node")):
            tap(*centre(n)[0]); time.sleep(1); return n
    raise RuntimeError("field not found: " + label)
def back(): sh("input keyevent 4"); time.sleep(2)
FAILS = []
def check(cond, what):
    print(("PASS " if cond else "FAIL ") + what, flush=True)
    if not cond: FAILS.append(what)
def shot(name): time.sleep(3); subprocess.run(f"{ADB} exec-out screencap -p > {args.out}/{name}.png", shell=True); print("shot", name, flush=True)

relay = subprocess.Popen([args.relay_bin, "--bind", "127.0.0.1", "--port", str(RPORT), "--password", RPASS],
                         stdout=open(f"{args.out}/relay-server.log", "w"), stderr=subprocess.STDOUT)
d = tempfile.mkdtemp(prefix="lsm-relay-peer-")
P = ui_seed.Peer(PEER, [args.native_cli, "--data", d, "--name", PEER, "--no-discovery", "--port", "42400", "--disco-port", "42401"], args.out)
P.cmd(f"relay 127.0.0.1 {RPORT} {RPASS}")
try:
    sh("svc power stayon true; input keyevent 224; wm dismiss-keyguard; settings put system screen_off_timeout 1800000")
    sh("cmd overlay enable com.android.internal.systemui.navbar.gestural")  # gesture navigation, as in android_chat_shots.py / the mockups
    for k in ("window_animation_scale", "transition_animation_scale", "animator_duration_scale"): sh(f"settings put global {k} 0")
    sh(f"pm clear {PKG}"); sh("cmd uimode night no"); sh(f"pm grant {PKG} android.permission.POST_NOTIFICATIONS")
    sh(f"am start -W -n {PKG}/.MainActivity"); time.sleep(10)
    check(bool(sh(f"pidof {PKG}").strip()), "app running after launch")
    # ---- 设置 › 中转服务器（可选）: off by default
    tap_text("设置"); tap_text("中转服务器（可选）")
    check(has("未启用") and has("保存并连接"), "relay screen opens, off by default (未启用)")
    check(has("局域网直连始终优先") or any("局域网直连始终优先" in (n.get("text") or "") for n in nodes()), "explains LAN direct is preferred")
    field("服务器地址"); sh("input text 10.0.2.2")
    field("端口"); sh("input keyevent 123; " + "input keyevent 67; " * 6 + f"input text {RPORT}")
    field("网络口令"); sh(f"input text {RPASS}"); sh("input keyevent 66")   # IME Done (Enter) saves
    time.sleep(2)
    check(ui_seed.wait(lambda: has("口令已设置") and has("网络口令（已设置）"), 20, 2) is not None, "Enter saves; passphrase shows 已设置 (label + row)")
    check(not any(RPASS in ((n.get("text") or "") + (n.get("content-desc") or "")) for n in nodes()), "passphrase is write-only (never shown again)")
    tap_text("通过中转服务器连接")                                            # toggle on -> saves with the stored passphrase
    check(ui_seed.wait(lambda: has("已连接中转服务器"), 90, 2) is not None, "relay toggled on -> 已连接中转服务器")
    sh("input keyevent 111"); time.sleep(1)                                    # Esc: hides the keyboard / clears focus
    # relay peer comes online through the server; trust both ways
    fp = ui_seed.wait(lambda: next((x[0][5:] for x in P.peers() if x[1] == "online" and "via=relay" in "\t".join(x)), None), 90)
    check(fp is not None, "app online at the relay-only peer (via=relay)")
    if fp: P.cmd(f"trust {fp}"); P.cmd(f"send {fp} 我在外地，经服务器联系你")
    back(); check(has("中转服务器（可选）") and not has("保存并连接"), "Back: relay screen -> 设置")
    tap_text("聊天")
    tap_text(PEER, 90); tap_text("菜单"); tap_text("信任"); back()
    check(ui_seed.wait(lambda: has("经服务器中转"), 30, 2) is not None, "relay peer in the list has the 经服务器 badge (icon + text)")
    for th in ("light", "dark"):
        sh("cmd uimode night " + ("yes" if th == "dark" else "no")); time.sleep(6)
        tap_text("设置"); tap_text("中转服务器（可选）"); shot(f"android-{th}-20-relay-settings"); back()
        tap_text("聊天"); shot(f"android-{th}-21-relay-list")
        tap_text("经服务器", idx=0)                                           # filter chip (first; row badges follow)
        check(has(PEER), f"{th}: 经服务器 filter shows the relay peer"); shot(f"android-{th}-22-relay-chip")
        tap_text(PEER); time.sleep(2)
        check(any((n.get("text") or "").endswith("已验证身份 · 经服务器") for n in nodes()), f"{th}: chat header 在线 · 已验证身份 · 经服务器")
        shot(f"android-{th}-23-relay-chat")
        back(); check(has("全部"), f"{th}: Back chat -> list"); back()     # Back clears the filter
        check(has("局域网安全通讯"), f"{th}: Back clears the filter, stays in the app")
    # ---- relay off: the peer goes offline (no LAN path), badge disappears
    tap_text("设置"); tap_text("中转服务器（可选）"); tap_text("通过中转服务器连接")
    check(ui_seed.wait(lambda: has("未启用"), 30, 2) is not None, "relay toggled off -> 未启用"); back(); tap_text("聊天")
    check(ui_seed.wait(lambda: not has("经服务器中转"), 60, 3) is not None, "badge gone after relay off")
except Exception as ex:
    FAILS.append(f"exception: {ex}"); print("EXCEPTION", ex, flush=True)
    shot("failure-screen"); sh("uiautomator dump /sdcard/ui.xml"); open(f"{args.out}/failure-ui.xml", "w").write(sh("cat /sdcard/ui.xml"))
finally:
    open(f"{args.out}/logcat.txt", "w").write(adb("logcat", "-d", "-v", "time", t=60))
    sh("cmd uimode night no"); P.stop(); relay.terminate()
json.dump({"fails": FAILS}, open(f"{args.out}/relay-ui-checks.json", "w"), ensure_ascii=False)
print("RELAY UI:", "OK" if not FAILS else "FAILED " + ", ".join(FAILS)); sys.exit(1 if FAILS else 0)
