#!/usr/bin/env bash
set -euo pipefail

# Asset-free platform diagnostic only. This does not build the game runtime.
# Run from a devkitPro shell; no WSL or Docker environment is required.
: "${DEVKITPRO:?Set DEVKITPRO to the devkitPro installation}"
source_root="${1:-$(pwd)}"
build_root="${2:-${source_root}/build/3ds-frontend}"
toolchain="${DEVKITPRO}/cmake/3DS.cmake"

if [[ ! -f "${toolchain}" || ! -f "${DEVKITPRO}/libctru/include/3ds.h" ]]; then
    printf '%s\n' 'Missing devkitPro 3DS SDK. Install devkitARM, libctru, 3ds-tools and 3ds-cmake.' >&2
    exit 1
fi
cmake -S "${source_root}/platform/3ds" -B "${build_root}" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="${toolchain}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DSTARFOX_3DS_BUILD_NATIVE=ON \
    -DSTARFOX_3DS_BUILD_HOST_TESTS=OFF
cmake --build "${build_root}" --parallel 2
printf 'Frontend diagnostic (NOT the game): %s\n' "${build_root}/starfox_3ds_frontend_check.3dsx"
printf 'PICA GPU diagnostic (NOT the game): %s\n' "${build_root}/starfox_3ds_gpu_check.3dsx"
