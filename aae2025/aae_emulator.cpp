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
#include "sys_timer.h"
#include "acommon.h"
#include "fileio/loaders.h"
#include "config.h"
#include <mmsystem.h>
#include "fonts.h"
//#include "gui/gui.h"
//#include "gui/animation.h"
#include "gamedriver.h"
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

#include "old_mame_raster.h"

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

glist gamelist[256];
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

///////////////////////////////////////  RASTER CODE START  ////////////////////////////////////////////////////
// This is only a test. Trying out new things. 

int vector_game;
int use_dirty;

//OSD VIDEO THINGS
int game_width = 0;
int game_height = 0;
int game_attributes = 0;
float vid_scale = 3.0;

int gfx_mode;
int gfx_width;
int gfx_height;

unsigned char current_palette[640][3];
//static unsigned char current_background_color;
//static PALETTE adjusted_palette;

//From MAME .30 for VH Hardware
#define MAX_COLOR_TUPLE 16      /* no more than 4 bits per pixel, for now */
#define MAX_COLOR_CODES 256     /* no more than 256 color codes, for now */
static unsigned char remappedtable[MAX_GFX_ELEMENTS * MAX_COLOR_TUPLE * MAX_COLOR_CODES];

//Move to Raster
void osd_modify_pen(int pen, unsigned char red, unsigned char green, unsigned char blue)
{
	if (current_palette[pen][0] != red ||
			current_palette[pen][1] != green ||
			current_palette[pen][2] != blue)
	{
	 current_palette[pen][0] = red;
	 current_palette[pen][1] = green;
	 current_palette[pen][2] = blue;

	//dirtycolor[pen] = 1;
   }
}

//Move to Raster
void osd_get_pen(int pen, unsigned char* red, unsigned char* green, unsigned char* blue)
{
	*red = current_palette[pen][0];
	*green = current_palette[pen][1];
	*blue = current_palette[pen][2];
}

/* Create a display screen, or window, large enough to accomodate a bitmap */
/* of the given dimensions. Attributes are the ones defined in driver.h. */
/* palette is an array of 'totalcolors' R,G,B triplets. The function returns */
/* in *pens the pen values corresponding to the requested colors. */
/* Return a osd_bitmap pointer or 0 in case of error. */
struct osd_bitmap* osd_create_display(int width, int height, unsigned int totalcolors,
	const unsigned char* palette, unsigned char* pens, int attributes)
{
	//	int i;

		//if (errorlog)
	wrlog("New Display width %d, height %d", width, height);

	/* Look if this is a vector game */
	if (Machine->drv->video_attributes & VIDEO_TYPE_VECTOR)
	{
		wrlog("Init: Vector game starting");
		vector_game = 1;
		//VECTOR_START();
	}
	else
		vector_game = 0;

	/* Is the game using a dirty system? */
	if ((Machine->drv->video_attributes & VIDEO_SUPPORTS_DIRTY))
		use_dirty = 1;
	else
		use_dirty = 0;

	//select_display_mode(); //I need to add this

	if (vector_game)
	{
		//use_double = 1;
		/* center display */
	}
	else /* center display based on visible area */
	{
		struct rectangle vis = Machine->drv->visible_area;
	}

	game_width = width;
	game_height = height;
	game_attributes = attributes;

	//Create the main bitmap screen
	main_bitmap = osd_create_bitmap(width + 2, height + 2);
	wrlog("Main Bitmap Created");
	if (!main_bitmap)
	{
		wrlog("Bitmap create failed, why?");
		return 0;
	}
	wrlog("exiting create display");
	return main_bitmap;
}

static void vh_close(void)
{
	int i;

	for (i = 0; i < MAX_GFX_ELEMENTS; i++) freegfx(Machine->gfx[i]);
	free(Machine->pens);
	//osd_close_display();
}

static int vh_open(void)
{
	int i;
	unsigned char* palette;
	unsigned char* colortable = nullptr;
	unsigned char convpalette[3 * MAX_PENS];
	unsigned char* convtable;

	wrlog("Running vh_open");

	wrlog("MIN Y:%d ", Machine->drv->visible_area.min_y);
	wrlog("MIN X:%d ", Machine->drv->visible_area.min_x);
	wrlog("MAX Y:%d ", Machine->drv->visible_area.max_y);
	wrlog("MAX x:%d ", Machine->drv->visible_area.max_x);

	wrlog("1");
	convtable = (unsigned char*)malloc(MAX_GFX_ELEMENTS * MAX_COLOR_TUPLE * MAX_COLOR_CODES);
	if (!convtable) return 1;

	for (i = 0; i < MAX_GFX_ELEMENTS; i++) Machine->gfx[i] = 0;

	wrlog("2");
	/* convert the gfx ROMs into character sets. This is done BEFORE calling the driver's */
	/* convert_color_prom() routine because it might need to check the Machine->gfx[] data */
	
	if (Machine->gamedrv->gfxdecodeinfo)
	{
			for (i = 0; i < MAX_GFX_ELEMENTS && Machine->gamedrv->gfxdecodeinfo[i].memory_region != -1; i++)
		{
			if ((Machine->gfx[i] = decodegfx(Machine->memory_region[Machine->gamedrv->gfxdecodeinfo[i].memory_region]
				+ Machine->gamedrv->gfxdecodeinfo[i].start,
				Machine->gamedrv->gfxdecodeinfo[i].gfxlayout)) == 0)
			{
				vh_close();
				free(convtable);
				return 1;
			}
			wrlog("I here at gfx convert is %d, memregion is %d", i, Machine->gamedrv->gfxdecodeinfo[i].memory_region);
			Machine->gfx[i]->colortable = &remappedtable[Machine->gamedrv->gfxdecodeinfo[i].color_codes_start];
			Machine->gfx[i]->total_colors = Machine->gamedrv->gfxdecodeinfo[i].total_color_codes;
			wrlog("Colortable here is remap table at is %d,total color codes is %d", Machine->gamedrv->gfxdecodeinfo[i].color_codes_start, Machine->gamedrv->gfxdecodeinfo[i].total_color_codes);
		}
	}
	wrlog("3");
	//Create a default pallet
	palette = (unsigned char*)malloc(MAX_GFX_ELEMENTS * MAX_COLOR_TUPLE * MAX_COLOR_CODES);

	for (int x = 0; x < (MAX_GFX_ELEMENTS * MAX_COLOR_TUPLE * MAX_COLOR_CODES); x++)
	{
		palette[x] = 1;
	}
	/* convert the palette */
	/* now the driver can modify the default values if it wants to. */
	if (Machine->drv->vh_convert_color_prom)
	{
		(*Machine->drv->vh_convert_color_prom)(convpalette, convtable, memory_region(REGION_PROMS));
		palette = convpalette;
		colortable = convtable;
	}
	
	wrlog("4");
	/* create the display bitmap, and allocate the palette */
	if ((Machine->scrbitmap = osd_create_display(
		Machine->gamedrv->screen_width, Machine->gamedrv->screen_height, Machine->gamedrv->total_colors,
		palette, Machine->pens, Machine->gamedrv->video_attributes)) == 0)
	{
		wrlog("Why is this returning?");
		free(convtable);
		exit(1);
	}
	else
	{
		wrlog("Created Display surface, Width: %d Height: %d", Machine->gamedrv->screen_width, Machine->gamedrv->screen_height);
	}
	/* initialize the palette */
	for (i = 0; i < MAX_COLOR_CODES; i++)
	{
		current_palette[i][0] = current_palette[i][1] = current_palette[i][2] = 0;
	}
	/* fill the palette starting from the end, so we mess up badly written */
	/* drivers which don't go through Machine->pens[]
	NOT DOING THIS, I need to be able to access the palette directly!!
	*/
	for (i = 0; i < MAX_PENS; i++) //totalcolors
	{
		Machine->pens[i] = i;// 255 - i;
	}

	wrlog("TotalColors here is %d", Machine->gamedrv->total_colors);
	for (i = 0; i < Machine->gamedrv->total_colors; i++)
	{
		current_palette[Machine->pens[i]][0] = palette[3 * i];
		current_palette[Machine->pens[i]][1] = palette[3 * i + 1];
		current_palette[Machine->pens[i]][2] = palette[3 * i + 2];
	}

	wrlog("Color Table Len %d", Machine->gamedrv->color_table_len);
	for (i = 0; i < Machine->gamedrv->color_table_len; i++)
		remappedtable[i] = Machine->pens[colortable[i]];

	//Fix code below so it works
	/*
	// free memory regions allocated with REGIONFLAG_DISPOSE (typically gfx roms)
	for (region = 0; region < MAX_MEMORY_REGIONS; region++)
	{
		if (Machine->memory_region_type[region] & REGIONFLAG_DISPOSE)
		{
			int i;

			// invalidate contents to avoid subtle bugs
			for (i = 0; i < memory_region_length(region); i++)
				memory_region(region)[i] = rand();
			free(Machine->memory_region[region]);
			Machine->memory_region[region] = 0;
		}
	}
	*/
	/* free the graphics ROMs, they are no longer needed */
	//free(Machine->memory_region[REGION_GFX1]);
	//Machine->memory_region[REGION_GFX1] = 0;

	free(convtable);
	wrlog("Returning from vh_open");
	return 0;
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
	Machine->gamedrv = gamedrv = &driver[game];
	Machine->drv = gamedrv;
	wrlog("Starting game, Driver name now is %s", Machine->gamedrv->name);
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
 *  TODO: Clean this up with strings later.
 * 
 ****************************************/
void list_all_roms()
{
	int loop = 2;
	int loop2 = 0;
	char* mylist;
	char str[128];

	mylist = (char*)malloc(0x150000);
	strcpy(mylist, "AAE All Games RomList\n");

	while (loop < (num_games - 1))
	{
		strcat(mylist, "\n");
		strcat(mylist, "Game Name: ");
		strcat(mylist, driver[loop].desc);
		strcat(mylist, ":\n");
		strcat(mylist, "Rom  Name: ");
		strcat(mylist, driver[loop].name);
		strcat(mylist, ".zip\n");
		wrlog("gamename %s", driver[loop].name);
		wrlog("gamename %s", driver[loop].desc);
		while (driver[loop].rom[loop2].romSize > 0)
		{
			if (driver[loop].rom[loop2].loadAddr == 999)
			{
				sprintf(str, "ROM_REGION(0x%04x, %s)\n", driver[loop].rom[loop2].romSize, rom_regions[driver[loop].rom[loop2].loadtype]);
				strcat(mylist, str);
			}
			else
				if (driver[loop].rom[loop2].loadAddr != 999)
				{
					if (driver[loop].rom[loop2].filename != (char*)-1)
					{
						if (driver[loop].rom[loop2].filename == (char*)-2)
							strcat(mylist, "ROM_CONTINUE");
						else
							strcat(mylist, driver[loop].rom[loop2].filename);

						strcat(mylist, " Size: ");
						sprintf(str, "0x%04x", driver[loop].rom[loop2].romSize);
						strcat(mylist, str);

						strcat(mylist, " Load Addr: ");
						sprintf(str, "0x%04x", driver[loop].rom[loop2].loadAddr);
						strcat(mylist, str);
						
						strcat(mylist, " CRC: ");
						sprintf(str, "0x%04x", driver[loop].rom[loop2].crc);
						strcat(mylist, str);

						strcat(mylist, " SHA1: ");
						sprintf(str, "%s", driver[loop].rom[loop2].sha);
						strcat(mylist, str);
						strcat(mylist, "\n");
					}
				}
			loop2++;
		}
		loop2 = 0; //reset!!
		loop++;
	}

	strcat(mylist, "\n\nNumber of Games/Clones supported: ");
	sprintf(str, "%d", num_games - 1);
	strcat(mylist, str);
	strcat(mylist, "\n");
	strcat(mylist, "\0");
	wrlog("SAVING Romlist");
	save_file_char("AAE All Game Roms List.txt", mylist, strlen(mylist));
	free(mylist);
}

/*************************************
 *
 *	Command Line Parsing Function
 *
 *************************************/

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

/*************************************
 *
 *	Sort the game list for the GUI
 *
 *************************************/

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

/*************************************
 *
 *	Shadow input port creation for
 * the Menu system and save
 *
 *************************************/

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

	if (osd_key_pressed_memory(OSD_KEY_F6))
	{
		logging ^= 1;
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

/*************************************
 *
 *	Speed Throttling:
 * from really of MAME code.
 *
 *************************************/

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
	wrlog("Running game %s", Machine->gamedrv->desc);

	//Check for setting greater then screen availability
	if (config.screenh > cy || config.screenw > cx)
	{
		allegro_message("MESSAGE", "Warning: \nphysical size smaller then config setting");
	}

	//////////////////////////////////////////////////////////////INITIAL VARIABLES SETUP ///////////////////////////////////////////////////
	options.cheat = 1;
	//set_aae_leds(0, 0, 0);  //Reset LEDS
	leds_status = 0;
	force_all_kbdleds_off();

	config.mainvol *= 12.75;
	config.pokeyvol *= 12.75; //Adjust from menu values
	//config.noisevol *= 12.75;

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

	init_machine();
	reset_memory_tracking(); // Before loading Anything!!!
	//////////////////////////////////////////////////////////////END VARIABLES SETUP ///////////////////////////////////////////////////
	if (Machine->gamedrv->rom )
	{
		goodload = load_roms(Machine->gamedrv->name, Machine->gamedrv->rom);
		if (goodload == EXIT_FAILURE) {
			wrlog("Rom loading failure, exiting..."); 
			have_error = 10; 
			gamenum = 0;
			if (!in_gui) { exit(1); }
			}

		if (Machine->gamedrv->artwork)
		{
			load_artwork(Machine->gamedrv->artwork);
			resize_art_textures();
		}
	}

	setup_video_config();
	if (config.bezel && gamenum) { msx = b1sx; msy = b1sy; esx = b2sx; esy = b2sy; }
	else { msx = sx; msy = sy; esx = ex; esy = ey; }

	frameavg = 0; fps_count = 0; frames = 0;
	//////////////////////////

	millsec = (double)1000 / (double) Machine->gamedrv->fps;

	mixer_init(config.samplerate, Machine->gamedrv->fps);

	// Load samples if the game has any. This needs to move!
	if (Machine->gamedrv->game_samples)
	{
		goodload = read_samples(Machine->gamedrv->game_samples, 0);
		if (goodload == EXIT_FAILURE) { wrlog("Samples loading failure, please check error output for details..."); }
	}

	//Now load the Ambient and menu samples
	// TODO: This has been disabled for now. !!!
	// TODO: Add this back as a separate routine, we can do this now with the new sound engine!!!!!!
	
	
	goodload = read_samples(noise_samples, 1);
	if (goodload == EXIT_FAILURE) { wrlog("Noise Samples loading failure, not critical, continuing."); }

	wrlog("Number of samples for this game is %d", num_samples);
	
	wrlog("Loaded sample number here is %d", nameToNum("flyback"));
	wrlog("Loaded sample NAME here is %s", numToName(7));

	setup_ambient(VECTOR);
	// Setup for the first game.
	wrlog("Initializing Game");
	wrlog("Loading InputPort Settings");
	load_input_port_settings();
	init_cpu_config(); ////////////////////-----------

	//Run this before Driver Init. 
	if (!(Machine->gamedrv->video_attributes & VIDEO_TYPE_VECTOR))
	{
		vh_open();
	}
			
	Machine->gamedrv->init_game();

	hiscoreloaded = 0;

	//If the game uses NVRAM, initalize/load it. . This code is from M.A.M.E. (TM)
	if (Machine->gamedrv->nvram_handler)
	{
		void* f;

		f = osd_fopen(Machine->gamedrv->name, 0, OSD_FILETYPE_NVRAM, 0);
		(*Machine->gamedrv->nvram_handler)(f, 0);
		if (f) osd_fclose(f);
	}

	wrlog("OpenGL Init");
	init_gl();
	end_gl();
	// TODO: Revisit THis
	texture_reinit(); //This cleans up the FBO textures preping them for reuse.

	wrlog("\n\n----END OF INIT -----!\n\n");
	//reset_for_new_game(gamenum, 0);
}

/*************************************
 *
 *	Main Emulation Loop
 *
 *************************************/

void emulator_run()
{
	if (config.debug_profile_code) {
		wrlog("Start of Frame");
	}
	// Load High Score
	if (hiscoreloaded == 0 && Machine->gamedrv->hiscore_load)
		hiscoreloaded = ((*Machine->gamedrv->hiscore_load))();

	// Setup for rendering a frame.
	if (Machine->drv->video_attributes & VIDEO_TYPE_VECTOR)
	{
		set_render();
	}
	else
	{
		//set_render_raster();
		set_render();
	}
	

	auto start = chrono::steady_clock::now();

	if (!paused && have_error == 0)
	{
		update_input_ports();
		osd_poll_joysticks();

		//if (driver[gamenum].pre_run) driver[gamenum].pre_run();
		if (config.debug_profile_code) {
			wrlog("Calling CPU Run");
		}
		cpu_run();
		if (Machine->gamedrv->run_game) Machine->gamedrv->run_game();
	}

	auto end = chrono::steady_clock::now();
	auto diff = end - start;
	if (config.debug_profile_code) {
		wrlog("Profiler: CPU Time: %f ", chrono::duration <double, milli>(diff).count());
	}
	// Complete and display the rendered frame after all video updates.
	render();

	msg_loop();

	inputport_vblank_end();
	//timer_clear_all_eof();
	cpu_clear_cyclecount_eof();

	throttle_speed();

	frames++;
	if (frames > 0xfffffff) { frames = 0; }
	//wrlog("Debug Remove: Mixer Update Start");
	mixer_update();
	if (config.debug_profile_code) {
		wrlog("End of Frame");
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

	// Handle List all roms
	for (int i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "-listromstotext") == 0)
		{
			wrlog("Listing all roms");
			list_all_roms();
			exit(1);
		}
	}

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
	//fillstars(stars);
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

/*************************************
 *
 *	End of emulation, cleanup and get out.
 *
 *************************************/

void emulator_end()
{
	wrlog("Emulator End Called.");

	// If we're using high scores, write them to disk.
	if (hiscoreloaded != 0 && Machine->gamedrv->hiscore_save)
		(*Machine->gamedrv->hiscore_save)();

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
	// Free samples??
	// Free textures??
	//END-----------------------------------------------------
	if (Machine->gamedrv->end_game) Machine->gamedrv->end_game();
	save_input_port_settings();
	if (Machine->input_ports) { free(Machine->input_ports); wrlog("Machine Input Ports Freed."); }
	free_cpu_memory();
	free_all_memory_regions();


	KillFont();

	AllowAccessibilityShortcutKeys(1);
	mixer_end();
	force_all_kbdleds_off();
}