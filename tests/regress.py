#!/usr/bin/env python3
"""Regression tests for XP-edition bugs. Usage: python3 tests/regress.py <lanmsg-cli[.exe]>
Runs the exe under wine when it ends in .exe."""
import os, subprocess, sys, tempfile, time, threading, queue
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
sys.exit(1 if fails else 0)
