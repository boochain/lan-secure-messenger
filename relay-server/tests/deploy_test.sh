#!/usr/bin/env bash
# deploy.sh 端到端测试 (end-to-end test). Usage: tests/deploy_test.sh <relay-server> <relay-test-client>
# Runs isolated under RELAY_HOME. As root with systemd (CI) it also exercises the real systemd unit.
set -u
NEW_BIN="$(readlink -f "$1")"; CLI="$(readlink -f "$2")"
HERE="$(cd "$(dirname "$0")" && pwd)"; D="$HERE/../deploy/deploy.sh"
T="$(mktemp -d)"; export RELAY_HOME="$T/home" RELAY_BIN="$NEW_BIN"
PASS=0; FAIL=0
check() { if eval "$2"; then echo "PASS: $1"; PASS=$((PASS+1)); else echo "FAIL: $1"; FAIL=$((FAIL+1)); fi; }
online() { "$RELAY_HOME/bin/relay-server" --status --config "$RELAY_HOME/etc/relay.toml" 2>/dev/null | sed -n 's/.*"online": \([0-9]*\).*/\1/p'; }
roundtrip() { # port pass
  "$CLI" --addr "127.0.0.1:$1" --password "$2" --name echo-$1 --echo --timeout 6 >/dev/null 2>&1 &
  local e=$!; sleep 0.7
  "$CLI" --addr "127.0.0.1:$1" --password "$2" --name send-$1 --to echo-$1 --count 20 --size 2048 --timeout 6 2>&1 | grep -q "OK roundtrip"; local r=$?
  kill $e 2>/dev/null; wait $e 2>/dev/null; return $r
}
# --- fake firewall tools: log every call, report INACTIVE ---
FW="$T/fakefw"; mkdir -p "$FW"; export FWLOG="$T/fw.log"; : > "$FWLOG"
for t in ufw firewall-cmd nft iptables; do
  cat > "$FW/$t" <<FAKE
#!/bin/sh
echo "$t \$*" >> "\$FWLOG"
case "$t \$*" in
  "ufw status"*) [ -f "$T/ufw-active" ] && echo "Status: active" || echo "Status: inactive" ;;
  "firewall-cmd --state"*) echo "not running"; exit 252 ;;
esac
exit 0
FAKE
  chmod +x "$FW/$t"
done
export PATH="$FW:$PATH"
if [ "$(id -u)" != 0 ] || [ ! -d /run/systemd/system ]; then export RELAY_NO_SYSTEMD=1; MODE=nosystemd; else MODE=systemd; export RELAY_SYSTEMD_DIR=/etc/systemd/system; fi
echo "== deploy test mode=$MODE home=$RELAY_HOME"

# 1. install
"$D" install --port 19701 --password 'Test-Pass-1' > "$T/install.log" 2>&1
check "install exit 0" "[ $? = 0 ]"
check "inactive firewall message" "grep -q '本机防火墙未启用，不做修改' '$T/install.log'"
check "cloud reminder" "grep -q '安全组' '$T/install.log'"
sleep 1
check "status shows running" "'$D' status | grep -q '运行中'"
check "relay round-trip after install" "roundtrip 19701 Test-Pass-1"
[ "$MODE" = systemd ] && check "systemd unit active" "systemctl is-active --quiet lsm-relay"
# 2. idempotent re-run
"$D" install --port 19701 --password 'Test-Pass-1' > "$T/reinstall.log" 2>&1
check "re-install idempotent" "[ $? = 0 ] && grep -q '版本相同' '$T/reinstall.log'"
check "single process after re-run" "[ \$(pgrep -fc \"$RELAY_HOME/bin/relay-server\") = 1 ]"
# 3. change port
"$D" config --port 19711 > "$T/config.log" 2>&1
sleep 1
check "port changed in config" "grep -q 'port = 19711' '$RELAY_HOME/etc/relay.toml'"
check "round-trip on new port" "roundtrip 19711 Test-Pass-1"
check "old port closed" "! (exec 3<>/dev/tcp/127.0.0.1/19701) 2>/dev/null"
# 4. upgrade over a fake older version with leftovers
"$D" stop >/dev/null 2>&1
mkdir -p "$T/legacy"
cat > "$T/legacy/relay-server" <<'OLD'
#!/bin/bash
if [ "${1:-}" = --version ]; then echo "relay-server 0.0.9"; exit 0; fi
exec -a relay-server sleep 600
OLD
chmod +x "$T/legacy/relay-server"
cp "$T/legacy/relay-server" "$RELAY_HOME/bin/relay-server"           # old binary in place
"$T/legacy/relay-server" & OLDPID=$!; disown; echo $OLDPID > "$T/legacy/old.pid"   # running old process
echo 999999 > "$T/legacy/stale.pid"                                   # stale PID file
export RELAY_LEGACY_BINS="$T/legacy/relay-server" RELAY_LEGACY_PIDS="$T/legacy/stale.pid $T/legacy/old.pid"
"$D" upgrade > "$T/upgrade.log" 2>&1
NEWV=$(sed -n 's/^version = "\(.*\)"/\1/p' "$(dirname "$0")/../Cargo.toml" | head -1)   # the version of the binary under test
check "upgrade reports old → new" "grep -qF '0.0.9 → $NEWV' '$T/upgrade.log'"
check "config backed up" "ls '$RELAY_HOME/etc/' | grep -q 'relay.toml.bak'"
check "kept port+pass" "grep -q 'port = 19711' '$RELAY_HOME/etc/relay.toml' && grep -q 'Test-Pass-1' '$RELAY_HOME/etc/relay.toml'"
check "old process was running" "[ -n '$OLDPID' ]"
check "old process gone" "! kill -0 $OLDPID 2>/dev/null"
check "legacy binary removed" "[ ! -f '$T/legacy/relay-server' ]"
check "stale pid removed" "[ ! -f '$T/legacy/stale.pid' ]"
sleep 1
check "round-trip after upgrade" "roundtrip 19711 Test-Pass-1"
check "status online count" "[ -n \"\$(online)\" ]"
# 5. remove autostart
"$D" remove-autostart > "$T/rm.log" 2>&1
check "remove-autostart exit 0" "[ $? = 0 ]"
[ "$MODE" = systemd ] && check "unit file gone" "[ ! -f /etc/systemd/system/lsm-relay.service ]"
# 6. background start then stop
"$D" start >/dev/null 2>&1; sleep 1
check "background start" "'$D' status | grep -q '运行中'"
"$D" stop >/dev/null 2>&1
check "stop" "'$D' status | grep -q '未运行'"
# 7. no firewall mutation on the inactive path
check "firewall: only read-only queries were run" "! grep -Ev '^(ufw status|firewall-cmd --state|nft list ruleset|iptables -S INPUT)' '$FWLOG'"
# 8. active firewall: never without consent; --open-firewall adds; uninstall removes only ours
touch "$T/ufw-active"; : > "$FWLOG"
"$D" config --port 19721 > "$T/fw1.log" 2>&1
check "active fw, no flag → unchanged" "! grep -q 'ufw allow' '$FWLOG' && grep -q '未传 --open-firewall' '$T/fw1.log'"
"$D" install --port 19722 --open-firewall > "$T/fw2.log" 2>&1
check "--open-firewall adds rule" "grep -q 'ufw allow 19722/tcp' '$FWLOG'"
check "rule recorded" "grep -q 'ufw|19722' '$RELAY_HOME/state/firewall-rules'"
# 9. uninstall
"$D" uninstall --yes > "$T/un.log" 2>&1
check "uninstall exit 0" "[ $? = 0 ]"
check "uninstall removed our ufw rule" "grep -q 'ufw delete allow 19722/tcp' '$FWLOG'"
check "uninstall removed files" "[ ! -e '$RELAY_HOME/bin' ] && [ ! -e '$RELAY_HOME/etc' ]"
check "nothing running" "[ -z \"\$(pgrep -f '$RELAY_HOME/bin/relay-server')\" ]"
# 10. dry-run changes nothing
"$D" install --port 19731 --password 'x12345678' --dry-run > "$T/dry.log" 2>&1
check "dry-run creates nothing" "[ ! -e '$RELAY_HOME/bin/relay-server' ] && grep -q '\[dry-run\]' '$T/dry.log'"
echo "== deploy test: $PASS passed, $FAIL failed (mode=$MODE)"
[ "$FAIL" = 0 ] || { for f in "$T"/*.log; do echo "--- $f"; cat "$f"; done; exit 1; }
rm -rf "$T"
