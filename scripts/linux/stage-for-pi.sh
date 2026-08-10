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

DEST=""
FORCE_CONFIG=0
PRUNE_DATA=0
for arg in "$@"; do
    case "$arg" in
        --force-config) FORCE_CONFIG=1 ;;
        --prune)        PRUNE_DATA=1 ;;
        -*)             echo "unknown option: $arg" >&2; exit 1 ;;
        *)              DEST="$arg" ;;
    esac
done

if [ -z "$DEST" ]; then
    echo "usage: $0 <destination-directory> [--force-config] [--prune]" >&2
    echo "  e.g. $0 /home/you/aae-pi-payload   (then copy it to the Pi)" >&2
    echo "       $0 /mnt/e/aae-pi              (a pen drive mounted at E:)" >&2
    echo >&2
    echo "  Re-staging PRESERVES the target's aae.ini, video.ini and ini/ -" >&2
    echo "  those hold tuning done on that machine (Pi glow values especially)." >&2
    echo "  --force-config overwrites them with this machine's config instead." >&2
    echo >&2
    echo "  --prune deletes roms/artwork/samples the source tree no longer has." >&2
    echo "  Without it those are reported and kept, because roms and artwork may" >&2
    echo "  have been added ON the target." >&2
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
#
# --delete on all three source transfers, unconditionally: nobody edits source
# on the target, so this tree is authoritative. Without it a header deleted or
# renamed here lingers on the payload forever and stays on the include path,
# where it silently shadows the real one - the worst kind of stale file,
# because the build still succeeds. The x64/ and .vs/ excludes are protected
# from deletion too (rsync never deletes what it was told to ignore).
echo "source tree..."
rsync -a --delete --info=stats1 \
    --exclude 'x64/' \
    --exclude '.vs/' \
    "$SRC/aae/" "$DEST/aae/"

cp "$SRC/CMakeLists.txt" "$DEST/"
mkdir -p "$DEST/scripts"
rsync -a --delete "$SRC/scripts/linux/" "$DEST/scripts/linux/"

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
rsync -a --delete --exclude '*.exe' "$SRC/tools/" "$DEST/tools/"

# --- Data. Everything resolves relative to the executable, and CMakeLists
# --- builds into x64/Release, so the data has to sit exactly there.
#
# shaders/vk is INCLUDED on purpose: SPIR-V is architecture-independent, so
# the .spv compiled on Windows run verbatim on ARM. That is what lets the Pi
# build without installing glslc.
echo
# Data is NOT pruned by default, and that asymmetry with the source tree above
# is deliberate: roms and artwork are exactly the things a user adds ON the
# target, and a refresh that deleted them would be unforgivable. So extras are
# counted and reported, and --prune is what actually removes them.
#
# The cost of not pruning is that a payload accumulates: re-staging after the
# tree's data set was trimmed left a 357M payload carrying 279M of artwork
# against a source tree holding 12M. The report below is what makes that
# visible instead of silent.
echo "game data (roms/artwork/samples/shaders)..."
mkdir -p "$DEST/x64/Release"
STALE_TOTAL=0
for d in roms artwork samples shaders pleiads snap; do
    if [ -d "$SRC/x64/Release/$d" ]; then
        if [ "$PRUNE_DATA" = "1" ]; then
            rsync -a --delete --info=stats1 \
                "$SRC/x64/Release/$d/" "$DEST/x64/Release/$d/"
        else
            rsync -a --info=stats1 "$SRC/x64/Release/$d/" "$DEST/x64/Release/$d/"
            # -n is a dry run, so this only counts what --delete WOULD remove.
            # Run after the real transfer, so every remaining candidate is a
            # file the destination has and this tree does not.
            stale=$(rsync -ain --delete \
                "$SRC/x64/Release/$d/" "$DEST/x64/Release/$d/" \
                | grep -c '^\*deleting' || true)
            if [ "$stale" -gt 0 ]; then
                echo "  note: $d/ has $stale file(s) not in this tree (kept)"
                STALE_TOTAL=$((STALE_TOTAL + stale))
            fi
        fi
    fi
done

# --- Config is PRESERVED on re-stage, unlike everything above.
#
# The target's settings are not a copy of the dev box's - they are tuning done
# ON that machine, for that machine, and they are not reproducible here. The
# Pi's GPU needs its own glow values in particular: the dual-filter pyramid
# quantizes to 8 bits at every level and then multiplies by glow2_gain (10 by
# default) on the final pass, so a single code of per-GPU rounding difference
# lands as ~4% of full scale in the output. Values tuned on desktop NVIDIA/AMD
# are not the right values on the Pi's v3d.
#
# The menu writes glow2_* into video.ini (menu.cpp, my_set_config_float with
# vidPath) and per-game settings into ini/, so a plain overwrite here destroyed
# exactly the work the user had just done on the target - silently, and on
# every payload refresh, which makes tuning feel impossible rather than merely
# lost.
#
# So: seed these when absent, never clobber them. Pass --force-config to push
# the dev box's config over the target's on purpose.
if [ "$FORCE_CONFIG" = "1" ]; then
    echo "config (--force-config: OVERWRITING the target's settings)..."
    [ -d "$SRC/x64/Release/ini" ] && rsync -a "$SRC/x64/Release/ini/" "$DEST/x64/Release/ini/"
    for f in aae.ini video.ini; do
        [ -f "$SRC/x64/Release/$f" ] && cp "$SRC/x64/Release/$f" "$DEST/x64/Release/"
    done
else
    echo "config (seeding only - the target's own tuning is preserved)..."
    [ -d "$SRC/x64/Release/ini" ] && rsync -a --ignore-existing "$SRC/x64/Release/ini/" "$DEST/x64/Release/ini/"
    for f in aae.ini video.ini; do
        if [ -f "$SRC/x64/Release/$f" ] && [ ! -f "$DEST/x64/Release/$f" ]; then
            cp "$SRC/x64/Release/$f" "$DEST/x64/Release/"
        elif [ -f "$DEST/x64/Release/$f" ]; then
            echo "  kept existing $f"
        fi
    done
fi

# --- Deliberately NOT copied: every Windows binary and build artefact. They
# --- are useless on ARM and would only confuse a later "which aae am I
# --- running" question.
rm -f "$DEST/x64/Release/"*.exe \
      "$DEST/x64/Release/"*.dll \
      "$DEST/x64/Release/"*.pdb \
      "$DEST/x64/Release/"*.lib 2>/dev/null || true

echo
echo "=== staged: $(du -sh "$DEST" | cut -f1)"
if [ "$STALE_TOTAL" -gt 0 ]; then
    echo "=== $STALE_TOTAL data file(s) on the payload are not in this tree."
    echo "    Added on the target, or left by an earlier stage - this cannot"
    echo "    tell which. Re-run with --prune to delete them."
fi
echo
echo "Next, on the Pi (after copying this directory across):"
echo "    cd <payload>"
echo "    bash scripts/linux/build-pi5.sh --check     # verify Vulkan 1.3 first"
echo "    bash scripts/linux/build-pi5.sh"
