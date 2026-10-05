#!/bin/bash
# Fill <dest-dir>/roms, artwork and samples for a release or a testing build.
#
#   bash scripts/linux/export-game-data.sh release <dest-dir>
#   bash scripts/linux/export-game-data.sh testing <dest-dir>
#
# Used by build-flatpak.sh and stage-for-pi.sh.
#
#   release - exactly what the PUBLIC repo (github.com/tcottrill/AAE) ships in
#             those three folders. Anything handed to other people must be
#             this: the local x64/Release holds the developer's full rom
#             collection, which is not redistributable.
#   testing - everything in this working tree's x64/Release, for the
#             developer's own machines.
#
# Either way each <dest>/<dir> is mirrored with --delete, so it ends up
# identical to its source and a switch between modes leaves nothing behind.
#
# Release uses a shallow, sparse, blob-filtered clone that fetches just those
# folders (a few tens of MB, not the whole repo). It lives in $HOME, off
# /mnt/c (drvfs is slow for git's many small writes), and is refreshed every
# run so a release always tracks the public repo's current main.
set -e

MODE="$1"
DEST="$2"
if [ "$MODE" != release ] && [ "$MODE" != testing ] || [ -z "$DEST" ]; then
    echo "usage: $0 <release|testing> <dest-dir>" >&2
    exit 1
fi

REPO_DIR="$(cd "$(dirname "$0")/../.." && pwd)"

if [ "$MODE" = release ]; then
    PUBLIC_URL="https://github.com/tcottrill/AAE.git"
    PUBLIC_SRC="$HOME/aae-public"
    echo "=== game data: RELEASE (public repo $PUBLIC_URL) ==="
    if [ -d "$PUBLIC_SRC/.git" ]; then
        git -C "$PUBLIC_SRC" fetch --depth 1 origin main
        git -C "$PUBLIC_SRC" reset --hard FETCH_HEAD
    else
        rm -rf "$PUBLIC_SRC"
        git clone --depth 1 --filter=blob:none --sparse --branch main "$PUBLIC_URL" "$PUBLIC_SRC"
    fi
    git -C "$PUBLIC_SRC" sparse-checkout set \
        x64/Release/roms x64/Release/artwork x64/Release/samples
    echo "    public repo at $(git -C "$PUBLIC_SRC" rev-parse --short HEAD)"
    SRC_DATA="$PUBLIC_SRC/x64/Release"
else
    echo "=== game data: TESTING (local $REPO_DIR/x64/Release - NOT redistributable) ==="
    SRC_DATA="$REPO_DIR/x64/Release"
fi

mkdir -p "$DEST"
for d in roms artwork samples; do
    rsync -a --delete "$SRC_DATA/$d/" "$DEST/$d/"
    echo "    $d: $(ls "$DEST/$d" | wc -l) files, $(du -sh "$DEST/$d" | cut -f1)"
done
