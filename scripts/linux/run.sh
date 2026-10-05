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

BIN="$PWD/x64/Release/aae"
if [ ! -x "$BIN" ]; then
    echo "error: $BIN not built. Run scripts/linux/build.sh aae first." >&2
    exit 1
fi

# The binary is BUILT into x64/Release (see CMakeLists.txt) precisely because
# every path resolves relative to the executable's own directory - aae.ini,
# video.ini, roms/, artwork/, hi/ and cfg/ all live beside it. cd there too so
# relative paths on the command line behave as expected.
cd x64/Release || exit 1

if grep -qi microsoft /proc/version 2>/dev/null && [ -e /dev/dxg ]; then
    # An already-set GALLIUM_DRIVER wins. WSLg intermittently fails to PRESENT
    # the d3d12 buffer: AAE creates the window, the context comes up on real
    # hardware, X reports the window IsViewable and frames are swapped - and
    # nothing appears on screen (WSLg shows its own "COPY MODE" banner; that
    # string is not in the AAE source). Dropping to the software rasteriser
    # brings the window back, slowly, which is the difference between "cannot
    # see it at all" and "can see it while I work". Escape hatch:
    #
    #     GALLIUM_DRIVER=llvmpipe bash scripts/linux/run.sh <game>
    #
    # Environmental, and moot on the Steam Machine and Pi, where Mesa picks the
    # native driver and the compositor presents normally.
    if [ -n "$GALLIUM_DRIVER" ]; then
        echo "WSL detected - honouring GALLIUM_DRIVER=$GALLIUM_DRIVER as set"
    else
        echo "WSL with GPU passthrough detected - selecting the D3D12 Gallium driver"
        export GALLIUM_DRIVER=d3d12
    fi
    export LD_LIBRARY_PATH="/usr/lib/wsl/lib:${LD_LIBRARY_PATH}"
fi

# Audio under WSL.
#
# /dev/snd contains only 'timer' - there is NO PCM device - so alsa_backend's
# snd_pcm_open("default") has nothing to open and cleanly reports no audio.
# WSLg instead exposes a PulseAudio socket, and the ALSA "default" device can
# be routed to it by libasound2-plugins, which ships the pulse PCM plugin and
# an alsa.conf.d drop-in that redirects "default".
#
# So: install the bridge once, and point it at WSLg's socket here.
#     sudo apt install -y libasound2-plugins
#
# None of this applies to SteamOS or a Pi, where a real PCM device exists.
if grep -qi microsoft /proc/version 2>/dev/null && [ -S /mnt/wslg/PulseServer ]; then
    export PULSE_SERVER=unix:/mnt/wslg/PulseServer
    if [ -f /usr/lib/x86_64-linux-gnu/alsa-lib/libasound_module_pcm_pulse.so ]; then
        echo "WSLg PulseAudio detected - ALSA 'default' will route to it"
    else
        echo "WSLg PulseAudio socket found, but the ALSA->Pulse bridge is missing." >&2
        echo "  There will be NO SOUND until you run:" >&2
        echo "    sudo apt install -y libasound2-plugins" >&2
    fi
fi

exec "$BIN" "$@"
