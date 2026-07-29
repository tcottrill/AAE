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
for d in roms artwork samples snap pleiads; do
    if [ -d "$APP_DATA/$d" ]; then
        ln -sfn "$APP_DATA/$d" "$DATA_DIR/$d"
    fi
done

# Writable state: create empty, and never overwrite what is already there -
# these hold the user's hiscores, key bindings and NVRAM.
mkdir -p "$DATA_DIR/hi" "$DATA_DIR/cfg" "$DATA_DIR/nv"

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
