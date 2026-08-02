#!/bin/bash
# Native AAE build for the Raspberry Pi 5 (aarch64, Raspberry Pi OS).
#
# Run this ON THE PI - not cross-compiled, not copied from another machine.
# Same reason the SteamOS target uses Flatpak: a binary built elsewhere links
# against that machine's glibc. Here we simply build natively, which the Pi is
# perfectly capable of.
#
#   bash scripts/linux/build-pi5.sh            # prereq check + build
#   bash scripts/linux/build-pi5.sh --check    # prereq check only, no build
#
# WHY VULKAN IS NOT OPTIONAL ON THIS TARGET
#
# The GL chain's shaders are "#version 330 core", i.e. GL 3.3. Mesa's v3d
# driver on the Pi tops out around GL/GLES 3.1, so the OpenGL renderer CANNOT
# run here - it is not a slower fallback, it is a non-starter. That is the
# entire reason the Vulkan chain exists. If the Vulkan checks below fail, the
# emulator has no working renderer on this machine.
#
# THE ONE HARD REQUIREMENT: VULKAN 1.3
#
# sys_vk.cpp requests apiVersion = VK_API_VERSION_1_3 and REJECTS any physical
# device reporting less (PickPhysicalDevice), because the whole chain is built
# on two 1.3 core features: dynamicRendering (no VkRenderPass/VkFramebuffer
# objects anywhere) and synchronization2 (every barrier uses the *2 structs).
# There is deliberately no 1.2 fallback path.
#
# Mesa's v3dv reached Vulkan 1.3 on the Pi 5 in the Mesa 24.x era. A Pi OS
# image older than that will report 1.2 and the app will exit with
# "no suitable physical device". The fix is to update Mesa, not to patch AAE.
set -e

MODE="${1:-build}"
REPO_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_DIR"

fail=0
say()  { printf '%s\n' "$*"; }
ok()   { printf '  OK    %s\n' "$*"; }
bad()  { printf '  FAIL  %s\n' "$*"; fail=1; }
warn() { printf '  WARN  %s\n' "$*"; }

say "=== AAE Pi 5 prerequisite check ==="

# --- Architecture. Not fatal (this script works on any aarch64 Linux), just
# --- loud, because "why is my build 20x slower" is usually "it is emulated".
case "$(uname -m)" in
    aarch64) ok "architecture: aarch64" ;;
    *)       warn "architecture is $(uname -m), expected aarch64 - is this really the Pi?" ;;
esac

# --- Toolchain.
command -v g++   >/dev/null && ok "g++   $(g++ -dumpversion)"   || bad "g++ missing:   sudo apt install build-essential"
command -v cmake >/dev/null && ok "cmake $(cmake --version | head -1 | cut -d' ' -f3)" || bad "cmake missing: sudo apt install cmake"

# --- Vulkan loader + headers. The headers are VENDORED in
# --- system/3rdparty/vulkan, so libvulkan-dev is NOT required; only the
# --- runtime loader is, and it is dlopen'd (never linked).
if ls /usr/lib/*/libvulkan.so.1 >/dev/null 2>&1 || ls /usr/lib/libvulkan.so.1 >/dev/null 2>&1; then
    ok "libvulkan.so.1 present (dlopen'd at runtime, never linked)"
else
    bad "libvulkan.so.1 missing: sudo apt install libvulkan1"
fi

# --- THE gate: does the driver actually expose Vulkan 1.3?
if command -v vulkaninfo >/dev/null 2>&1; then
    api="$(vulkaninfo --summary 2>/dev/null | grep -m1 -oE 'apiVersion[^0-9]*[0-9]+\.[0-9]+\.[0-9]+' | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' || true)"
    drv="$(vulkaninfo --summary 2>/dev/null | grep -m1 -E 'deviceName' | cut -d= -f2- | sed 's/^ *//' || true)"
    [ -n "$drv" ] && say "  device: $drv"
    if [ -n "$api" ]; then
        major="${api%%.*}"; rest="${api#*.}"; minor="${rest%%.*}"
        if [ "$major" -gt 1 ] || { [ "$major" -eq 1 ] && [ "$minor" -ge 3 ]; }; then
            ok "Vulkan $api (>= 1.3 required)"
        else
            bad "Vulkan $api - AAE requires 1.3 (dynamicRendering + synchronization2)."
            bad "  Update Mesa/Pi OS. There is no 1.2 fallback, and the GL chain"
            bad "  cannot run on v3d either (needs GL 3.3, v3d gives ~3.1)."
        fi
    else
        warn "could not parse a Vulkan version from vulkaninfo - check manually:"
        warn "  vulkaninfo --summary | grep -i apiversion"
    fi
else
    warn "vulkaninfo not installed - cannot verify the 1.3 requirement up front."
    warn "  sudo apt install vulkan-tools   (strongly recommended before building)"
fi

# --- X11 + GL libs. GLEW/GL are still needed to LINK even in a Vulkan-only
# --- run: the GL chain is compiled in regardless, it is simply never called.
for pkg in "GL:libgl1-mesa-dev" "GLEW:libglew-dev" "X11:libx11-dev" "asound:libasound2-dev"; do
    lib="${pkg%%:*}"; deb="${pkg##*:}"
    if ls /usr/lib/*/lib${lib}.so >/dev/null 2>&1 || ls /usr/lib/lib${lib}.so >/dev/null 2>&1; then
        ok "lib${lib} dev present"
    else
        bad "lib${lib} dev missing: sudo apt install ${deb}"
    fi
done

# --- Shader compiler. NOT required: SPIR-V is architecture-independent, so the
# --- .spv already in x64/Release/shaders/vk (compiled on any platform) work
# --- here verbatim. glslc only matters if you intend to EDIT shaders on the Pi.
if command -v glslc >/dev/null 2>&1; then
    ok "glslc present - shaders will be recompiled from source"
elif ls x64/Release/shaders/vk/*.spv >/dev/null 2>&1; then
    ok "no glslc, but prebuilt .spv found ($(ls x64/Release/shaders/vk/*.spv | wc -l) files) - SPIR-V is portable, fine"
else
    bad "no glslc AND no prebuilt shaders/vk/*.spv - the Vulkan chain cannot create pipelines."
    bad "  Either: sudo apt install glslc   or copy x64/Release/shaders/vk from a built tree."
fi

if [ "$fail" -ne 0 ]; then
    say ""
    say "=== prerequisites FAILED - fix the above before building ==="
    exit 1
fi
say "=== prerequisites OK ==="

[ "$MODE" = "--check" ] && exit 0

# -----------------------------------------------------------------------------
# Build. Note --target aae: the default target would also build aae_audiotest,
# which hard-errors without ALSA, and CMakeLists defines no install() rules so
# a plain `cmake --build` install step would be a no-op.
#
# The binary lands in x64/Release (RUNTIME_OUTPUT_DIRECTORY), beside aae.ini,
# video.ini, roms/ and artwork/ - everything resolves relative to the
# executable, so it must live there.
# -----------------------------------------------------------------------------
say ""
say "=== configuring ==="
cmake -S . -B build-pi5 -DCMAKE_BUILD_TYPE=Release

say ""
say "=== building (this takes a while on a Pi) ==="
cmake --build build-pi5 --target aae -j"$(nproc)"

say ""
say "=== done ==="
say "Binary: $REPO_DIR/x64/Release/aae"
say ""
say "Run it (Vulkan is the default renderer):"
say "    cd $REPO_DIR/x64/Release && ./aae asteroid"
say ""
say "If it fails to start, the log says why:"
say "    grep -E 'PickPhysicalDevice|chain online|ERROR' $REPO_DIR/x64/Release/systemlog.txt"
