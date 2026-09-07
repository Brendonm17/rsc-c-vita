#!/bin/sh
# build the PlayStation Vita hardware-renderer (vitaGL) eboot for RSC-C
# pass "vpk" as the first arg to build the full .vpk instead of just eboot.bin
set -e
: "${VITASDK:=/usr/local/vitasdk}"
export VITASDK
export PATH="$VITASDK/bin:/usr/bin:/bin:$PATH"
# $0 is unreliable (piped via the Windows->WSL bridge); cd to the project path
cd "/mnt/c/Users/Brendon Moncada/RuneScape_vita/rsc-c" 2>/dev/null || cd "$(dirname "$0")"

echo "VITASDK=$VITASDK"
arm-vita-eabi-gcc --version | head -1

if [ "$1" = "vpk" ]; then
    make -f Makefile.vita.gl -j"$(nproc 2>/dev/null || echo 4)"
    echo "Built: $(pwd)/rsc-c-vita.vpk"
else
    # code-only iteration: just the eboot, skips vpk packing
    make -f Makefile.vita.gl -j"$(nproc 2>/dev/null || echo 4)" eboot.bin
    echo "Built: $(pwd)/eboot.bin  (copy over D:\\app\\RSCC00001\\eboot.bin)"
fi
