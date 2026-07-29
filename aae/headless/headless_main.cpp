// -----------------------------------------------------------------------------
// headless_main.cpp
//
// Task 6 (Phase 3a) - proves aae_core can RUN with no window, no OSD, and no
// display anywhere. This is the Teensy 4.1 scenario minus the DACs: video is
// redirected into a vector sink (add_line, defined below) instead of a real
// vector monitor.
//
// This file implements its OWN minimal per-frame loop. It deliberately does
// NOT call run_game()/emulator_init()/emulator_run() from aae_emulator.cpp -
// those interleave core steps (ROM load, driver init, cpu_run) with OSD ones
// (GL init, artwork, audio, NVRAM, window aspect) that a headless/Teensy
// target has no business touching. Instead it hand-rolls the core-safe
// subset of both functions, taken from aae_emulator.cpp:
//   run_game()    (~lines 777-1067): driver select, init_machine() (inlined
//                 here), reset_memory_tracking(), load_roms(), timer_init(),
//                 driver init_game(), init_cpu_config().
//   emulator_run() (~lines 1380-1512): cpu_run(), driver run_game() callback,
//                 inputport_vblank_end(), cpu_clear_cyclecount_eof().
// -----------------------------------------------------------------------------
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "aae_mame_driver.h"   // AAEDriver, RunningMachine, Machine, driver_rom_archive()
#include "driver_registry.h"   // aae::FindDriverByName
#include "inptport.h"          // struct InputPort, IPT_END, inputport_vblank_end()
#include "memory.h"            // reset_memory_tracking(), free_all_memory_regions()
#include "fileio/aae_fileio.h" // load_roms()
#include "cpu_code/cpu_control.h" // cpu_run(), init_cpu_config(), free_cpu_memory(), cpu_clear_cyclecount_eof()
#include "cpu_code/timer.h"    // timer_init()
#include "vidhrdwr/mame_vector.h" // vector_add_point/vector_clear_list/vector_add_clip (AVG path, see below)
#include "config.h"            // config.exrompath

// ---------------------------------------------------------------------------
// The vector sink. This is exactly what a Teensy backend does instead,
// except driving DACs rather than incrementing a counter - old_mame_vecsim_dvg.cpp
// (the Cinematronics DVG path: tempest/qb3/etc.) calls these four functions
// (declared in vidhrdwr/emu_vector_draw.h, which IS in aae_core's include
// path) directly, whether a real renderer is listening or not.
//
// asteroid/bzone are Atari AVG hardware, not DVG, and DON'T go through this
// path directly - see the vector_add_point family below for why they still
// end up here.
// ---------------------------------------------------------------------------
static long g_vector_count = 0;

void add_line(float, float, float, float, int, rgb_t) { ++g_vector_count; }
void add_tex(float, float, int, rgb_t) {}
void cache_clear() {}
void set_texture_id(rtex_t*) {}

// ---------------------------------------------------------------------------
// vector_add_point / vector_add_clip / vector_clear_list
//
// DISCOVERY: there is a SECOND, parallel emu-to-renderer contract besides
// add_line/add_tex, used only by Atari AVG-family games (asteroid, bzone,
// mhavoc, ...) via aae_avg.cpp. It is documented in
// aae/aae/vidhrdwr/mame_vector.h: "drivers build the list via
// vector_add_point/.../vector_clear_list etc. The renderer-side
// implementation (mame_vector.cpp) stays in aae_video/". mame_vector.cpp is
// executable-side (AAE_COMMON_SOURCES) - it accumulates points from
// vector_add_point() into a list across the frame, then its vector_update()
// (called once per frame from aae_video/opengl_renderer.cpp - i.e. from the
// renderer's own render loop, NOT from anything run_game()/emulator_run()
// calls) drains that list through coordinate scaling/clipping and finally
// calls add_line()/add_tex() itself.
//
// Nothing in the core-safe subset this headless loop drives ever calls
// vector_update() - it is genuinely renderer-side, triggered by the OSD's
// render loop, not the emulation loop. Reimplementing its batching/transform
// logic here would mean reimplementing a renderer. Instead, this forwards
// each accumulated point straight to add_line() as a zero-length "dot" - the
// same convention old_mame_vecsim_dvg.cpp itself uses for an intensity-only
// point (see its z==0 case). That is a headless-appropriate simplification,
// not a faithful reproduction of the real antialiased/clipped beam segments,
// but it proves the same thing this whole exercise is after: the AVG
// hardware emulation in aae_avg.cpp is producing real vector output with
// nothing attached to consume it.
// ---------------------------------------------------------------------------
void vector_add_point(int x, int y, unsigned int color, int intensity)
{
	add_line((float)x, (float)y, (float)x, (float)y, intensity, (rgb_t)color);
}
void vector_add_clip(int, int, int, int) {}
void vector_clear_list() {}

// ---------------------------------------------------------------------------
// init_machine_headless
// Minimal re-implementation of aae_emulator.cpp's init_machine(): allocates
// a shadow copy of the driver's input port table in Machine->input_ports.
// Not calling into aae_emulator.cpp's real init_machine() (executable-side,
// not linkable here) - the logic is ~15 lines and copying it is far cheaper
// than trying to make that whole file link headless.
// ---------------------------------------------------------------------------
static int init_machine_headless(const AAEDriver* drv)
{
	if (!drv->input_ports)
		return 0;

	int total = 0;
	const struct InputPort* from = drv->input_ports;
	do { ++total; } while ((from++)->type != IPT_END);

	Machine->input_ports = (InputPort*)malloc(total * sizeof(struct InputPort));
	if (!Machine->input_ports)
		return 1;

	from = drv->input_ports;
	struct InputPort* to = Machine->input_ports;
	do
	{
		memcpy(to, from, sizeof(struct InputPort));
		++to;
	} while ((from++)->type != IPT_END);

	return 0;
}

int main(int argc, char** argv)
{
	const char* gameName = (argc > 1) ? argv[1] : "asteroid";
	const int frameCount = (argc > 2) ? std::atoi(argv[2]) : 600;

	std::printf("headless: looking up driver '%s'\n", gameName);

	const AAEDriver* drv = aae::FindDriverByName(gameName);
	if (!drv)
	{
		std::printf("headless: unknown game '%s' (registry has %zu drivers)\n",
			gameName, aae::AllDrivers().size());
		for (const auto* d : aae::AllDrivers())
			if (d && d->name) std::printf("  registered: %s\n", d->name);
		return 2;
	}

	// --- core-safe subset of run_game() ---------------------------------
	Machine->gamedrv = drv;
	Machine->drv = drv;
	Machine->orientation = drv->rotation;
	Machine->video_attributes = drv->video_attributes;
	Machine->visible_area = drv->visible_area;

	if (init_machine_headless(drv) != 0)
	{
		std::printf("headless: init_machine failed (out of memory)\n");
		return 2;
	}

	// The task's "core-safe subset of run_game()" list omits Step 10 (Input
	// ports / load_input_port_settings()), reasonably read as "file I/O we
	// don't need." But load_input_port_settings() does two more things
	// regardless of whether a save file exists (osd_fopen's null-return path
	// - our stub always takes it - skips straight past the file-reading
	// blocks in load_default_keys()/itself): it builds input_port_tag[] from
	// Machine->input_ports (read by readinputportbytag(), used by every
	// in->tag-keyed memory read handler - "IN0"/"IN1"/"COCKTAIL" etc.), and
	// it calls update_input_ports(), the ONLY place that seeds
	// input_port_value[] (read by readinputport()/input_port_N_r()) from the
	// driver's default values. Without either, every tagged input read logs
	// "Unable to locate input port" and returns -1, and every untagged one
	// reads a permanently-zero value - and several drivers' interrupt
	// handlers gate the vector-generator NMI on a self-test/DIP bit read
	// this way (e.g. asteroid_interrupt() checks bit 0x80 of port 0), so
	// getting this wrong can silently suppress the interrupt that drives the
	// whole vector pipeline. Calling the real function directly (rather than
	// hand-copying its tag-building loop) exercises exactly what the OSD
	// stubs in null_backends.cpp were written for, and skips only the file
	// I/O this test deliberately doesn't do.
	load_input_port_settings();

	reset_memory_tracking();

	// config (config.h's file-scope `inline settings config;`) is zero-
	// initialized and nothing here ever calls config.cpp's ini-loading code
	// (executable-side, and not something a headless/Teensy target should
	// need anyway), so config.exrompath is still nullptr at this point.
	// load_roms() unconditionally does `temppath = config.exrompath;` before
	// falling back to getpathM("roms", 0) if that path doesn't exist -
	// assigning a std::string from a null char* is undefined behavior
	// (crashes via strlen(nullptr) in practice). An empty string here makes
	// that first probe harmlessly miss and fall through to the getpathM
	// fallback, which is our stub in null_backends.cpp resolving relative to
	// the CURRENT WORKING DIRECTORY - see the Task 6 report for why this
	// target must be launched from x64/Release.
	static char s_emptyExRomPath[] = "";
	config.exrompath = s_emptyExRomPath;

	if (drv->rom)
	{
		if (load_roms(driver_rom_archive(drv), drv->rom) == EXIT_FAILURE)
		{
			std::printf("headless: ROM loading failed for '%s'\n", gameName);
			return 2;
		}
	}

	timer_init();

	if (drv->init_game)
	{
		if (drv->init_game() != 0)
		{
			std::printf("headless: driver init_game() failed\n");
			return 2;
		}
	}

	init_cpu_config();

	std::printf("headless: init complete, running %d frames of '%s'\n", frameCount, gameName);

	// --- core-safe subset of emulator_run(), looped N times -------------
	for (int frame = 0; frame < frameCount; ++frame)
	{
		cpu_run();
		if (drv->run_game)
			drv->run_game();
		inputport_vblank_end();
		cpu_clear_cyclecount_eof();
	}

	// --- teardown ---------------------------------------------------------
	if (drv->end_game)
		drv->end_game();

	if (Machine->input_ports)
	{
		free(Machine->input_ports);
		Machine->input_ports = nullptr;
	}

	free_cpu_memory();
	free_all_memory_regions();

	std::printf("headless: %s ran %d frames, %ld vectors emitted\n",
		gameName, frameCount, g_vector_count);

	return (g_vector_count != 0) ? 0 : 1;
}
