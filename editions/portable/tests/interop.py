#!/usr/bin/env python3
"""Interop test: XP edition (x86 lanmsg-cli.exe under wine) <-> real v0.5 lan-daemon (Rust).

Topology (needs root): Linux bridge br-lsm + three network namespaces so discovery uses real
subnet broadcast/multicast, not loopback tricks:
   lsmA 10.77.0.1  Rust lan-daemon "RustA"
   lsmB 10.77.0.2  XP edition under wine (separate WINEPREFIX + wineserver inside the netns)
   lsmC 10.77.0.3  Rust lan-daemon "RustC" (third group member for remove+rekey)
Usage: sudo python3 tests/interop.py --rust-bin <dir with lan-daemon, lan-msg> --exe build/x86/lanmsg-cli.exe
"""
import argparse, hashlib, json, os, queue, re, shutil, subprocess, sys, threading, time

ap = argparse.ArgumentParser()
ap.add_argument("--rust-bin", required=True)
ap.add_argument("--exe", required=True)
ap.add_argument("--work", default="/tmp/lsm-interop")
ap.add_argument("--p1", action="store_true", help="Rust side is >= v0.6.1 (CAP_EXT): run reply/react/edit/recall checks")
ap.add_argument("--native", action="store_true", help="run the Linux-native build of the XP core instead of wine")
args = ap.parse_args()
RB = os.path.abspath(args.rust_bin); EXE = os.path.abspath(args.exe); W = args.work
RESULTS = []
def sh(cmd, check=True, **kw):
    r = subprocess.run(cmd, shell=True, capture_output=True, text=True, **kw)
    if check and r.returncode: raise RuntimeError(f"{cmd}\n{r.stdout}\n{r.stderr}")
    return r.stdout
def ok(name, detail=""):
    RESULTS.append((name, "PASS", detail)); print(f"[PASS] {name} {detail}", flush=True)
def fail(name, detail=""):
    RESULTS.append((name, "FAIL", detail)); print(f"[FAIL] {name} {detail}", flush=True)
def wait(pred, timeout, step=0.3):
    end = time.time() + timeout
    while time.time() < end:
        try:
            v = pred()
            if v: return v
        except Exception: pass
        time.sleep(step)
    return None

# ---------------------------------------------------------------- network
def net_up():
    # The bridge lives in its own namespace (lsmBR): a fresh netns has no iptables/nft rules,
    # so br_netfilter on the host (docker) cannot drop our test traffic, and the host is untouched.
    net_down()
    sh("ip netns add lsmBR")
    sh("ip netns exec lsmBR ip link add br-lsm type bridge && ip netns exec lsmBR ip link set br-lsm up && ip netns exec lsmBR ip link set lo up")
    for i, ns in enumerate(["lsmA", "lsmB", "lsmC"], 1):
        sh(f"ip netns add {ns}")
        sh(f"ip netns exec lsmBR ip link add v{ns} type veth peer name eth0 netns {ns}")
        sh(f"ip netns exec lsmBR ip link set v{ns} master br-lsm up")
        sh(f"ip netns exec {ns} ip addr add 10.77.0.{i}/24 brd 10.77.0.255 dev eth0")
        sh(f"ip netns exec {ns} ip link set eth0 up")
        sh(f"ip netns exec {ns} ip link set lo up")
        sh(f"ip netns exec {ns} ip route add 224.0.0.0/4 dev eth0", check=False)
def net_down():
    for ns in ["lsmA", "lsmB", "lsmC", "lsmBR"]:
        sh(f"ip netns pids {ns} 2>/dev/null | xargs -r kill -9", check=False)
        sh(f"ip netns del {ns}", check=False)

# ---------------------------------------------------------------- Rust v0.5 daemon
class Rust:
    def __init__(self, tag, ns, extra=()):
        self.tag, self.ns = tag, ns
        self.dir = f"{W}/{tag}"; self.sock = f"{W}/{tag}.sock"
        os.makedirs(self.dir, exist_ok=True)
        sets = ["display_name=\"%s\"" % tag, "announce_interval_secs=1", f"download_dir=\"{self.dir}/dl\""] + list(extra)
        cmd = ["ip", "netns", "exec", ns, f"{RB}/lan-daemon", "--data-dir", self.dir, "--ipc", self.sock]
        for s in sets: cmd += ["--set", s]
        self.log = open(f"{W}/{tag}.log", "a")
        env = dict(os.environ, LSM_LOG="info", LSM_NO_DIALOG="1")
        self.p = subprocess.Popen(cmd, stdout=self.log, stderr=subprocess.STDOUT, env=env)
        wait(lambda: os.path.exists(self.sock), 15)
        self.fp = wait(lambda: self.j("status")["me"]["fingerprint"], 15)
        self.port = self.j("status")["me"]["listen_port"]
    def m(self, *a, check=True):
        r = subprocess.run([f"{RB}/lan-msg", "--ipc", self.sock] + list(a), capture_output=True, text=True, timeout=60)
        if check and r.returncode: raise RuntimeError(f"lan-msg {a}: {r.stderr}")
        return r.stdout
    def j(self, *a): return json.loads(self.m("--json", *a))
    def peer(self, fp):
        for p in self.j("status")["peers"]:
            if p["fingerprint"] == fp: return p
    def online(self, fp): p = self.peer(fp); return bool(p and p.get("online"))
    def stop(self):
        try: self.m("shutdown", check=False)
        except Exception: pass
        try: self.p.wait(5)
        except Exception: self.p.kill()

# ---------------------------------------------------------------- XP edition
class XP:
    def __init__(self, tag, ns, data, extra=()):
        self.tag, self.lines, self.q = tag, [], queue.Queue()
        self.log = open(f"{W}/{tag}.log", "a", encoding="utf-8")
        if args.native:
            cmd = ["ip", "netns", "exec", ns, EXE, "--data", data] + list(extra)
            env = dict(os.environ)
        else:
            cmd = ["ip", "netns", "exec", ns, "wine", EXE, "--data", data] + list(extra)
            env = dict(os.environ, WINEPREFIX=WPFX, WINEDEBUG="-all", WINEARCH="win32")
        self.p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
        threading.Thread(target=self._rd, daemon=True).start()
        r = self.cmd("id", 60)
        m = re.search(r"OK id (\S+) .* port=(\d+)", r[-1]); self.fp, self.port = m.group(1), int(m.group(2))
    def _rd(self):
        for raw in self.p.stdout:
            line = raw.decode("utf-8", "replace").rstrip("\r\n")
            self.log.write(line + "\n"); self.log.flush()
            self.lines.append(line); self.q.put(line)
    def cmd(self, c, timeout=30):
        while not self.q.empty(): self.q.get_nowait()
        self.p.stdin.write((c + "\n").encode("utf-8")); self.p.stdin.flush()
        out, end = [], time.time() + timeout
        while time.time() < end:
            try: l = self.q.get(timeout=0.5)
            except queue.Empty: continue
            out.append(l)
            if l.startswith("OK") or l.startswith("ERR"): return out
        raise TimeoutError(f"{self.tag}: no reply to {c}")
    def ev(self, pattern, timeout, since=0):
        rx = re.compile(pattern)
        def f():
            for l in self.lines[since:]:
                if rx.search(l): return l
        return wait(f, timeout, 0.2)
    def peers(self):
        return {l.split()[1]: l for l in self.cmd("peers") if l.startswith("PEER")}
    def online(self, fp): l = self.peers().get(fp); return bool(l and "\tonline\t" in l)
    def trusted(self, fp): l = self.peers().get(fp); return bool(l and "\ttrusted\t" in l)
    def stop(self):
        try: self.cmd("quit", 15)
        except Exception: pass
        try: self.p.wait(10)
        except Exception: self.p.kill()

def sha(path): return hashlib.sha256(open(path, "rb").read()).hexdigest()

# ---------------------------------------------------------------- main
if os.path.exists(W): shutil.rmtree(W)
os.makedirs(W)
WPFX = f"{W}/wpfx"
if not args.native:
    base = os.environ.get("BASE_WINEPREFIX", "/root/.wine-lsm")
    if not os.path.exists(base):
        print("creating wine prefix", base, flush=True)
        subprocess.run(f"WINEPREFIX={base} WINEARCH=win32 WINEDEBUG=-all wineboot -i", shell=True)
        subprocess.run(f"WINEPREFIX={base} WINEDEBUG=-all wineserver -w", shell=True)
        subprocess.run(f"WINEPREFIX={base} WINEDEBUG=-all wine reg add 'HKCU\\Software\\Wine' /v Version /d winxp /f", shell=True)
        subprocess.run(f"WINEPREFIX={base} WINEDEBUG=-all wineserver -w", shell=True)
    shutil.copytree(base, WPFX, symlinks=True)
XPDATA = "C:\\lsm" if not args.native else f"{W}/xpdata"
XPDL = (f"{WPFX}/drive_c/lsm/downloads" if not args.native else f"{W}/xpdata/downloads")

net_up()
A = C = X = None
try:
    t0 = time.time()
    A = Rust("RustA", "lsmA")
    C = Rust("RustC", "lsmC")
    X = XP("XP", "lsmB", XPDATA, ["--name", "XP旧电脑"])
    print(f"RustA={A.fp} RustC={C.fp} XP={X.fp}", flush=True)

    # 1. discovery (broadcast over the bridge, both directions)
    a_sees = wait(lambda: A.online(X.fp), 40); x_sees = wait(lambda: X.online(A.fp), 40)
    (ok if a_sees and x_sees else fail)("discovery: XP <-> RustA online via UDP broadcast", f"{time.time()-t0:.1f}s")
    xs = X.ev(r"^EV seen\t%s" % A.fp, 1); as_ = "XP旧电脑" in A.m("peers")
    (ok if xs and as_ else fail)("discovery: announces parsed both ways (names/fingerprints)", f"XP saw RustA announce={bool(xs)}, RustA lists 'XP旧电脑'={as_}")
    wait(lambda: C.online(X.fp) and X.online(C.fp), 40)
    (ok if X.online(C.fp) else fail)("discovery: XP <-> RustC online")

    # 2. PIN pairing, both directions
    pin = re.search(r"OK pin (\d{6})", X.cmd("pin-offer")[-1]).group(1)
    r = A.j("pin-pair", X.fp, pin)
    t1 = wait(lambda: X.trusted(A.fp), 10)
    (ok if r.get("ok") and t1 and A.peer(X.fp)["trust"] == "trusted" else fail)("PIN pairing: XP offers, v0.5 enters PIN", f"rust result={r}")
    bad = A.j("pin-pair", X.fp, "000000") if pin != "000000" else {"ok": False}
    (ok if bad.get("ok") is False else fail)("PIN pairing: wrong PIN rejected by XP", str(bad))
    pin2 = A.j("pin-offer")["pin"]
    n0 = len(X.lines); X.cmd(f"pin-pair {C.fp[:9]} 123456")  # wrong pin to C (C has no offer) -> fail
    X.cmd(f"pin-pair {A.fp} {pin2}")
    res = X.ev(r"^EV pin_result\t%s\tok" % A.fp, 10, n0)
    (ok if res else fail)("PIN pairing: v0.5 offers, XP enters PIN", res or "")
    # trust RustC both ways via PIN as well (needed for group)
    pin3 = C.j("pin-offer")["pin"]; n0 = len(X.lines); X.cmd(f"pin-pair {C.fp} {pin3}")
    (ok if X.ev(r"^EV pin_result\t%s\tok" % C.fp, 10, n0) and C.peer(X.fp)["trust"] == "trusted" else fail)("PIN pairing: XP <-> RustC")

    # 3. messages + receipts
    out = X.cmd(f"send {A.fp} 你好，来自 XP 的消息 ✓")[-1]; mid = out.split()[-1]
    dl = X.ev(r"^EV msg_update\t%s\t.*\tdelivered\t" % mid, 15)
    got = wait(lambda: "你好，来自 XP 的消息 ✓" in A.m("history", X.fp), 10)
    (ok if dl and got else fail)("message XP -> v0.5 delivered (Ack receipt)", mid)
    conv = f"dm:{X.fp}"; A.j("raw", "mark_read", json.dumps({"conversation": conv}))
    rd = X.ev(r"^EV msg_update\t%s\t.*\tread\t" % mid, 10)
    (ok if rd else fail)("read receipt v0.5 -> XP (status read)")
    r = A.j("send", X.fp, "hello XP, 这里是 v0.5"); rid = r["id"]
    got = X.ev(r"^EV msg\t%s\t.*\tin\treceived\ttext\thello XP, 这里是 v0.5" % rid, 15)
    dlv = wait(lambda: subprocess.run([f"{RB}/lan-msg", "--ipc", A.sock, "wait-delivered", rid, "--timeout", "1"]).returncode == 0, 15)
    (ok if got and dlv else fail)("message v0.5 -> XP delivered (Ack receipt)", rid)
    X.cmd(f"read {A.fp}")
    st = wait(lambda: [m for m in A.j("history", X.fp) if m["id"] == rid and m["status"] == "read"], 10)
    (ok if st else fail)("read receipt XP -> v0.5 (status read)")

    # 3b. v0.6.1 P1 (CAP_EXT): reply / react / edit / recall, both directions
    if args.p1:
        out = X.cmd(f"reply {A.fp} {rid} 收到，十点见")[-1]; xr = out.split()[-1]
        got = wait(lambda: [m for m in A.j("history", X.fp) if m["id"] == xr and m.get("reply_to") == rid and "hello XP" in m.get("quote", "")], 15)
        (ok if got else fail)("P1 reply XP -> Rust (reply_to + quote)", xr)
        X.cmd(f"react {rid} 👍")
        got = wait(lambda: [m for m in A.j("history", X.fp) if m["id"] == rid and any(r.get("emoji") == "👍" for r in m.get("reactions", []))], 15)
        (ok if got else fail)("P1 reaction XP -> Rust")
        A.j("raw", "react", json.dumps({"id": mid, "emoji": "❤️"}))
        got = wait(lambda: "reactions=%s=❤️" % A.fp in "".join(X.cmd(f"msginfo {mid}")), 15)
        (ok if got else fail)("P1 reaction Rust -> XP")
        A.j("raw", "edit_message", json.dumps({"id": rid, "body": "hello XP（已改）"}))
        got = wait(lambda: "edited=1" in "".join(X.cmd(f"msginfo {rid}")) and "hello XP（已改）" in "".join(X.cmd(f"msginfo {rid}")), 15)
        (ok if got else fail)("P1 edit Rust -> XP")
        X.cmd(f"recall {mid}")
        got = wait(lambda: [m for m in A.j("history", X.fp) if m["id"] == mid and m.get("recalled")], 15)
        (ok if got else fail)("P1 recall XP -> Rust")
    # long + multiline-ish unicode text
    long = "长消息" * 3000
    out = X.cmd(f"send {A.fp} {long}")[-1]; lid = out.split()[-1]
    (ok if X.ev(r"^EV msg_update\t%s\t.*\tdelivered" % lid, 15) and wait(lambda: any(m["body"] == long for m in A.j("history", X.fp)), 10) else fail)("long 27 KB UTF-8 message XP -> v0.5 intact")

    # 4. files both directions, sha256 match
    blob = f"{W}/blob_x.bin"; open(blob, "wb").write(os.urandom(3_000_000 + 12345))
    wblob = f"{WPFX}/drive_c/blob_x.bin" if not args.native else blob
    if not args.native: shutil.copy(blob, wblob)
    X.cmd(f"sendfile {A.fp} " + ("C:\\blob_x.bin" if not args.native else blob))
    dst = f"{A.dir}/dl/blob_x.bin"
    done = wait(lambda: os.path.exists(dst) and X.ev(r"^EV transfer\t\S+\tout\tdone\t", 0.1), 60)
    (ok if done and sha(dst) == sha(blob) else fail)("file XP -> v0.5 (3 MB, chunks/credit window) sha256 match", sha(blob)[:16])
    blob2 = f"{W}/blob_r.bin"; open(blob2, "wb").write(os.urandom(5_000_000 + 777))
    A.m("send-file", X.fp, blob2)
    d2 = f"{XPDL}/blob_r.bin"
    done = wait(lambda: os.path.exists(d2) and os.path.getsize(d2) == os.path.getsize(blob2) and any(t["state"] == "done" for t in A.j("transfers")), 60)
    (ok if done and sha(d2) == sha(blob2) else fail)("file v0.5 -> XP (5 MB) sha256 match", sha(blob2)[:16])
    # chinese file name
    cn = f"{W}/报告 2026.txt"; open(cn, "w", encoding="utf-8").write("中文内容\n" * 1000)
    A.m("send-file", X.fp, cn)
    (ok if wait(lambda: os.path.exists(f"{XPDL}/报告 2026.txt") and sha(f"{XPDL}/报告 2026.txt") == sha(cn), 30) else fail)("file with Chinese name v0.5 -> XP")

    # 5. groups
    wait(lambda: A.online(C.fp) and C.online(A.fp), 30); A.m("trust", C.fp); C.m("trust", A.fp)  # v0.5 <-> v0.5 trust (test setup)
    g = A.j("group-create", "混合群", X.fp, C.fp); gid = g["id"]
    inv = X.ev(r"^EV group\t%s\t混合群" % gid, 15)
    (ok if inv else fail)("group: v0.5 creates, XP accepts signed invite", gid)
    n0 = len(X.lines); A.m("send-group", gid, "群消息 from v0.5")
    (ok if X.ev(r"^EV msg\t\S+\tgroup:%s\tin\treceived\ttext\t群消息 from v0.5" % gid, 15, n0) else fail)("group text v0.5 -> XP (decrypt with group key)")
    out = X.cmd(f"sendgroup {gid} 群消息 from XP")[-1]; gmid = out.split()[-1]
    ga = wait(lambda: any(m["body"] == "群消息 from XP" for m in A.j("history", "g:"+gid)), 15)
    gc = wait(lambda: any(m["body"] == "群消息 from XP" for m in C.j("history", "g:"+gid)), 15)
    (ok if ga and gc and X.ev(r"^EV msg_update\t%s\t.*\tdelivered" % gmid, 15) else fail)("group text XP -> v0.5 members (RustA, RustC) + delivered")
    # creator (v0.5) removes RustC -> rekey reaches XP; XP keeps talking with new key
    A.m("group-remove", gid, C.fp)
    rk = wait(lambda: [l for l in X.cmd("groups") if gid in l and "epoch=1" in l and C.fp not in l], 15)
    n0 = len(X.lines); X.cmd(f"sendgroup {gid} after rekey")
    ga = wait(lambda: any(m["body"] == "after rekey" for m in A.j("history", "g:"+gid)), 15)
    (ok if rk and ga else fail)("group rekey from v0.5 creator applied by XP (epoch 1, member removed, new key works)")
    # XP creates a group, file into group, then removes RustC (XP signs rekey)
    out = X.cmd(f"group-create XP群 {A.fp},{C.fp}")[-1]; xg = out.split()[-1]
    ag = wait(lambda: any(gg["id"] == xg for gg in A.j("groups")), 15); cg = wait(lambda: any(gg["id"] == xg for gg in C.j("groups")), 15)
    (ok if ag and cg else fail)("group: XP creates, v0.5 peers verify XP's Ed25519 invite signature", xg)
    A.m("send-group", xg, "v0.5 in XP group")
    (ok if X.ev(r"^EV msg\t\S+\tgroup:%s\tin\treceived\ttext\tv0.5 in XP group" % xg, 15) else fail)("group text v0.5 -> XP in XP-created group")
    gb = f"{W}/gfile.bin"; open(gb, "wb").write(os.urandom(700_000))
    if not args.native: shutil.copy(gb, f"{WPFX}/drive_c/gfile.bin")
    X.cmd(f"sendfile group:{xg} " + ("C:\\gfile.bin" if not args.native else gb))
    fa = wait(lambda: os.path.exists(f"{A.dir}/dl/gfile.bin") and sha(f"{A.dir}/dl/gfile.bin") == sha(gb), 30)
    fc = wait(lambda: os.path.exists(f"{C.dir}/dl/gfile.bin") and sha(f"{C.dir}/dl/gfile.bin") == sha(gb), 30)
    (ok if fa and fc else fail)("group file XP -> both v0.5 members sha256 match")
    X.cmd(f"group-remove {xg} {C.fp}")
    crm = wait(lambda: not any(gg["id"] == xg for gg in C.j("groups")), 15)
    aep = wait(lambda: [gg for gg in A.j("groups") if gg["id"] == xg and C.fp not in json.dumps(gg)], 15)
    A.m("send-group", xg, "after XP rekey")
    (ok if crm and aep and X.ev(r"^EV msg\t\S+\tgroup:%s\tin\treceived\ttext\tafter XP rekey" % xg, 15) else fail)("group rekey signed by XP: RustC removed, RustA uses new key")
    # leave: RustA leaves XP group -> XP (creator) rekeys
    A.m("group-leave", xg)
    (ok if wait(lambda: [l for l in X.cmd("groups") if xg in l and A.fp not in l], 15) else fail)("GroupLeave v0.5 -> XP creator")

    # 6. reconnect after killed link (both sides), queued message delivered exactly once
    n0 = len(X.lines); X.cmd("drop-links")  # abrupt close, no Bye
    out = X.cmd(f"send {A.fp} after cut XP")[-1]; cid = out.split()[-1]
    rc = X.ev(r"^EV peer\t%s\tonline" % A.fp, 30, n0)
    dl = X.ev(r"^EV msg_update\t%s\t.*\tdelivered" % cid, 30, n0)
    cnt = wait(lambda: sum(1 for m in A.j("history", X.fp) if m["body"] == "after cut XP"), 10)
    (ok if rc and dl and cnt == 1 else fail)("reconnect after XP-side link kill; queued msg delivered exactly once", f"count={cnt}")
    n0 = len(X.lines)
    A.m("raw", "debug_drop_connection", json.dumps({"fingerprint": X.fp}))
    r = A.j("send", X.fp, "after cut v0.5"); rid = r["id"]
    rc = X.ev(r"^EV peer\t%s\tonline" % A.fp, 40, n0)
    got = X.ev(r"^EV msg\t%s\t" % rid, 40, n0)
    time.sleep(3)
    dup = sum(1 for l in X.lines[n0:] if l.startswith(f"EV msg\t{rid}\t"))
    (ok if rc and got and dup == 1 else fail)("reconnect after v0.5-side link kill (no Bye; XP heartbeat/redial); delivered once", f"count={dup}")
    # real TCP kill with ss -K inside XP's namespace
    n0 = len(X.lines)
    sh(f"ip netns exec lsmB ss -K dst 10.77.0.1", check=False); sh(f"ip netns exec lsmB ss -K src 10.77.0.2 dst 10.77.0.1", check=False)
    off = X.ev(r"^EV peer\t%s\toffline" % A.fp, 25, n0)
    if off:
        rc = X.ev(r"^EV peer\t%s\tonline" % A.fp, 40, n0 + 1)
        (ok if rc else fail)("reconnect after kernel TCP kill (ss -K)", off.split("\t")[-1])
    else:
        RESULTS.append(("reconnect after kernel TCP kill (ss -K)", "SKIP", "ss -K had no effect in this kernel")); print("[SKIP] ss -K unsupported")

    # 7. name change propagates
    X.cmd("name XP改名")
    (ok if wait(lambda: A.peer(X.fp)["name"] == "XP改名", 10) else fail)("Name frame XP -> v0.5")

    # 8. add-by-IP with discovery disabled, both directions (fresh identities)
    X.stop(); A.stop(); C.stop(); time.sleep(1)
    A = Rust("RustA2", "lsmA", ["discovery_enabled=false", "auto_trust=false"])
    X = XP("XP2", "lsmB", XPDATA + "2", ["--no-discovery"])
    time.sleep(4)
    pre = A.online(X.fp)
    X.cmd(f"add 10.77.0.1:{A.port}")
    up = wait(lambda: A.online(X.fp) and X.online(A.fp), 30)
    (ok if (not pre) and up else fail)("add-by-IP: XP dials v0.5 (no discovery)", f"before={pre}")
    X.stop(); A.stop(); time.sleep(1)
    A = Rust("RustA3", "lsmA", ["discovery_enabled=false"])
    X = XP("XP3", "lsmB", XPDATA + "3", ["--no-discovery"])
    time.sleep(4); pre = X.online(A.fp)
    A.m("add", f"10.77.0.2:{X.port}")
    up = wait(lambda: A.online(X.fp) and X.online(A.fp), 30)
    (ok if (not pre) and up else fail)("add-by-IP: v0.5 dials XP (no discovery)", f"before={pre}")
    out = X.cmd(f"send {A.fp} via add-by-ip")[-1]; mid = out.split()[-1]
    (ok if X.ev(r"^EV msg_update\t%s\t.*\tdelivered" % mid, 15) else fail)("message over add-by-IP link")
except Exception as e:
    import traceback; traceback.print_exc(); fail("harness exception", repr(e)[:300])
finally:
    for o in (X, A, C):
        try:
            if o: o.stop()
        except Exception: pass
    if not args.native: subprocess.run(f"WINEPREFIX={WPFX} wineserver -k", shell=True)
    net_down()
    n_pass = sum(1 for r in RESULTS if r[1] == "PASS"); n_fail = sum(1 for r in RESULTS if r[1] == "FAIL")
    with open(f"{W}/results.json", "w") as f: json.dump(RESULTS, f, ensure_ascii=False, indent=1)
    print(f"\nRESULT: {n_pass} passed, {n_fail} failed, {len(RESULTS)-n_pass-n_fail} skipped  (mode={'native' if args.native else 'wine'})")
    sys.exit(1 if n_fail or not RESULTS else 0)
