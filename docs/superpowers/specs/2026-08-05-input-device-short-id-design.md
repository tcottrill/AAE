# Input device short IDs and live identification (Windows)

**Date:** 2026-08-05
**Scope:** Windows RawInput only (`aae/system/input/rawinput.cpp`, `aae/aae/menu.cpp`)
**Status:** approved, ready to plan

## Problem

The INPUT DEVICES menu lists mice and keyboards by the friendly name resolved in
`ri_friendly_device_name()`. On a normal Windows machine several devices resolve
to the *same* generic string, so the rows read identically and the user cannot
tell which physical box a row refers to:

```
Player 1 Keyboard   KBD 1: HID Keyboard Device *
Player 2 Keyboard   KBD 2: HID Keyboard Device *
Player 3 Keyboard   KBD 3: HID Keyboard Device *
```

The index prefix (`KBD 2:`) is the only differentiator, and it is an enumeration
artifact that carries no physical meaning.

## Evidence

Probed on the dev box (2 physical keyboards, 1 physical mouse). Six devices
register as mouse/keyboard class:

| Name shown today | VID:PID | Interface | USB port | keys/btns | Actually is |
|---|---|---|---|---|---|
| `HID Keyboard Device` | 2516:007F | MI_02 Col04 | Port 9 | 232 | Cooler Master, media collection |
| `HID-compliant mouse` | 2516:007F | MI_02 Col03 | Port 9 | 8 btn | Cooler Master, phantom mouse |
| `HID Keyboard Device` | 2516:007F | MI_00 | Port 9 | 173 | Cooler Master, real keyboard |
| `HID Keyboard Device` | 04F2:1228 | MI_00 | Port 7 | 264 | Chicony, real keyboard |
| `ROG KERIS WIRELESS AIMPOINT` | 0B05:1A68 | MI_01 | Port 8 | 5 btn | the real mouse |
| `ROG KERIS WIRELESS AIMPOINT` | 0B05:1A68 | MI_02 Col04 | Port 8 | 154 | mouse's phantom keyboard |

Three keyboard rows collide on `HID Keyboard Device`. Two of those three are the
*same physical keyboard* exposing two HID collections.

Pattern worth knowing: the real device is the plain `MI_xx` node with no `Col`;
every `Col0x` sibling is a secondary collection.

### Approaches ruled out by the probe

- **`HidD_GetProductString` / `HidD_GetManufacturerString`** — returns
  `ERROR_ACCESS_DENIED` (5) on *every* mouse and keyboard, including with
  `dwDesiredAccess = 0`. Windows blocks user-mode opens of keyboard and mouse HID
  collections. The true USB product string ("MasterKeys Pro L") is therefore
  unreachable. Not fixable by elevation in any form worth shipping.
- **Walking the USB tree for a better name** — the ancestors are
  `USB Input Device` → `USB Composite Device` → `USB Root Hub`. No product name
  anywhere. `HID Keyboard Device` is genuinely the best name Windows will provide
  for these devices, and no amount of registry work improves it.

### What does discriminate

1. **VID:PID** — separates different physical devices, not collections within one.
2. **`MI_xx` / `Col_xx` from the raw path** — the only thing separating two
   collections of one physical device. Already stored in `RI_*_DEV::path`.
3. **Physical USB port** — `LocationInformation = "Port_#0009.Hub_#0001"` on the
   composite-USB ancestor. Identifies the physical box with no user action.

## Design

### 1. Device tag

A short, stable, display-only identifier appended to the name.

**Format:** `[USB<port>.<mi>.<col>]`

- Port: from the nearest ancestor whose `LocationInformation` matches
  `Port_#%u.Hub_#%u`. Rendered `USB<port>` when hub == 1, else `USB<hub>-<port>`
  so devices behind a cabinet hub stay distinct.
- `<mi>`: the `&MI_xx` value from the raw path, parsed base 16, printed decimal.
  Omitted when absent.
- `<col>`: the `&Col_xx` value, same treatment. Omitted when absent.

**Rendered on the dev box:**

```
KBD 1: HID Keyboard Device [USB9.2.4]
KBD 2: HID Keyboard Device [USB9.0]
KBD 3: HID Keyboard Device [USB7.0]
KBD 4: ROG KERIS WIRELESS AIMPOINT [USB8.2.4]
MOUSE 1: HID-compliant mouse [USB9.2.3]
MOUSE 2: ROG KERIS WIRELESS AIMPOINT [USB8.1]
```

All six unique.

**Fallback:** devices with no USB ancestor carrying a `Port_#` location (PS/2,
Bluetooth) get `[<hash>]` — the low 16 bits of an FNV-1a hash of the sanitized
path, printed as four uppercase hex digits. Guarantees every device always has a
non-empty, unique, stable tag.

### 2. Implementation

`ri_device_tag(HANDLE hDevice, char* out, size_t outlen)` in `rawinput.cpp`,
beside `ri_friendly_device_name()`. Called once per device at registration from
`ri_mouse_slot()` and `ri_kbd_slot()`.

Steps:

1. Raw path via `RIDI_DEVICENAME`.
2. Path → device instance ID: strip the `\\?\` prefix, map `#` → `\`, drop the
   trailing `{class-guid}` chunk. This is the transform already in
   `ri_device_regpath()` minus the `SYSTEM\CurrentControlSet\Enum\` prefix —
   factor the shared part out rather than duplicating it.
3. `CM_Locate_DevNodeA()`, then `CM_Get_Parent()` up to 4 levels, testing each
   node's `CM_DRP_LOCATION_INFORMATION` against `Port_#%u.Hub_#%u`. Scanning for
   the pattern is deliberate: the immediate USB parent carries a dotted numeric
   location (`0002.0000.0000.009...`) and only the grandparent carries the
   `Port_#` form, but that depth is not guaranteed across topologies.
4. Parse `&MI_` and `&Col` out of the raw path (case-insensitive — `RIDI_DEVICENAME`
   returns `Col04`, `CM_Get_Device_ID` returns `COL04`).
5. Compose, or fall back to the hash.

New field `char tag[24]` on both `RI_MOUSE_DEV` and `RI_KBD_DEV`. Written once at
registration and never mutated, so it needs no mutex protection — the same
argument the existing comment makes for `name[]`.

`cfgmgr32` is linked with `#pragma comment(lib, "cfgmgr32.lib")`, matching how
`Joystick.cpp` pulls in dinput8. No `.vcxproj` change.

The registration `LOG_INFO` gains the tag and the full raw path, so the log alone
is enough to diagnose a device-identity report.

### 3. Live activity marker

`seen` is sticky: once every device has been touched, every row shows `*` and the
signal is gone. Add a timestamp alongside it.

- `ULONGLONG last_input` on both device structs, stamped with `GetTickCount64()`
  wherever `seen` is currently set in `RawInput_ProcessInternal`. A plain
  scalar, not `std::atomic`: the structs are initialized with `ZeroMemory` and
  an atomic member would make them non-copyable and the zeroing pedantically
  ill-formed. An aligned 64-bit load/store is atomic on x64, and the worst case
  from a stale read is one frame of a marker appearing late.
- `RawInput_MouseActive(int i)` / `RawInput_KeyboardActive(int i)` →
  `GetTickCount64() - last_input < RI_ACTIVE_MS` (1000).
- Menu marker precedence: `" <<"` while active, `" *"` for seen-ever, nothing
  otherwise.

No refresh plumbing is needed: `MenuItem::getValueDisplay` is already evaluated
every draw, which is why today's `*` appears without an explicit invalidate.

### 4. Device roster rows

The eight `Player N Mouse/Keyboard` rows only show the device *assigned* to that
player, so identifying a device through them means reassigning it first. Add a
read-only roster to the bottom of the INPUT DEVICES page: one row per mouse and
per keyboard, label `MOUSE n` / `KBD n`, value = name + tag + live marker, no
`onAdjust` (a no-op lambda if `MenuItem` requires one — to be confirmed against
`menu.cpp` during planning).

This is MAME's Input Devices list (`src/frontend/mame/ui/inputdevices.cpp`)
reduced to AAE's flat menu: press a key on a physical keyboard, watch which row
reacts, change no assignments.

**Known limitation:** the roster is built when the page is built, so a device
hot-plugged while the page is open does not appear until the page is re-entered.
This matches the existing behavior of the player rows' device-count ranges.

### 5. Accessors

`RawInput_GetMouseTag(int i)` / `RawInput_GetKeyboardTag(int i)` declared in
`sys_input.h`, returning `""` for invalid indices (the existing name accessors
return `"NONE"`; a tag has no such sentinel, and callers concatenate).

`RawInput_GetMouseName`/`GetKeyboardName` keep returning the bare friendly name.
Call sites compose name + tag. This keeps the log format independent of the menu
format and avoids widening `name[64]`.

## Deliberately unchanged

- **Persistence.** The tag is display-only. Device identity stays the full
  sanitized path in `config.mouse_player_path[]` / `kbd_player_path[]`. A port
  number is precisely the thing that changes when a device is replugged
  elsewhere, and the existing "ASSIGNED DEVICE NOT ATTACHED" detection and
  self-heal-on-reattach already handle that correctly.
- **No filtering.** Phantom collections stay listed. An I-PAC-style encoder
  legitimately presents multiple collections, and MAME never hides devices.
- **Linux.** Out of scope. `evdev` reports real product names from
  `/sys/class/input/eventN/device/name` and a `phys` string that already contains
  the port, so the collision this spec addresses largely does not occur there.

## Files touched

| File | Change |
|---|---|
| `aae/system/input/rawinput.cpp` | `ri_device_tag()`, shared instance-id helper, `tag[24]` + atomic `last_input` fields, timestamp stamping, four new accessors, richer registration log |
| `aae/system/input/sys_input.h` | declarations for the tag and active accessors |
| `aae/aae/menu.cpp` | compose name + tag in the 8 player rows, live-marker precedence, roster rows |

## Verification

1. Build `MSBuild Debug|x64` (the known-good config).
2. Open INPUT DEVICES; confirm all six rows render exactly the strings in the
   Design section, with no two identical.
3. Press a key on each physical keyboard in turn; confirm `<<` appears on the
   expected roster row and on no other, and decays within about a second.
4. Move the real mouse; confirm the same for `MOUSE 2: ... [USB8.1]`.
5. Confirm pre-existing player assignments in `aae.ini` still resolve after the
   change — the tag must not affect path matching.
6. Confirm the registration log lines carry tag and raw path.
7. Replug a keyboard into a different USB port; confirm the tag changes, the
   assignment reports not-attached, and nothing crashes.
