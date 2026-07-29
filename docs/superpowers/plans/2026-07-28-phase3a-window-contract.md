# Phase 3a Implementation Plan — `ISystemWindow`, CMake, and a Headless Proof

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Define the last of the three backend contracts (window), add a CMake build, and **prove the emulation core runs headless** — no window, no renderer, no audio — which is the Teensy target's precondition.

**Architecture:** `WindowSetup` sheds its `RECT`/`DWORD` members; `ISystemWindow` describes a window and `IPresentSurface` describes presentation *separately*, so a headless backend can satisfy the first and return `nullptr` for the second instead of stubbing a swapchain it has no concept of. The existing Win32 code is wrapped, not rewritten. Then a small console target links `aae_core` against null backends and runs a vector game for N frames — if that works, headless is demonstrated rather than asserted, and the symbols it needs are the Teensy porting checklist.

**Tech Stack:** C++17 / MSVC 2022 (v143), MSBuild **and** CMake (both on Windows), x64 only. **No test framework** — the tests are the build, a link-level proof, and a runtime pass.

**Spec:** `docs/superpowers/specs/2026-07-28-phase3a-window-contract-design.md`

---

## Conventions for every task

**The build:**

```bash
"/c/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" aae/aae.vcxproj -t:Rebuild -p:Configuration=Release -p:Platform=x64 -v:q -nologo
```

**Pass state:** exit 0 with exactly these six warning lines. A **seventh is a regression**.

```
cpu_i8085.cpp(452,11): warning C4101: 'temp32': unreferenced local variable
foodf.cpp(76,2):       warning C4333: '>>': right shift by too large amount, data loss
pacman.cpp(104,16):    warning C4018: '<': signed/unsigned mismatch
pacman.cpp(716,16):    warning C4018: '<': signed/unsigned mismatch
phoenix.cpp(392,12):   warning C4018: '<': signed/unsigned mismatch
gaplus_video.cpp(23,16): warning C4018: '<': signed/unsigned mismatch
```

Since Phase 2 these are attributed to `aae_core.vcxproj`, which builds those files. Use `-t:Rebuild` for verification builds; incremental builds legitimately show fewer.

**Two projects since Phase 2.** `aae/aae_core.vcxproj` (StaticLibrary, 86 files, include path **omits** `./aae/aae_video`, `./aae/gui`, `./system/window`) and `aae/aae.vcxproj` (48 files, links the lib). **Never add an omitted directory back to the core's include path** — that restriction is the enforced boundary. Phase 2's negative test must keep passing: `#include "framework.h"` in a core file yields `C1083 file-not-found`.

**`WIN7BUILD` must survive** in both projects' `Release|x64` `PreprocessorDefinitions`.

**Do NOT touch** WIP files: `aae/aae/drivers/bwidow.cpp`, `aae/aae/led_service_handler.cpp`, `aae/aae/drivers/tempest_with_random_checks.cpp`, `aae/aae/sndhrdwr/generic.cpp`, `aae/aae/sndhrdwr/generic.h`.

**Guard rules, learned the hard way in Phases 1-2:**
- A `#error` guard goes **immediately after** the file's `#include` block. Above it, the macro under test is not yet defined and the guard passes unconditionally — a test incapable of failing. Three such guards shipped before being caught.
- **Never write an order-sensitive guard.** `#ifdef SOMEHEADER_H` tests "was this seen earlier in this TU", not "did this header pull it in". Two were removed for firing on correct code (`MATHUTILS_H`, `__XAUDIO2_INCLUDED__`). `_WINDOWS_` in a core TU is fine because it is wrong regardless of order.
- **Never leave HEAD un-buildable.** A guard whose boundary a later task closes is committed commented-out with a `TODO(Task N)` tag.

**Existing live guards — do not disturb:** `_WINDOWS_` in `memory.cpp`, `cpu_code/cpu_6502.cpp`, `drivers/centiped.cpp`, `vidhrdwr/SegaG80vid.cpp`, `vidhrdwr/aae_avg.cpp`, `drivers/invaders.cpp`; `__XAUDIO2_INCLUDED__` in `memory.cpp`. Parked: `acommon.cpp` (includes `framework.h` directly; Task 6 may finally close it).

---

## File Structure

| file | status | responsibility |
|---|---|---|
| `aae/system/window/sys_window.h` | **create** | Platform-neutral: `WindowSetup`, `ISystemWindow`, `IPresentSurface`. No `windows.h`. |
| `aae/system/window/win32/win32_window.h` / `.cpp` | **create** | `Win32Window : ISystemWindow` + `Win32PresentSurface`. Wraps existing code. |
| `aae/system/window/win32/win32_private.h` | **create** | `win_get_window()` (`HWND`), `Win32WindowState` (style/exStyle/disableNC). For `winmain.cpp` and raw input only. |
| `aae/system/window/framework.h` | modify → shrink | Retains only what is genuinely Win32 and not window-shaped, or disappears entirely. |
| `aae/system/util/sys_dialog.h` | **create** | `aae_message_box()` — `allegro_message`/`osMessage` are not window operations. |
| `aae/headless/headless_main.cpp` | **create** | The proof: links `aae_core`, null backends, runs N frames, counts vectors. |
| `aae/headless/null_backends.cpp` | **create** | `NullWindow : ISystemWindow` (returns `nullptr` for presentation) + stub `osd_*`. |
| `CMakeLists.txt` | **create** | Builds `aae_core`, `aae`, `aae_headless` on Windows. |
| 19 `framework.h` consumers | modify | Switch to `sys_window.h`; keep a private include only where genuinely Win32. |

---

## Task 0: Baseline

- [ ] **Step 1: Confirm HEAD is green**

Run the build. Expected exit 0, exactly six warning lines. If not, STOP.

- [ ] **Step 2: Confirm the Phase 2 boundary still holds**

Add `#include "framework.h"` as the first line of `aae/aae/drivers/asteroid.cpp`, build, and confirm:

```
asteroid.cpp(1,10): error C1083: Cannot open include file: 'framework.h': No such file or directory [aae_core.vcxproj]
```

Revert the line; confirm `git diff` is clean. **This exact error must still appear at the end of the phase** — it is the invariant everything else is built on.

- [ ] **Step 3: Confirm CMake is available**

```bash
cmake --version
```

Expected: a version string. If absent, STOP and report — Task 5 cannot proceed.

---

## Task 1: Survey `WindowSetup` — measurement only, no code changes

`WindowSetup` (`aae/system/window/framework.h:10-51`) is ~40 fields of **live mutable state**, not just configuration, and 19 files see it. Editing it blind is the largest risk in this phase. This task produces the evidence; Task 2 acts on it.

**Files:** none modified. Produce a written report.

- [ ] **Step 1: Enumerate every field and its readers/writers**

For each member of `WindowSetup`, find every read and every write across the tree:

```bash
grep -rn 'GetWindowSetup()\|WindowSetup' --include=*.cpp --include=*.h aae/ | wc -l
```

then per field, e.g.:

```bash
grep -rn '\.style\b\|->style\b' --include=*.cpp --include=*.h aae/
```

Produce a table: `field | type | files reading | files writing | verdict (NEUTRAL / WIN32-PRIVATE)`.

The spec's provisional verdicts, **to be confirmed not assumed**:
- `rect`, `windowedRect`, `screenRect` (`RECT`) → NEUTRAL, need a neutral rectangle type
- `style`, `exStyle` (`DWORD`), `disableNC`, `disableRoundedCorners` → WIN32-PRIVATE
- the other ~30 (`bool`/`int`/`float`) → already NEUTRAL

**Any WIN32-PRIVATE field with a reader outside `aae/system/window/` is a design problem, not a mechanical one — report it, do not improvise.**

- [ ] **Step 2: Decide the rectangle type**

Read `aae/system/math/rect2d.h`. Report:
- What type(s) it defines and their member names
- Whether its semantics are left/top/right/bottom (like Win32 `RECT`) or x/y/width/height

Then find every arithmetic use of the three rect fields:

```bash
grep -rn 'rect\.\(right\|left\|top\|bottom\)\|Rect\.\(right\|left\|top\|bottom\)' --include=*.cpp --include=*.h aae/
```

**Recommendation to make in your report:** if `rect2d.h`'s semantics differ from `RECT`'s, prefer defining a small neutral rect that matches `RECT` semantics over rewriting call-site arithmetic. A silent geometry inversion will not fail the build and will not be obvious at runtime.

- [ ] **Step 3: Enumerate the `framework.h` function surface**

For each function `framework.h` declares, list its callers and classify: `WINDOW` (belongs on `ISystemWindow`), `PRESENT` (belongs on `IPresentSurface`), `DIALOG` (`allegro_message`, `osMessage`, `GetLastErrorStdStr`), or `WIN32-PRIVATE` (`win_get_window`).

- [ ] **Step 4: Report**

No commit — this task produces a report that Tasks 2-4 consume. Include every table above plus any surprise.

### STATUS: Task 1 completed 2026-07-28. Findings the later tasks depend on:

1. **No WIN32-PRIVATE field leaks outside `system/window/`.** `style`, `exStyle`, `disableNC`, `disableRoundedCorners` are touched only by `winmain.cpp` and `windows_util.cpp`. Task 2 is a relocation, not a design problem.
2. **`rect2d.h`'s `Rect2D` is extents-based** (x/y/width/height + cached half-extents, floats) while `RECT` and all ~15 touch sites are edge-based. **Define a new `SysRect{int left,top,right,bottom;}`** — converting edge arithmetic to extents risks a silent geometry inversion that compiles cleanly and misbehaves only at particular window sizes. All 15 sites are in two files, both under `system/window/`.
3. **`opengl_renderer.h` is cheap to close** (Task 4) — and doing so retires `acommon.cpp`'s parked `_WINDOWS_` guard, since `acommon.cpp` needs nothing from `framework.h` by either path.
4. **9 of 15 functions and 8 of 19 consumers are dead or side-effect-only** (Task 4).
5. **The headless blocker is `Machine`'s storage**, not orchestration (Task 6 Step 1a).
6. **`aae_emulator.cpp` has independent Win32 needs** beyond `WindowSetup` — `GetSystemMetrics(SM_CXSCREEN/CYSCREEN)` at `:779-780` and a raw `DWORD` at `:201`. It stays executable-side this phase regardless.

**Latent bug found, deliberately NOT fixed here** (this phase changes no behaviour): `screenRect` is written at `winmain.cpp:1164-1167` and never read anywhere; `minWindowHeight` is never read or written at all; and `minWindowWidth` is applied as a clamp at `winmain.cpp:908` with **no corresponding height clamp** — so a window can be resized below its minimum height but not its minimum width. Worth a follow-up.

---

## Task 2: Neutralise `WindowSetup`

**Files:** `aae/system/window/sys_window.h` (create), `aae/system/window/framework.h` (modify), `aae/system/window/win32/win32_private.h` (create), plus any consumer touching a moved field.

- [ ] **Step 1: Create `sys_window.h` with the neutral struct**

```c
#pragma once
// ===========================================================================
// sys_window.h - the platform-neutral window contract.
//
// Backends: Win32 (today), GUI/Linux (Wayland or X11, serving both the Steam
// Machine and the Pi), and Headless (Teensy - no window at all).
//
// Nothing here may reference windows.h or any platform type.
// ===========================================================================
#include <cstdint>

// Neutral rectangle. Member semantics match Win32 RECT (edges, not extents)
// so the ~N existing call sites doing `rect.right - rect.left` keep working.
struct SysRect { int left = 0, top = 0, right = 0, bottom = 0; };

struct WindowSetup {
    SysRect rect{};
    SysRect windowedRect{};
    SysRect screenRect{};
    bool  borderlessFullscreen = false;
    bool  useFullscreen = false;
    bool  centerWindow = true;
    bool  useAspectRatio = false;
    bool  aspectOverrideActive = false;
    int   windowWidth = 1024, windowHeight = 768;
    int   clientWidth = 0, clientHeight = 0;
    int   minWindowWidth = 320, minWindowHeight = 240;
    bool  resizable = false;
    bool  dpiAware = true;
    float dpiScale = 1.0f;
    bool  isMinimized = false, isFocused = true;
    bool  cursorClipEnabled = true;
    int   startingMonitor = 1;
    // Copy the REMAINING neutral fields verbatim from framework.h's original
    // struct, preserving names, types, defaults and comments. Do not
    // reorder, rename or "tidy" them - consumers read them by name.
};

WindowSetup& GetWindowSetup();
```

Use whatever `SysRect` shape Task 1 Step 2 recommended; if `rect2d.h` already provides an edge-semantics rectangle, use that instead of defining `SysRect` and say so.

- [ ] **Step 2: Move the Win32-only fields**

Create `aae/system/window/win32/win32_private.h`:

```c
#pragma once
// Win32-only window state. PRIVATE to the Win32 backend - only files under
// system/window/win32/ and winmain.cpp may include this.
#include <windows.h>

struct Win32WindowState {
    DWORD style = 0;
    DWORD exStyle = 0;
    bool  disableNC = false;
    bool  disableRoundedCorners = false;
};

Win32WindowState& GetWin32WindowState();
extern HWND win_get_window();
```

Move exactly the fields Task 1 confirmed as WIN32-PRIVATE. Update their (few) readers to use `GetWin32WindowState()`.

- [ ] **Step 3: Build**

Run the build. Expected exit 0, six warning lines.

Expect `error C2039: 'style': is not a member of 'WindowSetup'` at any reader you missed. If such a reader is **outside** `system/window/`, STOP and report — that contradicts Task 1's survey and is a design question.

- [ ] **Step 4: Commit**

```bash
git add aae/system/window/
git commit -m "refactor(window): neutralise WindowSetup

RECT becomes an edge-semantics SysRect; the Win32-only style/exStyle/
disableNC/disableRoundedCorners move to a backend-private struct. The ~30
remaining fields were already platform-neutral and are unchanged."
```

---

## Task 3: Define `ISystemWindow` and `IPresentSurface`

**Files:** `aae/system/window/sys_window.h` (modify), `aae/system/util/sys_dialog.h` (create)

- [ ] **Step 1: Add the two interfaces to `sys_window.h`**

```c
class IPresentSurface;

// The window contract. A headless backend (Teensy) implements this and
// returns nullptr from Presentation().
class ISystemWindow {
public:
    virtual ~ISystemWindow() = default;

    virtual bool Create(const WindowSetup& setup) = 0;
    virtual void Destroy() = 0;

    // Drain the platform event queue. Returns false when the app should quit.
    virtual bool PumpEvents() = 0;

    virtual int   ClientWidth()  const = 0;
    virtual int   ClientHeight() const = 0;
    virtual float DpiScale()     const = 0;

    virtual void ToggleBorderlessFullscreen() = 0;
    virtual void RestoreViewport() = 0;

    virtual void SetCursorVisible(bool visible) = 0;
    virtual void EnableCursorClip(bool enable) = 0;
    virtual void ForceCursorClipUpdate() = 0;
    virtual void SetMousePos(int x, int y) = 0;
    virtual void GetMousePos(int* x, int* y) const = 0;

    // Presentation is OPTIONAL. nullptr means this backend does not present
    // to a display - a headless/Teensy backend, where video is redirected
    // into the vector/DAC path. That is not an error condition.
    virtual IPresentSurface* Presentation() { return nullptr; }
};

// Implemented only by backends that actually present to a display.
class IPresentSurface {
public:
    virtual ~IPresentSurface() = default;

    virtual void SwapBuffers() = 0;
    virtual void GetDrawableSize(int* w, int* h) const = 0;

    // Instance extensions this platform requires (VK_KHR_surface plus one
    // platform extension), NULL-terminated.
    virtual const char* const* RequiredVkInstanceExtensions(uint32_t* count) const = 0;
    // void* are VkInstance / VkSurfaceKHR*, so this header needs no Vulkan.
    virtual bool CreateVkSurface(void* instance, void* outSurface) = 0;
};

// The active window. Never null after startup; the headless backend supplies
// a real ISystemWindow whose Presentation() is nullptr.
ISystemWindow& GetSystemWindow();
```

- [ ] **Step 2: Create `sys_dialog.h`**

`allegro_message` and `osMessage` are message boxes, not window operations, and do not belong on `ISystemWindow`:

```c
#pragma once
// Platform-neutral user-facing message. Win32 shows a MessageBox; a headless
// backend logs. Not a window operation - hence its own header.
void aae_message_box(const char* title, const char* text);
void aae_message_boxf(int flags, const char* fmt, ...);
```

Keep the existing signatures and semantics from `framework.h` — match them exactly rather than redesigning. `GetLastErrorStdStr()` is Win32-only diagnostics and stays in the Win32 backend.

- [ ] **Step 3: Build and commit**

The interfaces have no implementations yet, so nothing should change behaviourally. Expected exit 0, six warning lines.

```bash
git add aae/system/window/sys_window.h aae/system/util/sys_dialog.h
git commit -m "feat(window): define ISystemWindow and IPresentSurface

Presentation is deliberately separate and optional so a headless backend
can satisfy the window contract honestly - returning nullptr rather than
stubbing a swapchain it has no concept of. That case is the test of whether
this abstraction is real."
```

---

## Task 4: `Win32Window` implements the interfaces

**Files:** `aae/system/window/win32/win32_window.h` / `.cpp` (create), `aae/system/window/framework.h` (shrink), the 19 consumers.

This is a **re-homing of working code, not a rewrite.** Move existing bodies from `framework.h`/`windows_util.cpp`/`winmain.cpp` into methods. Do not change behaviour.

- [ ] **Step 1: Implement `Win32Window` and `Win32PresentSurface`**

`Win32Window : ISystemWindow` wraps the existing window/cursor/DPI code. `Win32PresentSurface : IPresentSurface` wraps buffer swapping and reports `VK_KHR_surface` + `VK_KHR_win32_surface`. `Win32Window::Presentation()` returns the surface.

Provide `GetSystemWindow()` returning the singleton, mirroring how `GetWindowSetup()` already works.

- [ ] **Step 2: Repoint the 19 consumers**

Switch each from `framework.h` to `sys_window.h` (plus `sys_dialog.h` where it used `allegro_message`/`osMessage`). Files that genuinely need Win32 — `winmain.cpp`, `win10_win11_required_code.cpp`, `windows_util.cpp` — include `win32/win32_private.h` instead. That is correct, not a shortfall.

**`aae/aae/aae_video/opengl_renderer.h` — Task 1 found this is CHEAP, not cascading.** The header itself uses nothing from `framework.h`; every symbol it declares is `int`/`bool`/`float`/`rtex_t`/`aae::math::mat4`. Only its own `.cpp` calls `GetWindowSetup()` (lines 157, 379, 577, 1552), and those are plain `bool`/`float` field reads. Of the 17 files including it, the 5 that are Win32-heavy already carry their own direct `framework.h` include.

So: **move `#include "framework.h"` from `opengl_renderer.h` into `opengl_renderer.cpp`.** Expected to be a two-line change with no fan-out.

**Nine of `framework.h`'s fifteen functions are dead** — `GetClientWidth`, `GetClientHeight`, `osMessage`, `GetLastErrorStdStr`, `ForceCursorClipUpdate`, `SetMousePos`, `GetMousePos`, and the two explicitly-deprecated `ClipAndHideCursor`/`UnclipAndShowCursor`. **Delete them rather than porting them onto the interface** — but grep once more for each before deleting, including for function-pointer references, and report anything that turns out to be live.

That leaves the genuine cross-module surface at four: `GetWindowSetup()`, `win_get_window()`, `ToggleBorderlessFullscreen()`, `allegro_message()`.

**Eight of the nineteen consumers are DEAD or side-effect-only** w.r.t. `framework.h`: `acommon.cpp`, `mame_fileio.cpp`, `config.cpp`, `osd_video.cpp`, `galsnd_stream.cpp`, `namco.cpp` use nothing from it; `led_service_handler.cpp` and `win10_win11_required_code.cpp` use nothing *declared* by it but do need the transitive `<Windows.h>` — those two get a direct `#include <windows.h>` instead of a deletion. `mame_fileio.cpp` and `config.cpp` need only `MAX_PATH`, likewise.

- [ ] **Step 3: Build**

Expected exit 0, six warning lines. Expect `C2065` for Win32 symbols in a file that lost `framework.h`; fix by adding `win32/win32_private.h` **to that `.cpp`** if it is genuinely Win32, or by using the neutral interface if it is not. Record every file and which it was.

- [ ] **Step 4: Try to close `acommon.cpp`'s parked guard**

If `opengl_renderer.h` no longer pulls `framework.h`, uncomment `acommon.cpp`'s `_WINDOWS_` guard and build. If green, leave it live and update the comment to past tense. If it fires, re-park it and update the comment to name the real remaining path.

- [ ] **Step 5: Commit**

```bash
git add aae/system/window/ aae/aae/ aae/system/graphics/
git commit -m "refactor(window): Win32Window implements ISystemWindow

Existing window/cursor/DPI code re-homed behind the interface, not
rewritten. The <N> consumers now include sys_window.h; the genuinely Win32
ones take win32/win32_private.h."
```

---

## Task 5: CMake for Windows

**Files:** `CMakeLists.txt` (create)

The vcxproj stays the primary Windows build. CMake is added alongside and must produce a working `aae.exe`. Getting it right on the platform that can be verified is the point — the Linux build then becomes a source-list swap.

- [ ] **Step 1: Write `CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.20)
project(aae LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 17)

# --- Platform backend selection -------------------------------------------
# The whole point of this structure: adding Linux later is a source-list
# swap, not a rewrite. Keep platform files OUT of the shared lists.
if(WIN32)
    set(AAE_PLATFORM_SOURCES
        aae/system/window/win32/win32_window.cpp
        aae/system/window/winmain.cpp
        aae/system/window/windows_util.cpp
        aae/system/window/win10_win11_required_code.cpp
        aae/system/input/rawinput.cpp
        aae/system/input/Joystick.cpp
        aae/system/audio/xaudio2_backend.cpp
        aae/system/util/wintimer.cpp
    )
    set(AAE_PLATFORM_DEFINES _WINDOWS GLEW_STATIC
        _DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR WIN7BUILD)
endif()

# --- aae_core: the emulation core -----------------------------------------
# Include list deliberately OMITS system/window, aae/aae_video and aae/gui.
# That omission is the enforced boundary - do not add them.
# Both lists are EXPLICIT, derived from the two vcxproj files so they cannot
# silently drift. Generate them once with:
#   grep -o 'ClCompile Include="[^"]*"' aae/aae_core.vcxproj \
#     | sed 's|ClCompile Include="||; s|"$||; s|\\|/|g' | sort
# and the same against aae/aae.vcxproj. Paste the results below verbatim.
# Do NOT use file(GLOB) - it silently picks up new files and would let the
# core/exe split drift apart from the vcxproj without anyone noticing.
set(AAE_CORE_SOURCES
    aae/aae/cpu_code/cpu_6502.cpp
    aae/aae/drivers/asteroid.cpp
    # ...the full 86 from aae_core.vcxproj...
)
set(AAE_EXE_SOURCES
    aae/aae/aae_emulator.cpp
    aae/aae/acommon.cpp
    # ...the 48 from aae.vcxproj, MINUS the platform files already listed
    #    in AAE_PLATFORM_SOURCES above, so nothing is compiled twice...
)

add_library(aae_core STATIC ${AAE_CORE_SOURCES})
target_include_directories(aae_core PUBLIC
    aae/aae aae/aae/sndhrdwr aae/aae/vidhrdwr aae/aae/machine
    aae/aae/fileio aae/aae/drivers aae/aae/cpu_code
    aae/system/audio aae/system/graphics aae/system/input
    aae/system/math aae/system/util aae/system/3rdparty aae/
)
target_compile_definitions(aae_core PRIVATE ${AAE_PLATFORM_DEFINES})

# --- aae: the executable ---------------------------------------------------
add_executable(aae ${AAE_EXE_SOURCES} ${AAE_PLATFORM_SOURCES})
target_include_directories(aae PRIVATE aae/system/window aae/aae/aae_video aae/aae/gui)
target_link_libraries(aae PRIVATE aae_core)
target_compile_definitions(aae PRIVATE ${AAE_PLATFORM_DEFINES})
```

Derive `AAE_CORE_SOURCES` and `AAE_EXE_SOURCES` from the two vcxproj files so the lists cannot drift — 86 and 48 respectively. Prefer explicit lists over globs if the glob proves fragile; correctness beats brevity here.

- [ ] **Step 2: Configure and build**

```bash
cmake -S . -B build-cmake -A x64
cmake --build build-cmake --config Release
```

Expected: configures and produces `aae.exe`.

- [ ] **Step 3: Verify parity**

Confirm the CMake `aae_core` include list omits `system/window`, `aae_video` and `gui` — the boundary must hold in **both** build systems. Test it: temporarily add `#include "framework.h"` to a core file, build with CMake, confirm a file-not-found error. Revert.

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt
git commit -m "build: add CMake for the Windows targets

Alongside the vcxproj, not replacing it. Platform sources are a single
swappable variable so the Linux build is a source-list swap rather than
blind CMake debugging on a machine we cannot reach."
```

---

## Task 6: The headless proof

**This is the task that answers "is the core truly separate?"** Not by assertion — by linking it against nothing but stubs and running a game.

**Files:** `aae/headless/headless_main.cpp`, `aae/headless/null_backends.cpp` (create), `CMakeLists.txt` (modify)

- [ ] **Step 1: Write the null backends**

`aae/headless/null_backends.cpp`:

```c
#include "sys_window.h"
#include "osdepend.h"

// A window backend with no window. Presentation() returns nullptr - this
// backend does not present to a display. On the Teensy, video is redirected
// into the vector/DAC path instead.
class NullWindow : public ISystemWindow {
public:
    bool Create(const WindowSetup&) override { return true; }
    void Destroy() override {}
    bool PumpEvents() override { return true; }
    int  ClientWidth()  const override { return 1024; }
    int  ClientHeight() const override { return 768; }
    float DpiScale()    const override { return 1.0f; }
    void ToggleBorderlessFullscreen() override {}
    void RestoreViewport() override {}
    void SetCursorVisible(bool) override {}
    void EnableCursorClip(bool) override {}
    void ForceCursorClipUpdate() override {}
    void SetMousePos(int, int) override {}
    void GetMousePos(int* x, int* y) const override { *x = 0; *y = 0; }
    // Presentation() inherits the base nullptr - deliberately not overridden.
};

static NullWindow g_null_window;
ISystemWindow& GetSystemWindow() { return g_null_window; }

// --- OSD stubs -------------------------------------------------------------
// Add one stub per link error, and RECORD EACH ONE. The resulting list is
// the checklist a Teensy backend must implement.
```

- [ ] **Step 2: Write the headless main**

**Task 1 already answered this, and the answer is better than feared.**

The blocker is **not** that orchestration lives in the exe. The CPU and driver primitives are already clean and already in `aae_core`: `cpu_run()` (`cpu_control.cpp:640`, one frame of CPU cycles, no video/audio/window dependency), `driver_registry.cpp`, `memory.cpp`, `timer_init()`, and every driver's own `init_game()`/`run_game()`/`end_game()` callbacks.

**The blocker is a single global's storage.** `aae/aae/aae_emulator.cpp:135-136`:

```c
static struct RunningMachine machine;
struct RunningMachine* Machine = &machine;
```

`Machine` is declared `extern` in `aae_mame_driver.h:295` and dereferenced by `cpu_control.cpp`, `memory.cpp` and nearly every driver — but its *storage* exists only in `aae_emulator.cpp`, which is executable-side. **Linking `aae_core` alone therefore fails with an unresolved external for `Machine`**, not merely a missing convenience wrapper.

- [ ] **Step 1a: Move `Machine`'s storage into the core**

Move those two lines out of `aae_emulator.cpp` into a new small core-side file, `aae/aae/machine_state.cpp`, added to `aae_core.vcxproj`'s `ClCompile` list. Nothing else moves. `aae_emulator.cpp` keeps using `Machine` via the existing `extern` declaration.

Build the executable afterwards and confirm exit 0 with the six warning lines — this must be behaviour-neutral, since it relocates a definition without changing it.

**If a second unresolved global appears when you link the headless target, apply the same treatment and record it.** Task 1 found only `Machine`, but the linker is the authority.

**Do NOT move `run_game()`, `emulator_init()` or `emulator_run()`.** Those interleave core steps with OSD ones (GL, artwork, audio, NVRAM, window aspect) and splitting them is a separate phase. `headless_main.cpp` implements its own minimal loop instead — see Step 2.

```c
// Counts vectors emitted by the emulation core with no display anywhere.
// If this runs, the core is headless-capable - which is the Teensy
// target's precondition.
static long g_vector_count = 0;

void add_line(float, float, float, float, int, rgb_t) { ++g_vector_count; }
void add_tex(float, float, int, rgb_t) {}
void cache_clear() {}
void set_texture_id(rtex_t*) {}

int main(int argc, char** argv)
{
    const char* game = (argc > 1) ? argv[1] : "asteroid";
    const int   frames = (argc > 2) ? atoi(argv[2]) : 600;

    if (!headless_init(game)) {          // whatever core entry point you resolved above
        printf("headless: failed to init '%s'\n", game);
        return 2;
    }
    for (int i = 0; i < frames; ++i) {
        headless_run_one_frame();        // likewise
    }
    headless_shutdown();

    printf("headless: %s ran %d frames, %ld vectors emitted\n",
           game, frames, g_vector_count);
    return g_vector_count > 0 ? 0 : 1;
}
```

`headless_init` / `headless_run_one_frame` / `headless_shutdown` are thin local wrappers over whichever core entry points you resolved in the paragraph above — define them in this same file. Name the real functions they call in your report.

Providing `add_line` here **is the point** — it is exactly what a Teensy backend does, except driving DACs instead of a counter.

- [ ] **Step 3: Add the target to CMake**

```cmake
add_executable(aae_headless
    aae/headless/headless_main.cpp
    aae/headless/null_backends.cpp)
target_link_libraries(aae_headless PRIVATE aae_core)
# NOTE: no aae_video, no system/window, no audio backend, no GL.
```

- [ ] **Step 4: Link it — and treat every error as data**

```bash
cmake --build build-cmake --config Release --target aae_headless
```

**Every `LNK2019: unresolved external symbol` is an item on the Teensy porting checklist.** For each: either add a stub to `null_backends.cpp`, or — if the symbol should not be reachable from the core at all — record it as a genuine core/OSD boundary leak worth fixing later.

**Keep the complete list.** It is the single most valuable output of this phase. Expect `osd_*`, `LOG_INFO`, and audio symbols; be suspicious of anything renderer-shaped.

If the list exceeds ~40 distinct symbols, stop and report before stubbing them all — that would mean the core is less separable than Phase 2 suggested, which is a finding in itself.

- [ ] **Step 5: Run it**

```bash
./build-cmake/Release/aae_headless.exe asteroid
```

Expected: `headless: asteroid ran 600 frames, NNNNN vectors emitted` with a non-zero count, exit 0.

**A non-zero vector count is the proof.** The 6502 executed, the DVG walked its display list, and vectors were produced with no window, no renderer, no audio and no OS display — which is precisely the Teensy scenario.

If it links but emits zero vectors, the core runs but something is not driving the vector generator — report rather than guessing.

- [ ] **Step 6: Commit**

```bash
git add aae/headless/ CMakeLists.txt
git commit -m "test: headless target proves the core runs with no display

Links aae_core against a null window (Presentation() == nullptr) and stub
OSD services, runs <N> frames of asteroid, and counts vectors emitted
through add_line. A non-zero count demonstrates - rather than asserts -
that the emulation core is display-independent, which is the Teensy
target's precondition.

The <N> symbols this needed stubbed are the Teensy porting checklist; see
the commit body / report."
```

---

## Task 7: Verification

- [ ] **Step 1: Both builds green**

MSBuild: exit 0, exactly six warning lines. CMake: configures and builds `aae`, `aae_core`, `aae_headless`.

- [ ] **Step 2: The Phase 2 boundary still holds**

Add `#include "framework.h"` to `aae/aae/drivers/asteroid.cpp`; both build systems must fail with file-not-found. Revert; confirm `git diff` clean.

- [ ] **Step 3: `sys_window.h` is neutral**

```bash
grep -n 'windows\.h\|HWND\|RECT\|DWORD\|LPARAM\|WPARAM' aae/system/window/sys_window.h
```

Expected: comments only.

- [ ] **Step 4: Runtime — exercise exactly what this phase touched**

Run `x64/Release`-side builds of `asteroid`, `pacman` and `bzone`. For each verify: window creates at the right size; **borderless-fullscreen toggle** works both ways; resize behaves; cursor clips and unclips entering/leaving the menu; alt-tab loses and regains focus cleanly; DPI scaling is right.

Then confirm the CMake-built `aae.exe` behaves identically to the MSBuild one — build both, run both, compare.

- [ ] **Step 5: Headless still passes**

```bash
./build-cmake/Release/aae_headless.exe asteroid
./build-cmake/Release/aae_headless.exe bzone
```

Both non-zero vector counts.

- [ ] **Step 6: Commit any fixes**

If a step failed, fix and commit. If everything passed, say so explicitly rather than making an empty commit.

---

## Done criteria

- [ ] `sys_window.h` exists, is free of Win32 types, and declares `WindowSetup`, `ISystemWindow`, `IPresentSurface`
- [ ] `Win32Window` implements them; the 19 consumers use the neutral header
- [ ] `WindowSetup` has no `RECT`/`DWORD` members
- [ ] MSBuild green with exactly six warning lines; CMake builds all three targets
- [ ] Phase 2's negative test passes in **both** build systems
- [ ] **`aae_headless asteroid` reports a non-zero vector count**
- [ ] The Teensy porting checklist (Task 6 Step 4's symbol list) is written down
- [ ] asteroid, pacman, bzone run correctly; fullscreen toggle, cursor clip, alt-tab all verified
- [ ] `WIN7BUILD` present in both projects' Release|x64 defines

## Explicitly NOT in this plan

Any evdev, ALSA, Wayland, X11 or Vulkan-Linux code (that is 3b, on the Steam Machine); `VK_KHR_display` (not needed — the Pi runs a desktop); replacing the vcxproj with CMake; splitting `emu_vector_draw.cpp` (still GL-saturated, carried from Phase 1); Teensy audio via the PT8211 DAC (later); any behaviour change.
