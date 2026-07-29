# Phase 3b Design — Linux toolchain, portable utilities, ALSA audio

**Date:** 2026-07-28
**Status:** Approved for planning
**Branch context:** follows `refactor/phase3a-window-contract`
**Predecessors:** `2026-07-28-osd-contract-design.md` (Phase 1), `2026-07-28-phase2-core-lib-design.md` (Phase 2), `2026-07-28-phase3a-window-contract-design.md` (Phase 3a)

---

## 0. The end point

**Linux must do everything Windows does. That is the programme's #1 goal and its definition of done** (user, 2026-07-28).

Not "Linux runs the emulator". Not "Linux runs most games". Feature parity: every game, every renderer feature, every input device, every audio path, the menus, the artwork, the configuration — the same on both.

This has one consequence that governs the rest of this document, and every phase document after it:

> **Nothing in this programme is dropped. Things are only ordered.**

Where a section below says "deferred to 3c" it means *scheduled later*, never *decided against*. A Linux build that is missing positional audio, or per-device input, or a window, is an intermediate state with a dated successor — not an acceptable resting place. Any deferral in this spec that does not have a phase named against it is a defect in this spec.

The corollary for verification: parity claims need evidence of *sameness*, not evidence of *working*. "Linux produces vectors" is not the test; "Linux produces the same 89,414 vectors Windows does" is. That principle is why §3.1's goal 2 is an equality and not a threshold.

---

## 1. Motivation

Phases 1–3a defined all three backend contracts — input (`sys_input.h`), OSD services and audio (`osdepend.h`, `IAudioBackend`), and the window (`ISystemWindow`) — and made the core/OSD boundary compiler-enforced by turning `aae_core` into a static library whose include path cannot reach the OSD. Phase 3a proved the payoff: `aae_headless` runs real games with no window, no GL and no audio.

Every one of those phases was verified **on Windows only**. Phase 3b is where a second compiler and a second operating system get a say.

### 1.1 The environment changed, and it changes the plan

Phase 3a's spec recorded that there was no Linux toolchain on the development machine, and split its scope along exactly that line. Re-measured for this phase:

```
wsl    -> C:\WINDOWS\system32\wsl.exe   (stub only: "WSL is not installed")
gcc    -> NOT FOUND        clang -> NOT FOUND
ninja  -> NOT FOUND        docker -> NOT FOUND
cmake  -> C:\Program Files\CMake\bin\cmake.exe
```

**Decision (user, 2026-07-28): install WSL2 + a native Ubuntu toolchain on the development box. Done, and measured the same day:**

```
Ubuntu 26.04 LTS   g++ 15.2.0   cmake 4.2.3   ninja   make   git
repo visible at /mnt/c/Source2026/AAE_publish
still missing: pkg-config, libasound2-dev, libx11-dev, libgl1-mesa-dev,
               libglu1-mesa-dev, libudev-dev
```

Two things about that toolchain that the plan must account for:

- **g++ 15 is much stricter than the MSVC this code grew up under.** Expect a first pass dominated by missing `#include`s that MSVC supplied transitively, narrowing conversions, and `-Wall` findings. These are real defects being surfaced, not toolchain noise.
- **CMake 4.x removes compatibility with `cmake_minimum_required` below 3.5.** `CMakeLists.txt` declares 3.20, so it configures — but the `LINK_LIBRARY` genex path (3.24+) is the one that will be taken here, and the hand-written `--whole-archive` fallback below it will therefore go untested on this box. It still matters: an older distro CMake on the Steam Machine or Pi may take the fallback, and if it is wrong, every game silently vanishes.

This is the single highest-leverage item in the whole port programme. It converts every Linux claim in this phase from "written blind, checked later by pasting errors back from the Steam Machine" into "checked by a compiler in seconds" — the same standard that made Phases 1–3a trustworthy, and that caught a misfiled header breaking 72 of 86 core files when no grep-based rule could have.

What WSL2 can and cannot verify:

| | verifiable under WSL2 | needs the real box |
|---|---|---|
| `aae_core` compiles with g++ | **yes** | — |
| `aae_headless` builds, runs, produces correct output | **yes** | — |
| ALSA backend compiles and links | **yes** | — |
| ALSA backend is *audible* | usually (WSLg routes PulseAudio) | confirm on hardware |
| evdev against real devices | no | yes |
| GLX/Wayland against a real GPU | software Mesa only | yes |

Scope is therefore set by what a compiler can check, not by what is conceptually next.

### 1.2 Scope: prep + timer + audio. Window and input are 3c.

**Decision (user, 2026-07-28).** The four Linux backends sketched in `CMakeLists.txt` are not comparable in size. `posix_timer.cpp` is a hundred lines. `alsa_backend.cpp` is a real but bounded piece of work against an interface that already exists. `linux_window.cpp` and especially `evdev_input.cpp` are not: evdev has to re-implement the per-device multi-HID keyboard/mouse identity and player-assignment system that Windows currently spreads across RawInput, XInput, DirectInput and WinMM.

3b takes the two whose interfaces are settled and whose correctness a headless machine can judge. 3c takes the two that need a screen and a human.

### 1.3 The proof must exercise real code, not stubs

**Decision (user, 2026-07-28).** `aae_headless` reaches green by stubbing `sys_log`, `sys_fileio`, `path_helper`, `iniFile` and `mixer` in `aae/headless/null_backends.cpp`. A Linux `aae_headless` would therefore go green **without a single one of those files ever becoming portable, and without ALSA being exercised at all** — a green build that proves less than it appears to.

So this phase adds a second Linux target, `aae_audiotest`, that links the **real** `mixer.cpp` against a **real** backend and makes a sound. That forces three of the utility files portable as a side effect, and it starts paying down the audio verification debt outstanding since Phase 2 (which rewrote every sample and stream path — `WAVEFORMATEX`→`WaveFormat`, the entire voice path behind an interface — and has only ever been smoke-tested).

---

## 2. Measured current state

All measured 2026-07-28 on `refactor/phase3a-window-contract`.

### 2.1 `aae_core` is very nearly Linux-clean already

Scanning all 88 `aae_core` translation units:

- **Zero** include a Windows header directly (`windows.h`, `Xinput.h`, `xaudio2.h`, `dinput.h`, `mmsystem.h`, `io.h`, `winsock*`).
- **Zero** use `#pragma comment`, `__forceinline`, `__declspec`, `_stricmp`, `strcpy_s`, `fopen_s`, `_snprintf`, `Sleep()` or precompiled-header stubs.
- **One** uses `sprintf_s`: `cpu_code/cpu_6502.cpp`, seven calls, all inside the disassembler (`:2551`–`:2586`).
- **One** uses `_MSC_VER`: `vidhrdwr/old_mame_raster.cpp` — and it is **already correct**, with `posix_memalign`/`free` as the non-MSVC path (`:54`–`:65`). No work needed.

This is a far better starting position than the Phase 3a work list assumed, and it is the reason goal 2 below is achievable in one phase.

### 2.2 `joystick.h` is the *only* thing keeping the core Windows-bound

Phase 3a's work list opened with "split `os_input.cpp`", on the grounds that it lives in `aae_core` but does not compile OSD-free. That is true, but the cause is much narrower than a file split.

`os_input.cpp` includes six headers. Five are already clean — `osdepend.h` carries an explicit "must stay platform-neutral" comment, `os_basic.h` and `config.h` have no includes at all, `sys_log.h` includes only `<string>`, and `sys_input.h` mentions `windows.h` only inside comments. The sixth, `joystick.h`, opens with:

```c
#include <windows.h>
#include <Xinput.h>
```

Its 503 lines are otherwise Allegro-compatible structs and macros with no Win32 types. The entire Win32 surface of the header is **two things**:

1. `bool joystick_check_combo(int player, WORD buttonMask);` — `WORD` from `windows.h`.
2. `JOY_COMBO_PAUSE` / `JOY_COMBO_ESC` / `JOY_COMBO_MENU`, defined over `XINPUT_GAMEPAD_*` from `Xinput.h`.

The only consumer of that combo API outside the backend is `aae_emulator.cpp` (`:1095`, `:1174`, `:1187`, `:1209`) — executable-side. Nothing in `aae_core` touches it.

**Why the boundary test missed this.** Phase 2's enforcement works by *excluding include directories* from `aae_core`'s search path, but `system/input` is legitimately on that path — the core needs `sys_input.h`. And the complementary `#ifdef _WINDOWS_ → #error` leak guard, the idiom adopted in Phase 1, is present in only seven core files (`acommon.cpp`, `cpu_6502.cpp`, `centiped.cpp`, `invaders.cpp`, `memory.cpp`, `aae_avg.cpp`, `SegaG80vid.cpp`). `os_input.cpp` is not one of them. Either mechanism alone would have caught it.

### 2.3 The audio contract is trapped inside the Win32 backend's header

`IAudioBackend` — the twenty-method neutral interface Phase 2 built, and the whole reason an ALSA backend is possible — is declared at `system/audio/xaudio2_backend.h:34`. That file includes `<xaudio2.h>` (or `<xaudio2redist.h>`) **unconditionally**, at `:19`–`:23`.

Two consequences:

- `mixer.cpp` includes `xaudio2_backend.h`, so **`mixer.cpp` cannot be parsed on Linux at all**.
- An ALSA backend would have to `#include "xaudio2_backend.h"` to learn what it implements.

The item recorded in the Phase 3a work list — "`mixer.cpp` hardcodes `std::make_unique<XAudio2Backend>()`" (`mixer.cpp:672`) — is the symptom. The header is the cause, and fixing only the symptom leaves `mixer.cpp` unbuildable.

Two related facts make this cheaper than it sounds:

- `WaveFormat` already lives in the neutral `mixer.h` (`:520`), from Phase 2.
- `VoiceHandle` is **opaque**: forward-declared in the header, defined privately in `xaudio2_backend.cpp:39`. An ALSA backend defines its own layout. Since exactly one backend TU is ever linked per platform, this is not an ODR problem — but it *would* become one if both were ever linked into the same binary, so they must not be.

### 2.4 `audio_3d.h` is already neutral; only its implementation is not

`system/audio/audio_3d.h` includes only `<cstdint>` and forward-declares `VoiceHandle`. Its seven functions are plain floats and integers. `mixer.cpp` therefore compiles against it unchanged on Linux — it needs an *implementation*, not an interface change. `audio_3d.cpp` is the Windows-bound half (`<xaudio2.h>`, `<x3daudio.h>`, no guard).

### 2.5 `wintimer.h` has a name collision, not merely a dependency

```c
#include <windows.h>
#include <time.h>
typedef struct timer_s { __int64 frequency; ... BOOL performance_timer; ... } timer_t;
extern timer_t g_timer;
```

On Linux, POSIX `<time.h>` already declares `timer_t` (the `timer_create` handle type). Redefining it as a different type is a hard compile error, not a warning. `posix_timer.cpp` therefore **cannot** simply implement the existing header — the type has to be renamed first. `__int64` and `BOOL` in the same struct need replacing too.

### 2.6 `path_helper` and `sys_fileio` duplicate each other

Both independently wrap `GetModuleFileName` to find the executable directory: `path_helper.cpp:46` (`GetModuleFileNameW`) and `:84` (`GetModuleFileNameA`), and `sys_fileio.cpp:37`/`:44` doing the identical thing again. Porting this twice would be a mistake; one of them should call the other.

### 2.7 `sys_log.cpp`'s Windows surface is console colouring only

`FOREGROUND_*` constants (`:119`–`:123`), `GetStdHandle`/`GetConsoleScreenBufferInfo`/`SetConsoleTextAttribute` (`:299`–`:311`), and `AllocConsole` + `_setmode(_fileno(...))` (`:262`–`:268`). No file I/O uses Win32. This is the smallest of the three utility ports.

---

## 3. Design

### 3.1 Goal — four checkable statements

1. **`aae_core` compiles under g++ on Linux**, with the same warning posture as Windows (`-Wall`, not `-Werror` — the six pre-existing warnings must not become build failures).

2. **Linux `aae_headless` builds, runs, and reports exactly the same vector counts as Windows**: `asteroid 600` → **89,414**, `bzone 600` → **353,693**.

   Exactly, not merely non-zero. Those numbers are the product of six CPU cores, the timer system, the AVG vector generator and the whole memory subsystem agreeing across two compilers, two C++ standard libraries and two ABIs. A count that is close but not equal is a real emulation-accuracy bug, and it is worth far more caught here than discovered later on hardware.

3. **`aae_audiotest` is audible on both platforms** — the same `mixer.cpp`, driving XAudio2 on Windows and ALSA on Linux, through the `IAudioBackend` seam.

4. **The Windows build is unchanged.** MSBuild and CMake each list **132/132** games and emit the **same six warnings**. Both link with whole-archive (`/WHOLEARCHIVE:` / `--whole-archive`) — without it every self-registering driver is silently dropped, which cost 83 games in Phase 2 and would cost all of them on Linux, where CMake is the only build system.

### 3.2 The audio contract moves into its own header

**Approach chosen (user, 2026-07-28): extract the interface.**

```
audio_backend.h    (new, neutral)   IAudioBackend, opaque VoiceHandle,
                                    std::unique_ptr<IAudioBackend> create_audio_backend();
xaudio2_backend.h  (reduced)        class XAudio2Backend + <xaudio2.h>
alsa_backend.h     (new)            class AlsaBackend + <alsa/asoundlib.h>
```

`mixer.cpp` includes `audio_backend.h` only, names no concrete backend, and calls the factory. Each platform TU defines `create_audio_backend()` and its own private `VoiceHandle`. This is the same shape as `ISystemWindow` / `Win32Window` from Phase 3a, so the codebase gains no new idiom.

Rejected alternatives:

- **Guard `<xaudio2.h>` behind `#ifdef _WIN32` in place.** Smaller diff, but leaves the portable contract living in — and named after — one implementation. That is exactly the arrangement Phase 3a rejected when it split `sys_window.h` from `win32/`.
- **A runtime backend registry.** One backend per platform, selected at compile time; a registry buys nothing and reintroduces the static-initialisation-and-selective-linking fragility that silently deleted 83 games in Phase 2.

### 3.3 The three utility files get one portable implementation each

**Approach chosen (user, 2026-07-28): portable single implementation, not `_win32`/`_posix` file pairs.** These files are ~90% OS-independent logic; splitting them would triple the file count to isolate a handful of lines.

| file | change |
|---|---|
| `sys_log.cpp` | ANSI SGR escapes instead of `SetConsoleTextAttribute`. On Windows, enable `ENABLE_VIRTUAL_TERMINAL_PROCESSING` once at console init — AAE already requires Windows 10/11 (`win10_win11_required_code.cpp`), well past the 1511 build that introduced it, so console output is visually unchanged. `AllocConsole`/`_setmode` stay behind a `#ifdef _WIN32` — Linux has no equivalent concept and needs none. |
| `path_helper.cpp` | One per-platform primitive, `exe_path()`: `GetModuleFileNameW` on Windows, `readlink("/proc/self/exe")` on Linux. Everything above it — directory extraction, separator handling, joining — moves to `std::filesystem`, which also removes the hand-rolled `'\\'` searching at `:55` and `:93`. |
| `sys_fileio.cpp` | `std::filesystem` for existence and attribute queries; its duplicate exe-path code (`:36`–`:44`) is deleted and delegates to `path_helper`. |

`std::filesystem` is C++17, already required by both vcxprojs and by `CMAKE_CXX_STANDARD 17`, and already used in `sys_texture.cpp`.

### 3.4 `sys_timer.h` replaces `wintimer.h`

A neutral `sys_timer.h` declares the same six functions (`TimerInit`, `TimerShutdown`, `TimerGetTime`, `TimerGetTimeMS`, `TimerElapsedSinceLastCall`, `TimerIsHighResolution`, `TimerReset`) with:

- `timer_t` renamed **`AaeTimer`** — mandatory, see §2.5.
- `__int64` → `int64_t`, `BOOL` → `bool`.
- No `<windows.h>`.

`wintimer.cpp` implements it over `QueryPerformanceCounter` exactly as today. `posix_timer.cpp` implements it over `clock_gettime(CLOCK_MONOTONIC)`, which is always high-resolution — the multimedia-timer fallback fields exist only for the Win32 path and the POSIX implementation reports `TimerIsHighResolution() == true` unconditionally.

### 3.5 `alsa_backend.cpp`

The largest single item. Twenty `IAudioBackend` methods over `snd_pcm_*`.

The structural point: **XAudio2 gives you a voice mixer; ALSA does not.** ALSA hands back one PCM stream. So `VoiceCreate`/`VoiceSubmit`/`VoiceStart`/`VoiceSetVolume`/`VoiceSetFrequencyRatio`/`VoiceExitLoop`/`VoiceBuffersQueued` become a small software mixer inside the backend: a voice list, per-voice gain and resampling ratio, loop-point handling, summed into the output period buffer on a dedicated thread.

That mixing code is genuinely portable and will be wanted again for the Teensy target, so it is written as a plain sub-component of the backend with no ALSA calls in it — the ALSA half is only device open, hardware-parameter negotiation, and `snd_pcm_writei`.

`VoiceSetOutputMatrix` (the positional-audio hook) returns `false` on ALSA in this phase; see §3.6.

### 3.6 Positional audio on Linux is explicitly deferred

`audio_3d_null.cpp` provides the Linux implementation of `audio_3d.h`: `audio_3d_init()` returns `false`, everything else no-ops. `mixer.cpp` already gates the entire positional path on `g_3d_inited` (`:686`, `:971`, `:1351`, `:1375`), so nothing else changes.

Real positional audio would mean reimplementing X3DAudio's channel-matrix computation — a self-contained piece of DSP work, scheduled as **Phase 3d** (§5). Parity requires it; this phase does not deliver it.

The null file exists so the gap is *loud*. A Linux build that silently plays everything centre-panned is indistinguishable from a working one until someone notices the stereo field is dead, months later. `audio_3d_null.cpp` names the omission in the source tree, and its `audio_3d_init()` must log a warning at startup rather than returning `false` quietly.

### 3.7 Build system

Both build systems stay in lockstep, as Phase 3a established:

- New files (`audio_backend.h`, `alsa_backend.*`, `sys_timer.h`, `posix_timer.cpp`, `audio_3d_null.cpp`) added to `CMakeLists.txt` **and**, where they are Windows-relevant, to the vcxprojs. Source lists stay explicit — never `file(GLOB)`, so the core/OSD split cannot drift unnoticed.
- The `_aae_core_count EQUAL 88` and `_aae_exe_count EQUAL 47` assertions are updated in the same commit as any change that moves them. Most of this phase adds headers or Linux-only translation units, neither of which affects the Windows counts — and `_aae_exe_count` is already guarded by `if(WIN32 ...)`, so the Linux backend files do not perturb it. If a number does not need to change, it must not be changed.
- `aae_audiotest` is a new target on both platforms, linking `mixer.cpp`, the platform audio backend, the three portable utility files, and `aae_core`.
- **The three `find_package(... REQUIRED)` calls in the `UNIX` branch must be scoped to the targets that need them.** As written, `find_package(OpenGL REQUIRED)`, `find_package(ALSA REQUIRED)` and `find_package(X11 REQUIRED)` run at configure time for the whole project, so a box without `libx11-dev` cannot configure *at all* — and therefore cannot build `aae_headless`, which needs none of the three. Since 3b's deliverables are exactly the two targets that do not need X11 or GL, this would block the phase on a dependency it does not use. ALSA becomes required only when `aae_audiotest`/`alsa_backend.cpp` is being built; X11 and OpenGL only when the `aae` target is, which is 3c.
- The `EXISTS`-guarded Linux backend list in `CMakeLists.txt` loses `alsa_backend.cpp` and `posix_timer.cpp` from its "not yet implemented" warning as they land; `linux_window.cpp` and `evdev_input.cpp` remain warned-about, which is accurate — the `aae` target still will not link on Linux at the end of this phase. **That is expected and is not a failure of 3b.** `aae_headless` and `aae_audiotest` are the Linux deliverables; `aae` becomes linkable in 3c.

### 3.8 Regression guards added

Every fix in this phase gets a mechanism that prevents its recurrence, not just the fix:

- `#ifdef _WINDOWS_ → #error` added to `os_input.cpp` — the guard whose absence allowed §2.2.
- An `#ifdef <xaudio2 include guard> → #error` in `mixer.cpp`, so the OS-specific header cannot creep back in. The exact macro must be read out of the SDK's `xaudio2.h` and the redist's `xaudio2redist.h` rather than guessed — Phase 1 shipped three `#error` guards that could never fire because the macro name was assumed, and the guard must be proven to trigger by deliberately including the header once before the guard is committed.
- A `static_assert` in `Joystick.cpp` proving the new neutral `AAE_JOYBTN_*` constants still equal their `XINPUT_GAMEPAD_*` counterparts, so a future SDK change cannot silently break the combos.
- The Linux build itself becomes the strongest guard: once CI-less but routine, `g++` catches Windows-ism regressions that no grep can.

---

## 4. Work items, in dependency order

| # | item | verified by |
|---|---|---|
| 1 | WSL2 + Ubuntu toolchain — **done**; dev packages (`pkg-config`, `libasound2-dev`, `libx11-dev`, `libgl1-mesa-dev`, `libglu1-mesa-dev`, `libudev-dev`) still to install (user action; `sudo` needs a password) | `pkg-config --modversion alsa` returns a version |
| 2 | Neutralise `joystick.h`; add the `_WINDOWS_` guard to `os_input.cpp` | Windows builds green, 132 games; `os_input.cpp` compiles with no Windows header reachable |
| 3 | `sprintf_s` → `snprintf` in `cpu_6502.cpp` | Windows builds green |
| 4 | `aae_core` builds under g++ | Linux `libaae_core.a` produced |
| 5 | Linux `aae_headless` builds and runs | vector counts equal the Windows values exactly |
| 6 | Extract `audio_backend.h`; `mixer.cpp` uses the factory | Windows `aae.exe` still runs with audio |
| 7 | Portable `sys_log.cpp`, `path_helper.cpp`, `sys_fileio.cpp` | Windows unchanged; all three compile under g++ |
| 8 | `sys_timer.h` + `posix_timer.cpp`; `wintimer.cpp` retargeted | both platforms compile; Windows timing unchanged |
| 9 | `alsa_backend.cpp` + `audio_3d_null.cpp` | links under g++ |
| 10 | `aae_audiotest` on both platforms | **audible** on Windows and on Linux |

Items 2–3 and 6–8 are Windows-verifiable and can land before item 1 completes.

---

## 5. Not in this phase — scheduled, not dropped

Per §0, everything here has a phase named against it. None of it is optional to reaching parity.

| item | phase | why not now |
|---|---|---|
| `linux_window.cpp` (X11/Wayland surface + GL context) | **3c** | needs a screen and a human to judge |
| `evdev_input.cpp`, multi-HID parity with RawInput/XInput/DirectInput/WinMM | **3c** | largest single item in the programme; needs real devices |
| `sys_gl.cpp` WGL→GLX split, `sys_texture.cpp`'s `win32/win32_private.h` | **3c** | travels with the window backend |
| Real positional audio on Linux (§3.6) | **3d** | self-contained DSP work: reimplementing X3DAudio's channel-matrix maths |
| Vulkan renderer path | **4** | required only by the Pi (Mesa v3d tops out near GL/GLES 3.1, below the `#version 330 core` shaders). The Steam Machine's `radeonsi` does GL 4.6 and needs none of it — but the Pi is a parity target too, so this is scheduled, not conditional |

**The `aae` executable will not link on Linux at the end of 3b.** `aae_headless` and `aae_audiotest` are this phase's Linux deliverables. That is a planned intermediate state with 3c as its successor, and it is the one place in this programme where "it doesn't build" is an acceptable reported outcome — stated here so it is never mistaken for a regression.

Still owed from earlier phases, unchanged by this one: the interactive audio pass on Windows (fullscreen toggle, cursor clipping, alt-tab focus, audio by ear), `emu_vector_draw.cpp`'s GL saturation, and `segag80snd.cpp:178` calling `allegro_message()` from a sound path.

---

## 6. Risks

**The vector counts will not match on the first try.** `char` is signed by default on x86 Linux and on MSVC, but *unsigned* on ARM — which matters for the Pi later, and may already surface here through `<cstdint>` mismatches. `long` is 64-bit on Linux and 32-bit on Windows. Struct padding and bitfield layout differ. Any of these can shift emulation behaviour subtly.

This is the phase's most valuable risk, and it is handled by *not* routing around it: a mismatch gets its own investigation and its own fix in the core, not a per-platform tolerance. A count that differs is telling us something true about the code.

**ALSA under WSLg may not produce sound even when the code is correct.** WSLg routes audio through PulseAudio, and ALSA-to-Pulse bridging is not guaranteed. If item 10's Linux half cannot be demonstrated under WSL, it is demonstrated on the Steam Machine instead — the compile-and-link half still lands here, and the phase does not claim "audible on Linux" until someone has heard it.

**`ENABLE_VIRTUAL_TERMINAL_PROCESSING` changes Windows console output if it fails to enable.** The fallback is uncoloured text, not garbage escape sequences — but it must be checked visually on Windows before the phase is called done, because a log that silently loses its colour coding is a real regression for a debugging tool.
