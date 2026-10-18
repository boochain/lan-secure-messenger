#!/bin/sh
# Build the GTK 4 + libadwaita (>= 1.4) Linux GUI: build/linux-adw/lan-messenger-adw
set -eu
VER=${VER:-0.6.0-linux.dev}
cd "$(dirname "$0")/.."
OUT=build/linux-adw; mkdir -p "$OUT"
CORE="src/core.c src/xcrypto.c src/third_party/monocypher.c src/third_party/monocypher-ed25519.c"
pkg-config --atleast-version=1.4 libadwaita-1 || { echo "libadwaita >= 1.4 required (have $(pkg-config --modversion libadwaita-1 2>/dev/null || echo none))"; exit 1; }
sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e 's/^/"/' -e 's/$/\\n"/' linux/data/lsm-adw.css | { echo "static const char LSM_ADW_RULES[] ="; cat; echo ";"; } > $OUT/lsm_adw_css.h
RELAY=""; grep -q "lsm_relay_status" src/core.h && RELAY="-DLSM_HAVE_RELAY"
cc $RELAY -O2 -std=gnu99 -Isrc -I$OUT -Wall -Wno-unused-function -Wno-misleading-indentation -Wno-stringop-truncation -Wno-format-truncation -DLSM_APP_VERSION="\"$VER\"" \
   -D_FORTIFY_SOURCE=2 -fstack-protector-strong -o $OUT/lan-messenger-adw linux/adw_gui.c $CORE $(pkg-config --cflags --libs libadwaita-1 gtk4) -lpthread -ldl -Wl,-z,relro,-z,now
echo built $OUT/lan-messenger-adw
