#!/bin/sh
# build the PlayStation Vita .vpk for RSC-C

set -e

# default to the standard VitaSDK location if the env var isn't set
: "${VITASDK:=/usr/local/vitasdk}"
export VITASDK
export PATH="$VITASDK/bin:/usr/bin:/bin:$PATH"

cd "$(dirname "$0")"

echo "VITASDK=$VITASDK"
arm-vita-eabi-gcc --version | head -1
printf 'sdl2: '; arm-vita-eabi-pkg-config --modversion sdl2

# pass "noclean" as the first arg to skip the clean
[ "$1" = "noclean" ] || make -f Makefile.vita clean
make -f Makefile.vita -j"$(nproc 2>/dev/null || echo 2)"

echo
echo "Built: $(pwd)/rsc-c-vita.vpk"
echo "Install on the Vita with VitaShell: FTP the .vpk across, highlight it, press X."
