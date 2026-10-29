#!/usr/bin/env python3
"""Encrypted backup interop: C core <-> Rust lan-core (LSMBAK02, Argon2id + ChaCha20-Poly1305).
  C export -> Rust import: identity fingerprint, a contact's name/trust/muted/address survive.
  Rust export -> C restore: identity fingerprint survives; wrong password is rejected on both sides.
usage: backup_interop.py --cli build/linux/lanmsg-cli --rust-tool target/release/examples/backup_tool
"""
import argparse, os, subprocess, sys, tempfile, time

ap = argparse.ArgumentParser(); ap.add_argument("--cli", required=True); ap.add_argument("--rust-tool", required=True); a = ap.parse_args()
T = tempfile.mkdtemp(prefix="bakio-"); PW = "备份密码-secret1"; fails = []
def check(name, ok, extra=""):
    print(("PASS " if ok else "FAIL ") + name + (("  " + extra) if extra and not ok else "")); (None if ok else fails.append(name))
def rust(*args): return subprocess.run([a.rust_tool, *args], capture_output=True, text=True, timeout=120)

def c_session(data, cmds, extra=()):
    p = subprocess.Popen([a.cli, "--data", data, "--name", "张三-C", "--no-discovery", "--port", "0", *extra], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, encoding="utf-8")
    out = p.communicate("\n".join(cmds + ["quit"]) + "\n", timeout=180)[0]; return out

# --- C export -> Rust import
cd = os.path.join(T, "c1"); bak1 = os.path.join(T, "c 1.lsmbak")
out = c_session(cd, ["add 127.0.0.1:9", "id", "backup %s %s" % (PW, bak1)])
cfp = next((l.split()[2] for l in out.splitlines() if l.startswith("OK id ")), "")
check("C backup command", ("OK backup " + bak1) in out and os.path.getsize(bak1) > 52, out[-400:])
# contact fields are checked by the Rust->C->Rust round trip below (needs no live peer)
r = rust("import", os.path.join(T, "r1"), "wrong-password", bak1); check("Rust rejects C backup with wrong password", r.returncode != 0)
r = rust("import", os.path.join(T, "r1"), PW, bak1); check("Rust imports C backup", r.returncode == 0, r.stderr)
r = rust("show", os.path.join(T, "r1")); check("fingerprint preserved C->Rust", r.stdout.strip() == "FP " + cfp, r.stdout + r.stderr + " want " + cfp)

# --- Rust export (with a contact) -> C restore -> C export -> Rust import: contact fields survive both converters
rb = os.path.join(T, "r2"); bak2 = os.path.join(T, "r2.lsmbak")
r = rust("export", rb, PW, os.path.join(T, "seed.lsmbak")); check("Rust export", r.returncode == 0, r.stderr)
rfp = rust("show", rb).stdout.split()[1]
CFP = "ABCD-1234-ABCD-1234"
os.makedirs(os.path.join(rb, "config"), exist_ok=True)
with open(os.path.join(rb, "config", "contacts.json"), "w", encoding="utf-8") as f:
    f.write('{"contacts":[{"fingerprint":"%s","name":"李四 \\"Q\\"\\t\\u00e9","trust":"trusted","ed25519_pk":"%s","last_addrs":["10.1.2.3:41235"],"last_seen_ms":1700000000000,"muted":true}],"manual":["10.9.9.9:41235"],"muted_groups":[]}' % (CFP, "ab" * 32))
r = rust("export", rb, PW, bak2); check("Rust export with contact", r.returncode == 0, r.stderr)
cd2 = os.path.join(T, "c2")
r = subprocess.run([a.cli, "--data", cd2, "--restore", bak2, "--password", "nope-nope"], capture_output=True, text=True); check("C rejects Rust backup with wrong password", r.returncode != 0 and "密码错误" in r.stdout, r.stdout)
r = subprocess.run([a.cli, "--data", cd2, "--restore", bak2, "--password", PW], capture_output=True, text=True, encoding="utf-8")
check("C restores Rust backup", r.returncode == 0 and r.stdout.strip() == "OK restore " + rfp, r.stdout + r.stderr + " want " + rfp)
bak3 = os.path.join(T, "c2.lsmbak")
out = c_session(cd2, ["id", "peers", "backup %s %s" % (PW, bak3)])
check("fingerprint preserved Rust->C", ("OK id " + rfp) in out, out[-400:])
peer = next((l for l in out.splitlines() if l.startswith("PEER " + CFP)), "")
check("contact restored in C (trusted, muted)", "trusted" in peer and "muted" in peer, peer or out[-400:])
r = rust("import", os.path.join(T, "r3"), PW, bak3); check("Rust imports C re-export", r.returncode == 0, r.stderr)
r = rust("show", os.path.join(T, "r3"), CFP)
check("contact name/trust/muted/addr survive Rust->C->Rust", r.stdout.splitlines()[-1:] == ['CONTACT 李四 "Q"\té\tTrusted\ttrue\t10.1.2.3:41235'] and ("FP " + rfp) in r.stdout, repr(r.stdout + r.stderr))
print("backup interop: %d failure(s)" % len(fails)); sys.exit(1 if fails else 0)
