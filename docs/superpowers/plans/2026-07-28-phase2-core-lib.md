# Phase 2 Implementation Plan — Portable Audio Interface + Enforced Core Library

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `mixer.h` platform-neutral, then add an `aae_core` static-library target whose include path physically cannot reach the OSD — turning "does the core depend on the OSD?" from a grep question into a linker question.

**Architecture:** Two halves. First, XAudio2 is pushed out of `mixer.h`'s public surface: `WAVEFORMATEX` becomes a neutral `WaveFormat` POD, and the `IXAudio2SourceVoice*` / `XAUDIO2_BUFFER` pair becomes an opaque `VoiceHandle*` manipulated through twelve new `IAudioBackend` methods. Second, an `aae_core.vcxproj` static library takes the 86 emulation-core translation units under `aae/aae/`, with `system/window`, `aae/aae_video` and `aae/gui` removed from its include-directory list; `aae.exe` links it and supplies everything under `system/`.

**Tech Stack:** C++17 / MSVC 2022 (v143), MSBuild, x64 only. **No test framework** — the tests are the build, a pre-existing `#error` guard, and a runtime pass.

**Spec:** `docs/superpowers/specs/2026-07-28-phase2-core-lib-design.md`

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

**Incremental builds legitimately print fewer warnings** when those files don't recompile. Use `-t:Rebuild` for any verification build.

**`WIN7BUILD` must survive.** `aae.vcxproj`'s `Release|x64` `PreprocessorDefinitions` contains `WIN7BUILD`; it selects `<xaudio2redist.h>` over `<xaudio2.h>` at `mixer.h:403-408`. Preserve it in both projects. This is the one uncommitted working-tree change in the vcxproj, and it is intentional.

**Do NOT touch** these WIP files: `aae/aae/drivers/bwidow.cpp`, `aae/aae/led_service_handler.cpp`, `aae/aae/drivers/tempest_with_random_checks.cpp`, `aae/aae/sndhrdwr/generic.cpp`, `aae/aae/sndhrdwr/generic.h`.

**Guard placement rule.** A `#error` boundary guard goes **immediately after the file's `#include` block, never before it.** Above the includes the preprocessor has read no header yet, so the macro under test is undefined and the guard passes unconditionally — a test incapable of failing. Also: **never leave HEAD un-buildable.** A guard whose boundary a later task closes is committed commented-out with a `TODO(Task N)` tag.

**Existing guard inventory — do not disturb.** Live `_WINDOWS_` guards in `memory.cpp`, `cpu_code/cpu_6502.cpp`, `drivers/centiped.cpp`, `vidhrdwr/SegaG80vid.cpp`, `vidhrdwr/aae_avg.cpp`; a `__XAUDIO2_INCLUDED__` guard in `memory.cpp`; parked guards in `drivers/invaders.cpp` (Task 4 of this plan turns it on) and `acommon.cpp` (stays parked — that file includes `framework.h` directly).

---

## File Structure

| file | status | responsibility |
|---|---|---|
| `aae/system/audio/mixer.h` | modify | Platform-neutral public audio API: `WaveFormat`, `SoundState`, `CHANNEL`, `SAMPLE`, sample/stream functions. **No `<xaudio2.h>` when this plan is done.** |
| `aae/system/audio/xaudio2_backend.h` | modify | `IAudioBackend` — the interface a Linux/ALSA backend implements. Neutral signatures. Declares `struct VoiceHandle;` |
| `aae/system/audio/xaudio2_backend.cpp` | modify | The Win32 backend. Defines `VoiceHandle`, owns all XAudio2 calls. |
| `aae/system/audio/mixer.cpp` | modify | Software mixer + channel bookkeeping. Calls the backend; holds no XAudio2 types. |
| `aae/system/audio/audio_3d.h` / `.cpp` | modify | Positional audio. Takes `VoiceHandle*`, not `IXAudio2SourceVoice*`. |
| `aae/aae_core.vcxproj` | **create** | Static library: the 86 emulation-core TUs, with a restricted include path. |
| `aae/aae.vcxproj` | modify | Drops the 86 core files, gains a `ProjectReference` to `aae_core`. |

---

## Task 0: Baseline

- [ ] **Step 1: Confirm HEAD is green**

Run the build. Expected: exit 0, exactly the six warning lines above.

If not green, STOP — do not start Task 1 on a broken tree.

- [ ] **Step 2: Confirm the parked guard is still parked**

```bash
grep -n '^// #ifdef _WINDOWS_' aae/aae/drivers/invaders.cpp
```

Expected: one hit, around line 34. The guard sits commented-out beneath a comment block explaining that `invaders.cpp`'s own `#include "mixer.h"` reaches `<windows.h>` via `<xaudio2.h>`'s COM/objbase chain. That guard is this plan's Task 4 acceptance test.

(There is no `TODO(...)` tag on it — Phase 1's Task 5 replaced the original tagged comment with the accurate explanation above. Do not go looking for one.)

- [ ] **Step 3: Re-measure the voice-path inventory**

```bash
grep -o 'voice->[A-Za-z]*\|g_xaudio2->[A-Za-z]*' aae/system/audio/mixer.cpp | sort | uniq -c | sort -rn
```

Expected 12 distinct operations / 26 sites: `DestroyVoice` 6, `FlushSourceBuffers` 5, `Stop` 4, `SetVolume` 2, `GetState` 2, and one each of `SubmitSourceBuffer`, `Start`, `SetOutputMatrix`, `SetFrequencyRatio`, `GetVoiceDetails`, `ExitLoop`, `CreateSourceVoice`.

If the counts differ, the code has moved since planning — report the new counts before proceeding.

---

## Task 1: `WaveFormat` replaces `WAVEFORMATEX`

`SAMPLE::fx` is a `WAVEFORMATEX`, one of three reasons `mixer.h` includes `<xaudio2.h>`. It is read or written at ~80 sites in `mixer.cpp`, and **all seven fields are used** — a three-field POD is not enough.

**Files:**
- Modify: `aae/system/audio/mixer.h` (struct `SAMPLE`, ~line 507)
- Modify: `aae/system/audio/mixer.cpp` (~80 sites)
- Modify: `aae/system/audio/xaudio2_backend.cpp` (conversion helper)

- [ ] **Step 1: Add `WaveFormat` and the PCM constant to `mixer.h`**

Insert above `struct SAMPLE`:

```c
// Platform-neutral replacement for WAVEFORMATEX. Field semantics are
// identical; only the spelling changes, so a backend converts by field copy.
// All seven members are used by mixer.cpp - do not trim this struct.
struct WaveFormat {
    uint16_t format_tag    = 1;   // AAE_WAVE_FORMAT_PCM
    uint16_t channels      = 0;
    uint32_t rate          = 0;   // samples per second   (was nSamplesPerSec)
    uint32_t avg_bytes_sec = 0;   //                      (was nAvgBytesPerSec)
    uint16_t block_align   = 0;   //                      (was nBlockAlign)
    uint16_t bits          = 0;   // bits per sample      (was wBitsPerSample)
    uint16_t cb_size       = 0;   // 0 for PCM
};

// Mirrors WAVE_FORMAT_PCM from <mmreg.h> so mixer.h needs no Windows header.
#define AAE_WAVE_FORMAT_PCM 1
```

- [ ] **Step 2: Change the `SAMPLE` member**

In `struct SAMPLE` (`mixer.h:507`), replace:

```c
	WAVEFORMATEX fx = {};
```

with:

```c
	WaveFormat fx = {};
```

- [ ] **Step 3: Rename every field use in `mixer.cpp`**

Apply these renames to every `fx.` access. There are ~80; the compiler will find any you miss.

| old | new |
|---|---|
| `fx.nSamplesPerSec` | `fx.rate` |
| `fx.nChannels` | `fx.channels` |
| `fx.wBitsPerSample` | `fx.bits` |
| `fx.nBlockAlign` | `fx.block_align` |
| `fx.nAvgBytesPerSec` | `fx.avg_bytes_sec` |
| `fx.wFormatTag` | `fx.format_tag` |
| `fx.cbSize` | `fx.cb_size` |

Also replace `WAVE_FORMAT_PCM` with `AAE_WAVE_FORMAT_PCM` at `mixer.cpp:595`, `:1693`, `:1928`, `:2242`, `:2324`, `:2358`.

Remove now-unneeded `static_cast<WORD>(...)` / `static_cast<DWORD>(...)` around assignments to these fields (e.g. `mixer.cpp:599`, `:1696`, `:1697`, `:1875`, `:1931`, `:1932`) — the target types are now `uint16_t`/`uint32_t`. If a cast is still needed to silence a conversion warning, use `static_cast<uint16_t>` / `static_cast<uint32_t>`, never `WORD`/`DWORD`.

**Watch `mixer.cpp:2082-2087`** — it serialises the format into a WAV header by appending each field separately:

```c
	append(&sample->fx.wFormatTag, 2);
	append(&sample->fx.nChannels, 2);
	append(&sample->fx.nSamplesPerSec, 4);
	append(&sample->fx.nAvgBytesPerSec, 4);
	append(&sample->fx.nBlockAlign, 2);
	append(&sample->fx.wBitsPerSample, 2);
```

becomes:

```c
	append(&sample->fx.format_tag, 2);
	append(&sample->fx.channels, 2);
	append(&sample->fx.rate, 4);
	append(&sample->fx.avg_bytes_sec, 4);
	append(&sample->fx.block_align, 2);
	append(&sample->fx.bits, 2);
```

The byte counts stay the same because the field widths are unchanged (`uint16_t`=2, `uint32_t`=4). **The WAV output must be byte-identical** — this writes real files.

Likewise `mixer.cpp:2230-2235` parses a WAV header with `std::memcpy` into individual fields; the widths match, so only the names change.

- [ ] **Step 4: Add the conversion helper in the backend**

In `aae/system/audio/xaudio2_backend.cpp`, add a file-local helper:

```c
static WAVEFORMATEX ToWaveFormatEx(const WaveFormat& f)
{
    WAVEFORMATEX w{};
    w.wFormatTag      = f.format_tag;
    w.nChannels       = f.channels;
    w.nSamplesPerSec  = f.rate;
    w.nAvgBytesPerSec = f.avg_bytes_sec;
    w.nBlockAlign     = f.block_align;
    w.wBitsPerSample  = f.bits;
    w.cbSize          = f.cb_size;
    return w;
}
```

`mixer.cpp:1449`'s `CreateSourceVoice(&ch.voice, &sample->fx, ...)` still passes `&sample->fx` and will not compile. Leave that one site broken for now — **Task 2 moves it into the backend**. To keep this task's build green, temporarily convert at the call site:

```c
	const WAVEFORMATEX wfx = ToWaveFormatEx(sample->fx);
	if (FAILED(g_xaudio2->CreateSourceVoice(&ch.voice, &wfx, 0, 8.0f)))
```

and declare `ToWaveFormatEx` in `xaudio2_backend.h` so `mixer.cpp` can call it. Task 2 removes this temporary exposure.

- [ ] **Step 5: Build**

Run the build. Expected: exit 0, exactly the six warning lines.

Expect `error C2039: 'nSamplesPerSec': is not a member of 'WaveFormat'` for any site you missed — fix and repeat.

- [ ] **Step 6: Verify WAV output is unchanged**

`save_sample_to_buffer` writes real files. Confirm the header-writing path still produces a valid 44-byte RIFF header by inspecting the code, and confirm no `append()` byte count changed. State this explicitly in your report.

- [ ] **Step 7: Commit**

```bash
git add aae/system/audio/mixer.h aae/system/audio/mixer.cpp aae/system/audio/xaudio2_backend.h aae/system/audio/xaudio2_backend.cpp
git commit -m "refactor(audio): replace WAVEFORMATEX with a neutral WaveFormat POD

All seven fields are used across ~80 sites in mixer.cpp, so the struct
carries all seven. Field widths are unchanged, keeping the WAV header
read/write paths byte-identical.

One temporary ToWaveFormatEx() call remains in mixer.cpp for
CreateSourceVoice; Task 2 moves it into the backend."
```

---

## Task 2: `VoiceHandle` replaces the XAudio2 voice pair

`CHANNEL` holds `IXAudio2SourceVoice* voice` and `XAUDIO2_BUFFER buffer` — the other two reasons `mixer.h` includes `<xaudio2.h>`. Making them opaque forces all 26 direct XAudio2 sites in `mixer.cpp` behind the backend interface.

**Files:**
- Modify: `aae/system/audio/mixer.h` (struct `CHANNEL`, ~lines 475-476)
- Modify: `aae/system/audio/xaudio2_backend.h` (12 new methods, `VoiceHandle` fwd decl)
- Modify: `aae/system/audio/xaudio2_backend.cpp` (define `VoiceHandle`, implement the 12)
- Modify: `aae/system/audio/mixer.cpp` (26 call sites)

- [ ] **Step 1: Declare the opaque handle and the twelve methods**

In `aae/system/audio/xaudio2_backend.h`, above `class IAudioBackend`:

```c
// Opaque per-channel voice. The concrete definition lives in the backend
// .cpp - mixer.cpp only ever holds a pointer. A backend with no per-voice
// concept (ALSA) routes these into its own software mixer.
struct VoiceHandle;
```

Add to `IAudioBackend`, before the `protected:` section:

```c
	// --- Per-channel voice path -------------------------------------------
	// Returns nullptr on failure. The backend owns the allocation; release
	// it with VoiceDestroy.
	virtual VoiceHandle* VoiceCreate(const WaveFormat& fmt) = 0;
	virtual void         VoiceDestroy(VoiceHandle* v) = 0;

	// Queue PCM for playback. loop=true repeats indefinitely.
	virtual bool VoiceSubmit(VoiceHandle* v, const uint8_t* data,
	                         uint32_t bytes, bool loop) = 0;
	virtual bool VoiceStart(VoiceHandle* v) = 0;
	virtual void VoiceStop(VoiceHandle* v) = 0;
	virtual void VoiceFlush(VoiceHandle* v) = 0;

	// Stop looping at the end of the current pass; the tail still plays.
	virtual void VoiceExitLoop(VoiceHandle* v) = 0;

	virtual void VoiceSetVolume(VoiceHandle* v, float gain) = 0;
	virtual void VoiceSetFrequencyRatio(VoiceHandle* v, float ratio) = 0;

	// Number of buffers still queued. 0 means playback has drained - this is
	// how the mixer decides a one-shot has finished.
	virtual uint32_t VoiceBuffersQueued(VoiceHandle* v) = 0;

	// Source channel count of this voice (1 or 2).
	virtual uint32_t VoiceInputChannels(VoiceHandle* v) = 0;

	// Per-channel gain matrix, src*dst floats in row-major order.
	virtual void VoiceSetOutputMatrix(VoiceHandle* v, uint32_t srcChannels,
	                                  uint32_t dstChannels,
	                                  const float* matrix) = 0;
```

`mixer.h` must be included by `xaudio2_backend.h` for `WaveFormat` — add `#include "mixer.h"` if not already present.

- [ ] **Step 2: Define `VoiceHandle` and implement the twelve in the backend**

In `aae/system/audio/xaudio2_backend.cpp`:

```c
struct VoiceHandle {
    IXAudio2SourceVoice* voice  = nullptr;
    XAUDIO2_BUFFER       buffer = {};
};
```

Implement each method on `XAudio2Backend` by moving the corresponding code out of `mixer.cpp`. The mapping from the measured inventory:

| method | replaces (mixer.cpp) |
|---|---|
| `VoiceCreate` | `:1449` `g_xaudio2->CreateSourceVoice` |
| `VoiceDestroy` | `:1087`, `:1442`, `:1483`, `:1501`, `:1595`, `:1678` `DestroyVoice` |
| `VoiceStop` | `:1085`, `:1440`, `:1593`, `:1676` `Stop` |
| `VoiceFlush` | `:1086`, `:1441`, `:1500`, `:1594`, `:1677` `FlushSourceBuffers` |
| `VoiceSubmit` | `:1473-1478` (memset + AudioBytes/pAudioData/LoopCount + `SubmitSourceBuffer`) |
| `VoiceStart` | `:1498` `Start` |
| `VoiceExitLoop` | `:1566` `ExitLoop` |
| `VoiceSetVolume` | `:1241`, `:1491` `SetVolume` |
| `VoiceSetFrequencyRatio` | `:1318` `SetFrequencyRatio` |
| `VoiceBuffersQueued` | `:962-963`, `:1525-1526` `GetState` + `XAUDIO2_VOICE_STATE` |
| `VoiceInputChannels` | `:1132-1133` `GetVoiceDetails` |
| `VoiceSetOutputMatrix` | `:1168` `SetOutputMatrix` |

`VoiceSubmit` absorbs the `XAUDIO2_BUFFER` setup that was at `mixer.cpp:1473-1476`:

```c
bool XAudio2Backend::VoiceSubmit(VoiceHandle* v, const uint8_t* data,
                                 uint32_t bytes, bool loop)
{
    if (!v || !v->voice) return false;
    std::memset(&v->buffer, 0, sizeof(v->buffer));
    v->buffer.AudioBytes = bytes;
    v->buffer.pAudioData = data;
    v->buffer.LoopCount  = loop ? XAUDIO2_LOOP_INFINITE : 0;
    if (FAILED(v->voice->SubmitSourceBuffer(&v->buffer))) {
        LOG_ERROR("VoiceSubmit: SubmitSourceBuffer failed");
        return false;
    }
    return true;
}
```

`VoiceBuffersQueued`:

```c
uint32_t XAudio2Backend::VoiceBuffersQueued(VoiceHandle* v)
{
    if (!v || !v->voice) return 0;
    XAUDIO2_VOICE_STATE st{};
    v->voice->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    return st.BuffersQueued;
}
```

Every method must null-check `v` and `v->voice` — `mixer.cpp` currently guards with `if (ch.voice)` at each site, and that guard moves inside.

- [ ] **Step 3: Change the `CHANNEL` members**

In `mixer.h`, replace lines 475-476:

```c
	IXAudio2SourceVoice* voice = nullptr;
	XAUDIO2_BUFFER buffer = {};
```

with:

```c
	VoiceHandle* voice = nullptr;   // opaque; owned by the audio backend
```

`CHANNEL::buffer` disappears entirely — the backend owns it now. Add `struct VoiceHandle;` as a forward declaration near the top of `mixer.h`.

- [ ] **Step 4: Rewrite the 26 call sites in `mixer.cpp`**

Each becomes a `g_backend->Voice*(ch.voice, ...)` call. Examples:

```c
	// was: ch.voice->Stop(); ch.voice->FlushSourceBuffers(); ch.voice->DestroyVoice();
	g_backend->VoiceStop(ch.voice);
	g_backend->VoiceFlush(ch.voice);
	g_backend->VoiceDestroy(ch.voice);
	ch.voice = nullptr;
```

```c
	// was: XAUDIO2_VOICE_STATE st{}; ch.voice->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
	//      ... st.BuffersQueued ...
	const uint32_t queued = g_backend->VoiceBuffersQueued(ch.voice);
```

Change `SetPan` (`mixer.cpp:1127`) to take the handle:

```c
static void SetPan(VoiceHandle* voice, int panByte)
{
    if (!voice) return;
    const uint32_t srcCh = g_backend->VoiceInputChannels(voice);
    /* ...existing matrix maths unchanged... */
    g_backend->VoiceSetOutputMatrix(voice, srcCh, dstCh, m.data());
}
```

Delete the `g_xaudio2` global (`mixer.cpp:207`) once `CreateSourceVoice` has moved. Remove the temporary `ToWaveFormatEx` call added in Task 1 Step 4 — `VoiceCreate` does the conversion internally now — and remove its declaration from `xaudio2_backend.h`.

- [ ] **Step 5: Build**

Run the build. Expected: exit 0, exactly the six warning lines.

- [ ] **Step 6: Commit**

```bash
git add aae/system/audio/
git commit -m "refactor(audio): put the voice path behind IAudioBackend

CHANNEL::voice becomes an opaque VoiceHandle* and CHANNEL::buffer moves
into the backend, so mixer.cpp no longer touches XAudio2. Twelve new
interface methods cover the 26 direct call sites measured during planning.

A backend with no per-voice concept (ALSA) implements these against its
own software mixer."
```

---

## Task 3: `audio_3d` takes `VoiceHandle*`

`audio_3d_apply_2d(ch.voice, ...)` is called at `mixer.cpp:974` and `:1381`. Its signature takes `IXAudio2SourceVoice*`, so positional audio would re-leak XAudio2.

**Files:**
- Modify: `aae/system/audio/audio_3d.h`, `aae/system/audio/audio_3d.cpp`

- [ ] **Step 1: Change the signature**

In `audio_3d.h`, change the `IXAudio2SourceVoice*` parameter of `audio_3d_apply_2d` (and any sibling function taking one) to `VoiceHandle*`. Add `struct VoiceHandle;` forward declaration. Remove any `<xaudio2.h>` include from `audio_3d.h`.

- [ ] **Step 2: Update the implementation**

In `audio_3d.cpp`, route the per-voice work through the backend rather than calling XAudio2 directly. If it calls `SetOutputMatrix`, use `g_backend->VoiceSetOutputMatrix(...)`; if it queries channels, use `VoiceInputChannels`. `audio_3d.cpp` may keep including XAudio2 headers if it needs X3DAudio maths — only the *header* must be neutral.

If `audio_3d.cpp` turns out to need XAudio2 APIs with no equivalent among the twelve methods, **STOP and report** which — that means the interface is short and needs a thirteenth method rather than a workaround.

- [ ] **Step 3: Build and commit**

Run the build. Expected: exit 0, six warning lines.

```bash
git add aae/system/audio/audio_3d.h aae/system/audio/audio_3d.cpp aae/system/audio/mixer.cpp
git commit -m "refactor(audio): audio_3d takes VoiceHandle, not IXAudio2SourceVoice"
```

---

## Task 4: Drop `<xaudio2.h>` from `mixer.h` — the acceptance test

This is the task the whole audio half exists for.

**Files:**
- Modify: `aae/system/audio/mixer.h` (remove the xaudio2 include block, ~lines 403-408)
- Modify: `aae/system/audio/xaudio2_backend.h` (scrub Win32 spellings)
- Modify: `aae/aae/drivers/invaders.cpp` (turn the parked guard on)

- [ ] **Step 1: Scrub the Win32 spellings from `IAudioBackend`**

In `xaudio2_backend.h`:

| before | after |
|---|---|
| `virtual HRESULT Init(int rateHz, int fps) = 0;` | `virtual bool Init(int rateHz, int fps) = 0;` |
| `virtual BYTE* GetNextBuffer() = 0;` | `virtual uint8_t* GetNextBuffer() = 0;` |
| `virtual HRESULT Submit(BYTE* buffer, DWORD bytes) = 0;` | `virtual bool Submit(uint8_t* buffer, uint32_t bytes) = 0;` |

Update `XAudio2Backend`'s overrides to match, returning `true`/`false`. Log the underlying `HRESULT` before returning `false` so no diagnostic detail is lost.

Call sites (verified — none inspects a specific `HRESULT`):
- `mixer.cpp:670-671` — `const HRESULT hr = backend->Init(...); if (FAILED(hr))` becomes `if (!backend->Init(...))`
- `mixer.cpp:766` — `BYTE* soundbuffer = ...` becomes `uint8_t* soundbuffer = ...`
- `mixer.cpp:783`, `:940` — `Submit(...)` return is already ignored; just change the `static_cast<DWORD>` at `:940` to `static_cast<uint32_t>`

- [ ] **Step 2: Remove the xaudio2 include from `mixer.h`**

Delete this block (`mixer.h:403-408`):

```c
#ifndef WIN7BUILD 
#include <xaudio2.h> 
#else 
#include <xaudio2redist.h> 
#endif
```

Move it into `xaudio2_backend.h` if it is not already there — that header is Win32-only and is the right home.

- [ ] **Step 3: Add a neutrality guard to `mixer.h`**

At the **end** of `mixer.h`, after all its includes and declarations:

```c
// Boundary guard: mixer.h is the platform-neutral audio contract. If this
// fires, an XAudio2 header re-entered - fix the header, not this guard.
// (Consumers that include <xaudio2.h> themselves before mixer.h would trip
// this spuriously; none do today. If that changes, move the guard rather
// than deleting it.)
#ifdef __XAUDIO2_INCLUDED__
#error "xaudio2 leaked back into mixer.h - it must stay platform-neutral"
#endif
```

- [ ] **Step 4: Turn on the parked guard in `invaders.cpp` — THE ACCEPTANCE TEST**

`aae/aae/drivers/invaders.cpp` carries a `_WINDOWS_` guard committed commented-out since Phase 1, tagged `TODO(Task 5)`, red because `invaders.cpp` includes `mixer.h` which pulled `windows.h`.

Uncomment the three directive lines, delete the `TODO` tag, and rewrite the explanatory comment in past tense:

```c
// Boundary guard: nothing driver code includes may drag in the Win32 API.
// MUST stay below every #include above - the preprocessor is a single forward
// pass, so a guard placed above the includes can never fire.
//
// Was parked through Phase 1: mixer.h reached <windows.h> via <xaudio2.h>.
// Phase 2 made mixer.h neutral, so this now passes.
#ifdef _WINDOWS_
#error "windows.h leaked into driver code"
#endif
```

- [ ] **Step 5: Build — this is the pass/fail moment**

Run the build. Expected: **exit 0, exactly the six warning lines, with the `invaders.cpp` guard live.**

If it fails with `#error "windows.h leaked into driver code"`, something in `invaders.cpp`'s include chain still reaches `windows.h`. Find it with `/showIncludes`:

```bash
"/c/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" aae/aae.vcxproj -p:Configuration=Release -p:Platform=x64 -nologo -v:n -p:ShowIncludes=true 2>&1 | grep -B40 'invaders.cpp' | grep -i 'windows.h'
```

Report the chain rather than guessing.

- [ ] **Step 6: Confirm `mixer.h` is genuinely neutral**

```bash
grep -n 'xaudio2\|windows\.h\|HRESULT\|BYTE\|DWORD\|WORD\|IXAudio2\|WAVEFORMATEX' aae/system/audio/mixer.h
```

Expected: hits only in comments. Any live code hit is a miss.

- [ ] **Step 7: Commit**

```bash
git add aae/system/audio/ aae/aae/drivers/invaders.cpp
git commit -m "refactor(audio): mixer.h is now platform-neutral

Drops <xaudio2.h> and scrubs HRESULT/BYTE/DWORD from IAudioBackend. The
_WINDOWS_ guard parked in invaders.cpp since Phase 1 is live and green -
that guard was this change's acceptance test.

39 core files no longer see windows.h through the audio header."
```

---

## Task 5: Create `aae_core.vcxproj`

**Files:**
- Create: `aae/aae_core.vcxproj`

- [ ] **Step 1: Write the project file**

Create `aae/aae_core.vcxproj` with exactly this content. The `ClCompile` list in the final `ItemGroup` is filled in at Step 2.

**Generate a fresh GUID** for `<ProjectGuid>` — run `powershell -Command "[guid]::NewGuid().ToString().ToUpper()"` and paste it in the form `{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}`. Record it; Task 6 needs the same value.

```xml
<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="Debug|Win32"><Configuration>Debug</Configuration><Platform>Win32</Platform></ProjectConfiguration>
    <ProjectConfiguration Include="Release|Win32"><Configuration>Release</Configuration><Platform>Win32</Platform></ProjectConfiguration>
    <ProjectConfiguration Include="Debug|x64"><Configuration>Debug</Configuration><Platform>x64</Platform></ProjectConfiguration>
    <ProjectConfiguration Include="Release|x64"><Configuration>Release</Configuration><Platform>x64</Platform></ProjectConfiguration>
  </ItemGroup>

  <PropertyGroup Label="Globals">
    <VCProjectVersion>17.0</VCProjectVersion>
    <ProjectGuid>{GENERATE-A-FRESH-GUID-SEE-ABOVE}</ProjectGuid>
    <RootNamespace>aaecore</RootNamespace>
    <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>
  </PropertyGroup>

  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.Default.props" />

  <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Debug|x64'" Label="Configuration">
    <ConfigurationType>StaticLibrary</ConfigurationType>
    <UseDebugLibraries>true</UseDebugLibraries>
    <PlatformToolset>v143</PlatformToolset>
    <CharacterSet>Unicode</CharacterSet>
  </PropertyGroup>
  <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Release|x64'" Label="Configuration">
    <ConfigurationType>StaticLibrary</ConfigurationType>
    <UseDebugLibraries>false</UseDebugLibraries>
    <PlatformToolset>v143</PlatformToolset>
    <WholeProgramOptimization>true</WholeProgramOptimization>
    <CharacterSet>Unicode</CharacterSet>
  </PropertyGroup>
  <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Debug|Win32'" Label="Configuration">
    <ConfigurationType>StaticLibrary</ConfigurationType>
    <UseDebugLibraries>true</UseDebugLibraries>
    <PlatformToolset>v143</PlatformToolset>
    <CharacterSet>Unicode</CharacterSet>
  </PropertyGroup>
  <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Release|Win32'" Label="Configuration">
    <ConfigurationType>StaticLibrary</ConfigurationType>
    <UseDebugLibraries>false</UseDebugLibraries>
    <PlatformToolset>v143</PlatformToolset>
    <CharacterSet>Unicode</CharacterSet>
  </PropertyGroup>

  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.props" />
  <ImportGroup Label="ExtensionSettings" />
  <ImportGroup Label="PropertySheets">
    <Import Project="$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props"
            Condition="exists('$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props')"
            Label="LocalAppDataPlatform" />
  </ImportGroup>

  <ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Debug|x64'">
    <ClCompile>
      <WarningLevel>Level3</WarningLevel>
      <SDLCheck>true</SDLCheck>
      <ConformanceMode>true</ConformanceMode>
      <LanguageStandard>stdcpp17</LanguageStandard>
      <LanguageStandard_C>stdc17</LanguageStandard_C>
      <PreprocessorDefinitions>_DEBUG;_WINDOWS;GLEW_STATIC;_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR;%(PreprocessorDefinitions)</PreprocessorDefinitions>
      <AdditionalIncludeDirectories>./aae;./aae/sndhrdwr;./aae/vidhrdwr;./aae/machine;./aae/fileio;./aae/drivers;./aae/cpu_code;./system/audio;./;./system/graphics;./system/input;./system/font;./system/math;./system/util;./system/3rdparty;./system/platform;./system/project</AdditionalIncludeDirectories>
    </ClCompile>
  </ItemDefinitionGroup>
  <ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Release|x64'">
    <ClCompile>
      <WarningLevel>Level3</WarningLevel>
      <FunctionLevelLinking>true</FunctionLevelLinking>
      <IntrinsicFunctions>true</IntrinsicFunctions>
      <SDLCheck>true</SDLCheck>
      <ConformanceMode>true</ConformanceMode>
      <LanguageStandard>stdcpp17</LanguageStandard>
      <LanguageStandard_C>stdc17</LanguageStandard_C>
      <PreprocessorDefinitions>NDEBUG;_WINDOWS;GLEW_STATIC;_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR;WIN7BUILD;%(PreprocessorDefinitions)</PreprocessorDefinitions>
      <AdditionalIncludeDirectories>./aae;./aae/sndhrdwr;./aae/vidhrdwr;./aae/machine;./aae/fileio;./aae/drivers;./aae/cpu_code;./system/audio;./;./system/graphics;./system/input;./system/font;./system/math;./system/util;./system/3rdparty;./system/platform;./system/project</AdditionalIncludeDirectories>
    </ClCompile>
  </ItemDefinitionGroup>

  <ItemGroup>
    <!-- Step 2 fills in the 86 core ClCompile entries here -->
  </ItemGroup>

  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.targets" />
  <ImportGroup Label="ExtensionTargets" />
</Project>
```

Three deliberate differences from `aae.vcxproj`:
- `ConfigurationType` is `StaticLibrary`, not `Application`.
- **No `<Link>` section and no `app.manifest`** — a static library does not link.
- `AdditionalIncludeDirectories` omits `./aae/aae_video`, `./aae/gui` and `./system/window`. **That omission is the entire point of this task** — do not add them back to fix a build error.

Everything else (`v143`, `Unicode`, `10.0`, `stdcpp17`/`stdc17`, `ConformanceMode`, `SDLCheck`, `Level3`, the Release `FunctionLevelLinking`/`IntrinsicFunctions`/`WholeProgramOptimization`, and the `PreprocessorDefinitions` including `WIN7BUILD`) matches `aae.vcxproj` exactly.

The `Win32` configurations are present so the solution stays consistent but carry no compiler settings — matching `aae.vcxproj`, where those configs are known-broken and out of scope.

**No precompiled headers** — `aae.vcxproj` uses none.

- [ ] **Step 2: Add the 86 core files**

Add a `<ClCompile Include="..."/>` entry for **every `aae\...` path currently in `aae.vcxproj` EXCEPT these 29**, which stay in the exe:

```
aae\aae_emulator.cpp
aae\acommon.cpp
aae\config.cpp
aae\menu.cpp
aae\os_basic.cpp
aae\led_service_handler.cpp
aae\aae_video\gl_fbo.cpp
aae\aae_video\gl_shader.cpp
aae\aae_video\gl_texturing.cpp
aae\aae_video\mame_layout.cpp
aae\aae_video\mame_vector.cpp
aae\aae_video\opengl_renderer.cpp
aae\aae_video\vector_draw.cpp
aae\aae_video\vector_fonts.cpp
aae\cpu_code\ccpu.cpp
aae\drivers\astrocade.cpp
aae\drivers\mhavoc.cpp
aae\drivers\starwars.cpp
aae\drivers\tempest.cpp
aae\drivers\warlord.cpp
aae\fileio\mame_fileio.cpp
aae\fileio\texture_handler.cpp
aae\gui\driver_gui.cpp
aae\sndhrdwr\galsnd_stream.cpp
aae\sndhrdwr\namco.cpp
aae\sndhrdwr\segag80snd.cpp
aae\vidhrdwr\emu_vector_draw.cpp
aae\vidhrdwr\fast_poly.cpp
aae\vidhrdwr\osd_video.cpp
```

**No `system\...` file goes in the core library.** All 19 stay in the exe — they are platform backends (`Joystick.cpp` uses XInput/DirectInput, `wintimer.cpp` uses mmsystem, `path_helper.cpp`/`sys_fileio.cpp`/`sys_log.cpp` use `<windows.h>` directly, `glew.c` is OpenGL). Their *headers* remain on the core's include path where neutral, so core code still calls `LOG_INFO`, `sample_start` and `IsKeyDown`; those symbols resolve at link time against the exe.

Expected: **86 `ClCompile` entries** in `aae_core.vcxproj`.

Two entries carry a per-file override in `aae.vcxproj` — copy it verbatim if the file lands in the core lib:

```xml
<ClCompile Include="aae\cpu_code\ccpu.cpp">
  <ExcludedFromBuild Condition="'$(Configuration)|$(Platform)'=='Release|x64'">false</ExcludedFromBuild>
</ClCompile>
```

(`ccpu.cpp` is in the EXE list above, so this applies to it there; `osd_video.cpp` likewise.)

- [ ] **Step 3: Build the library alone**

```bash
"/c/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" aae/aae_core.vcxproj -t:Rebuild -p:Configuration=Release -p:Platform=x64 -v:q -nologo
```

Expected: exit 0. A static library does not link, so **undefined symbols are normal and expected** — `osd_*`, `LOG_INFO`, `sample_start` etc. resolve later against the exe.

Expect `fatal error C1083: Cannot open include file: 'framework.h'` (or `opengl_renderer.h` / `menu.h` / `sys_gl.h`) for any file that should have been on the EXE list. **That error is the whole point** — it is the boundary working. Move the offending file to the exe list and record it in your report; do not add the directory back to the include path.

- [ ] **Step 4: Commit**

```bash
git add aae/aae_core.vcxproj
git commit -m "build: add aae_core static library with a restricted include path

86 emulation-core translation units under aae/aae/. Its include list omits
system/window, aae/aae_video and aae/gui, so a core file reaching for the
OSD is now a compile error rather than something a grep might catch.

All of system/ stays in the exe - those are platform backends."
```

---

## Task 6: Link `aae_core` into `aae.exe`

**Files:**
- Modify: `aae/aae.vcxproj`

- [ ] **Step 1: Remove the 86 core files from `aae.vcxproj`**

Delete the `<ClCompile>` entries now built by the library. `aae.vcxproj` keeps exactly the 29 `aae\...` files listed in Task 5 Step 2 plus all 19 `system\...` files — **48 `ClCompile` entries**.

Leave every `<ClInclude>` entry alone; headers are not compiled and the lists may overlap harmlessly.

- [ ] **Step 2: Add the project reference**

Before the closing `</Project>`, alongside the existing `<ImportGroup>` elements:

First read back the GUID you generated in Task 5:

```bash
grep -o '<ProjectGuid>{[^}]*}</ProjectGuid>' aae/aae_core.vcxproj
```

Then add this before the closing `</Project>` of `aae/aae.vcxproj`, substituting that exact GUID (including the braces):

```xml
<ItemGroup>
  <ProjectReference Include="aae_core.vcxproj">
    <Project>{THE-GUID-FROM-THE-GREP-ABOVE}</Project>
  </ProjectReference>
</ItemGroup>
```

MSBuild builds the library first and links `aae_core.lib` automatically. If the GUID does not match `aae_core.vcxproj`'s `<ProjectGuid>`, MSBuild reports `error MSB4126: The specified solution configuration is invalid` or silently fails to build the dependency — verify the two match before building.

- [ ] **Step 3: Build the exe**

Run the standard build. Expected: exit 0, exactly the six warning lines.

Expect `error LNK2019: unresolved external symbol` if a symbol lives in neither project — that means a file was dropped from both lists. Cross-check the two `ClCompile` lists sum to 134.

Expect `warning LNK4098` or LTCG mismatch warnings if `WholeProgramOptimization` differs between the projects — both must be `true` in Release.

- [ ] **Step 4: Verify the file counts**

```bash
echo "core: $(grep -c '<ClCompile Include=' aae/aae_core.vcxproj)"
echo "exe:  $(grep -c '<ClCompile Include=' aae/aae.vcxproj)"
```

Expected: `core: 86`, `exe: 48`. Sum must be 134.

- [ ] **Step 5: Commit**

```bash
git add aae/aae.vcxproj
git commit -m "build: link aae.exe against aae_core.lib

aae.vcxproj drops the 86 core translation units and gains a project
reference. Core->OSD calls resolve at link time through osdepend.h, which
is exactly the contract Phase 1 established."
```

---

## Task 7: Verification

- [ ] **Step 1: The negative test — prove the include restriction works**

Temporarily add to a core-library file, `aae/aae/drivers/asteroid.cpp`, as its first include:

```c
#include "framework.h"
```

Rebuild. Expected: **`fatal error C1083: Cannot open include file: 'framework.h': No such file or directory`**.

It must be a *file-not-found* error, not a `_WINDOWS_` guard trip. File-not-found proves the restricted include path is doing the work; a guard trip would mean the directory is still reachable and only the guard caught it.

Revert the line and confirm `git diff` is clean.

- [ ] **Step 2: Full rebuild**

Run the build. Expected: exit 0, exactly the six warning lines.

- [ ] **Step 3: Confirm the guard inventory**

```bash
grep -rn "_WINDOWS_\|__XAUDIO2_INCLUDED__" --include=*.cpp --include=*.h aae/ | grep -B1 error
```

Expected: live guards in `memory.cpp`, `cpu_6502.cpp`, `centiped.cpp`, `SegaG80vid.cpp`, `aae_avg.cpp`, `mixer.h`, and now `invaders.cpp`. `acommon.cpp`'s stays parked (it includes `framework.h` directly).

- [ ] **Step 4: Runtime — audio is the risk here**

The fresh build lands in `aae/x64/Release/aae.exe`, **not** the `x64/Release/aae.exe` beside the ROMs. Copy it across under a test name rather than overwriting the tracked binary:

```bash
cp aae/x64/Release/aae.exe x64/Release/aae_phase2_test.exe
```

Then from `x64/Release/`:

```bash
./aae_phase2_test.exe asteroid
```

Tasks 1-4 rewrote every sample and stream path, so **audio must be checked by ear, not just by log**:
- `asteroid` — vector rendering; thrust, fire, explosion, saucer sounds; all 14 samples load
- `pacman` — raster path and colours
- `bzone` — engine idle, shot, explosion; sample-heavy vector

Also verify in-game: open the menu, rebind a key through **KEY CONFIG (GLOBAL)**, and confirm volume control still works (it exercises `SetMasterVolume` through the scrubbed interface).

Delete `x64/Release/aae_phase2_test.exe` when done.

- [ ] **Step 5: Commit any fixes**

If a verification step failed, fix it and commit. If everything passed, say so explicitly rather than making an empty commit.

---

## Done criteria

- [ ] `mixer.h` contains no `<xaudio2.h>` include and no `HRESULT`/`BYTE`/`DWORD`/`WAVEFORMATEX`/`IXAudio2*` in live code
- [ ] The `_WINDOWS_` guard in `drivers/invaders.cpp` is **live and green**
- [ ] `aae_core.vcxproj` exists as a `StaticLibrary` with 86 `ClCompile` entries and no `system/window`, `aae/aae_video` or `aae/gui` on its include path
- [ ] `aae.vcxproj` has 48 `ClCompile` entries and a `ProjectReference` to `aae_core`
- [ ] Adding `#include "framework.h"` to a core file fails with **C1083 file-not-found**
- [ ] Build is exit 0 with exactly the six baseline warning lines
- [ ] `WIN7BUILD` is present in `Release|x64` of both projects
- [ ] asteroid, pacman and bzone run with correct audio, verified by ear

## Explicitly NOT in this plan

Moving files into `src/emu` / `src/osd` directories; CMake; `ISystemWindow` or any Linux/Teensy code; splitting `emu_vector_draw.cpp` (still GL-saturated — Phase 3's first file); replacing `allegro_message()` in `sndhrdwr/segag80snd.cpp`; any behaviour change beyond `HRESULT`→`bool`, which is diagnostics-preserving by construction.
