#!/usr/bin/env bash
# Stability stress: rapid text ping-pong, concurrent small files, peer leave mid-transfer.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/target/debug/lan-msg"
DIR="$ROOT/stress-run"
rm -rf "$DIR"
mkdir -p "$DIR/a" "$DIR/b" "$DIR/files"
for i in $(seq 1 8); do
  echo "stress-file-$i-$(head -c 32 /dev/urandom | base64 | tr -d '\n')" > "$DIR/files/f$i.txt"
done
dd if=/dev/urandom of="$DIR/files/mid.bin" bs=1024 count=512 status=none

DISC=41334
PORT_A=41335
PORT_B=41336
export RUST_LOG=lan_core=warn

cargo build -p lan-cli -q
"$BIN" --config-dir "$DIR/a" --name StressA --bind 0.0.0.0 \
  --discovery-port "$DISC" --listen-port "$PORT_A" id >"$DIR/id_a.txt"
"$BIN" --config-dir "$DIR/b" --name StressB --bind 0.0.0.0 \
  --discovery-port "$DISC" --listen-port "$PORT_B" id >"$DIR/id_b.txt"
FP_A=$(grep -E '指纹' "$DIR/id_a.txt" | awk '{print $NF}')
FP_B=$(grep -E '指纹' "$DIR/id_b.txt" | awk '{print $NF}')
test -n "$FP_A" && test -n "$FP_B"

"$BIN" --config-dir "$DIR/a" --name StressA --bind 0.0.0.0 \
  --discovery-port "$DISC" --listen-port "$PORT_A" --auto-trust \
  daemon >"$DIR/a.log" 2>&1 &
PID_A=$!
"$BIN" --config-dir "$DIR/b" --name StressB --bind 0.0.0.0 \
  --discovery-port "$DISC" --listen-port "$PORT_B" --auto-trust \
  daemon >"$DIR/b.log" 2>&1 &
PID_B=$!
cleanup() { kill "$PID_A" "$PID_B" 2>/dev/null || true; wait "$PID_A" "$PID_B" 2>/dev/null || true; }
trap cleanup EXIT
sleep 4

echo "=== Rapid text ping-pong (20 msgs) ==="
FAIL=0
for i in $(seq 1 10); do
  if ! "$BIN" --config-dir "$DIR/a" --bind 0.0.0.0 --discovery-port "$DISC" --listen-port $((41400+i)) --auto-trust \
    send "$FP_B" "ping-$i" --wait 3 >"$DIR/send_a_$i.txt" 2>&1; then FAIL=$((FAIL+1)); fi
  if ! "$BIN" --config-dir "$DIR/b" --bind 0.0.0.0 --discovery-port "$DISC" --listen-port $((41500+i)) --auto-trust \
    send "$FP_A" "pong-$i" --wait 3 >"$DIR/send_b_$i.txt" 2>&1; then FAIL=$((FAIL+1)); fi
done
ACK=$(grep -h '回执' "$DIR"/send_a_*.txt "$DIR"/send_b_*.txt 2>/dev/null | wc -l | tr -d ' ')
echo "acks_observed=$ACK fail_sends=$FAIL"
if [[ "$FAIL" -gt 4 ]]; then echo "STRESS TEXT FAIL"; exit 1; fi
if [[ "$ACK" -lt 10 ]]; then echo "STRESS ACK FAIL (acks=$ACK want >=10)"; exit 1; fi
echo "RAPID TEXT OK"

echo "=== Concurrent small files (4 parallel) ==="
SF_PIDS=()
for i in 1 2 3 4; do
  "$BIN" --config-dir "$DIR/a" --bind 0.0.0.0 --discovery-port "$DISC" --listen-port $((41600+i)) --auto-trust \
    send-file "$FP_B" "$DIR/files/f$i.txt" --wait 12 >"$DIR/sf_$i.txt" 2>&1 &
  SF_PIDS+=($!)
done
for pid in "${SF_PIDS[@]}"; do wait "$pid" || true; done
sleep 1
GOT=0
for i in 1 2 3 4; do
  if find "$DIR/b" -name "f$i.txt" 2>/dev/null | grep -q .; then GOT=$((GOT+1)); fi
done
echo "files_received=$GOT"
if [[ "$GOT" -lt 3 ]]; then echo "STRESS FILES FAIL"; exit 1; fi
PARTS=$(find "$DIR/b" -name '*.part' 2>/dev/null | wc -l | tr -d ' ')
echo "leftover_parts=$PARTS"
if [[ "$PARTS" -gt 0 ]]; then echo "STRESS PART LEAK FAIL"; find "$DIR/b" -name '*.part'; exit 1; fi
echo "CONCURRENT FILES OK"

echo "=== Mid-transfer kill / recovery ==="
"$BIN" --config-dir "$DIR/a" --bind 0.0.0.0 --discovery-port "$DISC" --listen-port 41710 --auto-trust \
  send-file "$FP_B" "$DIR/files/mid.bin" --wait 2 >"$DIR/mid_send.txt" 2>&1 &
MID_PID=$!
sleep 0.3
kill "$PID_B" 2>/dev/null || true
wait "$MID_PID" 2>/dev/null || true
"$BIN" --config-dir "$DIR/b" --name StressB --bind 0.0.0.0 \
  --discovery-port "$DISC" --listen-port "$PORT_B" --auto-trust \
  daemon >"$DIR/b2.log" 2>&1 &
PID_B=$!
sleep 3
find "$DIR/b" -name '*.part' -delete 2>/dev/null || true
"$BIN" --config-dir "$DIR/a" --bind 0.0.0.0 --discovery-port "$DISC" --listen-port 41711 --auto-trust \
  send "$FP_B" "after-kill-ok" --wait 8 >"$DIR/recover.txt" 2>&1
if ! grep -Eqi '回执|已发送|已送达|文件已发送' "$DIR/recover.txt"; then
  echo "STRESS RECOVERY FAIL"; cat "$DIR/recover.txt"; exit 1
fi
if grep -qi 'panic' "$DIR/a.log" "$DIR/b.log" "$DIR/b2.log" 2>/dev/null; then
  echo "STRESS PANIC DETECTED"; exit 1
fi
echo "MID-TRANSFER / RECOVERY OK"
echo "=== STRESS PASSED ==="
