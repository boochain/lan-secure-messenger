#!/bin/sh
# Launch test of the Linux artifacts inside a foreign distro container.
# usage (inside container, repo mounted at /src, artifacts in /src/dist/linux): sh tests/distro_launch.sh
# Installs only what a desktop user already has (GTK 3 runtime + an X server for the test), starts the
# AppImage GUI under Xvfb, checks that its window maps and the core reports ready, takes a screenshot,
# and runs the static CLI from the tarball.
set -eu
. /etc/os-release; echo "== $PRETTY_NAME"
if command -v apt-get >/dev/null; then export DEBIAN_FRONTEND=noninteractive; apt-get update -qq >/dev/null; apt-get install -y -qq libgtk-3-0 xvfb xdotool imagemagick fonts-noto-cjk dbus-x11 file >/dev/null
elif command -v dnf >/dev/null; then dnf install -y -q gtk3 xorg-x11-server-Xvfb xdotool ImageMagick google-noto-sans-cjk-ttc-fonts dbus-x11 file findutils >/dev/null
elif command -v zypper >/dev/null; then zypper -q -n in gtk3-tools libgtk-3-0 xvfb-run xdotool ImageMagick noto-sans-sc-fonts dbus-1-x11 file >/dev/null; fi
cd /src/dist/linux; AI=$(ls LanMessenger-*.AppImage | head -1); TGZ=$(ls lan-messenger-*.tar.gz | head -1); chmod +x "$AI"
SHOT=/src/dist/linux/launch-$(echo "$ID-$VERSION_ID" | tr -c 'a-zA-Z0-9.\n-' _).png
Xvfb :91 -screen 0 1100x700x24 >/dev/null 2>&1 & XP=$!; sleep 2; export DISPLAY=:91
LSM_NO_TRAY=1 LSM_VERBOSE=1 ./"$AI" --appimage-extract-and-run --data /tmp/lm --name "distro-test" --no-discovery > /tmp/gui.log 2>&1 & GP=$!
ok=0; for i in $(seq 1 40); do if xdotool search --onlyvisible --classname lan-messenger-gtk >/dev/null 2>&1; then ok=1; break; fi; sleep 0.5; done
sleep 2; import -window root "$SHOT" 2>/dev/null || true
alive=0; kill -0 $GP 2>/dev/null && alive=1
kill $GP 2>/dev/null || true; kill $XP 2>/dev/null || true
echo "GUI window mapped=$ok still-running=$alive screenshot=$SHOT"; grep -iE "critical|error" /tmp/gui.log | grep -v "appindicator" | head -5 || true
mkdir -p /tmp/t && tar -C /tmp/t -xzf "$TGZ"; CLI=$(ls -d /tmp/t/*/bin/lanmsg-cli)
file "$CLI" | sed 's/,.*BuildID[^,]*//' ; out=$(printf 'id\nquit\n' | "$CLI" --data /tmp/lc --no-discovery 2>&1 | grep '^OK id' || true); echo "CLI: $out"
[ "$ok" = 1 ] && [ "$alive" = 1 ] && [ -n "$out" ] && echo "RESULT PASS $PRETTY_NAME" || { echo "RESULT FAIL $PRETTY_NAME"; cat /tmp/gui.log | tail -20; exit 1; }
