#!/usr/bin/env python3
"""Cross-platform interop matrix. Every pair of implementations (including same-kind pairs) is put on a
private Linux bridge in two network namespaces and must: discover each other (UDP broadcast), trust,
exchange a text message both ways with an Ack receipt, and send a file both ways with sha256 match.

Kinds:
  rust    v0.5 lan-daemon + lan-msg (Windows v0.5 code base, Linux build)   --rust-bin DIR
  native  portable C core, Linux build of lanmsg-cli                      --native-cli PATH
  wine    XP edition x86 lanmsg-cli.exe under wine                         --wine-exe PATH
  linux   Linux edition CLI (same C core, packaged build)                  --linux-cli PATH
  android slot: --android-cmd "/data/local/tmp/lanmsg-cli" (Android build of the core on an adb device/emulator)  (skipped when absent)
Usage: sudo python3 tests/matrix.py --rust-bin ... --native-cli ... --wine-exe ... [--kinds rust,native,wine]
Writes results JSON to <work>/matrix.json and a table to stdout. Exit 1 on any failure.
"""
import argparse, hashlib, itertools, json, os, queue, re, shutil, subprocess, sys, threading, time
ap = argparse.ArgumentParser()
ap.add_argument("--rust-bin"); ap.add_argument("--native-cli"); ap.add_argument("--wine-exe")
ap.add_argument("--linux-cli"); ap.add_argument("--android-cmd")
ap.add_argument("--kinds", default="rust,native,wine,linux,android")
ap.add_argument("--work", default="/tmp/lsm-matrix")
args = ap.parse_args()
W = args.work
def sh(c, check=True):
    r = subprocess.run(c, shell=True, capture_output=True, text=True)
    if check and r.returncode: raise RuntimeError(c + r.stderr)
    return r.stdout
def wait(pred, t, step=0.3):
    end = time.time() + t
    while time.time() < end:
        try:
            v = pred()
            if v: return v
        except Exception: pass
        time.sleep(step)
    return None
def sha(p): return hashlib.sha256(open(p, "rb").read()).hexdigest()

def net_up():
    net_down(); sh("ip netns add mxBR"); sh("ip netns exec mxBR ip link add br0 type bridge && ip netns exec mxBR ip link set br0 up")
    for i, ns in enumerate(["mxA", "mxB"], 1):
        sh(f"ip netns add {ns}"); sh(f"ip netns exec mxBR ip link add v{ns} type veth peer name eth0 netns {ns}")
        sh(f"ip netns exec mxBR ip link set v{ns} master br0 up")
        sh(f"ip netns exec {ns} ip addr add 10.78.0.{i}/24 brd 10.78.0.255 dev eth0 && ip netns exec {ns} ip link set eth0 up && ip netns exec {ns} ip link set lo up")
def net_down():
    for ns in ["mxA", "mxB", "mxBR"]:
        sh(f"ip netns pids {ns} 2>/dev/null | xargs -r kill -9", False); sh(f"ip netns del {ns}", False)

class Rust:
    def __init__(s, tag, ns, port=0):
        s.dir = f"{W}/{tag}"; s.sock = f"{W}/{tag}.sock"; os.makedirs(s.dir, exist_ok=True); s.dl = f"{s.dir}/dl"; s.port = port
        cmd = (["ip", "netns", "exec", ns] if ns else []) + [f"{args.rust_bin}/lan-daemon", "--data-dir", s.dir, "--ipc", s.sock]
        extra = [f"listen_port={port}", f"discovery_port={port + 1}", "discovery_enabled=false"] if port else []
        for x in [f'display_name="{tag.split("/")[-1]}"', "announce_interval_secs=1", f'download_dir="{s.dl}"'] + extra: cmd += ["--set", x]
        s.p = subprocess.Popen(cmd, stdout=open(f"{W}/{tag}.log", "w"), stderr=subprocess.STDOUT, env=dict(os.environ, LSM_NO_DIALOG="1"))
        wait(lambda: os.path.exists(s.sock), 15); s.fp = wait(lambda: s.j("status")["me"]["fingerprint"], 15)
    def m(s, *a): return subprocess.run([f"{args.rust_bin}/lan-msg", "--ipc", s.sock] + list(a), capture_output=True, text=True, timeout=60).stdout
    def j(s, *a): return json.loads(s.m("--json", *a))
    def online(s, fp): return any(p["fingerprint"] == fp and p.get("online") for p in s.j("status")["peers"])
    def trust(s, fp): s.m("trust", fp)
    def send(s, fp, t):
        s.m("send", fp, t); return wait(lambda: [m["id"] for m in s.j("history", fp) if m["body"] == t], 10)[0]
    def delivered(s, fp, mid): return any(m["id"] == mid and m["status"] in ("delivered", "read") for m in s.j("history", fp))
    def has(s, fp, t): return any(m.get("body") == t for m in s.j("history", fp))
    def sendfile(s, fp, path): s.m("send-file", fp, path)
    def dlpath(s, name): return f"{s.dl}/{name}"
    def stop(s):
        s.m("shutdown")
        try: s.p.wait(5)
        except Exception: s.p.kill()

class CCli:
    """native / wine / linux / android: all speak the same lanmsg-cli line protocol on stdin/stdout"""
    def __init__(s, tag, ns, kind, port=0):
        s.kind, s.lines, s.q, s.port = kind, [], queue.Queue(), port; env = dict(os.environ)
        pre = ["ip", "netns", "exec", ns] if ns else []
        if kind == "wine":
            s.pfx = f"{W}/{tag}-wpfx"; os.makedirs(os.path.dirname(s.pfx), exist_ok=True); base = os.environ.get("BASE_WINEPREFIX", "/root/.wine-lsm")
            if not os.path.exists(base):
                sh(f"WINEPREFIX={base} WINEARCH=win32 WINEDEBUG=-all wineboot -i; WINEPREFIX={base} wineserver -w", False)
            shutil.copytree(base, s.pfx, symlinks=True)
            env.update(WINEPREFIX=s.pfx, WINEDEBUG="-all", WINEARCH="win32")
            cmd = pre + ["wine", args.wine_exe, "--data", "C:\\lsm"]; s.dl = f"{s.pfx}/drive_c/lsm/downloads"
        elif kind == "android":
            # the Android build of the same core, running on the emulator; host peers are reached at 10.0.2.2
            s.dev = f"/data/local/tmp/mx/{tag}"; s.dl = s.dev + "/downloads"
            cmd = [ADB, "shell", "-T", f"{args.android_cmd} --data {s.dev} --no-discovery --port 41235"]
        else:
            exe = args.native_cli if kind == "native" else args.linux_cli
            cmd = pre + [exe, "--data", f"{W}/{tag}"]; s.dl = f"{W}/{tag}/downloads"
        cmd += ["--name", tag.split("/")[-1], "--announce-ms", "1000"]
        if port: cmd += ["--port", str(port), "--disco-port", str(port + 1), "--no-discovery"]
        s.p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
        os.makedirs(os.path.dirname(f"{W}/{tag}"), exist_ok=True); s.log = open(f"{W}/{tag}.log", "w", encoding="utf-8")
        threading.Thread(target=s._rd, daemon=True).start()
        r = s.cmd("id", 60); s.fp = re.search(r"OK id (\S+)", r[-1]).group(1)
    def _rd(s):
        for raw in s.p.stdout:
            l = raw.decode("utf-8", "replace").rstrip("\r\n"); s.log.write(l + "\n"); s.log.flush(); s.lines.append(l); s.q.put(l)
    def cmd(s, c, t=30):
        while not s.q.empty(): s.q.get_nowait()
        s.p.stdin.write((c + "\n").encode()); s.p.stdin.flush(); out = []; end = time.time() + t
        while time.time() < end:
            try: l = s.q.get(timeout=0.5)
            except queue.Empty: continue
            out.append(l)
            if l.startswith(("OK", "ERR")): return out
        raise TimeoutError(c)
    def online(s, fp): return any(l.startswith(f"PEER {fp}\tonline") for l in s.cmd("peers"))
    def trust(s, fp): s.cmd(f"trust {fp}")
    def send(s, fp, t): return s.cmd(f"send {fp} {t}")[-1].split()[-1]
    def delivered(s, fp, mid): return any(re.match(r"^EV msg_update\t%s\t.*\t(delivered|read)" % mid, l) for l in s.lines)
    def has(s, fp, t): return any(l.startswith("MSG") and l.endswith("\t" + t) for l in s.cmd(f"history {fp}"))
    def sendfile(s, fp, path):
        if s.kind == "wine":
            shutil.copy(path, f"{s.pfx}/drive_c/{os.path.basename(path)}"); path = "C:\\" + os.path.basename(path)
        elif s.kind == "android":
            subprocess.run([ADB, "push", path, "/data/local/tmp/" + os.path.basename(path)], capture_output=True); path = "/data/local/tmp/" + os.path.basename(path)
        s.cmd(f"sendfile {fp} {path}")
    def dlpath(s, name):
        if s.kind != "android": return f"{s.dl}/{name}"
        local = f"{W}/pulled-{name}"; subprocess.run([ADB, "pull", f"{s.dl}/{name}", local], capture_output=True); return local
    def stop(s):
        try: s.cmd("quit", 15)
        except Exception: pass
        try: s.p.wait(10)
        except Exception: s.p.kill()

avail = {"rust": bool(args.rust_bin), "native": bool(args.native_cli), "wine": bool(args.wine_exe),
         "linux": bool(args.linux_cli), "android": bool(args.android_cmd)}
kinds = [k for k in args.kinds.split(",") if k]
ADB = os.environ.get("ADB", "adb")
def mk(kind, tag, ns, port=0): return Rust(tag, ns, port) if kind == "rust" else CCli(tag, ns, kind, port)
if os.path.exists(W): shutil.rmtree(W)
os.makedirs(W); results = []
for a, b in itertools.combinations_with_replacement(kinds, 2):
    pair = f"{a}<->{b}"
    if a == b == "android":
        results.append({"pair": pair, "status": "SKIP", "why": "needs two Android devices"}); print(f"[SKIP] {pair} (needs two devices)"); continue
    if not (avail[a] and avail[b]):
        results.append({"pair": pair, "status": "SKIP", "why": "implementation not provided"}); print(f"[SKIP] {pair}"); continue
    checks = {}; net_up(); A = B = None; t0 = time.time()
    try:
        tg = f"{a}-{b}"
        if "android" in (a, b):
            # emulator NAT: no broadcast across it, so connect by IP (the host is 10.0.2.2 from the guest)
            if a == "android" and b == "android": raise RuntimeError("android<->android needs two devices")
            other = b if a == "android" else a; hp = 47600 + 4 * kinds.index(other)
            A = mk("android", f"{tg}/androidA", None); B = mk(other, f"{tg}/{other}B", None, hp)
            A.cmd(f"add 10.0.2.2:{hp}")
            checks["add-by-IP (NAT)"] = bool(wait(lambda: A.online(B.fp) and B.online(A.fp), 40))
        else:
            A = mk(a, f"{tg}/{a}A", "mxA"); B = mk(b, f"{tg}/{b}B", "mxB")
            checks["discovery"] = bool(wait(lambda: A.online(B.fp) and B.online(A.fp), 40))
        A.trust(B.fp); B.trust(A.fp)
        for X, Y, d in ((A, B, "A->B"), (B, A, "B->A")):
            t = f"矩阵消息 {d} {pair} ✓"; mid = X.send(Y.fp, t)
            checks[f"msg {d}"] = bool(wait(lambda: Y.has(X.fp, t), 15) and wait(lambda: X.delivered(Y.fp, mid), 15))
            name = f"mx_{d.replace('->','_')}.bin"; src = f"{W}/{name}"; open(src, "wb").write(os.urandom(1_500_000 + len(d)))
            X.sendfile(Y.fp, src)
            checks[f"file {d}"] = bool(wait(lambda: os.path.exists(Y.dlpath(name)) and os.path.getsize(Y.dlpath(name)) == os.path.getsize(src) and sha(Y.dlpath(name)) == sha(src), 60))
    except Exception as e:
        checks["harness"] = False; print("  exception:", repr(e)[:200])
    finally:
        for n in (A, B):
            try: n and n.stop()
            except Exception: pass
        net_down()
    st = "PASS" if checks and all(checks.values()) else "FAIL"
    results.append({"pair": pair, "status": st, "checks": checks, "secs": round(time.time() - t0, 1)})
    print(f"[{st}] {pair} " + " ".join(f"{k}={'ok' if v else 'FAIL'}" for k, v in checks.items()), flush=True)
json.dump(results, open(f"{W}/matrix.json", "w"), ensure_ascii=False, indent=1)
print("\nMATRIX: %d pass, %d fail, %d skip" % tuple(sum(r["status"] == s for r in results) for s in ("PASS", "FAIL", "SKIP")))
sys.exit(1 if any(r["status"] == "FAIL" for r in results) else 0)
