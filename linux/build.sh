#!/bin/sh
# Build + package the Linux edition. Meant to run on the OLDEST supported base (Ubuntu 20.04, glibc 2.31)
# so the GUI binary runs on every newer distro. GTK 3 is NOT bundled: every desktop distro ships it,
# and a bundled GTK breaks the user's theme (dark mode) and input methods (fcitx/ibus for Chinese).
# Output: dist/linux/LanMessenger-<ver>-x86_64.AppImage and dist/linux/lan-messenger-<ver>-linux-<arch>.tar.gz
set -eu
VER=${VER:-0.5.0-linux.1}
ARCH=$(uname -m)
cd "$(dirname "$0")/.."
OUT=build/linux-rel; DIST=dist/linux; rm -rf "$OUT" "$DIST"; mkdir -p "$OUT" "$DIST"
CORE="src/core.c src/xcrypto.c src/third_party/monocypher.c src/third_party/monocypher-ed25519.c"
CF="-O2 -std=gnu99 -Isrc -Wall -Wno-unused-function -Wno-misleading-indentation -Wno-stringop-truncation -Wno-format-truncation -Wno-deprecated-declarations -DLSM_APP_VERSION=\"$VER\" -D_FORTIFY_SOURCE=2 -fstack-protector-strong"
sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e 's/^/"/' -e 's/$/\\n"/' linux/data/lsm-theme.css | { echo "static const char LSM_THEME_CSS[] ="; cat; echo ";"; } > $OUT/lsm_theme_css.h
cc $CF -I$OUT -o $OUT/lan-messenger-gtk linux/gtk_gui.c $CORE $(pkg-config --cflags --libs gtk+-3.0) -lpthread -ldl -Wl,-z,relro,-z,now
# CLI: fully static with musl when available (runs on any Linux incl. Alpine / NAS boxes), else glibc dynamic
if command -v musl-gcc >/dev/null 2>&1; then musl-gcc $CF -static -o $OUT/lanmsg-cli src/cli.c $CORE -lpthread; else cc $CF -o $OUT/lanmsg-cli src/cli.c $CORE -lpthread; fi
strip $OUT/lan-messenger-gtk $OUT/lanmsg-cli
# ---- tarball
T=$OUT/lan-messenger-$VER-linux-$ARCH; mkdir -p $T/bin $T/share/applications $T/share/icons/hicolor/256x256/apps
cp $OUT/lan-messenger-gtk $OUT/lanmsg-cli $T/bin/; cp linux/data/lan-messenger.png $T/share/icons/hicolor/256x256/apps/; cp linux/data/lan-messenger.png $T/bin/
cp linux/data/lan-messenger.desktop $T/share/applications/; cp linux/data/README-zh.txt linux/data/install.sh linux/data/firewall-setup.sh $T/; chmod +x $T/install.sh $T/firewall-setup.sh
tar -C $OUT -czf $DIST/lan-messenger-$VER-linux-$ARCH.tar.gz lan-messenger-$VER-linux-$ARCH
# ---- AppImage
A=$OUT/AppDir; mkdir -p $A/usr/bin; cp $OUT/lan-messenger-gtk $OUT/lanmsg-cli $A/usr/bin/
cp linux/data/lan-messenger.png $A/; cp linux/data/lan-messenger.desktop $A/; cp linux/data/AppRun $A/AppRun; chmod +x $A/AppRun
TOOL=${APPIMAGETOOL:-}
if [ -z "$TOOL" ]; then TOOL=$OUT/appimagetool; curl -fsSL -o $TOOL https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-$ARCH.AppImage; chmod +x $TOOL; fi
ARCH=$ARCH "$TOOL" --appimage-extract-and-run --no-appstream $A $DIST/LanMessenger-$VER-$ARCH.AppImage
ls -la $DIST
