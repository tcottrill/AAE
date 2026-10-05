#!/bin/bash
# Release Pi payload: roms/artwork/samples exactly as the public AAE repo
# ships them. Extra options (--force-config, --prune) pass through.
#   wsl -d Ubuntu -- bash /mnt/c/Source2026/AAE_publish/scripts/linux/stage-for-pi-release.sh /mnt/e/aae-pi
exec bash "$(dirname "$0")/stage-for-pi.sh" --release "$@"
