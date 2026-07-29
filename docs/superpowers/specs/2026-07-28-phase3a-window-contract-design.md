# Phase 3a Design — `ISystemWindow` + CMake

**Date:** 2026-07-28
**Status:** Approved for planning
**Branch context:** follows `refactor/phase2-core-lib`
**Predecessors:** `2026-07-28-osd-contract-design.md` (Phase 1), `2026-07-28-phase2-core-lib-design.md` (Phase 2)

---

## 1. Motivation

Phase 1 gave a backend author the input contract (`sys_input.h`) and the OSD-services contract (`osdepend.h`). Phase 2 gave them the audio contract (`IAudioBackend`, twelve `Voice*` methods) and made the boundary compiler-enforced. **The window is the one contract still missing.**

Phase 3a defines it, and adds a CMake build — both fully verifiable on Windows.

### 1.1 Why this is split from the Linux backend itself

There is **no Linux toolchain on the development machine**: WSL is not installed, and `gcc`, `clang` and `ninja` are all absent. Only `cmake` is present.

That is decisive. Phases 1 and 2 were trustworthy because a compiler checked every claim — it caught three `#error` guards that could never fire, a plan that would have left HEAD un-buildable for three tasks, and a misfiled header that broke 72 of 86 core files and that no grep-based rule could have found. Linux code written on this machine gets none of that scrutiny.

So the work splits along the line the environment draws:

| | scope | verifiable here? |
|---|---|---|
| **3a (this spec)** | `ISystemWindow` + Win32 implementation + CMake for Windows | **yes** |
| **3b (later)** | evdev, ALSA, Wayland/X11 surface, `sys_platform_linux.cpp` | no — needs the Linux box |

3a is the same work regardless of how 3b is eventually built, so it does not wait on that decision.

### 1.2 Target order: the GUI variant first, the Pi second

**One interface, three implementations — but only two beyond Win32** (user, 2026-07-28):

| implementation | serves | shape |
|---|---|---|
| `Win32Window` | Windows | exists today; this phase wraps it |
| **GUI** | **Steam Machine and Raspberry Pi** | fullscreen window over a desktop compositor; Wayland or X11 |
| **Headless** | Teensy 4.1 | **no window at all** — video is redirected into the vector/DAC path for a real vector arcade monitor |

**`VK_KHR_display` is not required.** An earlier draft assumed a compositor-less cabinet Pi driving DRM/KMS directly. The Pi will in fact run a normal Linux desktop with AAE fullscreen over it (user, 2026-07-28), which is the same code path as the Steam Machine. The two differ in toolchain and GPU, not in windowing — one backend source, built twice. This supersedes the Pi-first, `VK_KHR_display` assumption recorded in `linux-pi-port-plan`. Should a compositor-less cabinet build ever be wanted, it becomes a third implementation of this same interface; nothing here precludes it.

**A generic *interface* is right; a generic *implementation* is not.** A compositor backend negotiates for a surface, a headless backend has no display concept whatsoever, and merging them would produce a file that is mostly `#ifdef` — strictly worse than two small honest ones. Genericity belongs at the interface.

**The headless case is the strongest test of that interface,** and it drives a real design decision in §3.2: presentation must be separable from windowing. A Teensy backend has no swapchain, no surface and no extensions, so an interface that makes `SwapBuffers()` and the Vulkan hooks mandatory would force it to stub five methods with lies. If a null implementation can satisfy the window contract honestly, the abstraction is real; if it cannot, the interface is still leaking presentation assumptions.

Consequences of the GUI target worth recording, because they revise the assumptions in `linux-pi-port-plan` (which was written Pi-first and compositor-less):

- **x86_64, not ARM.** No cross-compilation for this target; builds are native on the box. The Teensy work remains a separate, genuinely cross-compiled target.
- **AMD GPU → Mesa RADV**, a mature and complete Vulkan 1.3 implementation — considerably more forgiving than the Pi's v3dv, which the original plan was written around.
- **SteamOS has an immutable root filesystem.** Installing a toolchain requires `steamos-readonly disable` or a `distrobox`/`toolbox` container. A practical obstacle for 3b, not a design one.
- **Surface layer.** The recorded plan targets `VK_KHR_display` (no compositor) for a cabinet-style Pi. A generic Linux frontend wants X11/Wayland instead. **`VK_KHR_display` becomes a later Pi specialization**, not the primary path. The Pi is planned next after Linux, so the interface must accommodate both without favouring either.

---

## 2. Measured current state

Measured 2026-07-28 on `refactor/phase2-core-lib`.

### 2.1 Nineteen files include `framework.h`

`aae_emulator.cpp`, `aae_video/mame_layout.cpp`, `aae_video/opengl_renderer.h`, `acommon.cpp`, `config.cpp`, `fileio/mame_fileio.cpp`, `fileio/texture_handler.cpp`, `led_service_handler.cpp`, `menu.cpp`, `os_basic.cpp`, `sndhrdwr/galsnd_stream.cpp`, `sndhrdwr/namco.cpp`, `sndhrdwr/segag80snd.cpp`, `vidhrdwr/osd_video.cpp`, `system/graphics/sys_gl.cpp`, `system/graphics/sys_texture.cpp`, `system/window/win10_win11_required_code.cpp`, `system/window/windows_util.cpp`, `system/window/winmain.cpp`.

All are on the **executable** side after Phase 2 — none is in `aae_core`. So this phase cannot break the core boundary; it is entirely app/render-layer work.

### 2.2 The real coupling is `WindowSetup`, not the functions

`framework.h`'s function surface is small — `GetWindowSetup()`, `GetClientWidth/Height()`, `win_get_window()`, `ToggleBorderlessFullscreen()`, `RestoreWindowViewport()`, `allegro_message()`, `osMessage()`, `GetLastErrorStdStr()`, `ClipAndHideCursor()`, `UnclipAndShowCursor()`, `EnableCursorClip()`, `ForceCursorClipUpdate()`, `SetMousePos()`, `GetMousePos()`.

The hard part is **`struct WindowSetup`** (`framework.h:10-51`), a ~40-field configuration and live-state struct read across the app layer, with Win32 types embedded:

| member | type | disposition |
|---|---|---|
| `rect`, `windowedRect`, `screenRect` | `RECT` | **neutralise** — a rectangle is not a Win32 concept |
| `style`, `exStyle` | `DWORD` | **Win32-private** — these are `WS_*`/`WS_EX_*` window-style bitmasks with no meaning on X11/Wayland |
| `disableNC`, `disableRoundedCorners` | `bool` | **Win32-private** — non-client area and rounded corners are Win11 concepts |
| everything else (~30 fields: sizes, DPI, fullscreen/focus/minimised flags, monitor index, aspect) | `bool`/`int`/`float` | **already neutral**, keep |

`aae/system/math/rect2d.h` already exists in the tree and is the natural home for the neutral rectangle type rather than inventing one.

### 2.3 `opengl_renderer.h` includes `framework.h`

One of the 19 is a **header**, and it is the render-side header Phase 1 deliberately left Win32-coupled. It is the reason `acommon.cpp`'s `_WINDOWS_` guard is still parked. Phase 3a is the phase that can finally close it.

---

## 3. Design

Three work items, in order.

### 3.1 Neutralise `WindowSetup`

Split the struct in two:

```c
// sys_window.h - platform-neutral
struct WindowSetup {
    aae::Rect2D rect{};            // was RECT
    aae::Rect2D windowedRect{};
    aae::Rect2D screenRect{};
    bool  useFullscreen = false;
    bool  borderlessFullscreen = false;
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
    // ...remaining neutral fields unchanged...
};
```

The Win32-only members (`style`, `exStyle`, `disableNC`, `disableRoundedCorners`) move into a `Win32WindowState` struct private to the Win32 backend, keyed alongside the neutral one. **Nothing outside `system/window/` reads them today** — this must be verified per field during planning, not assumed.

`aae::Rect2D` comes from the existing `system/math/rect2d.h`. If its shape does not match `RECT`'s semantics (left/top/right/bottom vs x/y/w/h), the conversion happens at the Win32 boundary only — the ~30 call sites reading `rect.right - rect.left` must be surveyed before choosing.

### 3.2 `ISystemWindow`

```c
// sys_window.h
class ISystemWindow {
public:
    virtual ~ISystemWindow() = default;

    virtual bool Create(const WindowSetup& setup) = 0;
    virtual void Destroy() = 0;

    // Drain the platform event queue. Returns false when the app should quit.
    virtual bool PumpEvents() = 0;
    virtual void SwapBuffers() = 0;

    virtual int  ClientWidth()  const = 0;
    virtual int  ClientHeight() const = 0;
    virtual void GetDrawableSize(int* w, int* h) const = 0;
    virtual float DpiScale() const = 0;

    virtual void ToggleBorderlessFullscreen() = 0;
    virtual void RestoreViewport() = 0;

    virtual void SetCursorVisible(bool visible) = 0;
    virtual void EnableCursorClip(bool enable) = 0;
    virtual void ForceCursorClipUpdate() = 0;
    virtual void SetMousePos(int x, int y) = 0;
    virtual void GetMousePos(int* x, int* y) const = 0;

    // Presentation is OPTIONAL - see below. Returns nullptr on a headless
    // backend, which is not an error.
    virtual IPresentSurface* Presentation() { return nullptr; }
};
```

**Presentation is a separate, optional interface.** This is what makes the headless Teensy backend honest rather than a pile of stubs:

```c
// Implemented only by backends that actually present to a display.
class IPresentSurface {
public:
    virtual ~IPresentSurface() = default;

    virtual void SwapBuffers() = 0;
    virtual void GetDrawableSize(int* w, int* h) const = 0;

    // Instance extensions this platform requires (VK_KHR_surface plus one
    // platform extension). Returns a NULL-terminated array of C strings.
    virtual const char* const* RequiredVkInstanceExtensions(uint32_t* count) const = 0;
    // Opaque handles: VkInstance/VkSurfaceKHR, so this header needs no Vulkan.
    virtual bool CreateVkSurface(void* instance, void* outSurface) = 0;
};
```

`Win32Window` and the GUI backend return a real `IPresentSurface`. The **headless Teensy backend returns `nullptr`** — it has no swapchain, no surface and no instance extensions, and says so plainly instead of stubbing four methods with meaningless values. Callers that need to present check once for null; callers that only need window geometry never touch it.

The Vulkan hooks are shaped as "required instance extensions + create surface + drawable size" rather than anything compositor-specific, so `VK_KHR_win32_surface`, `VK_KHR_wayland_surface` and `VK_KHR_xcb_surface` all fit without an interface change — as would `VK_KHR_display` if a compositor-less build is ever wanted.

`allegro_message`, `osMessage`, `GetLastErrorStdStr` are **not** window operations and do not belong on this interface. They move to `sys_log.h` / a small `sys_dialog.h`, with the Win32 message-box implementation staying in the backend.

### 3.3 `Win32Window : ISystemWindow`

Wrap the existing implementation in `system/window/` — this is a re-homing of working code, not a rewrite. `win_get_window()` (returning `HWND`) stays in a Win32-private header for `winmain.cpp` and the raw-input plumbing; it does **not** go on the interface.

The 19 consumers switch to `sys_window.h`. Where a consumer genuinely needs Win32 (e.g. `winmain.cpp`), it keeps a private include — that is correct, not a shortfall.

**If closing `opengl_renderer.h`'s `framework.h` include proves large, it is deferred**, and `acommon.cpp`'s guard stays parked one more phase. That is a scoping decision to make when the size is known, not now.

### 3.4 A headless target — proving the core is separable

**Added at the user's request (2026-07-28): "make sure the core is truly separate for headless running."**

Phase 2 established that the core *compiles* without the OSD. That is not the same as the core *running* without one. A small console target settles it:

`aae_headless` links `aae_core` against a `NullWindow` (a real `ISystemWindow` whose `Presentation()` returns `nullptr`), stub `osd_*` services, and a local `add_line` that counts instead of drawing. It runs N frames of a vector game and prints the vector count.

**A non-zero count is the proof**: the 6502 executed, the DVG walked its display list, and vectors were produced with no window, no renderer, no audio and no OS display. That is exactly the Teensy scenario, minus the DACs.

Two things make this worth more than a test:

1. **The link errors are the Teensy porting checklist.** Every `LNK2019` names something a bare-metal backend must supply. That list is a deliverable of this phase, not a by-product.
2. **It will expose orchestration living on the wrong side.** `aae_emulator.cpp` (holding `emulator_init`/`run_a_game`) is executable-side, so the headless target cannot call it. Whether the core exposes enough to drive a driver directly is genuinely unknown; if it does not, that is a finding worth having early — a Teensy port hits the same wall.

Teensy audio (a PT8211 16-bit DAC) is explicitly **not** in scope; the headless target stubs audio entirely.

### 3.5 CMake for Windows

`CMakeLists.txt` producing the same two targets as the vcxproj: `aae_core` (static lib, restricted include path) and `aae` (executable, linking it). Same C++17 standard, same preprocessor definitions including `WIN7BUILD`, same include directories.

**The vcxproj remains the primary Windows build.** CMake is added alongside and must produce a working `aae.exe`; it does not replace anything in this phase.

Structure the source lists so the platform backend is a single swappable variable — `AAE_PLATFORM_SOURCES` selecting `system/window/win32/*` today and `system/window/linux/*` later. Getting this right on the platform that can be verified is the entire point: the Linux build then becomes a source-list swap rather than blind CMake debugging.

---

## 4. Verification

1. **Build both ways.** MSBuild: exit 0 with exactly the six known warning lines (`cpu_i8085.cpp` C4101; `foodf.cpp` C4333; `pacman.cpp` C4018 ×2 at 104 and 716; `phoenix.cpp` C4018; `gaplus_video.cpp` C4018). CMake: configures and builds a working `aae.exe`.
2. **Boundary intact.** The Phase 2 negative test still passes — `#include "framework.h"` in an `aae_core` file yields `C1083` file-not-found.
3. **`sys_window.h` is neutral** — no `windows.h`, no `RECT`/`DWORD`/`HWND` in live code.
4. **Runtime**, exercising exactly what this phase touches: window creation, resize, **borderless-fullscreen toggle**, DPI scaling, cursor clipping in and out of the menu, and alt-tab focus loss/restore. `asteroid`, `pacman`, `bzone` all boot and play.
5. **Both binaries behave identically** — the CMake-built and MSBuild-built exes must be functionally indistinguishable.

---

## 5. Non-goals

- Any evdev, ALSA, Wayland, X11 or Vulkan-Linux code — that is 3b.
- `VK_KHR_display` / Pi specialization — later, though the interface must not preclude it.
- Replacing the vcxproj with CMake.
- Splitting `emu_vector_draw.cpp` (still GL-saturated — carried from Phase 1).
- `sndhrdwr/segag80snd.cpp:178`'s `allegro_message()` from a sound path — though §3.2 moves `allegro_message` itself, which may make the one-line fix natural to fold in.
- Any behaviour change.

---

## 6. Risks

| risk | likelihood | mitigation |
|---|---|---|
| **`WindowSetup` is read more widely, and less cleanly, than the field table suggests** — it is live mutable state, not just config, and 19 files see it | **high** | The largest unknown in this phase. Survey every read and write of every field before editing; if a Win32-only field turns out to be read outside `system/window/`, that is a design question, not a mechanical fix. Report rather than improvise. |
| `aae::Rect2D`'s semantics differ from `RECT`'s (x/y/w/h vs l/t/r/b), silently inverting arithmetic at ~30 call sites | medium | Check `rect2d.h` first. If they differ, prefer a new neutral rect matching `RECT` semantics over rewriting call-site arithmetic — a silent geometry bug will not fail the build. |
| Closing `opengl_renderer.h`'s `framework.h` include cascades into the renderer | medium | Explicitly deferrable (§3.3). Sequence it last; if large, leave it and keep `acommon.cpp`'s guard parked. |
| CMake and MSBuild diverge subtly (flags, defines, LTCG) and produce different behaviour | medium | Verification step 5 requires both binaries to be exercised, not merely built. Diff the effective compile flags if behaviour differs. |
| The Vulkan hook signatures prove wrong once a real Wayland surface is written in 3b | low-medium | They are used by nothing yet, so revising them in 3b is cheap. Do not over-fit to Win32 — validate the shape against the Wayland and `VK_KHR_display` creation calls on paper during planning. |

---

## 7. Decisions on record

| decision | value | rationale |
|---|---|---|
| Split 3a from 3b | yes | No Linux toolchain on the dev machine (§1.1); unverified Linux code would break the discipline that made Phases 1-2 trustworthy |
| Genericity | generic **interface**, per-target **implementations** | User asked whether one generic backend would be better. It would not: a compositor backend and a headless one share essentially no code, so a merged implementation would be mostly `#ifdef`. |
| Backends beyond Win32 | **two** — GUI and Headless | GUI serves the Steam Machine *and* the Pi (both run a desktop, AAE fullscreen over it). Headless serves the Teensy. |
| `VK_KHR_display` | **not needed** | Supersedes the Pi-first cabinet assumption in `linux-pi-port-plan`. The Pi runs a desktop with HDMI out, fullscreen to hide it (user, 2026-07-28) — the same path as the Steam Machine. |
| Presentation vs windowing | **separate interfaces** | Driven by the Teensy: a headless backend must be able to satisfy the window contract honestly, returning `nullptr` for presentation rather than stubbing a swapchain it does not have. |
| Target order | Steam Machine (SteamOS desktop), then Pi | User, 2026-07-28. Same backend source; different toolchain and GPU. |
| CMake scope | Windows only, alongside the vcxproj | Verifiable here; makes the Linux build a source-list swap instead of blind debugging |
| `win_get_window()` | stays Win32-private | Returning an `HWND` from a portable interface would defeat its purpose |
