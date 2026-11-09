#!/usr/bin/env bash
# Builds Sonic Adventure for the Switch as a homebrew NRO and stages the SD card's folder:
#
#   out/sd/switch/sonicadv/sonicadv.nro
#   out/sd/switch/sonicadv/sonicadv.toml   the game's config with the SD card's paths
#
# The disc (the GDI and its tracks, into disc/) and 1ST_READ.BIN are game data and are never
# staged by this script: they are copied on Mark's PC from his own dump.
#
# Linux only. Environment:
#   DEVKITPRO   default /opt/devkitpro (on a cloud machine: CoD-nx's
#               tools/linux/setup_switch_wine.sh over a copy of Mark's C:\devkitPro)
#   NVK_SDK     default /opt/nvk-sdk (mesa-switch NVK: lib/libvulkan.a, include/vulkan)
#   JOBS        parallel compile jobs (default: by free memory, at most nproc)
#   PARTS       translation units the game's C++ is split into (default 32)
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
NVK_SDK="${NVK_SDK:-/opt/nvk-sdk}"
PARTS="${PARTS:-32}"
HOST="$REPO/out/build-host"
BUILD="$REPO/out/build-switch"
SD="$REPO/out/sd/switch/sonicadv"
export WINEDEBUG=-all

fail() { echo "error: $*" >&2; exit 1; }
[ -f "$DEVKITPRO/cmake/Switch.cmake" ] || fail "devkitPro not found at $DEVKITPRO"
[ -f "$NVK_SDK/lib/libvulkan.a" ] || fail "no NVK in $NVK_SDK"
[ -f "$DEVKITPRO/portlibs/switch/include/SDL2/SDL.h" ] || fail "switch-sdl2 missing"
[ -f "$REPO/games/sonicadv/extracted/fs/1ST_READ.BIN" ] || fail "the disc is not extracted"

# The dream-recomp patches, as tools/win/common.ps1 applies them: a stamp of the whole set, and
# a clean submodule with every patch applied whenever the set changes.
STAMP="$REPO/out/dream-patches-switch.stamp"
mkdir -p "$REPO/out"
want=$(cat "$REPO"/patches/dream-recomp/*.patch | sha256sum | cut -d' ' -f1)
if [ "$(cat "$STAMP" 2>/dev/null)" != "$want" ]; then
    echo "applying dream-recomp patches ..."
    git -C "$REPO/dream-recomp" checkout -q -- .
    git -C "$REPO/dream-recomp" clean -fdq
    for p in "$REPO"/patches/dream-recomp/*.patch; do
        git -C "$REPO/dream-recomp" apply --whitespace=nowarn "$p" || fail "patch $(basename "$p") does not apply"
    done
    echo "$want" > "$STAMP"
fi

# The translator runs here, on the build machine, so it is built for it.
if [ ! -x "$HOST/translator/dream-translate" ]; then
    cmake -S "$REPO/dream-recomp" -B "$HOST" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DDREAM_BUILD_TESTS=OFF -DDREAM_RENDERER=OFF -DDREAM_WERROR=OFF >/dev/null
fi
ninja -C "$HOST" dream-translate >/dev/null

if [ -z "${JOBS:-}" ]; then
    mem_mb=$(( $(awk '/^MemAvailable:/ {print $2}' /proc/meminfo) / 1024 ))
    JOBS=$(( mem_mb / 2000 )); [ "$JOBS" -lt 1 ] && JOBS=1
    [ "$JOBS" -gt "$(nproc)" ] && JOBS=$(nproc)
fi

cmake -S "$REPO" -B "$BUILD" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Switch.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DDREAM_DEV_INTERPRETER=ON -DDREAM_BUILD_TESTS=OFF -DDREAM_WERROR=OFF \
    -DDREAM_TRANSLATE_PARTS="$PARTS" \
    -DDREAM_HOST_TRANSLATE="$HOST/translator/dream-translate" \
    -DCMAKE_CXX_LINKER_LAUNCHER="$REPO/switch/link_rsp.sh" \
    -DNVK_SDK="$NVK_SDK" >/dev/null
ninja -C "$BUILD" -j"$JOBS" sonicadv_boot_nro

mkdir -p "$SD"
cp "$BUILD/games/sonicadv/sonicadv_boot.nro" "$SD/sonicadv.nro"
cp "$BUILD/games/sonicadv/sonicadv_boot.elf" "$REPO/out/sonicadv-switch.elf"  # for crash offsets
# The config, pointed at the SD card's layout (relative to sdmc:/switch/sonicadv/).
sed -e 's#^image = ".*/\([^/"]*\.gdi\)"#image = "disc/\1"#' \
    -e 's#^path = "extracted/fs/1ST_READ.BIN"#path = "1ST_READ.BIN"#' \
    "$REPO/games/sonicadv/sonicadv.toml" > "$SD/sonicadv.toml"
# The US version's config (its boot program is kept as 1ST_READ_US.BIN, taken from the disc).
sed -e 's#^image = ".*/\([^/"]*\.gdi\)"#image = "disc/\1"#' \
    -e 's#^path = "extracted_us/1ST_READ.plain.bin"#path = "1ST_READ_US.BIN"#' \
    "$REPO/games/sonicadv/sonicadv_us.toml" > "$SD/sonicadv_us.toml"
echo
echo "staged $SD/sonicadv.nro ($(stat -c %s "$SD/sonicadv.nro") bytes)"
sha256sum "$SD/sonicadv.nro"
