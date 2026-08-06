# Input Device Short IDs and Live Identification Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make every mouse and keyboard row in the INPUT DEVICES menu uniquely identifiable, by appending a short USB-port-based tag to the display name and adding a live "this device is sending input right now" marker.

**Architecture:** A new header-only module holds the pure string parsing behind the tag so it can be unit-tested without Win32. `rawinput.cpp` adds the Win32 device-tree walk (`cfgmgr32`) that supplies the USB port, stores `tag[]` and `last_input` per device, and exposes four new accessors. `menu.cpp` composes name + tag + marker for the per-player rows and adds a read-only device roster. Linux gets neutral stubs so the shared `menu.cpp` keeps linking; Linux behavior is byte-for-byte unchanged.

**Tech Stack:** C++17, Win32 RawInput + cfgmgr32, MSBuild (`Debug|x64`), CMake for the Linux half.

**Spec:** [2026-08-05-input-device-short-id-design.md](../specs/2026-08-05-input-device-short-id-design.md)

---

## File Structure

| File | Status | Responsibility |
|---|---|---|
| `aae/system/input/ri_device_tag.h` | **Create** | Pure, Win32-free string helpers: path ordinal parsing, `Port_#`/`Hub_#` parsing, FNV-1a hash, tag formatting. Header-only so both `rawinput.cpp` and a throwaway test harness can include it. No `.vcxproj` change needed — headers do not need project entries to compile. |
| `aae/system/input/rawinput.cpp` | Modify | Win32 glue: device-instance-id derivation, the `cfgmgr32` parent walk, `tag[24]` and `last_input` fields, timestamp stamping, four accessors, richer registration log. |
| `aae/system/input/sys_input.h` | Modify | Declarations for the four new accessors. |
| `aae/system/input/linux/evdev_input.cpp` | Modify | Neutral stubs for the four accessors so the shared `menu.cpp` links on Linux. |
| `aae/aae/menu.cpp` | Modify | `device_row_text()` helper, tag + live marker on the 8 player rows, read-only device roster rows. |

**Why the new header:** the parsing (hex vs decimal, case sensitivity, hub numbering) is the only genuinely bug-prone part of this change, and it is pure. Isolating it makes it testable with no build-system ceremony. The Win32 calls stay in `rawinput.cpp`.

---

### Task 1: Pure tag helpers, test-first

**Files:**
- Create: `aae/system/input/ri_device_tag.h`
- Test: `<scratchpad>/tagtest.cpp` (throwaway, never committed)

Throughout this plan, `<scratchpad>` means:
`C:\Users\user9\AppData\Local\Temp\claude\C--Source2026-AAE-publish\5aab3f20-ced9-4856-bb72-4c36ddfcee92\scratchpad`

The expected tag strings below are not invented — they were derived by probing the dev box's real hardware (see the spec's Evidence table).

- [ ] **Step 1: Write the failing test**

Create `<scratchpad>/tagtest.cpp`:

```cpp
// Throwaway harness for the pure helpers in ri_device_tag.h.
#include <stdio.h>
#include <string.h>
#include "ri_device_tag.h"   // found via the /I flag in the build command below

static int g_fail = 0;

static void check_tag(const char* what, const char* rawpath, const char* location,
                      const char* sanitized, const char* expect)
{
    unsigned port = 0, hub = 0;
    bool have_port = location ? ri_parse_port_location(location, &port, &hub) : false;
    const int mi  = ri_path_ordinal(rawpath, "&MI_");
    const int col = ri_path_ordinal(rawpath, "&Col");

    char got[24];
    ri_format_tag(have_port, port, hub, mi, col, sanitized, got, sizeof(got));

    const bool ok = strcmp(got, expect) == 0;
    if (!ok) g_fail++;
    printf("%s %-10s expected %-14s got %s\n", ok ? "PASS" : "FAIL", what, expect, got);
}

int main()
{
    // The six real devices on the dev box.
    check_tag("kbd1",   "\\\\?\\HID#VID_2516&PID_007F&MI_02&Col04#8&2314f73c&0&0003#{884b96c3}",
              "Port_#0009.Hub_#0001", "x", "[USB9.2.4]");
    check_tag("mouse1", "\\\\?\\HID#VID_2516&PID_007F&MI_02&Col03#8&2314f73c&0&0002#{378de44c}",
              "Port_#0009.Hub_#0001", "x", "[USB9.2.3]");
    check_tag("kbd2",   "\\\\?\\HID#VID_2516&PID_007F&MI_00#8&2f28a8b9&0&0000#{884b96c3}",
              "Port_#0009.Hub_#0001", "x", "[USB9.0]");
    check_tag("kbd3",   "\\\\?\\HID#VID_04F2&PID_1228&MI_00#8&1e2d9879&0&0000#{884b96c3}",
              "Port_#0007.Hub_#0001", "x", "[USB7.0]");
    check_tag("mouse2", "\\\\?\\HID#VID_0B05&PID_1A68&MI_01#8&3b22c1af&0&0000#{378de44c}",
              "Port_#0008.Hub_#0001", "x", "[USB8.1]");
    check_tag("kbd4",   "\\\\?\\HID#VID_0B05&PID_1A68&MI_02&Col04#8&3c7fe54&0&0003#{884b96c3}",
              "Port_#0008.Hub_#0001", "x", "[USB8.2.4]");

    // Hex ordinals: MI_0A must render as 10, not 0A-as-decimal.
    check_tag("hexmi",  "\\\\?\\HID#VID_1234&PID_5678&MI_0A#x#{g}",
              "Port_#0003.Hub_#0001", "x", "[USB3.10]");

    // Behind an external hub, the hub number is shown so ports stay distinct.
    check_tag("hub2",   "\\\\?\\HID#VID_1234&PID_5678&MI_00#x#{g}",
              "Port_#0002.Hub_#0002", "x", "[USB2-2.0]");

    // Uppercase COL (CM_Get_Device_ID returns uppercase) parses the same.
    check_tag("upper",  "\\\\?\\HID#VID_1234&PID_5678&MI_02&COL04#x#{g}",
              "Port_#0005.Hub_#0001", "x", "[USB5.2.4]");

    // No MI/Col at all: port alone.
    check_tag("bare",   "\\\\?\\HID#VID_1234&PID_5678#x#{g}",
              "Port_#0004.Hub_#0001", "x", "[USB4]");

    // No USB ancestor (PS/2, Bluetooth): stable hash of the sanitized path.
    // Two different paths must not collide; the same path must be stable.
    char a[24], b[24], a2[24];
    ri_format_tag(false, 0, 0, -1, -1, "ACPI_PNP0303_4_1f2e3d", a, sizeof(a));
    ri_format_tag(false, 0, 0, -1, -1, "ACPI_PNP0F13_4_1f2e3d", b, sizeof(b));
    ri_format_tag(false, 0, 0, -1, -1, "ACPI_PNP0303_4_1f2e3d", a2, sizeof(a2));
    printf("%s hash-uniq  %s vs %s\n", strcmp(a, b) ? "PASS" : "FAIL", a, b);
    printf("%s hash-stable %s vs %s\n", strcmp(a, a2) == 0 ? "PASS" : "FAIL", a, a2);
    if (strcmp(a, b) == 0) g_fail++;
    if (strcmp(a, a2) != 0) g_fail++;
    if (strlen(a) != 6) { printf("FAIL hash-format %s (want [XXXX])\n", a); g_fail++; }

    printf("\n%s (%d failures)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail);
    return g_fail ? 1 : 0;
}
```

- [ ] **Step 2: Run the test and verify it fails to compile**

```bash
cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" && cl /nologo /EHsc /W3 /I C:\Source2026\AAE_publish\aae\system\input tagtest.cpp /Fe:tagtest.exe'
```

Run it from the scratchpad directory. Expected: **compile error**, `Cannot open include file: 'ri_device_tag.h'`. That is the failing state — the header does not exist yet.

- [ ] **Step 3: Write the header**

Create `aae/system/input/ri_device_tag.h`:

```cpp
// ---------------------------------------------------------------------------
// ri_device_tag.h -- pure string helpers behind the input-device short ID.
//
// Windows reports several distinct HID collections under one generic name
// ("HID Keyboard Device"), so the INPUT DEVICES menu can show three identical
// rows. The tag appends a short, stable, physically meaningful suffix:
//
//     HID Keyboard Device [USB9.0]     <- USB port 9, interface 0
//     HID Keyboard Device [USB9.2.4]   <- same box, interface 2 collection 4
//
// Everything here is Win32-free on purpose: rawinput.cpp includes it for the
// real thing and supplies the port from a cfgmgr32 walk, while a console
// harness can include it to test the parsing without the emulator.
//
// The tag is DISPLAY ONLY. Device identity remains the full sanitized path --
// a port number is exactly what changes when a device is replugged elsewhere.
// ---------------------------------------------------------------------------
#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Case-insensitive substring search. Device paths arrive in mixed case:
// RIDI_DEVICENAME yields "Col04" while CM_Get_Device_ID yields "COL04".
static const char* ri_stristr(const char* hay, const char* needle)
{
	const size_t n = strlen(needle);
	if (!n) return hay;
	for (; *hay; hay++)
		if (_strnicmp(hay, needle, n) == 0) return hay;
	return nullptr;
}

// Value following a marker in a raw device path, e.g. "&MI_" in
// "...&MI_02&Col04#...". USB interface and collection numbers are rendered as
// hex digits, so parse base 16. Returns -1 when the marker is absent.
static int ri_path_ordinal(const char* rawpath, const char* marker)
{
	const char* p = ri_stristr(rawpath, marker);
	if (!p) return -1;
	p += strlen(marker);

	int value = 0, digits = 0;
	while (digits < 4)
	{
		const char c = *p;
		int d;
		if      (c >= '0' && c <= '9') d = c - '0';
		else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
		else break;
		value = value * 16 + d;
		p++;
		digits++;
	}
	return digits ? value : -1;
}

// Parse a device-tree LocationInformation string of the form
// "Port_#0009.Hub_#0001". Both numbers are decimal. Returns false for the
// other location formats Windows uses (the immediate USB parent reports a
// dotted numeric string with no Port_# at all).
static bool ri_parse_port_location(const char* loc, unsigned* port, unsigned* hub)
{
	const char* p = ri_stristr(loc, "Port_#");
	const char* h = ri_stristr(loc, "Hub_#");
	if (!p || !h) return false;
	*port = (unsigned)strtoul(p + 6, nullptr, 10);
	*hub  = (unsigned)strtoul(h + 5, nullptr, 10);
	return true;
}

// FNV-1a folded to 16 bits. Used only as the last-resort tag for devices with
// no USB ancestor (PS/2, Bluetooth); the fold gives better spread than simple
// truncation.
static unsigned short ri_path_hash16(const char* s)
{
	unsigned int h = 2166136261u;
	for (; *s; s++)
	{
		h ^= (unsigned char)*s;
		h *= 16777619u;
	}
	return (unsigned short)((h >> 16) ^ (h & 0xffffu));
}

// Compose the final bracketed tag. Without a USB port there is nothing
// physically meaningful to show, so fall back to the path hash: an opaque tag
// is still better than two rows that read identically.
static void ri_format_tag(bool have_port, unsigned port, unsigned hub,
                          int mi, int col, const char* sanitized_path,
                          char* out, size_t outlen)
{
	if (!have_port)
	{
		snprintf(out, outlen, "[%04X]", ri_path_hash16(sanitized_path ? sanitized_path : ""));
		return;
	}

	// Hub 1 is the root hub and adds no information; deeper hubs do, and a
	// cabinet with a powered hub would otherwise collapse to one number.
	char portbuf[16];
	if (hub <= 1) snprintf(portbuf, sizeof(portbuf), "USB%u", port);
	else          snprintf(portbuf, sizeof(portbuf), "USB%u-%u", hub, port);

	if      (mi >= 0 && col >= 0) snprintf(out, outlen, "[%s.%d.%d]", portbuf, mi, col);
	else if (mi >= 0)             snprintf(out, outlen, "[%s.%d]",    portbuf, mi);
	else if (col >= 0)            snprintf(out, outlen, "[%s.c%d]",   portbuf, col);
	else                          snprintf(out, outlen, "[%s]",       portbuf);
}
```

- [ ] **Step 4: Run the test and verify it passes**

```bash
cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" && cl /nologo /EHsc /W3 /I C:\Source2026\AAE_publish\aae\system\input tagtest.cpp /Fe:tagtest.exe && tagtest.exe'
```

Expected: every line reads `PASS`, final line `ALL PASSED (0 failures)`, exit code 0.

If `hexmi` fails with `[USB3.0]`, the ordinal parser stopped at the `A`. If `upper` fails, the search is case-sensitive.

- [ ] **Step 5: Commit**

```bash
git add aae/system/input/ri_device_tag.h && git commit -m "feat(input): pure helpers for device short-ID tags"
```

---

### Task 2: Build the tag at device registration

**Files:**
- Modify: `aae/system/input/rawinput.cpp`

- [ ] **Step 1: Add the include and the cfgmgr32 link**

Find the existing include block at the top of `rawinput.cpp` and add:

```cpp
#include <cfgmgr32.h>
#include "ri_device_tag.h"

// Device-tree queries for the short device tag. Linked here rather than in
// the .vcxproj, matching how Joystick.cpp pulls in dinput8.
#pragma comment(lib, "cfgmgr32.lib")
```

- [ ] **Step 2: Add the `tag` field to both device structs**

In `RI_MOUSE_DEV` (around line 81), after `char path[260];` and its comment block, add:

```cpp
	char tag[24];          // short disambiguator, e.g. "[USB9.0]" -- several
	                       // devices legitimately share one friendly name, so
	                       // this is what makes menu rows distinguishable.
	                       // DISPLAY ONLY: identity is still path[].
```

In `RI_KBD_DEV` (around line 105), after `char path[260];`, add the identical field and comment.

- [ ] **Step 3: Factor out the instance-id transform**

`ri_device_regpath()` (line 180) already performs the path→instance-id transform and then prefixes the registry root. The cfgmgr32 walk needs the bare instance id, so split it. Replace the whole of `ri_device_regpath` with:

```cpp
// "\\?\HID#VID_2516&PID_007F&MI_00#8&2f28a8b9&0&0000#{guid}" ->
// "HID\VID_2516&PID_007F&MI_00\8&2f28a8b9&0&0000"
// This is the device instance id, which both the registry path and the
// cfgmgr32 device-tree lookup are built from.
static bool ri_device_instid(const char* rawname, char* out, size_t outlen)
{
	if (strncmp(rawname, "\\\\?\\", 4) != 0 && strncmp(rawname, "\\??\\", 4) != 0)
		return false;

	size_t o = 0;
	for (const char* p = rawname + 4; *p && o < outlen - 1; p++)
		out[o++] = (*p == '#') ? '\\' : *p;
	out[o] = 0;

	// drop the trailing "{DeviceClasses-guid}" chunk
	char* last = strrchr(out, '\\');
	if (!last) return false;
	*last = 0;
	return true;
}

// The same identity under the registry's Enum root.
static bool ri_device_regpath(const char* rawname, char* out, size_t outlen)
{
	static const char base[] = "SYSTEM\\CurrentControlSet\\Enum\\";
	const size_t blen = sizeof(base) - 1;
	if (blen >= outlen) return false;
	memcpy(out, base, blen);
	return ri_device_instid(rawname, out + blen, outlen - blen);
}
```

- [ ] **Step 4: Add the tag builder**

Immediately after `ri_sanitize_path()` (which currently ends around line 323), add:

```cpp
// Build the short device tag. The USB port comes from the device tree: walk
// up from the HID node looking for the first ancestor whose
// LocationInformation carries the "Port_#n.Hub_#n" form. The depth is not
// fixed -- the immediate USB parent reports a dotted numeric location and
// only the composite device above it carries Port_# -- so scan rather than
// assuming a level. The interface/collection ordinals come from the raw path.
//
// `sanitized_path` is only used for the no-USB-ancestor fallback hash, so it
// must already be filled in by the caller.
static void ri_device_tag(HANDLE hDevice, const char* sanitized_path,
                          char* out, size_t outlen)
{
	out[0] = 0;

	char rawpath[512] = { 0 };
	UINT sz = sizeof(rawpath) - 1;
	if (GetRawInputDeviceInfoA(hDevice, RIDI_DEVICENAME, rawpath, &sz) == (UINT)-1 || !rawpath[0])
	{
		ri_format_tag(false, 0, 0, -1, -1, sanitized_path, out, outlen);
		return;
	}

	const int mi  = ri_path_ordinal(rawpath, "&MI_");
	const int col = ri_path_ordinal(rawpath, "&Col");

	bool have_port = false;
	unsigned port = 0, hub = 0;

	char instid[512];
	DEVINST dn;
	if (ri_device_instid(rawpath, instid, sizeof(instid)) &&
	    CM_Locate_DevNodeA(&dn, instid, CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS)
	{
		for (int level = 0; level < 4 && !have_port; level++)
		{
			char loc[256] = { 0 };
			ULONG len = sizeof(loc) - 1, type = 0;
			if (CM_Get_DevNode_Registry_PropertyA(dn, CM_DRP_LOCATION_INFORMATION,
			                                      &type, loc, &len, 0) == CR_SUCCESS)
				have_port = ri_parse_port_location(loc, &port, &hub);

			DEVINST parent;
			if (CM_Get_Parent(&parent, dn, 0) != CR_SUCCESS) break;
			dn = parent;
		}
	}

	ri_format_tag(have_port, port, hub, mi, col, sanitized_path, out, outlen);
}
```

- [ ] **Step 5: Call it at registration, after the path is sanitized**

In `ri_mouse_slot()`, the current body fills the name before the path. The tag's fallback hash needs the sanitized path, so reorder. Replace lines 338-350 (from `int i = g_numMice;` to `return i;`) with:

```cpp
	int i = g_numMice;
	ZeroMemory(&g_mice[i], sizeof(g_mice[i]));
	g_mice[i].handle = hDevice;

	// path first: the tag's fallback hash is derived from it
	UINT psz = sizeof(g_mice[i].path) - 1;
	if (GetRawInputDeviceInfoA(hDevice, RIDI_DEVICENAME, g_mice[i].path, &psz) == (UINT)-1)
		g_mice[i].path[0] = 0;
	ri_sanitize_path(g_mice[i].path);

	ri_friendly_device_name(hDevice, g_mice[i].name, sizeof(g_mice[i].name));
	ri_device_tag(hDevice, g_mice[i].path, g_mice[i].tag, sizeof(g_mice[i].tag));

	g_numMice++;
	LOG_INFO("RawInput: mouse %d registered: %s %s  path=%s",
	         i + 1, g_mice[i].name, g_mice[i].tag, g_mice[i].path);
	return i;
```

Apply the identical reordering to `ri_kbd_slot()` (lines 364-376), substituting `g_kbds`, `g_numKbds`, and the log text `"RawInput: keyboard %d registered: %s %s  path=%s"`.

- [ ] **Step 6: Build**

```bash
msbuild aae.sln -p:Configuration=Debug -p:Platform=x64 -v:minimal
```

Expected: build succeeds. If the linker cannot find `CM_Locate_DevNodeA`, the `#pragma comment(lib, "cfgmgr32.lib")` from Step 1 is missing or misplaced.

- [ ] **Step 7: Verify the tags against real hardware**

Run the built `aae.exe` once, then inspect the log for the registration lines.

Expected on the dev box: six lines whose tags are exactly `[USB9.2.4]`, `[USB9.2.3]`, `[USB9.0]`, `[USB7.0]`, `[USB8.1]`, `[USB8.2.4]`. No two identical. If every tag is a 4-hex-digit hash instead, the cfgmgr32 walk found no `Port_#` ancestor — check that `ri_device_instid` is dropping the trailing GUID chunk.

- [ ] **Step 8: Commit**

```bash
git add aae/system/input/rawinput.cpp && git commit -m "feat(input): derive a short USB-port tag for each raw input device"
```

---

### Task 3: Per-device last-input timestamp

**Files:**
- Modify: `aae/system/input/rawinput.cpp`

- [ ] **Step 1: Add the timestamp field to both structs**

In `RI_MOUSE_DEV`, after the `tag[24]` field from Task 2, add:

```cpp
	ULONGLONG last_input;  // GetTickCount64() of the most recent event, 0 if
	                       // never. Drives the live "which device is this"
	                       // marker; `seen` above is sticky and so goes blind
	                       // once every device has been touched once.
	                       // A plain scalar, not std::atomic: the struct is
	                       // ZeroMemory'd at registration and an atomic member
	                       // would make that ill-formed. Aligned 64-bit
	                       // load/store is atomic on x64, and the worst case
	                       // from a stale read is one frame of a late marker.
```

Add the identical field to `RI_KBD_DEV`.

- [ ] **Step 2: Add the activity window constant**

In `rawinput.cpp`, immediately above the `RI_MOUSE_DEV` struct (around line 81), add:

```cpp
// How long after an event a device still counts as "active" for the UI
// marker. Long enough to survive the gap between two keystrokes, short
// enough that the marker clearly follows what you are touching.
#define RI_ACTIVE_MS 1000
```

- [ ] **Step 3: Stamp on every keyboard event**

At line 684, replace:

```cpp
				g_kbds[slot].seen = 1;
```

with:

```cpp
				g_kbds[slot].seen = 1;
				g_kbds[slot].last_input = GetTickCount64();
```

- [ ] **Step 4: Stamp on every mouse event**

At line 717, replace:

```cpp
				m.seen = 1;
```

with:

```cpp
				m.seen = 1;
				m.last_input = GetTickCount64();
```

- [ ] **Step 5: Build**

```bash
msbuild aae.sln -p:Configuration=Debug -p:Platform=x64 -v:minimal
```

Expected: build succeeds. Nothing reads the field yet, so there is no behavior change.

- [ ] **Step 6: Commit**

```bash
git add aae/system/input/rawinput.cpp && git commit -m "feat(input): stamp per-device last-input time"
```

---

### Task 4: Accessors on both platforms

Both platforms are changed in one commit so the tree never contains a state where the shared `menu.cpp` fails to link on Linux.

**Files:**
- Modify: `aae/system/input/rawinput.cpp`
- Modify: `aae/system/input/sys_input.h`
- Modify: `aae/system/input/linux/evdev_input.cpp`

- [ ] **Step 1: Add the Windows mouse accessors**

In `rawinput.cpp`, immediately after `RawInput_MouseSeenInput()` (ends line 908), add:

```cpp
// Short disambiguating tag, e.g. "[USB9.0]". Written once at registration and
// never mutated, so the pointer is safe to hand out. Returns "" for an invalid
// index -- callers concatenate this onto a name, so there is no useful
// sentinel string.
const char* RawInput_GetMouseTag(int index)
{
	std::lock_guard<std::mutex> lock(g_miceMutex);
	if (index < 0 || index >= g_numMice) return "";
	return g_mice[index].tag;
}

// 1 while this device produced input within the last RI_ACTIVE_MS. Unlike the
// sticky `seen` flag this decays, so the UI can show which device is being
// touched right now -- the thing that actually identifies a physical box.
int RawInput_MouseActive(int index)
{
	std::lock_guard<std::mutex> lock(g_miceMutex);
	if (index < 0 || index >= g_numMice) return 0;
	const ULONGLONG t = g_mice[index].last_input;
	if (!t) return 0;
	return (GetTickCount64() - t) < RI_ACTIVE_MS ? 1 : 0;
}
```

- [ ] **Step 2: Add the Windows keyboard accessors**

After `RawInput_KeyboardSeenInput()` (ends line 971), add:

```cpp
const char* RawInput_GetKeyboardTag(int index)
{
	std::lock_guard<std::mutex> lock(g_kbdsMutex);
	if (index < 0 || index >= g_numKbds) return "";
	return g_kbds[index].tag;
}

int RawInput_KeyboardActive(int index)
{
	std::lock_guard<std::mutex> lock(g_kbdsMutex);
	if (index < 0 || index >= g_numKbds) return 0;
	const ULONGLONG t = g_kbds[index].last_input;
	if (!t) return 0;
	return (GetTickCount64() - t) < RI_ACTIVE_MS ? 1 : 0;
}
```

- [ ] **Step 3: Declare them in the neutral header**

In `sys_input.h`, after `int RawInput_FindMouseByPath(const char* path);` (line 610-611), add:

```cpp
const char* RawInput_GetMouseTag(int index);       // short disambiguator such
                                                   // as "[USB9.0]" for devices
                                                   // sharing a friendly name;
                                                   // "" when unavailable
int  RawInput_MouseActive(int index);              // 1 while the device is
                                                   // producing input right now
                                                   // (decays; `seen` is sticky)
```

After `int RawInput_KeyboardSeenInput(int index);` (line 632), add:

```cpp
const char* RawInput_GetKeyboardTag(int index);
int  RawInput_KeyboardActive(int index);
```

- [ ] **Step 4: Add the Linux stubs**

In `evdev_input.cpp`, after `RawInput_MouseSeenInput()` (ends line 675), add:

```cpp
//==============================================================================
// Device tag / live activity -- Windows-only cosmetics.
//
// These exist so the shared menu.cpp links. Windows needs them because it
// reports several distinct HID collections under one generic name
// ("HID Keyboard Device"); evdev reports real product names, so there is
// nothing to disambiguate. Returning "" makes the menu compose no tag, and
// returning 0 leaves the Linux rows showing the sticky `seen` marker exactly
// as they did before.
//==============================================================================
const char* RawInput_GetMouseTag(int)    { return ""; }
const char* RawInput_GetKeyboardTag(int) { return ""; }
int RawInput_MouseActive(int)            { return 0; }
int RawInput_KeyboardActive(int)         { return 0; }
```

- [ ] **Step 5: Build Windows**

```bash
msbuild aae.sln -p:Configuration=Debug -p:Platform=x64 -v:minimal
```

Expected: build succeeds.

- [ ] **Step 6: Commit**

```bash
git add aae/system/input/rawinput.cpp aae/system/input/sys_input.h aae/system/input/linux/evdev_input.cpp && git commit -m "feat(input): expose device tag and live-activity accessors"
```

---

### Task 5: Tag and live marker on the player rows

**Files:**
- Modify: `aae/aae/menu.cpp:1359-1445`

- [ ] **Step 1: Add the shared row-text helper**

Immediately before `void MenuManager::BuildInputDevicesMenu() {` (line 1330), add:

```cpp
// Shared text for every row that names a physical input device -- the
// per-player assignment rows and the device roster below them.
//
//     HID Keyboard Device [USB9.0] <<
//
// The tag disambiguates devices Windows reports under one generic name. The
// live "<<" marker beats the sticky "*": once every device has been touched
// once, "*" is on every row and tells you nothing, whereas "<<" appears only
// on the device you are touching right now.
static std::string device_row_text(const char* name, const char* tag,
                                   bool active, bool seen)
{
    std::string s = name ? name : "";
    if (tag && tag[0]) {
        s += " ";
        s += tag;
    }
    if (active)     s += " <<";
    else if (seen)  s += " *";
    return s;
}
```

- [ ] **Step 2: Use it in the mouse rows**

In the mouse `getValueDisplay` lambda, replace lines 1367-1371:

```cpp
            std::string s = "MOUSE " + std::to_string(v + 1) + ": " + RawInput_GetMouseName(v);
            // '*' = this device has actually sent input (wiggle a mouse
            // to identify it; phantom keyboard collections never move)
            if (RawInput_MouseSeenInput(v)) s += " *";
            return s;
```

with:

```cpp
            return "MOUSE " + std::to_string(v + 1) + ": " +
                   device_row_text(RawInput_GetMouseName(v),
                                   RawInput_GetMouseTag(v),
                                   RawInput_MouseActive(v) != 0,
                                   RawInput_MouseSeenInput(v) != 0);
```

- [ ] **Step 3: Use it in the keyboard rows**

Replace lines 1424-1427:

```cpp
            std::string s = "KBD " + std::to_string(v + 1) + ": " + RawInput_GetKeyboardName(v);
            // '*' = this keyboard has actually sent a key (press one to identify)
            if (RawInput_KeyboardSeenInput(v)) s += " *";
            return s;
```

with:

```cpp
            return "KBD " + std::to_string(v + 1) + ": " +
                   device_row_text(RawInput_GetKeyboardName(v),
                                   RawInput_GetKeyboardTag(v),
                                   RawInput_KeyboardActive(v) != 0,
                                   RawInput_KeyboardSeenInput(v) != 0);
```

- [ ] **Step 4: Build**

```bash
msbuild aae.sln -p:Configuration=Debug -p:Platform=x64 -v:minimal
```

Expected: build succeeds.

- [ ] **Step 5: Verify in the running app**

Launch `aae.exe`, open INPUT DEVICES, and cycle "Player 1 Keyboard" through its devices.

Expected: each keyboard now reads e.g. `KBD 2: HID Keyboard Device [USB9.0] *`. With a device selected, press a key on the matching physical keyboard: `*` becomes `<<` and reverts about a second after you stop.

- [ ] **Step 6: Commit**

```bash
git add aae/aae/menu.cpp && git commit -m "feat(menu): show device tag and live activity on player device rows"
```

---

### Task 6: Read-only device roster

The player rows only show the device assigned to that player, so identifying a device through them means reassigning it first. The roster lists every device at once, changing nothing.

**Files:**
- Modify: `aae/aae/menu.cpp:1500`

- [ ] **Step 1: Append the roster to the page**

`BuildInputDevicesMenu()` currently ends at line 1500 with the joystick loop's closing `}` followed by the function's `}`. Insert before the function's closing brace:

```cpp
    // ------------------------------------------------------------------
    // Device roster: every attached mouse and keyboard, read-only. The
    // rows above only show the device assigned to a player, so identifying
    // a device through them would mean reassigning it first. Here you press
    // a key (or wiggle a mouse) and watch which row shows "<<", changing
    // nothing.
    //
    // These are ENABLED items with no-op callbacks rather than isDisabled
    // ones: Draw() renders disabledReason INSTEAD of getValueDisplay() for a
    // disabled item, which would hide the device string entirely. Empty
    // hasLeft/hasRight suppress the adjust arrows.
    //
    // Built once when the page is entered, so a device hot-plugged while the
    // page is open appears after leaving and re-entering -- the same rule the
    // rows above already follow for their device-count ranges.
    // ------------------------------------------------------------------
    {
        MenuItem header;
        header.label = "---- DEVICES ----";
        header.onAdjust   = [](int) {};
        header.onActivate = []() {};
        header.hasLeft    = []() { return false; };
        header.hasRight   = []() { return false; };
        m_items.push_back(header);
    }

    for (int i = 0; i < RawInput_GetMouseCount(); i++) {
        MenuItem mi;
        mi.label = "MOUSE " + std::to_string(i + 1);
        mi.getValueDisplay = [i]() -> std::string {
            return device_row_text(RawInput_GetMouseName(i),
                                   RawInput_GetMouseTag(i),
                                   RawInput_MouseActive(i) != 0,
                                   RawInput_MouseSeenInput(i) != 0);
        };
        mi.onAdjust   = [](int) {};
        mi.onActivate = []() {};
        mi.hasLeft    = []() { return false; };
        mi.hasRight   = []() { return false; };
        m_items.push_back(mi);
    }

    for (int i = 0; i < RawInput_GetKeyboardCount(); i++) {
        MenuItem mi;
        mi.label = "KBD " + std::to_string(i + 1);
        mi.getValueDisplay = [i]() -> std::string {
            return device_row_text(RawInput_GetKeyboardName(i),
                                   RawInput_GetKeyboardTag(i),
                                   RawInput_KeyboardActive(i) != 0,
                                   RawInput_KeyboardSeenInput(i) != 0);
        };
        mi.onAdjust   = [](int) {};
        mi.onActivate = []() {};
        mi.hasLeft    = []() { return false; };
        mi.hasRight   = []() { return false; };
        m_items.push_back(mi);
    }
```

- [ ] **Step 2: Build**

```bash
msbuild aae.sln -p:Configuration=Debug -p:Platform=x64 -v:minimal
```

Expected: build succeeds.

- [ ] **Step 3: Verify the roster identifies devices**

Launch `aae.exe` and open INPUT DEVICES. Scroll to the `---- DEVICES ----` section.

Expected on the dev box, six rows:

```
MOUSE 1    HID-compliant mouse [USB9.2.3]
MOUSE 2    ROG KERIS WIRELESS AIMPOINT [USB8.1]
KBD 1      HID Keyboard Device [USB9.2.4]
KBD 2      HID Keyboard Device [USB9.0]
KBD 3      HID Keyboard Device [USB7.0]
KBD 4      ROG KERIS WIRELESS AIMPOINT [USB8.2.4]
```

Press a key on the Cooler Master keyboard: `<<` appears on `KBD 2` **only**. Press a key on the other keyboard: `<<` appears on `KBD 3` only. Move the real mouse: `<<` on `MOUSE 2` only. No arrows are drawn on any roster row, and left/right on one does nothing.

- [ ] **Step 4: Commit**

```bash
git add aae/aae/menu.cpp && git commit -m "feat(menu): add a read-only device roster to INPUT DEVICES"
```

---

### Task 7: Regression pass

**Files:** none modified — this task verifies the spec's acceptance criteria.

- [ ] **Step 1: Confirm existing assignments still resolve**

Before launching, note the `[input]` section of `x64/Debug/aae.ini` (or whichever ini the Debug build loads) — specifically `mouse_player1_path`, `kbd_player1_path` and friends.

Launch `aae.exe`, open INPUT DEVICES, confirm the player rows show the same assigned devices as before this change and that none reads `ASSIGNED DEVICE NOT ATTACHED` where it previously resolved. The tag must not affect path matching — it is display-only.

- [ ] **Step 2: Confirm the tag survives a restart**

Quit and relaunch. Every tag must be identical to the previous run. A tag that changes between runs means the port walk is picking up something transient.

- [ ] **Step 3: Confirm replug behavior**

Move one keyboard to a different USB port and relaunch.

Expected: that device's tag shows the new port number; if it was assigned to a player, that row reports `NOT ATTACHED (USING ALL KBDS)` (the existing keyboard fallback); nothing crashes. Re-assign it and confirm it sticks across one more restart.

- [ ] **Step 4: Confirm the registration log**

Check the log for the six `RawInput: ... registered:` lines. Each must carry a name, a tag, and a full sanitized path.

- [ ] **Step 5: Confirm Linux still builds**

The four accessors are declared in the neutral `sys_input.h` and called from the shared `menu.cpp`, so the Linux half must still link.

```bash
cmake --build build-linux -j
```

Expected: builds clean. If it fails with undefined references to `RawInput_GetMouseTag` or `RawInput_MouseActive`, the Task 4 Step 4 stubs are missing from `evdev_input.cpp`.

- [ ] **Step 6: Delete the throwaway test harness**

```bash
rm <scratchpad>/tagtest.cpp <scratchpad>/tagtest.exe <scratchpad>/tagtest.obj
```

Nothing in the repo references it; it existed to drive Task 1.

---

## Notes for the implementer

- **Do not** put the tag into `name[]`. The log format and the menu format are deliberately independent, and `name[64]` would overflow on long product names plus a tag.
- **Do not** use the tag as a persistence key. `config.mouse_player_path[]` / `kbd_player_path[]` stay the identity. A USB port number is exactly what changes when a device moves.
- **Do not** filter phantom collections out of the roster. An I-PAC-style encoder legitimately presents several collections, and MAME lists everything for the same reason.
- The `Debug|x64` MSBuild configuration is the known-good one; the x86/Win32 configurations are broken for unrelated reasons and are not worth debugging here.
