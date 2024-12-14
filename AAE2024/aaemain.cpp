//============================================================================
// AAE is a poorly written M.A.M.E (TM) derivitave based on early MAME 
// code, 0.29 through .90 mixed with code of my own. This emulator was 
// created solely for my amusement and learning and is provided only 
// as an archival experience. 
// 
// All MAME code used and abused in this emulator remains the copyright 
// of the dedicated people who spend countless hours creating it. All
// MAME code should be annotated as belonging to the MAME TEAM. 
//============================================================================

#include "aaemain.h"

#include <stdio.h>
#include <stdlib.h>
#include <allegro.h>
#include "alleggl.h"
#include "winalleg.h"
#include <malloc.h>
#include "sys_timer.h"
#include "acommon.h"
#include "fileio/loaders.h"
#include "config.h"
#include "aae_mame_driver.h"
#include <mmsystem.h>
#include "allglint.h"
#include "fonts.h"
#include "gui/gui.h"
#include "gui/animation.h"
#include "gamedriver.h"
#include "rand.h"
#include "glcode.h"
#include "samples.h"
#include "menu.h"
#include "aae_avg.h"
#include "fpsclass.h"
#include "vector.h"
#include "os_input.h"
#include "timer.h"
#include "vector_fonts.h"
#include "gl_texturing.h"

//New 2024
#include "os_basic.h"
#include <chrono>

#ifdef WIN10BUILD
#include "win10_win11_required_code.h"
#endif // WIN10BUILD



using namespace std;
using namespace chrono;


//TEST VARIABLES
static int res_reset;
static int x_override;
static int y_override;
static int win_override = 0;

int previous_game = 0;
int hiscoreloaded;
int sys_paused = 0;
int show_fps = 0;
FpsClass* m_frame; //For frame counting. Prob needs moved out of here really.

// M.A.M.E. (TM) Variables for testing
static struct RunningMachine machine;
struct RunningMachine* Machine = &machine;
static const struct AAEDriver* gamedrv;
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

volatile int close_button_pressed = FALSE;

void close_button_handler(void)
{
	close_button_pressed = TRUE;
}
END_OF_FUNCTION(close_button_handler)


int mystrcmp(const char* s1, const char* s2)
{
	while (*s1 && *s2 && *s1 == *s2) {
		s1++;
		s2++;
	}

	return *s1 - *s2;
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

void set_cpu()
{
	return;
	HANDLE hProcess = GetCurrentProcess();
	DWORD dwProcessAffinityMask, dwSystemAffinityMask;
	GetProcessAffinityMask(hProcess, &dwProcessAffinityMask, &dwSystemAffinityMask);

	//If you have 2 processors both masks should be 3 by default. You can then use the calls

	//SetProcessAffinityMask( hProcess, 1L );// use CPU 0 only
	//SetProcessAffinityMask( hProcess, 2L );// use CPU 1 only
	SetProcessAffinityMask(hProcess, 3L);// allow running on both CPUs
}

int run_a_game(int game)
{
	Machine->gamedrv = gamedrv = &driver[game];
	wrlog("Starting game, Driver name now is %s", Machine->gamedrv->name);
	//Machine->drv = drv = gamedrv->drv;

	return 0;
}

void reset_for_new_game(int new_gamenum, int in_giu)
{

//	if (!in_gui) { driver[gamenum].end_game(); }
    cache_end();
	cache_clear();

	if (driver[gamenum].end_game)
	{
		driver[gamenum].end_game();
	}

	wrlog("@@@RESETTING for NEW GAME CALLED@#@@@@");

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
	
	wrlog("restarting");
	free_samples();//Free any allocated Samples
	wrlog("Finished Freeing Samples");
	free_game_textures(); //Free textures
	wrlog("Finished Freeing Textures");
	//If Using GUI and resolution change, or not in gui
	//if (in_gui && res_reset==0)
	wrlog("Done Freeing All"); //Log it.
	wrlog("Saving Input Port Settings");
	save_input_port_settings();
	config.artwork = 0;
	config.bezel = 0;
	config.overlay = 0;
	if (in_gui == 0) { gamenum = 0; }
	else 	gamenum = new_gamenum;
	wrlog("DONE here is %d, exiting", done);
	//Catch
	if (done == 1) { exit(1); }
	if (done == 2) { done = 0; run_game(); }
	if (done == 3) { done = 0; gamenum = 0; run_game(); }
}

int init_machine(void)
{
	wrlog("Calling init machine to build shadow input ports");

	if (gamedrv->input_ports)
	{
		int total;
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
		sys_paused ^= 1;
		if (sys_paused) mute_sound();
		else restore_sound();
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
			if (gamenum)
			{
				in_gui = 1;
			}
			done = 1;
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

	/*
		if (have_error == 0  && show_menu==0 )

		 if ( (gamenum > 0) && in_gui){done=3;}else {done=1;} log_it("Quitting, code %d",showinfo);

		 if (have_error != 0){have_error=0;Sleep(150);clear_keybuf();errorsound=0;}

		 if (config.debug){
		 k=(key_shifts & KB_SHIFT_FLAG);
		 if (k&&key[KEY_RIGHT]){msy--;}
		 else if (k&&key[KEY_LEFT]){msx++;}
		 else if (k&&key[KEY_DOWN]){esx++;}
		 else if (k&&key[KEY_UP]){esy--;}
		 else if (key[KEY_RIGHT]){msy++;}
		 else if (key[KEY_LEFT]) {msx--;}
		 else if (key[KEY_DOWN]) {esx--;}
		 else if (key[KEY_UP])   {esy++;}
		 }

		 if (getport(15) & 0x40) {if (paused==0) {paused=1;mute_sound(); }else if (paused==1){paused=0;restore_sound();}}//pause_loop();KeyCheck(KEY_P)
		 //if (key[config.ksstate]) {Sleep(150);save_state6502();}
		 //if (key[config.klstate]) {Sleep(150);}

		*/
}

void run_game(void)
{
	DEVMODE dvmd;
	int cx = GetSystemMetrics(SM_CXSCREEN);
	// height
	int cy = GetSystemMetrics(SM_CYSCREEN);
	//width
	int game0 = 0;
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

	//Check for same resolution or need to switch.
	//res_reset = 1;
	//testwidth = config.screenw; testheight = config.screenh; testwindow = config.windowed;

	//GameRect.min_x = driver[gamenum].gamerect[0];
	//GameRect.max_x = driver[gamenum].gamerect[1];
	//GameRect.min_y = driver[gamenum].gamerect[2];
	//GameRect.max_y = driver[gamenum].gamerect[3];

	//wrlog("Game Main Texture rect: X: %d Y: %d EX: %d EY: %d", driver[gamenum].gamerect[0], driver[gamenum].gamerect[1], driver[gamenum].gamerect[2], driver[gamenum].gamerect[3]);
	setup_game_config();
	sanity_check_config();
	wrlog("Running game %s", driver[gamenum].desc);
	
	if (in_gui) {
		//if (config.windowed == testwindow && config.screenh == testheight && config.screenw == testwidth) { res_reset = 0; }
		//else { res_reset = 1; }
	}
	if (x_override || y_override)
	{
		//res_reset = 1;
	//	config.screenh = y_override;
		//config.screenw = x_override;
	}
	if (win_override)

	//	config.windowed = win_override - 2;
	
	//Check for setting greater then screen availability
	if ( config.screenh > cy || config.screenw > cx)
	{
		allegro_message("Overriding config screen settings, \nphysical size smaller then config setting");
	//	config.screenh = cy; config.screenw=cx; res_reset=1;
	}

	//if (res_reset)
	//{
		wrlog("OpenGL Init");
		init_gl();
		end_gl();   //Clean up any stray textures
		 //Reinit OpenGl
	//}
	//else
	texture_reinit(); //This cleans up the FBO textures preping them for reuse.

	//////////////////////////////////////////////////////////////INITIAL VARIABLES SETUP ///////////////////////////////////////////////////
	options.cheat = 1;
	set_aae_leds(0, 0, 0);  //Reset LEDS
	
	//config.mainvol = 255;
	//config.pokeyvol = 250; //Adjust from menu values
	//config.noisevol = 200;
	
	//Check;
	if (config.noisevol > 255) config.noisevol = 254;
	if (config.pokeyvol > 255) config.pokeyvol = 254;
	if (config.mainvol > 255) config.mainvol = 254;
	if (config.noisevol <= 0) config.noisevol = 1;
	if (config.mainvol <= 0) config.mainvol = 1;
	if (config.pokeyvol <= 0) config.pokeyvol = 1;

	set_volume(config.mainvol, 0);

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

	art_loaded[0] = 0;
	art_loaded[1] = 0;
	art_loaded[2] = 0;
	art_loaded[3] = 0;

	wrlog("Config.linewidth 2: %f", config.linewidth);

	run_a_game(gamenum);
	init_machine();

	//////////////////////////////////////////////////////////////END VARIABLES SETUP ///////////////////////////////////////////////////
	if (gamenum) {
		goodload = load_roms(driver[gamenum].name, driver[gamenum].rom);
		if (goodload == 0) {
			wrlog("Rom loading failure, exiting..."); have_error = 10; gamenum = 0;
			if (!in_gui) { exit(1); }
		}

		load_artwork(driver[gamenum].artwork);
		resize_art_textures();
	}

	setup_video_config();
	if (config.bezel && gamenum) { msx = b1sx; msy = b1sy; esx = b2sx; esy = b2sy; }
	else { msx = sx; msy = sy; esx = ex; esy = ey; }

	frameavg = 0; fps_count = 0; frames = 0;
	//////////////////////////
	

	millsec = (double)1000 / (double)driver[gamenum].fps;
	
	// Load samples if the game has any. This needs to move!
	if (driver[gamenum].game_samples)
	{
		goodload = load_samples(driver[gamenum].game_samples, 0);
		if (!goodload) { wrlog("Samples loading failure, please check error output for details..."); }
	}
	
	//Now load the Ambient and menu samples
	goodload = load_samples(noise_samples, 1);
	if (!goodload) { wrlog("Noise Samples loading failure, not critical, continuing."); }
	voice_init(num_samples); //INITIALIZE SAMPLE VOICES
	wrlog("Number of samples for this game is %d", num_samples);

	//setup_ambient(VECTOR);

	wrlog("Initializing Game");

	wrlog("Loading InputPort Settings");
	load_input_port_settings();
	init_cpu_config(); ////////////////////-----------
	driver[gamenum].init_game();
	if (gamenum) game0 = 1; else game0 = 0;
	previous_game = gamenum;
	WATCHDOG = 0;

	//If the game uses NVRAM, initalize/load it. . This code is from M.A.M.E. (TM)
	if (driver[gamenum].nvram_handler)
	{
		void* f;

		f = osd_fopen(Machine->gamedrv->name, 0, OSD_FILETYPE_NVRAM, 0);
		(*driver[gamenum].nvram_handler)(f, 0);
		if (f) osd_fclose(f);
	}

	wrlog("\n\n----END OF INIT -----!\n\n");
	wrlog("Config.linewidth 3: %f", config.linewidth);
	while (!done && !close_button_pressed)
	{
    	wrlog("STARTING FRAME");
		//set_new_frame(); //This is for the AVG Games.
		
		

		set_render();
	
		auto start = chrono::steady_clock::now();
		if (driver[gamenum].cpu_type[0] == CPU_M6809 || driver[gamenum].cpu_type[0] == CPU_MZ80)
		{
			if (!paused && have_error == 0) { if (driver[gamenum].pre_run) driver[gamenum].pre_run(); }
			if (!paused && have_error == 0) { cpu_run_mame(); }
			
		}
		
		if (driver[gamenum].cpu_type[0] == CPU_M6502 || driver[gamenum].cpu_type[0] == CPU_68000)
		{
			if (!paused && have_error == 0) { if (driver[gamenum].pre_run) driver[gamenum].pre_run(); }
			if (!paused && have_error == 0) { run_cpus_to_cycles(); }
		}
		
		if (!paused && have_error == 0) { driver[gamenum].run_game(); }

		auto end = chrono::steady_clock::now();
		auto diff = end - start;
		wrlog("CPU Time: %f ", chrono::duration <double, milli>(diff).count());

		render();
		
		msg_loop();

		inputport_vblank_end();
		update_input_ports();

		//timer_clear_all_eof();

		gametime = TimerGetTimeMS();

		//if (driver[gamenum].fps !=60){
		while (((double)(gametime)-(double)starttime) < (double)millsec)
		{
			HANDLE current_thread = GetCurrentThread();
			int old_priority = GetThreadPriority(current_thread);

			if (((double)gametime - (double)starttime) < (double)(millsec - 4))
			{
				SetThreadPriority(current_thread, THREAD_PRIORITY_TIME_CRITICAL);
				Sleep(1);
				SetThreadPriority(current_thread, old_priority);
			}
			else Sleep(0);
			gametime = TimerGetTimeMS();
		}
		//  }
		if (frames > 60) {
			frameavg++; if (frameavg > 10000) { frameavg = 0; fps_count = 0; }
			fps_count += 1000 / ((double)(gametime)-(double)starttime);
		}
		
		frames++;
		if (frames > 0xfffffff) { frames = 0; }
		starttime = TimerGetTimeMS();

		allegro_gl_flip();
		wrlog("END OF FRAME");
		//wrlog("FRAME %d", frames);
	}
	wrlog("------- Calling game end and reset to GUI -----------");
	if (gamenum == 0) { done = 1; } //Roll off to exit
	else { done = 3; }

	
	reset_for_new_game(gamenum, 0);
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
		if (gamenum > 0) { allegro_message("%s rom list: %s", driver[gamenum].name, mylist); }
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

		if (gamenum > 0) { allegro_message("%s rom verify: %s", driver[gamenum].name, mylist); }
		free(mylist);
		LogClose();
		exit(1);  break;

	case 3: mylist = (char*)malloc(5000);
		strcpy(mylist, "\n");
		while (strcmp(driver[gamenum].game_samples[i], "NULL")) { strcat(mylist, driver[gamenum].game_samples[i]); strcat(mylist, " "); i++; }
		strcat(mylist, "\0");
		if (gamenum > 0) allegro_message("%s Samples: %s", driver[gamenum].name, mylist);
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

		if (gamenum > 0) { allegro_message("%s sample verify: %s", driver[gamenum].name, mylist); }
		free(mylist);
		LogClose();
		exit(1);  break;
	}
}


int main(int argc, char* argv[])
{
	int i;
	int loop = 0;
	int loop2 = 0;
	char str[20];
	char* mylist;
	TIMECAPS caps;

	//For resolution
	int horizontal;
	int vertical;


	// int goodload;
	 //set_cpu();
	LogOpen("aae.log");
	allegro_init();
	install_allegro_gl();
	install_timer();
	msdos_init_input();

	reserve_voices(16, -1);
	set_volume_per_voice(3);
	//AUTODETECT
	if (install_sound(DIGI_AUTODETECT, MIDI_NONE, NULL)) {
		allegro_message("Can't initialize sound driver.\nThis program requires sound support, sorry.");
		return -1;
	}
	set_volume_per_voice(3);

	LOCK_FUNCTION(close_button_handler);
	set_close_button_callback(close_button_handler);

	gamenum = 0; //Clear to make sure!
	x_override = 0;
	y_override = 0;
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
	wrlog("Sorting games");
	sort_games();
	loop2 = 0;
	loop = 1;

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

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-listromstotext") == 0)
		{
			allegro_message("Listing all game roms to a text file.");
			mylist = (char*)malloc(0x50000);
			strcpy(mylist, "AAE All Games RomList\n");

			while (loop < (num_games - 1)) {
				strcat(mylist, "\n");
				strcat(mylist, "Game Name: ");
				strcat(mylist, driver[loop].desc);
				strcat(mylist, ":\n");
				strcat(mylist, "Rom Name: ");
				strcat(mylist, driver[loop].name);
				strcat(mylist, ".zip\n");
				wrlog("gamename %s", driver[loop].name);
				wrlog("gamename %s", driver[loop].desc);
				while (driver[loop].rom[loop2].romSize > 0)
				{
					if (driver[loop].rom[loop2].loadAddr != 0x999) {
						if (driver[loop].rom[loop2].filename != (char*)-1) {
							strcat(mylist, driver[loop].rom[loop2].filename);

							strcat(mylist, " Size: ");
							sprintf(str, "%x", driver[loop].rom[loop2].romSize);
							strcat(mylist, str);

							strcat(mylist, " Load Addr: ");
							sprintf(str, "%x", driver[loop].rom[loop2].loadAddr);
							strcat(mylist, str);

							strcat(mylist, "\n");
						}
					}
					loop2++;
				}

				loop2 = 0; //reset!!
				loop++;
			}
			strcat(mylist, "\0");
			wrlog("SAVING Romlist");
			save_file_char("AAE All Game Roms List.txt", (unsigned char*)mylist, strlen(mylist));
			free(mylist);
			LogClose();
			exit(1);
		}

		// Here is where we are checking command line for a supported game name. 
		// If not, it jums straight into the GUI, bleh.
		for (loop = 1; loop < (num_games - 1); loop++)
		{
			if (strcmp(argv[1], driver[loop].name) == 0)
			{
				gamenum = loop;
			}
		}
		if (argc > 2) gameparse(argc, argv);
	}

	//THIS IS WHERE THE CODING STARTS

	if (gamenum) in_gui = 0; else in_gui = 1;

	timeGetDevCaps(&caps, sizeof(TIMECAPS));
	timeBeginPeriod(caps.wPeriodMin);

	wrlog("Setting timer resolution to Min Supported: %d (ms)", caps.wPeriodMin);

	wrlog("Number of supported joysticks: %d ", num_joysticks);
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

	frames = 0; //init frame counter
	testsw = 0; //init testswitch off , had to make common for all
	config.hack = 0; //Just to set, used for some games with hacks.
	showinfo = 0;
	done = 0;
	//////////////////////////////////////////////////////
	//set_display_switch_mode(SWITCH_PAUSE);
	TimerInit(); //Start timer
	wrlog("Number of supported games in this release: %d", num_games);

	initrand();
	fillstars(stars);
	have_error = 0;

#ifdef WIN10BUILD
	disable_windows10_window_scaling();
#endif // WIN10BUILD

	//key_set_flag = 0;
	//ListDisplaySettings();
	
	switch (config.priority)
	{
	case 4: SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS);break;
	case 3: SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);break;
	case 2: SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);break;
	case 1: SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);break;
	case 0: SetPriorityClass(GetCurrentProcess(),IDLE_PRIORITY_CLASS);break;
	}
	//Override the above for now. 
	SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
	SetThreadPriority(GetCurrentThread(), ABOVE_NORMAL_PRIORITY_CLASS);
	
	//set_window_title("AAE BETA 2");
   // set_window_close_button(0);
   /////
	//AllocConsole();
   // SetConsoleTitle("Alert Window");

	//freopen("CONIN$","rb",stdin);   // reopen stdin handle as console window input
	//freopen("CONOUT$","wb",stdout);  // reopen stout handle as console window output
	//freopen("CONOUT$","wb",stderr); // reopen stderr handle as console window output

	//printf("This is a console output test");
	//wrlog("CPU TYPE FOR THIS GAME is %d %d %d %d", driver[gamenum].cputype[0],driver[gamenum].cputype[1],driver[gamenum].cputype[2],driver[gamenum].cputype[3]);
	
	// Main run point moves to Run Game, which takes care of jumping back and forth into the GUI as well as cleaning up after each game. 
	// Unfortunately, it does neither very well. 
	//
	wrlog("Config.linewidth 1: %f", config.linewidth);

	run_game();
	wrlog("Shutting down program");
	//END-----------------------------------------------------
	
	timeEndPeriod(caps.wPeriodMin);
	KillFont();
	//SetGammaRamp(0, 0, 0);
	set_aae_leds(0, 0, 0);
	AllowAccessibilityShortcutKeys(1);
	//FreeConsole( );

	LogClose();
	return 0;
}
END_OF_MAIN();

/*

void ListDisplaySettings(void) {
  DEVMODE devMode;
  LONG    i;
  int	  x=0;
  LONG    modeExist;
   // enumerate all modes & set to current in list

  for (i=0; i <52;i++) {vidmodes[i].curvid=0;vidmodes[i].vidfin=0;}

  vidmodes[51].curvid=0; //Set defaults
  modeExist = EnumDisplaySettings(NULL, 0, &devMode);
  wrlog("Available Screen modes for %s", devMode.dmDeviceName);
  for (i=0; modeExist;i++) {
	  if (devMode.dmDisplayFrequency < 70 && devMode.dmBitsPerPel > 8 && devMode.dmPelsHeight >=480){
		  wrlog( "%ldx%ldx%ld-bit color (%ld Hz)", devMode.dmPelsWidth, devMode.dmPelsHeight, devMode.dmBitsPerPel, devMode.dmDisplayFrequency);
						vidmodes[x].vidheight=devMode.dmPelsHeight;
						vidmodes[x].vidwidth=devMode.dmPelsWidth;
						vidmodes[x].vidbpp=devMode.dmBitsPerPel;
						vidmodes[x].vidfin=0;
						x++;
						}
	modeExist = EnumDisplaySettings(NULL, i, &devMode);
  }
	vidmodes[x].vidheight=-1;
	vidmodes[x].vidwidth=-1;
	vidmodes[x].vidbpp=-1;
	vidmodes[51].vidfin=(x-1);
	//check for current mode and set//
	x=0;
	while (vidmodes[x].vidwidth !=-1 && x < 51)
	{
	if (vidmodes[x].vidwidth ==config.screenw && vidmodes[x].vidheight==config.screenh && vidmodes[x].vidbpp==config.colordepth)
	{vidmodes[51].curvid=x;wrlog("Current res is number %d",vidmodes[51].curvid);}

	x++;
	}
	if (vidmodes[51].curvid==0)wrlog("Crap, no video mode matched, interesting..?");
}
*/