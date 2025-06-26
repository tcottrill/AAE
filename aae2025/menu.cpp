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
//
//============================================================================

#include "menu.h"
#include "framework.h"
#include "aae_mame_driver.h"
#include "sys_video/glcode.h"
#include "sys_video/vector_fonts.h"
#include "sys_video/gl_texturing.h"
#include "inptport.h"
#include "deftypes.h"
#include "osdepend.h"
#include "os_input.h"
#include "math.h"
#include "config.h"
#include "colordefs.h"
#include <stdio.h>

#pragma warning( disable : 4305 4244 )

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

// For savecall when exiting.
static int last_menu_setting = 0;


// TODO: Add another value here to differentiate settings that should NOT be saved into a "game" ini file like screen size!

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


const char* key_names[256] = {
	"NULL",              // Code 0x0
	"LBUTTON",           // Code 0x1
	"RBUTTON",           // Code 0x2
	"CANCEL",            // Code 0x3
	"MBUTTON",           // Code 0x4
	"XBUTTON1",          // Code 0x5
	"XBUTTON2",          // Code 0x6
	"UNDEF",             // Code 0x7
	"BACKSPACE",         // Code 0x8
	"TAB",               // Code 0x9
	"LF",                // Code 0xA
	"VT",                // Code 0xB
	"CLEAR",             // Code 0xC
	"ENTER",             // Code 0xD
	"UNDEF",             // Code 0xE
	"UNDEF",             // Code 0xF
	"SHIFT",             // Code 0x10
	"CONTROL",           // Code 0x11
	"MENU",              // Code 0x12
	"PAUSE",             // Code 0x13
	"CAPSLOCK",          // Code 0x14
	"KANA",              // Code 0x15
	"IME_ON",            // Code 0x16
	"JUNJA",             // Code 0x17
	"CANCEL",            // Code 0x18
	"KANJI",             // Code 0x19
	"IME_OFF",           // Code 0x1A
	"ESC",               // Code 0x1B
	"CONVERT",           // Code 0x1C
	"NONCONVERT",        // Code 0x1D
	"ACCEPT",            // Code 0x1E
	"MODECHANGE",        // Code 0x1F
	"SPACE",             // Code 0x20
	"PGUP",              // Code 0x21
	"PGDN",              // Code 0x22
	"END",               // Code 0x23
	"HOME",				 // Code 0x24
	"LEFT",              // Code 0x25
	"UP",				 // Code 0x26
	"RIGHT",			 // Code 0x27
	"DOWN",				 // Code 0x28
	"SELECT",            // Code 0x29
	"PRINT",             // Code 0x2A
	"PLUS",              // Code 0x2B
	"PRINTSCRN",         // Code 0x2C
	"INSERT",            // Code 0x2D
	"DEL",				 // Code 0x2E
	"HELP",              // Code 0x2F
	"0",                 // Code 0x30
	"1",                 // Code 0x31
	"2",                 // Code 0x32
	"3",                 // Code 0x33
	"4",                 // Code 0x34
	"5",                 // Code 0x35
	"6",                 // Code 0x36
	"7",                 // Code 0x37
	"8",                 // Code 0x38
	"9",                 // Code 0x39
	"UNDEF",             // Code 0x3A
	"UNDEF",			 // Code 0x3B
	"UNDEF",			 // Code 0x3C
	"UNDEF",             // Code 0x3D
	"UNDEF",			 // Code 0x3E
	"UNDEF",		     // Code 0x3F
	"UNDEF",             // Code 0x40
	"A",                 // Code 0x41
	"B",                 // Code 0x42
	"C",                 // Code 0x43
	"D",                 // Code 0x44
	"E",                 // Code 0x45
	"F",                 // Code 0x46
	"G",                 // Code 0x47
	"H",                 // Code 0x48
	"I",                 // Code 0x49
	"J",                 // Code 0x4A
	"K",                 // Code 0x4B
	"L",                 // Code 0x4C
	"M",                 // Code 0x4D
	"N",                 // Code 0x4E
	"O",                 // Code 0x4F
	"P",                 // Code 0x50
	"Q",                 // Code 0x51
	"R",                 // Code 0x52
	"S",                 // Code 0x53
	"T",                 // Code 0x54
	"U",                 // Code 0x55
	"V",                 // Code 0x56
	"W",                 // Code 0x57
	"X",                 // Code 0x58
	"Y",                 // Code 0x59
	"Z",                 // Code 0x5A
	"LWIN",				 // Code 0x5B
	"RWIN",				 // Code 0x5C
	"APPS",				 // Code 0x5D
	"RES",		         // Code 0x5E
	"SLEEP",             // Code 0x5F
	"0_PAD",             // Code 0x60
	"1_PAD",             // Code 0x61
	"2_PAD",             // Code 0x62
	"3_PAD",             // Code 0x63
	"4_PAD",             // Code 0x64
	"5_PAD",             // Code 0x65
	"6_PAD",             // Code 0x66
	"7_PAD",             // Code 0x67
	"8_PAD",             // Code 0x68
	"9_PAD",             // Code 0x69
	"MULTIPLY",          // Code 0x6A
	"ADD",               // Code 0x6B
	"SEP",               // Code 0x6C
	"SUBTRACT",          // Code 0x6D
	"DECIMAL",           // Code 0x6E
	"DIVIDE",            // Code 0x6F
	"F1",                // Code 0x70
	"F2",                // Code 0x71
	"F3",                // Code 0x72
	"F4",                // Code 0x73
	"F5",                // Code 0x74
	"F6",                // Code 0x75
	"F7",                // Code 0x76
	"F8",                // Code 0x77
	"F9",                // Code 0x78
	"F10",               // Code 0x79
	"F11",               // Code 0x7A
	"F12",				 // Code 0x7B
	"F13",               // Code 0x7C
	"F14",				 // Code 0x7D
	"F15",               // Code 0x7E
	"F16",               // Code 0x7F
	"F17",               // Code 0x80
	"F18",				 // Code 0x81
	"F19",				 // Code 0x82
	"F20",				 // Code 0x83
	"F21",				 // Code 0x84
	"F22",				 // Code 0x85
	"F23",				 // Code 0x86
	"F24",				 // Code 0x87
	"CIRCUMFLEX",        // Code 0x88
	"PERMILLE",          // Code 0x89
	"SCARON",            // Code 0x8A
	"LEFTANGLEQUOTE",    // Code 0x8B
	"OE",                // Code 0x8C
	"UNASSIGNED141",     // Code 0x8D
	"ZCARON",            // Code 0x8E
	"UNASSIGNED143",     // Code 0x8F
	"NUMLOCK",           // Code 0x90
	"SCRLOCK",           // Code 0x91
	"RSQUOTE",           // Code 0x92
	"LDQUOTE",           // Code 0x93
	"RDQUOTE",           // Code 0x94
	"BULLET",            // Code 0x95
	"ENDASH",            // Code 0x96
	"EMDASH",            // Code 0x97
	"SMALLTILDE",        // Code 0x98
	"TRADEMARK",         // Code 0x99
	"SCARON_SMALL",      // Code 0x9A
	"RIGHTANGLEQUOTE",   // Code 0x9B
	"OE_SMALL",          // Code 0x9C
	"UNASSIGNED157",     // Code 0x9D
	"ZCARON_SMALL",      // Code 0x9E
	"YDIAERESIS",        // Code 0x9F
	"LSHIFT",            // Code 0xA0
	"RSHIFT",		     // Code 0xA1
	"LCONTROL",			 // Code 0xA2
	"RCONTROL",          // Code 0xA3
	"LMENU",			 // Code 0xA4
	"RMENU",             // Code 0xA5
	"BBACK",			 // Code 0xA6
	"ALTGR",			 // Code 0xA7
	"DIAERESIS",         // Code 0xA8
	"COPYRIGHT",         // Code 0xA9
	"FEMININEORD",       // Code 0xAA
	"LEFTANGLEQUOTE",    // Code 0xAB
	"NOTSIGN",           // Code 0xAC
	"SOFT_HYPHEN",       // Code 0xAD
	"REGISTERED",        // Code 0xAE
	"MACRON",            // Code 0xAF
	"DEGREE",            // Code 0xB0
	"PLUSMINUS",         // Code 0xB1
	"SUPER2",            // Code 0xB2
	"SUPER3",            // Code 0xB3
	"ACUTE",             // Code 0xB4
	"MICRO",             // Code 0xB5
	"PILCROW",           // Code 0xB6
	"MIDDOT",            // Code 0xB7
	"CEDILLA",           // Code 0xB8
	"SUPER1",            // Code 0xB9
	"SEMICOLON",	     // Code 0xBA
	"EQUALS",            // Code 0xBB
	"ONEQUARTER",        // Code 0xBC
	"MINUS",	         // Code 0xBD
	"STOP",				 // Code 0xBE
	"SLASH",			 // Code 0xBF
	"TILDE",             // Code 0xC0
	"UPPER_A_ACUTE",     // Code 0xC1
	"UPPER_A_CIRCUMFLEX",// Code 0xC2
	"UPPER_A_TILDE",     // Code 0xC3
	"UPPER_A_UMLAUT",    // Code 0xC4
	"UPPER_A_RING",      // Code 0xC5
	"UPPER_AE",          // Code 0xC6
	"UPPER_C_CEDILLA",   // Code 0xC7
	"UPPER_E_GRAVE",     // Code 0xC8
	"UPPER_E_ACUTE",     // Code 0xC9
	"UPPER_E_CIRCUMFLEX",// Code 0xCA
	"UPPER_E_UMLAUT",    // Code 0xCB
	"UPPER_I_GRAVE",     // Code 0xCC
	"UPPER_I_ACUTE",     // Code 0xCD
	"UPPER_I_CIRCUMFLEX",// Code 0xCE
	"UPPER_I_UMLAUT",    // Code 0xCF
	"UPPER_ETH",         // Code 0xD0
	"UPPER_N_TILDE",     // Code 0xD1
	"UPPER_O_GRAVE",     // Code 0xD2
	"UPPER_O_ACUTE",     // Code 0xD3
	"UPPER_O_CIRCUMFLEX",// Code 0xD4
	"UPPER_O_TILDE",     // Code 0xD5
	"UPPER_O_UMLAUT",    // Code 0xD6
	"MULTIPLY",          // Code 0xD7
	"UPPER_O_SLASH",     // Code 0xD8
	"UPPER_U_GRAVE",     // Code 0xD9
	"UPPER_U_ACUTE",     // Code 0xDA
	"OPENBRACE",         // Code 0xDB
	"BACKSLASH",	     // Code 0xDC
	"CLOSEBRACE",        // Code 0xDD
	"QUOTE",		     // Code 0xDE
	"LOWER_SHARP_S",     // Code 0xDF
	"LOWER_A_GRAVE",     // Code 0xE0
	"LOWER_A_ACUTE",     // Code 0xE1
	"LOWER_A_CIRCUMFLEX",// Code 0xE2
	"LOWER_A_TILDE",     // Code 0xE3
	"LOWER_A_UMLAUT",    // Code 0xE4
	"LOWER_A_RING",      // Code 0xE5
	"LOWER_AE",          // Code 0xE6
	"LOWER_C_CEDILLA",   // Code 0xE7
	"LOWER_E_GRAVE",     // Code 0xE8
	"LOWER_E_ACUTE",     // Code 0xE9
	"LOWER_E_CIRCUMFLEX",// Code 0xEA
	"LOWER_E_UMLAUT",    // Code 0xEB
	"LOWER_I_GRAVE",     // Code 0xEC
	"LOWER_I_ACUTE",     // Code 0xED
	"LOWER_I_CIRCUMFLEX",// Code 0xEE
	"LOWER_I_UMLAUT",    // Code 0xEF
	"LOWER_ETH",         // Code 0xF0
	"LOWER_N_TILDE",     // Code 0xF1
	"LOWER_O_GRAVE",     // Code 0xF2
	"LOWER_O_ACUTE",     // Code 0xF3
	"LOWER_O_CIRCUMFLEX",// Code 0xF4
	"LOWER_O_TILDE",     // Code 0xF5
	"LOWER_O_UMLAUT",    // Code 0xF6
	"DIVIDE",            // Code 0xF7
	"LOWER_O_SLASH",     // Code 0xF8
	"LOWER_U_GRAVE",     // Code 0xF9
	"LOWER_U_ACUTE",     // Code 0xFA
	"LOWER_U_CIRCUMFLEX",// Code 0xFB
	"LOWER_U_UMLAUT",    // Code 0xFC
	"LOWER_Y_ACUTE",     // Code 0xFD
	"LOWER_THORN",       // Code 0xFE
	"NONE"				 // Code 0xFF
};

static MENUS glmenu[] = {
   { "WINDOWED ", {"YES", "YES"},
   1, 100,1,1,{1,1},MENU_INT,0,0,0},
   { "RESOLUTION ", {"1024x768","1152x864","1280x1024","1600x1200","1920x1080","","",""},
   4, 200,1,1,{0,1,2,3,4,0,0,0,0,0},MENU_INT,0,0,0},
   // Note to self: These are not currently doing anything.
   { "GAMMA ADJUST ", {"-8%","-5%","-3%","0%","+3%","+5%","+8%","+12%","+15","+18"},
   9, 300,0,0,{157,147,137,127,117,107,100,97,94,90},MENU_FLOAT,.5,-20,20},
   { "BRIGHTNESS ADJ", {"-8%","-5%","-3%","0%","+3%","+5%","+8%","+12%","+15","+18"},
   9, 300,0,0,{113,117,122,127,134,140,144,149,155,161},MENU_FLOAT,.5,-20,20},
   { "CONTRAST ADJ", {"-8%","-5%","-3%","0%","+3%","+5%","+8%","+12%","+15","+18"},
   9, 300,0,0,{157,147,137,127,117,107,100,97,94,90},MENU_FLOAT,.5,-20,20},
   // **********************************
   { "VSYNC", {"DISABLED", "ENABLED"},
   1, 400,1,1,{0,1},MENU_INT,0,0,0},
   { "DRAW 0 LINES", {"DISABLED", "ENABLED"},   // This is currenly very limited 12/10/24
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
   1, 300,0,0,{0,0},MENU_FLOAT,5,0,20},
   { "HV CHATTER", {"NO", "YES"},
   1, 400,1,1,{0,1},MENU_INT,0,0,1},
   { "PS HISS", {"NO", "YES"},
   1, 500,1,1,{0,1},MENU_INT,0,0,1},
   { "PS NOISE",{"NO", "YES",},
   1, 600,1,1,{0,1},MENU_INT,0,0,1},
   { "NONE", {"NONE", " ", " ", " "}, 0,0, 0,0,{0,0,0,0,0,0,0,0,0,0},0,0,0,0},
   //{ "NONE", {"NONE", " ", " ", " "}, 0,0, 0,0,{0,0,0,0,0,0,0,0,0,0},0,0,0,0} // Todo: understand buffer overflow here and resolve.
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
	static int last_menu_setting;
	show_menu = on;
	if (last_menu_setting != on && on == 0) { save_video_menu(); save_sound_menu(); }
	last_menu_setting = on;
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
	// Super important.
	number = 0;
}

void change_menu_level(int dir) //This is up and down
{
	int level = 0;

	if (dir)
	{
		if (menulevel == VIDEOMENU) { if ((glmenu[menuitem].current) != currentval) { glmenu[menuitem].current = currentval; } }
		if (menulevel == AUDIOMENU) { if ((soundmenu[menuitem].current) != currentval) { soundmenu[menuitem].current = currentval; } }

		if (menuitem < (num_this_menu)) { menuitem++; } //change dir here

		if (menulevel == VIDEOMENU) { currentval = glmenu[menuitem].current; }
		if (menulevel == AUDIOMENU) { currentval = soundmenu[menuitem].current; }
	}

	else
	{
		if (menulevel == VIDEOMENU) { if ((glmenu[menuitem].current) != currentval) { glmenu[menuitem].current = currentval; } }
		if (menulevel == AUDIOMENU) { if ((soundmenu[menuitem].current) != currentval) { soundmenu[menuitem].current = currentval; } }

		if (menuitem > 0) { menuitem--; } //change dir here

		if (menulevel == VIDEOMENU) { currentval = glmenu[menuitem].current; }
		if (menulevel == AUDIOMENU) { currentval = soundmenu[menuitem].current; }
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
	case ROOTMENU: {
		menulevel = menulevel * (menuitem + 2);	
		menuitem = 0;
		break;
	}
				 //For changing levels
	case GLOBALKEYS: { sublevel = 1; break; }
	case LOCALKEYS: { sublevel = 1; break; }
	case GLOBALJOY: { sublevel = 1; break; }
	case LOCALJOY: { sublevel = 1; break; };
	case ANALOGMENU: do_mouse_menu(); break;
	case DIPMENU: { do_dipswitch_menu(); break; }
	}
}

//This is no longer being used
/*
void change_menu()
{
	//This is menu exit;

	   //Tweak for dipswitch to make sure to set selected item on exit
	if (menulevel == AUDIOMENU)
	{
		if ((soundmenu[menuitem].current) != currentval) { soundmenu[menuitem].current = currentval; }
		number = 0;
		save_sound_menu();
	}
	if (menulevel == VIDEOMENU)
	{
		if ((glmenu[menuitem].current) != currentval) { glmenu[menuitem].current = currentval; }
		number = 0;
		save_video_menu();
	}

	if (menulevel == DIPMENU) { number = 0; }
	if (menulevel == ANALOGMENU) { number = 0; } //if ((mousemenu[menuitem - 1].current) != currentval){ mousemenu[menuitem - 1].current = currentval; } number = 0; save_mouse_menu();}

	if (menulevel == GLOBALKEYS) { number = 0; }//save_keys();}
	if (menulevel > ROOTMENU) { menulevel = 100; menuitem = 1; number = 0; }
}
*/
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
	float top = 650.0;
	float LFT = 250.0;
	float SPC = 35;
	num_this_menu = 7;

	quad_from_center(520.0, 525.0, 580.0, 350.0, 20, 20, 80, 255);

	for (x = 0; x < num_this_menu + 1; x++)
	{
		if (menuitem == x) { C = RGB_PINK; }
		else { C = RGB_WHITE; }

		switch (x)
		{
		case 0: fprint(LFT, top - (SPC * x), C, 2.6, "KEY CONFIG (GLOBAL)"); break;
		case 1: fprint(LFT, top - (SPC * x), C, 2.6,  "KEY CONFIG (THIS GAME)"); break;
		case 2: fprint(LFT, top - (SPC * x), C, 2.6,  "JOY CONFIG (GLOBAL)"); break;
		case 3: fprint(LFT, top - (SPC * x), C, 2.6,  "JOY CONFIG (THIS GAME)"); break;
		case 4: fprint(LFT, top - (SPC * x), C, 2.6,  "ANALOG CONFIG"); break;
		case 5: fprint(LFT, top - (SPC * x), C, 2.6,  "DIPSWITCHES"); break;
		case 6: fprint(LFT, top - (SPC * x), C, 2.6,  "VIDEO SETUP"); break;
		case 7: fprint(LFT, top - (SPC * x), C, 2.6,  "SOUND SETUP"); break;
		}
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
	int left = 225;
	int top = 0;
	int bottom = 0;
	int shift = 0;
	int b = 0;
	// int newjoy;
	int joyindex;

	if (Machine->input_ports == 0)
		return 0;
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

	quad_from_center(450, 475, 580, 450, 20, 20, 80, 255);

	fprint(left, 650, RGB_WHITE, 1.9, "JOY CONFIG - Global");

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
				fprint(left, start, MAKE_RGB(255, color, 255), 2.3, menu_item[menuitem]);
			}
			else
			{
				color = 255;
				fprint(left, start, MAKE_RGB(255, color, 255), 2.0, "%s", entry[x]->name);
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

		fprint(left, 625, MAKE_RGB(255, 200, 30), 1.8, "Press a Button or N to clear");
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
	//char flag[400];
	int i, x;
	struct InputPort* in;
	int total;

	int color = 255;
	int spacing = 21;
	int start = 600;
	int left = 225;
	int top = 0;
	int bottom = 0;
	int shift = 0;
	int b = 0;
	int joyindex;

	if (Machine->input_ports == 0)
			return 0;

	in = Machine->input_ports;
	
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

	quad_from_center(450, 475, 580, 450, 20, 20, 80, 255);

	fprint(left, 650, RGB_WHITE, 1.9, "JOY CONFIG - This Game");

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
				fprint(left, start, MAKE_RGB(255, color, 255), 2.3, menu_item[menuitem]);
			}
			else
			{
				color = 255;
				fprint(left, start, MAKE_RGB(255, color, 255), 2.0, menu_item[x]);
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

		fprint(225, 625, MAKE_RGB(255, 200, 30), 1.8, "Press a Button or N to clear");
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
	int left = 225;
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
	quad_from_center(500, 475, 670, 500, 20, 20, 80, 255);
	fprint(left, 650, RGB_WHITE, 2.0, "Input Configuration (Global)");

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
				fprint(left, start, MAKE_RGB(255, color, 255), 2.0, menu_item[menuitem]);
			}
			else
			{
				color = 255;
				fprint(left, start, MAKE_RGB(255, color, 255), 2.0, "%s", entry[x]->name);
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
	int left = 225;
	int spacing = 21;
	int start = 600;
	int top = 0;
	int bottom = 0;
	int shift = 0;
	struct InputPort* in;
	int total;
	int b = 0;
	int i;

	LOG_INFO("---------------CALLING show game keys");

	in = Machine->input_ports;
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
	LOG_INFO("Made it here 1");
	num_this_menu = (total - 1);

	for (i = 0; i < total; i++)
	{
		if (i < total + 1)        //osd_key_name(input_port_key(entry[i]));
		{
			menu_subitem[i] = key_names[input_port_key(entry[i])];
			LOG_INFO("MENU ITEM Key # is %x", input_port_key(entry[i]));
			/* If the key isn't the default, flag it */
			//if (entry[i]->keyboard != IP_KEY_default)
			//	flag[i] = 1;
			//else
			//	flag[i] = 0;
		}
		else menu_subitem[i] = 0;	/* no subitem */
	}
	LOG_INFO("Made it here 2");
	quad_from_center(500, 475, 670, 450, 20, 20, 80, 255);

	fprint(left, 650, RGB_WHITE, 2.0, "Input Configuration (This Game)");// %d", num_this_menu);

	if (menuitem > num_this_menu) menuitem = num_this_menu;

	for (x = 0; x < total; x++)
	{
		if (menuitem == x)
		{
			color = 0;
			fprint(left, start, MAKE_RGB(255, color, 255), 2.0, menu_item[menuitem]);
			
		}
		else
		{
			color = 255;
			fprint(left, start, MAKE_RGB(255, color, 255), 2.0, menu_item[x]);
			//LOG_INFO("Printing menu item %x", menu_item[x]);
		}

		fprint(545, start, MAKE_RGB(255, color, 255), 2.0, "%s", menu_subitem[x]);//key_names[entry[x]->keyboard]);
		LOG_INFO("Printing menu item key name %x", menu_subitem[x]);
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
	int left = 175;
	int spacing = 21;
	int start = 600;
	int top = 0;
	int bottom = 0;
	int shift = 0;

	sel = menuitem;//selected - 1;
	in = Machine->input_ports;
	
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
	if (total == 0)
	{
		menulevel = 100; menuitem = 0; //Reset menu level to top and return
		return; //No Dipswitches!
	}
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

	fprint(285, 650, RGB_WHITE, 1.9, "DIPSWITCH MENU");

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

	quad_from_center(520, 525, 850, 325, 20, 20, 80, 255);

	for (x = 0; x < total; x++)
	{
		if (menuitem == x)
		{
			color = 0;
			fprint(left, start, MAKE_RGB(255, color, 255), 2.0, menu_item[menuitem]);
		}
		else
		{
			color = 255;
			fprint(left, start, MAKE_RGB(255, color, 255), 2.0, menu_item[x]);
		}

		fprint(580, start, MAKE_RGB(255, color, 255), 2.0, menu_subitem[x]);

		if (arrowize == 2 && menuitem == x)
		{
			fprint(get_string_len(), start, RGB_WHITE, 2.0, ">"); //740
		}
		if (arrowize == 1 && menuitem == x)
		{
			fprint(555, start, RGB_WHITE, 2.0, "<");
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

	if (k == OSD_KEY_PAUSE || k == OSD_KEY_ENTER) { k = 0; } //Make sure to trap the entering of the key
	if (k) {
		sublevel = 0;
		//key[k] = 0;
		//clear_keybuf();
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
	int left = 225;
	int color = 0;
	int start = 600;
	int top = 0;
	int bottom = 0;
	int shift = 0;

	sel = menuitem;//selected - 1;

	if (Machine->input_ports == 0)
		return 0;

	in = Machine->input_ports;
	
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

	if (total == 0)
	{
		menulevel = 100; menuitem = 0;
		return 0;
	}

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
			//LOG_INFO("Menu subitem %s", setting[i]);

			in++;
		}
		else menu_subitem[i] = 0;	/* no subitem */
	}

	quad_from_center(520, 575, 680, 200, 20, 20, 80, 255);

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
			fprint(left, start, MAKE_RGBA(255, color, 255, 255), 2.0, menu_item[menuitem]);
		}
		else
		{
			color = 255;
			fprint(left, start, MAKE_RGBA(255, color, 255, 255), 2.0, menu_item[x]);
		}

		fprint(left + 400, start, MAKE_RGBA(255, color, 255, 255), 2.0, menu_subitem[x]);

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

void do_sound_menu()
{
	int yval = 625;
	int color = 0;
	it = 0;
	int x = 0;
	int left = 225;

	if (number == 0)
	{
		while (soundmenu[number].NumOptions) { number++; }
		number--;
		num_this_menu = number;
		currentval = soundmenu[it].current;
	}

	quad_from_center(475, 575, 600, 275, 20, 20, 80, 255);
	
	if (gamenum == 0)
	{
		fprint(left, yval + 30, RGB_WHITE, 2.0, "Sound Settings - Global");
	}
	else {
		fprint(left, yval + 30, RGB_WHITE, 2.0, "Sound Settings - This Game");
	}
	val_low = 0;

	for (x = 0; x < num_this_menu + 1; x++)
	{
		val_low = 0;

		if (menuitem == it)
		{
			color = 0;
			if (soundmenu[(it)].menu_type == MENU_INT)
			{
				val_high = (soundmenu[(it)].NumOptions);
			}
			else if (soundmenu[(it)].menu_type == MENU_FLOAT)
			{
				val_low = soundmenu[(it)].Min * ceilf((1 / soundmenu[(it)].step));
				val_high = (soundmenu[(it)].Max * ceilf((1 / soundmenu[(it)].step)));
			}
		}
		else { color = 255; }

		if (soundmenu[(it)].menu_type == MENU_INT)
		{
			fprint(left + 350, yval, MAKE_RGB(255, color, 255), 2.0, "%s", soundmenu[(it)].options[soundmenu[(it)].current]);
		}
		else if (soundmenu[(it)].menu_type == MENU_FLOAT)
		{
			fprint(left + 350, yval, MAKE_RGB(255, color, 255), 2.0, "%2.1f", soundmenu[(it)].step * soundmenu[(it)].current);
		}

		fprint(left, yval, MAKE_RGB(255, color, 255), 2.0, soundmenu[(it)].heading);

		it++;
		yval -= 28; //35
	}
}

void check_sound_menu()
{
	if ((soundmenu[menuitem].current) != currentval) { soundmenu[menuitem].current = currentval; }

	if ((menuitem) == 0)
	{
		config.mainvol = (currentval * 12.75);
		//set_volume((int)(currentval * 12.75), 0);
		//play_sample(game_sounds[num_samples-5],currentval,128,1000,0);
	} //Main Vol
	if ((menuitem ) == 1)
	{
		config.pokeyvol = (currentval * 12.75);
		//play_sample(game_sounds[num_samples-5],currentval,128,1000,0);
	} //Pokey Vol
	if ((menuitem) == 2)
	{
		config.noisevol = (currentval * 12.75);
		//play_sample(game_sounds[num_samples-5],currentval,128,1000,0);
	}//Noise Vol

	if ((menuitem ) == 3) { config.hvnoise = currentval; setup_ambient(VECTOR);}
	if ((menuitem) == 4) { config.psnoise = currentval; setup_ambient(VECTOR); }
	if ((menuitem) == 5) { config.pshiss = currentval; setup_ambient(VECTOR);}
}

void setup_sound_menu()
{
	int x = 0;

	soundmenu[0].current = ceilf(config.mainvol);
	soundmenu[1].current = ceilf(config.pokeyvol);
	soundmenu[2].current = ceilf(config.noisevol);
	soundmenu[3].current = config.hvnoise;
	soundmenu[4].current = config.psnoise;
	soundmenu[5].current = config.pshiss;

	while (soundmenu[x].NumOptions != 0) { soundmenu[x].Changed = soundmenu[x].current; x++; }//SET TO DETECT CHANGED VALUES
}

void save_sound_menu()
{
	if (soundmenu[0].Changed != soundmenu[0].current) my_set_config_int("main", "mainvol", soundmenu[0].current, gamenum);
	if (soundmenu[1].Changed != soundmenu[1].current) my_set_config_int("main", "pokeyvol", soundmenu[1].current, gamenum);
	if (soundmenu[2].Changed != soundmenu[2].current) my_set_config_int("main", "noisevol", soundmenu[2].current, gamenum);
	if (soundmenu[3].Changed != soundmenu[3].current) my_set_config_int("main", "hvnoise", soundmenu[3].current, gamenum);
	if (soundmenu[4].Changed != soundmenu[4].current) my_set_config_int("main", "psnoise", soundmenu[4].current, gamenum);
	if (soundmenu[5].Changed != soundmenu[5].current) my_set_config_int("main", "pshiss", soundmenu[5].current, gamenum);
}

/*
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
	
	 if (mousemenu[0].Changed != mousemenu[0].current) my_set_config_int("main", "mouse1xs", mousemenu[0].current, gamenum);
	 if (mousemenu[1].Changed != mousemenu[1].current) my_set_config_int("main", "mouse1ys", mousemenu[1].current, gamenum);
	 if (mousemenu[2].Changed != mousemenu[2].current) my_set_config_int("main", "mouse1x_invert", mousemenu[2].current, gamenum);
	 if (mousemenu[3].Changed != mousemenu[3].current) my_set_config_int("main", "mouse1y_invert",  mousemenu[3].current, gamenum);
	 
}
*/
void set_points_lines()
{
	config.linewidth = glmenu[10].step * (glmenu[10].current);
	config.pointsize = glmenu[11].step * (glmenu[11].current);

	//Change this to be set in the gl code.
	glLineWidth(config.linewidth);//linewidth
	glPointSize(config.pointsize);//pointsize
}

void setup_video_menu()
{
	int x = 0;

	if (config.windowed) { glmenu[0].current = 1; }
	else { glmenu[0].current = 0; }//WINDOWED

	switch (config.screenw) //RESOLUTION
	{
	case 1024: glmenu[1].current = 0; break;
	case 1152: glmenu[1].current = 1; break;
	case 1280: glmenu[1].current = 2; break;
	case 1600: glmenu[1].current = 3; break;
	case 1920: glmenu[1].current = 4; break;
	default: glmenu[1].current = 0; break;
	}
	glmenu[2].current = (config.gamma - 127) / 2;
	glmenu[3].current = (config.bright - 127) / 2;
	glmenu[4].current = (config.contrast - 127) / 2;
	if (config.forcesync) { glmenu[5].current = 1; }
	else { glmenu[5].current = 0; }//VSYNC

	//Draw zero lines
	if (config.drawzero) { glmenu[6].current = 1; }
	else { glmenu[6].current = 0; }//VSYNC
	if (config.widescreen) { glmenu[7].current = 1; }
	else { glmenu[7].current = 0; }

	switch (config.vectrail) //SAMPLE LEVEL
	{
	case 0: glmenu[8].current = 0; break;
	case 1: glmenu[8].current = 1; break;
	case 2: glmenu[8].current = 2; break;
	case 3: glmenu[8].current = 3; break;
	default: glmenu[8].current = 0; break;
	}

	glmenu[9].current = config.vecglow;
	glmenu[10].current = config.m_line;// / .1; //config.m_line;
	glmenu[11].current = config.m_point;// / .1;//config.m_point;
	glmenu[12].current = config.gain;//'if (config.monitor) {glmenu[12].current=1;}else {glmenu[12].current=0;}//MONITOR
	if (config.artwork) { glmenu[13].current = 1; }
	else { glmenu[13].current = 0; }//ARTWORK
	if (config.overlay) { glmenu[14].current = 1; }
	else { glmenu[14].current = 0; }//OVERLAY
	if (config.bezel) { glmenu[15].current = 1; }
	else { glmenu[15].current = 0; }//BEZEL
	if (config.artcrop) { glmenu[16].current = 1; }
	else { glmenu[16].current = 0; }//CROP BEZEL

	switch (config.priority) //POINTSIZE
	{
	case 0: glmenu[17].current = 0; break;
	case 1: glmenu[17].current = 1; break;
	case 2: glmenu[17].current = 2; break;
	case 3: glmenu[17].current = 3; break;
	case 4: glmenu[17].current = 4; break;
	default: glmenu[17].current = 2; break;
	}

	glmenu[18].current = config.kbleds;

	while (glmenu[x].NumOptions != 0) { glmenu[x].Changed = glmenu[x].current; x++; }//SET TO DETECT CHANGED VALUES
}

void do_video_menu()
{
	int yval = 650;
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
	quad_from_center(520, 400, 580, 625, 20, 20, 80, 255);

	if (gamenum == 0)
	{
		fprint(330, yval + 30, RGB_WHITE, 2.0, "GL Settings - Global");
	}
	else {
		fprint(300, yval + 30, RGB_WHITE, 2.0, "GL Settings - This Game");
	}
	val_low = 0;

	for (x = 0; x < num_this_menu + 1; x++)
	{
		val_low = 0;

		if (menuitem == it)
		{
			color = 0;
			if (glmenu[(it)].menu_type == MENU_INT)
			{
				val_high = (glmenu[(it)].NumOptions);
			}
			else if (glmenu[(it)].menu_type == MENU_FLOAT)
			{
				val_low = glmenu[(it)].Min * ceilf((1 / glmenu[(it)].step));
				val_high = (glmenu[(it)].Max * ceilf((1 / glmenu[(it)].step)));
			}
		}
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

void check_video_menu()
{
	if ((glmenu[menuitem].current) != currentval) { glmenu[menuitem].current = currentval; }

	//if ((menuitem-1)==2){SetGammaRamp(127+(currentval*2),config.bright,config.contrast); }
	//if ((menuitem-1)==3){SetGammaRamp(config.gamma,127+(currentval*2),config.contrast); }
	//if ((menuitem-1)==4){SetGammaRamp(config.gamma,config.bright,127+(currentval*2)); }
	if ((menuitem - 1) == 4) { config.forcesync = currentval; }
	if ((menuitem - 1) == 5) { config.drawzero = currentval; }
	if ((menuitem - 1) == 6) { config.widescreen = currentval; } //Widescreen_calc(); //Recalculate Widescreen value
	if ((menuitem - 1) == 7) { config.vectrail = currentval; }
	if ((menuitem - 1) == 8) { config.vecglow = currentval; }
	if ((menuitem - 1) == 9) { config.linewidth = glmenu[10].step * currentval; set_points_lines();	}
	if ((menuitem - 1) == 10) { config.pointsize = glmenu[11].step * currentval; set_points_lines(); }
	if ((menuitem - 1) == 11) { config.gain = currentval; }
	if ((menuitem - 1) == 12) { if (art_loaded[0]) config.artwork = currentval; }
	if ((menuitem - 1) == 13) { if (art_loaded[1])config.overlay = currentval; }
	if ((menuitem - 1) == 14) { if (art_loaded[3]) config.bezel = currentval; setup_video_config(); }
	if ((menuitem - 1) == 15) { config.artcrop = currentval;  setup_video_config(); } //Reconfigure video size
	if ((menuitem - 1) == 16) { config.priority = currentval; }
	if ((menuitem - 1) == 17) { config.kbleds = currentval; }
}

void save_video_menu()
{
	LOG_DEBUG("SAVE VIDEO MENU CALLED");

	int x = 0;
	int y = 0;
	/*
		 if (glmenu[2].Changed != glmenu[2].current)   my_set_config_int("main", "gamma", (glmenu[2].current*2)+127, gamenum);
		 if (glmenu[3].Changed != glmenu[3].current)   my_set_config_int("main", "bright", (glmenu[3].current*2)+127, gamenum);
		 if (glmenu[4].Changed != glmenu[4].current)   my_set_config_int("main", "contrast", (glmenu[4].current*2)+127, gamenum);

		 */

	if (glmenu[1].Changed != glmenu[1].current)
	{
		switch (glmenu[1].current) //RESOLUTION
		{
		case 0: x = 1024; y = 768; break;
		case 1: x = 1152; y = 864; break;
		case 2: x = 1280; y = 1024; break;
		case 3: x = 1600; y = 1200; break;
		case 4: x = 1920; y = 1080; break;
		default:x = 1024; y = 768; break;
		}

		my_set_config_int("main", "screenw", x, gamenum);
		my_set_config_int("main", "screenh", y, gamenum);
	}

	if (glmenu[5].Changed != glmenu[5].current)   my_set_config_int("main", "force_vsync", glmenu[5].current, gamenum);
	if (glmenu[6].Changed != glmenu[6].current)   my_set_config_int("main", "drawzero", glmenu[6].Value[glmenu[6].current], gamenum);
	if (glmenu[7].Changed != glmenu[7].current)   my_set_config_int("main", "widescreen", glmenu[7].Value[glmenu[7].current], gamenum);
	if (glmenu[8].Changed != glmenu[8].current)   my_set_config_int("main", "vectortrail", glmenu[8].Value[glmenu[8].current], gamenum);
	if (glmenu[9].Changed != glmenu[9].current)   my_set_config_int("main", "vectorglow", glmenu[9].current, gamenum);
	if (glmenu[10].Changed != glmenu[10].current) my_set_config_int("main", "m_line", glmenu[10].current, gamenum);
	if (glmenu[11].Changed != glmenu[11].current) my_set_config_int("main", "m_point", glmenu[11].current, gamenum);
	if (glmenu[12].Changed != glmenu[12].current) my_set_config_int("main", "gain", glmenu[12].current, gamenum);
	if (glmenu[13].Changed != glmenu[13].current) my_set_config_int("main", "artwork", glmenu[13].current, gamenum);
	if (glmenu[14].Changed != glmenu[14].current) my_set_config_int("main", "overlay", glmenu[14].current, gamenum);
	if (glmenu[15].Changed != glmenu[15].current) my_set_config_int("main", "bezel", glmenu[15].current, gamenum);
	if (glmenu[16].Changed != glmenu[16].current) my_set_config_int("main", "artcrop", glmenu[16].current, gamenum);
	if (glmenu[18].Changed != glmenu[17].current) my_set_config_int("main", "priority", glmenu[17].current, gamenum);
	if (glmenu[18].Changed != glmenu[18].current) { my_set_config_int("main", "kbleds", glmenu[18].current, gamenum); }
}
