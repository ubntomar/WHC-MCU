#!/usr/bin/env bash
# bootstrap_ch32v203_sdk.sh
#
# Reproduces ch32v/ch32v203/sdk/ from the official WCH upstream repo.
# Run once after a fresh clone of WHC-MCU.
#
# Requires: git, standard coreutils.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
V203="$ROOT/ch32v/ch32v203"
UP="$V203/_upstream"
SDK="$V203/sdk"

REPO="https://github.com/openwch/ch32v20x.git"

echo "[1/3] Cloning $REPO into $UP (shallow)..."
if [ -d "$UP/.git" ] && [ -f "$UP/README.md" ]; then
    echo "      _upstream already present, skipping clone."
else
    rm -rf "$UP"
    git clone --depth 1 "$REPO" "$UP"
fi

SRC="$UP/EVT/EXAM/SRC"
USR="$UP/EVT/EXAM/GPIO/GPIO_Toggle/User"

echo "[2/3] Copying HAL files into $SDK ..."
mkdir -p \
    "$SDK/Core" \
    "$SDK/Debug" \
    "$SDK/Peripheral/inc" \
    "$SDK/Peripheral/src" \
    "$SDK/Startup" \
    "$SDK/Ld"

cp -f "$SRC/Core/"*                       "$SDK/Core/"
cp -f "$SRC/Debug/"*                      "$SDK/Debug/"
cp -f "$SRC/Peripheral/inc/"*             "$SDK/Peripheral/inc/"
cp -f "$SRC/Peripheral/src/"*             "$SDK/Peripheral/src/"
cp -f "$SRC/Startup/startup_ch32v20x_D6.S" "$SDK/Startup/"
cp -f "$SRC/Ld/Link.ld"                   "$SDK/Ld/"

# Project-level headers that live alongside main.c in upstream examples,
# but that we want globally available via the sdk include path.
cp -f "$USR/ch32v20x_conf.h"     "$SDK/Core/"
cp -f "$USR/ch32v20x_it.c"       "$SDK/Core/"
cp -f "$USR/ch32v20x_it.h"       "$SDK/Core/"
cp -f "$USR/system_ch32v20x.c"   "$SDK/Core/"
cp -f "$USR/system_ch32v20x.h"   "$SDK/Core/"

echo "[3/3] Patching system_ch32v20x.c to use HSI (boards in this repo use 8 MHz HSE, not the SDK default 32 MHz)..."
sed -i \
    -e 's|^#define SYSCLK_FREQ_96MHz_HSE  96000000|//#define SYSCLK_FREQ_96MHz_HSE  96000000\n#define SYSCLK_FREQ_HSI    HSI_VALUE|' \
    "$SDK/Core/system_ch32v20x.c"

echo
echo "Done. You can now build with:"
echo "  cd $V203/projects/blink_pa1 && make && make flash"
