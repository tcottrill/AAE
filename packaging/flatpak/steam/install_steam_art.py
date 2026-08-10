#!/usr/bin/env python3
"""Install AAE's Steam library artwork for an existing non-Steam shortcut.

Steam has no mechanism for an application to supply its own Game Mode art -
the library reads per-user files from userdata/<uid>/config/grid/, named by
the SHORTCUT's appid. So the user first adds AAE to Steam ("Add to Steam" in
Desktop Mode), and this script then:

  1. finds every Steam user's shortcuts.vdf,
  2. locates the AAE shortcut in it and reads its REAL appid (no CRC
     recomputation - third-party tools recompute crc32(exe+name) and break
     the moment the shortcut's fields differ),
  3. copies the four images into that user's grid/ under the right names:
         <appid>p.png       portrait capsule (library grid tile)
         <appid>.png        wide capsule (header)
         <appid>_hero.png   page banner
         <appid>_logo.png   logo overlaid on the hero
         <appid>.json       logo position (centred on the hero)

UPDATE POLICY - ours is refreshed, theirs is sacred. A record of the hash of
every file this installer writes is kept in AAE's data directory. On a later
run, a grid file that still matches what we installed is OURS and is replaced
by newer shipped art, so pushing new artwork in an app update reaches every
user automatically. A file matching neither the recorded nor the shipped hash
was changed by the USER - custom art, or a logo position they dragged (Steam
rewrites the json for that) - and is left alone. --force replaces everything
regardless, record or none. Run from the flatpak:

    flatpak run io.github.tcottrill.AAE --install-steam-art [--force]
"""
import hashlib
import json
import os
import struct
import sys

ART_DIR = "/app/share/aae-steam"
ART = {
    "capsule_portrait.png": "{id}p.png",
    "capsule_wide.png": "{id}.png",
    "hero.png": "{id}_hero.png",
    "logo.png": "{id}_logo.png",
}
MATCH = "io.github.tcottrill.aae"   # how the shortcut references us

# The logo-position payload (see the module docstring).
LOGO_POS = ('{"nVersion":1,"logoPosition":'
            '{"pinnedPosition":"CenterCenter","nWidthPct":55,"nHeightPct":60}}')

# Where the installed-hash record lives. Inside the sandbox XDG_DATA_HOME is
# the flatpak's own data directory, so this sits beside aae.ini and dies with
# `flatpak uninstall --delete-data` - after which every grid file reads as
# "not ours" and is preserved, which is the safe direction to fail.
RECORD = os.path.join(
    os.environ.get("AAE_DATA_DIR")
    or os.path.join(os.environ.get("XDG_DATA_HOME")
                    or os.path.expanduser("~/.local/share"), "aae"),
    "steam-art-installed.json")


def sha256_bytes(b):
    return hashlib.sha256(b).hexdigest()


def load_record():
    try:
        with open(RECORD) as f:
            return json.load(f)
    except Exception:
        return {}


def save_record(rec):
    try:
        os.makedirs(os.path.dirname(RECORD), exist_ok=True)
        with open(RECORD, "w") as f:
            json.dump(rec, f, indent=1, sort_keys=True)
    except Exception as e:
        print(f"aae: warning - could not save install record ({e})")


def parse_shortcuts(data):
    """Parse binary VDF into a list of shortcut dicts (strings + ints only)."""
    pos = [0]

    def read_cstr():
        end = data.index(b"\x00", pos[0])
        s = data[pos[0]:end].decode("utf-8", "replace")
        pos[0] = end + 1
        return s

    def read_map():
        out = {}
        while True:
            if pos[0] >= len(data):
                # Root map: some writers end the file without the final 0x08.
                return out
            t = data[pos[0]]
            pos[0] += 1
            if t == 0x08:               # end of map
                return out
            key = read_cstr()
            if t == 0x00:               # nested map
                out[key.lower()] = read_map()
            elif t == 0x01:             # string
                out[key.lower()] = read_cstr()
            elif t == 0x02:             # int32
                out[key.lower()] = struct.unpack_from("<i", data, pos[0])[0]
                pos[0] += 4
            else:
                raise ValueError(f"unknown VDF type 0x{t:02x} at {pos[0] - 1}")

    root = read_map()
    shortcuts = root.get("shortcuts", {})
    return list(shortcuts.values())


def find_aae(entries):
    for e in entries:
        hay = " ".join(str(e.get(k, "")) for k in
                       ("exe", "launchoptions", "appname")).lower()
        if MATCH in hay:
            return e
    return None


def main():
    force = "--force" in sys.argv

    steam_root = None
    for cand in (os.path.expanduser("~/.local/share/Steam"),
                 os.path.expanduser("~/.steam/steam")):
        if os.path.isdir(os.path.join(cand, "userdata")):
            steam_root = cand
            break
    if not steam_root:
        print("aae: no Steam installation found (looked for userdata/ under "
              "~/.local/share/Steam and ~/.steam/steam)")
        print("aae: if Steam IS installed, the flatpak may lack filesystem "
              "access - check: flatpak info --show-permissions io.github.tcottrill.AAE")
        return 1

    installed = 0
    for uid in sorted(os.listdir(os.path.join(steam_root, "userdata"))):
        vdf = os.path.join(steam_root, "userdata", uid, "config", "shortcuts.vdf")
        if not os.path.isfile(vdf):
            continue
        try:
            entries = parse_shortcuts(open(vdf, "rb").read())
        except Exception as e:
            print(f"aae: user {uid}: could not parse shortcuts.vdf ({e}) - skipped")
            continue

        entry = find_aae(entries)
        if not entry:
            print(f"aae: user {uid}: no AAE shortcut - add AAE to Steam first "
                  "(Desktop Mode: right-click aae in the app grid, Add to Steam)")
            continue
        appid = entry.get("appid")
        if not appid:
            print(f"aae: user {uid}: shortcut has no appid field - Steam too old?")
            continue
        appid &= 0xFFFFFFFF

        grid = os.path.join(steam_root, "userdata", uid, "config", "grid")
        os.makedirs(grid, exist_ok=True)
        record = load_record()
        wrote, fresh, custom = [], [], []

        # The four images plus the logo-position json, one policy for all:
        # write when missing, refresh when unchanged-since-we-wrote-it, keep
        # when the user has replaced or edited it (--force overrides).
        payloads = [(pat.format(id=appid),
                     open(os.path.join(ART_DIR, src), "rb").read())
                    for src, pat in ART.items()]
        payloads.append((f"{appid}.json", LOGO_POS.encode()))

        for name, payload in payloads:
            dst = os.path.join(grid, name)
            key = f"{uid}/{name}"
            new_hash = sha256_bytes(payload)
            if os.path.exists(dst) and not force:
                cur = sha256_bytes(open(dst, "rb").read())
                if cur == new_hash:
                    fresh.append(name)          # already current
                    record[key] = new_hash
                    continue
                if cur != record.get(key):
                    custom.append(name)         # the user's - hands off
                    continue
            with open(dst, "wb") as f:          # missing, ours-but-old, or forced
                f.write(payload)
            record[key] = new_hash
            wrote.append(name)

        save_record(record)
        parts = [f"aae: user {uid}: shortcut appid {appid}"]
        if wrote:
            parts.append(f"installed {', '.join(wrote)}")
        if fresh:
            parts.append(f"{len(fresh)} already current")
        if custom:
            parts.append(f"kept user-modified {', '.join(custom)}")
        print(", ".join(parts))
        installed += 1

    if installed:
        print("aae: done - restart Steam (or the Deck) to see the artwork")
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
