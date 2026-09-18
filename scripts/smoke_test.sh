#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/target/debug/lan-msg"
DIR="$ROOT/smoke-run"
rm -rf "$DIR"
mkdir -p "$DIR/a" "$DIR/b" "$DIR/files"
echo "hello-from-A-encrypted-file" > "$DIR/files/sample.txt"

DISC=41234
PORT_A=41235
PORT_B=41236

export RUST_LOG=lan_core=info

echo "=== Identity A ==="
"$BIN" --config-dir "$DIR/a" --name Alice --bind 0.0.0.0 \
  --discovery-port "$DISC" --listen-port "$PORT_A" id | tee "$DIR/id_a.txt"
FP_A=$(grep '指纹' "$DIR/id_a.txt" | awk '{print $NF}')

echo "=== Identity B ==="
"$BIN" --config-dir "$DIR/b" --name Bob --bind 0.0.0.0 \
  --discovery-port "$DISC" --listen-port "$PORT_B" id | tee "$DIR/id_b.txt"
FP_B=$(grep '指纹' "$DIR/id_b.txt" | awk '{print $NF}')

echo "FP_A=$FP_A"
echo "FP_B=$FP_B"
test -n "$FP_A" && test -n "$FP_B"

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

echo "=== Daemon log snippets ==="
head -50 "$DIR/a.log" || true
echo "---"
head -50 "$DIR/b.log" || true

echo "=== Send text A -> B ==="
"$BIN" --config-dir "$DIR/a" --name Alice --bind 0.0.0.0 \
  --discovery-port "$DISC" --listen-port 41246 --auto-trust \
  send "$FP_B" "你好 Bob，这是加密消息" --wait 8 | tee "$DIR/send_text.txt"

sleep 1

echo "=== Send file A -> B ==="
"$BIN" --config-dir "$DIR/a" --name Alice --bind 0.0.0.0 \
  --discovery-port "$DISC" --listen-port 41247 --auto-trust \
  send-file "$FP_B" "$DIR/files/sample.txt" --wait 8 | tee "$DIR/send_file.txt"

sleep 2

echo "=== B log ==="
cat "$DIR/b.log"

echo "=== Find received files ==="
find "$DIR/b" -type f | sort

TEXT_OK=0
FILE_OK=0
if grep -q '你好 Bob' "$DIR/b.log"; then
  echo "TEXT OK"
  TEXT_OK=1
else
  echo "TEXT FAIL"
fi

recv=$(find "$DIR/b" -name 'sample.txt' 2>/dev/null | head -1 || true)
if [[ -n "${recv:-}" ]]; then
  echo "Received file: $recv"
  diff -u "$DIR/files/sample.txt" "$recv"
  echo "FILE OK"
  FILE_OK=1
else
  echo "FILE FAIL"
fi

if [[ "$TEXT_OK" -eq 1 && "$FILE_OK" -eq 1 ]]; then
  echo "=== SMOKE PASSED ==="
  exit 0
else
  echo "=== SMOKE FAILED ==="
  exit 1
fi
