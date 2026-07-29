#!/bin/bash
# Run the Linux AAE build with sane defaults.
#
#   wsl -d Ubuntu -- bash /mnt/c/Source2026/AAE_publish/scripts/linux/run.sh asteroid
#
# The one thing this exists for: under WSLg, Mesa defaults to LLVMPIPE - a
# pure-CPU software rasteriser - even though GPU passthrough is available. AAE's
# vector beam renderer is multi-pass FBO work with shaders, so software
# rendering makes it crawl.
#
# Setting GALLIUM_DRIVER=d3d12 selects Mesa's D3D12 Gallium driver, which goes
# through /dev/dxg to the real GPU. Measured on this box:
#     default                 llvmpipe (LLVM 21.1.8, 256 bits)     <- very slow
#     GALLIUM_DRIVER=d3d12    D3D12 (NVIDIA GeForce RTX 4070 Ti)   <- hardware
#
# MESA_LOADER_DRIVER_OVERRIDE and LIBGL_ALWAYS_SOFTWARE=0 do NOT work here -
# both still land on llvmpipe. GALLIUM_DRIVER is the one that takes.
#
# None of this applies to the Steam Machine or a real Pi, where Mesa picks the
# native driver (radeonsi / v3d) on its own. It is WSL-specific, so it is
# applied only when WSL is actually detected.
set -e
cd "$(dirname "$0")/../.." || exit 1

BIN="$PWD/build-linux/aae"
if [ ! -x "$BIN" ]; then
    echo "error: $BIN not built. Run scripts/linux/build.sh aae first." >&2
    exit 1
fi

# ROMs, artwork, samples and aae.ini all live next to the shipped Windows
# binary, and paths resolve relative to the executable's directory - so run
# from there rather than from build-linux.
cd x64/Release || exit 1

if grep -qi microsoft /proc/version 2>/dev/null && [ -e /dev/dxg ]; then
    echo "WSL with GPU passthrough detected - selecting the D3D12 Gallium driver"
    export GALLIUM_DRIVER=d3d12
    export LD_LIBRARY_PATH="/usr/lib/wsl/lib:${LD_LIBRARY_PATH}"
fi

exec "$BIN" "$@"
