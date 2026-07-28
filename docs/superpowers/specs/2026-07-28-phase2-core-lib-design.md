# Phase 2 Design — Enforced Core/OSD Boundary + Portable Audio Interface

**Date:** 2026-07-28
**Status:** Approved for planning
**Branch context:** follows `refactor/osd-contract-phase1`
**Predecessor:** `docs/superpowers/specs/2026-07-28-osd-contract-design.md`

---

## 1. Motivation

Phase 1 declared the emulation-core ↔ OSD boundary and removed `windows.h` from the core. But "does the core depend on the OSD?" is still answered by **grepping**. Phase 2 makes the **linker** answer it.

The user is hand-writing the Linux window/input/audio shims (no SDL — a standing decision, see `linux-pi-port-plan`). So the deliverable that matters most is not the directory layout; it is a set of interfaces a backend author can implement, plus a build that mechanically rejects a core file reaching across the boundary.

**Phase 1 already delivered two of the three contracts** a backend author needs:

| contract | status |
|---|---|
| input — `aae/system/input/sys_input.h` | done (Phase 1) |
| OSD services — `aae/aae/osdepend.h`, 29 functions | done (Phase 1) |
| **audio — `IAudioBackend`** | **this phase** |
| window — `ISystemWindow` | **not yet — Phase 3's first task** |

---

## 2. Measured current state

All figures measured 2026-07-28 on `refactor/osd-contract-phase1`.

### 2.1 Half the core is already OSD-clean

Of 117 candidate `.cpp` files under `aae/aae` (excluding `aae_video/`), **63 include none of** `mixer.h`, `framework.h`, `sys_gl.h`, `opengl_renderer.h`, `menu.h`:

| clean | count | significance |
|---|---|---|
| `cpu_code` (incl. `8088/`) | 15 | every CPU core |
| `vidhrdwr` | 14 | the AVG/DVG vector generators |
| `drivers` | 18 | |
| `machine` | 8 | |
| root + `sndhrdwr` | 8 | |

The CPU, vector-generation and machine layers — precisely the Teensy-relevant subset — are clean today.

### 2.2 One header gates 72% of the remainder

Of the 54 blocked files: **39 include `mixer.h`** (24 drivers + 15 `sndhrdwr`, i.e. essentially all chip sound emulation). The rest: 12 `framework.h`, 13 `opengl_renderer.h`, 6 `menu.h`, 3 `sys_gl.h` (files may appear in more than one bucket).

### 2.3 The audio leak is three struct fields

`mixer.h` includes `<xaudio2.h>` (→ `objbase.h` → `windows.h`) solely because three public struct members are XAudio2 types:

```
aae/system/audio/mixer.h:475   IXAudio2SourceVoice* voice  = nullptr;   // struct CHANNEL
aae/system/audio/mixer.h:476   XAUDIO2_BUFFER       buffer = {};        // struct CHANNEL
aae/system/audio/mixer.h:507   WAVEFORMATEX         fx     = {};        // struct SAMPLE
```

`IAudioBackend` (`aae/system/audio/xaudio2_backend.h:25`) already exists and is a sound abstraction — ring-buffer model, `Init`/`Shutdown`/`GetNextBuffer`/`Submit`/`SetMasterVolume`/`OutputChannelCount`/`OutputChannelMask`. Its remaining problem is only that its signatures use Win32 spellings: `HRESULT Init(...)`, `BYTE* GetNextBuffer()`, `HRESULT Submit(BYTE*, DWORD)`.

Only 4 files reference `xaudio2_backend.h`, all within `aae/system/audio/` — the leak is well contained.

### 2.4 Every include is flat

All source uses flat includes (`#include "asteroid.h"`), resolved through 20 `AdditionalIncludeDirectories` entries. Two consequences:

- Relocating files would need **zero** `#include` edits — only project file lists and include dirs.
- **Directory structure therefore enforces nothing.** With every directory on the include path, a file in a notional `src/emu/` could still `#include "mixer.h"` and compile. Enforcement must come from a build target with a *restricted* include-dir list.

This is why Phase 2 does not move files (§5).

### 2.5 The vcxproj is not blocked

Phase 1 treated `aae/aae.vcxproj` as untouchable, inherited from the 2026-07-11 GL-scrub plan's note that it was "tangled with driver WIP". **That is stale.** The actual working-tree diff is one line:

```
-NDEBUG;_WINDOWS;GLEW_STATIC;_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR;%(PreprocessorDefinitions)
+NDEBUG;_WINDOWS;GLEW_STATIC;_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR;WIN7BUILD;%(PreprocessorDefinitions)
```

The vcxproj may be edited in Phase 2, provided the `WIN7BUILD` define survives. It selects `<xaudio2redist.h>` over `<xaudio2.h>` at `mixer.h:403-408`; both define `__XAUDIO2_INCLUDED__`.

---

## 3. Design

Two work items. **Item 1 before item 2**, so the library is defined once at its real size rather than defined at 63 files and immediately rewritten.

### 3.1 Item 1 — De-Win32 the audio interface

**Goal:** `mixer.h` stops including `<xaudio2.h>`, and therefore stops pulling `windows.h` into 39 files.

**a. Move the three XAudio2 fields behind an opaque pointer.** `struct CHANNEL` and `struct SAMPLE` keep their identity and all non-Win32 members; the voice/buffer/format triple moves into a backend-private type:

```c
// mixer.h - public
struct VoiceHandle;              // opaque; defined only in the backend .cpp

struct CHANNEL {
    /* ...existing non-Win32 members unchanged... */
    VoiceHandle* voice = nullptr;   // replaces IXAudio2SourceVoice* + XAUDIO2_BUFFER
};
```

The XAudio2 members move to the definition of `VoiceHandle` inside the backend `.cpp`. `SAMPLE::fx` (`WAVEFORMATEX`) is replaced by a neutral POD declared in `mixer.h`:

```c
struct WaveFormat { uint32_t rate; uint16_t channels; uint16_t bits; };
```

Conversion to `WAVEFORMATEX` happens only at the backend edge.

**`mixer.cpp` drives XAudio2 directly, and that is the real work in this item.** Beyond `IAudioBackend`, `mixer.cpp` holds a `g_xaudio2` global and manages source voices itself:

```
mixer.cpp:1449   FAILED(g_xaudio2->CreateSourceVoice(&ch.voice, &sample->fx, 0, 8.0f))
mixer.cpp:1478   FAILED(ch.voice->SubmitSourceBuffer(&ch.buffer))
mixer.cpp:1498   FAILED(ch.voice->Start())
```

This is the per-channel "voice path" that sits alongside the software-mix path. Once `CHANNEL::voice` is opaque, these three sites cannot reach through it.

**The voice path moves into the backend**, behind three new `IAudioBackend` methods:

```c
virtual VoiceHandle* VoiceCreate(const WaveFormat& fmt) = 0;
virtual bool         VoiceSubmit(VoiceHandle* v, const uint8_t* data, uint32_t bytes, bool loop) = 0;
virtual bool         VoiceStart(VoiceHandle* v) = 0;
```

The complete set was measured, not guessed — **12 distinct operations across 26 call sites** in `mixer.cpp`:

| operation | sites | | operation | sites |
|---|---|---|---|---|
| `DestroyVoice` | 6 | | `SubmitSourceBuffer` | 1 |
| `FlushSourceBuffers` | 5 | | `Start` | 1 |
| `Stop` | 4 | | `SetOutputMatrix` | 1 |
| `SetVolume` | 2 | | `SetFrequencyRatio` | 1 |
| `GetState` | 2 | | `GetVoiceDetails` | 1 |
| `CreateSourceVoice` | 1 | | `ExitLoop` | 1 |

Twelve methods is a tractable interface, and each maps onto something an ALSA backend can express (`GetState` → frames-consumed query; `ExitLoop` → clear the loop flag; `SetOutputMatrix` → per-channel gains). `GetVoiceDetails` is the one that does not generalise — it queries XAudio2 for channel count, which the backend already exposes via `OutputChannelCount()`; fold it into that rather than adding a method.

This is deliberate, not incidental: a Linux backend needs exactly this interface anyway. ALSA has no per-voice concept, so its implementation routes `VoiceCreate`/`VoiceSubmit` into the existing software mixer. Leaving the voice path in `mixer.cpp` would push that decision into shared code where it does not belong.

**`mixer.cpp` itself stays Win32-aware and that is correct** — it is implementation, not interface. Only `mixer.h` must become neutral. The measure of success is the header, not the source file.

**b. Scrub Win32 spellings from `IAudioBackend`**, so an ALSA implementation reads naturally:

| before | after |
|---|---|
| `HRESULT Init(int rateHz, int fps)` | `bool Init(int rateHz, int fps)` |
| `BYTE* GetNextBuffer()` | `uint8_t* GetNextBuffer()` |
| `HRESULT Submit(BYTE*, DWORD bytes)` | `bool Submit(uint8_t* buffer, uint32_t bytes)` |

`SetMasterVolume`/`GetMasterVolume`/`OutputChannelCount`/`OutputChannelMask`/`OutputRate`/`FramesPerUpdate` are already neutral and keep their signatures.

`HRESULT`→`bool` is the one semantic change, and it is safe — verified 2026-07-28, not assumed. `Init`'s result is tested with `FAILED()` only (`mixer.cpp:670-671`); `Submit`'s result is **ignored entirely** at both call sites (`mixer.cpp:783`, `mixer.cpp:940`); `GetNextBuffer` returns a pointer. No caller inspects a specific `HRESULT` value. The implementation logs the underlying `HRESULT` before returning `false`, so no diagnostic detail is lost.

**c. Acceptance test — already in the tree.** The `_WINDOWS_` boundary guard in `aae/aae/drivers/invaders.cpp` has been committed **commented out and tagged `TODO(Task 5)`** since Phase 1 Task 2, red by design because `invaders.cpp` needs audio. Item 1 is complete exactly when that guard can be uncommented and the build stays green. This is a mechanical pass/fail, not a judgement call.

### 3.2 Item 2 — `aae_core` static-library target

**Goal:** make a core file's reach across the boundary a compile error.

Add `aae/aae_core.vcxproj`, a static library (`.lib`) containing the core `.cpp` files, whose `AdditionalIncludeDirectories` **omits**:

- `./system/window` — `framework.h`
- `./aae/aae_video` — `opengl_renderer.h`, `sys_gl.h`, `vector_draw_gl.h`
- `./aae/gui` — `menu.h`

`./system/audio` is **not** excluded: after item 1, `mixer.h` is platform-neutral and the core legitimately uses it.

`aae.exe` links `aae_core.lib` and supplies the OSD side — the `osd_*` implementations (`os_input.cpp`, `mame_fileio.cpp`, `osd_video.cpp`, `led_service_handler.cpp`), the renderer, the window, the menu and the audio backend. Core→OSD calls resolve at link time through `osdepend.h`, which is exactly the contract Phase 1 established.

Expected membership after item 1: ~102 of 117 files. The ~15 that remain in the exe are genuine app/render glue — `acommon.cpp`, `menu.cpp`, `aae_emulator.cpp`, `gui/*`, `fileio/texture_handler.cpp`. That is the correct home for them, not a shortfall.

**Both configurations** (Debug|x64, Release|x64) get the new target. The `WIN7BUILD` define in Release must be preserved (§2.5). x86/Win32 remains known-broken and out of scope.

---

## 4. Verification

No unit-test framework exists; the build plus a runtime pass is the test.

1. **Item 1 red→green:** uncomment the `_WINDOWS_` guard in `invaders.cpp`. Build must be green with it live. Then also verify `mixer.h` contains no `#include <xaudio2*.h>`.
2. **Item 2 negative test:** temporarily add `#include "framework.h"` to a file in `aae_core` and confirm the build **fails** with a *file-not-found* error (not merely a `_WINDOWS_` guard trip). That proves the restricted include-dir list is doing the work, not the guard. Revert.
3. **Full build:** exit 0 with exactly the six known warning lines (`cpu_i8085.cpp` C4101; `foodf.cpp` C4333; `pacman.cpp` C4018 ×2 at 104 and 716; `phoenix.cpp` C4018; `gaplus_video.cpp` C4018). A seventh is a regression.
4. **Runtime:** `asteroid` (vector + samples), `pacman` (raster), `bzone` (sample-heavy vector) all boot, render and play sound. Item 1 touches every sample and stream path, so audio must be checked by ear, not just by log.
5. **Guard inventory intact:** the live `_WINDOWS_` guards from Phase 1 (`memory.cpp`, `cpu_6502.cpp`, `centiped.cpp`, `SegaG80vid.cpp`, `aae_avg.cpp`) and the `__XAUDIO2_INCLUDED__` guard (`memory.cpp`) still pass.

---

## 5. Non-goals

- **No file moves.** No `src/emu` / `src/osd` directories. Per §2.4 relocation buys zero enforcement, and 300 files of churn would obscure git blame across the whole codebase. Layout remains available as a later cosmetic pass, and is far safer once the lib target exists to catch anything a move breaks.
- **No CMake.** Needed for Linux/Teensy, but the Windows build is unaffected by this phase and CMake belongs with the toolchain that requires it — Phase 3.
- **No `ISystemWindow`.** The window interface is Phase 3's first task. Phase 2 does not touch `framework.h` or the 12 files using it.
- **No splitting of `emu_vector_draw.cpp`.** Known Phase 1 carry-over: it remains GL-saturated and a Teensy backend cannot link it. Real, but a distinct piece of work.
- **No behavior change.** Item 1's `HRESULT`→`bool` narrowing is the only semantic edit, and it is diagnostics-preserving by construction.

---

## 6. Risks

| risk | likelihood | mitigation |
|---|---|---|
| **The voice path is the real cost of item 1, not the three struct fields.** Making `CHANNEL::voice` opaque forces all 26 direct XAudio2 call sites in `mixer.cpp` behind new interface methods | **medium** (was assessed high before measurement) | Enumerated: 12 operations / 26 sites, listed in §3.1a. That is under the threshold at which item 1 would have needed its own phase, so it stays here. Build the interface from that measured list, and re-measure before starting in case the count has moved. |
| `SAMPLE::fx` (`WAVEFORMATEX`) is read by WAV loading and resampling, not only by voice creation | medium | Survey every `->fx` / `.fx` use in `mixer.cpp` before switching to `WaveFormat`. Conversion helper at the backend edge; keep field names aligned to minimise the diff. |
| Some `mixer.h` consumer depends on `windows.h` arriving transitively (same class as Phase 1's `inptport.h`) | medium | Expected; the compiler names each one. Fix by adding the true dependency, not by restoring the leak. |
| Splitting the vcxproj disturbs the build in ways unrelated to the boundary (PCH, per-file settings, link order) | medium | Introduce `aae_core.vcxproj` with settings copied verbatim from `aae.vcxproj`, changing only `ConfigurationType`, the file list and the include dirs. Verify Debug and Release separately. |
| `HRESULT`→`bool` loses a diagnostic some call site depended on | low | Grep every `IAudioBackend` call site first; log the `HRESULT` inside the backend before returning. |
| The 63/117 measurement shifts once item 1 lands and files are actually compiled under restricted includes | low | The figure is directional. Final membership is whatever compiles; record the real number and the reason for each exclusion. |

---

## 7. Decisions on record

| decision | value | rationale |
|---|---|---|
| Enforcement over layout | option A | User, 2026-07-28. Directory structure enforces nothing under flat includes (§2.4); a restricted-include lib target does. |
| Audio separation is in scope for Phase 2 | yes | User, 2026-07-28: "separate the emulation core from the windows/input/sound systems" — window and input landed in Phase 1, sound is the remainder. |
| Audio before the lib target | yes | Lets the lib be defined once at ~102 files instead of at 63 and rewritten. |
| Linux shims are hand-written, no SDL | standing | User, reaffirmed 2026-07-28. `IAudioBackend`'s ring-buffer model maps onto ALSA's `snd_pcm_writei`. |
| vcxproj may be edited | yes | §2.5 — the "tangled with WIP" constraint was stale; the real diff is one `WIN7BUILD` define. |
