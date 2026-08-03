#!/bin/bash
# Stage a MINIMAL AAE payload for copying to a Raspberry Pi 5.
#
#   wsl -d Ubuntu -- bash /mnt/c/Source2026/AAE_publish/scripts/linux/stage-for-pi.sh /mnt/e/aae-pi
#
# INVOKE FROM POWERSHELL, AND PASS AN EXPLICIT /home/... PATH.
#
# Two different shells mangle this command, in opposite directions:
#   - Git Bash rewrites /mnt/... into its own filesystem namespace before
#     wsl.exe sees it ("no such file or directory" on a path that plainly
#     exists).
#   - PowerShell expands ~ and $HOME against WINDOWS first, so
#         ... stage-for-pi.sh ~/aae-pi-payload
#     arrives as C:\Users\you/aae-pi-payload. rsync then reads "C:" as an SSH
#     host and fails with "Could not resolve hostname c", after creating a
#     literal directory named C:Usersyou in the repo. Write the destination
#     out in full:  /home/you/aae-pi-payload
#     (The flatpak script's header documents the same trap; it has now cost
#     this project time three times.)
#
# WHY STAGE AT ALL
#
# The working tree is ~2.4GB and almost none of it belongs on the Pi: Windows
# .obj/.pdb trees, the cc65 toolchain, NuGet packages, backups, the .git
# history. This copies only what a native Pi build actually needs, which is
# roughly a tenth of that and fits any pen drive comfortably.
#
# WHY NOT JUST git clone
#
# The game data is essentially UNTRACKED in this repo - at the time of writing
# exactly one rom and seven artwork files are in git. A clone would arrive with
# source but no ROMs, no artwork, no samples. So the data has to be copied as
# files regardless, which is why this script handles both halves together.
set -e

DEST="${1:-}"
if [ -z "$DEST" ]; then
    echo "usage: $0 <destination-directory>" >&2
    echo "  e.g. $0 /mnt/e/aae-pi      (a pen drive mounted at E:)" >&2
    echo "       $0 ~/aae-pi-payload   (then rsync/scp it to the Pi)" >&2
    exit 1
fi

SRC="$(cd "$(dirname "$0")/../.." && pwd)"
mkdir -p "$DEST"

echo "=== staging from $SRC"
echo "=== to           $DEST"
echo

# --- Source. Everything the CMake build compiles, minus the Windows build
# --- output trees that live inside aae/ (aae/x64, aae/aae/x64) and are
# --- hundreds of MB of .obj/.pdb the Pi has no use for.
echo "source tree..."
rsync -a --info=stats1 \
    --exclude 'x64/' \
    --exclude '.vs/' \
    "$SRC/aae/" "$DEST/aae/"

cp "$SRC/CMakeLists.txt" "$DEST/"
mkdir -p "$DEST/scripts"
rsync -a "$SRC/scripts/linux/" "$DEST/scripts/linux/"

# tools/ is NOT optional, however Windows-looking it seems. CMakeLists.txt
# declares
#     add_executable(aae_uinput_test EXCLUDE_FROM_ALL tools/linux/uinput_devices.cpp)
# unconditionally under UNIX, and CMake errors at CONFIGURE time on a missing
# source file - even for a target nobody builds. Leaving this out means the Pi
# cannot configure at all, which reads as a broken payload rather than a
# missing directory.
#
# glslc.exe is skipped: it is the vendored WINDOWS shader compiler, useless on
# ARM, and the prebuilt .spv below make it unnecessary anyway.
echo "tools (uinput test harness - CMake needs it to configure)..."
rsync -a --exclude '*.exe' "$SRC/tools/" "$DEST/tools/"

# --- Data. Everything resolves relative to the executable, and CMakeLists
# --- builds into x64/Release, so the data has to sit exactly there.
#
# shaders/vk is INCLUDED on purpose: SPIR-V is architecture-independent, so
# the .spv compiled on Windows run verbatim on ARM. That is what lets the Pi
# build without installing glslc.
echo
echo "game data (roms/artwork/samples/shaders)..."
mkdir -p "$DEST/x64/Release"
for d in roms artwork samples shaders ini pleiads snap; do
    if [ -d "$SRC/x64/Release/$d" ]; then
        rsync -a --info=stats1 "$SRC/x64/Release/$d/" "$DEST/x64/Release/$d/"
    fi
done
for f in aae.ini video.ini; do
    [ -f "$SRC/x64/Release/$f" ] && cp "$SRC/x64/Release/$f" "$DEST/x64/Release/"
done

# --- Deliberately NOT copied: every Windows binary and build artefact. They
# --- are useless on ARM and would only confuse a later "which aae am I
# --- running" question.
rm -f "$DEST/x64/Release/"*.exe \
      "$DEST/x64/Release/"*.dll \
      "$DEST/x64/Release/"*.pdb \
      "$DEST/x64/Release/"*.lib 2>/dev/null || true

echo
echo "=== staged: $(du -sh "$DEST" | cut -f1)"
echo
echo "Next, on the Pi (after copying this directory across):"
echo "    cd <payload>"
echo "    bash scripts/linux/build-pi5.sh --check     # verify Vulkan 1.3 first"
echo "    bash scripts/linux/build-pi5.sh"
