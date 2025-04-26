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
//#include "aaemain.h"
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include "sys_timer.h"
#include "acommon.h"
#include "fileio/loaders.h"
#include "config.h"
#include <mmsystem.h>
#include "fonts.h"
#include "gui/gui.h"
#include "gui/animation.h"
#include "gamedriver.h"
#include "rand.h"
#include "glcode.h"
#include "menu.h"
#include "aae_avg.h"
#include "fpsclass.h"
#include "vector.h"
#include "os_input.h"
#include "timer.h"
#include "vector_fonts.h"
#include "gl_texturing.h"
#include "mixer.h"

//New 2024
#include "os_basic.h"
#include <chrono>

#ifndef WIN7BUILD
#include "win10_win11_required_code.h"
#endif // WIN7BUILD


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

extern int leds_status;

// M.A.M.E. (TM) Variables for testing
static struct RunningMachine machine;
struct RunningMachine* Machine = &machine;
static const struct AAEDriver* gamedrv;
static const struct AAEDriver* drv;
//static const struct MachineDriver* drv;
struct GameOptions	options;

struct { const char* desc; int x, y; } gfx_res[] = {
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

double gametime = 0;// = TimerGetTimeMS();
double starttime = 0;

void toLowerCase(char* str) {
	while (*str) {
		*str = tolower((unsigned char)*str); // Convert each character to lowercase
		str++;
	}
}

int run_a_game(int game)
{
	Machine->gamedrv = gamedrv = &driver[game];
	Machine->drv = gamedrv;
	wrlog("Starting game, Driver name now is %s", Machine->gamedrv->name);
	//Machine->drv = drv = gamedrv->drv;

	return 0;
}

static int clamp(int value)
{
	if (value < 0) return 0;
	if (value > 255) return 255;
	return value;
}
int mystrcmp(const char* s1, const char* s2)
{
	while (*s1 && *s2 && *s1 == *s2) {
		s1++;
		s2++;
	}

	return *s1 - *s2;
}

void gameparse(int argc, char* argv[])
{
	char* mylist;
	int x = 0;
	int loop = 0;
	int w = 0;
	int list = 0;
	int retval = 0;
	int i;
	int j;

	if (gamenum == 0) return;

	win_override = 0; //Set default before checking

	for (i = 1; i < argc; i++)
	{
		if (stricmp(argv[i], "-listroms") == 0) list = 1;
		if (stricmp(argv[i], "-verifyroms") == 0) list = 2;
		if (stricmp(argv[i], "-listsamples") == 0) list = 3;
		if (stricmp(argv[i], "-verifysamples") == 0) list = 4;
		if (strcmp(argv[2], "-debug") == 0) { config.debug = 1; }
		if (strcmp(argv[i], "-window") == 0) { win_override = 3; }
		if (strcmp(argv[i], "-nowindow") == 0) { win_override = 2; }

		for (j = 0; gfx_res[j].desc != NULL; j++)
		{
			if (stricmp(argv[i], gfx_res[j].desc) == 0)
			{
				x_override = gfx_res[j].x;
				y_override = gfx_res[j].y;
				break;
			}
		}
	}

	switch (list)
	{
	case 1:

		mylist = (char*)malloc(10000);
		strcpy(mylist, "\n");
		while (driver[gamenum].rom[x].romSize > 0)
		{
			if (driver[gamenum].rom[x].loadAddr != 0x999) {
				if (driver[gamenum].rom[x].filename != (char*)-1) {
					strcat(mylist, driver[gamenum].rom[x].filename);
					if (w > 1) { strcat(mylist, "\n"); w = 0; }
					else { strcat(mylist, " "); w++; }
				}
			}
			x++;
		}

		strcat(mylist, "\0");
		//	if (gamenum > 0) { allegro_message("%s rom list: %s", driver[gamenum].name, mylist); }
		free(mylist);
		LogClose();
		exit(1); break;

	case 2:
		setup_game_config(); //Have to do this so the Rom loader can find the rom path.......
		sanity_check_config();

		mylist = (char*)malloc(0x25000);
		strcpy(mylist, "\n");
		wrlog("Starting rom verify");
		while (driver[gamenum].rom[x].romSize > 0)
		{
			if (driver[gamenum].rom[x].loadAddr != 0x999) {
				if (driver[gamenum].rom[x].filename != (char*)-1) {
					strcat(mylist, driver[gamenum].rom[x].filename);
					retval = verify_rom(driver[gamenum].name, driver[gamenum].rom, x);

					switch (retval)
					{
					case 0: strcat(mylist, " BAD? "); break;
					case 1: strcat(mylist, " OK "); break;
					case 3: strcat(mylist, " BADSIZE "); break;
					case 4: strcat(mylist, " NOFILE "); break;
					case 5: strcat(mylist, " NOZIP "); break;
					}
					if (w > 1) { strcat(mylist, "\n"); w = 0; }
					else { strcat(mylist, " "); w++; }
				}
			}
			x++;
		}
		strcat(mylist, "\0");

		//if (gamenum > 0) { allegro_message("%s rom verify: %s", driver[gamenum].name, mylist); }
		free(mylist);
		LogClose();
		exit(1);  break;

	case 3: mylist = (char*)malloc(5000);
		strcpy(mylist, "\n");
		while (strcmp(driver[gamenum].game_samples[i], "NULL")) { strcat(mylist, driver[gamenum].game_samples[i]); strcat(mylist, " "); i++; }
		strcat(mylist, "\0");
		//if (gamenum > 0) allegro_message("%s Samples: %s", driver[gamenum].name, mylist);
		free(mylist);
		LogClose();
		exit(1);
		break;
	case 4: mylist = (char*)malloc(0x11000); i = 0;
		strcpy(mylist, "\n");
		wrlog("Starting sample verify");
		while (strcmp(driver[gamenum].game_samples[i], "NULL"))
		{
			strcat(mylist, driver[gamenum].game_samples[i]);
			retval = verify_sample(driver[gamenum].game_samples, i);

			switch (retval)
			{
			case 0: strcat(mylist, " BAD? "); break;
			case 1: strcat(mylist, " OK "); break;
			case 3: strcat(mylist, " BADSIZE "); break;
			case 4: strcat(mylist, " NOFILE "); break;
			case 5: strcat(mylist, " NOZIP "); break;
			}
			if (w > 1) { strcat(mylist, "\n"); w = 0; }
			else { strcat(mylist, " "); w++; }
			i++;
		}

		strcat(mylist, "\0");

		//if (gamenum > 0) { allegro_message("%s sample verify: %s", driver[gamenum].name, mylist); }
		free(mylist);
		LogClose();
		exit(1);  break;
	}
}

void sort_games(void)
{
	int go = 0;
	int i, b;
	char tempchar[128];
	int tempgame = 0;
	int result = 0;
	int loc;

	for (i = 1; i < num_games - 1; i++)
	{
		strcpy(tempchar, gamelist[i].glname);
		loc = gamelist[i].gamenum;
		for (b = i; b < num_games - 1; b++)
		{
			result = strcmp(tempchar, gamelist[b].glname);
			if (result > 0) {
				strcpy(tempchar, gamelist[b].glname);
				tempgame = gamelist[b].gamenum;
				loc = b; //save the location of this one.
				//OK, Start the move
				strcpy(gamelist[loc].glname, gamelist[i].glname);   //move lower stuff to new location
				gamelist[loc].gamenum = gamelist[i].gamenum;

				strcpy(gamelist[i].glname, tempchar);
				gamelist[i].gamenum = tempgame;
			}
		}
	}
}

int init_machine(void)
{
	wrlog("Calling init machine to build shadow input ports");

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

void msg_loop(void)
{
	if (osd_key_pressed_memory(OSD_KEY_RESET_MACHINE))
	{
		cpu_reset(0);
	}

	if (osd_key_pressed_memory(OSD_KEY_P))
	{
		paused ^= 1;
		if (paused) mute_audio();
		else resume_audio();
	}

	if (osd_key_pressed_memory(OSD_KEY_SHOW_FPS)) // TODO: Add some sort of on screen message here
	{
		show_fps ^= 1;
	}

	if (osd_key_pressed_memory(OSD_KEY_F12))
	{
		snapshot();
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
		{  //Are we in the GUI? If not jump back to the GUI else quit for real
			//if (gamenum)
			//{
			//	in_gui = 1;
			//}
			//if ( get_menu_level() > 100)
			done = 1; // Done == 1 means we are exiting?
		}
	}
	int a = get_menu_level();
	if (a != 700)
	{
		if (osd_key_pressed_memory_repeat(OSD_KEY_UI_UP, 4)) { change_menu_level(0); } //up
		if (osd_key_pressed_memory_repeat(OSD_KEY_UI_DOWN, 4)) { change_menu_level(1); } //down
		if (osd_key_pressed_memory(OSD_KEY_UI_LEFT)) { change_menu_item(0); } //left
		if (osd_key_pressed_memory(OSD_KEY_UI_RIGHT)) { change_menu_item(1); } //right
		if (osd_key_pressed_memory(OSD_KEY_UI_SELECT)) { select_menu_item(); }
	}
}

static void throttle_speed(void)
{
	double millsec = (double)1000 / (double)driver[gamenum].fps;
	gametime = TimerGetTimeMS();

	//if (Machine->drv->frames_per_second != 60) {
	while (((double)(gametime)-(double)starttime) < (double)millsec)
	{
		HANDLE current_thread = GetCurrentThread();
		int old_priority = GetThreadPriority(current_thread);

		if (((double)gametime - (double)starttime) < (double)(millsec - 4))
		{
			SetThreadPriority(current_thread, THREAD_PRIORITY_TIME_CRITICAL);
			//Sleep(1);
			SetThreadPriority(current_thread, old_priority);
		}
		//else Sleep(0);
		gametime = TimerGetTimeMS();
	}
	//}

	if (frames > 60)
	{
		frameavg++;
		if (frameavg > 10000) { frameavg = 0; fps_count = 0; }
		fps_count += 1000 / ((double)(gametime)-(double)starttime);
	}

	starttime = TimerGetTimeMS();
}

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

	setup_game_config();
	sanity_check_config();
	wrlog("Running game %s", driver[gamenum].desc);

	//Check for setting greater then screen availability
	if (config.screenh > cy || config.screenw > cx)
	{
		allegro_message("MESSAGE", "Warning: \nphysical size smaller then config setting");
	}

	//if (res_reset)
	//{
	wrlog("OpenGL Init");
	init_gl();
	end_gl();   //Clean up any stray textures
	//Reinit OpenGl
//}
//else
	// TODO: Revisit THis
	texture_reinit(); //This cleans up the FBO textures preping them for reuse.

	//////////////////////////////////////////////////////////////INITIAL VARIABLES SETUP ///////////////////////////////////////////////////
	options.cheat = 1;
	//set_aae_leds(0, 0, 0);  //Reset LEDS
	leds_status = 0;
	force_all_kbdleds_off();

	config.mainvol *= 12.75;
	config.pokeyvol *= 12.75; //Adjust from menu values
	config.noisevol *= 12.75;

	clamp(config.mainvol);
	clamp(config.pokeyvol);
	clamp(config.noisevol);
	//set_volume(config.mainvol, 0);

	SetThreadPriority(GetCurrentThread(), ABOVE_NORMAL_PRIORITY_CLASS);
	switch (config.priority)
	{
	case 4: SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS); break;
	case 3: SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS); break;
	case 2: SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS); break;
	case 1: SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS); break;
	case 0: SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS); break;
	}
	LimitThreadAffinityToCurrentProc();
	// Reset are settings.
	art_loaded[0] = 0;
	art_loaded[1] = 0;
	art_loaded[2] = 0;
	art_loaded[3] = 0;

	run_a_game(gamenum);
	// Wait, what? We are reloading, now????
	init_machine();

	//////////////////////////////////////////////////////////////END VARIABLES SETUP ///////////////////////////////////////////////////
	if (gamenum) {
		goodload = load_roms(driver[gamenum].name, driver[gamenum].rom);
		if (goodload == 0) {
			wrlog("Rom loading failure, exiting..."); have_error = 10; gamenum = 0;
			if (!in_gui) { exit(1); }
		}

		// TODO: Add some error checking here please.
		load_artwork(driver[gamenum].artwork);
		resize_art_textures();
	}

	setup_video_config();
	if (config.bezel && gamenum) { msx = b1sx; msy = b1sy; esx = b2sx; esy = b2sy; }
	else { msx = sx; msy = sy; esx = ex; esy = ey; }

	frameavg = 0; fps_count = 0; frames = 0;
	//////////////////////////

	millsec = (double)1000 / (double)driver[gamenum].fps;

	mixer_init(config.samplerate, driver[gamenum].fps);

	// Load samples if the game has any. This needs to move!
	if (driver[gamenum].game_samples)
	{
		goodload = read_samples(driver[gamenum].game_samples, 0);
		if (!goodload) { wrlog("Samples loading failure, please check error output for details..."); }
	}

	//Now load the Ambient and menu samples
	// TODO: This has been disabled for now. !!!
	//goodload = read_samples(noise_samples, 1);
	//if (!goodload) { wrlog("Noise Samples loading failure, not critical, continuing."); }
	//voice_init(num_samples); //INITIALIZE SAMPLE VOICES
	wrlog("Number of samples for this game is %d", num_samples);
	// RE-ADD WHEN ALLEGRO REMOVED AND SOUND ENGINE CHANGED.
	//setup_ambient(VECTOR);
	// Setup for the first game.
	wrlog("Initializing Game");
	wrlog("Loading InputPort Settings");
	load_input_port_settings();
	init_cpu_config(); ////////////////////-----------
	driver[gamenum].init_game();

	hiscoreloaded = 0;

	//If the game uses NVRAM, initalize/load it. . This code is from M.A.M.E. (TM)
	if (driver[gamenum].nvram_handler)
	{
		void* f;

		f = osd_fopen(Machine->gamedrv->name, 0, OSD_FILETYPE_NVRAM, 0);
		(*driver[gamenum].nvram_handler)(f, 0);
		if (f) osd_fclose(f);
	}

	wrlog("\n\n----END OF INIT -----!\n\n");
	//reset_for_new_game(gamenum, 0);
}

void emulator_run()
{
	// Load High Score
	if (hiscoreloaded == 0 && driver[gamenum].hiscore_load)
		hiscoreloaded = (*driver[gamenum].hiscore_load)();

	// Setup for rendering a frame.
	set_render();

	auto start = chrono::steady_clock::now();

	if (!paused && have_error == 0)
	{
		update_input_ports();
		osd_poll_joysticks();

		//if (driver[gamenum].pre_run) driver[gamenum].pre_run();
		wrlog("Calling CPU Run Mame");
		cpu_run_mame();
		if (driver[gamenum].run_game)driver[gamenum].run_game();
	}

	auto end = chrono::steady_clock::now();
	auto diff = end - start;
	wrlog("Profiler: CPU Time: %f ", chrono::duration <double, milli>(diff).count());
	// Complete and display the rendered frame.
	render();

	msg_loop();

	inputport_vblank_end();
	//timer_clear_all_eof();
	cpu_clear_cyclecount_eof();

	throttle_speed();

	frames++;
	if (frames > 0xfffffff) { frames = 0; }
	wrlog("Debug Remove: Mixer Update Start");
	mixer_update();
	wrlog("END OF FRAME");
}

void emulator_init(int argc, char** argv)
{
	int i;
	int loop = 0;
	int loop2 = 0;
	char str[20];
	char* mylist;

	//For resolution
	int horizontal;
	int vertical;

	//LogOpen("aae.log");

	os_init_input();
	// We start with the running game being the gui, unless overridden by the command line.
	gamenum = 18;
	x_override = 0;
	y_override = 0;
	// Build the supported game list.
	while (driver[loop].name != 0) { num_games++; loop++; }
	wrlog("Number of supported games is: %d", num_games);
	num_games++;

	for (loop = 1; loop < (num_games - 1); loop++) //
	{
		strcpy(gamelist[loop].glname, driver[loop].desc);
		gamelist[loop].gamenum = loop;
		if (loop < num_games - 2) { gamelist[loop].next = (loop + 1); }
		else { gamelist[loop].next = 1; }

		if (loop > 1) { gamelist[loop].prev = (loop - 1); }
		else { gamelist[loop].prev = (num_games - 2); }
	}
	// Sort the supported game list since they are not in alphabetical order.
	wrlog("Sorting games");
	sort_games();
	wrlog("Made it past here");
	//Move this after command line processing is re-added
	GetDesktopResolution(horizontal, vertical);
	wrlog("Actual primary monitor desktop screen size %d  %d", horizontal, vertical);
	//if (config.screenw == 0 && config.screenh == 0)
	//{
	config.screenw = horizontal;
	config.screenh = vertical;
	//}

	// Disable ShortCut Keys
	AllowAccessibilityShortcutKeys(0);

	//wrlog(" String %s Max Num %d", argv[1], argc);

	if (argv[1] == NULL)
	{
		wrlog("Usage: aae gamename -argument, -argument, etc.");
		show_mouse();
		allegro_message("Error", "Please run from a command prompt. \nUsage: aae gamename -argument, -argument, etc.");
		done = 1;
		emulator_end();
	}
	// Here is where we are checking command line for a supported game name.
		// If not, it jumps straight into the GUI, bleh.

	toLowerCase(argv[1]);

	for (loop = 1; loop < (num_games - 1); loop++)
	{
		if (strcmp(argv[1], driver[loop].name) == 0)
		{
			gamenum = loop;
			started_from_command_line = 1;
		}
	}
	//gamenum = 18;
	//	if (argc > 2) gameparse(argc, argv);
	//}

	//THIS IS WHERE THE CODING STARTS
	// Decide if we are still starting with the gui or not. ? Why do this twice? We already have gamenum=0 or a game?
	//if (gamenum) in_gui = 0; else in_gui = 1;
	in_gui = 0;

	//wrlog("Number of supported joysticks: %d ", num_joysticks);
	/*
	if (num_joysticks)
	{
		poll_joystick();
		for (loop = 0; loop < num_joysticks; loop++) {
			wrlog("joy %d number of sticks %d", loop, joy[loop].num_sticks);
			for (loop2 = 0; loop2 < joy[loop].num_sticks; loop2++)
			{
				wrlog("stick number  %d flag %d", loop2, joy[loop].stick[loop2].flags);
				wrlog("stick number  %d axis 0 pos %d", loop2, joy[loop].stick[loop2].axis[0].pos);
				wrlog("stick number  %d axis 1 pos %d", loop2, joy[loop].stick[loop2].axis[1].pos);
			}
		}
	}
	*/
	frames = 0; //init frame counter

	config.hack = 0; //Just to set, used for tempest only.
	showinfo = 0;
	done = 0;
	//////////////////////////////////////////////////////
	wrlog("Number of supported games in this release: %d", num_games);
	initrand();
	fillstars(stars);
	have_error = 0;

	switch (config.priority)
	{
	case 4: SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS); break;
	case 3: SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS); break;
	case 2: SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS); break;
	case 1: SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS); break;
	case 0: SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS); break;
	}

	run_game();
	wrlog("Starting Run Game");
}

void emulator_end()
{
	wrlog("Emulator End Called.");

	// If we're using high scores, write them to disk.
	if (hiscoreloaded != 0 && driver[gamenum].hiscore_save)
		(*driver[gamenum].hiscore_save)();

	//If the game uses NVRAM, save it. This code is from M.A.M.E. (TM)
	if (driver[gamenum].nvram_handler)
	{
		void* f;

		if ((f = osd_fopen(Machine->gamedrv->name, 0, OSD_FILETYPE_NVRAM, 1)) != 0)
		{
			(*driver[gamenum].nvram_handler)(f, 1);
			osd_fclose(f);
		}
	}
	config.artwork = 0;
	config.bezel = 0;
	config.overlay = 0;
	// Free samples??
	// Free textures??
	//END-----------------------------------------------------
	save_input_port_settings();
	KillFont();
	
	AllowAccessibilityShortcutKeys(1);
	mixer_end();
	force_all_kbdleds_off();
}