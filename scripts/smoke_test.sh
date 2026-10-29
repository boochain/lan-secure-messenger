#!/usr/bin/env bash
# v0.5 smoke test: two real lan-daemon processes + lan-msg client over unix-socket IPC.
#  1. one-way discovery (B's inbound UDP "blocked": discovery_listen=false; only A hears B)
#  2. both online, text both directions, receipts
#  3. file transfer + sha256 check
#  4. kill the TCP connection mid-conversation (ss -K if permitted, else debug RPC) -> auto reconnect,
#     queued message delivered
#  5. diagnostics, clean shutdown, logs written
set -euo pipefail
BIN=${BIN:-target/debug}
[ -n "${NO_BUILD:-}" ] || cargo build -q -p lan-daemon -p lan-cli
T=$(mktemp -d /tmp/lsm-smoke.XXXX)
PA=$((40000 + RANDOM % 10000)); PB=$((PA + 1))
D="$BIN/lan-daemon"; M="$BIN/lan-msg"
pids=()
cleanup() { for p in "${pids[@]}"; do kill "$p" 2>/dev/null || true; done; }
trap cleanup EXIT
common=(--set listen_port=0 --set multicast_enabled=false --set 'exclude_interfaces=[" | "]' --set auto_trust=true --set announce_interval_secs=1)
LSM_LOG=${LSM_LOG:-info} "$D" --data-dir "$T/a" --ipc "$T/a.sock" "${common[@]}" --set display_name=Alice --set discovery_port=$PA --set download_dir="\"$T/a/dl\"" >"$T/a.out" 2>&1 & pids+=($!)
LSM_LOG=${LSM_LOG:-info} "$D" --data-dir "$T/b" --ipc "$T/b.sock" "${common[@]}" --set display_name=Bob --set discovery_port=$PB --set discovery_listen=false \
  --set "announce_targets=[\"127.0.0.1:$PA\"]" --set download_dir="\"$T/b/dl\"" >"$T/b.out" 2>&1 & pids+=($!)
A() { "$M" --ipc "$T/a.sock" "$@"; }
B() { "$M" --ipc "$T/b.sock" "$@"; }
for i in $(seq 50); do [ -S "$T/a.sock" ] && [ -S "$T/b.sock" ] && break; sleep 0.1; done
FA=$(A --json status | python3 -c 'import json,sys;print(json.load(sys.stdin)["me"]["fingerprint"])')
FB=$(B --json status | python3 -c 'import json,sys;print(json.load(sys.stdin)["me"]["fingerprint"])')
echo "A=$FA B=$FB"
A wait-peer "$FB" --timeout 20 && B wait-peer "$FA" --timeout 20
echo "[ok] one-way discovery -> both online"
ID1=$(A --json send "$FB" "你好 Bob" | python3 -c 'import json,sys;print(json.load(sys.stdin)["id"])')
ID2=$(B --json send "$FA" "hi Alice" | python3 -c 'import json,sys;print(json.load(sys.stdin)["id"])')
A wait-delivered "$ID1" --timeout 10 && B wait-delivered "$ID2" --timeout 10
B history "$FA" | grep -q "你好 Bob"; A history "$FB" | grep -q "hi Alice"
echo "[ok] text both directions + delivery receipts"
head -c 3000000 /dev/urandom > "$T/blob.bin"
A send-file "$FB" "$T/blob.bin" >/dev/null
for i in $(seq 100); do ls "$T/b/dl/blob.bin" >/dev/null 2>&1 && B transfers | grep -q " done " && break; sleep 0.2; done
cmp "$T/blob.bin" "$T/b/dl/blob.bin"
echo "[ok] file transfer verified"
PORT_B=$(B --json status | python3 -c 'import json,sys;print(json.load(sys.stdin)["me"]["listen_port"])')
since() { A --json diag | python3 -c "import json,sys;d=json.load(sys.stdin);print(max([p.get('connected_for_ms') or 0 for p in d['peers']]+[0]))"; }
sudo -n ss -K dst 127.0.0.1 dport = ":$PORT_B" >/dev/null 2>&1 || true
sudo -n ss -K src 127.0.0.1 sport = ":$PORT_B" >/dev/null 2>&1 || true
sleep 0.5
if [ "$(since)" -gt 1500 ]; then A raw debug_drop_connection "{\"fingerprint\":\"$FB\"}" >/dev/null; echo "ss -K had no effect here; dropped link via debug RPC"; else echo "killed TCP via ss -K"; fi
ID3=$(A --json send "$FB" "after cut" | python3 -c 'import json,sys;print(json.load(sys.stdin)["id"])')
A wait-peer "$FB" --timeout 20 && B wait-peer "$FA" --timeout 20
A wait-delivered "$ID3" --timeout 20
[ "$(B history "$FA" | grep -c 'after cut')" = 1 ]
echo "[ok] reconnect after connection kill, queued message delivered exactly once"
if [ -n "${PARTITION:-}" ] && sudo -n iptables -L >/dev/null 2>&1; then
  echo "partition: dropping all packets to/from TCP $PORT_B and A's port for 25s (no FIN/RST -> heartbeat must detect)"
  PORT_A=$(A --json status | python3 -c 'import json,sys;print(json.load(sys.stdin)["me"]["listen_port"])')
  for P in $PORT_A $PORT_B; do sudo iptables -I INPUT -i lo -p tcp --dport $P -j DROP; sudo iptables -I INPUT -i lo -p tcp --sport $P -j DROP; done
  ID4=$(A --json send "$FB" "sent during partition" | python3 -c 'import json,sys;print(json.load(sys.stdin)["id"])')
  sleep 25
  A --json status | python3 -c "import json,sys;print('during partition A sees B online =',[p['online'] for p in json.load(sys.stdin)['peers']])"
  for P in $PORT_A $PORT_B; do sudo iptables -D INPUT -i lo -p tcp --dport $P -j DROP; sudo iptables -D INPUT -i lo -p tcp --sport $P -j DROP; done
  A wait-peer "$FB" --timeout 40 && A wait-delivered "$ID4" --timeout 40
  echo "[ok] recovered from silent partition (heartbeat timeout + redial)"
fi
A diag > "$T/diag.json"; python3 -c "import json;d=json.load(open('$T/diag.json'));print('diag peers:',[(p['name'],p['state'],p['direction']) for p in d['peers']])"
A shutdown >/dev/null; B shutdown >/dev/null
sleep 1
ls "$T/a/logs"/daemon*.log >/dev/null
echo "[ok] shutdown + logs in $T/a/logs"
echo "SMOKE PASS"
