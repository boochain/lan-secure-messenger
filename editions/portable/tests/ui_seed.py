#!/usr/bin/env python3
"""Shared screenshot fixture: real lanmsg-cli peers that reproduce the redesign mockup conversation
(design/redesign/data.py) against the app under test, so every platform's CI screenshots show the same
contacts, group, unread count, text exchange, image and file as the approved mockups.

The app side is driven through an `app` adapter with: trust(name), send_text(name, text),
open_chat(name), back(). Peers are any command that speaks the lanmsg-cli line protocol (native, wine, android)."""
import os, queue, re, subprocess, threading, time

class Peer:
    def __init__(s, name, cmd, log_dir):
        s.name, s.lines, s.q = name, [], queue.Queue()
        s.p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        s.log = open(os.path.join(log_dir, "seed-%s.log" % re.sub(r"\W", "_", name)), "w", encoding="utf-8")
        threading.Thread(target=s._rd, daemon=True).start()
        s.fp = re.search(r"OK id (\S+)", s.cmd("id", 60)[-1]).group(1)
    def _rd(s):
        for raw in s.p.stdout:
            l = raw.decode("utf-8", "replace").rstrip("\r\n"); s.log.write(l + "\n"); s.log.flush(); s.lines.append(l); s.q.put(l)
    def cmd(s, c, t=30):
        while not s.q.empty(): s.q.get_nowait()
        s.p.stdin.write((c + "\n").encode("utf-8")); s.p.stdin.flush(); out = []; end = time.time() + t
        while time.time() < end:
            try: l = s.q.get(timeout=0.5)
            except queue.Empty: continue
            out.append(l)
            if l.startswith(("OK", "ERR")): return out
        raise TimeoutError(f"{s.name}: {c}")
    def peers(s):
        return [l.split("\t") for l in s.cmd("peers") if l.startswith("PEER ")]
    def online(s, fp): return any(x[0] == "PEER " + fp and x[1] == "online" for x in s.peers())
    def stop(s):
        try: s.cmd("quit", 10)
        except Exception: pass
        try: s.p.wait(10)
        except Exception: s.p.kill()

def wait(f, t, step=1.0):
    end = time.time() + t
    while time.time() < end:
        try:
            r = f()
            if r: return r
        except Exception: pass
        time.sleep(step)
    return None

PEERS = ["王工", "Alice Chen", "张伟", "李娜", "Bob Miller"]
GROUP = "产品设计组"

def seed(mk, app_addr, app, pdf, photo, say=print):
    """mk(name, index) -> Peer. Returns the peers by name. Order = oldest activity first, so the list sorts like the mockup."""
    P = {n: mk(n, i) for i, n in enumerate(PEERS)}
    for p in P.values(): p.cmd(f"add {app_addr}")
    def app_fp():
        for x in P["王工"].peers():
            if x[1] == "online" and x[0][5:] not in [q.fp for q in P.values()]: return x[0][5:]
    fp = wait(app_fp, 90); assert fp, "app never came online for the seed peers"
    say(f"app fp {fp}")
    for p in P.values(): wait(lambda: p.online(fp), 60); p.cmd(f"trust {fp}")
    # 李娜 <-> 王工 know each other (group members)
    w, l = P["王工"], P["李娜"]
    l.cmd(f"add {app_addr.rsplit(':', 1)[0]}:{w.port}"); wait(lambda: l.online(w.fp) and w.online(l.fp), 60); l.cmd(f"trust {w.fp}"); w.cmd(f"trust {l.fp}")
    for n in ("王工", "李娜"): app.trust(n)
    time.sleep(2)
    P["Bob Miller"].cmd(f"send {fp} Thanks, got it!"); time.sleep(1.2)
    P["张伟"].cmd(f"send {fp} 季度报告我已经发到你邮箱了"); time.sleep(1.2)
    P["Alice Chen"].cmd(f"send {fp} Can you send me the latest build?"); time.sleep(1.2)
    w.cmd(f"send {fp} 早上好！下午三点的评审会改到 302 会议室了。"); time.sleep(.6)
    w.cmd(f"send {fp} Slides are in the shared folder — please take a look before the meeting."); time.sleep(1.5)
    app.send_text("王工", "收到 👍 新版界面截图发我看看？")
    w.cmd(f"read {fp}"); time.sleep(1)
    w.cmd(f"sendfile {fp} {photo}"); time.sleep(4)
    w.cmd(f"sendfile {fp} {pdf}"); time.sleep(4)
    w.cmd(f"send {fp} 这是最新的数据，第 4 页有改动。"); time.sleep(1.5)
    app.send_text("王工", "OK, see you at 3 pm. 会议室我来订。")
    time.sleep(2)
    gid = l.cmd(f"group-create {GROUP} {fp},{w.fp}")[-1].split()[-1]; say("group " + gid); time.sleep(4)
    for t in ("大家好，这周五交付新版界面", "图标的颜色按新规范统一了", "新版图标我放共享盘了，大家看下"):
        l.cmd(f"sendgroup {gid} {t}"); time.sleep(.8)
    time.sleep(3)
    return P
