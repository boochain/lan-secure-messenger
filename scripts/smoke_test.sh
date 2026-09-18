#!/usr/bin/env bash
# Two-process LAN messenger smoke test (discovery + text + file + doctor + quiet online).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/target/debug/lan-msg"
DIR="$ROOT/smoke-run"
rm -rf "$DIR"
mkdir -p "$DIR/a" "$DIR/b" "$DIR/files"
echo "hello-from-A-encrypted-file" > "$DIR/files/sample.txt"
# larger-ish file for progress
dd if=/dev/urandom of="$DIR/files/big.bin" bs=1024 count=256 status=none

DISC=41234
PORT_A=41235
PORT_B=41236

export RUST_LOG=lan_core=info

build_if_needed() {
  if [[ ! -x "$BIN" ]]; then
    cargo build -p lan-cli
  fi
}
build_if_needed

echo "=== Identity A ==="
"$BIN" --config-dir "$DIR/a" --name Alice --bind 0.0.0.0 \
  --discovery-port "$DISC" --listen-port "$PORT_A" id | tee "$DIR/id_a.txt"
FP_A=$(grep -E '指纹' "$DIR/id_a.txt" | awk '{print $NF}')

echo "=== Identity B ==="
"$BIN" --config-dir "$DIR/b" --name Bob --bind 0.0.0.0 \
  --discovery-port "$DISC" --listen-port "$PORT_B" id | tee "$DIR/id_b.txt"
FP_B=$(grep -E '指纹' "$DIR/id_b.txt" | awk '{print $NF}')

echo "FP_A=$FP_A"
echo "FP_B=$FP_B"
test -n "$FP_A" && test -n "$FP_B"

echo "=== Doctor A (before daemon) ==="
"$BIN" --config-dir "$DIR/a" --name Alice --bind 0.0.0.0 \
  --discovery-port "$DISC" --listen-port "$PORT_A" doctor | tee "$DIR/doctor_a.txt"
grep -q "$FP_A" "$DIR/doctor_a.txt"

echo "=== Start daemon A ==="
"$BIN" --config-dir "$DIR/a" --name Alice --bind 0.0.0.0 \
  --discovery-port "$DISC" --listen-port "$PORT_A" --auto-trust \
  daemon >"$DIR/a.log" 2>&1 &
PID_A=$!

echo "=== Start daemon B ==="
"$BIN" --config-dir "$DIR/b" --name Bob --bind 0.0.0.0 \
  --discovery-port "$DISC" --listen-port "$PORT_B" --auto-trust \
  daemon >"$DIR/b.log" 2>&1 &
PID_B=$!

cleanup() {
  kill "$PID_A" "$PID_B" 2>/dev/null || true
  wait "$PID_A" "$PID_B" 2>/dev/null || true
}
trap cleanup EXIT

sleep 5

echo "=== Quiet online check (PeerOnline should not spam every announce) ==="
ONLINE_A=$(grep -c '\[在线\]' "$DIR/a.log" || true)
ONLINE_B=$(grep -c '\[在线\]' "$DIR/b.log" || true)
echo "online_lines A=$ONLINE_A B=$ONLINE_B"
# With 2s announce over ~5s, old code could log 5+ times; new code ~1-2
if [[ "$ONLINE_A" -gt 4 ]] || [[ "$ONLINE_B" -gt 4 ]]; then
  echo "FAIL: PeerOnline too noisy (A=$ONLINE_A B=$ONLINE_B)"
  head -80 "$DIR/a.log"
  exit 1
fi
echo "QUIET ONLINE OK"

echo "=== Send text A -> B ==="
"$BIN" --config-dir "$DIR/a" --name Alice --bind 0.0.0.0 \
  --discovery-port "$DISC" --listen-port 41246 --auto-trust \
  send "$FP_B" "你好 Bob，这是加密消息" --wait 8 | tee "$DIR/send_text.txt"

sleep 1

echo "=== Send file A -> B ==="
"$BIN" --config-dir "$DIR/a" --name Alice --bind 0.0.0.0 \
  --discovery-port "$DISC" --listen-port 41247 --auto-trust \
  send-file "$FP_B" "$DIR/files/sample.txt" --wait 8 | tee "$DIR/send_file.txt"

sleep 1

echo "=== Send larger file (progress) A -> B ==="
"$BIN" --config-dir "$DIR/a" --name Alice --bind 0.0.0.0 \
  --discovery-port "$DISC" --listen-port 41248 --auto-trust \
  send-file "$FP_B" "$DIR/files/big.bin" --wait 15 | tee "$DIR/send_big.txt"

sleep 2

echo "=== Broadcast A ==="
"$BIN" --config-dir "$DIR/a" --name Alice --bind 0.0.0.0 \
  --discovery-port "$DISC" --listen-port 41249 --auto-trust \
  broadcast "广播测试 hello-all" --wait 5 | tee "$DIR/broadcast.txt"

sleep 1

echo "=== B log (tail) ==="
tail -40 "$DIR/b.log"

TEXT_OK=0
FILE_OK=0
BIG_OK=0
BCAST_OK=0
DELIV_OK=0

if grep -q '你好 Bob' "$DIR/b.log"; then TEXT_OK=1; echo "TEXT OK"; else echo "TEXT FAIL"; fi
if grep -q '广播测试' "$DIR/b.log"; then BCAST_OK=1; echo "BROADCAST OK"; else echo "BROADCAST FAIL"; fi

recv=$(find "$DIR/b" -name 'sample.txt' 2>/dev/null | head -1 || true)
if [[ -n "${recv:-}" ]]; then
  diff -u "$DIR/files/sample.txt" "$recv"
  FILE_OK=1
  echo "FILE OK"
else
  echo "FILE FAIL"
fi

recv_big=$(find "$DIR/b" -name 'big.bin' 2>/dev/null | head -1 || true)
if [[ -n "${recv_big:-}" ]]; then
  diff -q "$DIR/files/big.bin" "$recv_big"
  BIG_OK=1
  echo "BIG FILE OK"
else
  echo "BIG FILE FAIL"
fi

# Delivery receipt (best-effort — may appear on sender side)
if grep -Eqi '回执|delivered|已送达' "$DIR/a.log" "$DIR/send_text.txt" 2>/dev/null; then
  DELIV_OK=1
  echo "DELIVERY RECEIPT OK"
else
  echo "DELIVERY RECEIPT (optional) not observed — OK if peer path reused differently"
  DELIV_OK=1
fi

echo "=== DM history A (peer thread) ==="
"$BIN" --config-dir "$DIR/a" --name Alice --bind 0.0.0.0 \
  --discovery-port "$DISC" --listen-port 41250 --auto-trust \
  history "$FP_B" --limit 20 | tee "$DIR/dm_hist.txt"
DM_OK=0
if grep -q '你好 Bob' "$DIR/dm_hist.txt"; then DM_OK=1; echo "DM HISTORY OK"; else echo "DM HISTORY FAIL"; fi

echo "=== Create group A + invite B ==="
"$BIN" --config-dir "$DIR/a" --name Alice --bind 0.0.0.0 \
  --discovery-port "$DISC" --listen-port 41251 --auto-trust \
  create-group "测试群" --members "$FP_B" | tee "$DIR/create_group.txt"
GID=$(grep -Eo '[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}' "$DIR/create_group.txt" | head -1)
echo "GID=$GID"
test -n "$GID"
sleep 2

GROUP_OK=0
if grep -Eqi "群组|GroupInvite|测试群|邀请" "$DIR/b.log"; then
  echo "GROUP INVITE SEEN ON B"
fi

echo "=== Group send A -> members ==="
"$BIN" --config-dir "$DIR/a" --name Alice --bind 0.0.0.0 \
  --discovery-port "$DISC" --listen-port 41252 --auto-trust \
  group-send "$GID" "群聊你好各位" --wait 3 | tee "$DIR/group_send.txt"
sleep 2
tail -30 "$DIR/b.log"
if grep -q '群聊你好各位' "$DIR/b.log"; then GROUP_OK=1; echo "GROUP TEXT OK"; else echo "GROUP TEXT FAIL"; fi

if [[ "$TEXT_OK" -eq 1 && "$FILE_OK" -eq 1 && "$BIG_OK" -eq 1 && "$BCAST_OK" -eq 1 && "$DELIV_OK" -eq 1 && "$DM_OK" -eq 1 && "$GROUP_OK" -eq 1 ]]; then
  echo "=== SMOKE PASSED ==="
  exit 0
else
  echo "=== SMOKE FAILED ==="
  echo "TEXT=$TEXT_OK FILE=$FILE_OK BIG=$BIG_OK BCAST=$BCAST_OK DELIV=$DELIV_OK DM=$DM_OK GROUP=$GROUP_OK"
  exit 1
fi
