#!/usr/bin/env bash
# v0.5 stress: N daemons discover each other via unicast announces on loopback (full mesh),
# every node sends MSGS messages to every other node concurrently, plus concurrent file
# transfers; then verifies every message arrived exactly once and was acknowledged.
set -euo pipefail
N=${N:-5}; MSGS=${MSGS:-100}; FILE_MB=${FILE_MB:-8}
BIN=${BIN:-target/release}
[ -n "${NO_BUILD:-}" ] || cargo build -q --release -p lan-daemon -p lan-cli
T=$(mktemp -d /tmp/lsm-stress.XXXX)
BASE=$((42000 + RANDOM % 8000))
pids=()
cleanup() { for p in "${pids[@]}"; do kill "$p" 2>/dev/null || true; done; }
trap cleanup EXIT
targets() { local s="["; for j in $(seq 0 $((N-1))); do s+="\"127.0.0.1:$((BASE+j))\","; done; echo "${s%,}]"; }
for i in $(seq 0 $((N-1))); do
  "$BIN/lan-daemon" --data-dir "$T/n$i" --ipc "$T/n$i.sock" --set listen_port=0 --set discovery_port=$((BASE+i)) \
    --set multicast_enabled=false --set 'exclude_interfaces=[" | "]' --set auto_trust=true --set announce_interval_secs=1 \
    --set display_name="node$i" --set "announce_targets=$(targets)" --set download_dir="\"$T/n$i/dl\"" >"$T/n$i.out" 2>&1 &
  pids+=($!)
done
C() { local i=$1; shift; "$BIN/lan-msg" --ipc "$T/n$i.sock" "$@"; }
for i in $(seq 0 $((N-1))); do for k in $(seq 50); do [ -S "$T/n$i.sock" ] && break; sleep 0.1; done; done
declare -a FP
for i in $(seq 0 $((N-1))); do FP[$i]=$(C $i --json status | python3 -c 'import json,sys;print(json.load(sys.stdin)["me"]["fingerprint"])'); done
t0=$(date +%s.%N)
for i in $(seq 0 $((N-1))); do for j in $(seq 0 $((N-1))); do [ $i = $j ] || C $i wait-peer "${FP[$j]}" --timeout 30 >/dev/null; done; done
echo "[ok] full mesh of $N nodes online in $(python3 -c "print(round($(date +%s.%N)-$t0,2))")s"
head -c $((FILE_MB*1024*1024)) /dev/urandom > "$T/file.bin"
t0=$(date +%s.%N)
for i in $(seq 0 $((N-1))); do
  (
    for m in $(seq 1 $MSGS); do
      for j in $(seq 0 $((N-1))); do [ $i = $j ] || C $i raw send_text "{\"conversation\":\"dm:${FP[$j]}\",\"body\":\"m $i->$j #$m\"}" >/dev/null; done
    done
  ) &
  C $i send-file "${FP[$(( (i+1) % N ))]}" "$T/file.bin" >/dev/null
done
wait $(jobs -p | grep -v -f <(printf '%s\n' "${pids[@]}")) 2>/dev/null || true
python3 - "$T" "$N" "$MSGS" "$BIN" "${FP[@]}" <<'PY'
import json, subprocess, sys, time
T, N, MSGS, BIN = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
FP = sys.argv[5:]
def call(i, m, p):
    return json.loads(subprocess.check_output([f"{BIN}/lan-msg", "--ipc", f"{T}/n{i}.sock", "--json", "raw", m, json.dumps(p)]))
deadline = time.time() + 120
while True:
    bad = []
    for i in range(N):
        for j in range(N):
            if i == j: continue
            ms = call(i, "get_messages", {"conversation": f"dm:{FP[j]}", "limit": 2000})
            inc = [m["body"] for m in ms if m["direction"] == "in" and m["kind"] == "text"]
            out = [m for m in ms if m["direction"] == "out" and m["kind"] == "text"]
            if len(inc) != MSGS or len(set(inc)) != MSGS or any(m["status"] != "delivered" for m in out):
                bad.append((i, j, len(inc), sum(m["status"] == "delivered" for m in out)))
    tr_ok = all(any(t["state"] == "done" and t["direction"] == "in" for t in call(i, "get_snapshot", {})["transfers"]) for i in range(N))
    if not bad and tr_ok:
        break
    if time.time() > deadline:
        print("FAIL", bad[:5], "transfers ok:", tr_ok); sys.exit(1)
    time.sleep(1)
print(f"[ok] {N*(N-1)*MSGS} messages delivered exactly once with receipts; {N} x file transfers verified")
PY
for i in $(seq 0 $((N-1))); do cmp "$T/file.bin" "$T/n$i/dl/file.bin"; done
echo "[ok] all received files identical; elapsed $(python3 -c "print(round($(date +%s.%N)-$t0,2))")s"
for i in $(seq 0 $((N-1))); do C $i shutdown >/dev/null; done
echo "STRESS PASS"
