#!/bin/sh
# Build LanMessenger-adw AppImage inside debian:12 (glibc 2.36, the oldest target):
# glib 2.78 + GTK 4.12 + libadwaita 1.4 are built from source into $PREFIX and bundled in usr/lib.
# usage (as root in debian:12): sh linux/adw-appimage/build.sh   ->  dist/LanMessenger-adw-$VER-x86_64.AppImage
set -eux
VER=${VER:-0.6.0}; ARCH=x86_64; PREFIX=${PREFIX:-/opt/lsm-adw}
GLIB=2.78.6; GTK=4.12.5; ADW=1.4.7
cd "$(dirname "$0")/../.."; ROOT=$PWD; OUT=$ROOT/build/linux-adw-appimage; DIST=$ROOT/dist; mkdir -p $OUT $DIST
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq build-essential meson ninja-build pkg-config curl xz-utils file git python3 python3-packaging sassc gettext \
  libappstream-dev libffi-dev libpcre2-dev zlib1g-dev libmount-dev libselinux1-dev \
  libpango1.0-dev libcairo2-dev libgdk-pixbuf-2.0-dev libgraphene-1.0-dev libepoxy-dev libxkbcommon-dev libwayland-dev wayland-protocols \
  libx11-dev libxrandr-dev libxi-dev libxext-dev libxcursor-dev libxdamage-dev libxfixes-dev libxinerama-dev libfribidi-dev libharfbuzz-dev libfontconfig-dev libpng-dev libtiff-dev libjpeg-dev libfreetype-dev
export PKG_CONFIG_PATH=$PREFIX/lib/x86_64-linux-gnu/pkgconfig:$PREFIX/lib/pkgconfig:$PREFIX/share/pkgconfig
export LD_LIBRARY_PATH=$PREFIX/lib/x86_64-linux-gnu:$PREFIX/lib PATH=$PREFIX/bin:$PATH
if ! pkg-config --atleast-version=1.4 libadwaita-1; then
  cd $OUT
  fetch(){ [ -d "$2" ] || { curl -fsSL "$1" | tar xJ; }; }
  fetch https://download.gnome.org/sources/glib/2.78/glib-$GLIB.tar.xz glib-$GLIB
  (cd glib-$GLIB && meson setup _b --prefix=$PREFIX --buildtype=release -Dtests=false -Dman=false -Dgtk_doc=false -Dnls=disabled -Dselinux=disabled -Dsysprof=disabled && ninja -C _b install)
  fetch https://download.gnome.org/sources/gtk/4.12/gtk-$GTK.tar.xz gtk-$GTK
  (cd gtk-$GTK && meson setup _b --prefix=$PREFIX --buildtype=release -Dmedia-gstreamer=disabled -Dprint-cpdb=disabled -Dprint-cups=disabled -Dvulkan=disabled \
      -Dintrospection=disabled -Ddocumentation=false -Dman-pages=false -Dbuild-tests=false -Dbuild-examples=false -Dbuild-demos=false -Dbuild-testsuite=false -Df16c=disabled && ninja -C _b install)
  fetch https://download.gnome.org/sources/libadwaita/1.4/libadwaita-$ADW.tar.xz libadwaita-$ADW
  (cd libadwaita-$ADW && meson setup _b --prefix=$PREFIX --buildtype=release -Dintrospection=disabled -Dvapi=false -Dtests=false -Dexamples=false -Dgtk_doc=false && ninja -C _b install)
  cd $ROOT
fi
pkg-config --modversion glib-2.0 gtk4 libadwaita-1
VER=$VER sh linux/build-adw.sh
cc -O2 -std=gnu99 -Isrc -DLSM_APP_VERSION="\"$VER\"" -o build/linux-adw/lanmsg-cli src/cli.c src/core.c src/xcrypto.c src/third_party/monocypher.c src/third_party/monocypher-ed25519.c -lpthread
A=$OUT/AppDir; rm -rf $A; mkdir -p $A/usr/bin $A/usr/lib/gio/modules $A/usr/share/glib-2.0/schemas
cp build/linux-adw/lan-messenger-adw build/linux-adw/lanmsg-cli $A/usr/bin/; strip $A/usr/bin/*
# bundle only what we built (glib/gio/gobject/gmodule/gthread, gtk-4, adwaita-1); the rest is the distro's
for l in $(ldd $A/usr/bin/lan-messenger-adw | awk '{print $3}' | grep "^$PREFIX"); do cp -L "$l" $A/usr/lib/; done
# libadwaita >= 1.4 links appstream (about dialog release notes); not on every Debian 12 install -> bundle it and its small deps
for l in $(ldd $A/usr/bin/lan-messenger-adw | awk '{print $3}' | grep -E "libappstream|libxmlb|libyaml|libstemmer"); do cp -L "$l" $A/usr/lib/; done
strip --strip-unneeded $A/usr/lib/*.so* || true
cp $PREFIX/share/glib-2.0/schemas/*.xml $A/usr/share/glib-2.0/schemas/ 2>/dev/null || true
glib-compile-schemas $A/usr/share/glib-2.0/schemas
cp linux/data/lan-messenger.png $A/; sed -e 's/^Exec=.*/Exec=lan-messenger-adw/' -e 's/^StartupWMClass=.*/StartupWMClass=io.github.frec86.LanMessenger/' linux/data/lan-messenger.desktop > $A/lan-messenger.desktop
cp linux/adw-appimage/AppRun $A/AppRun; chmod +x $A/AppRun; ls -la $A/usr/lib
TOOL=${APPIMAGETOOL:-$OUT/appimagetool}; [ -x "$TOOL" ] || { curl -fsSL -o $TOOL https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-$ARCH.AppImage; chmod +x $TOOL; }
ARCH=$ARCH "$TOOL" --appimage-extract-and-run --no-appstream $A $DIST/LanMessenger-adw-$VER-$ARCH.AppImage
ls -la $DIST
