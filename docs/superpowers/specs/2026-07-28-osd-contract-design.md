# OSD Contract Design (Phase 1 of the Core/OSD Split)

**Date:** 2026-07-28
**Status:** Approved for planning
**Branch context:** `feature/vector-beam-renderer`

---

## 1. Motivation

AAE must run on three targets:

| Target | Scope | Renderer | Notes |
|---|---|---|---|
| Windows | full | OpenGL (Vulkan staged) | current, must not regress |
| Linux / Raspberry Pi 4–5 | full | Vulkan (`VK_KHR_display` on Pi) | see `linux-pi-port-plan` |
| Teensy 4.1 | **vector games only** | X/Y DACs, no framebuffer | bare metal, freestanding, runs the emulation itself |

The Teensy target is the demanding one: the Cortex-M7 executes the 6502/DVG/AVG itself and drives the DACs directly. That forces a genuine MAME-style `src/emu` ↔ `src/osd` separation rather than a cosmetic one.

AAE inherits MAME 0.3x naming (`osd_*`, `osdepend.h`) but **the boundary those names imply does not exist**. This spec defines it.

---

## 2. Program decomposition

This spec covers **Phase 1 only**. The full program:

| # | Phase | Serves | Status |
|---|---|---|---|
| 0 | `AaeKey` rename + neutral input header | Linux, Teensy | folded into Phase 1 |
| 1 | **Define the OSD contract** | both | **this spec** |
| 2 | Physical separation — `src/emu` / `src/osd/{win32,linux,teensy}`, core as a lib, CMake | both | later |
| 3 | Linux backend — evdev + ALSA + Vulkan/GL | Linux | later |
| 4 | Teensy freestanding — de-STL used CPU/timer paths, ROMs from SD/flash, DAC `add_line` backend | Teensy | later |

Rationale for the ordering:

- Phase 0 folds into Phase 1 because it touches the same headers; doing it separately means editing them twice.
- Phase 2 is where correctness becomes *checkable*: today "does the core depend on the OSD?" is answered by grepping; after Phase 2 it is answered by the linker.
- Phase 3 precedes Phase 4 because a full-featured second backend validates the contract in an environment that is debuggable. Bare metal is not the place to discover the interface is wrong.

**Recommended de-risking spike (not part of Phase 1):** compile just `cpu_6502` plus the vector generator for `arm-none-eabi`, linking nothing, and record what breaks. This is roughly a day and it collapses the largest unknown in the program — whether Phase 4 is "strip three headers" or "rewrite the timer system".

---

## 3. Measured current state

All figures below were measured on 2026-07-28 against the working tree. They are the evidence this design rests on.

### 3.1 The OSD surface is 29 functions

| group | count | declared in |
|---|---|---|
| input | 11 | `aae/aae/os_input.h` |
| file I/O | 8 | `aae/aae/fileio/mame_fileio.h` |
| video / palette | 3 | `aae/aae/vidhrdwr/osd_video.h` |
| bitmap | 3 | `aae/aae/vidhrdwr/old_mame_raster.h` |
| LED | 4 | `aae/aae/led_service_handler.h` |

Plus non-function members of the surface: `struct osd_bitmap` (`old_mame_raster.h:61`), `current_palette` (`osd_video.h:93`), and four byte-order macro aliases (`osd_fread_msbfirst` and siblings, `mame_fileio.h:44-47`).

`osd_update_display`, `osd_close_display`, `osd_die`, `osd_allocate_colors`, `osd_start_audio_stream`, `osd_key_invalid` and `osd_led_w` appear **only in comments** — they are dead MAME heritage references and are not part of the surface.

### 3.2 `osdepend.h` declares zero functions

`aae/aae/osdepend.h` contains only key/joy integer defines. The 29 functions are spread across five unrelated headers. There is no single place a new target can look to learn what it must implement.

### 3.3 Windows reaches the entire core, through two doors

`aae/aae/aae_mame_driver.h` is included by **109 files**. It contains:

- line 19 — `#include "framework.h"` → `<Windows.h>`
- line 36 — `#include "osd_video.h"`, and `aae/aae/vidhrdwr/osd_video.h:61` **also** includes `framework.h`

Removing line 19 alone is therefore a no-op: every consumer still receives `framework.h` transitively via `osd_video.h`, in the same translation unit. **Both doors must close together.**

`osd_video.h:62` includes `aae_mame_driver.h` back, so the two form an include cycle currently tolerated only by header guards.

`aae/aae/fileio/texture_handler.cpp:222` calls `win_get_window()` and reaches it purely through this backdoor — neither it nor anything in its own include chain includes `framework.h`.

`framework.h` itself does **not** include GLEW or any GL header; it is `<Windows.h>` + `sys_log.h` + a window/cursor/DPI helper API.

### 3.4 `mixer.h` is in the core's master header

`aae_mame_driver.h:31`. Removing it breaks **19 files**, each of which needs its own `#include "mixer.h"`:

`acommon.cpp`, `gui/driver_gui.cpp`, `sndhrdwr/tms5220.cpp`, `sndhrdwr/cinematronics_sound.cpp`, `machine/bosconian_machine.cpp`, and drivers `invaders`, `segag80`, `dkong`, `gaplus`, `bzone`, `asteroid`, `llander`, `rallyx`, `vicdual`, `xevious`, `yiear`, `clowns`, `galaga`, `galaxian_driver`.

`rallyx.cpp` reaches the mixer through local `PLAY`/`STOP` macros (lines 39–40) rather than direct calls.

### 3.5 The `KEY_*` set is small and collides with Linux

| symbol set | defines | external refs |
|---|---|---|
| `KEY_*` (`aae/system/input/rawinput.h`) | 140 | **10, in 2 files** — `acommon.cpp:128-131` (4), `os_input.cpp:126,127,133,192,193,194` (6) |
| `OSD_KEY_*` (`aae/aae/osdepend.h`) | 145 | ~270, across 30 files including every driver |

`KEY_*` values are Windows VK codes. Linux's `<linux/input-event-codes.h>` defines the same identifiers with different values (`KEY_A` is `30` there, not `0x41`), so no translation unit can include both. `OSD_KEY_*` does **not** collide and drivers only ever use that set — so the collision is confined to 10 call sites. `KEY_*` appears nowhere as a string literal, so no config parsing is affected.

Two near-misses to be aware of, both verified 2026-07-28 and both **not** work items: `osdepend.h:151-154` mention `KEY_RCONTROL`/`KEY_ALTGR`/`KEY_SLASH2`/`KEY_PAUSE` inside *comments* only; and `rawinput.cpp:219,231,242,282` use `KEY_READ`, which is the Win32 registry access mask from `<winreg.h>` and unrelated to this set. A naive grep counts both and reports 18.

Key bindings persist as raw integers via `writeword()` (`aae/aae/inptport.cpp:388`) into `default.cfg` and per-game cfg.

### 3.6 `rawinput.h` leaks Windows types

Included by 9 files, three of them drivers (`clowns.cpp`, `invaders.cpp`, `yiear.cpp`). Public surface uses `HRESULT` (618), `HWND`/`WPARAM`/`LPARAM` (618, 628), `INT` (638–640), `LONG` (649–661), and the header opens with `#include <windows.h>` (385).

### 3.7 `emu_vector_draw.h` conflates emulation and rendering

Accurate usage, counting only files other than its own `.cpp`:

| symbol | used by | verdict |
|---|---|---|
| `add_line` | `drivers/cchasm.cpp`, `vidhrdwr/old_mame_vecsim_dvg.cpp`, `aae_video/mame_vector.cpp`, `aae_video/vector_draw.h` | **the seam** — both sides |
| `add_tex` | `vidhrdwr/old_mame_vecsim_dvg.cpp`, `aae_video/mame_vector.cpp`, `aae_video/vector_draw.h` | **the seam** — both sides |
| `cache_clear` | 11 emu files (`ccpu.cpp`, `aztarac`, `cchasm`, `cinematronics_driver`, `mhavoc`, `segag80`, `tempest`, `aae_avg.cpp`, `SegaG80vid.cpp`, `vertigo_video.cpp`, `old_mame_vecsim_dvg.cpp`) + `gui/driver_gui.cpp` | emu-side |
| `set_texture_id` | `vidhrdwr/old_mame_vecsim_dvg.cpp` | emu-side |
| `vec_colors`, `colors` | emu-side only | emu-side |
| `draw_textured_shots` | `aae_video/opengl_renderer.cpp`, `aae_video/shader_definitions.h` | render-side |
| `modulate_color` | `aae_video/vector_draw.cpp` | render-side |
| `txdata` | render-side | render-side |
| `cache_texpoint`, `cache_tex_color` | **nothing outside `emu_vector_draw.cpp`** | private |
| `fpoint` | **nothing, anywhere** | dead |

The header includes `aae_mame_driver.h` (→ `windows.h`), `colordefs.h`, `render_types.h`, `MathUtils.h`.

`render_types.h` is already backend-neutral — the 2026-07-11 GL header scrub made `rtex_t` a plain `uint32_t` with no GL header behind it. So `set_texture_id(rtex_t*)` needs no change. Only `MathUtils.h` (`aae::math::mat4`, used solely by `draw_textured_shots`) is render-side.

---

## 4. Design

Five work items. **No behavior change is intended by any of them.**

### 4.1 `osdepend.h` becomes the contract header

`aae/aae/osdepend.h` gains declarations for all 29 `osd_*` functions, grouped by capability, each group in a marked section stating which targets must implement it:

| group | Windows | Linux | Teensy |
|---|---|---|---|
| input (11) | ✔ | ✔ | ✔ |
| file I/O (8) | ✔ | ✔ | ✔ (SD / flash) |
| video + bitmap (6) | ✔ | ✔ | ✖ stub — vector-only, no framebuffer |
| LED (4) | ✔ | ✖ stub | ✖ stub |

`osdepend.h` must itself be platform-neutral: no `windows.h`, no GL, no STL beyond `<cstdint>`.

**Relationship to the key sets.** `osdepend.h` keeps its existing `OSD_KEY_*` and `OSD_JOY_*` defines unchanged — those are the *logical* input names the 30 driver-side files use, they do not collide with anything on Linux, and they are not in scope for the rename. `osdepend.h` acquires **no** dependency on `sys_input.h`: its four apparent `KEY_*` references at lines 151–154 are inside comments, not code (verified 2026-07-28). It therefore stays fully self-contained and platform-neutral.

`AaeKey` is declared in `sys_input.h`, not `osdepend.h`: it describes *physical* keys reported by a backend, which is input-system territory, whereas `OSD_KEY_*` describes logical keys the emulation asks about. Collapsing the two numbering sets is deliberately **not** attempted here — it is a candidate for Phase 2, once the linker can prove what actually depends on which.

The five current homes (`os_input.h`, `mame_fileio.h`, `osd_video.h`, `old_mame_raster.h`, `led_service_handler.h`) keep their non-`osd_` content and include `osdepend.h`. Their `osd_*` declarations are removed, not duplicated — one declaration site only.

`struct osd_bitmap` moves to `osdepend.h` (it is part of the contract). The four byte-order macro aliases move with the file group.

### 4.2 Key rename and neutral input header

Replace the 140 `KEY_*` macros with an unscoped enum:

```c
enum AaeKey { AAEKEY_A = 0x41, AAEKEY_ESC = 0x1B, /* ... */ };
```

- **Values are unchanged.** This is a rename, not a renumber.
- Names are the existing suffix with an `AAEKEY_` prefix (`KEY_A` → `AAEKEY_A`).
- Unscoped, so values still convert implicitly to `int` — every `key[...]` index, `IsKeyDown(...)` call and `writeword()` cfg path compiles with nothing but the identifier swapped.
- An enum cannot leak into other headers the way a macro does, which is the root cause of the Linux collision. This fixes the class of problem, not only this instance.

Saved `.cfg` binding compatibility is explicitly **not** required (user decision, 2026-07-28) — bindings can be deleted and recreated. Since values are unchanged this should be moot in practice, but nothing in this design depends on preserving them.

`rawinput.h` splits in two:

- **`aae/system/input/sys_input.h`** — platform-neutral. The `AaeKey` enum, `key[256]`, `mouse_b`, query functions with `LONG`→`int32_t` and `INT`→`int`, the callback typedefs, the multi-HID `*_Ex` API and `RI_MAX_MICE`/`RI_MAX_KBDS`. **No `windows.h`.**
- **`aae/system/input/rawinput_win32.h`** — `RawInput_Initialize(HWND)`, `RawInput_ProcessInput(HWND, WPARAM, LPARAM)`, `RawInput_Shutdown()`, `RawInput_SetPaused(bool)`. Included only by `aae/system/window/winmain.cpp`.

The three drivers, `menu.cpp`, `acommon.cpp` and `os_input.cpp` then include only `sys_input.h`.

`HRESULT RawInput_Initialize` becomes `bool` on the neutral side; the Win32 header keeps whatever `winmain.cpp` needs.

### 4.3 Close both `framework.h` doors

Remove `#include "framework.h"` from both `aae_mame_driver.h:19` and `osd_video.h:61`, and break the `aae_mame_driver.h` ↔ `osd_video.h` cycle.

Whatever in `osd_video.h` genuinely requires Win32 either moves into a `.cpp` or goes behind the contract. `texture_handler.cpp` gains its own `#include "framework.h"`.

**This is the item with cascade risk** — it is the only one whose true blast radius cannot be established without compiling.

Adopt the leak-guard idiom already proven by the 2026-07-11 GL header scrub, substituting the Windows include guard for the GLEW one:

```c
#ifdef _WINDOWS_
#error "windows.h leaked into the emulation core"
#endif
```

placed in a handful of core translation units. This makes the boundary a permanent build-time regression test rather than a one-time cleanup.

### 4.4 Remove `mixer.h` from `aae_mame_driver.h`

Delete line 31; add `#include "mixer.h"` to the 19 files listed in §3.4. Mechanical.

This matters beyond tidiness: on Teensy there is no XAudio2, and `mixer.h` pulls `<xaudio2.h>` plus `<thread>`, `<condition_variable>` and `<atomic>` into all 109 consumers.

### 4.5 Split `emu_vector_draw.h`

- **Stays** in `emu_vector_draw.h` (emu-side): `add_line`, `add_tex`, `cache_clear`, `set_texture_id`, `vec_colors`, `colors`. Includes drop to `colordefs.h` + `render_types.h` — both already neutral. `aae_mame_driver.h` and `MathUtils.h` are removed.
- **Moves** to a renderer-side header under `aae/aae/aae_video/`: `draw_textured_shots(const aae::math::mat4&)`, `modulate_color`, `txdata`. This header keeps the `MathUtils.h` include.
- **Becomes file-static** in `emu_vector_draw.cpp`: `cache_texpoint`, `cache_tex_color`.
- **Deleted:** `fpoint` — unused anywhere in the tree.

After this, `add_line(sx, sy, ex, ey, intensity, col)` is the Teensy seam: the Teensy backend implements it by driving DACs instead of queuing vertices.

---

## 5. Non-goals

Explicitly **out of scope** for Phase 1:

- Moving files into `src/emu` / `src/osd` directories (Phase 2)
- CMake (Phase 2)
- Any Linux, evdev, ALSA, Vulkan or Teensy code (Phases 3–4)
- Removing `std::function` / `std::string` / `std::vector` from the CPU and timer cores (Phase 4)
- Editing `aae/aae.vcxproj` — it is tangled with driver work in progress. Headers do not need vcxproj entries to compile.
- Any change to emulation behavior, timing, audio or rendering output

---

## 6. Verification

No test framework exists in this project; the build plus a runtime pass is the test.

1. **Build** x64 (Debug or Release) via MSBuild:

   `MSBuild.exe aae\aae.vcxproj /p:Configuration=Release /p:Platform=x64 /v:q /nologo`

   Pass state: exit code 0, and no new warnings beyond the five pre-existing ones (`cpu_i8085.cpp` C4101, `foodf.cpp` C4333, `pacman.cpp`/`phoenix.cpp`/`gaplus_video.cpp` C4018).

   x86/Win32 configurations are known-broken and are not a gate.

2. **Leak guards** — the `#ifdef _WINDOWS_ → #error` blocks from §4.3 compile clean, proving the boundary holds.

3. **Runtime pass**, three games covering the three code paths touched:
   - `asteroid` — vector path (§4.5)
   - `pacman` — raster path (§4.3, `osd_video.h`)
   - `bzone` — sample-heavy, vector (§4.4 mixer, §4.5)

   Each must behave identically to the pre-change build: boots, plays, correct sound, correct input including the rebound-key path through KEY CONFIG.

4. **Input spot-check** — verify the multi-HID per-device keyboard/mouse assignment still works, since §4.2 touches that API surface.

---

## 7. Risks

| risk | likelihood | mitigation |
|---|---|---|
| §4.3 cascades — `osd_video.h` turns out to need Win32 in its public surface | medium | Sequence §4.3 last, after §4.1/4.2/4.4/4.5 are green. It is separable; if it proves large it becomes its own phase without invalidating the rest. |
| The 19-file `mixer.h` list is incomplete (a call site reached via a local macro or shadow prototype was missed) | low | The compiler finds the rest immediately. `rallyx.cpp`'s `PLAY`/`STOP` macros are the known instance of this pattern. |
| The `sys_input.h` split disturbs multi-HID behavior | low | Signature-preserving move; covered by verification step 4. |
| An `osd_*` implementation is found to have two definitions once declarations are centralized | low | This would be an existing latent bug; fix at the point of discovery and note it. |

---

## 8. Decisions on record

| decision | value | rationale |
|---|---|---|
| Teensy runs the emulation, not just DAC output | B | User, 2026-07-28. Forces a real core/OSD split. |
| `.cfg` binding compatibility | not required | User, 2026-07-28 — "they can always be deleted and recreated" |
| Key symbol form | unscoped `enum AaeKey`, values unchanged | Same mechanical cost as a macro rename; structurally prevents recurrence of the header-collision class |
| Keep VK-derived key values | yes | User clarified: rename, not renumber. Renumbering buys nothing — a translation table is needed on one side either way. |
| Canonical keycode set | AAE's existing numbering, not evdev | Linux backend translates evdev → `AaeKey`; keeps 18 call sites, the `key_names[]` table and `OSD_KEY_*` untouched |
