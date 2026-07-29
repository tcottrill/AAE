#!/bin/bash
# Build the AAE Flatpak and export a single-file bundle for the Steam Machine.
#
#   wsl -d Ubuntu -- bash /mnt/c/Source2026/AAE_publish/scripts/linux/build-flatpak.sh
#
# Produces aae.flatpak in the repo root. On the target:
#   flatpak install --user aae.flatpak
#   flatpak run io.github.tcottrill.AAE
#
# TWO THINGS THIS SCRIPT EXISTS FOR
#
# 1. --disable-rofiles-fuse. flatpak-builder normally exposes the build
#    directory through rofiles-fuse, a FUSE overlay that makes the source
#    read-only. That cannot mount here:
#        fusermount3: mounting over filesystem type 0x01021997 is forbidden
#        Error: Failure spawning rofiles-fuse
#    0x01021997 is TMPFS_MAGIC. Under WSL the mount simply is not permitted.
#    The flag skips the overlay; the only thing lost is a safety net against a
#    build scribbling on its own sources, which ours does not do.
#
# 2. Building OFF /mnt/c. The Windows drvfs mount is slow for the many small
#    file operations a flatpak build makes, and is where the FUSE trouble
#    starts. State goes in $HOME; only the finished bundle is written back to
#    the repo.
#
# The manifest's source paths are relative to the MANIFEST, not to the working
# directory, so building from elsewhere still picks up the right tree.
#
# ALWAYS INVOKE THIS AS A FILE, never by pasting its commands into
# `wsl -- bash -c "..."`. PowerShell expands $HOME before wsl.exe sees it, so
# the build directory became a literal folder named "C:Usersuser9" inside the
# repo and eu-strip failed on the malformed path - a failure that looks like a
# packaging bug and is purely a quoting one. It has now happened three times in
# this project; a script file is immune.
#
# --force-clean is ALWAYS passed and costs nothing. It empties the app
# directory only; the module cache that makes rebuilds fast lives separately in
# .flatpak-builder/cache and is reused regardless. Omitting it does not "keep
# the cache", it just makes flatpak-builder refuse to start:
#     App dir '...' is not empty. Please delete the existing contents or use
#     --force-clean.
set -e

REPO_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
MANIFEST="$REPO_DIR/packaging/flatpak/io.github.tcottrill.AAE.yml"
APP_ID="io.github.tcottrill.AAE"

WORK="$HOME/aae-flatpak"
mkdir -p "$WORK"
cd "$WORK"

if [ ! -f "$MANIFEST" ]; then
    echo "error: manifest not found at $MANIFEST" >&2
    exit 1
fi

# The full build log always lands in the repo, because the interesting line is
# never near the end: a failing compile is followed by pages of warnings, and
# truncating the tail throws away the actual error. (Piping this through
# PowerShell's Select-Object -Last did exactly that once already.)
LOG="$REPO_DIR/flatpak-build.log"

echo "=== building $APP_ID ==="
echo "    manifest: $MANIFEST"
echo "    workdir:  $WORK   (deliberately not on /mnt/c)"
echo "    log:      $LOG"
echo

set +e
flatpak-builder \
    --user \
    --force-clean \
    --disable-rofiles-fuse \
    --repo="$WORK/repo" \
    "$WORK/build" \
    "$MANIFEST" 2>&1 | tee "$LOG"
rc=${PIPESTATUS[0]}
set -e

if [ "$rc" -ne 0 ]; then
    echo
    echo "=== BUILD FAILED (exit $rc) - first real errors ==="
    grep -nE "FAILED:|error:|undefined reference|No such file or directory" "$LOG" | head -20
    echo
    echo "Full log: $LOG"
    exit "$rc"
fi

echo
echo "=== exporting bundle ==="
flatpak build-bundle "$WORK/repo" "$WORK/aae.flatpak" "$APP_ID"

# Copy the finished artefact back to the repo, where Windows can see it.
cp "$WORK/aae.flatpak" "$REPO_DIR/aae.flatpak"

echo
echo "=== done ==="
ls -lh "$REPO_DIR/aae.flatpak"
echo
echo "Install on the Steam Machine with:"
echo "    flatpak install --user aae.flatpak"
echo "    flatpak run $APP_ID"
