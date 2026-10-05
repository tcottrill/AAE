#!/bin/bash
# Testing Flatpak: everything in the local x64/Release (full rom collection).
# Produces aae-testing.flatpak - for your own machines only, never share it.
#   wsl -d Ubuntu -- bash /mnt/c/Source2026/AAE_publish/scripts/linux/build-flatpak-testing.sh
exec bash "$(dirname "$0")/build-flatpak.sh" --testing
