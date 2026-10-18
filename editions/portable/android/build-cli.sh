#!/bin/sh
# Console client for Android from the same C core (NDK clang, PIE, API 21). Used by tests/matrix.py over adb.
set -eu
NDK=${ANDROID_NDK_HOME:-$ANDROID_HOME/ndk/26.3.11579264}; T=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin
cd "$(dirname "$0")/.."; mkdir -p build/android
for abi in x86_64:x86_64-linux-android21 arm64-v8a:aarch64-linux-android21; do a=${abi%%:*}; tr=${abi#*:}
  $T/clang --target=$tr -O2 -std=gnu99 -fPIE -pie -Isrc -Wno-unused-function -DLSM_APP_VERSION=\"0.5.0-android.1\" \
    -o build/android/lanmsg-cli-$a src/cli.c src/core.c src/xcrypto.c src/third_party/monocypher.c src/third_party/monocypher-ed25519.c
done; ls -la build/android
