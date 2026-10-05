#!/bin/bash
# Testing Pi payload: everything in the local x64/Release (full rom
# collection), for your own Pi. Extra options (--force-config, --prune) pass
# through.
#   wsl -d Ubuntu -- bash /mnt/c/Source2026/AAE_publish/scripts/linux/stage-for-pi-testing.sh /mnt/e/aae-pi
exec bash "$(dirname "$0")/stage-for-pi.sh" --testing "$@"
