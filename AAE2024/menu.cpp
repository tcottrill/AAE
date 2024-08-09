
//============================================================================
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
//============================================================================

#include "menu.h"
#include "allegro.h"
#include "aae_mame_driver.h"
#include "sys_video/glcode.h"
#include "sys_video/vector_fonts.h"
#include "inptport.h"
#include "deftypes.h"
#include "osdepend.h"
#include "os_input.h"
#include "math.h"
#include "config.h"
#include "colordefs.h"
#include <stdio.h>

//TODO: Fix this mess eventually, remove/rewite the mame menu code to simplify it.

#define MENU_INT 0
#define MENU_FLOAT 1
#define MAX_ITEMS 10
#define GLOBAL_INPUT 5
#define LOCAL_INPUT 1

#define ROOTMENU   100
#define GLOBALKEYS 200
#define LOCALKEYS  300
#define GLOBALJOY  400
#define LOCALJOY   500
#define ANALOGMENU 600
#define DIPMENU    700
#define VIDEOMENU  800
#define AUDIOMENU  900

int menuitem = 0;
int menulevel = 100;
int sublevel = 0;
int key_set_flag = 0;
int show_menu = 0;

int num_this_menu = 0;
static int currentval = 0;
int val_low = 0;
int val_high = 0;
int it = 1;
static int number = 0;
static int curr_item = 0;

typedef struct
{
	const char* heading;
	const char* options[MAX_ITEMS];
	int NumOptions;
	int Changed; //changed
	int Default;
	int current;
	int Value[MAX_ITEMS];
	int menu_type;// MENU_INT MENU_FLOAT
	float step;
	int Min;
	int Max;
} MENUS;

const char* mouse_names[] =
{
  "(none)",  "MB1","MB2","MB3","MB4"
};

const char* key_names[] =
{
   "(none)",     "A",          "B",          "C",
   "D",          "E",          "F",          "G",
   "H",          "I",          "J",          "K",
   "L",          "M",          "N",          "O",
   "P",          "Q",          "R",          "S",
   "T",          "U",          "V",          "W",
   "X",          "Y",          "Z",          "0",
   "1",          "2",          "3",          "4",
   "5",          "6",          "7",          "8",
   "9",          "0_PAD",      "1_PAD",      "2_PAD",
   "3_PAD",      "4_PAD",      "5_PAD",      "6_PAD",
   "7_PAD",      "8_PAD",      "9_PAD",      "F1",
   "F2",         "F3",         "F4",         "F5",
   "F6",         "F7",         "F8",         "F9",
   "F10",        "F11",        "F12",        "ESC",
   "TILDE",      "MINUS",      "EQUALS",     "BACKSPACE",
   "TAB",        "OPENBRACE",  "CLOSEBRACE", "ENTER",
   "COLON",      "QUOTE",      "BACKSLASH",  "BACKSLASH2",
   "COMMA",      "STOP",       "SLASH",      "SPACE",
   "INSERT",     "DEL",        "HOME",       "END",
   "PGUP",       "PGDN",       "LEFT",       "RIGHT",
   "UP",         "DOWN",       "SLASH_PAD",  "ASTERISK",
   "MINUS_PAD",  "PLUS_PAD",   "DEL_PAD",    "ENTER_PAD",
   "PRTSCR",     "PAUSE",      "ABNT_C1",    "YEN",
   "KANA",       "CONVERT",    "NOCONVERT",  "AT",
   "CIRCUMFLEX", "COLON2",     "KANJI",      "EQUALS_PAD",
   "BACKQUOTE",  "SEMICOLON",  "COMMAND",    "UNKNOWN1",
   "UNKNOWN2",   "UNKNOWN3",   "UNKNOWN4",   "UNKNOWN5",
   "UNKNOWN6",   "UNKNOWN7",   "UNKNOWN8",   "LSHIFT",
   "RSHIFT",     "LCONTROL",   "RCONTROL",   "ALT",
   "ALTGR",      "LWIN",       "RWIN",       "MENU",
   "SCRLOCK",    "NUMLOCK",    "CAPSLOCK",   "MAX"
};

static MENUS glmenu[] = {
   { "WINDOWED ", {"NO", "YES"},
   1, 100,1,1,{0,1},MENU_INT,0,0,0},
   { "RESOLUTION ", {"AUTO", "AUTO","","",""},
   1, 200,1,1,{0,1},MENU_INT,0,0,0},

   { "GAMMA ADJUST ", {"-8%","-5%","-3%","0%","+3%","+5%","+8%","+12%","+15","+18"},
   9, 300,0,0,{157,147,137,127,117,107,100,97,94,90},MENU_FLOAT,.5,-20,20},
   { "BRIGHTNESS ADJ", {"-8%","-5%","-3%","0%","+3%","+5%","+8%","+12%","+15","+18"},
   9, 300,0,0,{113,117,122,127,134,140,144,149,155,161},MENU_FLOAT,.5,-20,20},
   { "CONTRAST ADJ", {"-8%","-5%","-3%","0%","+3%","+5%","+8%","+12%","+15","+18"},
   9, 300,0,0,{157,147,137,127,117,107,100,97,94,90},MENU_FLOAT,.5,-20,20},

   { "VSYNC", {"DISABLED", "ENABLED"},
   1, 400,1,1,{0,1},MENU_INT,0,0,0},
   { "DRAW 0 LINES", {"DISABLED", "ENABLED"},
   1, 500,0,0,{0,1},MENU_INT,0,0,0},
   { "GAME ASPECT", {"4:3","+5%","+10%","EDGE TO EDGE",""},
   3, 600,0,0,{0,1,2,3,4},MENU_INT,0,0,0},
   { "PHOSPHER TRAIL", {"NONE", "LITTLE","MORE","MAX","","","","","",""},
   3, 700,0,0,{0,1,2,3,0,0,0,0,0,0},MENU_INT,0,0,0},
   { "VECTOR GLOW", {"NO", "YES","MORE","MAX","","","","","",""},
   1, 800,0,0,{0,1,2,3,0,0,0,0,0,0},MENU_FLOAT,1,0,25},
   { "LINEWIDTH", {"NO", "YES","","","","","","","",""},
   8, 900,0,0,{0,0,0,0,0,0,0,0,0,0},MENU_FLOAT,.1,1,7},
   { "POINTSIZE", {"NO", "YES","","","","","","","",""},
   8, 1000,0,0,{0,0,0,0,0,0,0,0,0,0},MENU_FLOAT,.1,1,7},
   { "MONITOR GAIN", {"ANALOG", "LCD","","","","","","","",""},
   1, 1100,0,0,{0,1,0,0,0,0,0,0,0,0},MENU_FLOAT,1,-127,127},
   { "ARTWORK", {"NO", "YES","","","","","","","",""},
   1, 1200,1,1,{0,1,0,0,0,0,0,0,0,0},MENU_INT,0,0,0},
   { "OVERLAY", {"NO", "YES","","","","","","","",""},
   1, 1300,1,1,{0,1,0,0,0,0,0,0,0,0},MENU_INT,0,0,0},
   { "BEZEL ART", {"NO", "YES","","","","","","","",""},
   1, 1400,1,1,{0,1,0,0,0,0,0,0,0,0},MENU_INT,0,0,0},
   { "CROP BEZEL", {"NO", "YES","","","","","","","",""},
   1, 1500,1,1,{0,1,0,0,0,0,0,0,0,0},MENU_INT,0,0,0},
   { "PRIORITY", {"LOW", "NORMAL","ABOVE NORMAL","HIGH","DANGER!","","","","",""},
   4, 1700,1,1,{0,1,0,0,0,0,0,0,0,0},MENU_INT,0,0,0},
   { "KB LEDS", {"DISABLED", "ENABLED","","","","","","","",""},
   1, 1800,1,1,{0,1,0,0,0,0,0,0,0,0}},
   { "NONE", {"NONE", " ", " ", " "}, 0,0, 0,0,{0,0,0,0,0,0,0,0,0,0},0,0,0,0}
};

static MENUS soundmenu[] = {
   { "MAIN VOLUME",  {"NO", "YES"},
   1, 100,0,0,{0,0},MENU_FLOAT,5,0,20},
   { "POKEY/AY VOLUME", {"NO", "YES"},
   1, 200,0,0,{0,0},MENU_FLOAT,5,0,20},
   { "AMBIENT VOLUME", {"NO", "YES"},
   1, 200,0,0,{0,0},MENU_FLOAT,5,0,20},
   { "HV CHATTER", {"NO", "YES"},
   1, 300,0,0,{0,1},MENU_INT,0,0,0},
   { "PS HISS", {"NO", "YES"},
   1, 400,0,0,{0,1},MENU_INT,0,0,0},
   { "PS NOISE",{"NO", "YES",},
   1,500,0,0,{0,1,},MENU_INT,0,0,0},
   { "NONE", { "NONE", " " }, 0, 0, 0, 0, {0,0}, 0, 0, 0, 0 }
};

static MENUS mousemenu[] = {
 { "MOUSE X SENSITIVITY ", {"1", "2","3","4","5","6","7","8","9",""},
   8, 100,2,2,{0,1,2,3,4,5,6,7,8,9},MENU_INT,0,0,0},
 { "MOUSE Y SENSITIVITY ", {"1", "2","3","4","5","6","7","8","9",""},
   8, 200,2,2,{0,1,2,3,4,5,6,7,8,9},MENU_INT,0,0,0},
 { "MOUSE X INVERT", {"NO", "YES","?","?","?","","","","",""},
   1, 300,0,0,{0,1,0,0,0,0,0,0,0,0},MENU_INT,0,0,0},
 { "MOUSE Y INVERT", {"NO", "YES","?","?","?","","","","",""},
   1, 400,0,0,{0,1,0,0,0,0,0,0,0,0},MENU_INT,0,0,0},
 { "NONE", {"NONE", " ", " ", " "}, 0,0, 0,0,{0,0,0,0,0,0,0,0,0,0},0,0,0,0}
};

MENUS* curr_menu;

int get_menu_status()
{
	return show_menu;
}

void set_menu_status(int on)
{
	show_menu = on;
}

int get_menu_level()
{
	return menulevel;
}

void set_menu_level_top()
{
	menulevel = 100;
	menuitem = 0;
	sublevel = 0;
	key_set_flag = 0;
}

void do_the_menu()
{
	switch (menulevel)
	{
	case ROOTMENU:   do_root_menu(); break;
	case GLOBALKEYS: do_keyboard_menu(GLOBAL_INPUT); break;
	case LOCALKEYS:  do_gamekey_menu(LOCAL_INPUT); break;
	case GLOBALJOY:  do_joystick_menu(GLOBAL_INPUT); break;
	case LOCALJOY:   do_gamejoy_menu(LOCAL_INPUT); break;
	case ANALOGMENU: do_mouse_menu(); break;
	case DIPMENU:    do_dipswitch_menu(); break;
	case VIDEOMENU:  do_video_menu(); break;
	case AUDIOMENU:  do_sound_menu(); break;
	default: do_root_menu();
	}
}

void do_root_menu()
{
	int x;
	unsigned int C;
	int top = 575;
	int LFT = 250;
	int SPC = 50;
	num_this_menu = 7;

	// if (menuitem > num_this_menu) menuitem =  num_this_menu;
	// if (menuitem < 1) menuitem = 1;
	//draw_a_quad( 280,742,634, 175, 20, 20, 80, 220, 1);

	for (x = 0; x < num_this_menu + 1; x++)
	{
		if (menuitem == x) { C = RGB_PINK; }
		else { C = RGB_WHITE; }

		switch (x)
		{
		case 0: fprint(LFT, top - (SPC * x), C, 2.6, "KEY CONFIG (GLOBAL)"); break;
		case 1: fprint(LFT, top - (SPC * x), C, 2.6, "KEY CONFIG (THIS GAME)"); break;
		case 2: fprint(LFT, top - (SPC * x), C, 2.6, "JOY CONFIG (GLOBAL)"); break;
		case 3: fprint(LFT, top - (SPC * x), C, 2.6, "JOY CONFIG (THIS GAME)"); break;
		case 4: fprint(LFT, top - (SPC * x), C, 2.6, "ANALOG CONFIG"); break;
		case 5: fprint(LFT, top - (SPC * x), C, 2.6, "DIPSWTCHES"); break;
		case 6: fprint(LFT, top - (SPC * x), C, 2.6, "VIDEO SETUP"); break;
		case 7: fprint(LFT, top - (SPC * x), C, 2.6, "SOUND SETUP"); break;
		}
		// draw_center_tex(&menu_tex[x], 32, 335,580-(74*x), 0, NORMAL, RGB_WHITE, 2);
	}
}

int do_joystick_menu(int type)
{
	const char* menu_item[400];
	const char* menu_subitem[400];
	struct ipd* entry[400];
	char flag[400];
	int i, x;
	struct ipd* in;
	int total;
	extern struct ipd inputport_defaults[];

	//int newkey=0;
	int color = 255;
	int spacing = 21;
	int start = 600;
	int top = 0;
	int bottom = 0;
	int shift = 0;
	int b = 0;
	// int newjoy;
	int joyindex;

	if (Machine->input_ports == 0)
		return 0;
	//if (driver[gamenum].input_ports == 0)
	//	return 0;
	in = inputport_defaults;

	total = 0;
	while (in->type != IPT_END)
	{
		if (in->name != 0 && in->joystick != IP_JOY_NONE && (in->type & IPF_UNUSED) == 0
			&& !(!options.cheat && (in->type & IPF_CHEAT)))
		{
			entry[total] = in;
			menu_item[total] = in->name;
			total++;
		}

		in++;
	}

	if (total == 0) return 0;

	//menu_item[total] = "Return to Main Menu";
	menu_item[total] = 0;	/* terminate array */
	//total++;

	for (i = 0; i < total; i++)
	{
		if (i < total)
			menu_subitem[i] = osd_joy_name(entry[i]->joystick);
		else menu_subitem[i] = 0;	/* no subitem */
		flag[i] = 0;
	}

	num_this_menu = (total - 1);

	fprint(285, 700, RGB_WHITE, 1.9, "JOY CONFIG -Global");

	top = menuitem - 8;   if (top < 0) { b = abs(top); top = 0; }
	else b = 0;
	bottom = menuitem + 8; 	if (bottom > num_this_menu) bottom = num_this_menu;
	if (menuitem > num_this_menu) menuitem = num_this_menu;

	for (x = 0; x < total; x++)
	{
		if (top > bottom - 16) top = bottom - 16;
		if (x >= top && x <= bottom + b) {
			if (menuitem == x)
			{
				color = 0;
				fprint(145, start, MAKE_RGB(255, color, 255), 2.5, menu_item[menuitem]);
			}
			else
			{
				color = 255;
				fprint(145, start, MAKE_RGB(255, color, 255), 2.0, "%s", entry[x]->name);
			}
			fprint(545, start, MAKE_RGB(255, color, 255), 2.0, menu_subitem[x]);
			start -= 8 * 2.5;
		}
	}

	if (sublevel == 1) {
		if (osd_key_pressed_memory(OSD_KEY_FAST_EXIT) || osd_key_pressed_memory(OSD_KEY_CANCEL))
		{
			sublevel = 0;
			return 0;
		}
		if (osd_key_pressed_memory(OSD_KEY_A))
		{
			entry[menuitem]->joystick = OSD_JOY_FIRE;
			sublevel = 0;
		}
		if (osd_key_pressed_memory(OSD_KEY_B))
		{
			entry[menuitem]->joystick = OSD_JOY2_FIRE;
			sublevel = 0;
		}
		/* Clears entry "None" */
		if (osd_key_pressed_memory(OSD_KEY_N))
		{
			sublevel = 0;
			entry[menuitem]->joystick = 0;
		}

		fprint(460, 665, MAKE_RGB(255, 200, 30), 1.8, "Press a Button or N to clear");
		for (joyindex = 1; joyindex < OSD_MAX_JOY; joyindex++)
		{
			if (osd_joy_pressed(joyindex))
			{
				sublevel = 0;
				entry[menuitem]->joystick = joyindex;
				break;
			}
		}
	}
	return 0;
}

int do_gamejoy_menu(int type)
{
	const char* menu_item[400];
	const char* menu_subitem[400];
	struct InputPort* entry[40];
	char flag[400];
	int i, x;
	struct InputPort* in;
	int total;

	int color = 255;
	int spacing = 21;
	int start = 600;
	int top = 0;
	int bottom = 0;
	int shift = 0;
	int b = 0;
	int joyindex;

	if (Machine->input_ports == 0)
		//if (driver[gamenum].input_ports == 0)
		return 0;

	in = Machine->input_ports;
	//in = driver[gamenum].input_ports;

	total = 0;
	while (in->type != IPT_END)
	{
		if (input_port_name(in) != 0 && input_port_joy(in) != IP_JOY_NONE)
		{
			entry[total] = in;
			menu_item[total] = input_port_name(in);

			total++;
		}

		in++;
	}

	if (total == 0) return 0;

	//menu_item[total] = "Return to Main Menu";
	menu_item[total] = 0;// +1] = 0;	/* terminate array */
	//total++;

	for (i = 0; i < total; i++)
	{
		if (i < total)
		{
			menu_subitem[i] = osd_joy_name(input_port_joy(entry[i]));
			//if (entry[i]->joystick != IP_JOY_default)
			//	flag[i] = 1;
			//else
			//	flag[i] = 0;
		}
		else menu_subitem[i] = 0;	// no subitem
	}
	num_this_menu = (total - 1);

	fprint(285, 700, RGB_WHITE, 1.9, "JOY CONFIG - This Game");

	top = menuitem - 8;   if (top < 0) { b = abs(top); top = 0; }
	else b = 0;
	bottom = menuitem + 8; 	if (bottom > num_this_menu) bottom = num_this_menu;
	if (menuitem > num_this_menu) menuitem = num_this_menu;

	for (x = 0; x < total; x++)
	{
		if (top > bottom - 16) top = bottom - 16;
		if (x >= top && x <= bottom + b) {
			if (menuitem == x)
			{
				color = 0;
				fprint(145, start, MAKE_RGB(255, color, 255), 2.0, menu_item[menuitem]);
			}
			else
			{
				color = 255;
				fprint(145, start, MAKE_RGB(255, color, 255), 2.0, menu_item[x]);
			}
			fprint(545, start, MAKE_RGB(255, color, 255), 2.0, menu_subitem[x]);
			start -= 8 * 2.5;
		}
	}

	if (sublevel == 1) {
		if (osd_key_pressed_memory(OSD_KEY_FAST_EXIT) || osd_key_pressed_memory(OSD_KEY_CANCEL))
		{
			sublevel = 0;
			return 0;
		}
		if (osd_key_pressed_memory(OSD_KEY_A))
		{
			entry[menuitem]->joystick = OSD_JOY_FIRE;
			sublevel = 0;
		}
		if (osd_key_pressed_memory(OSD_KEY_B))
		{
			entry[menuitem]->joystick = OSD_JOY2_FIRE;
			sublevel = 0;
		}
		/* Clears entry "None" */
		if (osd_key_pressed_memory(OSD_KEY_N))
		{
			sublevel = 0;
			entry[menuitem]->joystick = 0;
		}

		fprint(460, 665, MAKE_RGB(255, 200, 30), 1.8, "Press a Button or N to clear");
		for (joyindex = 1; joyindex < OSD_MAX_JOY; joyindex++)
		{
			if (osd_joy_pressed(joyindex))
			{
				sublevel = 0;
				entry[menuitem]->joystick = joyindex;
				break;
			}
		}
	}
	return 0;
}

void do_keyboard_menu(int type)
{
	const char* menu_item[400];
	struct ipd* entry[400];
	int x = 0;
	int newkey = 0;
	int color = 255;
	int spacing = 21;
	int start = 600;
	int top = 0;
	int bottom = 0;
	int shift = 0;
	struct ipd* in;
	int total;
	static int page = 0;
	int b = 0;

	extern struct ipd inputport_defaults[];

	in = inputport_defaults;

	total = 0;
	while (in->type != IPT_END)
	{
		if (in->name != 0 && in->keyboard != IP_KEY_NONE && (in->type & IPF_UNUSED) == 0
			&& !(!options.cheat && (in->type & IPF_CHEAT)))
		{
			entry[total] = in;
			menu_item[total] = in->name;
			total++;
		}
		in++;
	}

	num_this_menu = (total - 1);

	fprint(145, 650, RGB_WHITE, 2.0, "Input Configuration (Global)");

	top = menuitem - 8;   if (top < 0) { b = abs(top); top = 0; }
	else b = 0;
	bottom = menuitem + 8; 	if (bottom > num_this_menu) bottom = num_this_menu;
	if (menuitem > num_this_menu) menuitem = num_this_menu;

	for (x = 0; x < total; x++)
	{
		if (top > bottom - 16) top = bottom - 16;
		if (x >= top && x <= bottom + b) {
			if (menuitem == x)
			{
				color = 0;
				fprint(145, start, MAKE_RGB(255, color, 255), 2.0, menu_item[menuitem]);
			}
			else
			{
				color = 255;
				fprint(145, start, MAKE_RGB(255, color, 255), 2.0, "%s", entry[x]->name);
			}

			fprint(545, start, MAKE_RGB(255, color, 255), 2.0, key_names[entry[x]->keyboard]);
			start -= 8 * 2.5;
		}
	}

	if (sublevel == 1) {
		newkey = change_key();
		if (newkey != OSD_KEY_NONE)
		{
			entry[menuitem]->keyboard = newkey;
			newkey = 0;
		}
	}
}

void do_gamekey_menu(int type)
{
	const char* menu_item[400];
	const char* menu_subitem[400];
	struct InputPort* entry[400];
	int x = 0;
	int newkey = 0;
	int color = 255;
	int spacing = 21;
	int start = 600;
	int top = 0;
	int bottom = 0;
	int shift = 0;
	struct InputPort* in;
	int total;
	int b = 0;
	int i;

	in = Machine->input_ports;
	//in = driver[gamenum].input_ports;
	total = 0;
	while (in->type != IPT_END)
	{
		if (input_port_name(in) != 0 && input_port_key(in) != IP_KEY_NONE)
		{
			entry[total] = in;
			menu_item[total] = input_port_name(in);
			total++;
		}

		in++;
	}
	if (total == 0) return;

	num_this_menu = (total - 1);

	for (i = 0; i < total; i++)
	{
		if (i < total + 1)        //osd_key_name(input_port_key(entry[i]));
		{
			menu_subitem[i] = key_names[input_port_key(entry[i])];
			/* If the key isn't the default, flag it */
			//if (entry[i]->keyboard != IP_KEY_default)
			//	flag[i] = 1;
			//else
			//	flag[i] = 0;
		}
		else menu_subitem[i] = 0;	/* no subitem */
	}

	fprint(145, 650, RGB_WHITE, 2.0, "Input Configuration (This Game)");// %d", num_this_menu);

	if (menuitem > num_this_menu) menuitem = num_this_menu;

	for (x = 0; x < total; x++)
	{
		if (menuitem == x)
		{
			color = 0;
			fprint(145, start, MAKE_RGB(255, color, 255), 2.0, menu_item[menuitem]);
		}
		else
		{
			color = 255;
			fprint(145, start, MAKE_RGB(255, color, 255), 2.0, menu_item[x]);
		}

		fprint(545, start, MAKE_RGB(255, color, 255), 2.0, "%s", menu_subitem[x]);//key_names[entry[x]->keyboard]);
		start -= 8 * 2.5;
	}
	if (sublevel == 1)
	{
		newkey = change_key();
		if (newkey != OSD_KEY_NONE)
		{
			if (osd_key_invalid(newkey))	/* pseudo key code? */
				newkey = IP_KEY_DEFAULT;

			entry[menuitem]->keyboard = newkey;
			newkey = 0;
		}
	}
}

void do_dipswitch_menu()
{
	const char* menu_item[40];
	const char* menu_subitem[40];
	struct InputPort* entry[40];
	char flag[40];
	int i, sel, b, x;
	struct InputPort* in;
	int total;
	int arrowize;
	int color = 255;
	int spacing = 21;
	int start = 600;
	int top = 0;
	int bottom = 0;
	int shift = 0;

	sel = menuitem;//selected - 1;
	in = Machine->input_ports;
	//in = driver[gamenum].input_ports;

	total = 0;
	while (in->type != IPT_END)
	{
		if ((in->type & ~IPF_MASK) == IPT_DIPSWITCH_NAME && input_port_name(in) != 0 &&
			(in->type & IPF_UNUSED) == 0 &&
			!(!options.cheat && (in->type & IPF_CHEAT)))
		{
			entry[total] = in;
			menu_item[total] = input_port_name(in);

			total++;
		}

		in++;
	}
	if (total == 0) return; //No Dipswitches!
	menu_item[total] = "Return to Main Menu";
	menu_item[total + 1] = 0;	/* terminate array */
	total++;

	for (i = 0; i < total; i++)
	{
		flag[i] = 0; /* TODO: flag the dip if it's not the real default */
		if (i < total - 1)
		{
			in = entry[i] + 1;
			while ((in->type & ~IPF_MASK) == IPT_DIPSWITCH_SETTING &&
				in->default_value != entry[i]->default_value)
				in++;

			if ((in->type & ~IPF_MASK) != IPT_DIPSWITCH_SETTING)
				menu_subitem[i] = "INVALID";
			else menu_subitem[i] = input_port_name(in);
		}
		else menu_subitem[i] = 0;	/* no subitem */
	}

	arrowize = 0;

	fprint(285, 700, RGB_WHITE, 1.9, "DIPSWITCH MENU");

	if (sel < total - 1)
	{
		in = entry[sel] + 1;
		while ((in->type & ~IPF_MASK) == IPT_DIPSWITCH_SETTING &&
			in->default_value != entry[sel]->default_value)
			in++;

		if ((in->type & ~IPF_MASK) != IPT_DIPSWITCH_SETTING)
			// invalid setting: revert to a valid one
			arrowize |= 1;
		else
		{
			if (((in - 1)->type & ~IPF_MASK) == IPT_DIPSWITCH_SETTING &&
				!(!options.cheat && ((in - 1)->type & IPF_CHEAT)))
				arrowize |= 1;
		}
	}
	if (sel < total - 1)
	{
		in = entry[sel] + 1;
		while ((in->type & ~IPF_MASK) == IPT_DIPSWITCH_SETTING &&
			in->default_value != entry[sel]->default_value)
			in++;

		if ((in->type & ~IPF_MASK) != IPT_DIPSWITCH_SETTING)
			// invalid setting: revert to a valid one
			arrowize |= 2;
		else
		{
			if (((in + 1)->type & ~IPF_MASK) == IPT_DIPSWITCH_SETTING &&
				!(!options.cheat && ((in + 1)->type & IPF_CHEAT)))
				arrowize |= 2;
		}
	}

	for (x = 0; x < total; x++)
	{
		if (menuitem == x)
		{
			color = 0;
			fprint(145, start, MAKE_RGB(255, color, 255), 2.0, menu_item[menuitem]);
		}
		else
		{
			color = 255;
			fprint(145, start, MAKE_RGB(255, color, 255), 2.0, menu_item[x]);
		}

		fprint(545, start, MAKE_RGB(255, color, 255), 2.0, menu_subitem[x]);

		if (arrowize == 2 && menuitem == x)
		{
			fprint(get_string_len(), start, RGB_WHITE, 2.0, ">"); //740
		}
		if (arrowize == 1 && menuitem == x)
		{
			fprint(520, start, RGB_WHITE, 2.0, "<");
		}

		start -= 8 * 2.5;
	}
	//displaymenu(menu_item,menu_subitem,flag,sel,arrowize);

	if (osd_key_pressed_memory_repeat(OSD_KEY_UI_DOWN, 8))
	{
		if (sel < total - 1) sel++;
		else sel = 0;
		menuitem = sel;
	}

	if (osd_key_pressed_memory_repeat(OSD_KEY_UI_UP, 8))
	{
		if (sel > 0) sel--;
		else sel = total - 1;
		menuitem = sel;
	}

	if (osd_key_pressed_memory_repeat(OSD_KEY_UI_RIGHT, 8))
	{
		if (sel < total - 1)
		{
			in = entry[sel] + 1;
			while ((in->type & ~IPF_MASK) == IPT_DIPSWITCH_SETTING &&
				in->default_value != entry[sel]->default_value)
				in++;

			if ((in->type & ~IPF_MASK) != IPT_DIPSWITCH_SETTING)
				// invalid setting: revert to a valid one
				entry[sel]->default_value = (entry[sel] + 1)->default_value & entry[sel]->mask;
			else
			{
				if (((in + 1)->type & ~IPF_MASK) == IPT_DIPSWITCH_SETTING &&
					!(!options.cheat && ((in + 1)->type & IPF_CHEAT)))
					entry[sel]->default_value = (in + 1)->default_value & entry[sel]->mask;
			}
		}
	}

	if (osd_key_pressed_memory_repeat(OSD_KEY_UI_LEFT, 8))
	{
		if (sel < total - 1)
		{
			in = entry[sel] + 1;
			while ((in->type & ~IPF_MASK) == IPT_DIPSWITCH_SETTING &&
				in->default_value != entry[sel]->default_value)
				in++;

			if ((in->type & ~IPF_MASK) != IPT_DIPSWITCH_SETTING)
				// invalid setting: revert to a valid one
				entry[sel]->default_value = (entry[sel] + 1)->default_value & entry[sel]->mask;
			else
			{
				if (((in - 1)->type & ~IPF_MASK) == IPT_DIPSWITCH_SETTING &&
					!(!options.cheat && ((in - 1)->type & IPF_CHEAT)))
					entry[sel]->default_value = (in - 1)->default_value & entry[sel]->mask;
			}
		}
	}

	if (osd_key_pressed_memory(OSD_KEY_UI_SELECT))
	{
		if (sel == total - 1) sel = -1;
		menulevel = 100; menuitem = 0;
	}

	if (osd_key_pressed_memory(OSD_KEY_FAST_EXIT) || osd_key_pressed_memory(OSD_KEY_CANCEL))
		sel = -1;

	if (osd_key_pressed_memory(OSD_KEY_CONFIGURE))
		sel = -2;

	if (sel == -1 || sel == -2)
	{
	}
}

void do_sound_menu()
{
	int yval = 600;
	int color = 0;
	it = 0;

	if (number == 0)
	{
		while (soundmenu[number].NumOptions)
		{
			number++;
		}
		num_this_menu = number - 1;
		currentval = soundmenu[it].current;
	}

	//  draw_a_quad( 270,752,620, 275, 20, 20, 80, 220, 1);
	if (gamenum == 0)
	{
		fprint(310, 700, RGB_WHITE, 2.2, "Sound Settings - Global");
	}
	else
	{
		fprint(310, 700, RGB_WHITE, 2.2, "Sound Settings - This Game");
	}
	val_low = 0;
	do
	{
		if (menuitem == it) { color = 0; }
		else { color = 255; }

		if (soundmenu[it].menu_type == MENU_INT)
		{
			fprint(640, yval, MAKE_RGB(255, color, 255), 2.0, "%s", soundmenu[it].options[soundmenu[it].current]);
		}
		else if (soundmenu[it].menu_type == MENU_FLOAT)
		{
			fprint(640, yval, MAKE_RGB(255, color, 255), 2.0, "%2.0f%%", soundmenu[it].step * soundmenu[it].current);
		}
		fprint(300, yval, MAKE_RGB(255, color, 255), 2.0, soundmenu[it].heading);
		it++;
		yval -= 35; //35
	} while (soundmenu[it].NumOptions);
}

void do_video_menu()
{
	int yval = 700;
	int color = 0;
	it = 0;
	int x = 0;

	if (number == 0)
	{
		while (glmenu[number].NumOptions) { number++; }
		number--;
		num_this_menu = number;
		currentval = glmenu[it].current;
	}
	// draw_a_quad( 230,795,715, 36, 20, 20, 80, 220, 1);
	//menu_textureit(&menu_tex[6],650,555, 16, 40);
	if (gamenum == 0)
	{
		fprint(330, yval + 30, RGB_WHITE, 2.0, "GL Settings - Global");
	}
	else {
		fprint(300, yval + 30, RGB_WHITE, 1.3, "GL Settings - This Game");
	}
	val_low = 0;

	for (x = 0; x < num_this_menu + 1; x++)
	{
		if (menuitem == it) { color = 0; }
		else { color = 255; }

		if (glmenu[(it)].menu_type == MENU_INT)
		{
			fprint(550, yval, MAKE_RGB(255, color, 255), 2.0, "%s", glmenu[(it)].options[glmenu[(it)].current]);
		}
		else if (glmenu[(it)].menu_type == MENU_FLOAT)
		{
			fprint(550, yval, MAKE_RGB(255, color, 255), 2.0, "%2.1f", glmenu[(it)].step * glmenu[(it)].current);
		}

		fprint(245, yval, MAKE_RGB(255, color, 255), 2.0, glmenu[(it)].heading);

		it++;
		yval -= 28; //35
	}
}

void change_menu_level(int dir) //This is up and down
{
	int level = 0;

	if (dir)
	{
		if (menulevel == VIDEOMENU) { if ((glmenu[menuitem - 1].current) != currentval) { glmenu[menuitem - 1].current = currentval; } }
		if (menulevel == AUDIOMENU) { if ((soundmenu[menuitem - 1].current) != currentval) { soundmenu[menuitem - 1].current = currentval; } }

		if (menuitem < (num_this_menu)) { menuitem++; } //change dir here

		if (menulevel == VIDEOMENU) { currentval = glmenu[menuitem - 1].current; }
		if (menulevel == AUDIOMENU) { currentval = soundmenu[menuitem - 1].current; }
	}

	else
	{
		if (menulevel == VIDEOMENU) { if ((glmenu[menuitem - 1].current) != currentval) { glmenu[menuitem - 1].current = currentval; } }
		if (menulevel == AUDIOMENU) { if ((soundmenu[menuitem - 1].current) != currentval) { soundmenu[menuitem - 1].current = currentval; } }

		if (menuitem > 0) { menuitem--; } //change dir here

		if (menulevel == VIDEOMENU) { currentval = glmenu[menuitem - 1].current; }
		if (menulevel == AUDIOMENU) { currentval = soundmenu[menuitem - 1].current; }
	}
}
void change_menu_item(int dir) //This is right and left
{
	if (menulevel == DIPMENU) return;

	if (dir)
	{
		currentval++; if (currentval > val_high) currentval = val_low;
		if (menulevel == AUDIOMENU) { check_sound_menu(); }
		if (menulevel == VIDEOMENU) { check_video_menu(); }
	}
	else
	{
		currentval--; if (currentval < val_low) currentval = val_high;
		if (menulevel == AUDIOMENU) { check_sound_menu(); }
		if (menulevel == VIDEOMENU) { check_video_menu(); }
	}
}

void select_menu_item() //This is enter
{
	switch (menulevel)
	{
	case ROOTMENU: {menulevel = menulevel * (menuitem + 2);
		// if (menulevel == DIPMENU && gamenum==0){menulevel=ROOTMENU;} //GUI has no dips
		menuitem = 0;
		break; }

				 //For changing levels
	case GLOBALKEYS: {sublevel = 1; break; }
	case LOCALKEYS: {sublevel = 1; break; }
	case GLOBALJOY: {sublevel = 1; break; }//do_joystick_menu(1);break
	case LOCALJOY: {sublevel = 1; break; };
	case ANALOGMENU: do_mouse_menu(); break;
	case DIPMENU: { do_dipswitch_menu(); break; }
				// case 600: do_video_menu();break;
	}
}

void change_menu()
{
	//This is menu exit;

	   //Tweak for dipswitch to make sure to set selected item on exit
	if (menulevel == AUDIOMENU) { if ((soundmenu[menuitem - 1].current) != currentval) { soundmenu[menuitem - 1].current = currentval; } number = 0; save_sound_menu(); }
	if (menulevel == VIDEOMENU) { if ((glmenu[menuitem - 1].current) != currentval) { glmenu[menuitem - 1].current = currentval; } number = 0; save_video_menu(); }
	if (menulevel == DIPMENU) { number = 0; }
	if (menulevel == ANALOGMENU) { number = 0; } //if ((mousemenu[menuitem - 1].current) != currentval){ mousemenu[menuitem - 1].current = currentval; } number = 0; save_mouse_menu();}

	if (menulevel == GLOBALKEYS) { number = 0; }//save_keys();}
	if (menulevel > ROOTMENU) { menulevel = 100; menuitem = 1; number = 0; }
}

void reset_menu()
{
	//This is for setting defaults                  //Below Resets selected item as well.
	//if (menulevel == DIPMENU) {currentval=dips[(menuitem-1)].default;}//reset_to_default_dips();
	if (menulevel == GLOBALKEYS) { ; }//set_default_keys();}
}

int change_key()
{
	int k = 0;
	key_set_flag = 1;
	fprint(460, 665, MAKE_RGB(255, 200, 30), 1.8, "Press a Key");
	// set_aae_leds(0); //Clear kb inputs
	k = osd_read_key_immediate();

	if (k == KEY_PAUSE || k == KEY_ENTER) { k = 0; } //Make sure to trap the entering of the key
	if (k) {
		sublevel = 0;
		key[k] = 0;
		clear_keybuf();
		key_set_flag = 0;
	}
	return k;
}

int do_mouse_menu()
{
	const char* menu_item[40];
	const char* menu_subitem[40];
	struct InputPort* entry[40];
	int i, sel, b, x;
	struct InputPort* in;
	int total, total2;
	int arrowize;
	int yval = 500;
	int color = 0;
	int start = 600;
	int top = 0;
	int bottom = 0;
	int shift = 0;

	sel = menuitem;//selected - 1;

	if (Machine->input_ports == 0)
		//if (driver[gamenum].input_ports == 0)
		return 0;

	in = Machine->input_ports;
	//in = driver[gamenum].input_ports;

	/* Count the total number of analog controls */
	total = 0;
	while (in->type != IPT_END)
	{
		if (((in->type & 0xff) > IPT_ANALOG_START) && ((in->type & 0xff) < IPT_ANALOG_END)
			&& !(!options.cheat && (in->type & IPF_CHEAT)))
		{
			entry[total] = in;
			total++;
		}
		in++;
	}

	if (total == 0) return 0;

	/* Each analog control has 3 entries - key & joy delta, reverse, sensitivity */

#define ENTRIES 3

	total2 = total * ENTRIES;

	//	menu_item[total2] = "Return to Main Menu";
	//	menu_item[total2 + 1] = 0;	/* terminate array */
	total2++;

	arrowize = 0;
	for (i = 0; i < total2; i++)
	{
		if (i < total2 - 1)
		{
			char label[30][40];
			char setting[30][40];
			int sensitivity, delta;
			int reverse;

			strcpy(label[i], input_port_name(entry[i / ENTRIES]));
			sensitivity = IP_GET_SENSITIVITY(entry[i / ENTRIES]);
			delta = IP_GET_DELTA(entry[i / ENTRIES]);
			reverse = (entry[i / ENTRIES]->type & IPF_REVERSE);

			switch (i % ENTRIES)
			{
			case 0:
				strcat(label[i], " Key/Joy Speed");
				sprintf(setting[i], "%d", delta);
				if (i == sel) arrowize = 3;
				break;
			case 1:
				strcat(label[i], " Reverse");
				if (reverse)
					sprintf(setting[i], "On");
				else
					sprintf(setting[i], "Off");
				if (i == sel) arrowize = 3;
				break;
			case 2:
				strcat(label[i], " Sensitivity");
				sprintf(setting[i], "%3d%%", sensitivity);
				if (i == sel) arrowize = 3;
				break;
			}

			menu_item[i] = label[i];
			menu_subitem[i] = setting[i];
			//wrlog("Menu subitem %s", setting[i]);

			in++;
		}
		else menu_subitem[i] = 0;	/* no subitem */
	}

	fprint(285, 700, RGB_WHITE, 2.0, "ANALOG SETTINGS");
	menu_item[total] = "Return to Main Menu";
	menu_subitem[total] = nullptr;
	menu_item[total + 1] = 0;	/* terminate array */
	total++;

	for (x = 0; x < total; x++)
	{
		if (menuitem == x)
		{
			color = 0;
			fprint(145, start, MAKE_RGBA(255, color, 255, 255), 2.0, menu_item[menuitem]);
		}
		else
		{
			color = 255;
			fprint(145, start, MAKE_RGBA(255, color, 255, 255), 2.0, menu_item[x]);
		}

		fprint(545, start, MAKE_RGBA(255, color, 255, 255), 2.0, menu_subitem[x]);

		start -= 8 * 2.5;
	}

	if (menuitem > total - 1) menuitem = total - 1;

	if (osd_key_pressed_memory_repeat(OSD_KEY_UI_DOWN, 8))
	{
		if (sel < total - 1) sel++;
		else sel = 0;
		menuitem = sel;
	}

	if (osd_key_pressed_memory_repeat(OSD_KEY_UI_UP, 8))
	{
		if (sel > 0) sel--;
		else sel = total - 1;
		menuitem = sel;
	}

	if (osd_key_pressed_memory_repeat(OSD_KEY_UI_LEFT, 8))
	{
		if ((sel % ENTRIES) == 0)
			/* keyboard/joystick delta */
		{
			int val = IP_GET_DELTA(entry[sel / ENTRIES]);

			val--;
			if (val < 1) val = 1;
			IP_SET_DELTA(entry[sel / ENTRIES], val);
		}
		else if ((sel % ENTRIES) == 1)
			/* reverse */
		{
			int reverse = entry[sel / ENTRIES]->type & IPF_REVERSE;
			if (reverse)
				reverse = 0;
			else
				reverse = IPF_REVERSE;
			entry[sel / ENTRIES]->type &= ~IPF_REVERSE;
			entry[sel / ENTRIES]->type |= reverse;
		}
		else if ((sel % ENTRIES) == 2)
			/* sensitivity */
		{
			int val = IP_GET_SENSITIVITY(entry[sel / ENTRIES]);

			val--;
			if (val < 1) val = 1;
			IP_SET_SENSITIVITY(entry[sel / ENTRIES], val);
		}
	}

	if (osd_key_pressed_memory_repeat(OSD_KEY_UI_RIGHT, 8))
	{
		if ((sel % ENTRIES) == 0)
			/* keyboard/joystick delta */
		{
			int val = IP_GET_DELTA(entry[sel / ENTRIES]);

			val++;
			if (val > 255) val = 255;
			IP_SET_DELTA(entry[sel / ENTRIES], val);
		}
		else if ((sel % ENTRIES) == 1)
			/* reverse */
		{
			int reverse = entry[sel / ENTRIES]->type & IPF_REVERSE;
			if (reverse)
				reverse = 0;
			else
				reverse = IPF_REVERSE;
			entry[sel / ENTRIES]->type &= ~IPF_REVERSE;
			entry[sel / ENTRIES]->type |= reverse;
		}
		else if ((sel % ENTRIES) == 2)
			/* sensitivity */
		{
			int val = IP_GET_SENSITIVITY(entry[sel / ENTRIES]);

			val++;
			if (val > 255) val = 255;
			IP_SET_SENSITIVITY(entry[sel / ENTRIES], val);
		}
	}

	if (osd_key_pressed_memory(OSD_KEY_UI_SELECT))
	{
		if (sel == total - 1) sel = -1;
		menulevel = 100; menuitem = 0;
	}

	if (osd_key_pressed_memory(OSD_KEY_FAST_EXIT) || osd_key_pressed_memory(OSD_KEY_CANCEL))
		sel = -1;

	if (osd_key_pressed_memory(OSD_KEY_CONFIGURE))
		sel = -2;

	if (sel == -1 || sel == -2)
	{
	}

	return sel + 1;

	//	it=1;
}

void check_sound_menu()
{
	/*
	if ((menuitem-1)==0){config.mainvol = (currentval * 12.75);set_volume((int)(currentval * 12.75),0); play_sample(game_sounds[num_samples-5],currentval,128,1000,0);} //Main Vol
	if ((menuitem-1)==1){ config.pokeyvol =  (currentval * 12.75);play_sample(game_sounds[num_samples-5],currentval,128,1000,0);} //Pokey Vol
	if ((menuitem-1)==2){ config.noisevol = (currentval * 12.75);play_sample(game_sounds[num_samples-5],currentval,128,1000,0);}//Noise Vol
	//if ((menuitem-1)==3){ config.hvnoise = currentval; setup_ambient(VECTOR);}
	//if ((menuitem-1)==4){ config.psnoise = currentval; setup_ambient(VECTOR);}
	//if ((menuitem-1)==5){ config.pshiss = currentval; setup_ambient(VECTOR);}
	*/
}

void setup_sound_menu()
{
	/*
	int x=0;

   soundmenu[0].current=ceilf(config.mainvol);
   soundmenu[1].current=ceilf(config.pokeyvol);
   soundmenu[2].current=ceilf(config.noisevol);
   soundmenu[3].current=config.hvnoise;
   soundmenu[4].current=config.psnoise;
   soundmenu[5].current=config.pshiss;
   while (soundmenu[x].NumOptions !=0){soundmenu[x].Changed=soundmenu[x].current;x++;}//SET TO DETECT CHANGED VALUES
*/
}

void save_sound_menu()
{
	/*
  if (soundmenu[0].Changed != soundmenu[0].current) my_set_config_int("main", "mainvol", soundmenu[0].current, gamenum);//soundmenu[0].Value[soundmenu[0].current], gamenum);
  if (soundmenu[1].Changed != soundmenu[1].current) my_set_config_int("main", "pokeyvol",soundmenu[1].current, gamenum);//.Value[soundmenu[1].current], gamenum);
  if (soundmenu[2].Changed != soundmenu[2].current) my_set_config_int("main", "noisevol",soundmenu[2].current, gamenum);//.Value[soundmenu[2].current], gamenum);
  if (soundmenu[3].Changed != soundmenu[3].current) my_set_config_int("main", "hvnoise",  soundmenu[3].current, gamenum);
  if (soundmenu[4].Changed != soundmenu[4].current) my_set_config_int("main", "psnoise" ,soundmenu[4].current, gamenum);
  if (soundmenu[5].Changed != soundmenu[5].current) my_set_config_int("main", "pshiss", soundmenu[5].current, gamenum);
*/
}

void setup_mouse_menu()
{
	int x = 0;

	// mousemenu[0].current=config.mouse1xs;
	// mousemenu[1].current=config.mouse1ys;
   //  mousemenu[2].current=config.mouse1x_invert;
   //  mousemenu[3].current=config.mouse1y_invert;
	while (mousemenu[x].NumOptions != 0) { mousemenu[x].Changed = mousemenu[x].current; x++; }//SET TO DETECT CHANGED VALUES
}

void save_mouse_menu()
{
	/*
	 if (mousemenu[0].Changed != mousemenu[0].current) my_set_config_int("main", "mouse1xs", mousemenu[0].current, gamenum);
	 if (mousemenu[1].Changed != mousemenu[1].current) my_set_config_int("main", "mouse1ys", mousemenu[1].current, gamenum);
	 if (mousemenu[2].Changed != mousemenu[2].current) my_set_config_int("main", "mouse1x_invert", mousemenu[2].current, gamenum);
	 if (mousemenu[3].Changed != mousemenu[3].current) my_set_config_int("main", "mouse1y_invert",  mousemenu[3].current, gamenum);
	 */
}

void check_video_menu()
{
	//if ((menuitem-1)==2){SetGammaRamp(127+(currentval*2),config.bright,config.contrast); }
	//if ((menuitem-1)==3){SetGammaRamp(config.gamma,127+(currentval*2),config.contrast); }
	//if ((menuitem-1)==4){SetGammaRamp(config.gamma,config.bright,127+(currentval*2)); }
	if ((menuitem - 1) == 6) { config.drawzero = currentval; }
	if ((menuitem - 1) == 7) { config.widescreen = currentval; } //Widescreen_calc(); //Recalculate Widescreen value
	if ((menuitem - 1) == 8) { config.vectrail = currentval; }
	if ((menuitem - 1) == 9) { config.vecglow = currentval; }
	if ((menuitem - 1) == 10) { config.linewidth = glmenu[10].step * currentval; }
	if ((menuitem - 1) == 11) { config.pointsize = glmenu[11].step * currentval; }
	if ((menuitem - 1) == 12) { config.gain = currentval; }

	//if ((menuitem-1)==13) {if (art_loaded[0])config.artwork=currentval;}
	//if ((menuitem-1)==14) {if (art_loaded[1])config.overlay=currentval;}
	//if ((menuitem-1)==15) {if (art_loaded[3]) config.bezel=currentval;setup_video_config();}
	//if ((menuitem-1)==16) {config.artcrop=currentval;setup_video_config();} //Reconfigure video size

	if ((menuitem - 1) == 19) { config.kbleds = currentval; }
	//SetGammaRamp(double gamma, double bright, double contrast);*/
}

void setup_video_menu()
{
	int x = 0;

	/*
		if (config.windowed) {glmenu[0].current=1;}else {glmenu[0].current=0;}//WINDOWED

		switch (config.screenw) //RESOLUTION
		 { case 640: glmenu[1].current=0;break;
		   case 800: glmenu[1].current=1;break;
		   case 1024: glmenu[1].current=2;break;
		   case 1152: glmenu[1].current=3;break;
		   case 1280: glmenu[1].current=4;break;
		   case 1600: glmenu[1].current=5;break;
		   default: glmenu[1].current=0;break;
		 }
		glmenu[2].current=(config.gamma-127)/2;
		glmenu[3].current=(config.bright-127)/2;
		glmenu[4].current=(config.contrast-127)/2;
		 if (config.forcesync) {glmenu[5].current=1;}else {glmenu[5].current=0;}//VSYNC

	  */
	  //Draw zero lines
 //	 if (config.drawzero) {glmenu[6].current=1;}else {glmenu[6].current=0;}//VSYNC
  //    if (config.widescreen)  {glmenu[7].current=1;}else {glmenu[7].current=0;}

 /*
		 switch (config.vectrail) //SAMPLE LEVEL
	  { case 0: glmenu[8].current=0;break;
		case 1: glmenu[8].current=1;break;
		case 2: glmenu[8].current=2;break;
		case 3: glmenu[8].current=3;break;
		default: glmenu[8].current=0;break;
	  }

		glmenu[9].current=config.vecglow;
	   // glmenu[10].current=config.m_line;// / .1; //config.m_line;
	   // glmenu[11].current=config.m_point;// / .1;//config.m_point;
		glmenu[12].current=config.gain;//'if (config.monitor) {glmenu[12].current=1;}else {glmenu[12].current=0;}//MONITOR
		if (config.artwork) {glmenu[13].current=1;}else {glmenu[13].current=0;}//ARTWORK
		if (config.overlay) {glmenu[14].current=1;}else {glmenu[14].current=0;}//OVERLAY
		if (config.bezel)   {glmenu[15].current=1;}else {glmenu[15].current=0;}//BEZEL
		if (config.artcrop) {glmenu[16].current=1;}else {glmenu[16].current=0;}//CROP BEZEL

	  switch (config.priority) //POINTSIZE
	  { case 0: glmenu[18].current=0;break;
		case 1: glmenu[18].current=1;break;
		case 2: glmenu[18].current=2;break;
		case 3: glmenu[18].current=3;break;
		case 4: glmenu[18].current=4;break;
		default: glmenu[18].current=2;break;
	  }
   */
   // glmenu[19].current=config.kbleds;

	while (glmenu[x].NumOptions != 0) { glmenu[x].Changed = glmenu[x].current; x++; }//SET TO DETECT CHANGED VALUES
}

void save_video_menu()
{
	int x = 0;
	int y = 0;
	/*
		if (glmenu[0].Changed != glmenu[0].current){ my_set_config_int("main", "windowed",glmenu[0].current, gamenum);}

		  switch (glmenu[1].current) //RESOLUTION
		 {
		   case 0: x=640;y=480;break;
		   case 1: x=800;y=600;break;
		   case 2: x=1024;y=768;break;
		   case 3: x=1152;y=864;break;
		   case 4: x=1280;y=1024;break;
		   case 5: x=1600;y=1200;break;
		   default:x=1024;y=768;break;
		 }
		 if (glmenu[1].Changed != glmenu[1].current){ WriteInteger("main", "screenw", x, gamenum);
														WriteInteger("main", "screenh", y, gamenum);}

		 if (glmenu[2].Changed != glmenu[2].current)   WriteInteger("main", "gamma", (glmenu[2].current*2)+127, gamenum);
		 if (glmenu[3].Changed != glmenu[3].current)   WriteInteger("main", "bright", (glmenu[3].current*2)+127, gamenum);
		 if (glmenu[4].Changed != glmenu[4].current)   WriteInteger("main", "contrast", (glmenu[4].current*2)+127, gamenum);
		 if (glmenu[5].Changed != glmenu[5].current)   WriteInteger("main", "force_vsync",glmenu[5].current, gamenum);
		 if (glmenu[6].Changed != glmenu[6].current)   WriteInteger("main", "drawzero", glmenu[6].Value[glmenu[6].current], gamenum);
		 if (glmenu[7].Changed != glmenu[7].current)   WriteInteger("main", "widescreen", glmenu[7].Value[glmenu[7].current], gamenum);
		 if (glmenu[8].Changed != glmenu[8].current)   WriteInteger("main", "vectortrail", glmenu[8].Value[glmenu[8].current], gamenum);
		 if (glmenu[9].Changed != glmenu[9].current)   WriteInteger("main", "vectorglow", glmenu[9].current, gamenum);
		 if (glmenu[10].Changed != glmenu[10].current) WriteInteger("main", "m_line",glmenu[10].current, gamenum);
		 if (glmenu[11].Changed != glmenu[11].current) WriteInteger("main", "m_point",glmenu[11].current, gamenum);
		 if (glmenu[12].Changed != glmenu[12].current) WriteInteger("main", "gain",glmenu[12].current, gamenum);
		 if (glmenu[13].Changed != glmenu[13].current) WriteInteger("main", "artwork",glmenu[13].current, gamenum);
		 if (glmenu[14].Changed != glmenu[14].current) WriteInteger("main", "overlay",glmenu[14].current, gamenum);
		 if (glmenu[15].Changed != glmenu[15].current) WriteInteger("main", "bezel",glmenu[15].current, gamenum);
		 if (glmenu[16].Changed != glmenu[16].current) WriteInteger("main", "artcrop",glmenu[16].current, gamenum);
		 if (glmenu[17].Changed != glmenu[17].current) WriteInteger("main", "screenburn",glmenu[17].current, gamenum);
		 if (glmenu[18].Changed != glmenu[18].current) WriteInteger("main", "priority",glmenu[18].current, gamenum);
		 if (glmenu[19].Changed != glmenu[19].current) WriteInteger("main", "kbleds",glmenu[19].current, gamenum);
	*/
}

void set_points_lines()
{
	config.linewidth = glmenu[10].step * (glmenu[10].current);
	config.pointsize = glmenu[11].step * (glmenu[11].current);

	//Change this to be set in the gl code.
	//glLineWidth(config.linewidth);//linewidth
	//glPointSize(config.pointsize);//pointsize
}