#!/bin/bash
# Build AAE on any Linux box (x86_64 or aarch64).
#
#   bash scripts/linux/build-linux.sh            # prereq check + build
#   bash scripts/linux/build-linux.sh --check    # prereq check only
#
# Distro-agnostic: package hints adapt to apt / dnf / pacman / zypper.
#
# For the Raspberry Pi specifically, prefer scripts/linux/build-pi5.sh - same
# build, but it treats Vulkan 1.3 as a HARD requirement, because Mesa's v3d
# driver cannot run the OpenGL chain at all (see the note under "renderers"
# below). On a normal desktop Linux either renderer is viable, so this script
# reports the situation and lets you choose.
#
# WHAT GETS BUILT
#
# The `aae` target only. Two reasons it is named explicitly rather than using
# the default: `aae_audiotest` hard-errors when ALSA headers are absent and
# would fail the whole build over a target nobody asked for, and CMakeLists
# defines no install() rules, so a bare `cmake --build` install step would be
# a no-op that merely looks successful.
#
# The binary lands in x64/Release, NOT in the build directory - CMakeLists
# sets RUNTIME_OUTPUT_DIRECTORY there so the executable sits beside aae.ini,
# video.ini, roms/ and artwork/. Everything in AAE resolves relative to the
# executable, so it has to live with its data.
set -e

MODE="${1:-build}"
REPO_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_DIR"

fail=0
ok()   { printf '  OK    %s\n' "$*"; }
bad()  { printf '  FAIL  %s\n' "$*"; fail=1; }
warn() { printf '  WARN  %s\n' "$*"; }

# --- Package-manager-aware hints, so the FAIL lines are copy-pasteable
# --- instead of "install the GLEW dev package somehow".
if   command -v apt-get >/dev/null 2>&1; then PM="sudo apt install -y"
     P_BUILD="build-essential cmake"; P_GL="libgl1-mesa-dev"; P_GLEW="libglew-dev"
     P_X11="libx11-dev"; P_ALSA="libasound2-dev"; P_VK="libvulkan1"; P_VKTOOL="vulkan-tools"
elif command -v dnf     >/dev/null 2>&1; then PM="sudo dnf install -y"
     P_BUILD="gcc-c++ make cmake"; P_GL="mesa-libGL-devel"; P_GLEW="glew-devel"
     P_X11="libX11-devel"; P_ALSA="alsa-lib-devel"; P_VK="vulkan-loader"; P_VKTOOL="vulkan-tools"
elif command -v pacman  >/dev/null 2>&1; then PM="sudo pacman -S --needed"
     P_BUILD="base-devel cmake"; P_GL="mesa"; P_GLEW="glew"
     P_X11="libx11"; P_ALSA="alsa-lib"; P_VK="vulkan-icd-loader"; P_VKTOOL="vulkan-tools"
elif command -v zypper  >/dev/null 2>&1; then PM="sudo zypper install -y"
     P_BUILD="gcc-c++ make cmake"; P_GL="Mesa-libGL-devel"; P_GLEW="glew-devel"
     P_X11="libX11-devel"; P_ALSA="alsa-devel"; P_VK="libvulkan1"; P_VKTOOL="vulkan-tools"
else PM="(install via your package manager)"
     P_BUILD="gcc/g++ cmake"; P_GL="OpenGL dev"; P_GLEW="GLEW dev"
     P_X11="Xlib dev"; P_ALSA="ALSA dev"; P_VK="vulkan loader"; P_VKTOOL="vulkan-tools"
fi

echo "=== AAE Linux prerequisite check ($(uname -m)) ==="

command -v g++   >/dev/null && ok "g++   $(g++ -dumpversion)" \
                            || bad "g++ missing:   $PM $P_BUILD"
command -v cmake >/dev/null && ok "cmake $(cmake --version | head -1 | cut -d' ' -f3)" \
                            || bad "cmake missing: $PM $P_BUILD"

# --- Link-time dependencies. All of these are needed even for a Vulkan-only
# --- run: the GL chain is compiled into the binary regardless, it is simply
# --- never called when renderer=vulkan.
check_lib() { # <soname-fragment> <package-var> <label>
    if ls /usr/lib/*/lib$1.so /usr/lib/lib$1.so >/dev/null 2>&1; then
        ok "lib$1 dev present"
    else
        bad "lib$1 dev missing: $PM $2"
    fi
}
check_lib GL    "$P_GL"
check_lib GLEW  "$P_GLEW"
check_lib X11   "$P_X11"
check_lib asound "$P_ALSA"

# --- Vulkan. The loader is dlopen'd at runtime and never linked, and the
# --- headers are vendored in system/3rdparty/vulkan, so no -dev package is
# --- required to BUILD. It is only needed to RUN renderer=vulkan.
if ls /usr/lib/*/libvulkan.so.1 /usr/lib/libvulkan.so.1 >/dev/null 2>&1; then
    ok "libvulkan.so.1 present (dlopen'd at runtime, never linked)"
else
    warn "libvulkan.so.1 not found: $PM $P_VK"
    warn "  Builds fine without it; renderer=vulkan will fail at startup."
fi

# --- Which renderers this machine can actually run.
#
# AAE requires Vulkan 1.3 (sys_vk requests it and rejects lower devices - the
# chain is built on dynamicRendering and synchronization2 with no 1.2 path),
# and the GL chain requires GL 3.3 (its shaders are "#version 330 core").
# A box failing BOTH has no working renderer - which is exactly the Raspberry
# Pi situation, where Mesa v3d offers ~GL 3.1.
vk_ok=unknown
if command -v vulkaninfo >/dev/null 2>&1; then
    api="$(vulkaninfo --summary 2>/dev/null | grep -m1 -oE '[0-9]+\.[0-9]+\.[0-9]+' || true)"
    if [ -n "$api" ]; then
        maj="${api%%.*}"; rest="${api#*.}"; min="${rest%%.*}"
        if [ "$maj" -gt 1 ] || { [ "$maj" -eq 1 ] && [ "$min" -ge 3 ]; }; then
            ok "Vulkan $api - renderer=vulkan supported"; vk_ok=yes
        else
            warn "Vulkan $api - below the 1.3 AAE requires; renderer=vulkan will NOT start"; vk_ok=no
        fi
    fi
else
    warn "vulkaninfo not installed ($PM $P_VKTOOL) - cannot verify Vulkan 1.3"
fi

if [ "$vk_ok" = no ]; then
    if command -v glxinfo >/dev/null 2>&1; then
        gl="$(glxinfo 2>/dev/null | grep -m1 'OpenGL core profile version' | grep -oE '[0-9]+\.[0-9]+' || true)"
        [ -n "$gl" ] && warn "  OpenGL core $gl (the GL chain needs 3.3) - set renderer=opengl in aae.ini"
    else
        warn "  Set renderer=opengl in aae.ini [main]; that path needs OpenGL 3.3."
    fi
fi

# --- Shaders. SPIR-V is architecture-independent, so .spv built anywhere run
# --- here; glslc is only needed to rebuild them from source.
if command -v glslc >/dev/null 2>&1; then
    ok "glslc present - shaders compile from source during the build"
elif ls x64/Release/shaders/vk/*.spv >/dev/null 2>&1; then
    ok "no glslc, but $(ls x64/Release/shaders/vk/*.spv | wc -l) prebuilt .spv present (SPIR-V is portable)"
else
    warn "no glslc and no prebuilt shaders/vk/*.spv - renderer=vulkan cannot create pipelines"
fi

if [ "$fail" -ne 0 ]; then
    echo
    echo "=== prerequisites FAILED - install the packages above, then re-run ==="
    exit 1
fi
echo "=== prerequisites OK ==="
[ "$MODE" = "--check" ] && exit 0

echo
echo "=== configuring ==="
GEN=""; command -v ninja >/dev/null 2>&1 && GEN="-G Ninja"
cmake -S . -B build-linux $GEN -DCMAKE_BUILD_TYPE=Release

echo
echo "=== building ==="
cmake --build build-linux --target aae -j"$(nproc)"

echo
echo "=== done ==="
echo "Binary: $REPO_DIR/x64/Release/aae"
echo
echo "Run:  cd $REPO_DIR/x64/Release && ./aae"
echo "      (add a game name, e.g. ./aae asteroid; -renderer opengl to force GL)"
echo "Log:  $REPO_DIR/x64/Release/systemlog.txt"
