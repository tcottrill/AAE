# Phase 3b Implementation Plan — Linux toolchain, portable utilities, ALSA audio

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `aae_core` compile and run under g++ on Linux, prove it with a headless run whose vector counts match Windows exactly, and get the real `mixer.cpp` producing sound through a new ALSA backend on Linux and XAudio2 on Windows.

**Architecture:** Fix the small set of MSVC-isms g++ rejects (measured: five distinct causes across 14 files); lift `IAudioBackend` out of the Win32 backend's header into a neutral `audio_backend.h` with a per-platform `create_audio_backend()` factory; make `sys_log`/`path_helper`/`sys_fileio` portable with one implementation each rather than `_win32`/`_posix` pairs; rename `wintimer.h`'s colliding `timer_t`; add `alsa_backend.cpp` and a small `aae_audiotest` target that exercises the real mixer on both platforms.

**Tech Stack:** C++17. Windows: MSVC 2022 (v143), MSBuild **and** CMake. Linux: Ubuntu 26.04 under WSL2, g++ 15.2.0, CMake 4.2.3, ninja. **No test framework** — as in Phases 1–3a, the tests are the build, a link-level proof, a game count, and a runtime vector count compared across platforms.

**Spec:** `docs/superpowers/specs/2026-07-28-phase3b-linux-backends-design.md`

---

## Ground rules

**Parity is the goal (spec §0).** Nothing here is dropped; deferrals name a successor phase. A Linux build missing a feature is an intermediate state, never a resting place.

**Never remove the whole-archive link options.** `$<LINK_LIBRARY:WHOLE_ARCHIVE,aae_core>` in `CMakeLists.txt` and `/WHOLEARCHIVE:` in `aae/aae.vcxproj` are what keep self-registering drivers alive. Without them every game silently vanishes — this cost 83 games in Phase 2 and went unnoticed because the smoke tests only checked "builds green" and "the named game boots". **Always check the game count, not just that the build is green.**

**Two build systems must stay in lockstep.** Any file added to `aae/aae.vcxproj` or `aae/aae_core.vcxproj` must also be added to `CMakeLists.txt`, and vice versa. Source lists are explicit on purpose; never introduce `file(GLOB)`.

### Running commands in WSL — read this before Task 1

PowerShell mangles `$` and quotes before `wsl.exe` sees them. `wsl -d Ubuntu -- bash -c "for c in ...; do echo \$c; done"` silently produces empty variables — this was hit twice while measuring for this plan. Also, **`/tmp` does not survive between `wsl.exe` invocations**: the distro stops when its last process exits, so a script that writes `/tmp/out` and a later invocation that reads it will find nothing.

Both problems go away with the same habit: **put multi-line shell work in a `.sh` file and invoke that file**, and keep any output you need to read afterwards under `/mnt/c/...`.

```bash
wsl -d Ubuntu -- bash /mnt/c/Source2026/AAE_publish/scripts/linux/build.sh
```

Single short commands are fine inline:

```bash
wsl -d Ubuntu -- g++ --version
```

### Verification commands used throughout

Windows, MSBuild (the primary, authoritative build):

```bash
"/c/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" aae/aae.vcxproj -t:Rebuild -p:Configuration=Release -p:Platform=x64 -v:q -nologo
```

Expected: exit 0, **exactly six warning lines**.

Windows, CMake:

```bash
cmake -S . -B build-cmake -A x64 && cmake --build build-cmake --config Release
```

Game count (the check that matters most — run it after anything touching linking or driver files):

```bash
./x64/Release/aae.exe -listallgames | wc -l
```

Expected: **132**.

Linux (after Task 7 creates it):

```bash
wsl -d Ubuntu -- bash /mnt/c/Source2026/AAE_publish/scripts/linux/build.sh
```

---

## File structure

| File | Action | Responsibility |
|---|---|---|
| `aae/aae/fileio/aae_fileio.h` | modify | drop stray `const`; widen `RomModule::disposable` |
| `aae/aae/aae_mame_driver.h` | modify | drop stray `const` |
| `aae/aae/memory.h` | modify | `REGIONFLAG_*` become unsigned literals |
| `aae/aae/cpu_code/cpu_6502.cpp` | modify | `sprintf_s` → `snprintf` |
| `aae/aae/cpu_code/cpu_control.cpp` | modify | drop `static` on two exported handlers |
| `aae/aae/drivers/dkong.cpp`, `drivers/bwidow.cpp` | modify | drop `static` on exported handlers |
| `aae/aae/vidhrdwr/rallyx_vid.cpp` | modify | drop `static` on exported `tmpbitmap1` |
| `aae/aae/sndhrdwr/tms5220.cpp` | modify | k-tables unsigned + sign-extend at read sites |
| `aae/system/input/joystick.h` | modify | remove all Win32 from the header |
| `aae/system/input/Joystick.cpp` | modify | absorb the Win32 includes; add `static_assert` |
| `aae/aae/os_input.cpp` | modify | add `_WINDOWS_` leak guard |
| `aae/system/audio/audio_backend.h` | **create** | neutral `IAudioBackend` + `create_audio_backend()` |
| `aae/system/audio/xaudio2_backend.h/.cpp` | modify | concrete Win32 class only; defines the factory |
| `aae/system/audio/alsa_backend.h/.cpp` | **create** | ALSA `IAudioBackend` + software voice mixer |
| `aae/system/audio/voice_mixer.h/.cpp` | **create** | ALSA-free software voice mixing, reusable by Teensy |
| `aae/system/audio/audio_3d_null.cpp` | **create** | Linux stub for `audio_3d.h`, logs the gap |
| `aae/system/audio/mixer.cpp` | modify | use the factory; add xaudio2 leak guard |
| `aae/system/util/sys_timer.h` | **create** | neutral timer contract (`AaeTimer`) |
| `aae/system/util/wintimer.cpp` | modify | implement `sys_timer.h` |
| `aae/system/util/linux/posix_timer.cpp` | **create** | `clock_gettime` implementation |
| `aae/system/util/sys_log.cpp` | modify | ANSI colour instead of console attributes |
| `aae/system/util/path_helper.cpp` | modify | one `exe_path()` primitive + `std::filesystem` |
| `aae/system/util/sys_fileio.cpp` | modify | `std::filesystem`; delegate exe-path |
| `aae/audiotest/audiotest_main.cpp` | **create** | plays a sample through the real mixer |
| `scripts/linux/build.sh` | **create** | one-command Linux configure + build |
| `CMakeLists.txt` | modify | scoped `find_package`, new files, new target |
| `aae/aae.vcxproj`, `aae/aae_core.vcxproj` | modify | new files where Windows-relevant |

---

# Milestone A — `aae_core` and `aae_headless` on Linux

Tasks 1–8. Each of Tasks 1–6 is Windows-safe and must leave the Windows build green.

---

### Task 1: Install Linux dev packages and add the build script

**Files:**
- Create: `scripts/linux/build.sh`

- [ ] **Step 1: Install the dev packages** (needs the user's `sudo` password — ask them to run it if it prompts)

```bash
wsl -d Ubuntu -- sudo apt update
```
```bash
wsl -d Ubuntu -- sudo apt install -y pkg-config libasound2-dev libx11-dev libgl1-mesa-dev libglu1-mesa-dev libudev-dev
```

- [ ] **Step 2: Verify they landed**

```bash
wsl -d Ubuntu -- pkg-config --modversion alsa
```

Expected: a version number such as `1.2.14`. If it says `MISSING` or errors, stop — Task 13 cannot proceed without it.

- [ ] **Step 3: Create the build script**

Create `scripts/linux/build.sh`:

```bash
#!/bin/bash
# Configure and build the Linux targets. Run from anywhere:
#   wsl -d Ubuntu -- bash /mnt/c/Source2026/AAE_publish/scripts/linux/build.sh [target]
# Default target builds everything Linux currently supports.
set -e
cd "$(dirname "$0")/../.." || exit 1

TARGET="${1:-aae_headless}"

cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux --target "$TARGET"

echo "--- built: $TARGET ---"
ls -l "build-linux/$TARGET" 2>/dev/null || true
```

- [ ] **Step 4: Make it executable and commit**

```bash
wsl -d Ubuntu -- chmod +x /mnt/c/Source2026/AAE_publish/scripts/linux/build.sh
git add scripts/linux/build.sh
git commit -m "build(linux): add a one-command configure+build script"
```

Do **not** run the script yet — `CMakeLists.txt` cannot configure on Linux until Task 7.

---

### Task 2: Remove the two stray `const` keywords

This single change takes `aae_core` from 13/88 to 74/88 files compiling under g++. Both headers reach nearly every core file, which is why two words caused 146 errors.

`const struct Foo { ... };` declares no object, so the `const` qualifies nothing and removing it is semantically inert. The instances get their constness where they are actually declared — e.g. `ART_START` expands to `static const struct artworks name[] = {`.

**Files:**
- Modify: `aae/aae/fileio/aae_fileio.h:44`
- Modify: `aae/aae/aae_mame_driver.h:80`

- [ ] **Step 1: Edit `aae/aae/fileio/aae_fileio.h`**

Change line 44 from:

```c
const struct RomModule
```

to:

```c
struct RomModule
```

- [ ] **Step 2: Edit `aae/aae/aae_mame_driver.h`**

Change line 80 from:

```c
const struct artworks
```

to:

```c
struct artworks
```

- [ ] **Step 3: Verify Windows is still green**

```bash
"/c/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" aae/aae.vcxproj -t:Rebuild -p:Configuration=Release -p:Platform=x64 -v:q -nologo
```

Expected: exit 0, exactly six warning lines — the same six as before this task.

- [ ] **Step 4: Verify the game count did not move**

```bash
./x64/Release/aae.exe -listallgames | wc -l
```

Expected: `132`.

- [ ] **Step 5: Commit**

```bash
git add aae/aae/fileio/aae_fileio.h aae/aae/aae_mame_driver.h
git commit -m "fix(portability): drop stray const on two struct definitions

const struct Foo {...}; qualifies no declarator. MSVC accepts it; g++
rejects it outright, and because both headers reach nearly every core
file these two words produced 146 errors across 73 of the 88 aae_core
translation units. Instances still get their constness at the use sites
(ART_START/ROM_START expand to 'static const struct ...')."
```

---

### Task 3: `sprintf_s` → `snprintf` in the 6502 disassembler

**Files:**
- Modify: `aae/aae/cpu_code/cpu_6502.cpp:2551,2559,2563,2572,2576,2580,2586`

`sprintf_s(buf, sizeof(buf), fmt, ...)` and `snprintf(buf, sizeof(buf), fmt, ...)` have the same argument order and the same truncation-safe intent, so this is a rename. `snprintf` truncates rather than invoking the invalid-parameter handler; for a disassembly string that is the better behaviour anyway.

- [ ] **Step 1: Replace all seven calls**

In `aae/aae/cpu_code/cpu_6502.cpp`, replace every occurrence of `sprintf_s(` with `snprintf(`. There are exactly seven, all within the disassembler, all of the form:

```c
sprintf_s(buffer, sizeof(buffer), "%02X       %-4s", opcode, mnemonics[opcode]);
```

becoming:

```c
snprintf(buffer, sizeof(buffer), "%02X       %-4s", opcode, mnemonics[opcode]);
```

- [ ] **Step 2: Confirm `<cstdio>` is included**

`snprintf` needs `<cstdio>`. Check the top of the file; if absent, add `#include <cstdio>` alongside the existing includes.

- [ ] **Step 3: Verify there are none left**

```bash
grep -c "sprintf_s" aae/aae/cpu_code/cpu_6502.cpp
```

Expected: `0`.

- [ ] **Step 4: Windows build green**

```bash
"/c/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" aae/aae.vcxproj -t:Rebuild -p:Configuration=Release -p:Platform=x64 -v:q -nologo
```

Expected: exit 0, six warnings.

- [ ] **Step 5: Commit**

```bash
git add aae/aae/cpu_code/cpu_6502.cpp
git commit -m "fix(portability): snprintf instead of sprintf_s in the 6502 disassembler

Same argument order, same truncation-safe intent, and portable. The
only MSVC-only library call left in aae_core's 88 translation units."
```

---

### Task 4: Fix the `REGIONFLAG_DISPOSE` narrowing

`REGIONFLAG_DISPOSE` is `0x80000000`, which does not fit in `int`. `RomModule::disposable` is `int`, so `ROM_REGION(0xa000, REGION_GFX2, REGIONFLAG_DISPOSE)` narrows in a braced initialiser: an error under g++, and a silent conversion to `INT_MIN` under MSVC.

The flags are a bitmask, so unsigned is the correct type. Widening the field and making the literals explicitly unsigned fixes both compilers and removes the sign-dependence.

**Files:**
- Modify: `aae/aae/memory.h:71-73`
- Modify: `aae/aae/fileio/aae_fileio.h` (the `disposable` field)

- [ ] **Step 1: Make the flag literals unsigned**

In `aae/aae/memory.h`, change:

```c
#define REGIONFLAG_MASK			0xf8000000
#define REGIONFLAG_DISPOSE		0x80000000           /* Dispose of this region when done */
#define REGIONFLAG_SOUNDONLY	0x40000000           /* load only if sound emulation is turned on */
```

to:

```c
/* Bitmask flags packed into RomModule::disposable. 0x80000000 does not fit
   in a signed int - the u suffixes and the unsigned field they feed keep
   this well-defined instead of relying on MSVC's silent conversion. */
#define REGIONFLAG_MASK			0xf8000000u
#define REGIONFLAG_DISPOSE		0x80000000u          /* Dispose of this region when done */
#define REGIONFLAG_SOUNDONLY	0x40000000u          /* load only if sound emulation is turned on */
```

- [ ] **Step 2: Widen the field**

In `aae/aae/fileio/aae_fileio.h`, inside `struct RomModule`, change:

```c
	int disposable;
```

to:

```c
	unsigned int disposable;   /* REGIONFLAG_* bitmask; unsigned so 0x80000000 fits */
```

- [ ] **Step 3: Check for signed comparisons against the field**

```bash
grep -rn "disposable" aae/aae/ --include=*.cpp --include=*.h | grep -v "disposableFlag"
```

Inspect each hit. Any comparison of the form `< 0` or assignment to a signed local needs adjusting; a plain `&` bitmask test is fine as-is. Fix anything the compiler later flags.

- [ ] **Step 4: Windows build green and game count intact**

```bash
"/c/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" aae/aae.vcxproj -t:Rebuild -p:Configuration=Release -p:Platform=x64 -v:q -nologo
```
```bash
./x64/Release/aae.exe -listallgames | wc -l
```

Expected: exit 0 with six warnings; `132`.

- [ ] **Step 5: Run one game that uses the flag, to prove ROM regions still load**

```bash
./x64/Release/aae.exe gaplus
```

Expected: the game boots and its graphics are correct. `gaplus` is chosen because `ROM_REGION(0xa000, REGION_GFX2, REGIONFLAG_DISPOSE)` at `drivers/gaplus.cpp:953` is one of the sites that narrows — if disposal broke, its GFX2 region is the visible casualty.

- [ ] **Step 6: Commit**

```bash
git add aae/aae/memory.h aae/aae/fileio/aae_fileio.h
git commit -m "fix(portability): REGIONFLAG_* are unsigned, as is the field holding them

REGIONFLAG_DISPOSE is 0x80000000, which does not fit in the int
RomModule::disposable used to be. In a braced initialiser g++ rejects
the narrowing outright; MSVC silently stored INT_MIN. The flags are a
bitmask, so unsigned is the correct type on both."
```

---

### Task 5: Fix four extern-vs-static mismatches

In each case a header declares the symbol `extern` (implicitly, by declaring it at namespace scope) and the `.cpp` defines it `static`. MSVC accepts this; g++ rejects it as `-fpermissive`. These are genuine defects: the header advertises a symbol with external linkage that the definition then gives internal linkage, so any caller reaching it through the header is relying on something the language does not guarantee.

The fix is to drop `static` from the definition, matching the header that already exports it. Do **not** instead remove the header declaration — these are called across translation units.

**Files:**
- Modify: `aae/aae/cpu_code/cpu_control.cpp:1056` (`watchdog_reset_r`)
- Modify: `aae/aae/drivers/dkong.cpp:77` (`interrupt_enable_w`)
- Modify: `aae/aae/drivers/bwidow.cpp:129` (`avgdvg_reset_w`)
- Modify: `aae/aae/vidhrdwr/rallyx_vid.cpp:28` (`tmpbitmap1`)

> **Note:** `bwidow.cpp` has uncommitted local modifications in the working tree. Check `git diff aae/aae/drivers/bwidow.cpp` before editing so this change lands cleanly on top of that work rather than conflicting with it.

- [ ] **Step 1: `cpu_control.cpp:1056`**

The definition is written through the `READ_HANDLER` macro (`cpu_control.h:113`). Remove the leading `static` so it matches the declaration at `cpu_control.h:246`. The line reads roughly:

```c
static READ_HANDLER(watchdog_reset_r)
```

becoming:

```c
READ_HANDLER(watchdog_reset_r)
```

- [ ] **Step 2: `dkong.cpp:77`**

Same treatment for `interrupt_enable_w`, declared at `cpu_control.h:240`:

```c
static WRITE_HANDLER(interrupt_enable_w)
```

becoming:

```c
WRITE_HANDLER(interrupt_enable_w)
```

- [ ] **Step 3: `bwidow.cpp:129`**

Same for `avgdvg_reset_w`, declared at `aae_avg.h:118`:

```c
static WRITE_HANDLER(avgdvg_reset_w)
```

becoming:

```c
WRITE_HANDLER(avgdvg_reset_w)
```

- [ ] **Step 4: `rallyx_vid.cpp:28`**

`tmpbitmap1` is declared at `old_mame_raster.h:48` and defined `static` here. Remove the `static`:

```c
static struct osd_bitmap* tmpbitmap1;
```

becoming:

```c
struct osd_bitmap* tmpbitmap1;
```

**Careful:** `old_mame_raster.cpp` also defines `tmpbitmap1` (it appears in that file's public-globals block). If both definitions survive, the link fails with a duplicate symbol. Check first:

```bash
grep -n "tmpbitmap1" aae/aae/vidhrdwr/old_mame_raster.cpp aae/aae/vidhrdwr/rallyx_vid.cpp aae/aae/vidhrdwr/old_mame_raster.h
```

If `old_mame_raster.cpp` already defines it, then `rallyx_vid.cpp`'s copy is a *shadowing* definition that has been silently diverging — in that case delete the definition in `rallyx_vid.cpp` entirely rather than un-`static`ing it, so the file uses the shared one. Report which of the two cases applied.

- [ ] **Step 5: Windows build green and game count intact**

```bash
"/c/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" aae/aae.vcxproj -t:Rebuild -p:Configuration=Release -p:Platform=x64 -v:q -nologo
```
```bash
./x64/Release/aae.exe -listallgames | wc -l
```

Expected: exit 0, six warnings, `132`.

- [ ] **Step 6: Run the affected games**

```bash
./x64/Release/aae.exe dkong
```
```bash
./x64/Release/aae.exe bwidow
```
```bash
./x64/Release/aae.exe rallyx
```

Expected: all three boot and play. `rallyx` matters most — if Step 4 hit the duplicate-definition case, a wrong choice there shows up as corrupted or missing background graphics.

- [ ] **Step 7: Commit**

```bash
git add aae/aae/cpu_code/cpu_control.cpp aae/aae/drivers/dkong.cpp aae/aae/drivers/bwidow.cpp aae/aae/vidhrdwr/rallyx_vid.cpp
git commit -m "fix: four symbols declared extern in a header but defined static

Headers export watchdog_reset_r, interrupt_enable_w, avgdvg_reset_w and
tmpbitmap1; the definitions gave them internal linkage. MSVC accepts the
contradiction, g++ rejects it. Callers in other translation units were
relying on something the language does not guarantee."
```

---

### Task 6: Sign-correct the TMS5220 LPC coefficient tables

The k1–k10 tables are `const short` initialised with datasheet hex like `0x82C0`, which is `-32064` as a 16-bit signed value. MSVC truncates silently and gets the intended negative number; g++ rejects the narrowing (79 errors, all in this one file).

**The bit patterns must not change.** These feed the LPC lattice filter through `new_k[]` (an `int` array), so the values must sign-extend to negative integers exactly as they do on Windows. Making the tables `unsigned short` keeps the literals byte-identical to the datasheet and moves the signedness to the 14 read sites, where it is explicit and checkable — better than 79 individual casts scattered through the data.

**Files:**
- Modify: `aae/aae/sndhrdwr/tms5220.cpp` — declarations at lines 79, 85, 91, and the k4–k10 declarations following; read sites at 546–549 and 558–567

- [ ] **Step 1: Change the ten k-table declarations to `unsigned short`**

Each of `k1table` through `k10table` currently reads:

```c
const short k1table[0x20] = {
```

Change every one to:

```c
const unsigned short k1table[0x20] = {
```

Leave every literal untouched. `energytable` and `pitchtable` are already `const unsigned short` — do not change them.

- [ ] **Step 2: Sign-extend at the read sites**

There are 14 reads, four in the 4-coefficient frame path (lines ~546–549) and ten in the 10-coefficient path (lines ~558–567). Each becomes an explicit narrowing to signed 16-bit:

```c
		new_k[0] = (int16_t)k1table[extract_bits(5)];
		new_k[1] = (int16_t)k2table[extract_bits(5)];
		new_k[2] = (int16_t)k3table[extract_bits(4)];
		new_k[3] = (int16_t)k4table[extract_bits(4)];
```

and in the 10-coefficient path:

```c
	new_k[0] = (int16_t)k1table[extract_bits(5)];
	new_k[1] = (int16_t)k2table[extract_bits(5)];
	new_k[2] = (int16_t)k3table[extract_bits(4)];
	new_k[3] = (int16_t)k4table[extract_bits(4)];
	new_k[4] = (int16_t)k5table[extract_bits(4)];
	new_k[5] = (int16_t)k6table[extract_bits(4)];
	new_k[6] = (int16_t)k7table[extract_bits(4)];
	new_k[7] = (int16_t)k8table[extract_bits(3)];
	new_k[8] = (int16_t)k9table[extract_bits(3)];
	new_k[9] = (int16_t)k10table[extract_bits(3)];
```

- [ ] **Step 3: Add `<cstdint>` if absent**

`int16_t` needs it. Check the includes at the top of `tms5220.cpp` and add `#include <cstdint>` if it is not already there.

- [ ] **Step 4: Lock the intent with static assertions**

Immediately after the `k10table` definition, add:

```c
// The k tables hold signed 16-bit LPC coefficients written as unsigned hex
// straight from the TMS5220 datasheet. They are stored unsigned so the
// literals need no narrowing, and sign-extended at every read site above.
// These assertions pin the conversion: if the table type or a literal is
// ever changed, this fails at compile time rather than quietly detuning
// speech synthesis.
static_assert((int16_t)k1table[0]  == -32064, "k1table[0] must be 0x82C0 as signed 16-bit");
static_assert((int16_t)k1table[23] ==      0, "k1table[23] must be 0x0000");
static_assert((int16_t)k1table[31] ==  27968, "k1table[31] must be 0x6D40 as signed 16-bit");
```

- [ ] **Step 5: Verify no narrowing errors remain in this file**

```bash
grep -c "const short k" aae/aae/sndhrdwr/tms5220.cpp
```

Expected: `0`.

- [ ] **Step 6: Windows build green**

```bash
"/c/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" aae/aae.vcxproj -t:Rebuild -p:Configuration=Release -p:Platform=x64 -v:q -nologo
```

Expected: exit 0, six warnings. If the `static_assert`s fail, the values were mis-transcribed — do not adjust the assertions to match, re-check the table.

- [ ] **Step 7: Verify speech by ear**

```bash
./x64/Release/aae.exe bzone
```

Expected: Battlezone's speech ("player one", the enemy callouts) sounds exactly as before. This is the only real check that the coefficients still mean what they used to — a build that compiles proves nothing about the filter.

- [ ] **Step 8: Commit**

```bash
git add aae/aae/sndhrdwr/tms5220.cpp
git commit -m "fix(portability): TMS5220 k-tables unsigned, sign-extended at use

The LPC coefficient tables are datasheet hex (0x82C0 = -32064) stored in
const short. MSVC truncates silently; g++ rejects all 79 initialisers.
Storing them unsigned keeps the literals byte-identical to the datasheet
and makes the signedness explicit at the 14 read sites, with static
asserts pinning the conversion so a future edit cannot quietly detune
speech synthesis."
```

---

### Task 7: Neutralise `joystick.h` and add the leak guard to `os_input.cpp`

`os_input.cpp` lives in `aae_core` but does not compile OSD-free, because `joystick.h` opens with `<windows.h>` and `<Xinput.h>`. The header's entire Win32 surface is two things: a `WORD` parameter, and three combo macros written over `XINPUT_GAMEPAD_*`. Its other ~500 lines are Allegro-compatible structs with no Win32 types.

**Files:**
- Modify: `aae/system/input/joystick.h:264-265, 447-449, 454`
- Modify: `aae/system/input/Joystick.cpp`
- Modify: `aae/aae/os_input.cpp`

- [ ] **Step 1: Remove the Windows includes from the header**

In `aae/system/input/joystick.h`, delete lines 264–265:

```c
#include <windows.h>
#include <Xinput.h>
```

and replace them with:

```c
#include <cstdint>
```

- [ ] **Step 2: Replace the XInput-derived combo masks with neutral constants**

Replace lines 442–454 (the combo block) with:

```c
//------------------------------------------------------------------------------
// Button Combo Support
//
// Neutral gamepad button bits. The values match XINPUT_GAMEPAD_* exactly, so
// the Win32 backend passes them straight through; Joystick.cpp static_asserts
// that they still agree. A Linux evdev backend maps its own button codes onto
// these same bits.
//------------------------------------------------------------------------------

#define AAE_JOYBTN_DPAD_UP        0x0001
#define AAE_JOYBTN_DPAD_DOWN      0x0002
#define AAE_JOYBTN_DPAD_LEFT      0x0004
#define AAE_JOYBTN_DPAD_RIGHT     0x0008
#define AAE_JOYBTN_START          0x0010
#define AAE_JOYBTN_BACK           0x0020
#define AAE_JOYBTN_LEFT_THUMB     0x0040
#define AAE_JOYBTN_RIGHT_THUMB    0x0080
#define AAE_JOYBTN_LEFT_SHOULDER  0x0100
#define AAE_JOYBTN_RIGHT_SHOULDER 0x0200
#define AAE_JOYBTN_A              0x1000
#define AAE_JOYBTN_B              0x2000
#define AAE_JOYBTN_X              0x4000
#define AAE_JOYBTN_Y              0x8000

#define JOY_MAX_COMBOS 16  // Max number of distinct combo masks tracked simultaneously

#define JOY_COMBO_PAUSE   (AAE_JOYBTN_START      | AAE_JOYBTN_BACK)   // Start + Back  : pause/unpause
#define JOY_COMBO_ESC     (AAE_JOYBTN_LEFT_THUMB | AAE_JOYBTN_BACK)   // LS + Back     : ESC / return to GUI
#define JOY_COMBO_MENU    (AAE_JOYBTN_LEFT_THUMB | AAE_JOYBTN_START)  // LS + Start    : open/close menu

// Edge-triggered combo check: returns true once per press (not every frame while held).
// All bits in buttonMask must be simultaneously held to trigger.
// Always returns false on the WinMM fallback path.
bool joystick_check_combo(int player, uint16_t buttonMask);
```

- [ ] **Step 3: Absorb the Windows includes into the implementation**

At the top of `aae/system/input/Joystick.cpp`, before its `#include "joystick.h"`, ensure these are present:

```c
#include <windows.h>
#include <Xinput.h>
```

Change the definition of `joystick_check_combo` in that file so its signature matches the header (`uint16_t` rather than `WORD`).

- [ ] **Step 4: Pin the constants to XInput's**

Immediately after the includes in `aae/system/input/Joystick.cpp`, add:

```c
// joystick.h defines AAE_JOYBTN_* so the header stays free of <Xinput.h>.
// The values are XInput's, so this backend passes them through untouched.
// If a future SDK ever renumbers them, this fails the build rather than
// silently breaking every controller combo.
static_assert(AAE_JOYBTN_START       == XINPUT_GAMEPAD_START,       "AAE_JOYBTN_START drifted from XInput");
static_assert(AAE_JOYBTN_BACK        == XINPUT_GAMEPAD_BACK,        "AAE_JOYBTN_BACK drifted from XInput");
static_assert(AAE_JOYBTN_LEFT_THUMB  == XINPUT_GAMEPAD_LEFT_THUMB,  "AAE_JOYBTN_LEFT_THUMB drifted from XInput");
static_assert(AAE_JOYBTN_RIGHT_THUMB == XINPUT_GAMEPAD_RIGHT_THUMB, "AAE_JOYBTN_RIGHT_THUMB drifted from XInput");
static_assert(AAE_JOYBTN_A           == XINPUT_GAMEPAD_A,           "AAE_JOYBTN_A drifted from XInput");
static_assert(AAE_JOYBTN_B           == XINPUT_GAMEPAD_B,           "AAE_JOYBTN_B drifted from XInput");
static_assert(AAE_JOYBTN_X           == XINPUT_GAMEPAD_X,           "AAE_JOYBTN_X drifted from XInput");
static_assert(AAE_JOYBTN_Y           == XINPUT_GAMEPAD_Y,           "AAE_JOYBTN_Y drifted from XInput");
```

- [ ] **Step 5: Check for other Win32 types left in the header**

```bash
grep -nE "\b(WORD|DWORD|BOOL|HWND|HANDLE|UINT|LPARAM|WPARAM|GUID|XINPUT_)" aae/system/input/joystick.h
```

Expected: matches only inside comments. Any match in a declaration must be converted to a fixed-width type before proceeding.

- [ ] **Step 6: Add the leak guard to `os_input.cpp`**

At the top of `aae/aae/os_input.cpp`, **after** all its `#include` lines, add:

```c
// Build-time boundary test (the Phase 1 idiom). os_input.cpp is an aae_core
// file: nothing it includes may drag in windows.h. This is exactly the leak
// that went unnoticed until Phase 3b - joystick.h included <windows.h> and
// the include-directory exclusion could not catch it, because system/input
// is legitimately on the core's path.
#ifdef _WINDOWS_
#error "os_input.cpp is core code and must not see windows.h - check your includes"
#endif
```

- [ ] **Step 7: Prove the guard actually fires**

Phase 1 shipped three `#error` guards that could never trigger. Do not repeat that. Temporarily add `#include <windows.h>` as the first include of `os_input.cpp`, build, and confirm the `#error` fires:

```bash
"/c/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" aae/aae_core.vcxproj -p:Configuration=Release -p:Platform=x64 -v:q -nologo
```

Expected: build FAILS with "os_input.cpp is core code and must not see windows.h". Then remove the temporary include and confirm the build passes again. Report both outcomes.

- [ ] **Step 8: Windows build green, game count intact, controller still works**

```bash
"/c/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" aae/aae.vcxproj -t:Rebuild -p:Configuration=Release -p:Platform=x64 -v:q -nologo
```
```bash
./x64/Release/aae.exe -listallgames | wc -l
```

Expected: exit 0, six warnings, `132`.

Then, with a gamepad connected, run any game and confirm LS+Start opens the menu, LS+Back exits, and Start+Back pauses. These are the three combos this task rewrote; nothing else exercises them.

- [ ] **Step 9: Commit**

```bash
git add aae/system/input/joystick.h aae/system/input/Joystick.cpp aae/aae/os_input.cpp
git commit -m "refactor(input): joystick.h carries no Win32, os_input.cpp guards against it

The header's entire Windows surface was a WORD parameter and three combo
macros over XINPUT_GAMEPAD_*; its other 500 lines are neutral already.
Neutral AAE_JOYBTN_* constants replace them, static_asserted in
Joystick.cpp against XInput's values so a future SDK change fails the
build instead of breaking every combo.

Adds the _WINDOWS_ leak guard to os_input.cpp - the check whose absence
let this survive Phase 2. The include-directory exclusion could never
have caught it, since system/input is legitimately on the core's path."
```

---

### Task 8: Scope the `find_package` calls and build `aae_core` on Linux

`CMakeLists.txt`'s `UNIX` branch calls `find_package(OpenGL REQUIRED)`, `find_package(ALSA REQUIRED)` and `find_package(X11 REQUIRED)` at project scope. A box without `libx11-dev` therefore cannot configure at all — and so cannot build `aae_headless`, which needs none of the three. Since this phase's deliverables are exactly the targets that do not need X11 or GL, that must be fixed before anything Linux can be built.

**Files:**
- Modify: `CMakeLists.txt:286-322`

- [ ] **Step 1: Defer the dependency lookups**

In the `elseif(UNIX AND NOT APPLE)` branch, replace:

```cmake
    set(AAE_PLATFORM_DEFINES GLEW_STATIC)
    find_package(OpenGL REQUIRED)
    find_package(ALSA REQUIRED)
    find_package(X11 REQUIRED)
    set(AAE_PLATFORM_LIBS OpenGL::GL ALSA::ALSA X11::X11 pthread dl)
```

with:

```cmake
    set(AAE_PLATFORM_DEFINES GLEW_STATIC)

    # Looked up lazily, per target, NOT at project scope. The Phase 3b
    # deliverables (aae_headless, aae_audiotest) need none of OpenGL or X11,
    # and aae_headless needs no ALSA either - so requiring all three up front
    # would make a box without libx11-dev unable to configure, and therefore
    # unable to build the very targets that do not use it. Each find_package
    # below is guarded by the target that actually needs it.
    find_package(ALSA)
    find_package(OpenGL)
    find_package(X11)

    set(AAE_PLATFORM_LIBS pthread dl)
    if(OpenGL_FOUND)
        list(APPEND AAE_PLATFORM_LIBS OpenGL::GL)
    endif()
    if(ALSA_FOUND)
        list(APPEND AAE_PLATFORM_LIBS ALSA::ALSA)
    endif()
    if(X11_FOUND)
        list(APPEND AAE_PLATFORM_LIBS X11::X11)
    endif()
```

- [ ] **Step 2: Guard the `aae` target so a missing backend does not break configuration**

The `aae` executable cannot link on Linux until Phase 3c supplies `linux_window.cpp` and `evdev_input.cpp`. Configuring it is fine; *building* it is expected to fail. Immediately before `add_executable(aae ...)`, add:

```cmake
# On Linux the aae executable cannot link until Phase 3c provides
# linux_window.cpp and evdev_input.cpp (see the EXISTS check above). It is
# still declared, so the target exists and its sources are checked - but it
# is excluded from the default build so `cmake --build build-linux` does not
# fail on a target this phase knowingly does not deliver. Build it
# explicitly with --target aae once 3c lands.
set(AAE_EXCLUDE_FROM_ALL "")
if(UNIX AND _aae_missing_backends)
    set(AAE_EXCLUDE_FROM_ALL EXCLUDE_FROM_ALL)
endif()
```

and change `add_executable(aae ${AAE_COMMON_SOURCES} ${AAE_PLATFORM_SOURCES})` to:

```cmake
add_executable(aae ${AAE_EXCLUDE_FROM_ALL} ${AAE_COMMON_SOURCES} ${AAE_PLATFORM_SOURCES})
```

- [ ] **Step 3: Configure on Linux**

```bash
wsl -d Ubuntu -- bash /mnt/c/Source2026/AAE_publish/scripts/linux/build.sh aae_core
```

Expected: CMake configures (a warning about the two missing Phase 3c backends is correct and expected), then builds `libaae_core.a`.

If compilation errors appear, they are new findings — Tasks 2–7 cleared every error measured on 2026-07-29, but `-fsyntax-only` does not catch everything a real compile does (notably template instantiation and missing definitions). Fix them in this task and note each one in the commit message.

- [ ] **Step 4: Confirm the archive exists and holds all 88 objects**

```bash
wsl -d Ubuntu -- bash -c "cd /mnt/c/Source2026/AAE_publish && ar t build-linux/libaae_core.a | wc -l"
```

Expected: `88`.

- [ ] **Step 5: Verify Windows CMake still configures**

```bash
cmake -S . -B build-cmake -A x64 && cmake --build build-cmake --config Release
```

Expected: configures and builds all targets, as before.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt
git commit -m "build(cmake): scope the Linux dependency lookups to the targets that need them

find_package(OpenGL/ALSA/X11 REQUIRED) ran at project scope, so a box
without libx11-dev could not configure at all - and therefore could not
build aae_headless, which needs none of the three. Each is now optional
and applied per target.

Also excludes the aae executable from the default Linux build while its
window and input backends are missing, so 'cmake --build' does not fail
on a target Phase 3b knowingly does not deliver. Phase 3c removes this."
```

---

### Task 9: Build and run `aae_headless` on Linux, and compare vector counts

This is Milestone A's proof, and the first real parity check in the programme.

**Files:**
- Modify: `CMakeLists.txt` (only if `aae_headless` needs a Linux-specific tweak)

- [ ] **Step 1: Build it**

```bash
wsl -d Ubuntu -- bash /mnt/c/Source2026/AAE_publish/scripts/linux/build.sh aae_headless
```

Expected: `build-linux/aae_headless` is produced.

Likely fixes needed here, all legitimate parts of this task: `null_backends.cpp` and `headless_main.cpp` have never been compiled by g++, so expect missing includes that MSVC supplied transitively. Fix them in place.

- [ ] **Step 2: Record the Windows baseline**

```bash
cd x64/Release && ../../build-cmake/Release/aae_headless.exe asteroid 600
```
```bash
cd x64/Release && ../../build-cmake/Release/aae_headless.exe bzone 600
```

Expected: `89414` and `353693` respectively. If these differ from the values recorded in Phase 3a, stop and investigate — the baseline itself has moved, and Tasks 2–7 are the suspects.

- [ ] **Step 3: Run the same two on Linux**

`aae_headless` must run from `x64/Release/` so it finds `roms/`:

```bash
wsl -d Ubuntu -- bash -c "cd /mnt/c/Source2026/AAE_publish/x64/Release && ../../build-linux/aae_headless asteroid 600"
```
```bash
wsl -d Ubuntu -- bash -c "cd /mnt/c/Source2026/AAE_publish/x64/Release && ../../build-linux/aae_headless bzone 600"
```

Expected: **exactly** `89414` and `353693`.

- [ ] **Step 4: If the counts differ, investigate — do not paper over it**

A mismatch is a real emulation difference between MSVC and g++, and finding it here is the point of this milestone. Likely causes, in order of probability:

1. **Signed/unsigned or width differences** — `long` is 64-bit on Linux and 32-bit on Windows. Search the divergent driver's path for `long`.
2. **Uninitialised memory** read before write, where the two runtimes happen to differ.
3. **Struct padding or bitfield layout** in a hardware-register struct.
4. **Floating-point** differences in the vector generator (x87 vs SSE, or contraction) — try `-ffp-contract=off`.

Bisect by frame count: run with `1`, `10`, `100` frames and find the first frame at which the counts diverge, then trace that frame. Record the finding in the commit message whatever the outcome.

If the cause is genuinely a Linux-only defect, fix it. If it is a latent bug affecting both platforms, fix it and note that Windows behaviour changed.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "test(linux): aae_headless runs on Linux with vector counts matching Windows

asteroid 600 -> 89,414 and bzone 600 -> 353,693 on both MSVC and g++ 15.2.
Identical counts, not merely non-zero: they are the product of six CPU
cores, the timer system, the AVG vector generator and the memory
subsystem agreeing across two compilers and two ABIs. This is the first
real parity check in the port programme."
```

---

**Milestone A checkpoint.** Before continuing: Windows MSBuild green with six warnings, 132 games, and Linux `aae_headless` matching Windows exactly. Do not start Milestone B until all four hold.

---

# Milestone B — the real mixer, on both platforms

---

### Task 10: Extract `IAudioBackend` into a neutral header

`IAudioBackend` is declared at `xaudio2_backend.h:34`, in a file that includes `<xaudio2.h>` unconditionally at lines 19–23. So `mixer.cpp` cannot be parsed on Linux, and an ALSA backend would have to include the XAudio2 header to learn what it implements.

**Files:**
- Create: `aae/system/audio/audio_backend.h`
- Modify: `aae/system/audio/xaudio2_backend.h`
- Modify: `aae/system/audio/xaudio2_backend.cpp`
- Modify: `aae/system/audio/mixer.cpp:142,672`

- [ ] **Step 1: Create `aae/system/audio/audio_backend.h`**

Move the whole `IAudioBackend` class body across verbatim — do not retype the twenty method signatures, copy them, so no subtle mismatch creeps in.

```cpp
//==============================================================================
// audio_backend.h -- the platform-neutral audio output contract.
//
// This is what a backend implements. It deliberately does NOT live in
// xaudio2_backend.h, where it used to: that header includes <xaudio2.h>
// unconditionally, so the neutral contract could not be read on any platform
// but Windows, and mixer.cpp - which is entirely portable - could not even be
// parsed on Linux.
//
// Implementations:
//   xaudio2_backend.cpp  Windows
//   alsa_backend.cpp     Linux
//   (Teensy: later, per docs/superpowers/specs - a DAC-driven backend)
//
// Exactly ONE backend translation unit is linked into any given binary. Each
// defines its own layout for the opaque VoiceHandle below, so linking two at
// once would be an ODR violation - the build must never do it.
//==============================================================================
#pragma once

#include <cstdint>
#include <memory>

struct WaveFormat;    // defined in mixer.h
struct VoiceHandle;   // opaque; each backend defines its own layout

class IAudioBackend {
public:
    virtual ~IAudioBackend() = default;

    // ---- paste the twenty pure-virtual declarations from the old
    // ---- xaudio2_backend.h:36-104 here, unchanged.
};

//------------------------------------------------------------------------------
// Creates the backend for the platform this binary was built for. Defined in
// exactly one translation unit per platform (xaudio2_backend.cpp on Windows,
// alsa_backend.cpp on Linux). Returns nullptr if the audio device cannot be
// opened - callers must handle that, since a machine with no sound card is a
// legitimate configuration, not an error.
//------------------------------------------------------------------------------
std::unique_ptr<IAudioBackend> create_audio_backend();
```

- [ ] **Step 2: Reduce `xaudio2_backend.h` to the concrete class**

Delete the `IAudioBackend` class from it and add `#include "audio_backend.h"` near the top. Keep its `<xaudio2.h>` block and the `XAudio2Backend` class. The `struct WaveFormat;`/`struct VoiceHandle;` forward declarations move to `audio_backend.h`, so remove them here.

- [ ] **Step 3: Define the factory in `xaudio2_backend.cpp`**

At the end of the file:

```cpp
//------------------------------------------------------------------------------
// The Windows half of audio_backend.h's platform factory.
//------------------------------------------------------------------------------
std::unique_ptr<IAudioBackend> create_audio_backend()
{
    return std::make_unique<XAudio2Backend>();
}
```

- [ ] **Step 4: Make `mixer.cpp` platform-blind**

At `mixer.cpp:142`, change:

```cpp
#include "xaudio2_backend.h"
```

to:

```cpp
#include "audio_backend.h"
```

At `mixer.cpp:672`, change:

```cpp
	auto backend = std::make_unique<XAudio2Backend>();
```

to:

```cpp
	auto backend = create_audio_backend();
	if (!backend) {
		LOG_ERROR("No audio backend available - continuing without sound\n");
		return false;
	}
```

Check the surrounding function's return type and adjust that `return` to match it.

- [ ] **Step 5: Add the leak guard — and prove it fires**

First find the real include guard macro; do not guess it:

```bash
grep -m3 -n "#define.*XAUDIO2\|#ifndef" "packages/Microsoft.XAudio2.Redist.1.2.13/build/native/include/xaudio2redist.h"
```

Add to `mixer.cpp`, after its includes, using the macro you just read:

```cpp
// mixer.cpp is platform-neutral and must reach the audio device only through
// audio_backend.h. If an OS-specific audio header ever creeps back in, fail
// the build here rather than discovering it when the Linux build breaks.
#ifdef <the macro you found>
#error "mixer.cpp must not see xaudio2.h - go through IAudioBackend"
#endif
```

Then prove it: temporarily add `#include <xaudio2.h>` above the guard, build, confirm the `#error` fires, remove it, confirm the build passes. Report both. Phase 1 shipped three guards that could never fire because the macro was assumed — this step exists to prevent a fourth.

- [ ] **Step 6: Update both build systems**

Add `aae/system/audio/audio_backend.h` to `aae/aae.vcxproj` (as a `<ClInclude>`). Headers need no `CMakeLists.txt` entry, so no source-count assertion moves — **do not change the `88`/`47` numbers**.

- [ ] **Step 7: Windows build green, game count intact, audio still works**

```bash
"/c/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" aae/aae.vcxproj -t:Rebuild -p:Configuration=Release -p:Platform=x64 -v:q -nologo
```
```bash
./x64/Release/aae.exe -listallgames | wc -l
```
```bash
./x64/Release/aae.exe pacman
```

Expected: exit 0 with six warnings; `132`; and Pac-Man's sounds play exactly as before. This is the first change to the audio path since Phase 2, so listen rather than trusting the log.

- [ ] **Step 8: Commit**

```bash
git add aae/system/audio/audio_backend.h aae/system/audio/xaudio2_backend.h aae/system/audio/xaudio2_backend.cpp aae/system/audio/mixer.cpp aae/aae.vcxproj
git commit -m "refactor(audio): lift IAudioBackend out of the XAudio2 header

The neutral twenty-method contract lived in xaudio2_backend.h, which
includes <xaudio2.h> unconditionally - so mixer.cpp could not be parsed
on Linux, and an ALSA backend would have had to include the XAudio2
header to learn what it implements. audio_backend.h now holds the
interface and a create_audio_backend() factory that each platform
defines once. Same shape as ISystemWindow/Win32Window from Phase 3a."
```

---

### Task 11: Make `sys_log.cpp` portable

**Files:**
- Modify: `aae/system/util/sys_log.cpp:113-114, 117-124, 260-270, 295-315`

- [ ] **Step 1: Replace the colour helper with ANSI escapes**

Replace the `levelToColor` function (around lines 117–124) with:

```cpp
// ANSI SGR colour codes, understood by Linux terminals and by Windows 10+
// consoles once ENABLE_VIRTUAL_TERMINAL_PROCESSING is set (see below). The
// colours match what SetConsoleTextAttribute produced before, so console
// output is visually unchanged on Windows.
static const char* levelToAnsi(Log::Level level)
{
	switch (level) {
	case Log::Level::Debug: return "\x1b[90m";  // bright black (grey)
	case Log::Level::Info:  return "\x1b[97m";  // bright white
	case Log::Level::Warn:  return "\x1b[93m";  // bright yellow
	case Log::Level::Error: return "\x1b[91m";  // bright red
	default:                return "\x1b[37m";  // white
	}
}
static const char* ansiReset() { return "\x1b[0m"; }
```

- [ ] **Step 2: Replace the console-writing block**

Replace the `GetStdHandle`/`GetConsoleScreenBufferInfo`/`SetConsoleTextAttribute` block (around lines 295–315) with a platform-independent write:

```cpp
	// One code path for both platforms. Windows console attribute juggling
	// is gone: the escape codes above do the same job, and Windows honours
	// them once virtual-terminal processing is enabled at console setup.
	fputs(levelToAnsi(level), stdout);
	fputs(line.c_str(), stdout);
	fputs(ansiReset(), stdout);
	fflush(stdout);
```

Match `line` to whatever the surrounding code already calls the formatted message.

- [ ] **Step 3: Enable virtual-terminal processing on Windows at console creation**

In the console-setup block (around lines 260–270, where `AllocConsole` is called), keep the Windows-only section guarded and add the mode change:

```cpp
#ifdef _WIN32
			AllocConsole();
			// ... existing stdout/stderr reopen and _setmode calls ...
			_setmode(_fileno(stdout), _O_TEXT);
			_setmode(_fileno(stderr), _O_TEXT);

			// Make the console interpret the ANSI escapes levelToAnsi emits.
			// Windows 10 1511+ supports this; AAE already requires Win10/11
			// (see win10_win11_required_code.cpp), so it is always available.
			// If it fails, output degrades to uncoloured text - never to
			// visible escape sequences.
			{
				HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
				DWORD mode = 0;
				if (hOut != INVALID_HANDLE_VALUE && GetConsoleMode(hOut, &mode))
					SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
			}
#endif
```

- [ ] **Step 4: Guard the remaining Windows-only includes**

Change lines 113–114 from:

```cpp
#include <windows.h>
#include <io.h>
#include <fcntl.h>
```

to:

```cpp
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif
```

- [ ] **Step 5: Confirm no unguarded Win32 remains**

```bash
grep -nE "GetStdHandle|SetConsoleTextAttribute|FOREGROUND_|CONSOLE_SCREEN" aae/system/util/sys_log.cpp
```

Expected: the only hits are inside the `#ifdef _WIN32` block from Step 3.

- [ ] **Step 6: Compile it standalone under g++**

```bash
wsl -d Ubuntu -- bash -c "cd /mnt/c/Source2026/AAE_publish && g++ -std=c++17 -fsyntax-only -Iaae/system/util -Iaae/system/3rdparty aae/system/util/sys_log.cpp && echo OK"
```

Expected: `OK`.

- [ ] **Step 7: Windows build green, and check the colours by eye**

```bash
"/c/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" aae/aae.vcxproj -t:Rebuild -p:Configuration=Release -p:Platform=x64 -v:q -nologo
```
```bash
./x64/Release/aae.exe pacman
```

Expected: exit 0 with six warnings, and the console log still shows grey debug / white info / yellow warnings / red errors. **If you see raw `←[97m` sequences instead of colour, Step 3 did not take effect** — that is a regression, not a cosmetic issue, because it makes the log much harder to read.

- [ ] **Step 8: Commit**

```bash
git add aae/system/util/sys_log.cpp
git commit -m "refactor(log): ANSI colour instead of Windows console attributes

One code path for both platforms. Windows 10+ honours the escapes once
ENABLE_VIRTUAL_TERMINAL_PROCESSING is set at console creation, which AAE
can always rely on since it already requires Win10/11. AllocConsole and
_setmode stay Windows-guarded - Linux has no equivalent concept."
```

---

### Task 12: Make `path_helper.cpp` and `sys_fileio.cpp` portable

These two independently wrap `GetModuleFileName` to find the executable directory. Porting that twice would be a mistake, so `sys_fileio` delegates to `path_helper`.

**Files:**
- Modify: `aae/system/util/path_helper.cpp`
- Modify: `aae/system/util/path_helper.h`
- Modify: `aae/system/util/sys_fileio.cpp:29-46`

- [ ] **Step 1: Add the one per-platform primitive to `path_helper.cpp`**

Replace the `<windows.h>` include at line 34 and add, near the top of the file:

```cpp
#include <filesystem>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include "utf8conv.h"
#else
#include <unistd.h>
#include <limits.h>
#endif

//------------------------------------------------------------------------------
// The ONLY genuinely platform-specific operation in this file: ask the OS for
// the running executable's own path. Everything above it is std::filesystem.
//------------------------------------------------------------------------------
static std::filesystem::path exe_path()
{
#ifdef _WIN32
	wchar_t buf[MAX_PATH] = { 0 };
	DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
	if (len == 0 || len == MAX_PATH) return {};
	return std::filesystem::path(buf);
#else
	char buf[PATH_MAX] = { 0 };
	ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
	if (len <= 0) return {};
	buf[len] = '\0';
	return std::filesystem::path(buf);
#endif
}
```

- [ ] **Step 2: Rewrite the exported path functions over `std::filesystem`**

Both existing variants (the wide one at line ~44 and the narrow one at line ~82) hand-search for `'\\'`. Replace their bodies so they derive the directory from `exe_path()`:

```cpp
std::string getExeDir()
{
	std::filesystem::path dir = exe_path().parent_path();
	if (dir.empty()) return {};
	// Trailing separator, matching the previous behaviour of every caller
	// that concatenates a filename straight onto this string.
	return dir.string() + static_cast<char>(std::filesystem::path::preferred_separator);
}
```

Keep the existing exported function names and signatures exactly — check `path_helper.h` and preserve them, so no caller changes. If the header currently exports both a wide and a narrow variant, keep both; implement the wide one as `exe_path().parent_path().wstring()`.

- [ ] **Step 3: Delete the duplicate in `sys_fileio.cpp`**

Remove the two exe-path functions at lines ~36 and ~43 and have any internal caller use `path_helper`'s instead. Add `#include "path_helper.h"`.

- [ ] **Step 4: Replace `GetFileAttributesA` with `std::filesystem`**

At `sys_fileio.cpp:31`, replace:

```cpp
    DWORD attribs = GetFileAttributesA(dirName);
```

and the surrounding directory test with:

```cpp
    std::error_code ec;
    bool isDir = std::filesystem::is_directory(dirName, ec);
```

Adjust the function's return expression to use `isDir` (and treat `ec` being set as "not a directory", matching the old behaviour when `GetFileAttributes` returned `INVALID_FILE_ATTRIBUTES`).

- [ ] **Step 5: Guard or remove the `<windows.h>` include**

`sys_fileio.cpp:6` includes `<windows.h>` unconditionally. Once Steps 3–4 are done nothing should need it; delete the line. If something still does, wrap it in `#ifdef _WIN32` and note in the commit message what still requires it.

- [ ] **Step 6: Both files compile under g++**

```bash
wsl -d Ubuntu -- bash -c "cd /mnt/c/Source2026/AAE_publish && g++ -std=c++17 -fsyntax-only -Iaae/system/util -Iaae/system/3rdparty aae/system/util/path_helper.cpp aae/system/util/sys_fileio.cpp && echo OK"
```

Expected: `OK`.

- [ ] **Step 7: Windows build green, and paths still resolve**

```bash
"/c/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" aae/aae.vcxproj -t:Rebuild -p:Configuration=Release -p:Platform=x64 -v:q -nologo
```
```bash
./x64/Release/aae.exe pacman
```

Expected: exit 0 with six warnings, and Pac-Man loads its ROMs, artwork and samples. Path handling is what finds all three, so a silent regression here looks like "the game can't find its ROMs" — check the log for load failures even if the game runs.

- [ ] **Step 8: Commit**

```bash
git add aae/system/util/path_helper.cpp aae/system/util/path_helper.h aae/system/util/sys_fileio.cpp
git commit -m "refactor(fs): std::filesystem paths, with one per-platform exe_path()

path_helper and sys_fileio each wrapped GetModuleFileName separately and
hand-searched for backslashes. Now there is a single platform primitive -
GetModuleFileNameW on Windows, /proc/self/exe on Linux - and everything
above it is std::filesystem. sys_fileio delegates rather than duplicating."
```

---

### Task 13: Neutral timer contract and the POSIX implementation

`wintimer.h` does `typedef struct timer_s {...} timer_t;` while including `<time.h>`. On Linux, POSIX `<time.h>` already declares `timer_t`, so this is a hard redefinition error — `posix_timer.cpp` cannot implement the existing header without the rename.

**Files:**
- Create: `aae/system/util/sys_timer.h`
- Create: `aae/system/util/linux/posix_timer.cpp`
- Modify: `aae/system/util/wintimer.cpp`
- Delete: `aae/system/util/wintimer.h` (after updating its consumers)

- [ ] **Step 1: Find every consumer first**

```bash
grep -rn "wintimer.h\|timer_t\|g_timer\|TimerInit\|TimerGetTime" aae/ --include=*.cpp --include=*.h | grep -v "cpu_code/timer"
```

Record the list — every one needs its include swapped in Step 4. Note that `aae/aae/cpu_code/timer.cpp` is the *emulation* timer system and is unrelated; do not touch it.

- [ ] **Step 2: Create `aae/system/util/sys_timer.h`**

```cpp
//==============================================================================
// sys_timer.h -- the platform-neutral wall-clock timer contract.
//
// Replaces wintimer.h, whose `typedef struct timer_s {...} timer_t;` collided
// head-on with POSIX <time.h>, which declares timer_t as the timer_create()
// handle type. That was a hard redefinition error on Linux, not a warning -
// the type had to be renamed before any POSIX implementation was possible.
//
// Implementations: wintimer.cpp (QueryPerformanceCounter),
//                  linux/posix_timer.cpp (clock_gettime(CLOCK_MONOTONIC)).
//
// NOTE: unrelated to aae/aae/cpu_code/timer.cpp, which is the emulated
// machine's timer system.
//==============================================================================
#ifndef SYS_TIMER_H
#define SYS_TIMER_H

#include <cstdint>

struct AaeTimer
{
	int64_t  frequency;                 // Ticks per second
	float    resolution;                // Seconds per tick
	uint32_t mm_timer_start;            // Win32 multimedia-timer fallback only
	uint32_t mm_timer_elapsed;          // Win32 multimedia-timer fallback only
	bool     performance_timer;         // True when a high-resolution source is in use
	int64_t  performance_timer_start;
	int64_t  performance_timer_elapsed;
};

extern AaeTimer g_timer;

void  TimerInit(void);       // Call once at startup
void  TimerShutdown(void);   // Call at exit
float TimerGetTime(void);    // Seconds since TimerInit
float TimerGetTimeMS(void);  // Milliseconds since TimerInit
float TimerElapsedSinceLastCall(void);
bool  TimerIsHighResolution(void);
void  TimerReset(void);

#endif // SYS_TIMER_H
```

- [ ] **Step 3: Retarget `wintimer.cpp`**

Change its `#include "wintimer.h"` to `#include "sys_timer.h"`, add `#include <windows.h>` (which the old header used to supply), and replace every `timer_t` with `AaeTimer`, `__int64` with `int64_t`, and `BOOL` with `bool`. The `QueryPerformanceCounter` logic itself does not change.

- [ ] **Step 4: Update every consumer found in Step 1**

Replace `#include "wintimer.h"` with `#include "sys_timer.h"` in each, and any local use of `timer_t` with `AaeTimer`.

- [ ] **Step 5: Delete `wintimer.h`**

```bash
git rm aae/system/util/wintimer.h
```

Also remove its `<ClInclude>` entry from `aae/aae.vcxproj`.

- [ ] **Step 6: Create `aae/system/util/linux/posix_timer.cpp`**

```cpp
//==============================================================================
// posix_timer.cpp -- the Linux implementation of sys_timer.h.
//
// clock_gettime(CLOCK_MONOTONIC) is always high-resolution (nanoseconds) and
// is unaffected by wall-clock adjustments, so there is no fallback path and
// TimerIsHighResolution() is unconditionally true. The mm_timer_* fields in
// AaeTimer exist only for the Win32 multimedia-timer fallback and stay zero
// here.
//==============================================================================
#include "sys_timer.h"

#include <ctime>

AaeTimer g_timer = {};

static int64_t now_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
}

void TimerInit(void)
{
	g_timer.frequency               = 1000000000LL;   // nanoseconds
	g_timer.resolution              = 1.0f / 1000000000.0f;
	g_timer.performance_timer       = true;
	g_timer.performance_timer_start = now_ns();
	g_timer.performance_timer_elapsed = 0;
	g_timer.mm_timer_start          = 0;
	g_timer.mm_timer_elapsed        = 0;
}

void TimerShutdown(void)
{
	// Nothing to release: clock_gettime needs no setup or teardown, unlike
	// Win32's timeBeginPeriod/timeEndPeriod pair.
}

float TimerGetTime(void)
{
	return (float)((double)(now_ns() - g_timer.performance_timer_start) / 1000000000.0);
}

float TimerGetTimeMS(void)
{
	return (float)((double)(now_ns() - g_timer.performance_timer_start) / 1000000.0);
}

float TimerElapsedSinceLastCall(void)
{
	int64_t now  = now_ns();
	int64_t last = g_timer.performance_timer_elapsed
	                 ? g_timer.performance_timer_elapsed
	                 : g_timer.performance_timer_start;
	g_timer.performance_timer_elapsed = now;
	return (float)((double)(now - last) / 1000000000.0);
}

bool TimerIsHighResolution(void)
{
	return true;
}

void TimerReset(void)
{
	g_timer.performance_timer_start   = now_ns();
	g_timer.performance_timer_elapsed = 0;
}
```

- [ ] **Step 7: Register the new files in both build systems**

In `CMakeLists.txt`, `aae/system/util/wintimer.cpp` is already in `AAE_PLATFORM_SOURCES` for Windows. Add `aae/system/util/linux/posix_timer.cpp` to the Linux `AAE_PLATFORM_SOURCES_WANTED` list — it is already listed there as a wanted path, so once the file exists the `EXISTS` check picks it up and it drops out of the "not yet implemented" warning. Verify that warning now names only two files.

Add `sys_timer.h` to `aae/aae.vcxproj` as a `<ClInclude>`.

- [ ] **Step 8: Both platforms compile**

```bash
"/c/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" aae/aae.vcxproj -t:Rebuild -p:Configuration=Release -p:Platform=x64 -v:q -nologo
```
```bash
wsl -d Ubuntu -- bash -c "cd /mnt/c/Source2026/AAE_publish && g++ -std=c++17 -fsyntax-only -Iaae/system/util aae/system/util/linux/posix_timer.cpp && echo OK"
```

Expected: exit 0 with six warnings; `OK`.

- [ ] **Step 9: Confirm Windows timing is unchanged**

```bash
./x64/Release/aae.exe asteroid
```

Expected: the game runs at normal speed with no stutter. `TimerElapsedSinceLastCall` feeds frame pacing, so a mistake in the rename shows up as visibly wrong speed rather than a compile error.

- [ ] **Step 10: Commit**

```bash
git add aae/system/util/sys_timer.h aae/system/util/linux/posix_timer.cpp aae/system/util/wintimer.cpp aae/aae.vcxproj CMakeLists.txt
git rm --cached aae/system/util/wintimer.h 2>/dev/null || true
git commit -m "refactor(timer): sys_timer.h replaces wintimer.h; add the POSIX backend

wintimer.h typedef'd timer_t while including <time.h> - and POSIX
<time.h> already declares timer_t as the timer_create() handle type, so
the name had to change before any Linux implementation was possible.
AaeTimer replaces it, __int64/BOOL become int64_t/bool, and
posix_timer.cpp implements the contract over clock_gettime(CLOCK_MONOTONIC)."
```

---

### Task 14: The platform-neutral software voice mixer

`IAudioBackend`'s voice API assumes a hardware voice mixer, which XAudio2 provides and ALSA does not — ALSA hands back one PCM stream. That mixing has to exist somewhere, and it is wanted again for the Teensy target, so it goes in its own ALSA-free unit.

**Files:**
- Create: `aae/system/audio/voice_mixer.h`
- Create: `aae/system/audio/voice_mixer.cpp`

- [ ] **Step 1: Create `aae/system/audio/voice_mixer.h`**

```cpp
//==============================================================================
// voice_mixer.h -- software mixing of IAudioBackend voices into one PCM stream.
//
// XAudio2 gives you a hardware-ish voice mixer; ALSA does not - it hands back
// a single PCM stream. Everything IAudioBackend's Voice* methods promise
// (per-voice gain, frequency ratio, looping, queued-buffer counts) therefore
// has to be done in software on such a backend.
//
// Deliberately contains NO ALSA calls, and no OS calls at all: the Teensy
// target needs exactly this mixing over a pair of DACs, and duplicating it
// there would guarantee the two drift. alsa_backend.cpp owns device handling;
// this owns arithmetic.
//==============================================================================
#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <memory>
#include <mutex>

struct SoftVoice {
	std::vector<uint8_t> data;        // Submitted sample data, backend-owned copy
	uint32_t  channels     = 1;
	uint32_t  sampleRate   = 44100;
	uint32_t  bitsPerSample= 16;
	double    position     = 0.0;     // Fractional read cursor, in frames
	float     gain         = 1.0f;
	float     freqRatio    = 1.0f;
	bool      looping      = false;
	bool      playing      = false;
	bool      exitLoopReq  = false;   // VoiceExitLoop: finish this pass, then stop
	uint32_t  buffersQueued= 0;
};

class VoiceMixer {
public:
	// outChannels/outRate describe the single stream voices are summed into.
	void Configure(uint32_t outChannels, uint32_t outRate);

	// Sums every playing voice into `out` (interleaved int16), replacing its
	// contents. frames = samples per channel.
	void MixInto(int16_t* out, uint32_t frames);

	void SetMasterVolume(float linear);
	float GetMasterVolume() const;

	// Voices are owned here; the backend hands out opaque pointers to them.
	SoftVoice* Create();
	void       Destroy(SoftVoice* v);

private:
	std::mutex m_lock;
	std::vector<std::unique_ptr<SoftVoice>> m_voices;
	uint32_t m_outChannels = 2;
	uint32_t m_outRate     = 44100;
	float    m_master      = 0.80f;   // Matches mixer.cpp's default (~-1.9 dB)
};
```

- [ ] **Step 2: Create `aae/system/audio/voice_mixer.cpp`**

Implement each method. The mixing loop, which is the part worth getting right:

```cpp
#include "voice_mixer.h"

#include <algorithm>
#include <cmath>

void VoiceMixer::Configure(uint32_t outChannels, uint32_t outRate)
{
	std::lock_guard<std::mutex> g(m_lock);
	m_outChannels = outChannels;
	m_outRate     = outRate;
}

void VoiceMixer::SetMasterVolume(float linear)
{
	std::lock_guard<std::mutex> g(m_lock);
	m_master = std::clamp(linear, 0.0f, 1.0f);
}

float VoiceMixer::GetMasterVolume() const { return m_master; }

SoftVoice* VoiceMixer::Create()
{
	std::lock_guard<std::mutex> g(m_lock);
	m_voices.push_back(std::make_unique<SoftVoice>());
	return m_voices.back().get();
}

void VoiceMixer::Destroy(SoftVoice* v)
{
	std::lock_guard<std::mutex> g(m_lock);
	m_voices.erase(std::remove_if(m_voices.begin(), m_voices.end(),
		[v](const std::unique_ptr<SoftVoice>& p) { return p.get() == v; }),
		m_voices.end());
}

void VoiceMixer::MixInto(int16_t* out, uint32_t frames)
{
	std::lock_guard<std::mutex> g(m_lock);

	// Accumulate in 32-bit so summed voices cannot wrap before the clamp.
	std::vector<int32_t> acc(frames * m_outChannels, 0);

	for (auto& vp : m_voices) {
		SoftVoice* v = vp.get();
		if (!v->playing || v->data.empty()) continue;

		const uint32_t bytesPerSample = v->bitsPerSample / 8;
		const size_t   totalFrames    = v->data.size() / (bytesPerSample * v->channels);
		if (totalFrames == 0) continue;

		// Rate conversion and the voice's own frequency ratio, combined.
		const double step = ((double)v->sampleRate / (double)m_outRate) * (double)v->freqRatio;

		for (uint32_t f = 0; f < frames; ++f) {
			size_t idx = (size_t)v->position;
			if (idx >= totalFrames) {
				if (v->looping && !v->exitLoopReq) {
					v->position = 0.0;
					idx = 0;
				} else {
					v->playing      = false;
					v->exitLoopReq  = false;
					if (v->buffersQueued) --v->buffersQueued;
					break;
				}
			}

			for (uint32_t c = 0; c < m_outChannels; ++c) {
				// Mono voices feed every output channel; multi-channel voices
				// map channel-for-channel and repeat the last if short.
				const uint32_t src = (v->channels == 1) ? 0 : std::min(c, v->channels - 1);
				int32_t sample = 0;

				if (v->bitsPerSample == 16) {
					const int16_t* p = reinterpret_cast<const int16_t*>(v->data.data());
					sample = p[idx * v->channels + src];
				} else { // 8-bit unsigned PCM, as the sample loader produces
					sample = ((int32_t)v->data[idx * v->channels + src] - 128) << 8;
				}

				acc[f * m_outChannels + c] += (int32_t)(sample * v->gain);
			}

			v->position += step;
		}
	}

	for (size_t i = 0; i < acc.size(); ++i) {
		int32_t s = (int32_t)(acc[i] * m_master);
		out[i] = (int16_t)std::clamp(s, -32768, 32767);
	}
}
```

- [ ] **Step 3: Compile it under g++ and MSVC**

```bash
wsl -d Ubuntu -- bash -c "cd /mnt/c/Source2026/AAE_publish && g++ -std=c++17 -Wall -fsyntax-only -Iaae/system/audio aae/system/audio/voice_mixer.cpp && echo OK"
```

Expected: `OK`, with no warnings.

- [ ] **Step 4: Commit**

```bash
git add aae/system/audio/voice_mixer.h aae/system/audio/voice_mixer.cpp
git commit -m "feat(audio): software voice mixer, free of any OS dependency

IAudioBackend's Voice* API assumes a voice mixer. XAudio2 has one; ALSA
returns a single PCM stream, so per-voice gain, frequency ratio, looping
and queue accounting must be done in software. Kept in its own unit with
no ALSA and no OS calls, because the Teensy target needs exactly this
mixing over its DACs and a second copy would drift from this one."
```

---

### Task 15: The ALSA backend

**Files:**
- Create: `aae/system/audio/alsa_backend.h`
- Create: `aae/system/audio/alsa_backend.cpp`

- [ ] **Step 1: Create `aae/system/audio/alsa_backend.h`**

```cpp
//==============================================================================
// alsa_backend.h -- the Linux IAudioBackend implementation.
//
// Owns device handling only: open, hardware-parameter negotiation, and the
// snd_pcm_writei feed thread. All per-voice arithmetic lives in
// voice_mixer.h, which has no ALSA in it so the Teensy target can reuse it.
//==============================================================================
#pragma once

#include "audio_backend.h"
#include "voice_mixer.h"

#include <alsa/asoundlib.h>

#include <atomic>
#include <thread>
#include <vector>

class AlsaBackend : public IAudioBackend {
public:
	~AlsaBackend() override;

	// ---- declare every IAudioBackend method here with `override`, matching
	// ---- audio_backend.h exactly. Copy the signatures across rather than
	// ---- retyping them.

private:
	void FeedThread();

	snd_pcm_t*        m_pcm       = nullptr;
	VoiceMixer        m_mixer;
	std::thread       m_thread;
	std::atomic<bool> m_running{false};
	uint32_t          m_rate      = 44100;
	uint32_t          m_channels  = 2;
	snd_pcm_uframes_t m_period    = 0;
	std::vector<int16_t> m_scratch;   // One period, handed to the mixer
	std::vector<uint8_t> m_appBuffer; // Returned by GetNextBuffer()
};
```

- [ ] **Step 2: Implement `Init` and `Shutdown` in `alsa_backend.cpp`**

```cpp
#include "alsa_backend.h"
#include "sys_log.h"

bool AlsaBackend::Init(int rateHz, int fps)
{
	m_rate = (uint32_t)rateHz;

	int err = snd_pcm_open(&m_pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
	if (err < 0) {
		LOG_ERROR("ALSA: cannot open default device: %s\n", snd_strerror(err));
		m_pcm = nullptr;
		return false;
	}

	// One period per emulated frame keeps latency tied to the frame rate the
	// same way the XAudio2 backend's buffer sizing does.
	m_period = (snd_pcm_uframes_t)(m_rate / (fps > 0 ? fps : 60));

	snd_pcm_hw_params_t* hw = nullptr;
	snd_pcm_hw_params_alloca(&hw);
	snd_pcm_hw_params_any(m_pcm, hw);
	snd_pcm_hw_params_set_access(m_pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
	snd_pcm_hw_params_set_format(m_pcm, hw, SND_PCM_FORMAT_S16_LE);
	snd_pcm_hw_params_set_channels(m_pcm, hw, m_channels);
	snd_pcm_hw_params_set_rate_near(m_pcm, hw, &m_rate, nullptr);
	snd_pcm_hw_params_set_period_size_near(m_pcm, hw, &m_period, nullptr);

	err = snd_pcm_hw_params(m_pcm, hw);
	if (err < 0) {
		LOG_ERROR("ALSA: hw params failed: %s\n", snd_strerror(err));
		snd_pcm_close(m_pcm);
		m_pcm = nullptr;
		return false;
	}

	// m_rate may have been adjusted by set_rate_near - tell the mixer what
	// the device actually gave us, not what we asked for.
	m_mixer.Configure(m_channels, m_rate);
	m_scratch.assign(m_period * m_channels, 0);
	m_appBuffer.assign(m_period * m_channels * sizeof(int16_t), 0);

	LOG_INFO("ALSA: %u Hz, %u channels, period %lu frames\n",
	         m_rate, m_channels, (unsigned long)m_period);

	m_running = true;
	m_thread  = std::thread(&AlsaBackend::FeedThread, this);
	return true;
}

void AlsaBackend::Shutdown()
{
	m_running = false;
	if (m_thread.joinable()) m_thread.join();
	if (m_pcm) {
		snd_pcm_drain(m_pcm);
		snd_pcm_close(m_pcm);
		m_pcm = nullptr;
	}
}

AlsaBackend::~AlsaBackend() { Shutdown(); }
```

- [ ] **Step 3: Implement the feed thread, with underrun recovery**

```cpp
void AlsaBackend::FeedThread()
{
	while (m_running) {
		m_mixer.MixInto(m_scratch.data(), (uint32_t)m_period);

		snd_pcm_sframes_t written = snd_pcm_writei(m_pcm, m_scratch.data(), m_period);
		if (written < 0) {
			// EPIPE is an underrun: the device ran dry because we were late.
			// Recovering and continuing is correct - treating it as fatal
			// would kill audio on the first scheduling hiccup.
			written = snd_pcm_recover(m_pcm, (int)written, /*silent=*/1);
			if (written < 0) {
				LOG_ERROR("ALSA: write failed: %s\n", snd_strerror((int)written));
				break;
			}
		}
	}
}
```

- [ ] **Step 4: Implement the remaining methods over `VoiceMixer`**

Each maps directly. `VoiceCreate` returns `reinterpret_cast<VoiceHandle*>(m_mixer.Create())`; `VoiceSetVolume` sets `gain`; `VoiceSetFrequencyRatio` sets `freqRatio`; `VoiceStart`/`VoiceStop` set `playing`; `VoiceExitLoop` sets `exitLoopReq`; `VoiceBuffersQueued` returns `buffersQueued`; `VoiceInputChannels` returns `channels`. `VoiceSubmit` copies the data into the voice and increments `buffersQueued`.

`VoiceSetOutputMatrix` returns `false` — positional audio is Phase 3d (spec §3.6, §5). Add:

```cpp
bool AlsaBackend::VoiceSetOutputMatrix(VoiceHandle*, uint32_t, uint32_t, const float*)
{
	// Positional audio on Linux is Phase 3d. Returning false is honest:
	// mixer.cpp already treats it as "this backend cannot pan" and falls
	// back to its own gain path rather than silently doing nothing.
	return false;
}
```

Match the exact signature from `audio_backend.h`.

- [ ] **Step 5: Define the Linux factory**

At the end of `alsa_backend.cpp`:

```cpp
//------------------------------------------------------------------------------
// The Linux half of audio_backend.h's platform factory.
//------------------------------------------------------------------------------
std::unique_ptr<IAudioBackend> create_audio_backend()
{
	return std::make_unique<AlsaBackend>();
}
```

- [ ] **Step 6: Add to CMake**

In `CMakeLists.txt`'s Linux branch, `aae/system/audio/linux/alsa_backend.cpp` is already in `AAE_PLATFORM_SOURCES_WANTED`. Either move the file to that path or update the wanted list to `aae/system/audio/alsa_backend.cpp` — pick one and make the list match reality. Confirm the "not yet implemented" warning now names only `linux_window.cpp` and `evdev_input.cpp`.

- [ ] **Step 7: Compile**

```bash
wsl -d Ubuntu -- bash -c "cd /mnt/c/Source2026/AAE_publish && g++ -std=c++17 -Wall -fsyntax-only -Iaae/system/audio -Iaae/system/util -Iaae/aae \$(pkg-config --cflags alsa) aae/system/audio/alsa_backend.cpp && echo OK"
```

Expected: `OK`.

- [ ] **Step 8: Commit**

```bash
git add aae/system/audio/alsa_backend.h aae/system/audio/alsa_backend.cpp CMakeLists.txt
git commit -m "feat(audio): ALSA backend implementing IAudioBackend

Device handling only - open, hw-param negotiation, and a feed thread
that recovers from underruns rather than treating the first scheduling
hiccup as fatal. Per-voice arithmetic is VoiceMixer's. VoiceSetOutputMatrix
returns false: positional audio on Linux is Phase 3d, and mixer.cpp
already treats a false return as 'this backend cannot pan'."
```

---

### Task 16: `audio_3d_null.cpp` for Linux

**Files:**
- Create: `aae/system/audio/audio_3d_null.cpp`

- [ ] **Step 1: Create the file**

```cpp
//==============================================================================
// audio_3d_null.cpp -- the Linux implementation of audio_3d.h.
//
// Positional audio is Phase 3d (see the Phase 3b spec, sections 3.6 and 5).
// It is SCHEDULED, not abandoned - parity with Windows is the programme's
// stated end point.
//
// This file exists so the gap is loud. A Linux build that silently played
// everything centre-panned would be indistinguishable from a working one
// until somebody noticed months later that the stereo field was dead, so
// audio_3d_init() logs a warning rather than just returning false.
//
// mixer.cpp gates its entire positional path on g_3d_inited, so returning
// false here disables it cleanly with no other changes.
//==============================================================================
#include "audio_3d.h"
#include "sys_log.h"

bool audio_3d_init(uint32_t /*channel_mask*/, uint32_t /*dst_channels*/)
{
	LOG_WARN("Positional audio is not implemented on this platform "
	         "(Phase 3d) - sounds will play without 3D panning\n");
	return false;
}

void audio_3d_shutdown() {}
bool audio_3d_ready() { return false; }
void audio_3d_set_listener_2d(float /*x*/, float /*y*/) {}

bool audio_3d_apply_2d(VoiceHandle* /*voice*/, float /*x*/, float /*y*/,
                       float /*distance_scale*/)
{
	return false;
}

void audio_3d_debug_print_next_matrix() {}
uint32_t audio_3d_get_channel_mask() { return 0; }
```

Match `audio_3d_apply_2d`'s parameter list to the real declaration in `audio_3d.h:46` — check it and correct the stub if it differs.

- [ ] **Step 2: Add to the CMake Linux sources**

Add `aae/system/audio/audio_3d_null.cpp` to the Linux `AAE_PLATFORM_SOURCES` (unconditionally, not via the `EXISTS` list — this file exists as of this task).

- [ ] **Step 3: Compile**

```bash
wsl -d Ubuntu -- bash -c "cd /mnt/c/Source2026/AAE_publish && g++ -std=c++17 -Wall -fsyntax-only -Iaae/system/audio -Iaae/system/util aae/system/audio/audio_3d_null.cpp && echo OK"
```

Expected: `OK`.

- [ ] **Step 4: Commit**

```bash
git add aae/system/audio/audio_3d_null.cpp CMakeLists.txt
git commit -m "feat(audio): null positional-audio implementation for Linux

Phase 3d does the real thing (X3DAudio's channel-matrix maths has to be
reimplemented). This stub logs a warning at startup rather than failing
silently, so the gap is visible instead of being discovered months later
as a dead stereo field."
```

---

### Task 17: `aae_audiotest` on both platforms — the audible proof

The point of this target is that it links the **real** `mixer.cpp`, not the stubs `aae_headless` uses. Without it, a green Linux build proves nothing about audio.

**Files:**
- Create: `aae/audiotest/audiotest_main.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create `aae/audiotest/audiotest_main.cpp`**

Signatures below are the real ones, read out of `mixer.h` and `sys_log.h` on 2026-07-29: `int mixer_init(int rate, int fps)`, `void mixer_end()`, `void mixer_update()`, `int load_sample_from_buffer(const uint8_t*, size_t, const char* = nullptr, bool = true)`, `void sample_start(int chanid, int samplenum, int loop)`, `void sample_stop(int chanid)`, `int sample_playing(int chanid)`, `int mixer_alloc_channel(int low = 0, int high = MIXER_FIRST_RESERVED_CHANNEL)`, `bool Log::open(const std::string&)`, `void Log::close()`. The built-in sample is `unsigned char error_wav[10008]` from `error_wav.h`.

```cpp
//==============================================================================
// audiotest_main.cpp -- Phase 3b's audible proof.
//
// Links the REAL mixer.cpp against whichever IAudioBackend the platform
// factory supplies (XAudio2 on Windows, ALSA on Linux) and plays a sample.
// aae_headless deliberately stubs the mixer out, so it can go green on Linux
// without any audio code having run at all - this target is what closes that
// gap, and it is also the first exercise of the voice path since Phase 2
// rewrote it.
//
// Plays the built-in error_wav, which is compiled in and needs no ROM or
// asset directory - so this runs anywhere, including a bare Linux checkout.
//==============================================================================
#include "mixer.h"
#include "audio_backend.h"
#include "error_wav.h"
#include "sys_log.h"

#include <chrono>
#include <cstdio>
#include <thread>

int main(void)
{
	Log::open("audiotest.log");

	// 44.1 kHz, 60 fps - the same values aae_emulator.cpp initialises with.
	if (mixer_init(44100, 60) != 0) {
		fprintf(stderr, "mixer_init failed - no audio backend?\n");
		Log::close();
		return 1;
	}

	const int snd = load_sample_from_buffer(error_wav, sizeof(error_wav), "audiotest");
	if (snd < 0) {
		fprintf(stderr, "load_sample_from_buffer failed\n");
		mixer_end();
		Log::close();
		return 1;
	}

	const int ch = mixer_alloc_channel();
	if (ch < 0) {
		fprintf(stderr, "mixer_alloc_channel failed\n");
		mixer_end();
		Log::close();
		return 1;
	}

	printf("playing sample %d on channel %d - you should HEAR this\n", snd, ch);
	sample_start(ch, snd, 0);   // 0 = no loop

	// Pump mixer_update() the way the emulator's frame loop does, rather than
	// sleeping straight through: the software-mixer path does its work there,
	// so a plain sleep would test only the voice path and silently skip half
	// of what this target exists to exercise.
	for (int i = 0; i < 180; ++i) {          // 180 frames @ 60fps = 3 seconds
		mixer_update();
		std::this_thread::sleep_for(std::chrono::milliseconds(16));
	}

	const int stillPlaying = sample_playing(ch);
	sample_stop(ch);
	mixer_end();
	Log::close();

	printf("done (channel %s at the end) - if you heard nothing, this FAILED\n",
	       stillPlaying ? "still playing" : "finished");
	return 0;
}
```

- [ ] **Step 2: Add the target to `CMakeLists.txt`**

After the `aae_headless` target:

```cmake
# =============================================================================
# aae_audiotest - Phase 3b's audible proof. Links the REAL mixer.cpp against
# the platform's IAudioBackend, unlike aae_headless which stubs the mixer out
# entirely (see aae/headless/null_backends.cpp). Without this target a green
# Linux build would say nothing at all about whether audio works.
# =============================================================================
set(AAE_AUDIOTEST_SOURCES
    aae/audiotest/audiotest_main.cpp
    aae/system/audio/mixer.cpp
    aae/system/util/sys_log.cpp
    aae/system/util/path_helper.cpp
    aae/system/util/sys_fileio.cpp
    aae/system/3rdparty/miniz.c
)
if(WIN32)
    list(APPEND AAE_AUDIOTEST_SOURCES
        aae/system/audio/xaudio2_backend.cpp
        aae/system/audio/audio_3d.cpp)
else()
    list(APPEND AAE_AUDIOTEST_SOURCES
        aae/system/audio/alsa_backend.cpp
        aae/system/audio/voice_mixer.cpp
        aae/system/audio/audio_3d_null.cpp)
endif()

add_executable(aae_audiotest ${AAE_AUDIOTEST_SOURCES})
target_compile_features(aae_audiotest PRIVATE cxx_std_17)
target_include_directories(aae_audiotest PRIVATE
    aae/aae aae aae/system/audio aae/system/util aae/system/3rdparty)
target_compile_definitions(aae_audiotest PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
    ${AAE_PLATFORM_DEFINES})
if(WIN32)
    target_include_directories(aae_audiotest PRIVATE ${AAE_XAUDIO2_INCLUDE_DIR})
    target_compile_definitions(aae_audiotest PRIVATE
        $<$<NOT:$<CONFIG:Debug>>:WIN7BUILD>
        $<$<NOT:$<CONFIG:Debug>>:USING_XAUDIO2_REDIST>
        UNICODE _UNICODE)
    target_link_libraries(aae_audiotest PRIVATE ${AAE_PLATFORM_LIBS})
else()
    if(NOT ALSA_FOUND)
        message(FATAL_ERROR "aae_audiotest needs ALSA - install libasound2-dev")
    endif()
    target_link_libraries(aae_audiotest PRIVATE ALSA::ALSA pthread)
endif()
```

Expect unresolved symbols on the first link — `mixer.cpp` will pull in things this short list does not provide. Add only what the linker actually demands, and if it demands large parts of `aae_core`, link `aae_core` rather than growing the list by hand.

- [ ] **Step 3: Build and run it on Windows**

```bash
cmake --build build-cmake --config Release --target aae_audiotest
```
```bash
./build-cmake/Release/aae_audiotest.exe
```

Expected: three seconds of audible sound, then `done`.

- [ ] **Step 4: Build and run it on Linux**

```bash
wsl -d Ubuntu -- bash /mnt/c/Source2026/AAE_publish/scripts/linux/build.sh aae_audiotest
```
```bash
wsl -d Ubuntu -- bash -c "cd /mnt/c/Source2026/AAE_publish && ./build-linux/aae_audiotest"
```

Expected: three seconds of audible sound.

**If nothing is heard on Linux**, distinguish the two possible causes before concluding anything:

```bash
wsl -d Ubuntu -- bash -c "aplay -l 2>&1 | head"
```

If `aplay -l` lists no sound card, WSLg is not exposing one and the failure is environmental — the code may be fine. Record that outcome honestly, keep the compile-and-link result, and mark the audible half of this task as pending verification on the Steam Machine. **Do not report "audio works on Linux" on the strength of a successful link.**

- [ ] **Step 5: Commit**

```bash
git add aae/audiotest/audiotest_main.cpp CMakeLists.txt
git commit -m "test(audio): aae_audiotest exercises the real mixer on both platforms

aae_headless stubs the mixer out entirely, so a green Linux build said
nothing about audio. This links the real mixer.cpp against whichever
IAudioBackend the platform factory returns and plays a sample - the
first exercise of the voice path since Phase 2 rewrote it."
```

---

### Task 18: Full regression pass and phase report

- [ ] **Step 1: Windows MSBuild, from scratch**

```bash
"/c/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" aae/aae.vcxproj -t:Rebuild -p:Configuration=Release -p:Platform=x64 -v:q -nologo
```

Expected: exit 0, exactly six warning lines.

- [ ] **Step 2: Windows CMake, from scratch**

```bash
rm -rf build-cmake && cmake -S . -B build-cmake -A x64 && cmake --build build-cmake --config Release
```

Expected: all targets build.

- [ ] **Step 3: Game count from both binaries**

```bash
./x64/Release/aae.exe -listallgames | wc -l
```
```bash
./build-cmake/Release/aae.exe -listallgames | wc -l
```

Expected: `132` from both. Anything less means whole-archive linking regressed — check the `/WHOLEARCHIVE:` and `$<LINK_LIBRARY:WHOLE_ARCHIVE,...>` options are still present before looking anywhere else.

- [ ] **Step 4: Vector counts, both platforms**

```bash
cd x64/Release && ../../build-cmake/Release/aae_headless.exe asteroid 600 && ../../build-cmake/Release/aae_headless.exe bzone 600
```
```bash
wsl -d Ubuntu -- bash -c "cd /mnt/c/Source2026/AAE_publish/x64/Release && ../../build-linux/aae_headless asteroid 600 && ../../build-linux/aae_headless bzone 600"
```

Expected: `89414` and `353693` from both platforms.

- [ ] **Step 5: Play three games on Windows and listen**

```bash
./x64/Release/aae.exe pacman
```
```bash
./x64/Release/aae.exe bzone
```
```bash
./x64/Release/aae.exe dkong
```

Expected: all boot, play, and sound correct. `bzone` specifically exercises the TMS5220 tables from Task 6 — its speech is the check that Task 6 preserved the coefficients.

- [ ] **Step 6: Write the phase report**

Append a "Phase 3b outcome" section to `docs/superpowers/specs/2026-07-28-phase3b-linux-backends-design.md` recording: which tasks landed, the measured before/after g++ numbers (13/88 → 88/88), any vector-count divergence found in Task 9 and its cause, whether Linux audio was actually heard or only linked, and anything discovered that changes the Phase 3c plan.

- [ ] **Step 7: Commit**

```bash
git add docs/superpowers/specs/2026-07-28-phase3b-linux-backends-design.md
git commit -m "docs: record the Phase 3b outcome"
```

---

## Phase completion criteria

All of these must hold, and each must be *observed*, not assumed:

- [ ] Windows MSBuild green, exactly six warnings
- [ ] Windows CMake builds every target
- [ ] **132 games** listed by both Windows binaries
- [ ] `libaae_core.a` builds on Linux from all 88 translation units
- [ ] Linux `aae_headless` runs and reports **89,414** / **353,693** — identical to Windows
- [ ] `aae_audiotest` is **audible on Windows**
- [ ] `aae_audiotest` is **audible on Linux**, or the failure is confirmed environmental (no card under WSLg) and deferred to the Steam Machine with that stated explicitly
- [ ] Pac-Man, Battlezone and Donkey Kong play correctly on Windows with correct sound
- [ ] Both `#error` leak guards have been *proven* to fire

**Known and accepted at phase end:** the `aae` executable does not link on Linux. `linux_window.cpp` and `evdev_input.cpp` arrive in Phase 3c. This is the one place in the programme where "it doesn't build" is a correct reported outcome.
