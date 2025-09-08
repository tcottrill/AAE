//==========================================================================
// AAE is a poorly written M.A.M.E (TM) derivitave based on early MAME
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

#include "framework.h"
#include "aae_emulator.h"
#include "aae_mame_driver.h"
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include "wintimer.h"
#include "acommon.h"
#include "fileio/texture_handler.h"
#include "config.h"
#include <mmsystem.h>
#include "rand.h"
#include "glcode.h"
#include "menu.h"
#include "aae_avg.h"
#include "fpsclass.h"
#include "os_input.h"
#include "timer.h"
#include "vector_fonts.h"
#include "gl_texturing.h"
#include "mixer.h"
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <cstring>   // strlen
#include "utf8conv.h"
#include "old_mame_raster.h"
#include "osd_video.h"
#include "game_list.h"
//#include <thread>
//New 2024
#include "os_basic.h"
#include <chrono>

#ifndef WIN7BUILD
#include "win10_win11_required_code.h"
#endif // WIN7BUILD

#include <algorithm>
#include "FrameLimiter.h"
#include "driver_compat.h"
#include "driver_registry.h"   // AllDrivers(), FindDriverByName(), AAE_REGISTER_DRIVER

static double g_lastFrameTimestampMs = 0.0;

//#include <cctype>
//#include <cstdlib>     // for exit()
//#include <iostream>    // optional logging

using namespace std;
using namespace chrono;

//TEST VARIABLES
static int res_reset;
static int x_override;
static int y_override;
static int win_override = 0;

#pragma warning( disable : 4996 4244)

static int started_from_command_line = 0;
int hiscoreloaded;
//int sys_paused = 0;
int show_fps = 0;
FpsClass* m_frame; //For frame counting. Prob needs moved out of here really.

static int throttle = 1;
//New 2025
static bool game_loaded_sentinel = 0;
int art_loaded[6] = {};
extern int leds_status;

//GameList gList(&driver[0]);
GameList gList;

//AAEDriver* driver = nullptr;

// M.A.M.E. (TM) Variables for testing
static struct RunningMachine machine;
struct RunningMachine* Machine = &machine;
static const struct AAEDriver* gamedrv;
static const struct AAEDriver* drv;
//static const struct MachineDriver* drv;
struct GameOptions	options;

// --- Optional MMCSS ---
static HANDLE mmcssHandle = nullptr;

struct
{
	const char* desc;
	int x, y;
}

gfx_res[] =
{
	{ "-320x240"	, 320, 240 },
	{ "-512x384"	, 512, 384 },
	{ "-640x480"	, 640, 480 },
	{ "-800x600"	, 800, 600 },
	{ "-1024x768"	, 1024, 768 },
	{ "-1152x720"	, 1152, 720 },
	{ "-1152x864"	, 1152, 864 },
	{ "-1280x768"	, 1280, 768 },
	{ "-1280x1024"	, 1280, 1024 },
	{ "-1600x1200"	, 1600, 1200 },
	{ "-1680x1050"	, 1680, 1050 },
	{ "-1920x1080"	, 1920, 1080 },
	{ "-1920x1200"	, 1920, 1200 },
	{ NULL		, 0, 0 }
};

double gametime = 0.0;// = TimerGetTimeMS();
static double starttime = 0.0;

///////////////////////////////////////  RASTER CODE START  ////////////////////////////////////////////////////
// This is only a test. Trying out new things.

float vid_scale = 3.0;

// -----------------------------------------------------------------------------
// SetGamePerformanceMode
// -----------------------------------------------------------------------------
void SetGamePerformanceMode(const settings& config)
{
	// --- Process Priority ---
	DWORD procPriority = NORMAL_PRIORITY_CLASS;
	const char* priorityName = "NORMAL_PRIORITY_CLASS";

	switch (config.priority)
	{
	case 4:
		procPriority = REALTIME_PRIORITY_CLASS;
		priorityName = "REALTIME_PRIORITY_CLASS";
		break;
	case 3:
		procPriority = HIGH_PRIORITY_CLASS;
		priorityName = "HIGH_PRIORITY_CLASS";
		break;
	case 2:
		procPriority = ABOVE_NORMAL_PRIORITY_CLASS;
		priorityName = "ABOVE_NORMAL_PRIORITY_CLASS";
		break;
	case 1:
		procPriority = NORMAL_PRIORITY_CLASS;
		priorityName = "NORMAL_PRIORITY_CLASS";
		break;
	case 0:
		procPriority = IDLE_PRIORITY_CLASS;
		priorityName = "IDLE_PRIORITY_CLASS";
		break;
	default:
		LOG_INFO("Unknown priority level: %d", config.priority);
		break;
	}

	if (!SetPriorityClass(GetCurrentProcess(), procPriority))
		LOG_INFO("Failed to set process priority to %s (Error %lu)", priorityName, GetLastError());
	else
		LOG_INFO("Process priority set to %s", priorityName);

	// --- Optional Thread Boost ---
	if (config.boostThread)
	{
		if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL))
			LOG_INFO("Failed to boost thread priority. Error: %lu", GetLastError());
		else
			LOG_INFO("Main thread boosted to THREAD_PRIORITY_ABOVE_NORMAL");
	}

	//DWORD_PTR cores = (1ULL << std::thread::hardware_concurrency()) - 1;
	//SetThreadAffinityMask(GetCurrentThread(), cores);
}

///////////////////////////////////////  RASTER CODE END    ////////////////////////////////////////////////////

/*************************************
 *
 *	To lower: helper function.
 *
 *************************************/

void toLowerCase(char* str) {
	while (*str) {
		*str = tolower((unsigned char)*str); // Convert each character to lowercase
		str++;
	}
}

/*************************************
 *
 *	Set's up the pointers to
 *  link to MAME Code.
 *
 *************************************/

int run_a_game(int game)
{
	//Machine->gamedrv = gamedrv = &driver[game];
	Machine->gamedrv = gamedrv = aae::AllDrivers().at(game);
	Machine->drv = gamedrv;
	LOG_INFO("Starting game, Driver name now is %s", Machine->gamedrv->name);
	return 0;
}

/*************************************
 *
 *	Clamp to 255, not used?
 *
 *************************************/

static int clamp(int value)
{
	if (value < 0) return 0;
	if (value > 255) return 255;
	return value;
}

/*************************************
 *
 *	Compare Helper function
 *
 *************************************/

int mystrcmp(const char* s1, const char* s2)
{
	while (*s1 && *s2 && *s1 == *s2) {
		s1++;
		s2++;
	}

	return *s1 - *s2;
}

/****************************************
 *
 *	Output a full romlist as a text file
 *
 *
 ****************************************/

constexpr int REGION_TAG = 999;

void list_all_roms()
{
	const auto& reg = aae::AllDrivers();
	const int game_count = static_cast<int>(reg.size());
	if (game_count <= 0) return;

	// ---------------------------------------------------------------------
	// Dynamic buffer (pre-reserve old malloc size to avoid realloc churn)
	// ---------------------------------------------------------------------
	std::vector<char> buf;
	buf.reserve(0x150000);

	// ---------------------------------------------------------------------
	// Local helpers – lambdas capture buf by reference
	// ---------------------------------------------------------------------
	const auto append_cstr = [&](const char* s) {
		if (!s) return;
		buf.insert(buf.end(), s, s + std::strlen(s));
		};
	const auto append_str = [&](const std::string& s) {
		buf.insert(buf.end(), s.begin(), s.end());
		};
	const auto append_chr = [&](char c) {
		buf.push_back(c);
		};

	// ---------------------------------------------------------------------
	// Start of file
	// ---------------------------------------------------------------------
	append_cstr("AAE All Games RomList\n");

	for (int g = 0; g < game_count; ++g)
	{
		const AAEDriver* drv = reg[g];
		if (!drv || !drv->name || !drv->desc || !drv->rom) continue;

		append_cstr("\nGame Name: ");
		append_cstr(drv->desc);
		append_cstr(":\nRom  Name: ");
		append_cstr(drv->name);
		append_cstr(".zip\n");

		LOG_INFO("gamename %s", drv->name);
		LOG_INFO("gamedesc %s", drv->desc);

		for (int r = 0; drv->rom[r].romSize > 0; ++r)
		{
			const RomModule& rm = drv->rom[r];

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
			oss << " Size: 0x"
				<< std::hex << std::uppercase << std::setw(4) << std::setfill('0') << rm.romSize
				<< " Load Addr: 0x"
				<< std::hex << std::uppercase << std::setw(4) << std::setfill('0') << rm.loadAddr
				<< " CRC: 0x"
				<< std::hex << std::uppercase << std::setw(8) << std::setfill('0') << rm.crc
				<< " SHA1: " << (rm.sha ? rm.sha : "NULL") << '\n';

			append_str(oss.str());
		}
	}

	// Footer (use actual registry size; drop “-1” unless you intentionally skip an entry)
	std::ostringstream footer;
	footer << "\n\nNumber of Games/Clones supported: " << game_count << '\n';
	append_str(footer.str());

	// Ensure C-string termination for save routine
	append_chr('\0');

	// ---------------------------------------------------------------------
	// Save to file
	// ---------------------------------------------------------------------
	LOG_INFO("SAVING Romlist");
	save_file_char("AAE All Game Roms List.txt",
		buf.data(),
		static_cast<int>(buf.size() - 1));   // exclude null

	// release memory (optional)
	std::vector<char>().swap(buf);
}

/*************************************
 *
 *	Command Line Parsing Function
 *
 *************************************/

 // -----------------------------------------------------------------------------
 // Helper: convert to lowercase
 // -----------------------------------------------------------------------------
static std::string to_lowercase(const char* str) {
	std::string s(str ? str : "");
	std::transform(s.begin(), s.end(), s.begin(),
		[](unsigned char c) { return std::tolower(c); });
	return s;
}

// -----------------------------------------------------------------------------
// gameparse
// Secure, modern version of the original gameparse().
// - Uses lowercase args to simplify comparisons
// - Uses std::string for safe concatenation
// - Avoids all unsafe functions (malloc/strcpy/strcat)
// -----------------------------------------------------------------------------
void gameparse(int argc, char* argv[])
{
	int x = 0;
	int w = 0;
	int list = 0;
	int retval = 0;

	win_override = 0; // Set default before parsing

	// ------------------------------
	// Parse command-line arguments
	// ------------------------------
	for (int i = 1; i < argc; i++) {
		std::string arg = to_lowercase(argv[i]); // lowercase once

		if (arg == "-listroms")        list = 1;
		else if (arg == "-verifyroms") list = 2;
		else if (arg == "-listsamples") list = 3;
		else if (arg == "-verifysamples") list = 4;
		else if (arg == "-window")     win_override = 3;
		else if (arg == "-nowindow")   win_override = 2;

		// Match against gfx_res (also lowercase for comparison)
		for (int j = 0; gfx_res[j].desc != nullptr; j++) {
			std::string res = to_lowercase(gfx_res[j].desc);
			if (arg == res) {
				x_override = gfx_res[j].x;
				y_override = gfx_res[j].y;
				break;
			}
		}
	}

	// ------------------------------
	// Build log output safely
	// ------------------------------
	std::string logOutput;
	logOutput.reserve(65536); // reserve to avoid reallocations

	// ------------------------------
	// Process requested operation
	// ------------------------------
	switch (list)
	{
		// -------------------------------------------------------------------------
	case 1: // List ROMs
		logOutput.clear();
		x = 0;
		LOG_INFO("%s rom list:", driver[gamenum].name);

		while (driver[gamenum].rom[x].romSize > 0) {
			const auto& rom = driver[gamenum].rom[x];
			const char* fname = rom.filename;

			// If this is a ROM_REGION marker, log region name
			if (rom.loadAddr == ROM_REGION_START && rom.loadtype >= 0 && rom.loadtype < REGION_MAX) {
				LOG_INFO("Region: %s", rom_regions[rom.loadtype]);
			}

			// Skip ROM_RELOAD and ROM_CONTINUE
			if (fname && fname != (char*)-1 && fname != (char*)-2 &&
				rom.loadAddr != ROM_REGION_START &&
				rom.loadAddr != 0x999)
			{
				// Format CRC string
				char crcbuf[16];
				snprintf(crcbuf, sizeof(crcbuf), "%08X", rom.crc);

				// Compose full line
				logOutput = std::string(fname) +
					" | CRC: " + crcbuf +
					" | SHA1: " + (rom.sha ? rom.sha : "NULL");

				LOG_INFO("%s", logOutput.c_str());
				logOutput.clear();
			}
			x++;
		}

		LogClose();
		exit(1);
		break;

		// -------------------------------------------------------------------------
	case 2: // Verify ROMs
		//setup_game_config();
		//sanity_check_config();

		logOutput.clear();
		x = 0;
		LOG_INFO("Starting ROM verification for %s", driver[gamenum].name);

		while (driver[gamenum].rom[x].romSize > 0) {
			const char* fname = driver[gamenum].rom[x].filename;
			if (fname && fname != (char*)-1 && fname != (char*)-2 &&
				driver[gamenum].rom[x].loadAddr != ROM_REGION_START &&
				driver[gamenum].rom[x].loadAddr != 0x999)
			{
				logOutput += fname;
				LOG_INFO("LOOP ");
				retval = verify_rom(driver[gamenum].name, driver[gamenum].rom, x);
				switch (retval) {
				case 0: logOutput += " BAD? "; break;
				case 1: logOutput += " OK "; break;
				case 3: logOutput += " BADSIZE "; break;
				case 4: logOutput += " NOFILE "; break;
				case 5: logOutput += " NOZIP "; break;
				default: logOutput += " UNKNOWN "; break;
				}

				logOutput += (w > 1) ? "\n" : " ";
				w = (w > 1) ? 0 : w + 1;
				LOG_INFO("%s", logOutput.c_str());
				logOutput.clear();
			}
			x++;
		}
		LogClose();
		exit(1);
		break;

		// -------------------------------------------------------------------------
	case 3: // List Samples
		logOutput.clear();
		if (Machine->gamedrv->game_samples) {
			const char** samples = Machine->gamedrv->game_samples;
			for (int i = 0; samples[i] && std::strcmp(samples[i], "NULL") != 0; ++i) {
				logOutput += samples[i];
			}
		}
		LOG_INFO("%s sample list: %s", Machine->gamedrv->name, logOutput.c_str());
		LogClose();
		exit(1);
		break;

		// -------------------------------------------------------------------------
	case 4: // Verify Samples
		logOutput.clear();
		LOG_INFO("Starting sample verification...");
		LOG_INFO("Starting sample verification.");
		if (Machine->gamedrv->game_samples) {
			const char** samples = Machine->gamedrv->game_samples;
			for (int i = 0; samples[i] && std::strcmp(samples[i], "NULL") != 0; ++i) {
				logOutput += samples[i];

				retval = verify_sample(samples, i);
				switch (retval) {
				case 0: logOutput += " BAD? "; break;
				case 1: logOutput += " OK ";    break;
				case 3: logOutput += " BADSIZE "; break;
				case 4: logOutput += " NOFILE ";  break;
				case 5: logOutput += " NOZIP ";   break;
				default: logOutput += " UNKNOWN "; break;
				}

				logOutput += (w > 1) ? "\n" : " ";
				w = (w > 1) ? 0 : w + 1;
			}
		}
		LOG_INFO("%s sample verify: %s", Machine->gamedrv->name, logOutput.c_str());
	}
}

/*************************************
 *
 *	MAME Shadow input port creation for
 * the Menu system and save
 *
 *************************************/

int init_machine(void)
{
	LOG_INFO("Calling init machine to build shadow input ports");

	if (gamedrv->input_ports)
	{
		int total = 1;
		const struct InputPort* from;
		struct InputPort* to;

		from = gamedrv->input_ports;

		total = 0;
		do
		{
			total++;
		} while ((from++)->type != IPT_END);

		if ((Machine->input_ports = (InputPort*)malloc(total * sizeof(struct InputPort))) == 0)
			return 1;

		from = gamedrv->input_ports;
		to = Machine->input_ports;

		do
		{
			memcpy(to, from, sizeof(struct InputPort));

			to++;
		} while ((from++)->type != IPT_END);
	}
	return 0;
}

/*************************************
 *
 *	Main Message Loop to handle other
 * input during the emulation loop
 *
 *************************************/

void msg_loop(void)
{
	if (osd_key_pressed_memory(OSD_KEY_RESET_MACHINE))
	{
		cpu_reset(0);
	}

	if (osd_key_pressed_memory(OSD_KEY_P))
	{
		paused ^= 1;
		if (paused) pause_audio(); else restore_audio();
		//else resume_audio();
	}

	if (osd_key_pressed_memory(OSD_KEY_SHOW_FPS))
	{
		show_fps ^= 1;
	}

	if (osd_key_pressed_memory(OSD_KEY_F12))
	{
		snapshot();
	}

	if (osd_key_pressed_memory(OSD_KEY_F10))
	{
		throttle ^= 1;
		frameavg = 0; fps_count = 0;
		SetvSync(throttle);
	}

	if (osd_key_pressed_memory(OSD_KEY_CONFIGURE))
	{
		int m = get_menu_status();
		m ^= 1;
		set_menu_status(m);
	}
	//This is the cascading exit code.
	if (osd_key_pressed_memory(OSD_KEY_CANCEL))
	{
		if (get_menu_status())
		{
			if (get_menu_level() > 100)
			{
				set_menu_level_top();
			}
			else
			{
				set_menu_level_top();
				set_menu_status(0);
			}
		}
		else
		{
			done = 1; // Exiting.
		}
	}
	int a = get_menu_level();
	if (a != 700)
	{
		if (get_menu_status()) // Work around for ALT-ENTER Issue. Only read these keys if IN MENU
		{
			if (osd_key_pressed_memory_repeat(OSD_KEY_UI_UP, 4)) { change_menu_level(0); } //up
			if (osd_key_pressed_memory_repeat(OSD_KEY_UI_DOWN, 4)) { change_menu_level(1); } //down
			if (osd_key_pressed_memory(OSD_KEY_UI_LEFT)) { change_menu_item(0); } //left
			if (osd_key_pressed_memory(OSD_KEY_UI_RIGHT)) { change_menu_item(1); } //right
			if (osd_key_pressed_memory(OSD_KEY_UI_SELECT)) { select_menu_item(); }  // Enter
		}
	}
}

/*************************************
 *
 *	Main Game load and
 *  configuration function.
 *
 *************************************/

void run_game(void)
{
	DEVMODE dvmd = { 0 };
	int cx = GetSystemMetrics(SM_CXSCREEN);
	// height
	int cy = GetSystemMetrics(SM_CYSCREEN);
	//width
	int retval = 0;
	int goodload = 99;
	int testwidth = 0;
	int testheight = 0;
	int testwindow = 0;
	double starttime = 0;
	double gametime = 0;
	double millsec = 0;

	num_samples = 0;
	errorlog = 0;

	EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &dvmd);
	// ------------Setup all the aliases for Machine, gamedrv, drv, etc. -------------
	run_a_game(gamenum);

	setup_game_config();
	sanity_check_config();

	// Now before we run a game, we have to override any command line options that were previously set,
	// because setup_game_config overrides everything.

	LOG_INFO("Running game %s", Machine->gamedrv->desc);

	//Check for setting greater then screen availability
	if (config.screenh > cy || config.screenw > cx)
	{
		allegro_message("MESSAGE", "Warning: \nphysical size smaller then config setting");
	}

	//////////////////////////////////////////////////////////////INITIAL VARIABLES SETUP ///////////////////////////////////////////////////
	options.cheat = 1;
	//set_aae_leds(0, 0, 0);  //Reset LEDS
	leds_status = 0;
	osd_set_leds(0);
	//force_all_kbdleds_off();

	config.mainvol *= 12.75;
	config.pokeyvol *= 12.75; //Adjust from menu values
	//config.noisevol *= 12.75;

	clamp(config.mainvol);
	clamp(config.pokeyvol);
	clamp(config.noisevol);
	//set_volume(config.mainvol, 0);

	art_loaded[5] = {};

	init_machine();
	reset_memory_tracking(); // Before loading Anything!!!
	//////////////////////////////////////////////////////////////END VARIABLES SETUP ///////////////////////////////////////////////////
	// Load Roms
	if (Machine->gamedrv->rom)
	{
		goodload = load_roms(Machine->gamedrv->name, Machine->gamedrv->rom);
		if (goodload == EXIT_FAILURE)
		{
			LOG_INFO("Rom loading failure, exiting...");

			have_error = 10;

			// We may have allocated regions before failing; free them so we exit cleanly.
			free_all_memory_regions();

			// Make sure we don't continue init. Mark app to quit and return out.
			done = 1;
			return;
		}
	}

	LOG_INFO("OpenGL Init");
	init_gl();
	// Load Artwork
	if (Machine->gamedrv->artwork)
	{
		load_artwork(Machine->gamedrv->artwork);
		resize_art_textures();
	}
	// Load configs for Video.
	setup_video_config();
	if (config.bezel && gamenum) { msx = b1sx; msy = b1sy; esx = b2sx; esy = b2sy; }
	else { msx = sx; msy = sy; esx = ex; esy = ey; }

	frameavg = 0; fps_count = 0; frames = 0;
	//////////////////////////

	millsec = (double)1000 / (double)Machine->gamedrv->fps;

	//Init the Mixer.
	mixer_init(config.samplerate, Machine->gamedrv->fps);

	// Load samples if the game has any. This needs to move!
	if (Machine->gamedrv->game_samples)
	{
		goodload = read_samples(Machine->gamedrv->game_samples, 0);
		if (goodload == EXIT_FAILURE) { LOG_INFO("Samples loading failure, please check error output for details..."); }
	}

	//Now load the Ambient and menu samples

	//goodload = read_samples(noise_samples, 1);
	//if (goodload == EXIT_FAILURE) { LOG_INFO("Noise Samples loading failure, not critical, continuing."); }

	LOG_INFO("Number of samples for this game is %d", num_samples);
	// Configure the Ambient Sounds.
	setup_ambient(VECTOR);
	// Setup for the first game.
	LOG_INFO("Initializing Game");
	LOG_INFO("Loading InputPort Settings");
	load_input_port_settings();
	

	// At this point, we know we are running a game, so set this to true
	game_loaded_sentinel = true;

	//Run this before Driver Init.
	if (!(Machine->gamedrv->video_attributes & VIDEO_TYPE_VECTOR))
	{
		vh_open();
	}

	Machine->gamedrv->init_game();
	init_cpu_config(); ////////////////////-----------

	hiscoreloaded = 0;

	//If the game uses NVRAM, initalize/load it. . This code is from M.A.M.E. (TM)
	if (Machine->gamedrv->nvram_handler)
	{
		void* f;

		f = osd_fopen(Machine->gamedrv->name, 0, OSD_FILETYPE_NVRAM, 0);
		(*Machine->gamedrv->nvram_handler)(f, 0);
		if (f) osd_fclose(f);
	}

	LOG_INFO("\n\n----END OF INIT -----!\n\n");

	SetGamePerformanceMode(config);

	//	LOG_INFO("Gamelist here is %s   Desc: %s", gamelist[0].displayName.c_str(), gamelist[0].description.c_str());
}

/*************************************
 *
 *	Main Emulation Loop
 *
 *************************************/

void emulator_run()
{
	//wglSwapIntervalEXT(0);
	auto begin = chrono::steady_clock::now();

	if (get_menu_status() == 0 && !paused) { ClipAndHideCursor(win_get_window()); }
	else UnclipAndShowCursor();

	if (config.debug_profile_code) {
		LOG_INFO("Start of Frame");
	}
	// Load High Score
	if (hiscoreloaded == 0 && Machine->gamedrv->hiscore_load)
		hiscoreloaded = ((*Machine->gamedrv->hiscore_load))();

	// Setup for rendering a frame.
	set_render();

	if (!paused && have_error == 0)
	{
		update_input_ports();
		osd_poll_joysticks();

		if (config.debug_profile_code) {
			LOG_INFO("Calling CPU Run");
		}
		auto start = chrono::steady_clock::now();
		cpu_run();

		if (config.debug_profile_code) {
			auto end = chrono::steady_clock::now();
			auto diff = end - start;
			LOG_INFO("Profiler: CPU Time: %f ", chrono::duration <double, milli>(diff).count());
		}

		if (Machine->gamedrv->run_game) Machine->gamedrv->run_game();
	}

	// Complete and display the rendered frame after all video updates.
	render();

	msg_loop();

	inputport_vblank_end();
	//timer_clear_all_eof();
	cpu_clear_cyclecount_eof();

	if (config.debug_profile_code) {
		auto end = chrono::steady_clock::now();
		auto diff = end - begin;
		LOG_INFO("Profiler: Total Frame Time before throttle: %f ", chrono::duration <double, milli>(diff).count());
	}

	GLSwapBuffers();

	if (throttle)
	{
		FrameLimiter::Throttle();   // waits until the next frame boundary
		mixer_update();
	}

	// Update FPS counters just like throttle_speed() did, *after* pacing.
	const double now = TimerGetTimeMS();
	if (frames > 60) {
		frameavg++;
		if (frameavg > 10000) { frameavg = 0; fps_count = 0.0; }
		const double dt = now - g_lastFrameTimestampMs;
		if (dt > 0.0) {
			fps_count += 1000.0 / dt;   // instantaneous FPS accumulator
		}
	}
	g_lastFrameTimestampMs = now;

	frames++;
	if (frames > 0xfffffff) { frames = 0; }

	if (config.debug_profile_code) {
		LOG_INFO("End of Frame");
	}
}

/*************************************
 *
 *	Init Code - Called from WinMain,
	command line arguments are passed.
 *
 *************************************/

void emulator_init(int argc, char** argv)
{
	//int i;
	int loop = 0;
	int loop2 = 0;
	//char str[20];
	//char* mylist;

	//For resolution
	int horizontal;
	int vertical;

	os_init_input();
	// We start with the running game being the gui, unless overridden by the command line.
	gamenum = 18;
	x_override = 0;
	y_override = 0;
	// Build the supported game list.
	//while (driver[loop].name != 0) { num_games++; loop++; }
	//LOG_INFO("Number of supported games is: %d", num_games);
	//num_games++;
	// Build the supported game list from the registry.
	const auto& reg = aae::AllDrivers();
	num_games = static_cast<int>(reg.size());
	LOG_INFO("Number of supported games is: %d", num_games);

	//Move this after command line processing is re-added
	GetDesktopResolution(horizontal, vertical);
	LOG_INFO("Actual primary monitor desktop screen size %d  %d", horizontal, vertical);
	//if (config.screenw == 0 && config.screenh == 0)
	//{
	config.screenw = horizontal;
	config.screenh = vertical;
	//}

	// Disable ShortCut Keys
	//AllowAccessibilityShortcutKeys(0);
	// Just to make sure we know that we have not loaded a game yet.
	game_loaded_sentinel = false;

	// Normalize all command-line args to lowercase *before* any parsing.
	// If an arg begins with '-' or '/', leave the prefix and lowercase the rest.
	for (int i = 1; i < argc; ++i)
	{
		if (!argv[i]) continue;
		if (argv[i][0] == '-' || argv[i][0] == '/')
			toLowerCase(argv[i] + 1);  // skip the dash/slash, lowercase the rest
		else
			toLowerCase(argv[i]);      // lowercase whole token (e.g., game name)
	}

	LOG_INFO(" String %s Max Num %d", argv[1], argc);

	if (argv[1] == NULL)
	{
		LOG_INFO("Usage: aae gamename -argument, -argument, etc.");
		//	show_mouse();
		allegro_message("Error", "Please run from a command prompt. \nUsage: aae gamename -argument, -argument, etc.");
		done = 1;
		emulator_end();
		return;
	}

	// Here is where we are checking command line for a supported game name.
		// If not, it jumps straight into the GUI, bleh.

//	toLowerCase(argv[1]);

	// Handle List all roms
	for (int i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "-listromstotext") == 0)
		{
			LOG_INFO("Listing all roms");
			list_all_roms();
			exit(1);
		}
	}
	
	for (int i = 0; i < num_games; ++i) {
		if (std::strcmp(argv[1], reg[i]->name) == 0) {
			gamenum = i;
			started_from_command_line = 1;
			break;
		}
	}
	//	gamenum = 83;
	if (argc > 2) gameparse(argc, argv);
	//}

	//THIS IS WHERE THE CODING STARTS
	//LOG_INFO("Number of supported joysticks: %d ", num_joysticks);
	/*
	if (num_joysticks)
	{
		poll_joystick();
		for (loop = 0; loop < num_joysticks; loop++) {
			LOG_INFO("joy %d number of sticks %d", loop, joy[loop].num_sticks);
			for (loop2 = 0; loop2 < joy[loop].num_sticks; loop2++)
			{
				LOG_INFO("stick number  %d flag %d", loop2, joy[loop].stick[loop2].flags);
				LOG_INFO("stick number  %d axis 0 pos %d", loop2, joy[loop].stick[loop2].axis[0].pos);
				LOG_INFO("stick number  %d axis 1 pos %d", loop2, joy[loop].stick[loop2].axis[1].pos);
			}
		}
	}
	*/
	frames = 0; //init frame counter

	showinfo = 0;
	done = 0;
	//////////////////////////////////////////////////////
	LOG_INFO("Number of supported games in this release: %d", num_games);
	initrand();
	//fillstars(stars);
	have_error = 0;
	FrameLimiter::Init(reg.at(gamenum)->fps);
	g_lastFrameTimestampMs = TimerGetTimeMS();
	// Build the Complete Driver List
	gList.build(aae::AllDrivers());
	run_game();
	LOG_INFO("Finished Run Game");

	// This would be a really good time to parse the rest of the command line vars 
	// and override anything loaded and set by the config. 
	int val;
	// ------------------------------ TEMP --
	// Parse command-line arguments
	// Move this to ANOTHER command 
	// line handler, or something 
	// else
	// -------------------------------------
	for (int i = 1; i < argc; i++) {
		std::string arg = to_lowercase(argv[i]); // lowercase once
			
		 if (arg == "-debug")      
		 val = 1;  // default if no explicit value
		 if (i + 1 < argc && (std::strcmp(argv[i + 1], "0") == 0 || std::strcmp(argv[i + 1], "1") == 0)) {
			 val = std::atoi(argv[i + 1]);
			 ++i; // consume next token
			 config.debug = val;
		 }
		
	 }
}

/*************************************
 *
 *	End of emulation, cleanup and get out.
 *
 *************************************/

void emulator_end()
{
	LOG_INFO("Emulator End Called.");

	// We only want to shut down the game and run all the cleanup if we actually ran one.
	if (game_loaded_sentinel == true)
	{
		// If we're using high scores, write them to disk.
		if (hiscoreloaded != 0 && Machine->gamedrv->hiscore_save)
			(*Machine->gamedrv->hiscore_save)();
		LOG_INFO("End 1");
		//If the game uses NVRAM, save it. This code is from M.A.M.E. (TM)
		if (Machine->gamedrv->nvram_handler)
		{
			void* f;

			if ((f = osd_fopen(Machine->gamedrv->name, 0, OSD_FILETYPE_NVRAM, 1)) != 0)
			{
				(*Machine->gamedrv->nvram_handler)(f, 1);
				osd_fclose(f);
			}
		}
		config.artwork = 0;
		config.bezel = 0;
		config.overlay = 0;

		//END-----------------------------------------------------
		if (Machine->gamedrv->end_game) Machine->gamedrv->end_game();
		save_input_port_settings();
		if (Machine->input_ports) {
			free(Machine->input_ports);
			LOG_INFO("Machine Input Ports Freed.");
		}

		free_cpu_memory();

		free_all_memory_regions();

		// Free samples and shutdown audio code
		mixer_end();

		// Free textures
		destroy_all_textures();
	}
	FrameLimiter::Shutdown();
	//force_all_kbdleds_off();
	osd_set_leds(0);
	LOG_INFO("End Final");
}