#!/bin/bash
# syntax check of one or more sources with the NDK's clang (aarch64 Android 29)
D=$(cd "$(dirname "$0")" && pwd)
CXX=${NDK:-/opt/ndk/android-ndk-r27c}/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++
for f in "$@"; do
  $CXX --target=aarch64-linux-android29 -std=c++20 -fexperimental-library -fsyntax-only -DFMT_HEADER_ONLY -DEDEN_DSMOD_BUILD_DEV_TOOLS=0 -I$D/shim -I$D/eden -I$D/third_party -I$D/third_party/stb -I$D/third_party/bc_decoder -I$D/module -I$D/third_party/zstd -I$D/module -Wno-everything "$f" 2>&1 | grep -E "error" | head -${N:-8}
done
