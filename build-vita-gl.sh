#!/bin/sh
# build the PlayStation Vita hardware-renderer (vitaGL) eboot for RSC-C
set -e
: "${VITASDK:=/usr/local/vitasdk}"
export VITASDK
export PATH="$VITASDK/bin:/usr/bin:/bin:$PATH"
cd "$(dirname "$0")"

echo "VITASDK=$VITASDK"
arm-vita-eabi-gcc --version | head -1

if [ "$1" = "vpk" ]; then
    make -f Makefile.vita.gl -j"$(nproc 2>/dev/null || echo 4)"
    echo "Built: $(pwd)/rsc-c-vita.vpk"
else
    # code-only iteration: just the eboot, skips vpk packing
    make -f Makefile.vita.gl -j"$(nproc 2>/dev/null || echo 4)" eboot.bin
    echo "Built: $(pwd)/eboot.bin  (copy to ux0:app/RSCC00001/ on the Vita)"
fi
