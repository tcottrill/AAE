# Linux Keyboard LED Output Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the seven lamp-driving arcade drivers light real keyboard LEDs on Linux, matching the Windows behavior, verified by an automated uinput harness.

**Architecture:** The platform-neutral LED bookkeeping is hoisted out of the `#ifdef` first so the Linux port is four functions instead of seven. Per-device LED capability and writes go into `EvdevDevice`, mirroring the existing force-feedback members exactly. The four `osd_*` entry points live in `evdev_input.cpp`, which owns the device table — no service thread, since a `write()` to an already-open fd is nothing like the Windows IOCTL path. A 250ms re-apply heartbeat in the existing per-frame poll holds the LEDs against libinput, which also owns them because nothing calls `EVIOCGRAB`.

**Tech Stack:** C++17, Linux evdev/uinput, CMake. Windows side is MSBuild `Debug|x64`.

**Spec:** [2026-08-05-linux-keyboard-leds-design.md](../specs/2026-08-05-linux-keyboard-leds-design.md)

---

## Test-first ordering

The verification harness is built **before** the implementation, so the real work lands against a test already failing for the right reason. Task 5 is the explicit RED gate; Task 9 is GREEN.

Task 2 exists to make that possible. `aae_inputtest` links `evdev_input.cpp` but not `led_service_handler.cpp`, so a `--leds` mode cannot link until the `osd_*` symbols live in the evdev backend. Task 2 **moves** the existing Linux stubs there — no behavior change — so every later task builds and the RED gate is runnable.

Task order: 1 hoist → 2 move stubs → 3 inputtest driver → 4 harness → **5 RED** → 6 device LEDs → 7 real `osd_*` → 8 heartbeat → **9 GREEN**.

## File Structure

| File | Status | Responsibility |
|---|---|---|
| `aae/aae/led_service_handler.cpp` | Modify | Neutral `set_led_status` bookkeeping hoisted out of the `#ifdef`; Win32 `osd_*` unchanged; Linux `osd_*` stubs removed once the evdev ones exist. |
| `aae/system/input/linux/evdev_device.h` | Modify | `SupportsLeds()`, `SetLeds()`, `GetLeds()`, `m_hasLeds`. |
| `aae/system/input/linux/evdev_device.cpp` | Modify | EV_LED capability probe, LED write/read, capability logging. |
| `aae/system/input/linux/evdev_input.cpp` | Modify | The four `osd_*` entry points, snapshot/restore, heartbeat, pause handling. |
| `aae/inputtest/inputtest_main.cpp` | Modify | `--leds` mode driving a deterministic mask sequence through the real API. |
| `tools/linux/uinput_devices.cpp` | Modify | EV_LED on the synthetic keyboards, pump all devices, record LED events, `--ledtest` assertions. |

**No CMake changes.** `aae_inputtest` already links `evdev_input.cpp` and `evdev_device.cpp`, which is where the `osd_*` functions will live. **No flatpak changes** — the manifest already carries `--device=input` and `--device=all`.

---

### Task 1: Hoist the neutral LED bookkeeping

Pure refactor. No behavior change on either platform. This is what reduces the Linux port to four functions.

**Files:**
- Modify: `aae/aae/led_service_handler.cpp`

Safe to do: `aae_headless` defines its own `set_led_status`/`set_led_status_all` (`aae/headless/null_backends.cpp:276-277`) and does **not** link `led_service_handler.cpp`, so hoisting cannot create duplicate symbols.

- [ ] **Step 1: Move the latch variables and the trio above the `#ifdef`**

In `led_service_handler.cpp`, after the existing includes (line 6) and **before** `#ifndef _WIN32`, insert:

```cpp
// ---------------------------------------------------------------------------
// Platform-neutral LED bookkeeping.
//
// These latch the driver-facing LED indices and fold them into the mask that
// osd_set_leds() takes. Nothing about it is platform-specific, so it lives
// OUTSIDE the #ifdef: it used to exist twice -- a real copy in the Win32
// branch and a do-nothing copy in the other -- which meant a non-Windows build
// discarded LED state before the osd_ layer was ever reached.
//
// LED mapping: 0 -> NumLock, 1 -> CapsLock, 2 -> ScrollLock.
// ---------------------------------------------------------------------------
static int g_led0_numlock    = 0;
static int g_led1_capslock   = 0;
static int g_led2_scrolllock = 0;

static int led_mask()
{
	int mask = 0;
	if (g_led0_numlock)    mask |= (1 << 0);
	if (g_led1_capslock)   mask |= (1 << 1);
	if (g_led2_scrolllock) mask |= (1 << 2);
	return mask;
}

// which: 0..2 as above. Any other index is ignored -- omegrace.cpp asks for
// index 3, which has no keyboard indicator to map onto. Pre-existing on both
// platforms; see the spec's non-goals.
void set_led_status(int which, int on)
{
	const int v = (on != 0) ? 1 : 0;

	if      (which == 0) g_led0_numlock    = v;
	else if (which == 1) g_led1_capslock   = v;
	else if (which == 2) g_led2_scrolllock = v;
	else return;

	osd_set_leds(led_mask());
}

// Currently latched state for one LED, or 0 for an invalid index.
int get_led_status(int which)
{
	if (which == 0) return g_led0_numlock;
	if (which == 1) return g_led1_capslock;
	if (which == 2) return g_led2_scrolllock;
	return 0;
}

void set_led_status_all(int led0, int led1, int led2)
{
	g_led0_numlock    = (led0 != 0) ? 1 : 0;
	g_led1_capslock   = (led1 != 0) ? 1 : 0;
	g_led2_scrolllock = (led2 != 0) ? 1 : 0;

	osd_set_leds(led_mask());
}
```

- [ ] **Step 2: Delete the three stubs from the non-Win32 branch**

In the `#ifndef _WIN32` block, delete these three lines (currently `:21-23`), leaving the four `osd_*` stubs in place:

```cpp
void set_led_status(int, int)          {}
int  get_led_status(int)               { return 0; }
void set_led_status_all(int, int, int) {}
```

Update that block's comment so it describes only what remains — the four `osd_*` stubs — rather than the whole LED surface.

- [ ] **Step 3: Delete the duplicates from the Win32 branch**

In the `#else` branch, delete the three latch variables (currently `:44-46`):

```cpp
static int g_led0_numlock = 0;
static int g_led1_capslock = 0;
static int g_led2_scrolllock = 0;
```

and the whole of `set_led_status`, `get_led_status` and `set_led_status_all` with their comment blocks (currently `:351-420`). The Win32 `osd_*` functions and everything above them stay exactly as they are.

- [ ] **Step 4: Build Windows and confirm no behavior change**

```bash
msbuild aae.sln -p:Configuration=Debug -p:Platform=x64 -v:minimal
```

Expected: builds clean. Run Asteroids and confirm the 1P/2P start lamps still drive Num/Caps exactly as before — this task must be invisible.

- [ ] **Step 5: Build Linux**

```bash
cmake --build build-linux -j
```

Expected: builds clean.

- [ ] **Step 6: Commit**

```bash
git add aae/aae/led_service_handler.cpp && git commit -m "refactor(leds): hoist neutral LED bookkeeping out of the platform ifdef"
```

---

### Task 2: Move the Linux LED stubs into the evdev backend

Pure symbol move, no behavior change. This is what lets `aae_inputtest` link a `--leds` mode before the implementation exists, which is what makes the RED gate in Task 5 possible.

**Files:**
- Modify: `aae/aae/led_service_handler.cpp`
- Modify: `aae/system/input/linux/evdev_input.cpp`

- [ ] **Step 1: Add the stubs to the evdev backend**

In `evdev_input.cpp`, after the mouse accessor block (the section ending with `RawInput_MouseSeenInput`, around `:675`), add:

```cpp
//==============================================================================
// Keyboard LEDs -- the osdepend.h LED contract.
//
// Declared locally rather than by including osdepend.h, which would drag the
// whole emulator surface into the input backend; linux_main.cpp forward-
// declares osd_led_service_stop the same way.
//
// Stubs for now: Task 7 of the LED plan fills these in. They live here rather
// than in led_service_handler.cpp because this file owns the open device fds
// the real implementation needs.
//==============================================================================
void osd_led_service_start();
void osd_led_service_stop();
void osd_set_leds(int state);
int  osd_get_leds();

void osd_led_service_start() {}
void osd_led_service_stop()  {}
void osd_set_leds(int)       {}
int  osd_get_leds()          { return 0; }
```

- [ ] **Step 2: Delete the stubs from `led_service_handler.cpp`**

The `#ifndef _WIN32` block now holds only the four `osd_*` stubs. Delete the block entirely and change the `#else` guarding the Win32 implementation to a plain `#ifdef _WIN32`, so the file reads:

```cpp
// (neutral bookkeeping from Task 1)

// The non-Windows osd_led_* implementations live in the evdev backend
// (aae/system/input/linux/evdev_input.cpp), where the open device fds are.

#ifdef _WIN32
// ... the entire Win32 implementation, unchanged ...
#endif // _WIN32
```

- [ ] **Step 3: Build both platforms**

```bash
cmake --build build-linux -j
```

```bash
msbuild aae.sln -p:Configuration=Debug -p:Platform=x64 -v:minimal
```

Expected: both clean. A duplicate-symbol error on Linux means the old stubs were not fully removed.

- [ ] **Step 4: Commit**

```bash
git add aae/aae/led_service_handler.cpp aae/system/input/linux/evdev_input.cpp && git commit -m "refactor(leds): move the Linux LED stubs to the evdev backend"
```

---

### Task 3: `aae_inputtest --leds` driver mode

A deterministic mask sequence the harness can assert against. Links cleanly now that Task 2 put the `osd_*` symbols in `evdev_input.cpp`.

**Files:**
- Modify: `aae/inputtest/inputtest_main.cpp`

- [ ] **Step 1: Declare the LED contract and add the mode**

Near the top of `inputtest_main.cpp`, with the other declarations:

```cpp
// The LED half of the OSD contract, implemented by the evdev backend. Declared
// here rather than including osdepend.h, which drags in the whole emulator
// surface this harness deliberately does not link (same reason linux_main.cpp
// forward-declares osd_led_service_stop).
extern void osd_led_service_start();
extern void osd_led_service_stop();
extern void osd_set_leds(int state);
extern int  osd_get_leds();
```

Then add the mode function:

```cpp
// Walk every LED combination with a pause between each, so a watcher can tie
// each observed EV_LED burst to a known requested mask. The sequence is fixed
// and documented here because uinput_devices.cpp --ledtest asserts against it:
//   0 -> 1 -> 2 -> 4 -> 7 -> 0
static int run_led_test()
{
	static const int kSequence[] = { 0, 1, 2, 4, 7, 0 };

	printf("[LEDTEST] starting LED service\n");
	fflush(stdout);
	osd_led_service_start();

	for (int mask : kSequence) {
		printf("[LEDTEST] request mask=%d\n", mask);
		fflush(stdout);
		osd_set_leds(mask);

		const int got = osd_get_leds();
		if (got != mask) {
			printf("[LEDTEST] FAIL osd_get_leds()=%d after requesting %d\n", got, mask);
			fflush(stdout);
		}

		// Long enough for at least one 250ms heartbeat to fire.
		for (int i = 0; i < 40; i++) {      // 40 * 15ms = 600ms
			EvdevInput_Poll();
			struct timespec ts { 0, 15 * 1000 * 1000 };
			nanosleep(&ts, nullptr);
		}
	}

	printf("[LEDTEST] stopping LED service\n");
	fflush(stdout);
	osd_led_service_stop();
	printf("[LEDTEST] done\n");
	fflush(stdout);
	return 0;
}
```

If `EvdevInput_Poll()` is not already declared in this file, add `extern void EvdevInput_Poll();` alongside the LED declarations. If the file already has an init path it calls before polling (e.g. `EvdevInput_Init()`), call `run_led_test()` **after** that init, not instead of it.

- [ ] **Step 2: Wire the flag into argument parsing**

In `main()`, alongside the existing flags, add a branch so `--leds` runs `run_led_test()` and returns, skipping the normal report loop.

- [ ] **Step 3: Build**

```bash
cmake --build build-linux --target aae_inputtest -j
```

Expected: builds and links clean, because Task 2 moved the `osd_*` symbols into `evdev_input.cpp`, which this target links.

- [ ] **Step 4: Run it and confirm it does nothing yet**

```bash
./build-linux/aae_inputtest --leds
```

Expected: the `[LEDTEST] request mask=` lines print and the program exits 0. Nothing lights up — the implementation is still stubs. That is the point.

- [ ] **Step 5: Commit**

```bash
git add aae/inputtest/inputtest_main.cpp && git commit -m "test(input): add --leds mode driving a deterministic LED sequence"
```

---

### Task 4: Teach the uinput harness about LEDs

**Files:**
- Modify: `tools/linux/uinput_devices.cpp`

- [ ] **Step 1: Add an LED capability parameter to `create_device`**

Change the signature (currently `:249-254`) to take LED codes, following the existing `keyCodes`/`relCodes` style:

```cpp
static bool create_device(VirtualDevice& dev,
                          const std::vector<int>& keyCodes,
                          const std::vector<int>& relCodes,
                          const std::vector<AbsAxis>& absAxes,
                          const std::vector<int>& ledCodes,
                          bool wantRumble,
                          unsigned short vendor, unsigned short product)
```

After the `absAxes` block (ends `:288`), before the `wantRumble` block, add:

```cpp
	// EV_LED is an OUTPUT capability: events flow from the client INTO this
	// process, arriving on the uinput fd exactly like force-feedback requests.
	// A keyboard that does not advertise these bits gets no LED writes at all,
	// so this is what makes the LED path testable.
	if (!ledCodes.empty()) {
		ioctl(dev.fd, UI_SET_EVBIT, EV_LED);
		for (int c : ledCodes) ioctl(dev.fd, UI_SET_LEDBIT, c);
	}
```

- [ ] **Step 2: Give the two keyboards LEDs and fix the other call sites**

At `:380-381`, the two keyboards become:

```cpp
	static const std::vector<int> kbdLeds = { LED_NUML, LED_CAPSL, LED_SCROLLL };
	if (!create_device(g_devices[0], fullKeyboard, {}, {}, kbdLeds, false, 0xAAE0, 0x0001)) return false;
	if (!create_device(g_devices[1], fullKeyboard, {}, {}, kbdLeds, false, 0xAAE0, 0x0002)) return false;
```

Add `{}` in the new LED position to the mouse call (`:386`) and the pad call (`:400`).

- [ ] **Step 3: Record LED events in the device service pump**

`service_ff` (`:432`) already reads the uinput fd. Rename it `service_device` and add an LED branch. Insert before the closing brace of the read loop, as a sibling of the `EV_FF` branch:

```cpp
		else if (ev.type == EV_LED) {
			// A client's write to /dev/input/eventN arrives here. Record it as
			// well as printing it: --ledtest asserts on the recorded stream.
			LedEvent rec;
			rec.device = dev.shortName;
			rec.code   = ev.code;
			rec.value  = ev.value;
			g_ledEvents.push_back(rec);

			const char* which =
				ev.code == LED_NUML    ? "NUM"   :
				ev.code == LED_CAPSL   ? "CAPS"  :
				ev.code == LED_SCROLLL ? "SCROLL": "?";
			printf("[LED] %-5s %-6s %s\n", dev.shortName.c_str(), which,
			       ev.value ? "ON" : "off");
			fflush(stdout);
		}
```

Declare the record type and store near `g_devices` (`:178`):

```cpp
// Every EV_LED event any client wrote to any of our devices, in arrival order.
struct LedEvent {
	std::string device;
	int code;
	int value;
};
static std::vector<LedEvent> g_ledEvents;
```

- [ ] **Step 4: Pump every device, not just the pad**

`sleep_servicing_ff` (`:494-503`) only services the pad, so LED writes to the keyboards would never be read. Replace it with:

```cpp
// Sleep while continuing to service every device.
//
// Two different things need this. A client's EVIOCSFF blocks in the kernel
// until this process completes the UI_BEGIN_FF_UPLOAD handshake, so a script
// that merely slept would leave joystick_set_rumble() hanging. And EV_LED
// events written to a keyboard are only observable by reading that keyboard's
// uinput fd -- servicing only the pad, as this did originally, made LED output
// invisible.
static void sleep_servicing_devices(int ms)
{
	while (ms > 0 && !g_stop) {
		for (VirtualDevice& d : g_devices)
			if (d.fd >= 0) service_device(d);
		const int slice = ms < 10 ? ms : 10;
		msleep(slice);
		ms -= slice;
	}
}
```

Update the call site at `:521` (`sleep` script command) and any other callers to the new name.

- [ ] **Step 5: Report EV_LED in the selftest capability line**

In `selftest()` (`:615-621`), add the LED capability alongside the others:

```cpp
		const bool hasKey = HAS_BIT(evbits, EV_KEY);
		const bool hasRel = HAS_BIT(evbits, EV_REL);
		const bool hasAbs = HAS_BIT(evbits, EV_ABS);
		const bool hasFF  = HAS_BIT(evbits, EV_FF);
		const bool hasLed = HAS_BIT(evbits, EV_LED);
		printf("  %-6s %s  caps:%s%s%s%s%s\n", d.shortName.c_str(), d.eventPath.c_str(),
		       hasKey ? " EV_KEY" : "", hasRel ? " EV_REL" : "",
		       hasAbs ? " EV_ABS" : "", hasFF ? " EV_FF" : "",
		       hasLed ? " EV_LED" : "");

		// The keyboards must advertise LEDs or the LED path cannot be tested.
		if ((d.shortName == "kbd1" || d.shortName == "kbd2") && !hasLed) {
			printf("  FAIL %-6s advertises no EV_LED\n", d.shortName.c_str());
			ok = false;
		}
```

- [ ] **Step 6: Add the `--ledtest` mode**

Add this function before `main()`:

```cpp
// Assert the LED stream produced by a program run under --exec.
//
// Deliberately checks PROPERTIES rather than an exact event-by-event script:
// the heartbeat means the same mask legitimately arrives many times, so an
// exact-sequence assertion would be testing the timer, not the mapping.
//
// The requested sequence is aae_inputtest --leds: 0, 1, 2, 4, 7, 0.
static bool ledtest_verify()
{
	bool ok = true;
	printf("\nLED verification (%zu events recorded)\n", g_ledEvents.size());

	if (g_ledEvents.empty()) {
		printf("  FAIL no EV_LED events arrived at all - the backend wrote nothing\n");
		return false;
	}

	// 1. Every LED we map must have been exercised at least once.
	for (int code : { LED_NUML, LED_CAPSL, LED_SCROLLL }) {
		bool seen = false;
		for (const LedEvent& e : g_ledEvents)
			if (e.code == code && e.value) { seen = true; break; }
		if (!seen) {
			printf("  FAIL LED code %d never turned on\n", code);
			ok = false;
		}
	}

	// 2. Both keyboards must have received the same events. One keyboard
	//    getting everything and the other nothing is the classic "only wrote
	//    to the first device" bug.
	size_t n1 = 0, n2 = 0;
	for (const LedEvent& e : g_ledEvents) {
		if (e.device == "kbd1") n1++;
		else if (e.device == "kbd2") n2++;
	}
	if (n1 == 0 || n2 == 0 || n1 != n2) {
		printf("  FAIL fan-out uneven: kbd1 got %zu events, kbd2 got %zu\n", n1, n2);
		ok = false;
	} else {
		printf("  PASS both keyboards received %zu events each\n", n1);
	}

	// 3. The last state written to each LED must be OFF, because the sequence
	//    ends at mask 0 and the service then restores its snapshot (all off,
	//    since these devices are created with every LED clear).
	bool leftOff = true;
	for (int code : { LED_NUML, LED_CAPSL, LED_SCROLLL }) {
		int last = -1;
		for (const LedEvent& e : g_ledEvents)
			if (e.device == "kbd1" && e.code == code) last = e.value;
		if (last > 0) {
			printf("  FAIL LED code %d left ON after service stop\n", code);
			ok = false;
			leftOff = false;
		}
	}
	if (leftOff) printf("  PASS all LEDs left off after service stop\n");

	// 4. Upper bound on volume.
	//
	//    READ THIS BEFORE CHANGING THE NUMBER. The kernel drops an EV_LED
	//    write whose value already matches the LED's current state --
	//    input_handle_event passes EV_LED through only when
	//    !!test_bit(code, dev->led) != value. So re-applying an UNCHANGED mask
	//    is invisible here no matter how often it happens, and this check
	//    cannot measure the heartbeat's rate. Property 5 tests the heartbeat
	//    directly instead.
	//
	//    What this bound still catches is a mask being rewritten with
	//    alternating values (a toggle bug), which would produce traffic
	//    without bound. Six mask changes across three LEDs on two keyboards
	//    cannot legitimately exceed a few dozen events.
	if (n1 > 200) {
		printf("  FAIL kbd1 got %zu events; six mask changes cannot produce "
		       "that many. Something is toggling LEDs rather than setting "
		       "them.\n", n1);
		ok = false;
	}

	printf("%s\n", ok ? "LED VERIFICATION PASSED" : "LED VERIFICATION FAILED");
	return ok;
}
```

Property 3 above proves the LEDs end up off, but not that `osd_led_service_stop()` *restored* a snapshot rather than simply clearing to zero — the synthetic devices start with every LED clear, so the two are indistinguishable. To tell them apart, `--ledtest` sets a non-zero state before launching the child:

```cpp
// Turn CapsLock on before the child runs, so the service's start-time snapshot
// is non-zero and "restore" becomes distinguishable from "clear to zero".
//
// Best-effort: whether the input core reflects a write back through EVIOCGLED
// for a uinput device is not guaranteed, so a pre-state that does not stick
// downgrades this to SKIP rather than failing the run.
static bool ledtest_set_prestate()
{
	VirtualDevice* kbd = find_device("kbd1");
	if (!kbd || kbd->eventPath.empty()) return false;

	const int fd = open(kbd->eventPath.c_str(), O_RDWR | O_NONBLOCK);
	if (fd < 0) return false;

	input_event evs[2] {};
	evs[0].type = EV_LED; evs[0].code = LED_CAPSL; evs[0].value = 1;
	evs[1].type = EV_SYN; evs[1].code = SYN_REPORT; evs[1].value = 0;
	const bool wrote = write(fd, evs, sizeof(evs)) == (ssize_t)sizeof(evs);

	unsigned long ledBits[(LED_MAX + 8 * sizeof(long)) / (8 * sizeof(long))] = {0};
	bool stuck = false;
	if (wrote && ioctl(fd, EVIOCGLED(sizeof(ledBits)), ledBits) >= 0)
		stuck = HAS_BIT(ledBits, LED_CAPSL) != 0;

	close(fd);
	return stuck;
}
```

Call it before the `--exec` child starts, remember the result, and after `ledtest_verify()` add:

```cpp
	if (!prestateStuck) {
		printf("  SKIP restore check - the pre-run LED state did not stick, so "
		       "restore cannot be distinguished from clear-to-zero here\n");
	} else {
		int last = -1;
		for (const LedEvent& e : g_ledEvents)
			if (e.device == "kbd1" && e.code == LED_CAPSL) last = e.value;
		if (last == 1)
			printf("  PASS CapsLock restored to its pre-run state\n");
		else {
			printf("  FAIL CapsLock was ON before the run and is %s after; "
			       "osd_led_service_stop() cleared instead of restoring\n",
			       last == 0 ? "off" : "untouched");
			ok = false;
		}
	}
```

Note this makes property 3's "all LEDs left off" expectation apply only to Num and Scroll when the pre-state stuck — guard it accordingly so the two checks do not contradict each other.

`HAS_BIT` is defined inside `selftest()`; hoist it to file scope next to the other helpers so both functions can use it.

Wire a `--ledtest` mode into `main()` that creates the devices, runs the `--exec` child exactly as the existing modes do, then calls `ledtest_verify()` and returns 0/1 from it. While waiting on the child it must call `sleep_servicing_devices()` rather than plain sleeping, or no LED events will ever be read.

- [ ] **Step 7: Build**

```bash
cmake --build build-linux --target aae_uinput_test -j
```

Expected: builds clean.

- [ ] **Step 8: Commit**

```bash
git add tools/linux/uinput_devices.cpp && git commit -m "test(uinput): advertise EV_LED and verify LED output"
```

---

### Task 5: RED gate — confirm the test fails for the right reason

No files change. This proves the harness detects the missing feature.

- [ ] **Step 1: Run the harness against the current stubs**

```bash
sudo ./build-linux/aae_uinput_test --ledtest --exec "./build-linux/aae_inputtest --leds"
```

- [ ] **Step 2: Confirm the failure and its reason**

Expected: `aae_inputtest` prints its `[LEDTEST] request mask=` lines, **no** `[LED]` lines appear, and verification ends with:

```
  FAIL no EV_LED events arrived at all - the backend wrote nothing
LED VERIFICATION FAILED
```

Exit code 1. That is correct — the Linux `osd_*` functions are still stubs.

If instead it fails with undefined references at link time, complete Task 2 Step 3's deferred note now by proceeding to Tasks 5-6 and returning here.

If `[LED]` lines *do* appear, stop: something already implements this and the plan's premise is wrong.

---

### Task 6: LED capability and writes in `EvdevDevice`

**Files:**
- Modify: `aae/system/input/linux/evdev_device.h`
- Modify: `aae/system/input/linux/evdev_device.cpp`

- [ ] **Step 1: Declare the LED surface**

In `evdev_device.h`, after the force-feedback block (`:71-78`), add:

```cpp
	// Keyboard indicator LEDs. AAE drives these as stand-ins for a cabinet's
	// start-button lamps. SupportsLeds() is false when the device has no LED
	// bits OR when the node could only be opened read-only -- two different
	// causes with the same symptom, logged apart, exactly as rumble does.
	//
	// The mask is AAE's own: bit0 = NumLock, bit1 = CapsLock, bit2 = ScrollLock.
	bool SupportsLeds() const { return m_hasLeds && m_writable; }
	bool SetLeds(int mask);
	int  GetLeds() const;
```

And with the other members (after `m_hasRumble`, `:91`):

```cpp
	bool        m_hasLeds     = false;
```

- [ ] **Step 2: Probe the capability in `Classify()`**

In `evdev_device.cpp::Classify()`, add the bit array alongside the others (after `:184`):

```cpp
	unsigned long ledBits[BitsToLongs(LED_MAX + 1)] = {0};
```

After the `hasEvFF` line (`:190`), add:

```cpp
	const bool hasEvLed = TestBit(evBits, EV_LED);
```

After the `EVIOCGBIT(EV_FF, ...)` call (`:195`), add:

```cpp
	if (hasEvLed) ioctl(m_fd, EVIOCGBIT(EV_LED, sizeof(ledBits)), ledBits);
```

After the `m_hasRumble` assignment (`:197`), add:

```cpp
	// Test the specific indicators AAE drives, not merely the EV_LED type bit:
	// plenty of devices report EV_LED for things that are not these three.
	m_hasLeds = hasEvLed && (TestBit(ledBits, LED_NUML)  ||
	                         TestBit(ledBits, LED_CAPSL) ||
	                         TestBit(ledBits, LED_SCROLLL));
```

- [ ] **Step 3: Report it in the capability log and warn on read-only**

Extend the `LOG_INFO` at `:220-225` to carry `leds=%d`:

```cpp
	LOG_INFO("evdev: %s '%s' -> %s (letters=%d rel=%d abs=%d ff_rumble=%d "
	         "leds=%d writable=%d identity=%s%s)",
	         m_devNode.c_str(), m_name.c_str(), EvdevKindName(m_kind),
	         letters, mouseAxes ? 1 : 0, padAxes ? 1 : 0, m_hasRumble ? 1 : 0,
	         m_hasLeds ? 1 : 0, m_writable ? 1 : 0, m_identity.c_str(),
	         m_weakIdentity ? " WEAK" : "");
```

After the rumble warning (`:232-236`), add its twin:

```cpp
	if (m_hasLeds && !m_writable) {
		LOG_WARN("evdev: '%s' has keyboard LEDs but opened read-only, so they "
		         "cannot be driven - add your user to the 'input' group or set a "
		         "udev rule for %s", m_name.c_str(), m_devNode.c_str());
	}
```

- [ ] **Step 4: Reset the flag in `Close()`**

In `Close()`, alongside `m_hasRumble = false;` (`:157`), add:

```cpp
	m_hasLeds   = false;
```

- [ ] **Step 5: Implement the write and read**

After `StopRumble()`, add:

```cpp
//------------------------------------------------------------------------------
// Write all three indicators. The mask is AAE's (bit0 Num, bit1 Caps, bit2
// Scroll); the mapping to LED_* is written out rather than relying on those
// constants happening to be 0, 1 and 2.
//
// All three are always written, including the ones that are off: this is the
// only way to turn an LED back off, and libinput may have changed any of them
// since the last call.
//------------------------------------------------------------------------------
bool EvdevDevice::SetLeds(int mask)
{
	if (!SupportsLeds()) return false;

	static const struct { int bit; uint16_t code; } kMap[] = {
		{ 1 << 0, LED_NUML   },
		{ 1 << 1, LED_CAPSL  },
		{ 1 << 2, LED_SCROLLL },
	};

	input_event evs[4] {};
	int n = 0;
	for (const auto& m : kMap) {
		evs[n].type  = EV_LED;
		evs[n].code  = m.code;
		evs[n].value = (mask & m.bit) ? 1 : 0;
		n++;
	}
	evs[n].type  = EV_SYN;
	evs[n].code  = SYN_REPORT;
	evs[n].value = 0;
	n++;

	const ssize_t want = (ssize_t)(sizeof(input_event) * n);
	if (write(m_fd, evs, sizeof(input_event) * n) != want) {
		LOG_WARN("evdev: writing LEDs to '%s' failed: %s",
		         m_name.c_str(), strerror(errno));
		return false;
	}
	return true;
}

//------------------------------------------------------------------------------
// Current indicator state as an AAE mask. Used once, to snapshot what the
// desktop had set before AAE starts driving the LEDs.
//------------------------------------------------------------------------------
int EvdevDevice::GetLeds() const
{
	if (m_fd < 0) return 0;

	unsigned long ledBits[BitsToLongs(LED_MAX + 1)] = {0};
	if (ioctl(m_fd, EVIOCGLED(sizeof(ledBits)), ledBits) < 0) return 0;

	int mask = 0;
	if (TestBit(ledBits, LED_NUML))    mask |= (1 << 0);
	if (TestBit(ledBits, LED_CAPSL))   mask |= (1 << 1);
	if (TestBit(ledBits, LED_SCROLLL)) mask |= (1 << 2);
	return mask;
}
```

`TestBit` and `BitsToLongs` live in the anonymous namespace at the top of this file, so both are already in scope.

- [ ] **Step 6: Build**

```bash
cmake --build build-linux -j
```

Expected: builds clean. Nothing calls the new methods yet.

- [ ] **Step 7: Commit**

```bash
git add aae/system/input/linux/evdev_device.h aae/system/input/linux/evdev_device.cpp && git commit -m "feat(evdev): per-device keyboard LED capability and writes"
```

---

### Task 7: Fill in the four `osd_*` entry points

**Files:**
- Modify: `aae/system/input/linux/evdev_input.cpp`

- [ ] **Step 1: Replace the Task 2 stubs with the real implementation**

In `evdev_input.cpp`, replace the four stub bodies added in Task 2 —

```cpp
void osd_led_service_start() {}
void osd_led_service_stop()  {}
void osd_set_leds(int)       {}
int  osd_get_leds()          { return 0; }
```

— with the state and implementations below. Keep the four forward declarations above them, and update the block comment: the "Stubs for now" sentence is replaced by the threading note.

```cpp
// Unlike the Win32 implementation there is NO service thread. That one needs
// one because IOCTL_KEYBOARD_SET_INDICATORS goes through SetupAPI handles that
// can block; here it is a write() to an fd that is already open, so it runs
// inline on the game thread with no locking to get wrong.

static bool s_ledServiceActive = false;
static int  s_ledDesiredMask   = 0;

// Per-device LED state as it was before AAE started driving it, so quitting
// does not leave a desktop user's NumLock light wrong. Indexed like s_devices.
static std::vector<int> s_ledSnapshot;

// Write the desired mask to every device that can take it.
static void ApplyLeds()
{
	for (size_t i = 0; i < s_devices.size(); i++) {
		if (!s_devices[i].IsOpen()) continue;
		if (!s_devices[i].SupportsLeds()) continue;
		s_devices[i].SetLeds(s_ledDesiredMask);
	}
}

void osd_led_service_start()
{
	if (s_ledServiceActive) return;

	s_ledSnapshot.assign(s_devices.size(), 0);
	int targets = 0;
	for (size_t i = 0; i < s_devices.size(); i++) {
		if (!s_devices[i].IsOpen() || !s_devices[i].SupportsLeds()) continue;
		s_ledSnapshot[i] = s_devices[i].GetLeds();
		targets++;
	}

	s_ledServiceActive = true;
	s_ledDesiredMask   = 0;

	if (targets == 0)
		LOG_WARN("evdev: LED service started but no device can take LED writes - "
		         "game lamps will not light (check 'input' group membership)");
	else
		LOG_INFO("evdev: LED service started, %d device(s)", targets);

	ApplyLeds();
}

void osd_led_service_stop()
{
	if (!s_ledServiceActive) return;
	s_ledServiceActive = false;

	// Restore what the desktop had, rather than clearing to zero as the Win32
	// path does. Deliberate divergence -- see the spec.
	for (size_t i = 0; i < s_devices.size() && i < s_ledSnapshot.size(); i++) {
		if (!s_devices[i].IsOpen() || !s_devices[i].SupportsLeds()) continue;
		s_devices[i].SetLeds(s_ledSnapshot[i]);
	}
	s_ledSnapshot.clear();
	s_ledDesiredMask = 0;
}

void osd_set_leds(int state)
{
	s_ledDesiredMask = state & 0x07;      // Num | Caps | Scroll
	if (!s_ledServiceActive) return;
	ApplyLeds();
}

// Reports what was requested, not what the hardware currently shows -- the
// same semantic as the Win32 implementation.
int osd_get_leds()
{
	return s_ledDesiredMask;
}
```

If `<vector>` is not already included by `evdev_input.cpp`, add it.

- [ ] **Step 2: Build**

```bash
cmake --build build-linux -j
```

Expected: clean. `led_service_handler.cpp` is untouched by this task — Task 2 already removed its Linux stubs, so there is no duplicate-symbol risk here.

- [ ] **Step 3: Run the harness**

```bash
cmake --build build-linux --target aae_inputtest -j && cmake --build build-linux --target aae_uinput_test -j
```

```bash
sudo ./build-linux/aae_uinput_test --ledtest --exec "./build-linux/aae_inputtest --leds"
```

Expected: `[LED]` lines now appear on both keyboards, and the per-code and fan-out properties pass. The event-volume property may report a low count until Task 8 adds the heartbeat — that is fine; only a count of **zero** would indicate a real problem at this stage.

- [ ] **Step 4: Commit**

```bash
git add aae/system/input/linux/evdev_input.cpp && git commit -m "feat(evdev): implement the OSD LED contract on Linux"
```

---

### Task 8: Re-apply heartbeat and pause handling

**Files:**
- Modify: `aae/system/input/linux/evdev_input.cpp`

- [ ] **Step 1: Add the heartbeat to `EvdevInput_Poll()`**

Nothing calls `EVIOCGRAB`, so libinput owns these LEDs too and resets them on keyboard activity. In `EvdevInput_Poll()` (`:456`), after the device read loop closes (`:488`) and before the hotplug rescan block (`:490`), add:

```cpp
	// Re-apply the LED mask periodically. The devices are NOT grabbed, so
	// libinput drives the same indicators and will overwrite anything written
	// here the moment a key is pressed.
	//
	// Windows needs no equivalent and does not have one: its worker applies
	// only when the mask CHANGES (led_service_handler.cpp:309) and its 250ms
	// wait is a clean-shutdown wake that re-applies nothing (:302). There the
	// keyboard class driver holds the indicator state; here another client
	// owns the same LEDs. Do not describe this as matching a Windows cadence.
	//
	// 250ms is chosen on its own merits: fast enough that a clobbered LED
	// corrects within about a quarter second, cheap enough to be irrelevant
	// beside the per-frame read loop above.
	//
	// While paused, the desktop gets its LEDs back rather than being fought
	// for control of them by a program that is not in the foreground.
	if (s_ledServiceActive) {
		static int  ledCountdown = 0;
		static bool ledWasPaused = false;

		if (s_paused != ledWasPaused) {
			ledWasPaused = s_paused;
			for (size_t i = 0; i < s_devices.size() && i < s_ledSnapshot.size(); i++) {
				if (!s_devices[i].IsOpen() || !s_devices[i].SupportsLeds()) continue;
				s_devices[i].SetLeds(s_paused ? s_ledSnapshot[i] : s_ledDesiredMask);
			}
			ledCountdown = 0;
		}

		if (!s_paused && --ledCountdown <= 0) {
			ApplyLeds();
			ledCountdown = 15;      // ~250ms at 60fps
		}
	}
```

- [ ] **Step 2: Reset the heartbeat when the mask changes**

`osd_set_leds` already applies immediately, so no change is needed — the heartbeat merely re-asserts the same value. Confirm by reading the function; if it were changed to defer to the heartbeat, a mask change would take up to 250ms to appear.

- [ ] **Step 3: Apply LEDs to a device that reattaches**

A keyboard unplugged and replugged mid-game comes back with its LEDs clear. In `ScanDevices()`, at the re-open-in-place branch (`:346`), after a successful `Open()`, add:

```cpp
				// A device that just came back has clear LEDs; put the current
				// lamp state back on it rather than waiting for a mask change
				// that may never come.
				if (s_ledServiceActive && s_devices[existing].SupportsLeds())
					s_devices[existing].SetLeds(s_ledDesiredMask);
```

The newly-opened-device branch at `:367` needs the same treatment, but that code path operates on a local `dev` object **before** it is moved into `s_devices`. Add the equivalent there against that local:

```cpp
		// Same reasoning as the reattach branch above: a device joining
		// mid-game must be brought up to the current lamp state.
		if (s_ledServiceActive && dev.SupportsLeds())
			dev.SetLeds(s_ledDesiredMask);
```

Place it after the `Open()` succeeds and after `Classify()` has run (LED capability is unknown before classification), and before the object is pushed into `s_devices`.

`s_ledSnapshot` is indexed like `s_devices`, and a device appended after `osd_led_service_start()` has no snapshot entry. Every loop that reads it is bounded on **both** sizes for exactly this reason — do not "simplify" those conditions to a single bound. A device with no snapshot entry simply is not restored, which is correct: AAE never saw its pre-run state.

- [ ] **Step 4: Build and run the harness**

```bash
cmake --build build-linux --target aae_inputtest -j && cmake --build build-linux --target aae_uinput_test -j
```

```bash
sudo ./build-linux/aae_uinput_test --ledtest --exec "./build-linux/aae_inputtest --leds"
```

Expected: `LED VERIFICATION PASSED`, exit code 0.

- [ ] **Step 5: Commit**

```bash
git add aae/system/input/linux/evdev_input.cpp && git commit -m "feat(evdev): hold LED state against libinput with a re-apply heartbeat"
```

---

### Task 9: GREEN gate — full verification

No files change.

- [ ] **Step 1: Harness selftest**

```bash
sudo ./build-linux/aae_uinput_test --selftest
```

Expected: every device line lists its capabilities, `kbd1` and `kbd2` both show `EV_LED`, no `FAIL` lines.

- [ ] **Step 2: End-to-end LED verification**

```bash
sudo ./build-linux/aae_uinput_test --ledtest --exec "./build-linux/aae_inputtest --leds"
```

Expected: `[LEDTEST]` request lines interleaved with `[LED]` lines on **both** `kbd1` and `kbd2`, then:

```
  PASS both keyboards received N events each
  PASS all LEDs left off after service stop
LED VERIFICATION PASSED
```

Exit code 0.

- [ ] **Step 3: Confirm the read-only path is reported, not silent**

```bash
sudo chmod 0444 $(ls /dev/input/event* | head -1) && ./build-linux/aae_inputtest --leds 2>&1 | grep -i "read-only\|input.*group"
```

Expected: the Task 5 Step 3 warning names the device and the `input` group. Restore permissions afterwards (the uinput nodes vanish with the harness, but a real node needs `chmod 0660` back).

- [ ] **Step 4: Windows regression**

```bash
msbuild aae.sln -p:Configuration=Debug -p:Platform=x64 -v:minimal
```

Run Asteroids on Windows and confirm the 1P/2P start lamps still drive Num/Caps as they did before Task 1. The whole Windows path should be untouched apart from where the neutral trio now lives.

- [ ] **Step 5: Real-hardware pass (owed, not blocking)**

On the Pi 5, run Asteroids and confirm the physical Num/Caps LEDs follow the 1P/2P start lamps, that pressing a key does not leave them stuck (the heartbeat wins), and that quitting restores the pre-run LED state.

This is the one thing the harness cannot prove. Record the result in the spec if it differs from the harness outcome.

---

## Notes for the implementer

- **Do not add a service thread.** The Windows one exists because its IOCTL path can block. Here it is a `write()` to an open fd from the thread that already owns the device table; a thread would add locking for nothing.
- **Do not classify by device kind.** LED capability comes from the EV_LED bits. The evdev backend's whole classification design refuses to trust names or kinds, and an LED-capable node should get writes regardless of how it was classified.
- **Write all three indicators every time**, including the off ones — it is the only way to turn one back off, and libinput may have changed any of them since the last write.
- **`osd_get_leds()` returns the requested mask**, not the hardware state, matching Windows. Do not "improve" it to read `EVIOCGLED`.
- The flatpak manifest needs **no** change; `--device=input` and `--device=all` are already present at lines 52 and 55.
