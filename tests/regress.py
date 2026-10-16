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
        n.fp = n.cmd("id", 60)[-1].split()[2]
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
# the 'done' event can precede the receiver's message record by a moment; wait until it is in history before restarting
for _ in range(50):
    if any(name in l for l in A.cmd(f"history {B.fp}") if "[file]" in l): break
    time.sleep(0.2)
A.quit(); B.quit()
A = Node("RA", base); B = Node("RB", base + 2)
la = [l for l in A.cmd(f"history {B.fp}") if "[file]" in l]; lb = [l for l in B.cmd(f"history {A.fp}") if "[file]" in l]
check("file message name survives restart (receiver + sender)", okrx and any(name in l for l in la) and any(name in l for l in lb), (la[:1], lb[:1]))
A.quit(); B.quit()
sys.exit(1 if fails else 0)
