//==========================================================================
// AAE is a poorly written M.A.M.E (TM) derivative based on early MAME
// code, 0.29 through .90 mixed with code of my own. This emulator was
// created solely for my amusement and learning and is provided only
// as an archival experience.
//
// All MAME code used and abused in this emulator remains the copyright
// of the dedicated people who spend countless hours creating it. All
// MAME code should be annotated as belonging to the MAME TEAM.
//
// SOME CODE BELOW IS FROM MAME and COPYRIGHT the MAME TEAM.
//==========================================================================



// ---------------------------------------------------------------------------
// Standard C++ headers
// ---------------------------------------------------------------------------
#include <cstdlib>      // malloc, free, srand, exit
#include <cstring>      // strcmp, strlen, memcpy, memset
#include <string>
#include <vector>
#include <sstream>      // ostringstream (list_all_roms)
#include <iomanip>      // hex, setw, setfill (list_all_roms)
#include <algorithm>    // transform (to_lowercase)
#include <chrono>       // steady_clock (profiling)

// ---------------------------------------------------------------------------
// Windows / platform headers
// ---------------------------------------------------------------------------
#include "framework.h"  // pulls in Windows.h with correct defines

#ifndef WIN7BUILD
#include "win10_win11_required_code.h"
#endif

// ---------------------------------------------------------------------------
// AAE headers
// ---------------------------------------------------------------------------
#include "aae_emulator.h"
#include "aae_mame_driver.h"
#include "wintimer.h"
#include "acommon.h"
#include "fileio/texture_handler.h"
#include "config.h"
#include "opengl_renderer.h"
#include "gl_fbo.h"
#include "menu.h"
#include "aae_avg.h"
#include "os_input.h"
#include "os_basic.h"
#include "timer.h"
#include "vector_fonts.h"
#include "gl_texturing.h"
#include "mixer.h"
#include "utf8conv.h"
#include "old_mame_raster.h"
#include "osd_video.h"
#include "game_list.h"
#include "FrameLimiter.h"
#include "driver_registry.h"   // AllDrivers(), FindDriverByName(), AAE_REGISTER_DRIVER
#include "joystick.h"
#include "mame_layout.h"
#include "windows_util.h"
#include <filesystem>
#include <path_helper.h>

#pragma warning(disable : 4996 4244)

using namespace std;
using namespace chrono;

// ---------------------------------------------------------------------------
// Module globals
// ---------------------------------------------------------------------------

static double g_lastFrameTimestampMs = 0.0;

// Command-line resolution override requested by -WxH flag
static int x_override = 0;
static int y_override = 0;
// -window / -nowindow override (3=window, 2=nowindow, 0=no override)
static int win_override = 0;

// 1 when the user launched with a game name argument (e.g. "aae asteroid")
// Used to skip the GUI and exit directly on ESC instead of returning to GUI.
static int started_from_command_line = 0;

int  hiscoreloaded = 0;
int  show_fps = 0;

// Per-game frame throttle flag. 1=throttle to game FPS, 0=run uncapped.
static int throttle = 1;

// True only after run_game() has fully initialized a game.
// Used as the authoritative "is a game actually running?" flag.
static bool game_loaded_sentinel = false;

// Artwork layer loaded flags (indexed 0..5, matching art_tex[] slots).
int art_loaded[6] = {};

// leds_status is defined in acommon.cpp and written by game drivers (e.g. llander).
// ResetPerGameRuntimeState() resets it to 0 between games.
extern int leds_status;

// Complete list of all supported games, built from the driver registry.
GameList gList;

// Runtime GUI driver index (resolved from the registry at startup, not hardcoded).
static int g_guiGameIndex = -1;
// Index of the game to switch to at the next safe frame boundary (-1 = none).
static int g_pendingSwitchGameNum = -1;
// Command-line debug override (-1 = no override)
static int debug_override = -1;
static int rotation_override = -1;  // -ror/-rol command-line override
static int artwork_override = -1;   // -noartwork command-line override
static int bezel_override = -1;     // -nobezel command-line override
static int overlay_override = -1;   // -nooverlay command-line override
static int prescale_override = -1;  // -prescale X command-line override
// TEMPORARY VARIABLE
// Tracks the last-applied bezel state for vertical vector games so we can
// detect mid-game toggles and resize the window accordingly.
static int g_lastBezelState = -1;  // -1 = not yet initialized

// ---------------------------------------------------------------------------
// MAME compatibility: RunningMachine and driver pointers.
// Machine is the globally visible pointer to the current running game state.
// gamedrv is the module-level alias used by init_machine().
// ---------------------------------------------------------------------------
static struct RunningMachine machine;
struct RunningMachine* Machine = &machine;
static const struct AAEDriver* gamedrv = nullptr;

struct GameOptions options;

// ---------------------------------------------------------------------------
// Volume change-filter state (avoid spamming SetVolume every frame).
// ---------------------------------------------------------------------------
static int g_lastAppliedMainVol255 = -1;

// -----------------------------------------------------------------------------
// Exit confirmation dialog state
//
// g_exitConfirmActive: 1 while the YES/NO dialog is visible, 0 otherwise.
// g_exitConfirmSel:    0 = YES is highlighted, 1 = NO is highlighted.
//
// Default selection is YES. The dialog is only shown when the player has
// already made a deliberate input to exit, so defaulting to YES is appropriate.
// Input is handled in msg_loop() each frame while g_exitConfirmActive is set.
// glcode.cpp reads these via get_exit_confirm_status() /
// get_exit_confirm_selection() to draw the overlay on top of the dim layer.
// -----------------------------------------------------------------------------
static int g_exitConfirmActive = 0;
static int g_exitConfirmSel = 0;  // default highlight: YES

int get_exit_confirm_status() { return g_exitConfirmActive; }
int get_exit_confirm_selection() { return g_exitConfirmSel; }

// ---------------------------------------------------------------------------
// Supported command-line resolution overrides (matched by gameparse()).
// ---------------------------------------------------------------------------
static const struct { const char* desc; int x, y; } gfx_res[] =
{
	{ "-320x240",   320,  240 },
	{ "-512x384",   512,  384 },
	{ "-640x480",   640,  480 },
	{ "-800x600",   800,  600 },
	{ "-1024x768", 1024,  768 },
	{ "-1152x720", 1152,  720 },
	{ "-1152x864", 1152,  864 },
	{ "-1280x768", 1280,  768 },
	{ "-1280x1024",1280, 1024 },
	{ "-1600x1200",1600, 1200 },
	{ "-1680x1050",1680, 1050 },
	{ "-1920x1080",1920, 1080 },
	{ "-1920x1200",1920, 1200 },
	{ nullptr, 0, 0 }
};

// Scale factor for the raster polygon renderer (pixels -> GL units).
float vid_scale = 3.0f;

// ---------------------------------------------------------------------------
// Forward declarations for static helpers defined later in this file.
// ---------------------------------------------------------------------------
static void ResetPerGameRuntimeState();
static int  FindGuiGameIndex();

// ---------------------------------------------------------------------------
// SetGamePerformanceMode
// Applies the configured Windows process/thread priority for game play.
// Called once per game start, after the game has fully initialized.
// ---------------------------------------------------------------------------
void SetGamePerformanceMode(const settings& cfg)
{
	DWORD      procPriority = NORMAL_PRIORITY_CLASS;
	const char* priorityName = "NORMAL_PRIORITY_CLASS";

	switch (cfg.priority)
	{
	case 4: procPriority = REALTIME_PRIORITY_CLASS;      priorityName = "REALTIME_PRIORITY_CLASS";      break;
	case 3: procPriority = HIGH_PRIORITY_CLASS;          priorityName = "HIGH_PRIORITY_CLASS";          break;
	case 2: procPriority = ABOVE_NORMAL_PRIORITY_CLASS;  priorityName = "ABOVE_NORMAL_PRIORITY_CLASS";  break;
	case 1: procPriority = NORMAL_PRIORITY_CLASS;        priorityName = "NORMAL_PRIORITY_CLASS";        break;
	case 0: procPriority = IDLE_PRIORITY_CLASS;          priorityName = "IDLE_PRIORITY_CLASS";          break;
	default:
		LOG_INFO("SetGamePerformanceMode: unknown priority level %d", cfg.priority);
		break;
	}

	if (!SetPriorityClass(GetCurrentProcess(), procPriority))
		LOG_INFO("Failed to set process priority to %s (Error %lu)", priorityName, GetLastError());
	else
		LOG_INFO("Process priority set to %s", priorityName);

	if (cfg.boostThread)
	{
		if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL))
			LOG_INFO("Failed to boost thread priority. Error: %lu", GetLastError());
		else
			LOG_INFO("Main thread boosted to THREAD_PRIORITY_ABOVE_NORMAL.");
	}
}

// ---------------------------------------------------------------------------
// FindGuiGameIndex  (static helper)
// Searches the driver registry for the "gui" named driver and returns its
// index. Returns -1 if the GUI driver is not registered.
// Result is NOT cached here - use g_guiGameIndex for the startup-resolved value.
// ---------------------------------------------------------------------------
static int FindGuiGameIndex()
{
	const auto& reg = aae::AllDrivers();
	for (int i = 0; i < (int)reg.size(); ++i)
	{
		if (reg[i] && reg[i]->name && std::strcmp(reg[i]->name, "gui") == 0)
			return i;
	}
	return -1;
}

// ---------------------------------------------------------------------------
// emulator_apply_pending_switch
// Call once per frame from WinMain after emulator_run().
// Applies a deferred game switch that was requested mid-frame via
// emulator_request_switch(). If the target game fails to start, falls back
// to the GUI driver automatically.
// ---------------------------------------------------------------------------
bool emulator_apply_pending_switch()
{
	if (g_pendingSwitchGameNum < 0)
		return false;

	const int target = g_pendingSwitchGameNum;
	g_pendingSwitchGameNum = -1;
	done = 0;

	const auto& reg = aae::AllDrivers();
	const char* targetName =
		(target >= 0 && target < (int)reg.size() && reg[target] && reg[target]->name)
		? reg[target]->name : "INVALID";

	LOG_INFO("ApplyPendingSwitch: target=%d (%s)", target, targetName);

	if (emulator_start_game(target))
		return true;

	LOG_INFO("ApplyPendingSwitch: start FAILED for %d (%s). Returning to GUI.", target, targetName);

	const int guiIdx = FindGuiGameIndex();
	if (guiIdx >= 0)
	{
		done = 0;
		emulator_start_game(guiIdx);
		done = 0;
		return true;
	}

	LOG_INFO("ApplyPendingSwitch: GUI driver not found; cannot recover.");
	return false;
}

// ---------------------------------------------------------------------------
// emulator_request_switch
// Request a deferred game switch. The switch is applied at the next safe
// frame boundary by emulator_apply_pending_switch(). Setting done=1 ends
// the current session cleanly before the switch happens.
// ---------------------------------------------------------------------------
void emulator_request_switch(int gameNum)
{
	g_pendingSwitchGameNum = gameNum;
	done = 1;
}

// ---------------------------------------------------------------------------
// RequestReturnToGui  (static helper)
// Converts a "quit this title" action into a "switch to GUI" request.
// If already in the GUI, does nothing (caller must set done=1 explicitly
// if they want to exit the application from the GUI).
// ---------------------------------------------------------------------------
static void RequestReturnToGui(const char* reason)
{
	const int guiIdx = FindGuiGameIndex();
	if (guiIdx < 0)
	{
		LOG_INFO("RequestReturnToGui: GUI driver not found. reason=%s", reason ? reason : "NULL");
		done = 1;   // fallback: no GUI to return to, just exit
		return;
	}

	if (gamenum == guiIdx)
	{
		LOG_INFO("RequestReturnToGui: already in GUI. reason=%s", reason ? reason : "NULL");
		return;
	}

	LOG_INFO("RequestReturnToGui: switching to GUI. reason=%s (gamenum=%d -> gui=%d)",
		reason ? reason : "NULL", gamenum, guiIdx);

	//glcode_vector_hard_clear_fbo1();
	g_pendingSwitchGameNum = guiIdx;
	done = 1;
}

// ---------------------------------------------------------------------------
// AAE_ApplyAudioVolumesFromConfig
// Applies config.mainvol to the mixer. Uses a change-filter to avoid
// calling mixer_set_master_volume_255() every single frame.
//   force - if non-zero, always apply regardless of whether value changed.
// ---------------------------------------------------------------------------
void AAE_ApplyAudioVolumesFromConfig(int force)
{
	const int v255 = (config.mainvol < 0) ? 0 : (config.mainvol > 255) ? 255 : (int)config.mainvol;

	if (force || v255 != g_lastAppliedMainVol255)
	{
		mixer_set_master_volume_255(v255);
		g_lastAppliedMainVol255 = v255;
	}
}

// ---------------------------------------------------------------------------
// toLowerCase
// In-place ASCII lowercase conversion. Operates on the raw char buffer
// passed in (used for normalizing argv before parsing).
// ---------------------------------------------------------------------------
void toLowerCase(char* str)
{
	for (; *str; ++str)
		*str = (char)tolower((unsigned char)*str);
}

// ---------------------------------------------------------------------------
// to_lowercase  (static helper)
// Returns a lowercase copy of a C string as a std::string.
// Used by gameparse() and the -debug option parser.
// ---------------------------------------------------------------------------
static std::string to_lowercase(const char* str)
{
	std::string s(str ? str : "");
	std::transform(s.begin(), s.end(), s.begin(),
		[](unsigned char c) { return (char)std::tolower(c); });
	return s;
}

// ---------------------------------------------------------------------------
// run_a_game
// Sets up Machine->gamedrv and the module-level gamedrv alias to point at
// the requested game's driver. Must be called before any game-specific
// code that reads Machine->gamedrv.
// ---------------------------------------------------------------------------
int run_a_game(int game)
{
	Machine->gamedrv = gamedrv = aae::AllDrivers().at(game);
	Machine->drv = gamedrv;
	LOG_INFO("Driver set to: %s", Machine->gamedrv->name);
	return 0;
}

// ---------------------------------------------------------------------------
// list_all_roms
// Enumerates all registered drivers and writes a text dump of every ROM
// entry to "AAE All Game Roms List.txt". Called from emulator_init() when
// the -listromstotext command-line option is given.
// ---------------------------------------------------------------------------

// Sentinel value in RomModule.loadAddr that marks a ROM_REGION() header entry.
static constexpr int REGION_TAG = 999;

void list_all_roms()
{
	const auto& reg = aae::AllDrivers();
	const int game_count = static_cast<int>(reg.size());
	if (game_count <= 0) return;

	std::vector<char> buf;
	buf.reserve(0x150000);

	// Lambda helpers that append into the flat char buffer.
	const auto append_cstr = [&](const char* s) {
		if (!s) return;
		buf.insert(buf.end(), s, s + std::strlen(s));
		};
	const auto append_str = [&](const std::string& s) {
		buf.insert(buf.end(), s.begin(), s.end());
		};

	append_cstr("AAE All Games RomList\n");

	for (int g = 0; g < game_count; ++g)
	{
		const AAEDriver* d = reg[g];
		if (!d || !d->name || !d->desc || !d->rom) continue;

		append_cstr("\nGame Name: ");
		append_cstr(d->desc);
		append_cstr(":\nRom  Name: ");
		append_cstr(d->name);
		append_cstr(".zip\n");

		for (int r = 0; d->rom[r].romSize > 0; ++r)
		{
			const RomModule& rm = d->rom[r];

			if (rm.loadAddr == REGION_TAG)
			{
				const char* region_name =
					(rm.loadtype >= 0 && rm.loadtype < REGION_MAX) ? rom_regions[rm.loadtype] : "UNKNOWN";
				std::ostringstream oss;
				oss << "ROM_REGION(0x"
					<< std::hex << std::uppercase << std::setw(4) << std::setfill('0') << rm.romSize
					<< ", " << region_name << ")\n";
				append_str(oss.str());
				continue;
			}

			if (rm.filename == reinterpret_cast<char*>(-1))
				continue;

			if (rm.filename == reinterpret_cast<char*>(-2))
				append_cstr("ROM_CONTINUE");
			else
				append_cstr(rm.filename ? rm.filename : "NULL");

			std::ostringstream oss;
			oss << " Size: 0x" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << rm.romSize
				<< " Load: 0x" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << rm.loadAddr
				<< " CRC: 0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << rm.crc
				<< " SHA1: " << (rm.sha ? rm.sha : "NULL") << '\n';
			append_str(oss.str());
		}
	}

	std::ostringstream footer;
	footer << "\n\nGames/Clones supported: " << game_count << '\n';
	append_str(footer.str());
	buf.push_back('\0');    // null-terminate for save_file_char

	LOG_INFO("Saving ROM list...");
	save_file_char("AAE All Game Roms List.txt",
		buf.data(),
		static_cast<int>(buf.size() - 1));  // exclude the null terminator
}

// ---------------------------------------------------------------------------
// gameparse
// Parses command-line arguments for the currently selected game.
// Handles -listroms, -verifyroms, -listsamples, -verifysamples, -window,
// -nowindow, and resolution override flags (-WxH).
//
// Only called from emulator_init() when a matching game name was found on
// the command line AND additional arguments follow the game name.
// ---------------------------------------------------------------------------
void gameparse(int argc, char* argv[])
{
	int list = 0;
	int w = 0;
	int retval = 0;

	win_override = 0;

	const AAEDriver* drv = aae::AllDrivers().at(gamenum);
	Machine->gamedrv = drv;
	Machine->drv = drv;

	// Parse all arguments to find option flags and resolution overrides.
	for (int i = 1; i < argc; i++)
	{
		std::string arg = to_lowercase(argv[i]);

		if (arg == "-listroms")       list = 1;
		else if (arg == "-verifyroms")     list = 2;
		else if (arg == "-listsamples")    list = 3;
		else if (arg == "-verifysamples")  list = 4;
		else if (arg == "-window")         win_override = 3;
		else if (arg == "-nowindow")       win_override = 2;

		// System rotation overrides (command line wins over INI)
		else if (arg == "-ror")            rotation_override = ROT90;
		else if (arg == "-rol")            rotation_override = ROT270;
		else if (arg == "-norotate")       rotation_override = ROT0;

		// Artwork layer overrides (command line wins over INI)
		else if (arg == "-noartwork")      artwork_override = 0;
		else if (arg == "-nobezel")        bezel_override = 0;
		else if (arg == "-nooverlay")      overlay_override = 0;
		else if (arg == "-prescale" && i + 1 < argc)
		{
			prescale_override = std::atoi(argv[++i]);
			if (prescale_override < 1) prescale_override = 1;
		}

		for (int j = 0; gfx_res[j].desc != nullptr; j++)
		{
			if (arg == to_lowercase(gfx_res[j].desc))
			{
				x_override = gfx_res[j].x;
				y_override = gfx_res[j].y;
				break;
			}
		}
	}

	std::string logOutput;
	logOutput.reserve(4096);

	switch (list)
	{
		// -------------------------------------------------------------------------
	case 1: // -listroms: print each ROM file name and CRC for this game
		LOG_INFO("%s rom list:", drv->name);
		for (int x = 0; drv->rom[x].romSize > 0; ++x)
		{
			const auto& rom = drv->rom[x];
			const char* fname = rom.filename;

			if (rom.loadAddr == ROM_REGION_START && rom.loadtype >= 0 && rom.loadtype < REGION_MAX)
				LOG_INFO("Region: %s", rom_regions[rom.loadtype]);

			if (fname && fname != (char*)-1 && fname != (char*)-2 &&
				rom.loadAddr != ROM_REGION_START && rom.loadAddr != 0x999)
			{
				char crcbuf[16];
				snprintf(crcbuf, sizeof(crcbuf), "%08X", rom.crc);
				LOG_INFO("%s | CRC: %s | SHA1: %s", fname, crcbuf, rom.sha ? rom.sha : "NULL");
			}
		}
		LogClose();
		exit(0);

		// -------------------------------------------------------------------------
	case 2: // -verifyroms: check each ROM file against expected CRC
		LOG_INFO("Starting ROM verification for %s", drv->name);
		for (int x = 0; drv->rom[x].romSize > 0; ++x)
		{
			const char* fname = drv->rom[x].filename;
			if (fname && fname != (char*)-1 && fname != (char*)-2 &&
				drv->rom[x].loadAddr != ROM_REGION_START && drv->rom[x].loadAddr != 0x999)
			{
				logOutput = fname;
				retval = verify_rom(drv->name, drv->rom, x);
				switch (retval)
				{
				case 0: logOutput += " BAD? ";    break;
				case 1: logOutput += " OK ";      break;
				case 3: logOutput += " BADSIZE "; break;
				case 4: logOutput += " NOFILE ";  break;
				case 5: logOutput += " NOZIP ";   break;
				default: logOutput += " UNKNOWN "; break;
				}
				logOutput += (w > 1) ? "\n" : " ";
				w = (w > 1) ? 0 : w + 1;
				LOG_INFO("%s", logOutput.c_str());
			}
		}
		LogClose();
		exit(0);

		// -------------------------------------------------------------------------
	case 3: // -listsamples: list sample names for this game
		if (drv->game_samples)
		{
			const char** samples = drv->game_samples;
			for (int i = 0; samples[i] && std::strcmp(samples[i], "NULL") != 0; ++i)
				logOutput += samples[i];
		}
		LOG_INFO("%s sample list: %s", drv->name, logOutput.c_str());
		LogClose();
		exit(0);

		// -------------------------------------------------------------------------
	case 4: // -verifysamples: check sample files for this game
		LOG_INFO("Starting sample verification for %s", drv->name);
		if (drv->game_samples)
		{
			const char** samples = drv->game_samples;
			for (int i = 0; samples[i] && std::strcmp(samples[i], "NULL") != 0; ++i)
			{
				logOutput = samples[i];
				retval = verify_sample(samples, i);
				switch (retval)
				{
				case 0: logOutput += " BAD? ";    break;
				case 1: logOutput += " OK ";      break;
				case 3: logOutput += " BADSIZE "; break;
				case 4: logOutput += " NOFILE ";  break;
				case 5: logOutput += " NOZIP ";   break;
				default: logOutput += " UNKNOWN "; break;
				}
				logOutput += (w > 1) ? "\n" : " ";
				w = (w > 1) ? 0 : w + 1;
			}
		}
		LOG_INFO("%s sample verify: %s", drv->name, logOutput.c_str());
		break;

	default:
		break;
	}
}

// ---------------------------------------------------------------------------
// list_all_games
// Writes a plain-text list of every registered driver to
// "AAE All Games List.txt". Each line contains the short ROM name and the
// human-readable description, tab-separated for easy parsing.
// Called from emulator_init() when the -listallgames option is given.
// ---------------------------------------------------------------------------
void list_all_games()
{
	const auto& reg = aae::AllDrivers();
	const int game_count = static_cast<int>(reg.size());
	if (game_count <= 0) return;

	std::vector<char> buf;
	buf.reserve(65536);

	const auto append_cstr = [&](const char* s) {
		if (!s) return;
		buf.insert(buf.end(), s, s + std::strlen(s));
		};

	append_cstr("AAE Game List\n");
	append_cstr("Name            Description\n");
	append_cstr("----            -----------\n");

	int listed = 0;
	for (int g = 0; g < game_count; ++g)
	{
		const AAEDriver* d = reg[g];
		// Skip null entries and the internal GUI driver.
		if (!d || !d->name || !d->desc) continue;
		if (std::strcmp(d->name, "gui") == 0) continue;

		// Left-pad the short name to 16 chars for a tidy column.
		char namebuf[32];
		snprintf(namebuf, sizeof(namebuf), "%-16s", d->name);
		append_cstr(namebuf);
		append_cstr(d->desc);
		append_cstr("\n");
		++listed;
	}

	// Footer with total count.
	char footer[64];
	snprintf(footer, sizeof(footer), "\nTotal games: %d\n", listed);
	append_cstr(footer);
	buf.push_back('\0');    // null-terminate for save_file_char

	LOG_INFO("Saving game list (%d entries)...", listed);
	save_file_char("AAE All Games List.txt",
		buf.data(),
		static_cast<int>(buf.size() - 1));  // exclude the null terminator
}

// ---------------------------------------------------------------------------
// init_machine
// Allocates a shadow copy of the game's input port table in Machine->input_ports.
// The shadow copy is what the menu system reads and modifies; the original
// in the driver struct is read-only.
// Returns 0 on success, 1 if malloc fails.
// ---------------------------------------------------------------------------
int init_machine(void)
{
	LOG_INFO("init_machine: building shadow input port table");

	if (!gamedrv->input_ports)
		return 0;

	// Count entries including the IPT_END terminator.
	int total = 0;
	const struct InputPort* from = gamedrv->input_ports;
	do { ++total; } while ((from++)->type != IPT_END);

	Machine->input_ports = (InputPort*)malloc(total * sizeof(struct InputPort));
	if (!Machine->input_ports)
	{
		LOG_ERROR("init_machine: malloc failed for %d InputPort entries", total);
		return 1;
	}

	from = gamedrv->input_ports;
	struct InputPort* to = Machine->input_ports;
	do
	{
		memcpy(to, from, sizeof(struct InputPort));
		++to;
	} while ((from++)->type != IPT_END);

	return 0;
}

// ---------------------------------------------------------------------------
// ResetPerGameRuntimeState  (static helper)
// Clears all per-game globals so nothing leaks between game sessions.
// Called from both emulator_stop_game() and emulator_init().
// ---------------------------------------------------------------------------
static void ResetPerGameRuntimeState()
{
	paused = 0;
	done = 0;
	have_error = 0;
	hiscoreloaded = 0;
	frameavg = 0;
	fps_count = 0.0;
	frames = 0;

	leds_status = 0;
	osd_set_leds(0);

	x_override = 0;
	y_override = 0;
	win_override = 0;

	for (int i = 0; i < 6; ++i)
		art_loaded[i] = 0;

	// Artwork enable flags are per-game; reset them so the next game starts clean.
	config.artwork = 0;
	config.bezel = 0;
	config.overlay = 0;

	game_loaded_sentinel = false;
}

// ---------------------------------------------------------------------------
// run_game
// Main per-game initialization sequence. Sets up all subsystems in order,
// then returns. The actual frame loop runs in emulator_run() which is called
// repeatedly by WinMain until done != 0.
//
// Uses a single goto-based fail path for early-exit cleanup so the teardown
// order is always consistent regardless of where initialization fails.
//
// Subsystem init order:
//   1. Driver pointers (run_a_game)
//   2. Config (setup_game_config / sanity_check_config)
//   3. ROM loading
//   4. OpenGL (init_gl - one-time only, guarded internally)
//   5. Scanlines texture (init_raster_overlay)
//   6. Raster FBO allocation (fbo_init_raster)
//   7. Artwork loading and resize
//   8. Video config (setup_video_config)
//   9. Audio (mixer_init, samples, ambient)
//  10. Input ports
//  11. VH open (raster games only)
//  12. Timers
//  13. Driver init_game()
//  14. CPU config
//  15. NVRAM load
//  16. Performance mode
// ---------------------------------------------------------------------------
void run_game(void)
{
	const int cx = GetSystemMetrics(SM_CXSCREEN);
	const int cy = GetSystemMetrics(SM_CYSCREEN);

	// Track which subsystems started so the fail path tears down correctly.
	bool did_init_gl = false;
	bool did_mixer_init = false;
	bool did_loaded_artwork = false;
	bool did_vh_open = false;
	bool did_timer_init = false;
	bool did_driver_init = false;

	num_samples = 0;
	errorlog = 0;
	game_loaded_sentinel = false;
	float gameAspect = 0;

	auto& ws = GetWindowSetup();

	// Step 1: Bind Machine to the chosen driver.
	run_a_game(gamenum);

	// Step 2: Per-game configuration.
	setup_game_config();
	sanity_check_config();

	// Re-apply the command line debug override over the INI file.
	if (debug_override != -1)
		config.debug = debug_override;
	// Re-apply the command line rotation override over the INI file.
	if (rotation_override != -1)
		config.system_rotation = rotation_override;
	// Re-apply artwork layer overrides over the INI file.
	if (artwork_override != -1)
		config.artwork = artwork_override;
	if (bezel_override != -1)
		config.bezel = bezel_override;
	if (overlay_override != -1)
		config.overlay = overlay_override;
	if (prescale_override != -1)
		config.prescale = prescale_override;

	// Reset per-game rendering suppression flags.
	g_scanline_override = 0;

	LOG_INFO("Starting game: %s", Machine->gamedrv->desc);

	if (config.screenh > cy || config.screenw > cx)
		allegro_message("MESSAGE", "Warning: physical screen size smaller than config setting.");

	options.cheat = 1;
	leds_status = 0;
	osd_set_leds(0);

	// Compose driver rotation with system rotation (command-line -ror/-rol).
		// Per MAME convention, orientation flags compose with XOR, not OR.
		// FLIP is applied after SWAP_XY, so XOR gives correct results for
		// all combinations (e.g. vertical game + ror = landscape again).
	Machine->orientation = Machine->drv->rotation ^ config.system_rotation;

	LOG_INFO("Orientation: driver=0x%X system=0x%X composed=0x%X",
		Machine->drv->rotation, config.system_rotation, Machine->orientation);

	// Scale stored volume byte values to the internal mixer range.
	// Guard prevents double-scaling if run_game() is called more than once.
	if (config.mainvol <= 1.0f && config.pokeyvol <= 1.0f && config.noisevol <= 1.0f)
	{
		config.mainvol *= 12.75f;
		config.pokeyvol *= 12.75f;
		config.noisevol *= 12.75f;
	}

	init_machine();
	reset_memory_tracking();    // must be before any ROM/memory allocations

	// Copy the driver's read-only visible_area into the running machine.
	Machine->visible_area = Machine->gamedrv->visible_area;

	// Step 3: ROM loading.
	if (Machine->gamedrv->rom)
	{
		if (load_roms(Machine->gamedrv->name, Machine->gamedrv->rom) == EXIT_FAILURE)
		{
			LOG_ERROR("ROM loading failed.");
			have_error = 10;
			goto fail;
		}
	}

	// Step 4: OpenGL init (guarded - only runs once per process lifetime).
	LOG_INFO("OpenGL init...");
	init_gl();
	did_init_gl = true;
	if (have_error != 0) goto fail;

	// Step 5: Initialize the raster rendering pipeline (raster games only).
	// Creates the screen quad shader and VAO/VBO (one-time init, safe to call
	// multiple times). Vector games use the existing FBO pipeline instead.
	if (!(Machine->gamedrv->video_attributes & VIDEO_TYPE_VECTOR))
	{
		fbo_init_raster();
		init_raster_overlay();
	}

	// Step 6: Legacy artwork loading and texture resize.
	if (Machine->gamedrv->artwork)
	{
		load_artwork(Machine->gamedrv->artwork);
		did_loaded_artwork = true;
		if (have_error != 0) goto fail;
	}

	// Step 7: MAME .lay layout loading (raster games only).
	// Searches external and local artwork paths for ZIP or loose .lay files.
	// Falls back to a synthetic screen-only layout if nothing is found.
	if (!(Machine->gamedrv->video_attributes & VIDEO_TYPE_VECTOR))
		Layout_LoadForGame(Machine->gamedrv);

	// Step 8: Video configuration (bezel/crop layout, scale, offsets).
	setup_video_config();
	if (have_error != 0) goto fail;

	frameavg = 0;
	fps_count = 0.0;
	frames = 0;

	// Step 9: Audio.
	mixer_init(config.samplerate, Machine->gamedrv->fps);
	did_mixer_init = true;
	if (have_error != 0) goto fail;

	// Apply initial volume settings from config. This also sets the g_lastAppliedMainVol255
	AAE_ApplyAudioVolumesFromConfig(1);

	// Load game samples from the driver's game_samples list. These are typically
	// used for discrete sound effects or voice samples. Music is usually streamed
	// from ROM or loaded as artwork assets instead, but there are exceptions.
	if (Machine->gamedrv->game_samples)
	{
		load_samples_batch(Machine->gamedrv->game_samples);
		if (have_error != 0) goto fail;
	}
	LOG_INFO("Samples loaded: %d", num_samples);

	// Load optional ambient audio (flyback, psnoise, hiss) from samples\aae.zip.
	load_ambient_samples();

	setup_ambient(VECTOR);
	if (have_error != 0) goto fail;

	// Step 10: Input ports.
	LOG_INFO("Loading input port settings...");
	load_input_port_settings();
	if (have_error != 0) goto fail;

	// Step 11: VH open (raster games only - sets up color/palette tables).
	if (!(Machine->gamedrv->video_attributes & VIDEO_TYPE_VECTOR))
	{
		vh_open();
		did_vh_open = true;
		if (have_error != 0) goto fail;
	}

	// Step 12: Compute game dimensions and update window aspect ratio.
	// InitGameDimensions computes the oriented output size and pixel aspect
	// from the driver's visible_area. ComputeGameAspect returns the display
	// aspect considering the active layout, bezel settings, and pixel aspect.
	//if (!(Machine->gamedrv->video_attributes & VIDEO_TYPE_VECTOR))
	//{
	//	Layout_InitGameDimensions();
	//	gameAspect = Layout_ComputeGameAspect();
	//	if (gameAspect > 0.0f)	WindowUtil_UpdateAspect(gameAspect);
	//}
	/*
	else	// Reset for vector
	{
		const rectangle& va = Machine->drv->visible_area;
		int scrW = (va.max_x - va.min_x + 1);
		int scrH = (va.max_y - va.min_y + 1);
		if (scrW > 0 && scrH > 0)
		{
			gameAspect = (float)scrW / (float)scrH;
			LOG_INFO("Window aspect from driver screen: %.3f (%dx%d)",	gameAspect, scrW, scrH);
		}

		if (gameAspect > 0.0f)		{ WindowUtil_UpdateAspect(gameAspect); }
	}
	*/

	// Step 12: Compute game dimensions and update window aspect ratio.
	Layout_InitGameDimensions();
	gameAspect = Layout_ComputeGameAspect();
	{
		bool isVector = (Machine->gamedrv->video_attributes & VIDEO_TYPE_VECTOR) != 0;
		bool isVertical = (Machine->gamedrv->rotation & ORIENTATION_SWAP_XY) != 0;
		bool hasBezel = (g_bezelAvailable != 0);
		bool bezelOn = (config.bezel != 0);

		// User aspect override: INI use_aspect=1 with aspect_ratio, or
		// command-line -aspect N:M. Command line wins over INI (handled
		// in winmain.cpp ParseCommandLineArgs). This overrides the
		// game-computed aspect entirely.
		// When aspectOverrideActive is false (the default), every game
		// uses its natural computed aspect — yiear gets its narrow window,
		// rotated games get portrait, etc. Nothing changes.
		if (ws.aspectOverrideActive && ws.aspectRatio > 0.0f)
		{
			LOG_INFO("Step 12: user aspect override: %.3f (game was %.3f)", ws.aspectRatio, gameAspect);
			gameAspect = ws.aspectRatio;
		}

		if (isVector && isVertical && hasBezel && bezelOn)
		{
			LOG_INFO("Step 12: vertical vector with bezel art active - keeping 4:3 window");
		}
		else if (gameAspect > 0.0f)
		{
			WindowUtil_UpdateAspect(gameAspect);
		}

		// Seed the per-frame tracker so emulator_run() can detect toggles.
		if (isVector && isVertical && hasBezel)
			g_lastBezelState = bezelOn ? 1 : 0;
		else
			g_lastBezelState = -1;  // not tracking
	}

	// Step 13: Timers (must exist before init_game()).
	timer_init();
	did_timer_init = true;
	if (have_error != 0) goto fail;

	// Step 14: Driver-specific initialization.
	Machine->gamedrv->init_game();
	did_driver_init = true;
	if (have_error != 0) goto fail;

	// Step 15: CPU configuration.
	init_cpu_config();
	if (have_error != 0) goto fail;

	hiscoreloaded = 0;

	// Step 16: NVRAM load.
	if (Machine->gamedrv->nvram_handler)
	{
		void* f = osd_fopen(Machine->gamedrv->name, 0, OSD_FILETYPE_NVRAM, 0);
		(*Machine->gamedrv->nvram_handler)(f, 0);
		if (f) osd_fclose(f);
		if (have_error != 0) goto fail;
	}

	LOG_INFO("--- Game init complete ---");

	// Step 17: Apply process/thread priority for game performance.
	SetGamePerformanceMode(config);

	game_loaded_sentinel = true;
	return;

fail:
	// -------------------------------------------------------------------------
	// Centralized cleanup. All paths converge here on any init failure.
	// Tear down in reverse init order; each step is guarded so it is safe
	// to call even if that step never started.
	// -------------------------------------------------------------------------
	game_loaded_sentinel = false;

	LOG_INFO("run_game FAIL: game=%d (%s) have_error=%d",
		gamenum,
		(Machine && Machine->gamedrv && Machine->gamedrv->name) ? Machine->gamedrv->name : "UNKNOWN",
		have_error);

	done = 1;

	save_input_port_settings();

	if (did_driver_init && Machine && Machine->gamedrv && Machine->gamedrv->end_game)
		Machine->gamedrv->end_game();

	if (Machine && Machine->input_ports)
	{
		free(Machine->input_ports);
		Machine->input_ports = nullptr;
		LOG_INFO("Input ports freed (fail path).");
	}

	free_cpu_memory();
	free_all_memory_regions();

	if (did_mixer_init)
		mixer_end();

	if (did_init_gl)
	{
		shutdown_raster_overlay();
		fbo_shutdown_raster();
		destroy_all_textures();
	}

	FrameLimiter::Shutdown();

	LOG_INFO("run_game: cleanup complete, returning to caller.");
}

// ---------------------------------------------------------------------------
// msg_loop
// Per-frame input handler for emulator-level hotkeys.
// Called at the start of emulator_run() before any game logic runs.
//
// Handles: pause, FPS toggle, snapshot, throttle, exit confirm dialog,
// menu toggle, cascading ESC/cancel, and menu navigation.
//
// All exit/cancel actions support both keyboard AND joystick:
//   - Select (menu):    OSD_KEY_UI_SELECT  or  A button only (button 0)
//   - Select (confirm): OSD_KEY_UI_SELECT  or  A button or Start (button 0 or 7)
//   - Cancel:           OSD_KEY_CANCEL     or  JOY_COMBO_ESC (LS + Back)
//   - Back (menu only): B button (button 1)
//   - Menu:             OSD_KEY_CONFIGURE  or  JOY_COMBO_MENU (LS + Start)
//   - Pause:            OSD_KEY_P          or  JOY_COMBO_PAUSE (Start + Back)
// ---------------------------------------------------------------------------
void msg_loop(void)
{
	if (osd_key_pressed_memory(OSD_KEY_RESET_MACHINE))
		cpu_reset(0);

	// Pause: keyboard P  -OR-  joystick Start + Back
	if (osd_key_pressed_memory(OSD_KEY_P) ||
		joystick_check_combo(0, JOY_COMBO_PAUSE))
	{
		if (get_menu_status() == 0)
		{
			paused ^= 1;
			if (paused) pause_audio(); else restore_audio();
		}
	}

	if (osd_key_pressed_memory(OSD_KEY_SHOW_FPS))
		show_fps ^= 1;

	if (osd_key_pressed_memory(OSD_KEY_SNAPSHOT))
		snapshot();

	if (osd_key_pressed_memory(OSD_KEY_THROTTLE))
	{
		throttle ^= 1;
		frameavg = 0;
		fps_count = 0;
		SetvSync(throttle);
	}

	// -------------------------------------------------------------------------
	// Exit confirmation dialog input handling.
	// When the confirm dialog is active, we consume all navigation and
	// selection input here and return early so nothing else fires this frame.
	//
	// Joystick support mirrors the menu navigation pattern:
	//   - A button or Start button acts as UI_SELECT (confirm)
	//   - LS + Back combo acts as CANCEL (dismiss dialog)
	//   - Left stick / D-pad left/right moves the highlight
	//
	// Start is permitted here as a confirm because the menu combo (LS + Start)
	// cannot fire while the dialog is consuming all input, so there is no
	// risk of Start accidentally reopening the menu.
	// -------------------------------------------------------------------------
	if (g_exitConfirmActive)
	{
		// Read joystick directional state for left/right navigation.
		// Use Check_Input for proper repeat timing on the stick.
		const bool joyLeft = Check_Input(JOY_INPUT_LEFT, joy[0].stick[0].axis[0].d1) != 0;
		const bool joyRight = Check_Input(JOY_INPUT_RIGHT, joy[0].stick[0].axis[0].d2) != 0;

		// A button (0) or Start (7) for select in the confirm dialog.
		// Start is safe here because the dialog consumes all input this frame.
		const bool joySelect = KeyFlip(JOY_INPUT_SELECT,
			(joy[0].button[0].b != 0) || (joy[0].button[7].b != 0)) != 0;

		// Left/Right move the highlight between YES (0) and NO (1).
		if (osd_key_pressed_memory(OSD_KEY_UI_LEFT) || joyLeft ||
			osd_key_pressed_memory(OSD_KEY_UI_RIGHT) || joyRight)
		{
			g_exitConfirmSel ^= 1;  // toggle between 0 and 1
		}

		// Enter / A button / Start confirms the highlighted choice.
		if (osd_key_pressed_memory(OSD_KEY_UI_SELECT) || joySelect)
		{
			if (g_exitConfirmSel == 0)
			{
				// YES selected - perform the exit action that originally triggered
				// the dialog (same logic as the un-guarded path below).
				g_exitConfirmActive = 0;
				const int guiIdx = FindGuiGameIndex();
				if (guiIdx >= 0 && gamenum != guiIdx && !started_from_command_line)
					RequestReturnToGui("CONFIRM_EXIT");
				else
					done = 1;
			}
			else
			{
				// NO selected - dismiss the dialog, resume normally.
				g_exitConfirmActive = 0;
			}
		}

		// ESC/Cancel or joystick LS+Back always dismisses without exiting.
		if (osd_key_pressed_memory(OSD_KEY_CANCEL) ||
			joystick_check_combo(0, JOY_COMBO_ESC))
		{
			g_exitConfirmActive = 0;
		}

		// While the dialog is up, do not process any other input this frame.
		return;
	}

	// -------------------------------------------------------------------------
	// Menu toggle: keyboard Tab (OSD_KEY_CONFIGURE)  -OR-  joystick LS + Start
	// -------------------------------------------------------------------------
	if (osd_key_pressed_memory(OSD_KEY_CONFIGURE) ||
		joystick_check_combo(0, JOY_COMBO_MENU))
	{
		int newStatus = get_menu_status() ^ 1;
		set_menu_status(newStatus);

		// Reset the menu-open input guard so stick deflection from the
		// combo press does not immediately trigger menu navigation.
		// (The guard counter lives in the menu navigation block below.)
	}

	// -------------------------------------------------------------------------
	// Cascading ESC / cancel: keyboard ESC  -OR-  joystick LS + Back
	//
	// Behavior depends on context:
	//   1. Menu is open and at a sub-level (>100): collapse to top level
	//   2. Menu is open at top level: close the menu
	//   3. Menu is closed, playing a game launched from GUI:
	//      return to GUI (no confirm needed, not actually exiting)
	//   4. Menu is closed, at GUI or launched from command line:
	//      exit the emulator (with optional confirm dialog)
	// -------------------------------------------------------------------------
	if (osd_key_pressed_memory(OSD_KEY_CANCEL) ||
		joystick_check_combo(0, JOY_COMBO_ESC))
	{
		if (get_menu_status())
		{
			// In menu: collapse to top level, or close menu entirely.
			if (get_menu_level() > 100)
				set_menu_level_top();
			else
			{
				set_menu_level_top();
				set_menu_status(0);
			}
		}
		else
		{
			// Outside menu: decide whether this is an exit or a return-to-GUI.
			const int guiIdx = FindGuiGameIndex();
			const bool wouldExit = (guiIdx < 0 || gamenum == guiIdx || started_from_command_line);

			if (wouldExit && config.confirm_exit)
			{
				// Show the YES/NO overlay instead of exiting immediately.
				g_exitConfirmActive = 1;
				g_exitConfirmSel = 0;  // default highlight: YES
			}
			else if (wouldExit)
			{
				done = 1;  // confirm_exit disabled - exit straight away
			}
			else
			{
				// Return to GUI (no confirm needed - not actually exiting).
				RequestReturnToGui("CANCEL");
			}
		}
	}

	// -------------------------------------------------------------------------
	// Menu navigation (only active when the menu is visible).
	// Uses Check_Input for joystick acceleration/repeat timing and
	// KeyFlip for single-pulse button detection.
	// -------------------------------------------------------------------------
	if (get_menu_status())
	{
		// Suppress navigation for a few frames after menu open to avoid
		// stick deflection from the combo press triggering movement.
		// The counter resets each time the menu is opened (toggled on).
		static int  s_menuOpenGuard = 0;
		static bool s_wasMenuOpen = false;

		const bool menuOpen = (get_menu_status() != 0);
		if (menuOpen && !s_wasMenuOpen)
			s_menuOpenGuard = 0;  // menu just opened: reset guard
		s_wasMenuOpen = menuOpen;

		if (s_menuOpenGuard < 6)
		{
			++s_menuOpenGuard;
			return;
		}

		// Run joystick directional state through Check_Input for proper
		// acceleration and repeat timing - same system used by the GUI driver.
		// Slots JOY_INPUT_UP/DOWN/LEFT/RIGHT/SELECT/BACK are reserved for
		// menu joystick navigation (see inptport.h).
		const bool joyUp = Check_Input(JOY_INPUT_UP, joy[0].stick[0].axis[1].d1) != 0;
		const bool joyDown = Check_Input(JOY_INPUT_DOWN, joy[0].stick[0].axis[1].d2) != 0;
		const bool joyLeft = Check_Input(JOY_INPUT_LEFT, joy[0].stick[0].axis[0].d1) != 0;
		const bool joyRight = Check_Input(JOY_INPUT_RIGHT, joy[0].stick[0].axis[0].d2) != 0;

		// A button (0) only for select inside the menu.
		// Start (7) is intentionally excluded here - it is reserved for the
		// LS + Start combo that opens/closes the menu, and allowing it as a
		// bare select would make it too easy to accidentally confirm items
		// when attempting to close the menu with the combo.
		const bool joySelect = KeyFlip(JOY_INPUT_SELECT,
			joy[0].button[0].b != 0) != 0;

		// B button (1) acts as Back/ESC within the menu only.
		// Uses KeyFlip so it fires once per press rather than every frame.
		const bool joyBack = KeyFlip(JOY_INPUT_BACK,
			joy[0].button[1].b != 0) != 0;

		if (osd_key_pressed_memory_repeat(OSD_KEY_UI_UP, 4) || joyUp)   change_menu_level(0);
		if (osd_key_pressed_memory_repeat(OSD_KEY_UI_DOWN, 4) || joyDown) change_menu_level(1);

		// LEFT/RIGHT: keyboard uses hold-to-ramp, joystick uses Check_Input ramp.
		static int left_hold = 0, left_counter = 0;
		static int right_hold = 0, right_counter = 0;

		const bool left_key_down = (osd_key_pressed(OSD_KEY_UI_LEFT) != 0);
		const bool right_key_down = (osd_key_pressed(OSD_KEY_UI_RIGHT) != 0);

		if (!left_key_down) { left_hold = 0; left_counter = 0; }
		if (!right_key_down) { right_hold = 0; right_counter = 0; }

		// Joystick left handled entirely by Check_Input (has its own ramp).
		if (joyLeft)
		{
			change_menu_item(0);
		}
		else if (osd_key_pressed_memory(OSD_KEY_UI_LEFT))
		{
			change_menu_item(0);
			left_hold = left_counter = 0;
		}
		else if (left_key_down)
		{
			++left_hold;
			if (left_hold > 10)
			{
				int div = (left_hold > 45) ? 2 : (left_hold > 25) ? 3 : 5;
				if ((++left_counter % div) == 0) change_menu_item(0);
			}
		}

		// Joystick right handled entirely by Check_Input (has its own ramp).
		if (joyRight)
		{
			change_menu_item(1);
		}
		else if (osd_key_pressed_memory(OSD_KEY_UI_RIGHT))
		{
			change_menu_item(1);
			right_hold = right_counter = 0;
		}
		else if (right_key_down)
		{
			++right_hold;
			if (right_hold > 10)
			{
				int div = (right_hold > 45) ? 2 : (right_hold > 25) ? 3 : 5;
				if ((++right_counter % div) == 0) change_menu_item(1);
			}
		}

		if (osd_key_pressed_memory(OSD_KEY_UI_SELECT) || joySelect)
			select_menu_item();

		// -------------------------------------------------------------------------
		// B button Back: collapse sub-menus or close the menu entirely.
		// Mirrors exactly what keyboard ESC does when the menu is open.
		// Only reachable here (inside get_menu_status() block) so it cannot
		// accidentally trigger an exit when the menu is closed.
		// -------------------------------------------------------------------------
		if (joyBack)
		{
			if (get_menu_level() > 100)
			{
				// We are in a sub-menu: collapse back to the top level.
				set_menu_level_top();
			}
			else
			{
				// Already at the top menu level: close the menu entirely.
				set_menu_level_top();
				set_menu_status(0);
			}
		}
	}
}

// ---------------------------------------------------------------------------
// emulator_run
// Per-frame update function. Called once per frame from WinMain's message
// loop. Drives the full emulation cycle: input -> CPU -> video -> audio.
//
// NOTE: All ESC/cancel/exit logic is now unified in msg_loop() above.
// The old duplicate OSD_KEY_ESC check that lived here has been removed
// because msg_loop() already handles OSD_KEY_CANCEL (which maps to ESC)
// with full joystick combo support and confirm_exit dialog integration.
// ---------------------------------------------------------------------------
void emulator_run()
{
	// Handle hotkeys (pause, throttle, menu, ESC, etc.) before any game logic.
	msg_loop();

	// If msg_loop() triggered an exit or game switch, bail out immediately.
	if (done)
		return;

	//-------------------- THIS IS A TEMPORARY HACK - REMOVE WHEN MOVED TO LAYOUTS FOR VECTOR --------------------------
	// --- Vertical vector bezel toggle detection ---
	// When the player toggles bezel art on/off for a vertical vector game,
	// the window needs to resize: 4:3 with bezel, thinner without.
	// Raster games handle this through the layout system; vector games
	// don't use layouts yet, so we detect the change here per-frame.
	{
		bool isVector = (Machine->gamedrv->video_attributes & VIDEO_TYPE_VECTOR) != 0;
		bool isVertical = (Machine->gamedrv->rotation & ORIENTATION_SWAP_XY) != 0;

		if (isVector && isVertical && g_bezelAvailable)
		{
			int curBezel = (config.bezel != 0) ? 1 : 0;
			if (g_lastBezelState != curBezel)
			{
				g_lastBezelState = curBezel;

				// If user has an aspect override active, always use that
				auto& ws = GetWindowSetup();
				if (ws.aspectOverrideActive && ws.aspectRatio > 0.0f)
				{
					WindowUtil_UpdateAspect(ws.aspectRatio);
				}
				else if (curBezel)
				{
					// Bezel turned ON -> force 4:3
					WindowUtil_UpdateAspect(4.0f / 3.0f);
				}
				else
				{
					// Bezel turned OFF -> use natural game aspect
					float aspect = Layout_ComputeGameAspect();
					if (aspect > 0.0f)
						WindowUtil_UpdateAspect(aspect);
				}
			}
		}
	}
	//-------------------- THIS IS A TEMPORARY HACK - REMOVE WHEN MOVED TO LAYOUTS FOR VECTOR --------------------------

	static double s_lastAudioMs = 0.0;
	const double  nowMs = TimerGetTimeMS();

	chrono::steady_clock::time_point begin;
	if (config.debug_profile_code)
	{
		begin = chrono::steady_clock::now();
		LOG_INFO("Frame start");
	}

	// Try to load the high score table if not yet done.
	if (hiscoreloaded == 0 && Machine->gamedrv->hiscore_load)
		hiscoreloaded = (*Machine->gamedrv->hiscore_load)();

	// For vector games, prepare the FBO render target before CPU/game runs.
	// Raster games also call set_render() now -- it binds fbo_raster with the
	// correct Y-down ortho so raster_poly_update() lands in the right place.
	set_render();

	// ALWAYS poll inputs so that hotkeys, menu, and joystick mapping
	// continue to function even when the game is paused.
	if (have_error == 0)
	{
		update_input_ports();
		osd_poll_joysticks();
	}

	if (!paused && have_error == 0)
	{
		if (config.debug_profile_code) LOG_INFO("CPU run");

		cpu_run();

		if (Machine->gamedrv->run_game)
			Machine->gamedrv->run_game();
	}

	// Complete rendering and present to the screen.
	// render() dispatches to final_render() (vector) or final_render_raster()
	// (raster) which handles all compositing, artwork, overlays, and the blit.
	render();

	inputport_vblank_end();
	cpu_clear_cyclecount_eof();

	if (config.debug_profile_code)
	{
		auto diff = chrono::steady_clock::now() - begin;
		LOG_INFO("Frame time (pre-throttle): %.3f ms",
			chrono::duration<double, milli>(diff).count());
	}

	GLSwapBuffers();

	if (throttle)
	{
		FrameLimiter::Throttle();
		mixer_update();
	}
	else
	{
		// Uncapped (benchmark) mode: cap audio servicing to ~60 Hz.
		if ((nowMs - s_lastAudioMs) >= (1000.0 / 60.0))
		{
			mixer_update();
			s_lastAudioMs = nowMs;
		}
	}

	// Rolling FPS average (skip first 60 frames while things settle).
	const double now = TimerGetTimeMS();
	if (frames > 60)
	{
		++frameavg;
		if (frameavg > 10000) { frameavg = 0; fps_count = 0.0; }
		const double dt = now - g_lastFrameTimestampMs;
		if (dt > 0.0) fps_count += 1000.0 / dt;
	}
	g_lastFrameTimestampMs = now;

	if (++frames > 0x0fffffff) frames = 0;

	if (config.debug_profile_code) LOG_INFO("Frame end");
}

// ---------------------------------------------------------------------------
// emulator_init
// Entry point called from WinMain. Parses command-line arguments, selects
// the starting game (or GUI), and kicks off the first run_game() call.
// ---------------------------------------------------------------------------
void emulator_init(int argc, char** argv)
{
	os_init_input();

	const auto& reg = aae::AllDrivers();
	num_games = static_cast<int>(reg.size());
	LOG_INFO("Registered drivers: %d", num_games);

	// Resolve the GUI driver index at runtime (not hardcoded).
	g_guiGameIndex = -1;
	{
		const AAEDriver* guiDrv = aae::FindDriverByName("gui");
		if (guiDrv)
		{
			for (int i = 0; i < (int)reg.size(); ++i)
			{
				if (reg[i] == guiDrv) { g_guiGameIndex = i; break; }
			}
		}
	}

	if (g_guiGameIndex < 0)
	{
		LOG_INFO("WARNING: GUI driver not found. Defaulting to gamenum=0.");
		gamenum = 0;
	}
	else
	{
		gamenum = g_guiGameIndex;
	}

	// Force config screen dimensions to the current desktop resolution.
	int horizontal = 0, vertical = 0;
	GetDesktopResolution(horizontal, vertical);
	LOG_INFO("Desktop resolution: %d x %d", horizontal, vertical);
	config.screenw = horizontal;
	config.screenh = vertical;

	game_loaded_sentinel = false;

	// Parse -debug flag early so it is active before run_game() starts.
	// Minimal, correct parsing for "-debug [0|1]".
	// -debug       => config.debug = 1
	// -debug 0/1   => config.debug = 0/1

	for (int i = 1; i < argc; ++i)
	{
		if (!argv[i]) continue;
		const std::string arg = to_lowercase(argv[i]);
		if (arg == "-debug")
		{
			int val = 1;
			if (i + 1 < argc && argv[i + 1] &&
				(std::strcmp(argv[i + 1], "0") == 0 || std::strcmp(argv[i + 1], "1") == 0))
			{
				val = (argv[i + 1][0] == '1') ? 1 : 0;
			}
			config.debug = val;
			debug_override = val;
		}
	}

	// Normalize all command-line args to lowercase before further parsing.
	for (int i = 1; i < argc; ++i)
	{
		if (!argv[i]) continue;
		if (argv[i][0] == '-' || argv[i][0] == '/')
			toLowerCase(argv[i] + 1);   // skip the dash/slash prefix
		else
			toLowerCase(argv[i]);
	}

	// Handle global utility options that do not require a game to be loaded.
	// These run before any game is selected and exit immediately when done.
	for (int i = 1; i < argc; ++i)
	{
		if (!argv[i]) continue;

		if (std::strcmp(argv[i], "-listromstotext") == 0)
		{
			LOG_INFO("Listing all ROMs to text...");
			list_all_roms();
			exit(0);
		}

		if (std::strcmp(argv[i], "-listallgames") == 0)
		{
			LOG_INFO("Listing all games to text...");
			list_all_games();
			exit(0);
		}
	}

	// Pick the first non-option token as the game name (if any).
	// If none is found, the GUI will start instead.
	const char* gameArg = nullptr;
	int         gameArgIndex = -1;
	for (int i = 1; i < argc; ++i)
	{
		if (!argv[i] || argv[i][0] == '\0') continue;
		if (argv[i][0] == '-' || argv[i][0] == '/') continue;
		gameArg = argv[i];
		gameArgIndex = i;
		break;
	}

	started_from_command_line = 0;

	if (gameArg)
	{
		LOG_INFO("Game argument: %s (argv[%d])", gameArg, gameArgIndex);
		for (int i = 0; i < num_games; ++i)
		{
			if (reg[i] && reg[i]->name && std::strcmp(gameArg, reg[i]->name) == 0)
			{
				gamenum = i;
				started_from_command_line = 1;
				break;
			}
		}
		if (!started_from_command_line)
			LOG_INFO("Unknown game '%s' -> starting GUI.", gameArg);
	}
	else
	{
		LOG_INFO("No game on command line -> starting GUI (gamenum=%d).", gamenum);
	}

	// gameparse() handles -listroms, -verifyroms, resolution overrides, etc.
	// Only call it when a matching game was found and extra args were provided.
	if (started_from_command_line)
		gameparse(argc, argv);

	frames = 0;
	showinfo = 0;
	done = 0;
	have_error = 0;

	srand((unsigned)time(nullptr));

	FrameLimiter::Init(reg.at(gamenum)->fps);
	g_lastFrameTimestampMs = TimerGetTimeMS();

	// Build the game list used by the GUI for display/selection.
	gList.build(aae::AllDrivers());

	// Clear any stale per-game state before the first run.
	ResetPerGameRuntimeState();

	run_game();
	LOG_INFO("run_game() returned.");
}

// ---------------------------------------------------------------------------
// emulator_is_game_running
// Returns true only when a game has successfully completed initialization.
// ---------------------------------------------------------------------------
bool emulator_is_game_running()
{
	return game_loaded_sentinel;
}

// ---------------------------------------------------------------------------
// emulator_get_current_game
// Returns the registry index of the currently running (or last started) game.
// ---------------------------------------------------------------------------
int emulator_get_current_game()
{
	return gamenum;
}

// ---------------------------------------------------------------------------
// emulator_is_gui_active
// Returns true when the currently running "game" is the GUI frontend driver.
// Compares gamenum against the index of the driver named "gui" in the
// registry. Any code that needs to distinguish GUI context from real gameplay
// (e.g. deciding whether to save settings to aae.ini vs a per-game ini)
// should call this rather than comparing gamenum directly.
// ---------------------------------------------------------------------------
bool emulator_is_gui_active()
{
	const int guiIdx = FindGuiGameIndex();
	return (guiIdx >= 0 && gamenum == guiIdx);
}

// ---------------------------------------------------------------------------
// emulator_stop_game
// Stops the current game and releases all per-game resources.
// Safe to call when no game is running.
// ---------------------------------------------------------------------------
void emulator_stop_game()
{
	if (!game_loaded_sentinel)
	{
		ResetPerGameRuntimeState();
		return;
	}

	LOG_INFO("Stopping game: %s",
		(Machine && Machine->gamedrv) ? Machine->gamedrv->name : "UNKNOWN");

	// 1) Save persistent data before teardown.
	if (hiscoreloaded != 0 && Machine->gamedrv->hiscore_save)
		(*Machine->gamedrv->hiscore_save)();

	if (Machine->gamedrv->nvram_handler)
	{
		void* f = osd_fopen(Machine->gamedrv->name, 0, OSD_FILETYPE_NVRAM, 1);
		if (f)
		{
			(*Machine->gamedrv->nvram_handler)(f, 1);
			osd_fclose(f);
		}
	}

	// 2) Driver-specific shutdown.
	if (Machine->gamedrv->end_game)
		Machine->gamedrv->end_game();

	// 3) Input ports.
	save_input_port_settings();
	if (Machine->input_ports)
	{
		free(Machine->input_ports);
		Machine->input_ports = nullptr;
		LOG_INFO("Input ports freed.");
	}

	// 4) CPU and memory.
	free_cpu_memory();
	free_all_memory_regions();

	// 5) Audio.
	mixer_end();

	// 6) Video and per-game GL assets.
	shutdown_raster_overlay();
	fbo_shutdown_raster();
	destroy_all_textures();

	// 6.5) Layout teardown (new artwork system).
	Layout_FreeTextures(g_layoutData);
	g_layoutData.elements.clear();
	g_layoutData.views.clear();
	g_activeView = nullptr;
	g_layoutEnabled = false;

	// 7) Frame limiter (per-game because FPS varies between games).
	FrameLimiter::Shutdown();

	// 8) Reset the RunningMachine struct for the next game.
	memset(&machine, 0, sizeof(machine));
	Machine = &machine;

	// 9) Clear all per-game globals.
	ResetPerGameRuntimeState();
	// Layout state is per-game; reset for the next game.
	g_activeView = nullptr;
	g_layoutEnabled = false;

	LOG_INFO("Game stopped.");
}

// ---------------------------------------------------------------------------
// emulator_start_game
// Stops any running game and starts the one at the given registry index.
// Returns true on success, false if the index is invalid or init fails.
// ---------------------------------------------------------------------------
bool emulator_start_game(int newGameNum)
{
	const auto& reg = aae::AllDrivers();
	if (newGameNum < 0 || newGameNum >= (int)reg.size())
	{
		LOG_INFO("emulator_start_game: invalid index %d", newGameNum);
		return false;
	}

	emulator_stop_game();

	gamenum = newGameNum;
	have_error = 0;
	done = 0;

	FrameLimiter::Init(reg.at(gamenum)->fps);
	g_lastFrameTimestampMs = TimerGetTimeMS();

	LOG_INFO("emulator_start_game: %d (%s)", gamenum, reg.at(gamenum)->name);

	run_game();

	return (have_error == 0);
}

// ---------------------------------------------------------------------------
// emulator_end
// Final application teardown. Always safe to call.
// ---------------------------------------------------------------------------
void emulator_end()
{
	LOG_INFO("Emulator shutting down...");
	emulator_stop_game();
	osd_set_leds(0);
	LOG_INFO("Emulator shutdown complete.");
}