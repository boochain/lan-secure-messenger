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
Relay mode (--relay --relay-bin PATH): an LSM relay-server runs on the bridge (10.78.0.254); direct traffic
between the two namespaces is blackholed so every pair must work THROUGH THE SERVER: online via relay, PIN pairing,
msg both ways + receipt, file both ways (sha256), file transfer interrupted by a server restart then retried (sha256), group create + group msg,
relay->direct and direct->relay switching, server restart, network drop (eth0 down/up), wrong passphrase.
Usage: sudo python3 tests/matrix.py --rust-bin ... --native-cli ... --wine-exe ... [--kinds rust,native,wine]
Writes results JSON to <work>/matrix.json and a table to stdout. Exit 1 on any failure.
"""
import argparse, hashlib, itertools, json, os, queue, re, shutil, subprocess, sys, threading, time
ap = argparse.ArgumentParser()
ap.add_argument("--rust-bin"); ap.add_argument("--native-cli"); ap.add_argument("--wine-exe")
ap.add_argument("--linux-cli"); ap.add_argument("--android-cmd")
ap.add_argument("--kinds", default="rust,native,wine,linux,android")
ap.add_argument("--work", default="/tmp/lsm-matrix")
ap.add_argument("--relay", action="store_true", help="run every pair through the relay server")
ap.add_argument("--relay-bin", help="relay-server binary (relay mode)")
ap.add_argument("--pairs", help="only these pairs, e.g. rust-wine,native-linux")
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
RESUME_SIZE = 120_000_000
import glob
def resume_check(X, Y, interrupt, tag, max_latency=None):
    """send a big file X->Y, interrupt the link at ~40%, expect an automatic resume: sha256 matches and the
    sender re-sent < 70% of the file (resumed_from >= 30%). -> (ok, detail)"""
    name = f"resume_{tag}.bin"; src = f"{W}/{name}"
    with open(src, "wb") as f:
        for _ in range(RESUME_SIZE // 4_000_000): f.write(os.urandom(4_000_000))
    want = sha(src); X.sendfile(Y.fp, src)
    import glob
    def part_bytes():
        # poll the receiver's .part file directly (C<->C on a veth is too fast for CLI polling)
        ps = glob.glob(f"{Y.dl}/.{name}*.part") if getattr(Y, "kind", "") != "android" else []
        if ps: n = os.path.getsize(ps[0]); return n >= 0.4 * RESUME_SIZE and ("in", n)
        x = Y.xfer(name, "in"); return x and x[1] >= 0.4 * RESUME_SIZE and x
    hit = wait(part_bytes, 120, 0.002)
    if not hit: return False, "never reached 40%"
    interrupt(); ti = time.time(); cut = hit[1]
    def progressed():
        if os.path.exists(Y.dlpath(name)): return True
        ps = glob.glob(f"{Y.dl}/.{name}*.part") if getattr(Y, "kind", "") != "android" else []
        if ps: return os.path.getsize(ps[0]) > cut + 2_000_000
        x = Y.xfer(name, "in"); return x and x[1] > cut + 2_000_000
    lat = wait(progressed, 120, 0.05) and time.time() - ti
    got = wait(lambda: os.path.exists(Y.dlpath(name)) and os.path.getsize(Y.dlpath(name)) == RESUME_SIZE and sha(Y.dlpath(name)) == want, 240, 1)
    xs = X.xfer(name, "out") or ("?", 0, RESUME_SIZE, 0)
    resent = (RESUME_SIZE - xs[3]) / RESUME_SIZE
    lat_ok = max_latency is None or (lat is not None and lat is not False and lat < max_latency)
    return bool(got) and resent < 0.7 and lat_ok, (f"resume after {lat:.1f}s " if lat else "never resumed ") + f"cut at {hit[1] * 100 // RESUME_SIZE}% resumed_from={xs[3] * 100 // RESUME_SIZE}% resent={resent:.0%} sha={'ok' if got else 'FAIL'}"
def switch_check(X, Y, switch, want_path, tag):
    """path switch MID-TRANSFER: send a big file X->Y over a throttled link (tbf 60 Mbit/s on both sides, so the
    transfer takes ~16 s), switch the path at ~25%, require that both sides report the new path while the file is
    still incomplete, then that it completes with the right sha256 and resumed instead of restarting. -> (ok, detail)"""
    for ns in ("mxA", "mxB"): sh(f"ip netns exec {ns} tc qdisc replace dev eth0 root tbf rate 60mbit burst 64kb latency 400ms", False)
    try:
        name = f"switch_{tag}.bin"; src = f"{W}/{name}"
        with open(src, "wb") as f:
            for _ in range(RESUME_SIZE // 4_000_000): f.write(os.urandom(4_000_000))
        want = sha(src); X.sendfile(Y.fp, src)
        def got_bytes():
            ps = glob.glob(f"{Y.dl}/.{name}*.part")
            if ps: return os.path.getsize(ps[0])
            x = Y.xfer(name, "in"); return x[1] if x else 0
        if not wait(lambda: got_bytes() >= 0.25 * RESUME_SIZE, 120, 0.05): return False, "never reached 25%"
        cut = got_bytes(); switch(); t0 = time.time()
        switched = wait(lambda: X.via(Y.fp) == want_path and Y.via(X.fp) == want_path, 90, 0.2)
        at = got_bytes(); mid = bool(switched) and not os.path.exists(Y.dlpath(name)) and at < RESUME_SIZE
        dt = time.time() - t0
        ok = wait(lambda: os.path.exists(Y.dlpath(name)) and os.path.getsize(Y.dlpath(name)) == RESUME_SIZE and sha(Y.dlpath(name)) == want, 240, 1)
        xs = X.xfer(name, "out") or ("?", 0, RESUME_SIZE, 0); xi = Y.xfer(name, "in") or ("?",)
        det = (f"switch to {want_path} after {dt:.1f}s at {cut * 100 // RESUME_SIZE}%->{at * 100 // RESUME_SIZE}% (mid-transfer={mid}) "
               f"resumed_from={xs[3] * 100 // RESUME_SIZE}% sha={'ok' if ok else 'BAD/missing'} state={xi[0]}")
        return bool(ok) and mid, det
    finally:
        for ns in ("mxA", "mxB"): sh(f"ip netns exec {ns} tc qdisc del dev eth0 root", False)
details = {}

def net_up():
    net_down(); sh("ip netns add mxBR"); sh("ip netns exec mxBR ip link add br0 type bridge && ip netns exec mxBR ip link set br0 up")
    for i, ns in enumerate(["mxA", "mxB"], 1):
        sh(f"ip netns add {ns}"); sh(f"ip netns exec mxBR ip link add v{ns} type veth peer name eth0 netns {ns}")
        sh(f"ip netns exec mxBR ip link set v{ns} master br0 up")
        sh(f"ip netns exec {ns} ip addr add 10.78.0.{i}/24 brd 10.78.0.255 dev eth0 && ip netns exec {ns} ip link set eth0 up && ip netns exec {ns} ip link set lo up")
RHOST, RPORT, RPASS = "10.78.0.254", 29443, "matrix-relay-pass"
def relay_up(host=False):
    # host=True: android pairs; the relay runs in the root namespace so the emulator reaches it at 10.0.2.2
    if not host: sh("ip netns exec mxBR ip addr add 10.78.0.254/24 dev br0", False)
    return subprocess.Popen(([] if host else ["ip", "netns", "exec", "mxBR"]) + [args.relay_bin, "--port", str(RPORT), "--password", RPASS],
                            stdout=open(f"{W}/relay-server.log", "a"), stderr=subprocess.STDOUT)
def relay_down(p):
    if p:
        p.terminate()
        try: p.wait(5)
        except Exception: p.kill()
def block_direct(on):
    op = "add" if on else "del"
    sh(f"ip netns exec mxA ip route {op} blackhole 10.78.0.2/32", False); sh(f"ip netns exec mxB ip route {op} blackhole 10.78.0.1/32", False)
def net_down():
    for ns in ["mxA", "mxB", "mxBR"]:
        sh(f"ip netns pids {ns} 2>/dev/null | xargs -r kill -9", False); sh(f"ip netns del {ns}", False)

class Rust:
    def __init__(s, tag, ns, port=0):
        s.dir = f"{W}/{tag}"; s.sock = f"{W}/{tag}.sock"; os.makedirs(s.dir, exist_ok=True); s.dl = f"{s.dir}/dl"; s.port = port
        cmd = (["ip", "netns", "exec", ns] if ns else []) + [f"{args.rust_bin}/lan-daemon", "--data-dir", s.dir, "--ipc", s.sock]
        extra = [f"listen_port={port}", f"discovery_port={port + 1}", "discovery_enabled=false"] if port else []
        for x in [f'display_name="{tag.split("/")[-1]}"', "announce_interval_secs=1", f'download_dir="{s.dl}"'] + extra: cmd += ["--set", x]
        s._cmd, s._log = cmd, f"{W}/{tag}.log"; s._spawn()
    def _spawn(s):
        s.p = subprocess.Popen(s._cmd, stdout=open(s._log, "a"), stderr=subprocess.STDOUT, env=dict(os.environ, LSM_NO_DIALOG="1"))
        wait(lambda: os.path.exists(s.sock), 15); s.fp = wait(lambda: s.j("status")["me"]["fingerprint"], 15)
    def restart_hard(s):
        """kill -9 mid-transfer (no shutdown), then start again on the same data dir"""
        s.p.kill(); s.p.wait()
        if os.path.exists(s.sock): os.remove(s.sock)
        time.sleep(1); s._spawn()
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
    def peer(s, fp): return next((p for p in s.j("status")["peers"] if p["fingerprint"] == fp), {})
    def via(s, fp):
        p = s.peer(fp); return ("relay" if p.get("via_relay") else "direct") if p.get("online") else None
    def trusted(s, fp): return s.peer(fp).get("trust") == "trusted"
    def relay(s, host, port, pw): s.m("relay", "on", host, str(port), pw)
    def relay_state(s): return s.j("relay", "status")["state"]
    def xfer(s, name, direction):
        """-> (state, transferred, size, resumed_from) of the newest transfer with that file name"""
        for t in reversed(s.j("transfers")):
            if t["name"] == name and t["direction"] == direction: return (t["state"], t["transferred"], t["size"], t.get("resumed_from", 0))
    def pin_offer(s): return s.j("pin-offer")["pin"]
    def pin_pair(s, fp, pin): s.m("pin-pair", fp, pin)
    def group_create(s, name, fp): s.m("group-create", name, fp); return s.group_id(name)
    def group_id(s, name): return next((g["id"] for g in s.j("groups") if g["name"] == name), None)
    def send_group(s, gid, t): s.m("send-group", gid, t)
    def has_group(s, gid, t): return any(m.get("body") == t for m in s.j("history", "g:" + gid))
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
        s._cmd, s._env = cmd, env
        os.makedirs(os.path.dirname(f"{W}/{tag}"), exist_ok=True); s.log = open(f"{W}/{tag}.log", "w", encoding="utf-8")
        s._spawn()
    def _spawn(s):
        s.p = subprocess.Popen(s._cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=s._env)
        threading.Thread(target=s._rd, daemon=True).start()
        r = s.cmd("id", 60); s.fp = re.search(r"OK id (\S+)", r[-1]).group(1)
    def restart_hard(s):
        """kill -9 mid-transfer (no quit), then start again on the same data dir"""
        s.p.kill(); s.p.wait()
        if s.kind == "wine": sh(f"WINEPREFIX={s.pfx} wineserver -k", False)
        if s.kind == "android": subprocess.run([ADB, "shell", f"pkill -9 -f 'lanmsg-cli --data {s.dev}'"], capture_output=True)
        time.sleep(1); s.lines.clear(); s._spawn()
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
    def via(s, fp):
        for l in s.cmd("peers"):
            if l.startswith(f"PEER {fp}\tonline"): return "relay" if "via=relay" in l else "direct"
        return None
    def trusted(s, fp): return any(l.startswith(f"PEER {fp}\t") and "\ttrusted\t" in l for l in s.cmd("peers"))
    def relay(s, host, port, pw):
        if s.kind == "android" and host.startswith("10.78."): host = "10.0.2.2"
        s.cmd(f"relay {host} {port} {pw}")
    def relay_state(s): return s.cmd("relay status")[-1].split()[2]
    def xfer(s, name, direction):
        r = None
        for l in s.cmd("transfers"):
            f = l.split("\t")
            if l.startswith("XFER ") and len(f) >= 5 and f[1] == direction and f[4] == name:
                a, b = f[3].split("/"); m = re.search(r"resumed=(\d+)", l)
                r = (f[2], int(a), int(b), int(m.group(1)) if m else 0)
        return r
    def pin_offer(s): return re.search(r"OK pin (\d+)", s.cmd("pin-offer")[-1]).group(1)
    def pin_pair(s, fp, pin): s.cmd(f"pin-pair {fp} {pin}")
    def group_create(s, name, fp): s.cmd(f"group-create {name} {fp}"); return s.group_id(name)
    def group_id(s, name):
        for l in s.cmd("groups"):
            if l.startswith("GROUP ") and l.split("\t")[1] == name: return l.split("\t")[0][6:]
    def send_group(s, gid, t): s.cmd(f"sendgroup {gid} {t}")
    def has_group(s, gid, t): return any(l.startswith("MSG") and l.endswith("\t" + t) for l in s.cmd(f"history group:{gid}"))
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
    if args.pairs and f"{a}-{b}" not in args.pairs.split(",") and f"{b}-{a}" not in args.pairs.split(","): continue
    if a == b == "android":
        results.append({"pair": pair, "status": "SKIP", "why": "needs two Android devices"}); print(f"[SKIP] {pair} (needs two devices)"); continue
    if not (avail[a] and avail[b]):
        results.append({"pair": pair, "status": "SKIP", "why": "implementation not provided"}); print(f"[SKIP] {pair}"); continue
    checks = {}; net_up(); A = B = None; t0 = time.time(); srv = None
    try:
        tg = f"{a}-{b}"
        if args.relay:
            andr = "android" in (a, b)
            if a == b == "android": raise RuntimeError("android<->android needs two devices")
            # android: emulator behind NAT, relay in the root namespace (guest -> 10.0.2.2, host peer -> 127.0.0.1);
            # no LAN path exists, so the relay is the only route (the direct-switch / netns-drop checks do not apply)
            srv = relay_up(andr); time.sleep(0.5)
            if not andr: block_direct(True)
            SRV = [srv]
            def restart_relay():
                relay_down(SRV[0]); time.sleep(1); SRV[0] = relay_up(andr)
            if andr:
                other = b if a == "android" else a
                A = mk("android", f"{tg}/androidA", None); B = mk(other, f"{tg}/{other}B", None, 47600 + 4 * kinds.index(other))
                B.relay("127.0.0.1", RPORT, RPASS); A.relay(RHOST, RPORT, RPASS)
            else:
                A = mk(a, f"{tg}/{a}A", "mxA"); B = mk(b, f"{tg}/{b}B", "mxB")
                A.relay(RHOST, RPORT, RPASS); B.relay(RHOST, RPORT, RPASS)
            checks["online via server"] = bool(wait(lambda: A.via(B.fp) == "relay" and B.via(A.fp) == "relay", 40))
            pin = A.pin_offer(); B.pin_pair(A.fp, pin)
            checks["PIN pairing"] = bool(wait(lambda: A.trusted(B.fp) and B.trusted(A.fp), 30))
            A.trust(B.fp); B.trust(A.fp)
            for X, Y, d in ((A, B, "A->B"), (B, A, "B->A")):
                t = f"中转消息 {d} {pair} ✓"; mid = X.send(Y.fp, t)
                checks[f"msg+receipt {d}"] = bool(wait(lambda: Y.has(X.fp, t), 20) and wait(lambda: X.delivered(Y.fp, mid), 20))
                name = f"rx_{d.replace('->','_')}.bin"; src = f"{W}/{name}"; open(src, "wb").write(os.urandom(1_500_000 + len(d)))
                X.sendfile(Y.fp, src)
                checks[f"file {d}"] = bool(wait(lambda: os.path.exists(Y.dlpath(name)) and os.path.getsize(Y.dlpath(name)) == os.path.getsize(src) and sha(Y.dlpath(name)) == sha(src), 90))
            gid = A.group_create(f"mxg{abs(hash(pair)) % 10000}", B.fp)
            ok = bool(gid and wait(lambda: B.group_id(f"mxg{abs(hash(pair)) % 10000}"), 20))
            if ok:
                gt = f"群消息 {pair}"; A.send_group(gid, gt); ok = bool(wait(lambda: B.has_group(gid, gt), 20))
            checks["group create+msg"] = ok
            # true resume: kill the relay server at ~40%, restart it, the transfer must continue
            ok, det = resume_check(A, B, lambda: restart_relay(), f"relay_{tg}")
            checks["resume (server killed at 40%)"] = ok; details[f"{pair} relay resume"] = det; print("   ", det, flush=True)
            ok, det = resume_check(A, B, lambda: B.restart_hard(), f"relayrst_{tg}", 10)
            checks["resume (receiver killed+restarted at 40%, <10s)"] = ok; details[f"{pair} relay receiver restart"] = det; print("   ", det, flush=True)
            ok, det = resume_check(A, B, lambda: A.restart_hard(), f"relaysrst_{tg}", 10)
            checks["resume (sender killed+restarted at 40%, <10s)"] = ok; details[f"{pair} relay sender restart"] = det; print("   ", det, flush=True)
            srv = SRV[0]
            if not andr:  # LAN-path checks: the emulator has no LAN path to the host peers
                # path switch MID-TRANSFER, both directions: the transfer pauses and resumes on the new link
                ok, det = switch_check(A, B, lambda: block_direct(False), "direct", f"r2d_{tg}")
                checks["transfer survives server->direct mid-transfer"] = ok; details[f"{pair} switch r->d"] = det; print("   ", det, flush=True)
                ok, det = switch_check(B, A, lambda: block_direct(True), "relay", f"d2r_{tg}")
                checks["transfer survives direct->server mid-transfer"] = ok; details[f"{pair} switch d->r"] = det; print("   ", det, flush=True)
                # relay -> direct
                block_direct(False)
                checks["switch server->direct"] = bool(wait(lambda: A.via(B.fp) == "direct" and B.via(A.fp) == "direct", 60))
                t = f"直连消息 {pair}"; A.send(B.fp, t); checks["msg after ->direct"] = bool(wait(lambda: B.has(A.fp, t), 20))
                # direct -> relay
                block_direct(True)
                checks["switch direct->server"] = bool(wait(lambda: A.via(B.fp) == "relay" and B.via(A.fp) == "relay", 90))
                t = f"回到中转 {pair}"; B.send(A.fp, t); checks["msg after ->server"] = bool(wait(lambda: A.has(B.fp, t), 20))
            # server restart
            relay_down(srv); time.sleep(1); srv = relay_up(andr)
            checks["server restart"] = bool(wait(lambda: A.via(B.fp) == "relay" and B.via(A.fp) == "relay", 90))
            t = f"重启后 {pair}"; A.send(B.fp, t); checks["msg after restart"] = bool(wait(lambda: B.has(A.fp, t), 20))
            if not andr:
                # network drop on A
                sh("ip netns exec mxA ip link set eth0 down"); time.sleep(8); sh("ip netns exec mxA ip link set eth0 up")
                sh("ip netns exec mxA ip route add blackhole 10.78.0.2/32", False)
                checks["network drop recovery"] = bool(wait(lambda: A.via(B.fp) == "relay" and B.via(A.fp) == "relay", 120))
                t = f"断网恢复 {pair}"; B.send(A.fp, t); checks["msg after drop"] = bool(wait(lambda: A.has(B.fp, t), 20))
            # wrong passphrase
            A.relay(RHOST, RPORT, "wrong-pass")
            checks["wrong passphrase rejected"] = bool(wait(lambda: A.relay_state() == "auth_failed", 30)) and bool(wait(lambda: B.via(A.fp) is None, 60))
            A.relay(RHOST, RPORT, RPASS)
            checks["recover after fixing passphrase"] = bool(wait(lambda: A.via(B.fp) == "relay", 60))
        elif "android" in (a, b):
            # emulator NAT: no broadcast across it, so connect by IP (the host is 10.0.2.2 from the guest)
            if a == "android" and b == "android": raise RuntimeError("android<->android needs two devices")
            other = b if a == "android" else a; hp = 47600 + 4 * kinds.index(other)
            A = mk("android", f"{tg}/androidA", None); B = mk(other, f"{tg}/{other}B", None, hp)
            A.cmd(f"add 10.0.2.2:{hp}")
            checks["add-by-IP (NAT)"] = bool(wait(lambda: A.online(B.fp) and B.online(A.fp), 40))
        else:
            A = mk(a, f"{tg}/{a}A", "mxA"); B = mk(b, f"{tg}/{b}B", "mxB")
            checks["discovery"] = bool(wait(lambda: A.online(B.fp) and B.online(A.fp), 40))
        if not args.relay: A.trust(B.fp); B.trust(A.fp)
        for X, Y, d in (() if args.relay else ((A, B, "A->B"), (B, A, "B->A"))):
            t = f"矩阵消息 {d} {pair} ✓"; mid = X.send(Y.fp, t)
            checks[f"msg {d}"] = bool(wait(lambda: Y.has(X.fp, t), 15) and wait(lambda: X.delivered(Y.fp, mid), 15))
            name = f"mx_{d.replace('->','_')}.bin"; src = f"{W}/{name}"; open(src, "wb").write(os.urandom(1_500_000 + len(d)))
            X.sendfile(Y.fp, src)
            checks[f"file {d}"] = bool(wait(lambda: os.path.exists(Y.dlpath(name)) and os.path.getsize(Y.dlpath(name)) == os.path.getsize(src) and sha(Y.dlpath(name)) == sha(src), 60))
        if not args.relay and "android" not in (a, b):
            # true resume on the LAN: abort the TCP link at ~40% (ss -K), peers redial, transfer continues
            ok, det = resume_check(A, B, lambda: sh("ip netns exec mxA ss -K dst 10.78.0.2", False), f"lan_{tg}")
            checks["resume (LAN link cut at 40%)"] = ok; details[f"{pair} LAN resume"] = det; print("   ", det, flush=True)
            ok, det = resume_check(A, B, lambda: B.restart_hard(), f"lanrst_{tg}", 10)
            checks["resume (receiver killed+restarted at 40%, <10s)"] = ok; details[f"{pair} LAN receiver restart"] = det; print("   ", det, flush=True)
            ok, det = resume_check(A, B, lambda: A.restart_hard(), f"lansrst_{tg}", 10)
            checks["resume (sender killed+restarted at 40%, <10s)"] = ok; details[f"{pair} LAN sender restart"] = det; print("   ", det, flush=True)
    except Exception as e:
        checks["harness"] = False; print("  exception:", repr(e)[:200])
    finally:
        for n in (A, B):
            try: n and n.stop()
            except Exception: pass
        relay_down(srv); net_down()
    st = "PASS" if checks and all(checks.values()) else "FAIL"
    results.append({"pair": pair, "status": st, "checks": checks, "secs": round(time.time() - t0, 1)})
    print(f"[{st}] {pair} " + " ".join(f"{k}={'ok' if v else 'FAIL'}" for k, v in checks.items()), flush=True)
json.dump(details, open(f"{W}/resume-details.json", "w"), ensure_ascii=False, indent=1)
if True:
    names = []
    for r in results:
        for k in r.get("checks", {}):
            if k not in names: names.append(k)
    print("\n| pair | " + " | ".join(names) + " |\n|" + "---|" * (len(names) + 1))
    for r in results:
        c = r.get("checks", {}); print(f"| {r['pair']} | " + " | ".join("✅" if c.get(n) else ("❌" if n in c else "–") for n in names) + " |")
json.dump(results, open(f"{W}/matrix.json", "w"), ensure_ascii=False, indent=1)
print("\nMATRIX: %d pass, %d fail, %d skip" % tuple(sum(r["status"] == s for r in results) for s in ("PASS", "FAIL", "SKIP")))
sys.exit(1 if any(r["status"] == "FAIL" for r in results) else 0)
