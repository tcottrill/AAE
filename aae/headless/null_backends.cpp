// -----------------------------------------------------------------------------
// null_backends.cpp
//
// Task 6 (Phase 3a) - the headless target's OSD layer. Two things live here:
//
//   1. NullWindow: a real ISystemWindow (aae/system/window/sys_window.h)
//      that deliberately does NOT override Presentation(), so it inherits
//      the base class's `return nullptr`. That is the documented, honest way
//      for a backend with no display to satisfy the window contract - it is
//      exactly what a Teensy backend would do too.
//
//   2. OSD stubs, added one at a time as the linker asked for them while
//      bringing up aae_headless. Each is annotated with why aae_core reaches
//      it even though nothing here ever presents a frame. The complete set
//      is the Teensy porting checklist - see the Task 6 report for the
//      grouped list.
// -----------------------------------------------------------------------------
#include "sys_window.h"
#include "osdepend.h"
#include "sys_log.h"
#include "path_helper.h"
#include "fileio/mame_fileio.h"   // readint/writeint/readword/writeword
#include "sys_fileio.h"           // fileExistsReadable/getLastFileSize/loadFile/saveFile/loadZip
#include "iniFile.h"              // get_config_string
#include "mixer.h"                // load_sample_from_buffer/load_silent_sample
#include "cpu_code/ccpu.h"        // run_ccpu/ccpu_reset
#include "joystick.h"             // JOYSTICK_INFO, num_joysticks, joy[], install_joystick, poll_joystick, joystick_find_by_id
#include "sys_input.h"            // mouse_b, key[], get_mouse_mickeys_ex, RawInput_*
#include "led_service_handler.h"  // set_led_status (distinct from osd_set_leds)
#include "fileio/texture_handler.h" // game_tex[10]

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdlib>

// =============================================================================
// 1. NullWindow
// =============================================================================
class NullWindow : public ISystemWindow {
public:
	bool Create(const WindowSetup&) override { return true; }
	void Destroy() override {}

	bool PumpEvents() override { return true; }

	int   ClientWidth()  const override { return 0; }
	int   ClientHeight() const override { return 0; }
	float DpiScale()     const override { return 1.0f; }

	void ToggleBorderlessFullscreen() override {}
	void RestoreViewport() override {}

	void SetCursorVisible(bool) override {}
	void EnableCursorClip(bool) override {}
	void ForceCursorClipUpdate() override {}
	void SetMousePos(int, int) override {}
	void GetMousePos(int* x, int* y) const override { if (x) *x = 0; if (y) *y = 0; }

	// Presentation() deliberately NOT overridden - inherits ISystemWindow's
	// `return nullptr`, which is the headless/Teensy contract.
};

static NullWindow g_nullWindow;

ISystemWindow& GetSystemWindow()
{
	return g_nullWindow;
}

static WindowSetup g_windowSetup;

WindowSetup& GetWindowSetup()
{
	return g_windowSetup;
}

// =============================================================================
// 2. OSD stubs - Teensy porting checklist
// =============================================================================

// --- logging --------------------------------------------------------------
// sys_log.cpp (the real Log::write/open/close) is executable-side (does
// Windows console coloring). LOG_INFO/LOG_ERROR/LOG_WARN/LOG_DEBUG are used
// throughout aae_core, so Log::write needs a body. Sent to stdout so the
// headless run's own progress prints aren't drowned out by interleaving with
// stderr buffering differences.
namespace Log {
	void write(Level level, const char* file, const char* function, int line, const char* format, ...)
	{
		(void)file; (void)function; (void)line;
		const char* tag = "INFO";
		switch (level)
		{
		case Level::Debug: tag = "DEBUG"; break;
		case Level::Info:  tag = "INFO";  break;
		case Level::Warn:  tag = "WARN";  break;
		case Level::Error: tag = "ERROR"; break;
		default: break;
		}
		char buf[1024];
		va_list args;
		va_start(args, format);
		vsnprintf(buf, sizeof(buf), format, args);
		va_end(args);
		std::printf("[%s] %s\n", tag, buf);
	}

	bool open(const std::string&) { return true; }
	void close() {}
	void setLevel(Level) {}
	void setConsoleOutputEnabled(bool) {}
}

// --- path helper ------------------------------------------------------------
// path_helper.cpp (the real getpathM) is executable-side: it resolves paths
// relative to the running .exe via GetModuleFileName. That is an OSD/platform
// concern the emulation core (aae_fileio.cpp's load_roms(), by way of
// verify_rom()/getpathM() fallback) should not need. The headless target has
// no "exe directory" concept to speak of, so this stub resolves relative to
// the process's CURRENT WORKING DIRECTORY instead - which is why aae_headless
// must be launched from x64/Release (see the Task 6 report for the exact
// command).
std::string getpathM(const char* dir, const char* file)
{
	std::string result = dir ? dir : "";
	if (file && file[0])
	{
		result += "\\";
		result += file;
	}
	return result;
}

// --- LEDs -------------------------------------------------------------------
// osdepend.h documents LEDs as win:yes linux:STUB teensy:STUB. os_input.cpp
// (aae_core) calls osd_set_leds(0) during os_init_input(), which the headless
// loop does not call - but led_service_handler.cpp (which implements these on
// the executable side) is a WIP file the task brief says not to touch, and is
// not part of aae_core, so a headless-side stub is the right home regardless.
void osd_set_leds(int) {}
void osd_led_service_start() {}
void osd_led_service_stop() {}
int  osd_get_leds() { return 0; }

// --- persisted config/hiscore file I/O --------------------------------------
// osd_fopen/fread/fwrite/fclose (mame_fileio.cpp) and readint/writeint/
// readword/writeword (also mame_fileio.cpp) are executable-side. None of
// them are on the path headless_main.cpp actually drives (it skips
// load_input_port_settings()/save_input_port_settings() and NVRAM entirely),
// but inptport.cpp's load_default_keys()/save_default_keys() and
// aae_fileio.cpp's load_hi_aae() call them unconditionally at file scope, so
// the whole translation unit needs them resolved at link time regardless of
// what headless_main.cpp calls at runtime. Returning "no file" / no-op is
// enough since these bodies never execute here.
void* osd_fopen(const char*, const char*, int, int) { return nullptr; }
int   osd_fread(void*, void*, int) { return 0; }
int   osd_fread_swap(void*, void*, int) { return 0; }
int   osd_fread_scatter(void*, void*, int, int) { return 0; }
int   osd_fwrite(void*, const void*, int) { return 0; }
int   osd_fseek(void*, int, int) { return 0; }
unsigned int osd_fcrc(void*) { return 0; }
void  osd_fclose(void*) {}

int  readint(void*, UINT32* num) { if (num) *num = 0; return -1; }
void writeint(void*, UINT32) {}
int  readword(void*, UINT16* num) { if (num) *num = 0; return -1; }
void writeword(void*, UINT16) {}

// --- ROM/sample zip + generic file access -----------------------------------
// sys_fileio.cpp (loadZip/getLastZ*/loadFile/saveFile/fileExistsReadable) is
// executable-side. load_roms() - the function this whole exercise is
// built around - does NOT go through this path (it drives miniz directly;
// see the CMakeLists.txt comment on why aae/system/3rdparty/miniz.c is
// compiled straight into aae_headless). These are only reachable from
// verify_rom()/load_ambient_samples()/load_hi_aae(), none of which
// headless_main.cpp calls - but, same as the group above, aae_fileio.obj is
// one translation unit and needs every external it mentions resolved.
bool fileExistsReadable(const char*) { return false; }
size_t getLastFileSize() { return 0; }
size_t getLastZSize() { return 0; }
uint32_t getLastZCrc() { return 0; }
uint8_t* loadFile(const std::string&) { return nullptr; }
uint8_t* loadFile(const char*) { return nullptr; }
bool saveFile(const char*, const unsigned char*, int) { return false; }
unsigned char* loadZip(const char*, const char*) { return nullptr; }

// get_config_string (iniFile.cpp, executable-side): reads aae.ini. Headless
// has no ini file at all, so this just hands back the caller's default,
// allocated the same way the real one documents ("caller must delete[]").
char* get_config_string(const char*, const char*, const char* szDefaultValue)
{
	const char* def = szDefaultValue ? szDefaultValue : "";
	size_t len = std::strlen(def) + 1;
	char* out = new char[len];
	std::memcpy(out, def, len);
	return out;
}

// --- sample/audio loading ----------------------------------------------------
// mixer.cpp is executable-side (owns the XAudio2/ALSA backend). Only reached
// from load_ambient_samples()/load_samples_batch(), neither of which
// headless_main.cpp calls - same whole-TU-pull-in reason as above.
int load_sample_from_buffer(const uint8_t*, size_t, const char*, bool) { return -1; }
int load_silent_sample(const char*) { return -1; }

// --- CCPU CPU core -----------------------------------------------------------
// GENUINE MISPLACED FILE, not an OSD boundary leak: ccpu.cpp (Cinematronics
// CCPU core - used by tempest/quantum/qb3/etc.) is an emulation-core CPU
// backend, indistinguishable in purpose from cpu_6502.cpp/cpu_z80.cpp/etc
// (all of which DO live in aae_core) - yet ccpu.cpp is currently compiled
// into the executable side (AAE_COMMON_SOURCES), not aae_core. That is a
// simple omission worth fixing (move ccpu.cpp into aae_core's source list)
// rather than anything this headless target should paper over long-term.
// Stubbed here only because asteroid/bzone never dispatch to CCPU, so this
// path is never actually exercised at runtime in this test.
int  run_ccpu(int) { return 0; }
void ccpu_reset() {}

// --- joystick / raw keyboard-mouse input ------------------------------------
// GENUINE BOUNDARY LEAK, and the most significant one found in this pass:
// os_input.cpp lives in aae_core and is the sole implementation of the
// osd_key_pressed()/osd_joy_pressed()/osd_poll_joysticks()/osd_analogjoy_read()/
// osd_trak_read() contract from osdepend.h - but its OWN implementation
// reaches directly into Windows RawInput/XInput internals: the `key[256]`
// and `mouse_b` globals and RawInput_FindMouseByPath/RawInput_FindKeyboardByPath/
// RawInput_IsKeyDownEx/get_mouse_mickeys_ex functions (sys_input.h, backed by
// rawinput.cpp - a Win32-only AAE_PLATFORM_SOURCES file), and the
// num_joysticks/joy[]/install_joystick/poll_joystick/joystick_find_by_id
// surface (joystick.h, backed by Joystick.cpp - also Win32-only).
//
// Worse than a link-only issue: joystick.h itself unconditionally
// #include <windows.h> and <Xinput.h> (no #ifdef guard) - so os_input.cpp,
// despite being counted as one of aae_core's 87 translation units, does not
// actually compile without Windows headers. Phase 2's "aae_core compiles
// without the OSD" was verified by omitting three directories from the
// include path; it did not (and could not, from a file list alone) catch a
// core file quietly depending on <windows.h> through a header it does
// include. On Linux/Teensy this file will not compile, not just fail to
// link - os_input.cpp needs to be split into the pseudo-key dispatch logic
// (genuinely portable) and a per-platform physical-input backend behind the
// osd_key_pressed contract, mirroring how sys_window.h/ISystemWindow already
// separates the window contract from its Win32 implementation.
//
// Stubbed here (not exercised meaningfully - asteroid/bzone read no live
// input over a fixed 600-frame headless run) purely so the link completes.
int install_joystick() { return 0; }
int poll_joystick() { return 0; }
int joystick_find_by_id(const char*) { return -1; }
void get_mouse_mickeys_ex(int, int* mickeyx, int* mickeyy) { if (mickeyx) *mickeyx = 0; if (mickeyy) *mickeyy = 0; }
int RawInput_FindMouseByPath(const char*) { return -1; }
int RawInput_FindKeyboardByPath(const char*) { return -1; }
int RawInput_IsKeyDownEx(int, int) { return 0; }

int num_joysticks = 0;
JOYSTICK_INFO joy[MAX_JOYSTICKS] = {};
int mouse_b = 0;
unsigned char key[256] = {};

// --- discrete sample playback + mixer channel/stream API --------------------
// mixer.cpp (executable-side - owns the XAudio2/ALSA backend) is where all of
// these live. Reached because asteroid.cpp/bzone.cpp are compiled directly
// into aae_headless (see the CMakeLists.txt comment on why) and their
// explosion/thrust/saucer sample effects call sample_start()/sample_stop()/
// etc. directly, and because aae_pokey.cpp (genuinely aae_core - the POKEY
// sound chip used by both games) streams its output through
// mixer_alloc_channel()/stream_start()/stream_update()/stream_stop(). No
// audio ever needs to actually play for a vector-count proof, so these are
// all no-ops/sentinels.
void set_led_status(int, int) {}
void set_led_status_all(int, int, int) {}

void sample_stop(int) {}
void sample_start(int, int, int) {}
void sample_set_volume(int, int) {}
void sample_set_volume_mixer(int, int) {}
void sample_set_freq(int, int) {}
int  sample_get_freq(int) { return 0; }
int  sample_playing(int) { return 0; }

int  mixer_alloc_channel(int, int) { return -1; }
void stream_start(int, int, int, int, bool) {}
void stream_stop(int, int) {}
void stream_update(int, short*) {}

// --- texture handle table -----------------------------------------------
// texture_handler.cpp (executable-side) owns the real GL texture objects.
// dvg_start_asteroid() (old_mame_vecsim_dvg.cpp, aae_core - called from
// asteroid.cpp's init_game()) sets game_tex[0] as the "shots" texture id via
// set_texture_id(), which is already a no-op above; this just gives that
// call somewhere valid to write to.
rtex_t game_tex[10] = {};
