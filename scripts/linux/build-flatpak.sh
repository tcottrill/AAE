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
echo "=== exporting bundle ===" | tee -a "$LOG"

# Appended to the SAME log as the build stage. This was outside the capture
# once, and when build-bundle failed it produced no diagnosable output at all -
# the build looked like it had succeeded right up to the point where no file
# appeared. Every stage that can fail writes to the log.
set +e
flatpak build-bundle "$WORK/repo" "$WORK/aae.flatpak" "$APP_ID" 2>&1 | tee -a "$LOG"
rc=${PIPESTATUS[0]}
set -e
if [ "$rc" -ne 0 ]; then
    echo "=== BUNDLE EXPORT FAILED (exit $rc) - see $LOG ===" >&2
    exit "$rc"
fi

# Copy the finished artefact back to the repo, where Windows can see it.
#
# Via a temporary name and then mv, for two reasons. A direct cp onto the live
# file fails outright if anything on the Windows side has it open - which is
# exactly what happens when someone is copying the previous bundle to a test
# machine while the next one builds, and it aborted two builds that had
# otherwise completely succeeded. And a half-copied 300MB file that looks
# finished is worse than no file: it installs, or seems to, and then behaves
# like a build nobody wrote.
TMP="$REPO_DIR/aae.flatpak.new"
if ! cp "$WORK/aae.flatpak" "$TMP"; then
    echo "error: could not write $TMP - is the destination open elsewhere?" >&2
    echo "       the finished bundle is still at $WORK/aae.flatpak" >&2
    exit 1
fi
if ! mv -f "$TMP" "$REPO_DIR/aae.flatpak"; then
    echo "error: could not replace $REPO_DIR/aae.flatpak (file in use?)." >&2
    echo "       the new bundle is at $TMP - rename it when the file is free." >&2
    exit 1
fi

echo
echo "=== done ==="
ls -lh "$REPO_DIR/aae.flatpak"
echo
echo "Install on the Steam Machine with:"
echo "    flatpak install --user aae.flatpak"
echo "    flatpak run $APP_ID"
