#!/bin/bash
# Release Flatpak: roms/artwork/samples exactly as the public AAE repo ships
# them. Produces aae-release.flatpak - the only bundle fit to share.
#   wsl -d Ubuntu -- bash /mnt/c/Source2026/AAE_publish/scripts/linux/build-flatpak-release.sh
exec bash "$(dirname "$0")/build-flatpak.sh" --release
