# Linux keyboard LED output

**Date:** 2026-08-05
**Scope:** `aae/aae/led_service_handler.cpp`, `aae/system/input/linux/evdev_device.{h,cpp}`, `aae/system/input/linux/evdev_input.cpp`, `tools/linux/uinput_devices.cpp`
**Status:** approved, ready to plan

## Problem

AAE drives the keyboard's Num/Caps/Scroll LEDs as stand-ins for a cabinet's
start-button lamps. Seven drivers already do this — `asteroid`, `bwidow`,
`bzone`, `mhavoc`, `milliped`, `omegrace`, `pacman` — all through
`set_led_status()`.

On Windows this works: `led_service_handler.cpp:29-422` enumerates keyboard
device interfaces through SetupAPI, opens a handle to each, and a low-priority
worker thread applies `IOCTL_KEYBOARD_SET_INDICATORS` whenever the mask changes.

On Linux every entry point is a stub (`led_service_handler.cpp:21-27`), so those
games run with dead lamps. `osdepend.h:313` already records the gap:
`// --- LEDs ---- win: yes  linux: STUB  teensy: STUB`.

The existing stub comment also names the intended mechanism — evdev `EV_LED`
writes, deliberately deferred to be done with the evdev backend rather than
half-done in a Win32 file. This spec is that work.

## Evidence

- **The evdev backend is already shaped for write-side device access.**
  `EvdevDevice::Open()` (`evdev_device.cpp:91-98`) tries `O_RDWR` first and falls
  back to `O_RDONLY`, recording the outcome in `m_writable`. Force feedback
  already rides on that: `SupportsRumble()` is `m_hasRumble && m_writable`
  (`evdev_device.h:76`), and `Classify()` probes the FF capability bits
  (`evdev_device.cpp:197`). LEDs follow the identical pattern.
- **Devices are already open and stay open.** The LED write goes to an existing
  fd — no enumeration, no second open, and none of the Windows SetupAPI
  machinery is needed.
- **Nothing calls `EVIOCGRAB`.** The keyboards are shared with the desktop, so
  on X11/Wayland libinput also owns those LEDs and will reset them on keyboard
  activity. This is what forces a periodic re-apply.
- **The flatpak sandbox already permits it.** `packaging/flatpak/io.github.tcottrill.AAE.yml`
  already carries `--device=input` (line 52) and `--device=all` (line 55). **No
  packaging change is required.**
- **`EvdevInput_Poll()`** (`evdev_input.cpp:456`) already runs every frame on the
  game thread and is the natural home for the re-apply heartbeat.
- **The uinput harness exists and is capable.** `tools/linux/uinput_devices.cpp`
  creates two synthetic keyboards, chmods the resulting nodes to 0666, and
  supports `--selftest`, `--script` and `--exec` (which drops back to the
  invoking user so the program under test runs unprivileged).

## Design

### 1. Hoist the neutral bookkeeping out of the `#ifdef`

`set_led_status`, `get_led_status` and `set_led_status_all` are pure logic:
latch three booleans, fold them into a mask, call `osd_set_leds`. They are
currently written twice in `led_service_handler.cpp` — a real copy in the Win32
branch (`:364-420`) and a do-nothing copy in the `#ifndef _WIN32` branch
(`:21-23`).

Move the real copy outside the `#ifdef` so there is one implementation for both
platforms, along with the three `g_ledN_*` latch variables. Only the four `osd_*`
functions remain platform-specific. This is a prerequisite, not a nicety: it is
what reduces the Linux port to implementing four functions.

### 2. LED capability and write, in `EvdevDevice`

Mirroring the rumble members exactly:

- `bool m_hasLeds` — set in `Classify()` from `EVIOCGBIT(EV_LED, ...)`, true when
  the device reports at least one of `LED_NUML` / `LED_CAPSL` / `LED_SCROLLL`.
  Probing the sub-bits rather than just the `EV_LED` type bit matches how
  `m_hasRumble` tests for `FF_RUMBLE` specifically.
- `bool SupportsLeds() const { return m_hasLeds && m_writable; }`
- `bool SetLeds(int mask)` — writes three `input_event` records
  (`type = EV_LED`, `code = LED_NUML|LED_CAPSL|LED_SCROLLL`, `value = 0|1`)
  followed by an `EV_SYN`/`SYN_REPORT`, to the already-open fd.
- `int GetLeds() const` — `EVIOCGLED`, returning AAE's 0/1/2 mask. Used only for
  the restore snapshot.

Capability comes from the EV_LED bits, never from `EvdevKind`. This follows the
existing rule that classification never trusts a device name, and it means an
LED-capable node gets writes regardless of how it was classified.

The AAE index → Linux code mapping is written explicitly
(0→`LED_NUML`, 1→`LED_CAPSL`, 2→`LED_SCROLLL`) rather than relying on the fact
that those constants happen to be 0, 1 and 2.

The capability log line at `evdev_device.cpp:223` gains an LED field, and the
`m_hasRumble && !m_writable` warning at `:232` gets an LED twin, so "device has
LEDs but the node is read-only" is reported as the permissions problem it is
rather than looking like missing hardware.

### 3. The four `osd_*` entry points

Implemented in `evdev_input.cpp`, which owns the `s_devices` table. **No service
thread**: unlike the Windows IOCTL path this is a small write to an already-open
fd, so it runs inline on the game thread with no locking to get wrong.

- `osd_led_service_start()` — snapshot every LED-capable device's current state
  via `GetLeds()`, mark the service active.
- `osd_set_leds(int mask)` — store the desired mask, write it to every
  `SupportsLeds()` device immediately, reset the heartbeat timer.
- `osd_get_leds()` — return the desired mask (matching the Windows semantic of
  reporting what was requested, not what the hardware currently shows).
- `osd_led_service_stop()` — restore each device's snapshot, mark inactive.

### 4. Re-apply heartbeat

Because the devices are not grabbed, libinput/X11 resets these LEDs on keyboard
activity. `EvdevInput_Poll()` re-applies the desired mask when more than 250ms
has elapsed since the last apply, with no new thread.

**This has no Windows counterpart, despite appearances.** The Win32 worker's
comment (`led_service_handler.cpp:287`) claims it "periodically re-appl[ies]
LEDs", but the loop does not: on `WAIT_TIMEOUT` it `continue`s without applying
(`:302-303`), and even when signalled it applies only if `desired != lastApplied`
(`:309`). Its 250ms is a clean-shutdown wake, as `:300` correctly says. Windows
gets away with apply-on-change-only because the keyboard class driver holds the
indicator state; on Linux another client owns the same LEDs and overwrites them.

So do **not** describe the Linux heartbeat as matching a Windows cadence — it is
a genuinely new behavior that Linux needs and Windows does not. 250ms is chosen
on its own merits: fast enough that a clobbered LED corrects within about a
quarter second, cheap enough to be irrelevant beside the per-frame read loop.

The heartbeat runs only while the service is active, i.e. between
`osd_led_service_start()` and `osd_led_service_stop()`. Before start and after
stop the poll touches no LEDs at all.

On a dedicated cabinet nothing contends and the cost is three writes per quarter
second per keyboard. On a desktop this is what makes the feature work at all.

**Stated consequence:** while a lamp-driving game runs on a desktop, the physical
Num/Caps/Scroll LEDs stop reflecting actual Num/Caps/Scroll state. That is the
feature working as intended, but it is a visible behavior change.

### 5. Restore on exit

`osd_led_service_start()` snapshots and `osd_led_service_stop()` restores, so
quitting AAE does not leave a desktop user's NumLock light wrong.

This is a **deliberate divergence from Windows**, which simply leaves the LEDs
off at shutdown (`aae_emulator.cpp:1949`, `:2014` call `osd_set_leds(0)`).
Approved on 2026-08-05.

### 6. Pause behavior — flagged decision

`EvdevInput_Poll()` has an `s_paused` state in which fds are still drained but
events are not acted on (`evdev_input.cpp:473-477`).

**This is what will be built unless overridden:** suspend the heartbeat while
paused and restore the snapshot on the pause transition, re-applying the desired
mask on resume. Continuing to fight the desktop for LED control while AAE is
unfocused is rude, and this is consistent with the restore-on-exit decision
above.

Flagged because it was not part of the approved design discussion. The
alternative — hold the LEDs regardless of focus — is a one-line difference and
can be switched at plan time.

### 7. Permissions

A node opened read-only yields `SupportsLeds() == false` and is skipped. Reported
once through the existing `s_lastComplaint` mechanism (`evdev_input.cpp:387-393`),
which already states permissions problems explicitly rather than letting them
look like absent hardware.

The requirement is membership in the `input` group, or an equivalent udev rule.
No flatpak manifest change is needed.

## Verification

Extend `tools/linux/uinput_devices.cpp`:

1. Advertise `EV_LED` with `LED_NUML`, `LED_CAPSL`, `LED_SCROLLL` on the two
   synthetic keyboards it already creates.
2. Add a mode that reads `EV_LED` events back off the uinput fds while a program
   under test runs via `--exec`, and asserts:
   - each AAE mask produces exactly the expected set of `EV_LED` events on
     **every** synthetic keyboard, not just the first;
   - re-applying an unchanged mask does not produce spurious events beyond the
     250ms heartbeat;
   - `osd_led_service_stop()` restores the pre-run state rather than clearing to
     zero.

This covers the mapping, capability and lifecycle logic in WSL. It cannot prove a
physical LED illuminates; that stays a real-hardware item, most cheaply checked
by running Asteroids on the Pi 5 and watching the LEDs follow the 1P/2P start
lamps.

## Non-goals

- **The MAME-style output module.** Routing lamp state to an external consumer
  (network/file output for real cabinet hardware) remains out of scope.
- **LED index 3.** Some drivers request a fourth LED (`omegrace.cpp:368` passes a
  literal `3`) which the 0..2 mapping silently drops. **Not a defect and not to
  be "fixed":** a PC keyboard has three indicators, so there is no fourth one to
  map a fourth lamp onto. Routing extra lamps anywhere real is the output
  module's job. Pre-existing and identical on Windows.
- **Teensy.** Still `STUB` per `osdepend.h:313`.
- **Grabbing devices.** `EVIOCGRAB` would end the LED contention outright but
  changes input behavior globally; out of scope.

## Files touched

| File | Change |
|---|---|
| `aae/aae/led_service_handler.cpp` | hoist the `set_led_status` trio and its latch variables out of the `#ifdef`; reduce the Linux stub block to the four `osd_*` functions, then delete it |
| `aae/system/input/linux/evdev_device.h` | `SupportsLeds()`, `SetLeds()`, `GetLeds()`, `m_hasLeds` |
| `aae/system/input/linux/evdev_device.cpp` | EV_LED capability probe in `Classify()`, the write and read implementations, capability log field, read-only warning twin |
| `aae/system/input/linux/evdev_input.cpp` | the four `osd_*` entry points, snapshot/restore state, heartbeat in `EvdevInput_Poll()` |
| `tools/linux/uinput_devices.cpp` | EV_LED on the synthetic keyboards; LED read-back assertions |
