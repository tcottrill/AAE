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
#include <vector>
#include <sstream>
#include <iomanip>
#include <cstring>   // strlen
#include "utf8conv.h"
#include "old_mame_raster.h"
#include "game_list.h"
//#include <thread>
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

static int throttle = 1;
//New 2025
static bool game_loaded_sentinel = 0;
int art_loaded[6] = {};
extern int leds_status;

GameList gamelist(driver);

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
	LOG_INFO("New Display width %d, height %d", width, height);

	/* Look if this is a vector game */
	if (Machine->drv->video_attributes & VIDEO_TYPE_VECTOR)
	{
		LOG_INFO("Init: Vector game starting");
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
	LOG_INFO("Main Bitmap Created");
	if (!main_bitmap)
	{
		LOG_INFO("Bitmap create failed, why?");
		return 0;
	}
	LOG_INFO("exiting create display");
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

	LOG_INFO("Running vh_open");

	LOG_INFO("MIN Y:%d ", Machine->drv->visible_area.min_y);
	LOG_INFO("MIN X:%d ", Machine->drv->visible_area.min_x);
	LOG_INFO("MAX Y:%d ", Machine->drv->visible_area.max_y);
	LOG_INFO("MAX x:%d ", Machine->drv->visible_area.max_x);

	LOG_INFO("1");
	convtable = (unsigned char*)malloc(MAX_GFX_ELEMENTS * MAX_COLOR_TUPLE * MAX_COLOR_CODES);
	if (!convtable) return 1;

	for (i = 0; i < MAX_GFX_ELEMENTS; i++) Machine->gfx[i] = 0;

	LOG_INFO("2");
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
			LOG_INFO("I here at gfx convert is %d, memregion is %d", i, Machine->gamedrv->gfxdecodeinfo[i].memory_region);
			Machine->gfx[i]->colortable = &remappedtable[Machine->gamedrv->gfxdecodeinfo[i].color_codes_start];
			Machine->gfx[i]->total_colors = Machine->gamedrv->gfxdecodeinfo[i].total_color_codes;
			LOG_INFO("Colortable here is remap table at is %d,total color codes is %d", Machine->gamedrv->gfxdecodeinfo[i].color_codes_start, Machine->gamedrv->gfxdecodeinfo[i].total_color_codes);
		}
	}
	LOG_INFO("3");
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

	LOG_INFO("4");
	/* create the display bitmap, and allocate the palette */
	if ((Machine->scrbitmap = osd_create_display(
		Machine->gamedrv->screen_width, Machine->gamedrv->screen_height, Machine->gamedrv->total_colors,
		palette, Machine->pens, Machine->gamedrv->video_attributes)) == 0)
	{
		LOG_INFO("Why is this returning?");
		free(convtable);
		exit(1);
	}
	else
	{
		LOG_INFO("Created Display surface, Width: %d Height: %d", Machine->gamedrv->screen_width, Machine->gamedrv->screen_height);
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

	LOG_INFO("TotalColors here is %d", Machine->gamedrv->total_colors);
	for (i = 0; i < Machine->gamedrv->total_colors; i++)
	{
		current_palette[Machine->pens[i]][0] = palette[3 * i];
		current_palette[Machine->pens[i]][1] = palette[3 * i + 1];
		current_palette[Machine->pens[i]][2] = palette[3 * i + 2];
	}

	LOG_INFO("Color Table Len %d", Machine->gamedrv->color_table_len);
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
	LOG_INFO("Returning from vh_open");
	return 0;
}

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
	Machine->gamedrv = gamedrv = &driver[game];
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
	if (num_games <= 0) return;
	// ---------------------------------------------------------------------
	// Dynamic buffer (pre-reserve old malloc size to avoid realloc churn)
	// ---------------------------------------------------------------------
	std::vector<char> buf;
	buf.reserve(0x150000);

	// ---------------------------------------------------------------------
	// Local helpers – lambdas capture buf by reference
	// ---------------------------------------------------------------------
	const auto append_cstr = [&](const char* s) {buf.insert(buf.end(), s, s + std::strlen(s)); };
	const auto append_str = [&](const std::string& s) {buf.insert(buf.end(), s.begin(), s.end()); };
	const auto append_chr = [&](char c) {buf.push_back(c); };

	// ---------------------------------------------------------------------
	// Start of file
	// ---------------------------------------------------------------------
	append_cstr("AAE All Games RomList\n");

	for (int g = 0; g < num_games; ++g)
	{
		const AAEDriver& drv = driver[g];

		if (!drv.name || !drv.desc || !drv.rom)
			continue;

		append_cstr("\nGame Name: ");
		append_cstr(drv.desc);
		append_cstr(":\nRom  Name: ");
		append_cstr(drv.name);
		append_cstr(".zip\n");

		LOG_INFO("gamename %s", drv.name);
		LOG_INFO("gamedesc %s", drv.desc);

		for (int r = 0; drv.rom[r].romSize > 0; ++r)
		{
			const RomModule& rm = drv.rom[r];

			if (rm.loadAddr == REGION_TAG)
			{
				std::ostringstream oss;
				oss << "ROM_REGION(0x"
					<< std::hex << std::setw(4) << std::setfill('0') << rm.romSize
					<< ", " << rom_regions[rm.loadtype] << ")\n";
				append_str(oss.str());
				continue;
			}

			if (rm.filename == reinterpret_cast<char*>(-1))
				continue;

			if (rm.filename == reinterpret_cast<char*>(-2))
				append_cstr("ROM_CONTINUE");
			else
				append_cstr(rm.filename);

			std::ostringstream oss;
			oss << " Size: 0x"
				<< std::hex << std::setw(4) << std::setfill('0') << rm.romSize
				<< " Load Addr: 0x"
				<< std::hex << std::setw(4) << std::setfill('0') << rm.loadAddr
				<< " CRC: 0x"
				<< std::hex << std::setw(4) << std::setfill('0') << rm.crc
				<< " SHA1: " << (rm.sha ? rm.sha : "NULL") << '\n';

			append_str(oss.str());
		}
	}
	// Footer
	std::ostringstream footer;
	footer << "\n\nNumber of Games/Clones supported: "
		<< (num_games - 1) << '\n';
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
	buf.clear();                // clears contents, keeps capacity
	std::vector<char>().swap(buf);  // optional: truly release memory
}

/*************************************
 *
 *	Command Line Parsing Function
 *  Most of this still doesn't work
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
		LOG_INFO("Starting rom verify");
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
		LOG_INFO("Starting sample verify");
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
  *
 *************************************/

void throttle_speed()
{
	const double ms_per_frame = 1000.0 / driver[gamenum].fps;
	double current_time = TimerGetTimeMS();

	if (throttle)
	{
		while ((current_time - starttime) < ms_per_frame)
		{
			double time_left = ms_per_frame - (current_time - starttime);

			if (time_left > 2.0)
				Sleep(1); // Allow other threads time
			else
				//Sleep(0); // Yield remainder of timeslice
				YieldProcessor();

			current_time = TimerGetTimeMS();
		}
	}

	if (frames > 60)
	{
		frameavg++;
		if (frameavg > 10000) { frameavg = 0; fps_count = 0.0; }
		double frame_time = current_time - starttime;
		if (frame_time > 0.0)
			fps_count += 1000.0 / frame_time;
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

	/*
	SetThreadPriority(GetCurrentThread(), ABOVE_NORMAL_PRIORITY_CLASS);

	switch (config.priority)
	{
	case 4:
		if (!SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS))
			LOG_INFO("Failed to set REALTIME priority. Error code: %lu", GetLastError());
		else
			LOG_INFO("Process priority set to REALTIME_PRIORITY_CLASS");
		break;
	case 3:
		if (!SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS))
			LOG_INFO("Failed to set HIGH priority. Error code: %lu", GetLastError());
		else
			LOG_INFO("Process priority set to HIGH_PRIORITY_CLASS");
		break;
	case 2:
		if (!SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS))
			LOG_INFO("Failed to set ABOVE NORMAL priority. Error code: %lu", GetLastError());
		else
			LOG_INFO("Process priority set to ABOVE_NORMAL_PRIORITY_CLASS");
		break;
	case 1:
		if (!SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS))
			LOG_INFO("Failed to set NORMAL priority. Error code: %lu", GetLastError());
		else
			LOG_INFO("Process priority set to NORMAL_PRIORITY_CLASS");
		break;
	case 0:
		if (!SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS))
			LOG_INFO("Failed to set IDLE priority. Error code: %lu", GetLastError());
		else
			LOG_INFO("Process priority set to IDLE_PRIORITY_CLASS");
		break;
	default:
		LOG_INFO("Unknown priority level: %d", config.priority);
		break;
	}
	LimitThreadAffinityToCurrentProc();
	*/
	/*
	switch (config.priority)
	{
	case 4: SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS); break;
	case 3: SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS); break;
	case 2: SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS); break;
	case 1: SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS); break;
	case 0: SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS); break;
	}
	LimitThreadAffinityToCurrentProc();
	*/
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
			exit(1);
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

	goodload = read_samples(noise_samples, 1);
	if (goodload == EXIT_FAILURE) { LOG_INFO("Noise Samples loading failure, not critical, continuing."); }

	LOG_INFO("Number of samples for this game is %d", num_samples);
	// Configure the Ambient Sounds.
	setup_ambient(VECTOR);
	// Setup for the first game.
	LOG_INFO("Initializing Game");
	LOG_INFO("Loading InputPort Settings");
	load_input_port_settings();
	init_cpu_config(); ////////////////////-----------

	// At this point, we know we are running a game, so set this to true
	game_loaded_sentinel = true;

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
	auto start = chrono::steady_clock::now();

	if (get_menu_status() == 0 && !paused) { ClipAndHideCursor(win_get_window()); }//scare_mouse(); CaptureMouseToWindow(win_get_window()); }
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
		cpu_run();

		auto end = chrono::steady_clock::now();
		auto diff = end - start;
		if (config.debug_profile_code) {
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

	throttle_speed();

	if (throttle)
	{
		mixer_update();
	}

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
	int i;
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
	while (driver[loop].name != 0) { num_games++; loop++; }
	LOG_INFO("Number of supported games is: %d", num_games);
	num_games++;

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

	toLowerCase(argv[1]);

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

	run_game();
	LOG_INFO("Starting Run Game");
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
	//force_all_kbdleds_off();
	osd_set_leds(0);
	LOG_INFO("End Final");
}

/*
static void throttle_speed(void)
{
	double millsec = (double)1000 / (double)driver[gamenum].fps;
	gametime = TimerGetTimeMS();
	if (throttle) {
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
			else YieldProcessor();
			gametime = TimerGetTimeMS();
		}
	}
	if (frames > 60)
	{
		frameavg++;
		if (frameavg > 10000) { frameavg = 0; fps_count = 0; }
		fps_count += 1000 / ((double)(gametime)-(double)starttime);
	}

	starttime = TimerGetTimeMS();
}
*/
/*
static void throttle_speed(void)
{
	const double millsec = 1000.0 / driver[gamenum].fps;
	const double sleep_threshold = millsec - 4.0;

	gametime = TimerGetTimeMS();
	double elapsed = gametime - starttime;

	if (throttle)
	{
		HANDLE current_thread = GetCurrentThread();
		int old_priority = GetThreadPriority(current_thread);

		while (elapsed < millsec)
		{
			if (elapsed < sleep_threshold)
			{
				SetThreadPriority(current_thread, THREAD_PRIORITY_TIME_CRITICAL);
				Sleep(1);
				SetThreadPriority(current_thread, old_priority);
			}
			else
			{
				YieldProcessor(); // efficient busy-wait
			}

			gametime = TimerGetTimeMS();
			elapsed = gametime - starttime;
		}
	}

	if (frames > 60)
	{
		frameavg++;
		if (frameavg > 10000)
		{
			frameavg = 0;
			fps_count = 0;
		}

		double frame_time = gametime - starttime;
		if (frame_time > 0.0)
			fps_count += 1000.0 / frame_time;
	}

	starttime = TimerGetTimeMS();
}
 */