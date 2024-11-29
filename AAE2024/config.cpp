#include "allegro.h"
#include "winalleg.h"
#include <stdio.h>
#include "config.h"
#include "aae_mame_driver.h"
#include "menu.h"
#include "log.h"

extern int gamenum;

int is_override = 0; //1 if file exists
char aaepath[360];
char gamepath[360];

static int file_is(const char* filename)
{
	FILE* file;

	if (file = fopen(filename, "r"))
	{
		fclose(file); return 1;
	}
	return 0;
}

char* my_get_config_string(const char* section, const char* name, const char* def)
{
	int val = -1;
	char* buffer;
	buffer = (char*)malloc(256);

	if (is_override) {
		GetPrivateProfileString(section, name, def, buffer, 256, gamepath);
		if (strcmp(buffer, def) == 0) { GetPrivateProfileString(section, name, def, buffer, 256, aaepath); return buffer; }
		else { return buffer; }
	}

	else {
		GetPrivateProfileString(section, name, def, buffer, 256, aaepath);
		if (strcmp(buffer, def) == 0) return (char*)def;
		else return buffer;
	}
}

int my_get_config_int(const char* section, const char* name, int def)
{
	int val = -1;
	if (is_override) { val = GetPrivateProfileInt(section, name, -1, gamepath); }
	if (val == -1) { return GetPrivateProfileInt(section, name, def, aaepath); }
	else { return val; }
}

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

	if (path == 0) { _snprintf(buffer, 64, "%d", val); WritePrivateProfileString(section, name, buffer, aaepath); }
	else { _snprintf(buffer, 64, "%d", val); WritePrivateProfileString(section, name, buffer, gamepath); }
}

void my_set_config_float(char* section, char* name, float val, int path)
{
	char buffer[64];

	if (path == 0) { _snprintf(buffer, 64, "%f", val); WritePrivateProfileString(section, name, buffer, aaepath); }
	else { _snprintf(buffer, 64, "%f", val); WritePrivateProfileString(section, name, buffer, gamepath); }
}

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
		bezelzoom = my_get_config_float((char*)driver[gamenum].name, "bezcropzoom", 1.0);//1.25;
		bezelx = my_get_config_int((char*)driver[gamenum].name, "bezcropx", 0);
		bezely = my_get_config_int((char*)driver[gamenum].name, "bezcropy", 0);
		overalpha = my_get_config_float((char*)driver[gamenum].name, "overalpha", 1.0);//1.25;
		//bezely=-50;
		//overalpha=1.0;
		//overbright=0;
	//bezelx=-100;
	}
	strcpy(aaepath, buffer);
	strcat(aaepath, "\\aae.ini");
}

//SETUP CONFIGURATION DATA
void setup_config(void)
{
	char buffer[MAX_PATH];

	GetCurrentDirectory(MAX_PATH, buffer);

	strcpy(aaepath, buffer);
	strcat(aaepath, "\\aae.ini");
	strcpy(gamepath, buffer);
	strcat(gamepath, "\\cfg\\");
	strcat(gamepath, driver[gamenum].name);
	strcat(gamepath, ".ini");

	is_override = file_is(gamepath);
	if (gamenum == 0) { is_override = 0; }
	wrlog("Main AAE Path: %s", aaepath);
	if (gamenum > 0) { wrlog("Game Config Path: %s", gamepath); }
	wrlog("Path Override Value: %d", is_override);
	//wrlog("Starting");

	wrlog("Loading configuration information for %s", driver[gamenum].desc);

	config.exrompath = my_get_config_string("main", "mame_rom_path", "NONE");
	wrlog("Configured Mame Rom Path is %s", config.exrompath);
	//////VIDEO///////////
	config.aspect = my_get_config_string("main", "aspect_ratio", "4:3");
	config.prescale = my_get_config_int("main", "prescale", 1);
	config.anisfilter = my_get_config_int("main", "anisfilter", 0);
	config.translucent = my_get_config_int("main", "translucent", 0);
	config.bezel = my_get_config_int("main", "bezel", 1);
	config.artwork = my_get_config_int("main", "artwork", 0);
	config.artcrop = my_get_config_int("main", "artcrop", 0);
	config.overlay = my_get_config_int("main", "overlay", 0);
	config.screenw = my_get_config_int("main", "screenw", 1024);
	config.screenh = my_get_config_int("main", "screenh", 768);
	config.windowed = my_get_config_int("main", "windowed", 0);
	config.vid_rotate = my_get_config_int("main", "vid_rotate", 1);
	config.colordepth = my_get_config_int("main", "colordepth", 32);
	config.m_line = my_get_config_int("main", "m_line", 20);
	config.m_point = my_get_config_int("main", "m_point", 16);
	config.linewidth = my_get_config_float("main", "linewidth", 2);
	//allegro_message("linewidth %f",config.linewidth);
	config.pointsize = my_get_config_float("main", "pointsize", 1.6);
	//allegro_message("pointsize %f",config.pointsize);
	config.forcesync = my_get_config_int("main", "force_vsync", 1);
	config.drawzero = my_get_config_int("main", "drawzero", 0);
	config.widescreen = my_get_config_int("main", "widescreen", 0);
	config.dblbuffer = my_get_config_int("main", "doublebuffer", 1);
	config.snappng = my_get_config_int("main", "snappng", 1);
	config.gamma = my_get_config_int("main", "gamma", 127);
	config.bright = my_get_config_int("main", "bright", 127);
	config.contrast = my_get_config_int("main", "contrast", 127);
	config.explode_point_size = my_get_config_int("main", "explode_point_size", 12);
	config.fire_point_size = my_get_config_int("main", "fire_point_size", 12);
	config.vecglow = my_get_config_int("main", "vectorglow", 5);
	config.vectrail = my_get_config_int("main", "vectortrail", 1);
	config.gain = my_get_config_int("main", "gain", 1);
	//config.accumlevel = my_get_config_int("main","accumlevel", 0);
	//config.colorhack = get_config_int("main","colorhack", 0);
	//config.bz_sstick = get_config_int("main","sstick", 1);
	//////KEYS///////
	//config.ksfps = my_get_config_int(NULL,"ksfps", 56);
	//config.showfps = my_get_config_int("main", "showfps" , 0);

	//config.kquit = my_get_config_int("main","kquit", 59);
	//config.kmenu = my_get_config_int("main","kmenu", 64);
	//config.kreset = my_get_config_int("main","kreset",107);
	//config.ktest = my_get_config_int("main","ktest", 75);
	//config.kpause = my_get_config_int("main","kpause", 16);
	//config.ksnap = my_get_config_int("main","ksnap", 59);
/*
	config.kstart1 = get_config_int(NULL,"kstart1", 28);
	config.kstart2 = get_config_int(NULL,"kstart2", 29);
	config.kcoin1 = get_config_int(NULL,"kcoin1", 32);
	config.kcoin2 = get_config_int(NULL,"kcoin2", 33);
	config.kp1up = get_config_int(NULL,"kp1up", 84);
	config.kp1down = get_config_int(NULL,"kp1down", 85);
	config.kp1left = get_config_int(NULL,"kp1left", 82);
	config.kp1right = get_config_int(NULL,"kp1right", 83);
	config.kp1but1 = get_config_int(NULL,"kp1but1", 117);
	config.kp1but2 = get_config_int(NULL,"kp1but2",119);
	config.kp1but3 = get_config_int(NULL,"kp1but3", 75);
	config.kp1but4 = get_config_int(NULL,"kp1but4", 83);
	config.kp1but5 = get_config_int(NULL,"kp1but5", 82);
	config.kp1but6 = get_config_int(NULL,"kp1but6", 83);
//	config.kp1but7 = get_config_int(NULL,"kp1but7", 44);
//	config.kp1but8 = get_config_int(NULL,"kp1but8", 45);
//	config.kp1but9 = get_config_int(NULL,"kp1but9", 46);

	config.kp2up = get_config_int(NULL,"kp2up", 84);
	config.kp2down = get_config_int(NULL,"kp2down", 85);
	config.kp2left = get_config_int(NULL,"kp2left", 82);
	config.kp2right = get_config_int(NULL,"kp2right", 83);
	config.kp2but1 = get_config_int(NULL,"kp2but1", 117);
	config.kp2but2 = get_config_int(NULL,"kp2but2",119);
	config.kp2but3 = get_config_int(NULL,"kp2but3", 75);
	config.kp2but4 = get_config_int(NULL,"kp2but4", 83);
	config.kp2but5 = get_config_int(NULL,"kp2but5", 82);
	config.kp2but6 = get_config_int(NULL,"kp2but6", 83);
//	config.kp2but7 = get_config_int(NULL,"kp2but7", 68);
//	config.kp2but8 = get_config_int(NULL,"kp2but8", 69);
//	config.kp2but9 = get_config_int(NULL,"kp2but9", 70);

	config.j1up =   get_config_int("main","jp1up", 2);
	config.j1down = get_config_int("main","jp1down", 3);
	config.j1left = get_config_int("main","jp1left", 0);
	config.j1right = get_config_int("main","jp1right", 1);
	config.j1but1 = get_config_int("main","jp1but1", 32);
	config.j1but2 = get_config_int("main","jp1but2", 33);
	config.j1but3 = get_config_int("main","jp1but3", 34);
	config.j1but4 = get_config_int("main","jp1but4", 35);
	config.j1but5 = get_config_int("main","jp1but5", 36);
	config.j1but6 = get_config_int("main","jp1but6", 37);
	config.j1but7 = get_config_int("main","jp1but7", 38);
	config.j1but8 = get_config_int("main","jp1but8", 39);

	config.j2up = get_config_int("main","jp2up", 66);
	config.j2down = get_config_int("main","jp2down", 67);
	config.j2left = get_config_int("main","jp2left", 64);
	config.j2right = get_config_int("main","jp2right", 65);
	config.j2but1 = get_config_int("main","jp2but1", 96);
	config.j2but2 = get_config_int("main","jp2but2", 97);
	config.j2but3 = get_config_int("main","jp2but3", 98);
	config.j2but4 = get_config_int("main","jp2but4", 99);
	config.j2but5 = get_config_int("main","jp2but5", 100);
	config.j2but6 = get_config_int("main","jp2but6", 101);
	config.j2but7 = get_config_int("main","jp2but7", 102);
	config.j2but8 = get_config_int("main","jp2but8", 103);
	*/
	////MOUSE//////////////////
	//config.mouse1xs= my_get_config_int("main","mouse1xs", 5);
	//config.mouse1ys= my_get_config_int("main","mouse1ys", 5);
	//config.mouse1x_invert= my_get_config_int("main","mouse1x_invert", 0);
	//config.mouse1y_invert= my_get_config_int("main","mouse1y_invert", 0);
	//config.mouse1b1= my_get_config_int("main","mouse1b1", 41);
	//config.mouse1b2= my_get_config_int("main","mouse1b2", 42);

	config.psnoise = my_get_config_int("main", "psnoise", 0);
	config.hvnoise = my_get_config_int("main", "hvnoise", 0);
	config.pshiss = my_get_config_int("main", "pshiss", 0);
	config.pokeyvol = my_get_config_int("main", "pokeyvol", 200);
	config.mainvol = my_get_config_int("main", "mainvol", 220);
	config.noisevol = my_get_config_int("main", "noisevol", 200);
	config.priority = my_get_config_int("main", "priority", 1);
	config.showinfo = my_get_config_int("main", "showinfo", 0);
	config.kbleds = my_get_config_int("main", "kbleds", 1);
}
void setup_game_config(void)
{
	char tempname[255];
	strcpy(tempname, "AAE Alpha Running ");
	strcat(tempname, driver[gamenum].desc);

	set_window_title(tempname);
	setup_config();

	// setup_video_menu();
	// setup_sound_menu();
	// setup_mouse_menu();
	// set_points_lines();
}