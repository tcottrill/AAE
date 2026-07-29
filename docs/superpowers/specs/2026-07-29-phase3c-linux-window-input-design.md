# Phase 3c Design — X11 window, GLX context, evdev input

**Date:** 2026-07-29
**Status:** Approved for planning
**Branch context:** follows `refactor/phase3b-linux-backends`
**Predecessors:** Phase 1 (OSD contract), Phase 2 (core lib), Phase 3a (window contract), Phase 3b (Linux toolchain, portable utilities, ALSA)

---

## 0. The end point, restated

**Linux must do everything Windows does. That is the programme's #1 goal and its definition of done** (user, 2026-07-28). Nothing is dropped; things are only ordered. Every deferral in this document names a successor phase.

Phase 3c is the phase that makes `aae` — the actual emulator, with a window you can see and controls you can use — run on Linux. At the end of it the Linux build stops being a headless proof and becomes the program.

---

## 1. What 3b left, and what 3c owes

Phase 3b delivered a Linux `aae_core` (88/88 TUs), an `aae_headless` whose vector counts are **identical** to Windows (asteroid 600 → 89,414; bzone 600 → 353,693), an ALSA audio backend, and portable logging/paths/file-IO/timer.

It deliberately did **not** deliver a window or input, so `CMakeLists.txt` still reports two missing backends and the `aae` target is `EXCLUDE_FROM_ALL` on Linux. That is this phase's job:

```
aae/system/window/linux/linux_window.cpp    ISystemWindow  (11 methods)
aae/system/input/linux/evdev_input.cpp      ~40 neutral input functions
```

Plus the two files Phase 3b explicitly bucketed here: `sys_gl.cpp`'s WGL→GLX split, and `sys_texture.cpp`'s `win32/win32_private.h` dependency.

### 1.1 Scale, measured

The Windows implementations this phase mirrors:

| file | lines | mirrored by |
|---|---|---|
| `system/window/winmain.cpp` | 1410 | `linux/linux_window.cpp` |
| `system/window/windows_util.cpp` | 271 | (mostly not needed) |
| `system/input/rawinput.cpp` | 1094 | `linux/evdev_input.cpp` |
| `system/input/Joystick.cpp` | 1617 | `linux/evdev_input.cpp` |
| `system/graphics/sys_gl.cpp` | 420 | GLX half of the same file |

~4,800 lines of Windows code. The Linux equivalent will be substantially smaller — much of `winmain.cpp` is message-pump and DPI plumbing with no X11 analogue, and evdev collapses RawInput + XInput + DirectInput + WinMM into **one** device model — but this is still by a wide margin the largest phase in the programme.

---

## 2. Decisions

### 2.1 X11 + GLX, not Wayland (user, 2026-07-29)

Xlib for the window, GLX for the GL 3.3 core context.

- It runs **natively on X11 desktops and under Wayland via XWayland**, so one backend covers the Steam Machine (SteamOS/gamescope) and the Pi today.
- It is roughly a third of the code of a native Wayland backend, which needs xdg-shell protocol boilerplate, client-side decorations, and its own fullscreen/cursor/pointer-lock handling.
- The GL shaders are `#version 330 core` and Mesa `radeonsi` does GL 4.6, so no shader work is needed on the Steam Machine.

**Cost — revised 2026-07-29 after checking how SteamOS actually works.** The original draft of this section called XWayland "a compositing hop" and treated it as the price of the decision. That was overcautious:

- **Gamescope hosts its own XWayland server, and every game in the Steam session runs inside it.** On SteamOS an X11 client is not a fallback — it is the primary, best-supported path, sandboxed in its own virtual desktop that cannot interfere with (or be interfered with by) the session.
- Gamescope receives game frames through XWayland **with no copy inside X**, and can DRM/KMS-flip them straight to the display, compositing with async Vulkan compute only when it must.

So on the Steam Machine specifically, X11 is not merely tolerated, it is the road the platform is paved for. The remaining honest cost is that X11 is the legacy stack generally, and a desktop Pi running a native Wayland session pays an XWayland cost that a native backend would not.

A native Wayland backend therefore stays a **later phase, if measurement ever justifies it** — and because `ISystemWindow` is a real interface, it is a new implementation rather than a rewrite. That is precisely what Phase 3a's abstraction bought.

**Not affected:** the Pi's eventual Vulkan path (Mesa v3d tops out near GL/GLES 3.1, below `#version 330 core`). That remains Phase 4 and is orthogonal to windowing.

### 2.1a SDL: re-evaluated, and used as a reference only (user, 2026-07-29)

SDL was explicitly rejected when this programme started, on the recorded grounds that **SDL2 cannot do per-device keyboards** — which would make the multi-HID goal in §2.2 impossible. That reason is now **out of date and should not be cited again**:

- **SDL3 does support per-device keyboards and mice.** `SDL_GetKeyboards()` / `SDL_GetMice()` return per-device IDs and events carry a `which` field naming the source device.

Two caveats survive, and neither is decisive on its own:

- SDL3 exposes per-device **events**, not per-device **polled state** (an open feature request). AAE's contract is polled — `RawInput_IsKeyDownEx(index, vk)`, `get_mouse_mickeys_ex(index, …)`, `RawInput_GetMouseButtons(index)` — so that layer gets hand-written either way. It is the same work on top of SDL as on top of evdev.
- Device enumeration is platform-variable (both functions return a single device on macOS). Not a target here, but a sign the abstraction is thinner than it looks.

**The decision therefore rests on where SDL would live, not on what it can do.** Taking it on Linux only would leave two divergent implementations of the same 40-function contract — worse than one hand-written evdev backend that mirrors the Windows one. Taking it on both would discard a working, tuned Win32 stack (RawInput multi-HID, XAudio2 voice path) and Phase 3b's ALSA/voice-mixer/timer work.

**Decision: SDL is a reference, not a dependency.** Its X11/GLX backend is the most battle-tested code in existence for GLX attribute selection, EWMH fullscreen, pointer grabs and evdev quirks, and it should be read freely while implementing §4. Nothing links against it.

Worth recording because it is a common objection: **SDL would not have broken the Teensy target.** `ISystemWindow`/`IAudioBackend` already exist, so SDL would have been one more backend behind them rather than a replacement for the architecture. It was not rejected for that reason.

### 2.2 Full multi-HID parity (user, 2026-07-29)

Per-device keyboards and mice with friendly names, path-stable identity, player routing, and gamepads with force-feedback rumble — the whole neutral input surface, not a merged-device subset.

This is the right call for a cabinet: AAE's multi-HID work exists so two players can have their own spinner or trackball, and a merged-input intermediate state cannot run that at all. It is also *easier* on Linux than on Windows, because evdev is one API for all four device classes where Windows needs four.

---

## 3. Measured current state

### 3.1 `sys_gl.h` leaks Win32 types — and they are dead

```c
HDC   GetGLDC();
HGLRC GetGLRC();
```

**Zero callers anywhere in the codebase.** This is the same shape of defect as Phase 3b's `IAudioBackend` (a neutral contract trapped behind an OS header) and the dead `HR()` macro in `mixer.cpp`: the Win32 types are load-bearing for nothing. Deleting both declarations removes the last Windows types from `sys_gl.h` and lets the header be included from the Linux build unchanged.

The rest of `sys_gl.h` is already neutral: `InitOpenGLContext(forceLegacyGL2, enableMultisample, useCoreProfile)`, `DeleteGLContext`, `IsOpenGLInitialized`, `GLSwapBuffers`, `SetvSync`, `CheckGLVersionSupport`, `ReSizeGLScene`, `ViewOrtho`, `CheckGLErrorEx`. Every one maps onto GLX.

### 3.2 The key enum is already portable

Phase 1 renamed `KEY_*` to `AAEKEY_*` precisely because `KEY_A` collides with Linux's `<linux/input-event-codes.h>` (where it is 30, not 0x41). The canonical set stays AAE's own, and **the evdev backend translates `KEY_*` → `AAEKEY_*`**. That translation table is new work in this phase, but the collision that would have made it impossible was designed out three phases ago.

### 3.3 `/dev/input` is permission-gated

Reading `/dev/input/event*` requires membership of the `input` group (or root):

```bash
sudo usermod -aG input $USER    # then log out and back in
```

This is a **deployment** fact, not a code one, but it must be surfaced properly: a backend that silently finds no devices because of permissions is indistinguishable from one with a bug. The backend logs the distinction explicitly (`EACCES` vs "none found"), with the fix in the message.

**On SteamOS this does persist, with a caveat** (checked 2026-07-29). The root filesystem is immutable, but `/etc` — where group membership lives — is a **writable overlay stored under `/var`, explicitly designed to survive OS updates**; that is the same mechanism that preserves the user's password and network configuration. However, `/etc` changes have been reported to occasionally conflict during an update, and SteamOS keeps rollback copies under `/etc/previous` and `/var/lib/steamos-atomupd/etc_backup` for exactly that reason.

**Practical consequence:** group membership is worth re-verifying after a SteamOS update, and the startup log line above is what makes a silent revert obvious rather than mysterious. Any udev rule the backend ever needs (it should not need one — `input` group membership is sufficient for `/dev/input/event*`) has the same persistence story.

Unverified: the actual state of the target machine. `id -nG | tr ' ' '\n' | grep -x input` answers it in one line.

---

## 4. Design

### 4.1 File layout

| File | Responsibility |
|---|---|
| `system/window/linux/linux_window.h/.cpp` | `LinuxWindow : ISystemWindow` — Xlib display/window/atoms, event pump, fullscreen, cursor |
| `system/window/linux/glx_present.h/.cpp` | `GlxPresentSurface : IPresentSurface` — the GL context and `SwapBuffers` |
| `system/input/linux/evdev_device.h/.cpp` | One `/dev/input/event*` node: open, capabilities, name, stable path, read loop |
| `system/input/linux/evdev_input.cpp` | The neutral `sys_input.h` surface over a set of `EvdevDevice`s |
| `system/input/linux/evdev_keymap.h/.cpp` | `KEY_*` → `AAEKEY_*` translation table |
| `system/graphics/sys_gl.cpp` | Gains a GLX branch; loses `GetGLDC`/`GetGLRC` |

Splitting `evdev_device` out from `evdev_input` matters: the device abstraction (a file descriptor with capabilities and an identity) is the same for keyboards, mice and pads, and keeping it separate is what stops `evdev_input.cpp` becoming a 1,500-line file with four device types tangled together — the shape the Windows side is in.

### 4.2 Presentation is separate from windowing

Phase 3a made `IPresentSurface` optional precisely so a headless backend can return `nullptr`. `LinuxWindow` returns a real `GlxPresentSurface`. Keeping the GL context in its own file (rather than inside the window) is what will let a future Wayland or Vulkan backend reuse the window logic — and it keeps `linux_window.cpp` free of GL headers.

### 4.3 Device identity: `/dev/input/by-id/`

The multi-HID player assignment needs identity that survives a reboot and a re-plug. `/dev/input/event7` does not; `/dev/input/by-id/usb-Logitech_Trackball-event-mouse` does. The backend enumerates `by-id` symlinks, resolves them to event nodes, and reports the **by-id path** as the device path through `RawInput_GetMousePath`/`GetKeyboardPath`.

Devices with no `by-id` entry (some virtual and platform devices) fall back to their event-node path, which is honest but weak — and logged as such, so a user whose assignment does not stick can see why.

### 4.4 Threading

`sys_input.h` documents the Win32 worker-thread pump as a **backend choice, not part of the contract**, and warns its "safe on x86" reasoning does not carry to weaker memory models. The evdev backend uses one thread with `poll()` across all device fds — simpler than the Windows design and a natural fit for file descriptors. State published to `key[]`/`mouse_b` uses explicit atomics rather than relying on x86 ordering, so the same code is correct on the Pi's ARM cores.

### 4.5 Fullscreen and cursor on X11

- **Fullscreen** via the EWMH `_NET_WM_STATE_FULLSCREEN` atom, not by overriding redirect. This is what `ToggleBorderlessFullscreen` maps to, and it behaves correctly under compositors including gamescope.
- **Cursor clip** (`EnableCursorClip`) via `XGrabPointer` with `confine_to` set to the window. Windows' `ClipCursor` has no exact analogue; the grab is the closest and is what games use.
- **Cursor visibility** via a 1×1 transparent `XCreatePixmapCursor`. X11 has no `ShowCursor` counter.

Recording these three because each is a place where a plausible-looking alternative behaves subtly wrong, and because `SetMousePos`/`GetMousePos` (`XWarpPointer`/`XQueryPointer`) interact with the grab.

---

## 5. Verification

The parity standard from Phase 3b applies: evidence of **sameness**, not merely of working.

1. `aae` links and runs on Linux — CMake's "not yet implemented" warning is gone and `EXCLUDE_FROM_ALL` is deleted.
2. **`aae -listallgames` reports `Total games: 132` on Linux**, matching Windows. Whole-archive linking is the thing most likely to silently regress here, and this is the check that catches it.
3. A vector game (asteroid) and a raster game (pacman) both boot, render and are playable.
4. Keyboard, mouse and gamepad all work; two keyboards produce independent per-player input through the `_Ex` API.
5. Windows is untouched: MSBuild exit 0, exactly six warnings, `Total games: 132`.

Verification runs under WSLg where it can (WSLg provides XWayland and Mesa software GL, so a window should genuinely appear), and on the Steam Machine for anything WSLg cannot judge — real GPU performance, real evdev devices, rumble.

---

## 6. Risks

**Input device permissions will bite first.** If the user is not in the `input` group, the backend finds nothing and looks broken. Mitigated by logging the distinction between "no devices" and "permission denied on /dev/input/event*" explicitly, with the fix in the message.

**WSLg cannot judge everything.** It has no real gamepad, no rumble, and software GL. Compile-and-run under WSLg is necessary but not sufficient; anything it cannot verify is reported as unverified rather than assumed, exactly as Phase 3b did with Linux audio.

**Key translation is a table, and tables have typos.** A wrong entry produces a key that silently does the wrong thing, which no build check catches. Mitigated by a translation test that walks the table and asserts round-trip consistency, plus an interactive pass.

**This phase is large enough to want splitting.** If the window half lands cleanly and evdev proves bigger than estimated, the window half ships on its own and evdev becomes 3d — with positional audio moving to 3e. Ordering, not scope reduction.

---

## 7. Out of scope, with successors named

| item | phase |
|---|---|
| Native Wayland backend | later, only if the XWayland hop measurably hurts |
| Real positional audio on Linux (X3DAudio matrix maths) | **3d** |
| Vulkan renderer path (required by the Pi) | **4** |
| Teensy 4.1 freestanding target | **5** |

---

## Phase 3c outcome

**Milestone A** (X11 window + GLX context) completed 2026-07-29 — `aae` builds, links, opens a window and renders on Linux.

**Milestone B** (evdev input) completed 2026-07-29. Structure differs from the plan in two deliberate ways, both recorded here because the reasoning is not recoverable from the diff:

1. **No poll thread on Linux.** The plan called for one thread plus `std::atomic` publication. `sys_input.h:19-22` states the Win32 worker thread is a backend design choice rather than part of the contract, and that its "safe on x86" reasoning does not carry to ARM. Since the contract fixes the types (`unsigned char key[256]`, `int mouse_b`), they cannot be made atomic without changing Windows. Draining non-blocking evdev fds on the game thread removes the cross-thread publication entirely instead of trying to make it correct, and suits the Teensy target, which has no threads. **Windows keeps its worker thread untouched** — `rawinput.cpp` was not modified in this phase.
2. **Gamepad in its own file.** `evdev_joystick.cpp` implements `joystick.h`; `evdev_input.cpp` implements `sys_input.h`. This mirrors the Windows split between `Joystick.cpp` and `rawinput.cpp` rather than the single file the plan named.

### New test infrastructure

WSL2 has **no `/dev/input` directory at all** — not empty, absent — so nothing could be enumerated on the dev box. Two tools were built:

- `tools/linux/uinput_devices.cpp` (`aae_uinput_test`) — fabricates two keyboards, a mouse and a gamepad through `/dev/uinput`. It deliberately does *not* imitate well-behaved hardware: the pad advertises axis ranges no real controller uses (`0..1023` unsigned left stick, `-2048..2047` right), and the mouse advertises `BTN_LEFT`, which lives in the `KEY_*` number space. `--exec` runs the program under test as the invoking user, so the whole run is one `sudo` command.
- `aae/inputtest/inputtest_main.cpp` (`aae_inputtest`) — links the REAL evdev backend against the real contract and reports every state change, the twin of `aae_audiotest`. It exists because the emulator does not log key presses, it responds to them, so on a machine with no display a working backend and a dead one produce identical logs.

### Verified by execution

Keymap `static_assert`s (20, compile-time); coverage 107 evdev codes → 106/109 bindable AAE keys. Capability classification (mouse with `BTN_LEFT` classified mouse, `letters=0`; keyboards `letters=26`; pad by `EV_ABS`+`FF_RUMBLE`). Keyboard: letters across three evdev rows, digit row distinct from keypad, arrows, F1/F12, L/R modifiers distinct, modifier flags while held. **Multi-HID: the same key from two keyboards reported `from=[kbd0]` then `from=[kbd1]`, and both held distinct keys simultaneously.** Callbacks fire per event and correctly do NOT fire on auto-repeat. Mouse: read-and-reset mickeys, accumulator, Allegro bit order `L=0x1 R=0x2 M=0x4`, wheel scaled to Win32 `WHEEL_DELTA` (one detent → 120). Gamepad: two different non-standard ranges both scaled correctly from `EVIOCGABS`, dead zone honoured, D-pad override to ±128, buttons in XInput order, all three `JOY_COMBO_*` firing exactly once. Rumble: `strong=49151 (75%) weak=16383 (25%)` for a requested `0.75/0.25`, then PLAY/STOP/erase.

Regression: Windows MSBuild exit 0 with exactly six warnings; `Total games: 132` on both platforms; vector counts unchanged at asteroid 89,414 / bzone 353,693.

### The bug the harness caught

**Analog stick Y was double-inverted.** The first implementation negated Y as `Joystick.cpp` does, with a comment asserting evdev behaved "like XInput". It does not: XInput reports the stick pushed up as *positive* and must be negated to reach AAE's screen-space convention, whereas evdev already reports up as *negative*, which IS that convention. The symptom was that pushing the stick up and pressing D-pad up produced opposite signs, since the D-pad path sets `-128` for up explicitly. Left unfixed, every vertical gamepad control would have been backwards while the keyboard looked fine.

Two test artefacts were also mistaken for backend faults and are worth remembering: a key tapped for less than one frame never appears in a polled view of `key[]` (net state change zero — a real property shared with the Win32 backend, which is why the observer now also watches the callbacks), and the uinput event nodes need `chown` to the invoking user as well as `chmod 0666`, because without udev they are `0600 root:root`.

### Unverified — reported as unverified, never assumed

- **Rumble physically spinning a motor.** The harness proves the correct magnitudes reach the device; no motor exists here.
- **`/dev/input/by-id/` identity.** WSL has no udev, so *every* device in testing fell back to weak event-number identity. Persistent player assignment across reboots is untested.
- **Two real keyboards on separate USB ports**, and real-hardware feel and latency.
- **`aae` itself played from a keyboard** (`asteroid`, `pacman`) — the backend is verified through `aae_inputtest`, not yet through an interactive session.
- **Linux audio still never heard** (carried from 3b; WSLg has no PCM device).

### Pre-existing defect found in passing, not fixed here

`aae.exe -listallgames` on Windows writes a correct file then exits `0xC0000409` (`STATUS_STACK_BUFFER_OVERRUN`). The older shipped binary fails identically and the Windows projects reference no Linux sources, so it predates this phase. It silently breaks the documented `./aae.exe -listallgames && grep ...` verification command, because the `&&` never runs.
