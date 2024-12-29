#include "allegro.h"
#include "winalleg.h"
#include <stdio.h>
#include "config.h"
#include "aae_mame_driver.h"
#include "menu.h"
#include "log.h"



extern int gamenum;

int is_override = 0; //1 if file exists
char aaepath[MAX_PATH];
char gamepath[MAX_PATH];

static int file_exists(const char* filename)
{
	FILE* file;

	if (file = fopen(filename, "r"))
	{
		fclose(file); return 1;
	}
	return 0;
}


// If return size is 0, value is empty:  DWORD size = GetPrivateProfileString("SectionName", "KeyName", "", buffer, 256, "path/to/your/ini.ini");
char* my_get_config_string(const char* section, const char* name, const char* def)
{
	int val = -1;
	char* buffer;
	buffer = (char*)malloc(MAX_PATH);
	DWORD size = 0;

	if (is_override) { // Check game file for value if file exists
		//wrlog("Returning string value from game file %s", name);
		size = GetPrivateProfileString(section, name, def, buffer, MAX_PATH, gamepath);
		if (strcmp(buffer, def) == 0) 
		{ 
			//wrlog("Returning string value from aae.ini %s", name);
			GetPrivateProfileString(section, name, def, buffer, MAX_PATH, aaepath); 
			return buffer;
		}
		else { return buffer; }
	}
	else {
		//wrlog("Game file doesn't exist, Returning string value from aae.ini %s", name);
		size = GetPrivateProfileString(section, name, def, buffer, MAX_PATH, aaepath);
		if ( strcmp(buffer, def) == 0) return (char*)def;
		else return buffer;
	}
}

int my_get_config_int(const char* section, const char* name, int def)
{
	int val = -1;
	if (is_override) // Try to get the value from the game file
	{
		val = GetPrivateProfileInt(section, name, -1, gamepath);

		if (val == -1)
		{
			//LOG_DEBUG("Returning int value from aae.ini file %s val %d", name, val);
			return GetPrivateProfileInt(section, name, def, aaepath); // Value does not exist in game file, get from aae.ini
		}
		else
		{
			//LOG_DEBUG("Returning int value from game file %s", name);
			return val;  // Return value from Game file
		}
	}
	else 
	{
		//LOG_DEBUG("Returning int value from aae.ini file since the game file does not exist. %s val %d", name, val);
		return GetPrivateProfileInt(section, name, def, aaepath); // Value does not exist in game file, get from aae.ini
	}
}

//Broken
double my_get_config_float(const char* section, const char* name, const double def)
{
	float val = -1;
	char buffer[64];

	if (is_override) { GetPrivateProfileString(section, name, "", buffer, sizeof(buffer), gamepath); }

	else { val = GetPrivateProfileString(section, name, "", buffer, sizeof(buffer), aaepath); }
	val = GetPrivateProfileString(section, name, "", buffer, sizeof(buffer), aaepath);
	val = atof(buffer);
	if (val == 0) { return def; }
	else { return val; }
}

void my_set_config_int(const char* section, const char* name, int val, int path)
{
	char buffer[64];

	if (path == 0) { snprintf(buffer, 64, "%d", val); WritePrivateProfileString(section, name, buffer, aaepath); }
	else { snprintf(buffer, 64, "%d", val); WritePrivateProfileString(section, name, buffer, gamepath); }
}

void my_set_config_float(char* section, char* name, float val, int path)
{
	char buffer[64];

	if (path == 0) { snprintf(buffer, 64, "%f", val); WritePrivateProfileString(section, name, buffer, aaepath); }
	else { snprintf(buffer, 64, "%f", val); WritePrivateProfileString(section, name, buffer, gamepath); }
}

// This sets up the drawing rect for each game, as well as with bezel, and for cropped bezel.
// Not ideal, and will need to be completly changed for new rendering.
// This sets the global variables sx, sy, ex, ey, reset every game. as well as b1sx, b1sy, b2sx, b2sy for bezel rendering.
void setup_video_config()
{
	char buffer[MAX_PATH];

	GetCurrentDirectory(MAX_PATH, buffer);

	strcpy(aaepath, buffer);
	strcat(aaepath, "\\video.ini");
	is_override = 0;

	//int b1sx,b1sy,b2sx,b2sy;
	//final_render(sx,sy,ex,ey,0,0);
	//int xmin, int xmax, int ymin, int ymax

							  //For main texture rendering
	sx = my_get_config_int((char*)driver[gamenum].name, "fullsx", 0);
	sy = my_get_config_int((char*)driver[gamenum].name, "fullsy", 900);
	ex = my_get_config_int((char*)driver[gamenum].name, "fullex", 900);
	ey = my_get_config_int((char*)driver[gamenum].name, "fulley", 0);
	overalpha = my_get_config_float((char*)driver[gamenum].name, "overalpha", 1.0);

	if (config.bezel && gamenum && config.artcrop == 0) {
		b1sx = my_get_config_int((char*)driver[gamenum].name, "bezelsx", 0);
		b1sy = my_get_config_int((char*)driver[gamenum].name, "bezelsy", 900);
		b2sx = my_get_config_int((char*)driver[gamenum].name, "bezelex", 900);
		b2sy = my_get_config_int((char*)driver[gamenum].name, "bezeley", 0);
		bezelzoom = 1.0;
		bezelx = 0;
		bezely = 0;
		overalpha = my_get_config_float((char*)driver[gamenum].name, "overalpha", 1.0);
	}

	if (config.bezel && config.artcrop && gamenum) {
		b1sx = my_get_config_int((char*)driver[gamenum].name, "cropsx", 0);
		b1sy = my_get_config_int((char*)driver[gamenum].name, "cropsy", 900);
		b2sx = my_get_config_int((char*)driver[gamenum].name, "cropex", 900);
		b2sy = my_get_config_int((char*)driver[gamenum].name, "cropey", 0);
		bezelzoom = my_get_config_float((char*)driver[gamenum].name, "bezcropzoom", 1.0);
		bezelx = my_get_config_int((char*)driver[gamenum].name, "bezcropx", 0);
		bezely = my_get_config_int((char*)driver[gamenum].name, "bezcropy", 0);
		overalpha = my_get_config_float((char*)driver[gamenum].name, "overalpha", 1.0);
		//bezely=-50;
		//overalpha=1.0;
		//overbright=0;
	    //bezelx=-100;
	}
	strcpy(aaepath, buffer);
	strcat(aaepath, "\\aae.ini");
}

//SETUP CONFIGURATION DATA

// New way of doing things: 
// So, for a very small subset of items, check if an item exists in a game.ini file, then load it. if not, load from the default aae.ini file. 
// If changed from the aae.ini file and you are not int he gui, save the changes to the game.ini file.

void setup_config(void)
{
	char buffer[MAX_PATH];

	GetCurrentDirectory(MAX_PATH, buffer);
	// aae.ini
	strcpy(aaepath, buffer);
	strcat(aaepath, "\\aae.ini");
	// gamename.ini
	strcpy(gamepath, buffer);
	strcat(gamepath, "\\ini\\");
	strcat(gamepath, driver[gamenum].name);
	strcat(gamepath, ".ini");
	// If game file exists, and it's not the gui, try to pull values from it. 
	is_override = file_exists(gamepath);
	if (gamenum == 0) { is_override = 0; }
	wrlog("Main AAE Path: %s", aaepath);
	if (gamenum > 0) { wrlog("Game Config Path: %s", gamepath); }
	//wrlog("Path Override Value: %d", is_override);
	wrlog("Loading configuration information for %s", driver[gamenum].desc);
	//////VIDEO///////////
	
	config.prescale = my_get_config_int("main", "prescale", 1);
	
	// Not currently used
	config.vid_rotate = my_get_config_int("main", "vid_rotate", 1);
	config.anisfilter = my_get_config_int("main", "anisfilter", 0);
	config.translucent = my_get_config_int("main", "translucent", 0);
	
	// Game.ini available - video
	config.m_line = my_get_config_int("main", "m_line", 20);
	config.m_point = my_get_config_int("main", "m_point", 16);
	config.linewidth = my_get_config_float("main", "linewidth", 2.0);
	config.pointsize = my_get_config_float("main", "pointsize", 1.6);
	config.bezel = my_get_config_int("main", "bezel", 1);
	config.artwork = my_get_config_int("main", "artwork", 0);
	config.artcrop = my_get_config_int("main", "artcrop", 0);
	config.overlay = my_get_config_int("main", "overlay", 0);
	config.explode_point_size = my_get_config_int("main", "explode_point_size", 12);
	config.fire_point_size = my_get_config_int("main", "fire_point_size", 12);
	config.vecglow = my_get_config_int("main", "vectorglow", 5);
	config.vectrail = my_get_config_int("main", "vectortrail", 1);
	config.gain = my_get_config_int("main", "gain", 1);
	
	// Game.ini available - SOUND
	config.psnoise = my_get_config_int("main", "psnoise", 0);
	config.hvnoise = my_get_config_int("main", "hvnoise", 0);
	config.pshiss = my_get_config_int("main", "pshiss", 0);
	config.pokeyvol = my_get_config_int("main", "pokeyvol", 200);
	config.mainvol = my_get_config_int("main", "mainvol", 220);
	config.noisevol = my_get_config_int("main", "noisevol", 200);
	///// END OF GAME.INI AVAILABLE

	config.drawzero = my_get_config_int("main", "drawzero", 0);
	config.widescreen = my_get_config_int("main", "widescreen", 0);
	config.priority = my_get_config_int("main", "priority", 1);
	config.kbleds = my_get_config_int("main", "kbleds", 1);
	// No longer used - review for removal
	config.colordepth = my_get_config_int("main", "colordepth", 32);
	config.dblbuffer = my_get_config_int("main", "doublebuffer", 1);
	config.forcesync = my_get_config_int("main", "force_vsync", 1);
	config.snappng = my_get_config_int("main", "snappng", 1);
	config.gamma = my_get_config_int("main", "gamma", 127);
	config.bright = my_get_config_int("main", "bright", 127);
	config.contrast = my_get_config_int("main", "contrast", 127);
	config.showinfo = my_get_config_int("main", "showinfo", 0);
	config.windowed = my_get_config_int("main", "windowed", 0);
	//////////////////////////////////////////////////////////////////
	
	//Putting these last so we can override any game settings. These will only ever come from the main aae.ini file!!!
	is_override = 0;
	config.aspect = my_get_config_string("main", "aspect_ratio", "4:3");
	config.screenw = my_get_config_int("main", "screenw", 1024);
	config.screenh = my_get_config_int("main", "screenh", 768);
	config.exrompath = my_get_config_string("main", "mame_rom_path", "NONE");
	wrlog("Configured Mame Rom Path is %s", config.exrompath);
	//wrlog("Config.mainvol loaded here is %d", config.mainvol);
	config.linewidth = config.m_line * .1f;
	config.pointsize = config.m_point * .1f;
}

void setup_game_config(void)
{
	char tempname[255];
	strcpy(tempname, "AAE Alpha Running ");
	strcat(tempname, driver[gamenum].desc);

	set_window_title(tempname);
	setup_config();

	setup_video_menu();
	setup_sound_menu();
	set_points_lines();
}