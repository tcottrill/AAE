#!/bin/bash
# AAE Flatpak launcher.
#
# Everything in AAE resolves relative to the executable's directory, which is
# right for an unzip-and-run layout and impossible inside a Flatpak: /app is
# READ-ONLY, and AAE writes hi/ (hiscores), cfg/ (per-game input config), nv/
# (NVRAM), aae.ini and systemlog.txt at runtime.
#
# So this seeds a writable directory under $XDG_DATA_HOME and points
# AAE_DATA_DIR at it (see exe_dir() in system/util/path_helper.cpp). The bulky
# read-only payload is SYMLINKED rather than copied - artwork alone is 279MB,
# and copying it per user would be absurd - while the few hundred KB that
# actually change are real files.
#
# Symlinks are transparent here because AAE opens these paths with ordinary
# fopen/miniz calls. It is only /proc/self/exe that resolves through symlinks,
# which is precisely why AAE_DATA_DIR exists rather than us symlinking the
# binary and hoping.
set -e

DATA_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/aae"
APP_DATA="/app/share/aae"

mkdir -p "$DATA_DIR"

# Read-only payload: link, never copy. Refreshed every launch so an app update
# that adds a rom set or new artwork is picked up without the user clearing
# anything.
#
# shaders/ is the Vulkan SPIR-V (Phase 4b). The VK chain loads
# "shaders/vk/<name>.spv" with plain relative fopen, which resolves against
# the working directory - and we cd to $DATA_DIR below - so the link is what
# makes renderer=vulkan (the default) find its pipelines at all.
for d in pleiads shaders; do
    if [ -d "$APP_DATA/$d" ]; then
        ln -sfn "$APP_DATA/$d" "$DATA_DIR/$d"
    fi
done

# roms/, artwork/ and samples/ get the same zero-copy treatment but ONE LEVEL
# DOWN: a real writable directory holding a symlink per shipped file, rather
# than a symlink to the whole directory.
#
# The distinction is the difference between a usable install and a dead end.
# Linking the directory itself makes it read-only, because it lands in /app -
# so adding a rom set or an artwork pack meant rebuilding and reinstalling the
# whole 42MB bundle. Linking the CONTENTS costs the same nothing (the payload
# is still stored exactly once, in /app) while leaving the directory itself
# writable, so new files can simply be dropped in beside the shipped ones:
#
#   ~/.var/app/io.github.tcottrill.AAE/data/aae/roms/
#
# That path is ordinary $HOME from the host's point of view - a file manager,
# scp or a USB stick all reach it with no sandbox permission of any kind.
#
# For a collection too large to sit there, aae.ini's mame_rom_path and
# mame_artwork_path take an absolute path instead; --filesystem in the manifest
# is what lets the sandbox read it.
for d in roms artwork samples; do
    [ -d "$APP_DATA/$d" ] || continue
    mkdir -p "$DATA_DIR/$d"

    # Clear links left by a previous version of the payload before relinking.
    # -xtype l matches only symlinks whose target is GONE, so a rom the user
    # added themselves is a regular file and can never be caught by this;
    # without it, a file dropped from the bundle would linger for ever as a
    # dangling entry that the game list still offers and no game can load.
    find "$DATA_DIR/$d" -maxdepth 1 -xtype l -delete 2>/dev/null || true

    # NO -f here, and that is the whole point: plain `ln -s` refuses to replace
    # an existing entry, so a real file the user dropped in always wins over the
    # shipped one of the same name. That is how a better dump or a custom
    # overlay gets substituted; relinking over it every launch would make the
    # substitution look as though it had been ignored. The failure for
    # already-linked files is expected every launch after the first, which is
    # what the redirect and the `|| true` are absorbing.
    for f in "$APP_DATA/$d"/*; do
        [ -e "$f" ] || continue
        ln -s "$f" "$DATA_DIR/$d/$(basename "$f")" 2>/dev/null || true
    done
done

# Writable state: create empty, and never overwrite what is already there -
# these hold the user's hiscores, key bindings and NVRAM.
#
# snap/ is in THIS list, not the symlink loop above, because it is an OUTPUT
# directory - the screenshot key writes into it. Linking it at /app/share/aae
# made every screenshot fail silently against the read-only sandbox, which is
# indistinguishable from the key not being bound.
mkdir -p "$DATA_DIR/hi" "$DATA_DIR/cfg" "$DATA_DIR/nv" "$DATA_DIR/snap"

# ini/ ships defaults but per-game overrides are written into it, so it is
# copied on first run rather than linked. -n so a re-launch never clobbers
# edits the user has made.
if [ -d "$APP_DATA/ini" ]; then
    mkdir -p "$DATA_DIR/ini"
    cp -rn "$APP_DATA/ini/." "$DATA_DIR/ini/" 2>/dev/null || true
fi

# aae.ini and video.ini are user-editable settings. Same rule: seed once,
# never overwrite. video.ini in particular drives flipping, resizing AND
# window location together, so silently replacing it would reset the display
# every update.
for f in aae.ini video.ini; do
    if [ -f "$APP_DATA/$f" ] && [ ! -f "$DATA_DIR/$f" ]; then
        cp "$APP_DATA/$f" "$DATA_DIR/$f"
    fi
done

export AAE_DATA_DIR="$DATA_DIR"

# A short path to the log, in the user's home.
#
# The real one lives at
#   ~/.var/app/io.github.tcottrill.AAE/data/aae/systemlog.txt
# which is miserable to type on a machine where you are working directly at
# the console rather than pasting. The symlink costs nothing and turns every
# diagnostic request into "grep something ~/aae.log".
ln -sfn "$DATA_DIR/systemlog.txt" "$HOME/aae.log" 2>/dev/null || true

# --diag prints the lines worth reporting and exits:
#
#   flatpak run io.github.tcottrill.AAE --diag
#
# Handled HERE, before the input warnings below, so its output is clean. Those
# warnings go to stderr and were landing in the middle of the report, which
# meant reading it required knowing to append 2>/dev/null - exactly the sort of
# extra thing this is meant to save.
if [ "$1" = "--diag" ]; then
    LOG="$DATA_DIR/systemlog.txt"
    if [ ! -f "$LOG" ]; then
        echo "no log yet at $LOG - run a game first"
        exit 1
    fi
    echo "=========== AAE diagnostics ==========="
    grep -E "built |ALSA:|ALSA streaming|Shutdown:|Frame pacing|evdev: .*(keyboard|mouse|gamepad) " "$LOG" \
        | sed -E 's/^[A-Z]+ +\([^)]*\) - //'
    echo "======================================="
    exit 0
fi

# cd there too: Log::open("systemlog.txt") in linux_main.cpp is relative to the
# working directory, not to AAE_DATA_DIR, so without this the log write fails
# silently in a read-only cwd.
cd "$DATA_DIR"

# Input diagnostics. Flatpak needs --device=input to expose /dev/input at all,
# and on a distro where the user is not in the 'input' group the backend finds
# devices it cannot open. Both produce "no input works", so say which up front
# rather than leaving it to the log.
if [ ! -d /dev/input ]; then
    echo "aae: WARNING - /dev/input is not visible inside the sandbox." >&2
    echo "aae:   The flatpak needs --device=input. Check with:" >&2
    echo "aae:   flatpak info --show-permissions io.github.tcottrill.AAE" >&2
elif ! ls /dev/input/event* >/dev/null 2>&1; then
    echo "aae: WARNING - /dev/input exists but contains no event devices." >&2
fi

exec /app/bin/aae "$@"
