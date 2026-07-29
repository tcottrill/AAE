# Phase 3c Implementation Plan — X11 window, GLX context, evdev input

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the real `aae` executable build, run and be playable on Linux — a window you can see, a GL context that renders, and keyboard/mouse/gamepad input with full per-device multi-HID routing.

**Architecture:** Xlib window + GLX context behind Phase 3a's `ISystemWindow`/`IPresentSurface`; one evdev backend replacing Windows' four input APIs. Presentation lives in its own file so a future Wayland or Vulkan backend reuses the window logic. The device abstraction (`EvdevDevice`) is separate from the neutral input surface (`evdev_input.cpp`) so four device classes don't tangle into one file.

**Tech Stack:** C++17, g++ 15.2 / Ubuntu 26.04 under WSL2, CMake 4.2.3 + Ninja. Xlib, GLX, `<linux/input.h>`. Windows stays MSVC 2022 + MSBuild. **No test framework** — the tests are the build, the game count, a rendered frame, and an interactive pass.

**Spec:** `docs/superpowers/specs/2026-07-29-phase3c-linux-window-input-design.md`

---

## Ground rules

**Parity is the goal.** Evidence of *sameness*, not merely of working. Nothing is dropped; deferrals name a successor phase.

**Never remove whole-archive linking.** `$<LINK_LIBRARY:WHOLE_ARCHIVE,aae_core>` / `/WHOLEARCHIVE:`. Without it every self-registering driver silently vanishes — 83 games in Phase 2. **Check the game count, never just that the build is green.**

**Both build systems stay in lockstep.** Any file added to a vcxproj goes in `CMakeLists.txt` too, and vice versa. Never `file(GLOB)`.

### Verification commands

Windows (authoritative build):

```bash
"/c/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" aae/aae.vcxproj -t:Rebuild -p:Configuration=Release -p:Platform=x64 -v:q -nologo
```

Expected: exit 0, **exactly six warning lines**.

Windows game count — note `-listallgames` writes a FILE and `exit(0)`s, it does not print to stdout, and **MSBuild outputs to `aae/x64/Release/`, not the stale tracked `x64/Release/`**:

```bash
cd aae/x64/Release && ./aae.exe -listallgames && grep "Total games:" "AAE All Games List.txt"
```

Expected: `Total games: 132`.

Linux build (put multi-line shell work in a `.sh` file — PowerShell eats `$` before `wsl.exe` sees it, and `/tmp` does not survive between invocations):

```bash
wsl -d Ubuntu -- bash /mnt/c/Source2026/AAE_publish/scripts/linux/build.sh aae
```

Linux game count:

```bash
wsl -d Ubuntu -- bash -c "cd /mnt/c/Source2026/AAE_publish/x64/Release && ../../build-linux/aae -listallgames && grep 'Total games:' 'AAE All Games List.txt'"
```

Expected: `Total games: 132` — the same number Windows reports.

---

## File structure

| File | Action | Responsibility |
|---|---|---|
| `aae/system/graphics/sys_gl.h` | modify | drop the dead `GetGLDC`/`GetGLRC` |
| `aae/system/graphics/sys_gl.cpp` | modify | `#ifdef _WIN32` WGL branch + GLX branch |
| `aae/system/graphics/sys_texture.cpp` | modify | remove the `win32/win32_private.h` dependency |
| `aae/system/window/linux/glx_present.h/.cpp` | **create** | `GlxPresentSurface : IPresentSurface` |
| `aae/system/window/linux/linux_window.h/.cpp` | **create** | `LinuxWindow : ISystemWindow` |
| `aae/system/input/linux/evdev_device.h/.cpp` | **create** | one `/dev/input/event*` node |
| `aae/system/input/linux/evdev_keymap.h/.cpp` | **create** | `KEY_*` → `AAEKEY_*` |
| `aae/system/input/linux/evdev_input.cpp` | **create** | the neutral `sys_input.h` surface |
| `CMakeLists.txt` | modify | new sources; delete `EXCLUDE_FROM_ALL` |

---

# Milestone A — a window that renders (Tasks 1–8)

---

### Task 1: Delete the dead Win32 accessors from `sys_gl.h`

`HDC GetGLDC()` and `HGLRC GetGLRC()` are the last Windows types in an otherwise neutral header, and **both have zero callers anywhere in the codebase** — the same shape as Phase 3b's dead `HR()` macro.

**Files:** modify `aae/system/graphics/sys_gl.h`, `aae/system/graphics/sys_gl.cpp`

- [ ] **Step 1: Confirm they are still dead before deleting**

```bash
grep -rn "GetGLDC\|GetGLRC" aae/ --include=*.cpp --include=*.h | grep -v "sys_gl\."
```

Expected: no output. If anything appears, STOP — a caller has been added since this plan was written and the fix is different.

- [ ] **Step 2: Delete both declarations from `sys_gl.h`**

Remove the `HDC GetGLDC();` and `HGLRC GetGLRC();` lines and their comment blocks. Replace with a note:

```c
// GetGLDC()/GetGLRC() were removed in Phase 3c. They returned HDC/HGLRC -
// the last Win32 types in this otherwise neutral header - and had no callers
// anywhere. Anything needing the platform GL handles should go through
// IPresentSurface (system/window/sys_window.h) instead.
```

- [ ] **Step 3: Delete both definitions from `sys_gl.cpp`**

- [ ] **Step 4: Confirm the header is Win32-free**

```bash
grep -nE "\b(HDC|HGLRC|HWND|HANDLE|DWORD|BOOL|LPARAM|WPARAM)\b" aae/system/graphics/sys_gl.h
```

Expected: matches only inside comments.

- [ ] **Step 5: Windows build green**

```bash
"/c/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" aae/aae.vcxproj -t:Rebuild -p:Configuration=Release -p:Platform=x64 -v:q -nologo
```

Expected: exit 0, exactly six warnings.

- [ ] **Step 6: Commit**

```bash
git add aae/system/graphics/sys_gl.h aae/system/graphics/sys_gl.cpp
git commit -m "refactor(gl): remove the dead GetGLDC/GetGLRC accessors

They returned HDC/HGLRC - the last Win32 types in an otherwise neutral
header - and had no callers anywhere in the codebase. Same shape as the
dead HR() macro Phase 3b found in mixer.cpp: Windows types load-bearing
for nothing. sys_gl.h can now be included from the Linux build unchanged."
```

---

### Task 2: Remove `sys_texture.cpp`'s `win32_private.h` dependency

**Files:** modify `aae/system/graphics/sys_texture.cpp`

- [ ] **Step 1: Find out what it actually needs**

```bash
grep -n "win32_private\|win_get_window\|HWND" aae/system/graphics/sys_texture.cpp
```

Record every symbol it uses from that header. If the only include is `win32/win32_private.h` and nothing from it is referenced (likely — Phase 3a found several such dead includes), this is a one-line deletion.

- [ ] **Step 2: Remove the include if unused; guard `<windows.h>` if still needed**

If symbols ARE used, wrap both the include and the using code in `#ifdef _WIN32` and note in the commit what still needs it.

- [ ] **Step 3: Compile under g++**

```bash
wsl -d Ubuntu -- bash -c "cd /mnt/c/Source2026/AAE_publish && g++ -std=c++17 -fsyntax-only -Iaae/system/graphics -Iaae/system/util -Iaae/system/3rdparty -Iaae/aae -Iaae aae/system/graphics/sys_texture.cpp && echo OK"
```

Expected: `OK`. Fix whatever it reports — expect missing includes MSVC supplied transitively.

- [ ] **Step 4: Windows build green, then commit**

```bash
git add aae/system/graphics/sys_texture.cpp
git commit -m "refactor(gfx): sys_texture.cpp no longer depends on win32_private.h"
```

---

### Task 3: Split `sys_gl.cpp` into WGL and GLX branches

**Files:** modify `aae/system/graphics/sys_gl.cpp`

- [ ] **Step 1: Read the current structure**

```bash
grep -n "^[a-zA-Z].*(\|wgl\|WGL\|ChoosePixelFormat\|SetPixelFormat" aae/system/graphics/sys_gl.cpp | head -40
```

Record which functions touch WGL. `InitOpenGLContext`, `DeleteGLContext`, `GLSwapBuffers` and `SetvSync` are the four expected to; `ReSizeGLScene`, `ViewOrtho`, `CheckGLErrorEx` and `CheckGLVersionSupport` should be pure GL and need no branch.

- [ ] **Step 2: Guard the Windows-only includes**

```c
#ifdef _WIN32
#include <windows.h>
#include "glew.h"
#include "wglew.h"
#include "win32/win32_private.h"
#pragma comment(lib, "glu32.lib")
#pragma comment(lib, "opengl32.lib")
#else
#include <GL/glew.h>
#include <GL/glx.h>
#endif
```

- [ ] **Step 3: Add the GLX branch of `InitOpenGLContext`**

The Linux implementation needs the X11 display and window, which `LinuxWindow` owns. Rather than have `sys_gl.cpp` reach into the window, add a small setter the window calls before `InitOpenGLContext`:

```c
#ifndef _WIN32
// Set by LinuxWindow::Create() before InitOpenGLContext() runs. sys_gl.cpp
// deliberately does NOT include linux_window.h - the dependency runs one way,
// window -> GL, exactly as it does on Windows where winmain passes the HWND.
void sys_gl_set_x11_target(void* display, unsigned long window);
#endif
```

Store them in file-scope statics and use them in the GLX path: `glXChooseFBConfig` → `glXCreateContextAttribsARB` with `GLX_CONTEXT_MAJOR_VERSION_ARB=3, MINOR=3, GLX_CONTEXT_PROFILE_MASK_ARB=GLX_CONTEXT_CORE_PROFILE_BIT_ARB` → `glXMakeCurrent`.

**`glXCreateContextAttribsARB` must be looked up via `glXGetProcAddressARB`** — it is an extension entry point, not a linkable symbol. Requesting a core profile without it silently gives a legacy context, and the `#version 330 core` shaders then fail at runtime rather than at context creation.

- [ ] **Step 4: `GLSwapBuffers` and `SetvSync` on GLX**

```c
// GLSwapBuffers: glXSwapBuffers(dpy, win);
// SetvSync:      glXSwapIntervalEXT(dpy, win, enabled ? 1 : 0)
//                (looked up via glXGetProcAddressARB; fall back to
//                 glXSwapIntervalMESA / glXSwapIntervalSGI, and if none
//                 exist, LOG_WARN that vsync is unavailable rather than
//                 silently doing nothing)
```

- [ ] **Step 5: Compile under g++ (will not link yet — no window)**

```bash
wsl -d Ubuntu -- bash -c "cd /mnt/c/Source2026/AAE_publish && g++ -std=c++17 -fsyntax-only -Iaae/system/graphics -Iaae/system/util -Iaae/system/3rdparty -Iaae/aae -Iaae -Iaae/system/window aae/system/graphics/sys_gl.cpp && echo OK"
```

- [ ] **Step 6: Windows build green, run a game to prove the GL path is untouched**

```bash
cp aae/x64/Release/aae.exe x64/Release/aae_t3c.exe && cd x64/Release && timeout 15 ./aae_t3c.exe asteroid
```

Expected: renders normally. Then `rm -f x64/Release/aae_t3c.exe`.

- [ ] **Step 7: Commit**

```bash
git add aae/system/graphics/sys_gl.cpp
git commit -m "feat(gl): GLX branch alongside the WGL one in sys_gl.cpp"
```

---

### Task 4: `GlxPresentSurface`

**Files:** create `aae/system/window/linux/glx_present.h`, `aae/system/window/linux/glx_present.cpp`

- [ ] **Step 1: Create the header**

```cpp
//==============================================================================
// glx_present.h -- IPresentSurface over GLX.
//
// Separate from linux_window.cpp on purpose: Phase 3a split presentation from
// windowing so a headless backend could return nullptr, and the same split is
// what will let a future Wayland or Vulkan backend reuse the window logic
// unchanged. It also keeps GL headers out of linux_window.cpp.
//==============================================================================
#pragma once

#include "sys_window.h"

class GlxPresentSurface : public IPresentSurface {
public:
	// Called by LinuxWindow::Create once the X11 window exists.
	void Attach(void* display, unsigned long window);

	void SwapBuffers() override;
	void GetDrawableSize(int* w, int* h) const override;

	const char* const* RequiredVkInstanceExtensions(uint32_t* count) const override;
	bool CreateVkSurface(void* instance, void* outSurface) override;

private:
	void*         m_display = nullptr;   // Display* - kept void* so this header
	unsigned long m_window  = 0;         // needs no Xlib include
};
```

- [ ] **Step 2: Implement it**

`SwapBuffers` → `glXSwapBuffers`. `GetDrawableSize` → `XGetGeometry`. The two Vulkan methods return `nullptr`/`false` with a comment naming **Phase 4** — the Pi needs Vulkan because Mesa v3d tops out near GL/GLES 3.1, but that is not this phase.

- [ ] **Step 3: Compile under g++, then commit**

```bash
git add aae/system/window/linux/glx_present.h aae/system/window/linux/glx_present.cpp
git commit -m "feat(window): GlxPresentSurface implementing IPresentSurface"
```

---

### Task 5: `LinuxWindow` — create, destroy, pump, geometry

**Files:** create `aae/system/window/linux/linux_window.h`, `aae/system/window/linux/linux_window.cpp`

Mirror `Win32Window`'s shape exactly (`aae/system/window/win32/win32_window.h`) so the two read as siblings.

- [ ] **Step 1: Create the header**

```cpp
#pragma once

#include "sys_window.h"
#include "linux/glx_present.h"

class LinuxWindow : public ISystemWindow {
public:
	bool Create(const WindowSetup& setup) override;
	void Destroy() override;
	bool PumpEvents() override;

	int   ClientWidth()  const override;
	int   ClientHeight() const override;
	float DpiScale()     const override;

	void ToggleBorderlessFullscreen() override;
	void RestoreViewport() override;

	void SetCursorVisible(bool visible) override;
	void EnableCursorClip(bool enable) override;
	void ForceCursorClipUpdate() override;
	void SetMousePos(int x, int y) override;
	void GetMousePos(int* x, int* y) const override;

	IPresentSurface* Presentation() override { return &m_presentSurface; }

private:
	GlxPresentSurface m_presentSurface;
	// Xlib handles live in the .cpp via a pimpl or file-scope struct so this
	// header stays free of <X11/Xlib.h> - which #defines Bool, Status, None
	// and other names that collide loudly with ordinary C++ code.
	struct Impl;
	Impl* m_impl = nullptr;
};
```

**The Xlib-out-of-the-header point is not stylistic.** `<X11/Xlib.h>` `#define`s `Bool`, `None`, `Status`, `Success` and `KeyPress`. Any header that includes it poisons every translation unit downstream — `None` in particular collides with common enum names and produces errors far from the cause.

- [ ] **Step 2: Implement `Create`**

`XOpenDisplay(nullptr)` → `XCreateWindow` sized from `setup.windowWidth/windowHeight` (centred if `setup.centerWindow`) → set `WM_DELETE_WINDOW` via `XSetWMProtocols` → `XSelectInput` for `StructureNotifyMask | FocusChangeMask | ExposureMask` → `XMapWindow` → `m_presentSurface.Attach(dpy, win)` → `sys_gl_set_x11_target(dpy, win)`.

**Input events are deliberately NOT selected here.** Keyboard and mouse come from evdev, not X11, because evdev is what gives per-device multi-HID identity. X11 events are only for window management.

If `XOpenDisplay` returns null, log the `DISPLAY` environment variable in the error — under WSL a missing or wrong `DISPLAY` is the single most likely cause and the message should say so.

- [ ] **Step 3: Implement `PumpEvents`**

`XPending`/`XNextEvent` loop handling `ConfigureNotify` (update client size), `FocusIn`/`FocusOut` (update `isFocused`, and re-apply or release the pointer grab), and `ClientMessage` for `WM_DELETE_WINDOW` (return false to request quit). Return true to continue.

- [ ] **Step 4: `ClientWidth`/`ClientHeight`/`DpiScale`**

Width/height from the cached `ConfigureNotify` size. For `DpiScale`, read the `Xft.dpi` X resource via `XResourceManagerString` and return `dpi / 96.0f`, defaulting to `1.0f` when absent — X11 has no per-monitor DPI API equivalent to Windows'.

- [ ] **Step 5: Compile, then commit**

```bash
git add aae/system/window/linux/linux_window.h aae/system/window/linux/linux_window.cpp
git commit -m "feat(window): LinuxWindow - create, destroy, event pump, geometry"
```

---

### Task 6: `LinuxWindow` — fullscreen and cursor

**Files:** modify `aae/system/window/linux/linux_window.cpp`

Each of these has a plausible-looking wrong answer, which is why the spec records them.

- [ ] **Step 1: `ToggleBorderlessFullscreen` via EWMH**

Send a `_NET_WM_STATE` ClientMessage toggling `_NET_WM_STATE_FULLSCREEN` to the root window with `SubstructureNotifyMask | SubstructureRedirectMask`.

Do **not** use override-redirect windows: they bypass the window manager, break alt-tab, and misbehave under compositors including gamescope.

- [ ] **Step 2: `SetCursorVisible`**

X11 has no `ShowCursor` counter. Hide by creating a 1×1 transparent pixmap cursor once and `XDefineCursor`; show with `XUndefineCursor`. Create the pixmap cursor at `Create` time and free it in `Destroy` — making one per call leaks an X resource every frame if a caller toggles per-frame.

- [ ] **Step 3: `EnableCursorClip` / `ForceCursorClipUpdate`**

`XGrabPointer` with `confine_to = window`, `owner_events = True`, `GrabModeAsync` for both. Release with `XUngrabPointer`.

**The grab must be released on focus loss and re-applied on focus gain** (Task 5 Step 3 wired the events). A pointer grab held while the window is unfocused makes the whole desktop unusable — the X11 equivalent of a stuck `ClipCursor`, and much harder for a user to escape.

- [ ] **Step 4: `SetMousePos` / `GetMousePos`**

`XWarpPointer` and `XQueryPointer`. Both are in window-relative coordinates, matching the Win32 side.

- [ ] **Step 5: Commit**

```bash
git add aae/system/window/linux/linux_window.cpp
git commit -m "feat(window): EWMH fullscreen, pointer confine, cursor visibility"
```

---

### Task 7: Temporary evdev stub so `aae` links

The window half is testable before input exists. A stub for the ~40 input functions gets `aae` linking now, and Milestone B replaces it.

**Files:** create `aae/system/input/linux/evdev_input.cpp` (stub form)

- [ ] **Step 1: Write the stub**

Define `key[256]`, `mouse_b`, and every function `sys_input.h` declares, all returning zero/false/empty. Model it on `aae/headless/null_backends.cpp`, which already stubs a subset.

Add at the top:

```c
// TEMPORARY (Phase 3c Milestone A). This file is a stub so the aae target can
// link and the X11/GLX window can be tested on its own. Milestone B replaces
// every one of these with the real evdev implementation. If you are reading
// this in a shipped build, something went wrong.
#warning "evdev_input.cpp is still the Milestone A stub - no input will work"
```

The `#warning` is deliberate: a silently-stubbed input layer looks exactly like broken hardware.

- [ ] **Step 2: Wire everything into CMake and drop `EXCLUDE_FROM_ALL`**

Add `linux_window.cpp`, `glx_present.cpp`, `evdev_input.cpp` to the Linux `AAE_PLATFORM_SOURCES`. The `EXISTS`-guarded list should now find all four backends, so `_aae_missing_backends` is empty and the `AAE_EXCLUDE_FROM_ALL` block resolves to nothing on its own — verify that rather than assuming, then delete the block and its comment.

Link `X11`, `GL` and `GLU`.

- [ ] **Step 3: Build `aae` on Linux**

```bash
wsl -d Ubuntu -- bash /mnt/c/Source2026/AAE_publish/scripts/linux/build.sh aae
```

Expect a first pass of missing includes and unresolved symbols from files never compiled by g++ before (`aae_emulator.cpp`, `winmain.cpp`'s replacement, `menu.cpp`, `driver_gui.cpp`, the `aae_video/` GL files). Fix them here; each is a real portability defect. Record anything non-obvious in the commit.

- [ ] **Step 4: THE game-count check**

```bash
wsl -d Ubuntu -- bash -c "cd /mnt/c/Source2026/AAE_publish/x64/Release && ../../build-linux/aae -listallgames && grep 'Total games:' 'AAE All Games List.txt'"
```

Expected: `Total games: 132`. Fewer means whole-archive linking regressed — check `$<LINK_LIBRARY:WHOLE_ARCHIVE,aae_core>` is still in the link before looking anywhere else.

- [ ] **Step 5: Run it and see a window**

```bash
wsl -d Ubuntu -- bash -c "cd /mnt/c/Source2026/AAE_publish/x64/Release && ../../build-linux/aae asteroid"
```

Expected: a window appears (WSLg provides XWayland and software Mesa) and Asteroids renders. **No input will work** — that is Milestone B. If the window appears but is black, check the GL context is core 3.3 and the shaders compiled; the log will say.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat(linux): aae links and renders on Linux; input still stubbed"
```

---

### Task 8: Milestone A checkpoint

- [ ] Windows MSBuild exit 0, exactly six warnings
- [ ] Windows `Total games: 132`
- [ ] Linux `Total games: 132`
- [ ] Linux `aae asteroid` opens a window and renders
- [ ] Linux `aae pacman` (raster path) also renders

Do not start Milestone B until all five hold.

---

# Milestone B — evdev input (Tasks 9–14)

**A note on the level of detail below.** Tasks 1–7 give literal code because the changes are small and exact. Tasks 11–13 do not: `evdev_input.cpp` is on the order of a thousand lines, and a plan containing a thousand lines of invented code would be worse than useless — it would be code written against remembered headers rather than real ones, and the implementer would spend longer reconciling it than writing it.

What these tasks specify instead is everything that is *hard to recover later*: the exact API surface to satisfy, the semantics that must match Windows (read-and-reset mickeys, Allegro button bit order, edge-triggered combos), the traps (classify by capability not name, atomics not x86 ordering, axis ranges from `EVIOCGABS`), and the verification. The bodies are written against the real `<linux/input.h>` at implementation time.

If a step below feels under-specified when you reach it, that is a signal to re-read `sys_input.h` and `joystick.h` — the contract is the specification.

---

### Task 9: `EvdevDevice` — one input node

**Files:** create `aae/system/input/linux/evdev_device.h`, `evdev_device.cpp`

- [ ] **Step 1: Design the type**

```cpp
enum class EvdevKind { Unknown, Keyboard, Mouse, Gamepad };

class EvdevDevice {
public:
	bool Open(const std::string& eventPath, const std::string& byIdPath);
	void Close();

	int         fd()   const { return m_fd; }
	EvdevKind   kind() const { return m_kind; }
	const std::string& name() const { return m_name; }   // EVIOCGNAME, friendly
	const std::string& path() const { return m_path; }   // by-id if available
	bool        seenInput() const { return m_seenInput; }

	// Reads all pending events; returns false if the device disappeared
	// (unplugged -> ENODEV), which the caller uses to drop it.
	bool ReadEvents(std::vector<input_event>& out);

private:
	int  m_fd = -1;
	EvdevKind m_kind = EvdevKind::Unknown;
	std::string m_name, m_path;
	bool m_seenInput = false;
};
```

- [ ] **Step 2: Classify by capability, not by name**

Query `EVIOCGBIT`. A device with `EV_KEY` bits in the `KEY_A..KEY_Z` range is a keyboard; one with `EV_REL` `REL_X`/`REL_Y` plus `BTN_LEFT` is a mouse; one with `BTN_GAMEPAD`/`BTN_SOUTH` or `EV_ABS` `ABS_X`/`ABS_Y` is a gamepad.

Classifying by device *name* looks easier and is wrong: many keyboards expose several event nodes (one of which carries only volume keys), and gaming mice enumerate a keyboard node for their macro keys. Capability bits are the only reliable signal.

- [ ] **Step 3: Enumerate with stable identity**

Walk `/dev/input/by-id/`, `readlink` each entry to its `event*` node, and keep the by-id path as `m_path`. Devices with no by-id entry fall back to their event path — logged as weak identity, because a user whose player assignment does not survive a reboot needs to know why.

- [ ] **Step 4: Distinguish "no devices" from "permission denied"**

```cpp
// A backend that finds nothing because of permissions looks exactly like a
// broken one. Say which it is, and say the fix.
if (errno == EACCES) {
    LOG_ERROR("evdev: permission denied opening %s - add your user to the "
              "'input' group (sudo usermod -aG input $USER) and log out/in",
              eventPath.c_str());
}
```

- [ ] **Step 5: Commit**

---

### Task 10: `KEY_*` → `AAEKEY_*` translation

**Files:** create `aae/system/input/linux/evdev_keymap.h`, `evdev_keymap.cpp`

- [ ] **Step 1: Build the table**

A `static const uint8_t kEvdevToAae[KEY_MAX+1]` mapping Linux codes to `AaeKey` values (`sys_input.h:460`, 120 values, `AAEKEY_A = 0x41`). Unmapped entries are 0.

This is the reason Phase 1 renamed `KEY_*` to `AAEKEY_*` rather than renumbering: `KEY_A` is 30 in `<linux/input-event-codes.h>` and 0x41 here, and the two sets must coexist in this one file.

- [ ] **Step 2: Guard against the trap this file is**

A wrong entry produces a key that silently does the wrong thing — no build check catches it. Add:

```cpp
// Spot-checks across the ranges most likely to be mistyped. A table is exactly
// the kind of thing that compiles perfectly and is quietly wrong.
static_assert(kEvdevToAae[KEY_A]     == AAEKEY_A,     "KEY_A must map to AAEKEY_A");
static_assert(kEvdevToAae[KEY_Z]     == AAEKEY_Z,     "KEY_Z must map to AAEKEY_Z");
static_assert(kEvdevToAae[KEY_0]     == AAEKEY_0,     "digit row");
static_assert(kEvdevToAae[KEY_ESC]   == AAEKEY_ESC,   "escape");
static_assert(kEvdevToAae[KEY_SPACE] == AAEKEY_SPACE, "space");
static_assert(kEvdevToAae[KEY_LEFT]  == AAEKEY_LEFT,  "arrow cluster");
static_assert(kEvdevToAae[KEY_F1]    == AAEKEY_F1,    "function row");
```

(`static_assert` on a `const` array element needs the array to be `constexpr` — Phase 3b hit exactly this with the TMS5220 tables, MSVC C2131.)

- [ ] **Step 3: Add a coverage report at startup**

Log how many of the 120 `AAEKEY_*` values have at least one evdev source. A gap means a key the user cannot bind, and it is far better surfaced at startup than discovered mid-game.

- [ ] **Step 4: Commit**

---

### Task 11: Keyboard path

**Files:** modify `aae/system/input/linux/evdev_input.cpp` (replacing the Task 7 stub incrementally)

- [ ] **Step 1: The poll thread**

One thread, `poll()` across every open device fd. Simpler than the Windows worker-thread + message-pump split, and the natural fit for file descriptors.

- [ ] **Step 2: Publish state with explicit atomics**

`key[256]` and `mouse_b` are read from the game thread and written here. `sys_input.h` notes the Win32 backend's "safe on x86" reasoning does **not** carry to ARM — and the Pi is a target — so use `std::atomic` with explicit ordering rather than relying on x86 store ordering.

- [ ] **Step 3: Implement the merged and per-device APIs**

`IsKeyDown`/`isKeyHeld`/`IsKeyUp`/`GetModifierFlags` over the merged state; `RawInput_GetKeyboardCount`/`GetKeyboardName`/`GetKeyboardPath`/`FindKeyboardByPath`/`KeyboardSeenInput`/`IsKeyDownEx` over per-device state. Both read the same event stream, exactly as the Windows backend does.

- [ ] **Step 4: Remove the `#warning` for the keyboard portion; commit**

---

### Task 12: Mouse path

- [ ] **Step 1: Relative motion** — accumulate `REL_X`/`REL_Y` per device into mickey counters; `get_mouse_mickeys` reads and RESETS, `get_mouse_mickeys_ex(index, ...)` does the same per device. The read-and-reset semantics matter: trackball and spinner games integrate these, and a missed reset doubles the reported motion.
- [ ] **Step 2: Buttons** — `BTN_LEFT/RIGHT/MIDDLE` into the `mouse_b` bitmask (bit 0 left, 1 right, 2 middle — Allegro-compatible, as the header documents).
- [ ] **Step 3: Wheel** — `REL_WHEEL` into `GetMouseWheel`/`GetMouseWheelChange`.
- [ ] **Step 4: Per-device API** — `RawInput_GetMouseCount`/`GetMouseName`/`GetMousePath`/`FindMouseByPath`/`GetMouseButtons`/`MouseSeenInput`.
- [ ] **Step 5: Commit**

---

### Task 13: Gamepad path

The `joystick.h` contract is already neutral (Phase 3b removed its `<windows.h>`/`<Xinput.h>` and replaced the combo masks with `AAE_JOYBTN_*`).

- [ ] **Step 1: Fill `joy[MAX_JOYSTICKS]`** — `JOYSTICK_INFO`/`STICK`/`AXIS`/`BUTTON` from `EV_ABS` (`ABS_X/Y/Z/RX/RY/RZ`, `ABS_HAT0X/Y`) and `EV_KEY` (`BTN_SOUTH` … `BTN_THUMBR`). Scale axes from each device's `EVIOCGABS` range to the Allegro-style −128..127 the structs expect — do not assume a range.
- [ ] **Step 2: `install_joystick`/`poll_joystick`/`remove_joystick`**, plus hotplug (a device vanishing gives `ENODEV` on read; new devices appear as new `by-id` entries on rescan).
- [ ] **Step 3: `joystick_check_combo`** over `AAE_JOYBTN_*`, edge-triggered once per press. Map evdev `BTN_*` onto the same bits `Joystick.cpp` static_asserts against XInput.
- [ ] **Step 4: Rumble** via `EVIOCSFF`/`write()` of an `ff_effect` with `FF_RUMBLE` — this is the parity item for `joystick_set_rumble`/`joystick_stop_rumble`.
- [ ] **Step 5: Identity** — `joystick_get_id`/`joystick_find_by_id` returning the by-id path, so player assignments survive reboots.
- [ ] **Step 6: Delete the Task 7 `#warning` entirely; commit**

---

### Task 14: Full regression and phase report

- [ ] Windows MSBuild exit 0, exactly six warnings
- [ ] Windows CMake builds every target
- [ ] `Total games: 132` on **both** platforms
- [ ] Vector counts still `89,414` / `353,693` on both (`aae_headless` must not have regressed)
- [ ] Linux: asteroid and pacman both render and are **playable from the keyboard**
- [ ] Linux: mouse works; a trackball/spinner game responds
- [ ] Linux: gamepad works, including the three `JOY_COMBO_*` combos
- [ ] Linux: **two keyboards produce independent per-player input** through the `_Ex` API — this is the multi-HID parity claim and it needs two physical devices
- [ ] Rumble confirmed on real hardware (WSLg cannot judge this)
- [ ] Windows interactive pass: fullscreen toggle, cursor clip, alt-tab focus

- [ ] **Write the outcome** into the spec as `## Phase 3c outcome`, recording what was verified where, what WSLg could not judge, and anything that changes the plan for Phase 3d (positional audio) or 4 (Vulkan).

Anything not verified is reported as **unverified**, never assumed — the standard Phase 3b set with Linux audio.
