#!/usr/bin/env python3
"""Tiny QEMU HMP helper: qmon.py shot out.png | key <keys...> | type <text> | click x y | raw <hmp cmd>"""
import socket, sys, time, subprocess, os
MON = os.environ.get("QMON", "/tmp/mon.sock")
def hmp(cmd):
    s = socket.socket(socket.AF_UNIX); s.connect(MON); s.settimeout(3)
    try: s.recv(4096)
    except Exception: pass
    s.sendall((cmd + "\n").encode()); time.sleep(0.25)
    try: out = s.recv(65536).decode("utf-8", "replace")
    except Exception: out = ""
    s.close(); return out
KEYMAP = {" ": "spc", "\n": "ret", ".": "dot", "\\": "backslash", ":": "shift-semicolon", "-": "minus", "/": "slash", "_": "shift-minus", ",": "comma", "=": "equal", "\"": "shift-apostrophe", "%": "shift-5", "!": "shift-1", "@": "shift-2", "#": "shift-3", "(": "shift-9", ")": "shift-0", "+": "shift-equal", "'": "apostrophe", ";": "semicolon"}
def typ(text):
    for ch in text:
        if ch in KEYMAP: k = KEYMAP[ch]
        elif ch.isupper(): k = "shift-" + ch.lower()
        else: k = ch
        hmp("sendkey " + k); time.sleep(0.05)
if __name__ == "__main__":
    a = sys.argv[1:]
    if a[0] == "shot":
        hmp(f"screendump /tmp/_s.ppm"); time.sleep(0.5)
        subprocess.run(["convert", "/tmp/_s.ppm", a[1]]); print(a[1])
    elif a[0] == "key":
        for k in a[1:]: hmp("sendkey " + k); time.sleep(0.15)
    elif a[0] == "type": typ(" ".join(a[1:]))
    elif a[0] == "click":
        # usb-tablet absolute coords 0..32767; screen size from argv or 800x600
        W, H = (int(a[3]), int(a[4])) if len(a) > 4 else (800, 600)
        x, y = int(a[1]) * 32767 // W, int(a[2]) * 32767 // H
        hmp(f"mouse_move {x} {y}"); time.sleep(0.2); hmp("mouse_button 1"); time.sleep(0.15); hmp("mouse_button 0")
    elif a[0] == "dblclick":
        W, H = (int(a[3]), int(a[4])) if len(a) > 4 else (800, 600)
        x, y = int(a[1]) * 32767 // W, int(a[2]) * 32767 // H
        hmp(f"mouse_move {x} {y}"); time.sleep(0.2)
        for _ in range(2): hmp("mouse_button 1"); time.sleep(0.08); hmp("mouse_button 0"); time.sleep(0.08)
    elif a[0] == "raw": print(hmp(" ".join(a[1:])))
