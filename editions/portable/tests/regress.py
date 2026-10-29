#!/usr/bin/env python3
"""Regression tests for XP-edition bugs. Usage: python3 tests/regress.py <lanmsg-cli[.exe]>
Runs the exe under wine when it ends in .exe."""
import os, shutil, subprocess, sys, tempfile, time, threading, queue
exe = sys.argv[1]
cmd = (["wine", exe] if exe.endswith(".exe") else [exe])
fails = 0
def start(extra=()):
    d = tempfile.mkdtemp(prefix="lsmreg")
    env = dict(os.environ, LSM_VERBOSE="1", WINEDEBUG="-all")
    p = subprocess.Popen(cmd + ["--data", d, "--name", "Reg", "--no-discovery", "--port", "0"] + list(extra),
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, env=env)
    q = queue.Queue()
    threading.Thread(target=lambda: [q.put(l.decode("utf-8", "replace")) for l in p.stdout], daemon=True).start()
    return p, q
def wait(q, pat, t):
    end = time.time() + t
    while time.time() < end:
        try: l = q.get(timeout=max(0.05, end - time.time()))
        except queue.Empty: break
        if pat in l: return l
    return None
def check(name, ok, info=""):
    global fails
    print(("[PASS] " if ok else "[FAIL] ") + name, info); fails += (not ok)

# 1. refused non-blocking connect must be noticed quickly (Winsock reports it only in exceptfds;
#    before the fix the link sat in "connecting" until the handshake timeout).
p, q = start()
assert wait(q, "EV ready", 30), "no ready"
p.stdin.write(b"add 127.0.0.1:1\n"); p.stdin.flush(); t0 = time.time()
l = wait(q, "connect failed", 8)
check("refused connect detected < 8 s", bool(l), f"{time.time()-t0:.1f}s")
p.stdin.write(b"quit\n"); p.stdin.flush(); p.wait(20)

# 2. piped stdin with CRLF lines (cmd 'echo x>>file' / 'type file |') must be parsed; EOF exits cleanly
p, q = start()
p.stdin.write(b"id\r\nname \xe6\xb5\x8b\xe8\xaf\x95\r\nid\r\n"); p.stdin.close()
l1 = wait(q, "OK id", 30); l2 = wait(q, "OK id", 10); bye = wait(q, "OK bye", 20)
check("CRLF piped commands + rename (UTF-8)", bool(l1 and l2 and "\u6d4b\u8bd5" in l2 and bye), (l2 or "").strip())

# 3. stdout redirected to a file from cmd.exe (XP users do "lanmsg-cli < cmds.txt > out.txt"):
#    every line present, CRLF endings, nothing overwritten by the event thread.
if exe.endswith(".exe"):
    d = tempfile.mkdtemp(prefix="lsmredir"); shutil.copy(exe, d + "/cli.exe")
    open(d + "/c.txt", "wb").write(b"add 127.0.0.1:1\r\nid\r\nsleep 1500\r\nquit\r\n")
    subprocess.run(["wine", "cmd", "/c", "cli.exe --data dd --no-discovery < c.txt > out.txt"], cwd=d, env=dict(os.environ, WINEDEBUG="-all"), timeout=90)
    out = open(d + "/out.txt", "rb").read()
    lines = out.split(b"\r\n")
    okk = any(l.startswith(b"OK add") for l in lines) and all(l[:2] in (b"OK", b"EV", b"ER", b"") for l in lines) and any(l.startswith(b"OK id") for l in lines) and any(l.startswith(b"EV ready") for l in lines) and b"OK bye" in out and b"\n" not in out.replace(b"\r\n", b"")
    check("redirected stdout: all lines, CRLF, first line intact", okk, repr(out[:60]))

# 4. file messages keep name + size across restart (both directions). Before the fix history.dat
#    stored the file message before its name/size were set, so after a restart it showed "(0 KB)".
class Node:
    def __init__(n, tag, port):
        n.d = f"/tmp/lsmreg-{tag}-{os.getpid()}"; n.lines = []
        dd = n.d if not exe.endswith(".exe") else "Z:" + n.d.replace("/", "\\")
        n.p = subprocess.Popen(cmd + ["--data", dd, "--name", tag, "--no-discovery", "--port", str(port), "--disco-port", str(port + 1)],
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, env=dict(os.environ, WINEDEBUG="-all"))
        threading.Thread(target=lambda: [n.lines.append(l.decode("utf-8", "replace").rstrip("\r\n")) for l in n.p.stdout], daemon=True).start()
        # take the fingerprint from the "OK id" line itself: an async "EV peer <fp> online" line (the other node
        # reconnecting) can arrive right after it, and its 3rd token is the *other* node's fingerprint
        n.fp = next(l for l in n.cmd("id", 60) if l.startswith("OK id")).split()[2]
    def cmd(n, line, t=30):
        k = len(n.lines); n.p.stdin.write((line + "\n").encode()); n.p.stdin.flush(); end = time.time() + t
        while time.time() < end:
            new = n.lines[k:]
            if any(l.startswith(("OK", "ERR")) for l in new): return new
            time.sleep(0.1)
        return n.lines[k:]
    def has(n, pat, t):
        end = time.time() + t
        while time.time() < end:
            if any(pat in l for l in n.lines): return True
            time.sleep(0.2)
        return False
    def quit(n): n.cmd("quit", 20); n.p.wait(20)
base = 47400 + os.getpid() % 200 * 4
A = Node("RA", base); B = Node("RB", base + 2)
B.cmd(f"add 127.0.0.1:{base}"); A.has("online", 10); B.has("online", 10); A.cmd(f"trust {B.fp}"); B.cmd(f"trust {A.fp}")
src = f"/tmp/lsmreg-file-{os.getpid()}.bin"; open(src, "wb").write(os.urandom(70000)); name = os.path.basename(src)
B.cmd(f"sendfile {A.fp} " + (src if not exe.endswith(".exe") else "Z:" + src.replace("/", "\\")))
okrx = A.has("\tin\tdone\t", 30)
# no grace period: the core persists (and syncs) the message record before it emits 'done', so quitting right after
# the receiver's and the sender's 'done' events must not lose it
okrx = okrx and B.has("\tout\tdone\t", 30)
A.quit(); B.quit()
A = Node("RA", base); B = Node("RB", base + 2)
la = [l for l in A.cmd(f"history {B.fp}") if "[file]" in l]; lb = [l for l in B.cmd(f"history {A.fp}") if "[file]" in l]
check("file message name survives restart (receiver + sender)", okrx and any(name in l for l in la) and any(name in l for l in lb), (la[:1], lb[:1]))
# 5. delete on this device only: gone locally (also after restart, via a tombstone record), the peer keeps it
mid = next(l.split()[2] for l in B.cmd(f"send {A.fp} 删除测试 delete-me") if l.startswith("OK"))
A.has("delete-me", 20)
ha = [l for l in A.cmd(f"history {B.fp}") if "delete-me" in l]
did = ha[0].split()[1] if ha else ""
okd = bool(did) and any(l.startswith("OK delete") for l in A.cmd(f"delete {did}")) and any(l.startswith("ERR") for l in A.cmd(f"delete {did}"))
A.quit(); A = Node("RA", base)
gone = not any("delete-me" in l for l in A.cmd(f"history {B.fp}"))
kept = any("delete-me" in l for l in B.cmd(f"history {A.fp}"))
check("delete message (this device only) survives restart, peer keeps it", okd and gone and kept, (mid, did, okd, gone, kept))
# 6. 免打扰 (mute) is local, persisted, and leaves trust alone
okm = any(l.startswith("OK mute") for l in A.cmd(f"mute {B.fp}"))
A.quit(); A = Node("RA", base)
pa = [l for l in A.cmd("peers") if l.startswith("PEER " + B.fp)]
muted_after_restart = bool(pa) and "\tmuted" in pa[0] and "trusted" in pa[0]
A.cmd(f"unmute {B.fp}"); pb = [l for l in A.cmd("peers") if l.startswith("PEER " + B.fp)]
check("mute survives restart, unmute clears it, trust unchanged", okm and muted_after_restart and bool(pb) and "\tmuted" not in pb[0], (pa[:1], pb[:1]))
# 7. 诊断 snapshot: sockets, relay, and the peer's link state
dg = [l for l in A.cmd("diag") if l.startswith("DIAG ")]
keys = {l.split("\t")[0][5:] for l in dg}
peer = [l for l in dg if l.startswith("DIAG peer\t" + B.fp)]
check("diag reports listen/discovery/relay and per-peer state", {"version", "tcp_listen", "udp_discovery", "relay", "outbox"} <= keys and bool(peer) and "online" in peer[0] and "muted=0" in peer[0], (sorted(keys), peer[:1]))
want = {"王工": "王", "Alice Chen": "AC", "CI 测试群": "C", "bob": "B", "Bob Miller Jr": "BM", "李 Bob": "李", "  ": "?"}
got = {k: next((l.split()[2] if len(l.split()) > 2 else "") for l in A.cmd(f"initials {k}") if l.startswith("OK initials")) for k in want if k.strip()}
check("avatar initials rule (CJK -> 1 char, Latin -> up to 2, never mixed)", all(got[k] == v for k, v in want.items() if k.strip()), got)
A.quit(); B.quit()
sys.exit(1 if fails else 0)
