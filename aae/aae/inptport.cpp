// ---------------------------------------------------------------------------- -
// Legacy MAME-Derived Module
// This file contains code originally developed as part of the M.A.M.E.™ Project.
// Portions of this file remain under the copyright of the original MAME authors
// and contributors. It has since been adapted and merged into the AAE (Another
// Arcade Emulator) project, with modernizations and enhancements applied.
//
// Integration:
//   This module is now part of the **AAE (Another Arcade Emulator)** codebase
//   and is integrated with its rendering, input, and emulation subsystems.
//
// Licensing Notice:
//   - Original portions of this code remain © the M.A.M.E.™ Project and its
//     respective contributors under their original terms of distribution.
//   - Modifications, enhancements, and new code are © 2025 Tim Cottrill and
//     released under the GNU General Public License v3 (GPLv3) or later.
//   - Redistribution must preserve both this notice and the original MAME
//     copyright acknowledgement.
//
// License:
//   This program is free software: you can redistribute it and/or modify
//   it under the terms of the GNU General Public License as published by
//   the Free Software Foundation, either version 3 of the License, or
//   (at your option) any later version.
//
//   This program is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
//   GNU General Public License for more details.
//
//   You should have received a copy of the GNU General Public License
//   along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// Original Copyright:
//   This file is originally part of and copyright the M.A.M.E.™ Project.
//   For more information about MAME licensing, see the original MAME source
//   distribution and its associated license files.
//
// -----------------------------------------------------------------------------
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


/***************************************************************************

  inptport.c

  Input ports handling

TODO:	remove the 1 analog device per port limitation
		support for inputports producing interrupts
		support for extra "real" hardware (PC throttle's, spinners etc)

***************************************************************************/

#include "aae_mame_driver.h"
#include "sys_log.h"
#include <math.h>
#include <string.h>
#include "inptport.h"
#include "osdepend.h"
#include "os_input.h"


/* Use the MRU code for 4way joysticks */
#define MRU_JOYSTICK

/* header identifying the version of the game.cfg file */
#define MAMECFGSTRING "MAMECFG\4"
#define MAMEDEFSTRING "MAMEDEF\3"

static unsigned short input_port_value[MAX_INPUT_PORTS];
static unsigned short input_vblank[MAX_INPUT_PORTS];

/* Assuming a maxium of one analog input device per port BW 101297 */
static struct InputPort* input_analog[MAX_INPUT_PORTS];
static int input_analog_current_value[MAX_INPUT_PORTS], input_analog_previous_value[MAX_INPUT_PORTS];
static int input_analog_init[MAX_INPUT_PORTS];

static int mouse_delta_x[OSD_MAX_JOY_ANALOG], mouse_delta_y[OSD_MAX_JOY_ANALOG];
static int analog_current_x[OSD_MAX_JOY_ANALOG], analog_current_y[OSD_MAX_JOY_ANALOG];
static int analog_previous_x[OSD_MAX_JOY_ANALOG], analog_previous_y[OSD_MAX_JOY_ANALOG];

static const char* input_port_tag[MAX_INPUT_PORTS];

//////////////////////////////////////////////////////
//Added for AAE Counter and Spulse ///////////////////
//////////////////////////////////////////////////////
typedef struct {
	int now;
	int prev;
	int s2;
	int s3;
	int timer;
} GM_Input;
GM_Input theinput[260];
//////////////////////////////////////////////////////
//END of Additions.                ///////////////////
//////////////////////////////////////////////////////

/***************************************************************************

  Configuration load/save

***************************************************************************/

char ipdn_defaultstrings[][MAX_DEFSTR_LEN] =
{
	"Off",
	"On",
	"No",
	"Yes",
	"Lives",
	"Bonus Life",
	"Difficulty",
	"Demo Sounds",
	"Coinage",
	"Coin A",
	"Coin B",
	"9 Coins/1 Credit",
	"8 Coins/1 Credit",
	"7 Coins/1 Credit",
	"6 Coins/1 Credit",
	"5 Coins/1 Credit",
	"4 Coins/1 Credit",
	"3 Coins/1 Credit",
	"2 Coins/1 Credit",
	"3 Coins/2 Credits",
	"4 Coins/3 Credits",
	"1 Coin/1 Credit",
	"3 Coins/4 Credits",
	"2 Coins/3 Credits",
	"1 Coin/2 Credits",
	"2 Coins/5 Credits",
	"1 Coin/3 Credits",
	"1 Coin/4 Credits",
	"1 Coin/5 Credits",
	"1 Coin/6 Credits",
	"1 Coin/7 Credits",
	"1 Coin/8 Credits",
	"1 Coin/9 Credits",
	"Free Play",
	"Cabinet",
	"Upright",
	"Cocktail",
	"Flip Screen",
	"Service Mode",
	"Pause",
	"Test",
	"Tilt",
	"Version",
	"Region",
	"International",
	"Japan",
	"USA",
	"Europe",
	"Asia",
	"World",
	"Hispanic",
	"Language",
	"English",
	"Japanese",
	"German",
	"French",
	"Italian",
	"Spanish",
	"Very Easy",
	"Easy",
	"Normal",
	"Medium",
	"Hard",
	"Harder",
	"Hardest",
	"Very Hard",
	"Very Low",
	"Low",
	"High",
	"Higher",
	"Highest",
	"Very High",
	"Players",
	"Controls",
	"Dual",
	"Single",
	"Game Time",
	"Continue Price",
	"Controller",
	"Light Gun",
	"Joystick",
	"Trackball",
	"Continues",
	"Allow Continue",
	"Level Select",
	"Infinite",
	"Stereo",
	"Mono",
	"Unused",
	"Unknown"
};

struct ipd inputport_defaults[] =
{
	{ IPT_UI_PAUSE, "Pause",         OSD_KEY_P, 0 },
	{ IPT_COIN1,  "Coin 1",          OSD_KEY_5, 0 },
	{ IPT_COIN2,  "Coin 2",          OSD_KEY_6, 0 },
	{ IPT_COIN3,  "Coin 3",          OSD_KEY_3, 0 },
	{ IPT_COIN4,  "Coin 4",          OSD_KEY_4, 0 },
	{ IPT_TILT,   "Tilt",            OSD_KEY_T, IP_JOY_NONE },
	{ IPT_START1, "1 Player Start",  OSD_KEY_1, 0 },
	{ IPT_START2, "2 Players Start", OSD_KEY_2, 0 },
	{ IPT_START3, "3 Players Start", OSD_KEY_7, 0 },
	{ IPT_START4, "4 Players Start", OSD_KEY_8, 0 },

	{ IPT_JOYSTICK_UP | IPF_PLAYER1, "P1 Up",          OSD_KEY_UP,      OSD_JOY_UP    },
	{ IPT_JOYSTICK_DOWN | IPF_PLAYER1, "P1 Down",        OSD_KEY_DOWN,    OSD_JOY_DOWN  },
	{ IPT_JOYSTICK_LEFT | IPF_PLAYER1, "P1 Left",        OSD_KEY_LEFT,    OSD_JOY_LEFT  },
	{ IPT_JOYSTICK_RIGHT | IPF_PLAYER1, "P1 Right",       OSD_KEY_RIGHT,   OSD_JOY_RIGHT },
	{ IPT_BUTTON1 | IPF_PLAYER1, "P1 Button 1",    OSD_KEY_LCONTROL,OSD_JOY_FIRE1 },
	{ IPT_BUTTON2 | IPF_PLAYER1, "P1 Button 2",    OSD_KEY_ALT,     OSD_JOY_FIRE2 },
	{ IPT_BUTTON3 | IPF_PLAYER1, "P1 Button 3",    OSD_KEY_SPACE,   OSD_JOY_FIRE3 },
	{ IPT_BUTTON4 | IPF_PLAYER1, "P1 Button 4",    OSD_KEY_LSHIFT,  OSD_JOY_FIRE4 },
	{ IPT_BUTTON5 | IPF_PLAYER1, "P1 Button 5",    OSD_KEY_Z,       OSD_JOY_FIRE5 },
	{ IPT_BUTTON6 | IPF_PLAYER1, "P1 Button 6",    OSD_KEY_X,       OSD_JOY_FIRE6 },
	{ IPT_BUTTON7 | IPF_PLAYER1, "P1 Button 7",    OSD_KEY_C,       OSD_JOY_FIRE7 },
	{ IPT_BUTTON8 | IPF_PLAYER1, "P1 Button 8",    OSD_KEY_V,       OSD_JOY_FIRE8 },
	{ IPT_BUTTON9 | IPF_PLAYER1, "P1 Button 9",    OSD_KEY_B,       OSD_JOY_FIRE9 },
	{ IPT_BUTTON10 | IPF_PLAYER1, "P1 Button 10",   OSD_KEY_N,       OSD_JOY_FIRE10 },
	{ IPT_JOYSTICKRIGHT_UP | IPF_PLAYER1, "P1 Right/Up",    OSD_KEY_I,       OSD_JOY_FIRE2 },
	{ IPT_JOYSTICKRIGHT_DOWN | IPF_PLAYER1, "P1 Right/Down",  OSD_KEY_K,       OSD_JOY_FIRE3 },
	{ IPT_JOYSTICKRIGHT_LEFT | IPF_PLAYER1, "P1 Right/Left",  OSD_KEY_J,       OSD_JOY_FIRE1 },
	{ IPT_JOYSTICKRIGHT_RIGHT | IPF_PLAYER1, "P1 Right/Right", OSD_KEY_L,       OSD_JOY_FIRE4 },
	{ IPT_JOYSTICKLEFT_UP | IPF_PLAYER1, "P1 Left/Up",     OSD_KEY_E,       OSD_JOY_UP },
	{ IPT_JOYSTICKLEFT_DOWN | IPF_PLAYER1, "P1 Left/Down",   OSD_KEY_D,       OSD_JOY_DOWN },
	{ IPT_JOYSTICKLEFT_LEFT | IPF_PLAYER1, "P1 Left/Left",   OSD_KEY_S,       OSD_JOY_LEFT },
	{ IPT_JOYSTICKLEFT_RIGHT | IPF_PLAYER1, "P1 Left/Right",  OSD_KEY_F,       OSD_JOY_RIGHT },

	{ IPT_JOYSTICK_UP | IPF_PLAYER2, "P2 Up",          OSD_KEY_R,       OSD_JOY2_UP    },
	{ IPT_JOYSTICK_DOWN | IPF_PLAYER2, "P2 Down",        OSD_KEY_F,       OSD_JOY2_DOWN  },
	{ IPT_JOYSTICK_LEFT | IPF_PLAYER2, "P2 Left",        OSD_KEY_D,       OSD_JOY2_LEFT  },
	{ IPT_JOYSTICK_RIGHT | IPF_PLAYER2, "P2 Right",       OSD_KEY_G,       OSD_JOY2_RIGHT },
	{ IPT_BUTTON1 | IPF_PLAYER2, "P2 Button 1",    OSD_KEY_A,       OSD_JOY2_FIRE1 },
	{ IPT_BUTTON2 | IPF_PLAYER2, "P2 Button 2",    OSD_KEY_S,       OSD_JOY2_FIRE2 },
	{ IPT_BUTTON3 | IPF_PLAYER2, "P2 Button 3",    OSD_KEY_Q,       OSD_JOY2_FIRE3 },
	{ IPT_BUTTON4 | IPF_PLAYER2, "P2 Button 4",    OSD_KEY_W,       OSD_JOY2_FIRE4 },
	{ IPT_BUTTON5 | IPF_PLAYER2, "P2 Button 5",    0,               OSD_JOY2_FIRE5 },
	{ IPT_BUTTON6 | IPF_PLAYER2, "P2 Button 6",    0,               OSD_JOY2_FIRE6 },
	{ IPT_BUTTON7 | IPF_PLAYER2, "P2 Button 7",    0,               OSD_JOY2_FIRE7 },
	{ IPT_BUTTON8 | IPF_PLAYER2, "P2 Button 8",    0,               OSD_JOY2_FIRE8 },
	{ IPT_BUTTON9 | IPF_PLAYER2, "P2 Button 9",    0,				OSD_JOY_FIRE9 },
	{ IPT_BUTTON10 | IPF_PLAYER2, "P2 Button 10",   0,				OSD_JOY_FIRE10 },
	{ IPT_JOYSTICKRIGHT_UP | IPF_PLAYER2, "P2 Right/Up",    0,               0 },
	{ IPT_JOYSTICKRIGHT_DOWN | IPF_PLAYER2, "P2 Right/Down",  0,               0 },
	{ IPT_JOYSTICKRIGHT_LEFT | IPF_PLAYER2, "P2 Right/Left",  0,               0 },
	{ IPT_JOYSTICKRIGHT_RIGHT | IPF_PLAYER2, "P2 Right/Right", 0,               0 },
	{ IPT_JOYSTICKLEFT_UP | IPF_PLAYER2, "P2 Left/Up",     0,               0 },
	{ IPT_JOYSTICKLEFT_DOWN | IPF_PLAYER2, "P2 Left/Down",   0,               0 },
	{ IPT_JOYSTICKLEFT_LEFT | IPF_PLAYER2, "P2 Left/Left",   0,               0 },
	{ IPT_JOYSTICKLEFT_RIGHT | IPF_PLAYER2, "P2 Left/Right",  0,               0 },

	{ IPT_JOYSTICK_UP | IPF_PLAYER3, "P3 Up",          OSD_KEY_I,       OSD_JOY3_UP    },
	{ IPT_JOYSTICK_DOWN | IPF_PLAYER3, "P3 Down",        OSD_KEY_K,       OSD_JOY3_DOWN  },
	{ IPT_JOYSTICK_LEFT | IPF_PLAYER3, "P3 Left",        OSD_KEY_J,       OSD_JOY3_LEFT  },
	{ IPT_JOYSTICK_RIGHT | IPF_PLAYER3, "P3 Right",       OSD_KEY_L,       OSD_JOY3_RIGHT },
	{ IPT_BUTTON1 | IPF_PLAYER3, "P3 Button 1",    OSD_KEY_RCONTROL,OSD_JOY3_FIRE1 },
	{ IPT_BUTTON2 | IPF_PLAYER3, "P3 Button 2",    OSD_KEY_RSHIFT,  OSD_JOY3_FIRE2 },
	{ IPT_BUTTON3 | IPF_PLAYER3, "P3 Button 3",    OSD_KEY_ENTER,   OSD_JOY3_FIRE3 },
	{ IPT_BUTTON4 | IPF_PLAYER3, "P3 Button 4",    0,               OSD_JOY3_FIRE4 },

	{ IPT_JOYSTICK_UP | IPF_PLAYER4, "P4 Up",          0,               OSD_JOY4_UP    },
	{ IPT_JOYSTICK_DOWN | IPF_PLAYER4, "P4 Down",        0,               OSD_JOY4_DOWN  },
	{ IPT_JOYSTICK_LEFT | IPF_PLAYER4, "P4 Left",        0,               OSD_JOY4_LEFT  },
	{ IPT_JOYSTICK_RIGHT | IPF_PLAYER4, "P4 Right",       0,               OSD_JOY4_RIGHT },
	{ IPT_BUTTON1 | IPF_PLAYER4, "P4 Button 1",    0,               OSD_JOY4_FIRE1 },
	{ IPT_BUTTON2 | IPF_PLAYER4, "P4 Button 2",    0,               OSD_JOY4_FIRE2 },
	{ IPT_BUTTON3 | IPF_PLAYER4, "P4 Button 3",    0,               OSD_JOY4_FIRE3 },
	{ IPT_BUTTON4 | IPF_PLAYER4, "P4 Button 4",    0,               OSD_JOY4_FIRE4 },

	{ IPT_PADDLE | IPF_PLAYER1,  "Paddle",        OSD_KEY_LEFT,  OSD_JOY_LEFT },
	{ (IPT_PADDLE | IPF_PLAYER1) + IPT_EXTENSION,             "Paddle",        OSD_KEY_RIGHT, OSD_JOY_RIGHT  },
	{ IPT_PADDLE | IPF_PLAYER2,  "Paddle 2",      OSD_KEY_D,     OSD_JOY2_LEFT },
	{ (IPT_PADDLE | IPF_PLAYER2) + IPT_EXTENSION,             "Paddle 2",      OSD_KEY_G,     OSD_JOY2_RIGHT },
	{ IPT_PADDLE | IPF_PLAYER3,  "Paddle 3",      OSD_KEY_J,     OSD_JOY3_LEFT },
	{ (IPT_PADDLE | IPF_PLAYER3) + IPT_EXTENSION,             "Paddle 3",      OSD_KEY_L,     OSD_JOY3_RIGHT },
	{ IPT_PADDLE | IPF_PLAYER4,  "Paddle 4",      0, 0 },
	{ (IPT_PADDLE | IPF_PLAYER4) + IPT_EXTENSION,             "Paddle 4",      0, 0 },
	{ IPT_DIAL | IPF_PLAYER1,    "Dial",          OSD_KEY_LEFT,  OSD_JOY_LEFT },
	{ (IPT_DIAL | IPF_PLAYER1) + IPT_EXTENSION,               "Dial",          OSD_KEY_RIGHT, OSD_JOY_RIGHT },
	{ IPT_DIAL | IPF_PLAYER2,    "Dial 2",        OSD_KEY_D,     OSD_JOY2_LEFT },
	{ (IPT_DIAL | IPF_PLAYER2) + IPT_EXTENSION,               "Dial 2",      OSD_KEY_G,     OSD_JOY2_RIGHT },
	{ IPT_DIAL | IPF_PLAYER3,    "Dial 3",        OSD_KEY_J,     OSD_JOY3_LEFT },
	{ (IPT_DIAL | IPF_PLAYER3) + IPT_EXTENSION,               "Dial 3",      OSD_KEY_L,     OSD_JOY3_RIGHT },
	{ IPT_DIAL | IPF_PLAYER4,    "Dial 4",        0, 0 },
	{ (IPT_DIAL | IPF_PLAYER4) + IPT_EXTENSION,               "Dial 4",        0, 0 },

	{ IPT_TRACKBALL_X | IPF_PLAYER1, "Track X",   OSD_KEY_LEFT,  OSD_JOY_LEFT },
	{ (IPT_TRACKBALL_X | IPF_PLAYER1) + IPT_EXTENSION,                 "Track X",   OSD_KEY_RIGHT, OSD_JOY_RIGHT },
	{ IPT_TRACKBALL_X | IPF_PLAYER2, "Track X 2", OSD_KEY_D,     OSD_JOY2_LEFT },
	{ (IPT_TRACKBALL_X | IPF_PLAYER2) + IPT_EXTENSION,                 "Track X 2", OSD_KEY_G,     OSD_JOY2_RIGHT },
	{ IPT_TRACKBALL_X | IPF_PLAYER3, "Track X 3", OSD_KEY_J,     OSD_JOY3_LEFT },
	{ (IPT_TRACKBALL_X | IPF_PLAYER3) + IPT_EXTENSION,                 "Track X 3", OSD_KEY_L,     OSD_JOY3_RIGHT },
	{ IPT_TRACKBALL_X | IPF_PLAYER4, "Track X 4", 0, 0 },
	{ (IPT_TRACKBALL_X | IPF_PLAYER4) + IPT_EXTENSION,                 "Track X 4", 0, 0 },

	{ IPT_TRACKBALL_Y | IPF_PLAYER1, "Track Y",   OSD_KEY_UP,    OSD_JOY_UP },
	{ (IPT_TRACKBALL_Y | IPF_PLAYER1) + IPT_EXTENSION,                 "Track Y",   OSD_KEY_DOWN,  OSD_JOY_DOWN },
	{ IPT_TRACKBALL_Y | IPF_PLAYER2, "Track Y 2", OSD_KEY_R,     OSD_JOY2_UP },
	{ (IPT_TRACKBALL_Y | IPF_PLAYER2) + IPT_EXTENSION,                 "Track Y 2", OSD_KEY_F,     OSD_JOY2_DOWN },
	{ IPT_TRACKBALL_Y | IPF_PLAYER3, "Track Y 3", OSD_KEY_I,     OSD_JOY3_UP },
	{ (IPT_TRACKBALL_Y | IPF_PLAYER3) + IPT_EXTENSION,                 "Track Y 3", OSD_KEY_K,     OSD_JOY3_DOWN },
	{ IPT_TRACKBALL_Y | IPF_PLAYER4, "Track Y 4", 0, 0 },
	{ (IPT_TRACKBALL_Y | IPF_PLAYER4) + IPT_EXTENSION,                 "Track Y 4", 0, 0 },

	{ IPT_AD_STICK_X | IPF_PLAYER1, "AD Stick X",   OSD_KEY_LEFT,  OSD_JOY_LEFT },
	{ (IPT_AD_STICK_X | IPF_PLAYER1) + IPT_EXTENSION,                "AD Stick X",   OSD_KEY_RIGHT, OSD_JOY_RIGHT },
	{ IPT_AD_STICK_X | IPF_PLAYER2, "AD Stick X 2", OSD_KEY_D,     OSD_JOY2_LEFT },
	{ (IPT_AD_STICK_X | IPF_PLAYER2) + IPT_EXTENSION,                "AD Stick X 2", OSD_KEY_G,     OSD_JOY2_RIGHT },
	{ IPT_AD_STICK_X | IPF_PLAYER3, "AD Stick X 3", OSD_KEY_J,     OSD_JOY3_LEFT },
	{ (IPT_AD_STICK_X | IPF_PLAYER3) + IPT_EXTENSION,                "AD Stick X 3", OSD_KEY_L,     OSD_JOY3_RIGHT },
	{ IPT_AD_STICK_X | IPF_PLAYER4, "AD Stick X 4", 0, 0 },
	{ (IPT_AD_STICK_X | IPF_PLAYER4) + IPT_EXTENSION,                "AD Stick X 4", 0, 0 },

	{ IPT_AD_STICK_Y | IPF_PLAYER1, "AD Stick Y",   OSD_KEY_UP,    OSD_JOY_UP },
	{ (IPT_AD_STICK_Y | IPF_PLAYER1) + IPT_EXTENSION,                "AD Stick Y",   OSD_KEY_DOWN,  OSD_JOY_DOWN },
	{ IPT_AD_STICK_Y | IPF_PLAYER2, "AD Stick Y 2", OSD_KEY_R,     OSD_JOY2_UP },
	{ (IPT_AD_STICK_Y | IPF_PLAYER2) + IPT_EXTENSION,                "AD Stick Y 2", OSD_KEY_F,     OSD_JOY2_DOWN },
	{ IPT_AD_STICK_Y | IPF_PLAYER3, "AD Stick Y 3", OSD_KEY_I,     OSD_JOY3_UP },
	{ (IPT_AD_STICK_Y | IPF_PLAYER3) + IPT_EXTENSION,                "AD Stick Y 3", OSD_KEY_K,     OSD_JOY3_DOWN },
	{ IPT_AD_STICK_Y | IPF_PLAYER4, "AD Stick Y 4", 0, 0 },
	{ (IPT_AD_STICK_Y | IPF_PLAYER4) + IPT_EXTENSION,                "AD Stick Y 4", 0, 0 },

	{ IPT_UNKNOWN,             "UNKNOWN",         IP_KEY_NONE,     IP_JOY_NONE },
	{ IPT_END,                 0,                 0,     0 }	/* returned when there is no match */
};

static void load_default_keys(void)
{
	void* f;

	if ((f = osd_fopen("default", 0, OSD_FILETYPE_CONFIG, 0)) != 0)
	{
		char buf[8];

		/* read header */
		if (osd_fread(f, buf, 8) != 8)
			goto getout;
		if (memcmp(buf, MAMEDEFSTRING, 8) != 0)
			goto getout;	/* header invalid */

		for (;;)
		{
			UINT32 type;
			UINT16 keyboard, joystick;
			int i;

			if (readint(f, &type) != 0)
				goto getout;
			if (readword(f, &keyboard) != 0)
				goto getout;
			if (readword(f, &joystick) != 0)
				goto getout;

			i = 0;
			while (inputport_defaults[i].type != IPT_END)
			{
				if (inputport_defaults[i].type == type)
				{
					inputport_defaults[i].keyboard = keyboard;
					inputport_defaults[i].joystick = joystick;
				}
				i++;
			}
		}

	getout:
		osd_fclose(f);
	}
}

static void save_default_keys(void)
{
	void* f;

	if ((f = osd_fopen("default", 0, OSD_FILETYPE_CONFIG, 1)) != 0)
	{
		int i;

		/* write header */
		osd_fwrite(f, MAMEDEFSTRING, 8);

		i = 0;
		while (inputport_defaults[i].type != IPT_END)
		{
			writeint(f, inputport_defaults[i].type);
			writeword(f, inputport_defaults[i].keyboard);
			writeword(f, inputport_defaults[i].joystick);
			i++;
		}

		osd_fclose(f);
	}
}

static int readip(void* f, struct InputPort* in)
{
	if (readint(f, &in->type) != 0)
		return -1;
	if (readword(f, &in->mask) != 0)
		return -1;
	if (readword(f, &in->default_value) != 0)
		return -1;
	if (readword(f, &in->keyboard) != 0)
		return -1;
	if (readword(f, &in->joystick) != 0)
		return -1;

	return 0;
}

static void writeip(void* f, struct InputPort* in)
{
	writeint(f, in->type);
	writeword(f, in->mask);
	writeword(f, in->default_value);
	writeword(f, in->keyboard);
	writeword(f, in->joystick);
}

int load_input_port_settings(void)
{
	void* f;

	LOG_INFO("Setting Default Keys");

	load_default_keys();

	LOG_INFO("opening File");

	if ((f = osd_fopen(Machine->gamedrv->name, 0, OSD_FILETYPE_CONFIG, 0)) != 0)
	{
		struct InputPort* in;
		unsigned int total, savedtotal;
		char buf[8];
		//int i;

		in = Machine->gamedrv->input_ports;
		
		/* calculate the size of the array */
		total = 0;
		while (in->type != IPT_END)
		{
			total++;
			in++;
		}

		/* read header */
		if (osd_fread(f, buf, 8) != 8)
			goto getout;
		if (memcmp(buf, MAMECFGSTRING, 8) != 0)
			goto getout;	/* header invalid */

		/* read array size */
		if (readint(f, &savedtotal) != 0)
			goto getout;
		if (total != savedtotal)
			goto getout;	/* different size */

		/* read the original settings and compare them with the ones defined in the driver */
		in = Machine->gamedrv->input_ports;
		while (in->type != IPT_END)
		{
			struct InputPort saved;

			if (readip(f, &saved) != 0)
				goto getout;

			if (in->mask != saved.mask ||
				in->default_value != saved.default_value ||
				in->type != saved.type ||
				in->keyboard != saved.keyboard ||
				in->joystick != saved.joystick)
				goto getout;	/* the default values are different */

			in++;
		}

		/* read the current settings */
		in = Machine->input_ports;
		while (in->type != IPT_END)
		{
			if (readip(f, in) != 0)
				goto getout;
			in++;
		}

	getout:
		osd_fclose(f);
	}

	//Update the input port tags regardless if there is a save file or not.
	memset(&input_port_tag, 0, sizeof(input_port_tag));
	struct InputPort* in = Machine->input_ports;
	LOG_INFO("Gamenum here in input ports is %d", gamenum);
	int temp = 0;

	while (in->type != IPT_END)
	{
		//Update input port definitions
		if (in->tag)
		{
			input_port_tag[temp] = in->tag;
			//LOG_INFO("Tag %s :: Num %d", in->tag, temp);
			temp++;
		}

		in++;
	}

	/* All analog ports need initialization */
	{
		int i;
		for (i = 0; i < MAX_INPUT_PORTS; i++)
			input_analog_init[i] = 1;
	}

	update_input_ports();

	/* if we didn't find a saved config, return 0 so the main core knows that it */
	/* is the first time the game is run and it should diplay the disclaimer. */
	if (f) return 1;
	else return 0;
}

void save_input_port_settings(void)
{
	void* f;

	save_default_keys();
	LOG_INFO("Saving input port settings (in function)");
	if ((f = osd_fopen(Machine->gamedrv->name, 0, OSD_FILETYPE_CONFIG, 1)) != 0)
	{
		struct InputPort* in;
		int total;

		in = Machine->gamedrv->input_ports;
		/* calculate the size of the array */
		total = 0;
		while (in->type != IPT_END)
		{
			total++;
			in++;
		}

		/* write header */
		osd_fwrite(f, MAMECFGSTRING, 8);
		/* write array size */
		writeint(f, total);
		/* write the original settings as defined in the driver */
		in = Machine->gamedrv->input_ports;
		while (in->type != IPT_END)
		{
			writeip(f, in);
			in++;
		}
		/* write the current settings */
		in = Machine->input_ports;
		while (in->type != IPT_END)
		{
			writeip(f, in);
			in++;
		}
		osd_fclose(f);
	}
}

/* Note that the following 3 routines have slightly different meanings with analog ports */
const char* input_port_name(const struct InputPort* in)
{
	int i, type;

	if (in->name != IP_NAME_DEFAULT) return in->name;

	i = 0;

	if ((in->type & ~IPF_MASK) == IPT_EXTENSION)
		type = (in - 1)->type & (~IPF_MASK | IPF_PLAYERMASK);
	else
		type = in->type & (~IPF_MASK | IPF_PLAYERMASK);

	while (inputport_defaults[i].type != IPT_END &&
		inputport_defaults[i].type != type)
		i++;

	if ((in->type & ~IPF_MASK) == IPT_EXTENSION)
		return inputport_defaults[i + 1].name;
	else
		return inputport_defaults[i].name;
}

int input_port_type_key(int type)
{
	int i;

	i = 0;

	while (inputport_defaults[i].type != IPT_END &&
		inputport_defaults[i].type != type)
		i++;

	return inputport_defaults[i].keyboard;
}

int input_port_type_joy(int type)
{
	int i;

	i = 0;

	while (inputport_defaults[i].type != IPT_END &&
		inputport_defaults[i].type != type)
		i++;

	return inputport_defaults[i].joystick;
}

int input_port_key(const struct InputPort* in)
{
	int i, type;

	while (in->keyboard == IP_KEY_PREVIOUS) in--;

	if ((in->type & ~IPF_MASK) == IPT_EXTENSION)
	{
		type = (in - 1)->type & (~IPF_MASK | IPF_PLAYERMASK);
		/* if port is disabled, or cheat with cheats disabled, return no key */
		if (((in - 1)->type & IPF_UNUSED))//|| (!options.cheat && ((in - 1)->type & IPF_CHEAT)))
			return IP_KEY_NONE;
	}
	else
	{
		type = in->type & (~IPF_MASK | IPF_PLAYERMASK);
		/* if port is disabled, or cheat with cheats disabled, return no key */
		if ((in->type & IPF_UNUSED))//|| (!options.cheat && (in->type & IPF_CHEAT)))
			return IP_KEY_NONE;
	}

	if (in->keyboard != IP_KEY_DEFAULT) return in->keyboard;

	i = 0;

	while (inputport_defaults[i].type != IPT_END &&
		inputport_defaults[i].type != type)
		i++;

	if ((in->type & ~IPF_MASK) == IPT_EXTENSION)
		return inputport_defaults[i + 1].keyboard;
	else
		return inputport_defaults[i].keyboard;
}

int input_port_joy(const struct InputPort* in)
{
	int i, type;

	while (in->joystick == IP_JOY_PREVIOUS) in--;

	if ((in->type & ~IPF_MASK) == IPT_EXTENSION)
	{
		type = (in - 1)->type & (~IPF_MASK | IPF_PLAYERMASK);
		/* if port is disabled, or cheat with cheats disabled, return no joy */
		if (((in - 1)->type & IPF_UNUSED) || ((in - 1)->type & IPF_CHEAT))
			return IP_JOY_NONE;
	}
	else
	{
		type = in->type & (~IPF_MASK | IPF_PLAYERMASK);
		/* if port is disabled, or cheat with cheats disabled, return no joy */
		if ((in->type & IPF_UNUSED) || (in->type & IPF_CHEAT))
			return IP_JOY_NONE;
	}

	if (in->joystick != IP_JOY_DEFAULT) return in->joystick;

	i = 0;

	while (inputport_defaults[i].type != IPT_END &&
		inputport_defaults[i].type != type)
		i++;

	if ((in->type & ~IPF_MASK) == IPT_EXTENSION)
		return inputport_defaults[i + 1].joystick;
	else
		return inputport_defaults[i].joystick;
}

void update_analog_port(int port)
{
	struct InputPort* in;
	int current, delta, type, sensitivity, clip, min, max, default_value;
	int axis, is_stick, check_bounds;
	int inckey, deckey, keydelta, incjoy, decjoy;
	int player;

	/* get input definition */
	in = input_analog[port];

	/* if we're not cheating and this is a cheat-only port, bail */
	if (in->type & IPF_CHEAT) return;
	type = (in->type & ~IPF_MASK);

	deckey = input_port_key(in);
	inckey = input_port_key(in + 1);
	decjoy = input_port_joy(in);
	incjoy = input_port_joy(in + 1);

	keydelta = IP_GET_DELTA(in);

	switch (type)
	{
	case IPT_PADDLE:
		axis = X_AXIS; is_stick = 0; check_bounds = 1; break;
	case IPT_DIAL:
		axis = X_AXIS; is_stick = 0; check_bounds = 0; break;
	case IPT_TRACKBALL_X:
		axis = X_AXIS; is_stick = 0; check_bounds = 0; break;
	case IPT_TRACKBALL_Y:
		axis = Y_AXIS; is_stick = 0; check_bounds = 0; break;
	case IPT_AD_STICK_X:
		axis = X_AXIS; is_stick = 1; check_bounds = 1; break;
	case IPT_AD_STICK_Y:
		axis = Y_AXIS; is_stick = 1; check_bounds = 1; break;
	default:
		/* Use some defaults to prevent crash */
		axis = X_AXIS; is_stick = 0; check_bounds = 0;
		//if (errorlog)
		//	fprintf (errorlog,"Oops, polling non analog device in update_analog_port()????\n");
	}

	sensitivity = IP_GET_SENSITIVITY(in);
	clip = IP_GET_CLIP(in);
	min = IP_GET_MIN(in);
	max = IP_GET_MAX(in);
	default_value = in->default_value * 100 / sensitivity;
	/* extremes can be either signed or unsigned */
	if (min > max)
	{
		if (in->mask > 0xff) min = min - 0x10000;
		else min = min - 0x100;
	}

	input_analog_previous_value[port] = input_analog_current_value[port];

	/* if IPF_CENTER go back to the default position, but without */
	/* throwing away sub-precision movements which might have been done. */
	/* sticks are handled later... */
	if ((in->type & IPF_CENTER) && (!is_stick))
		input_analog_current_value[port] -=
		(input_analog_current_value[port] * sensitivity / 100 - in->default_value) * 100 / sensitivity;

	current = input_analog_current_value[port];

	delta = 0;

	switch (in->type & IPF_PLAYERMASK)
	{
	case IPF_PLAYER2:          player = 1; break;
	case IPF_PLAYER3:          player = 2; break;
	case IPF_PLAYER4:          player = 3; break;
	case IPF_PLAYER1: default: player = 0; break;
	}

	if (axis == X_AXIS)
		delta = mouse_delta_x[player];
	else
		delta = mouse_delta_y[player];

	if (osd_key_pressed(deckey)) delta -= keydelta;
	if (osd_key_pressed(inckey)) delta += keydelta;
	if (osd_joy_pressed(decjoy)) delta -= keydelta;
	if (osd_joy_pressed(incjoy)) delta += keydelta;

	if (clip != 0)
	{
		if (delta * sensitivity / 100 < -clip)
			delta = -clip * 100 / sensitivity;
		else if (delta * sensitivity / 100 > clip)
			delta = clip * 100 / sensitivity;
	}

	if (in->type & IPF_REVERSE) delta = -delta;

	if (is_stick)
	{
		int new1, prev;

		/* center stick */
		if ((delta == 0) && (in->type & IPF_CENTER))
		{
			if (current > default_value)
				delta = -100 / sensitivity;
			if (current < default_value)
				delta = 100 / sensitivity;
		}

		/* An analog joystick which is not at zero position (or has just */
		/* moved there) takes precedence over all other computations */
		/* analog_x/y holds values from -128 to 128 (yes, 128, not 127) */

		if (axis == X_AXIS)
		{
			new1 = analog_current_x[player];
			prev = analog_previous_x[player];
		}
		else
		{
			new1 = analog_current_y[player];
			prev = analog_previous_y[player];
		}

		if ((new1 != 0) || (new1 - prev != 0))
		{
			delta = 0;

			if (in->type & IPF_REVERSE)
			{
				new1 = -new1;
				prev = -prev;
			}

			/* apply sensitivity using a logarithmic scale */
			if (in->mask > 0xff)
			{
				if (new1 > 0)
				{
					current = (int)(pow(new1 / 32768.0, 100.0 / sensitivity) * (max - in->default_value)
						+ in->default_value) * 100 / sensitivity;
				}
				else
				{
					current = (int)(pow(-new1 / 32768.0, 100.0 / sensitivity) * (min - in->default_value)
						+ in->default_value) * 100 / sensitivity;
				}
			}
			else
			{
				if (new1 > 0)
				{
					current = (int)(pow(new1 / 128.0, 100.0 / sensitivity) * (max - in->default_value)
						+ in->default_value) * 100 / sensitivity;
				}
				else
				{
					current = (int)(pow(-new1 / 128.0, 100.0 / sensitivity) * (min - in->default_value)
						+ in->default_value) * 100 / sensitivity;
				}
			}
		}
	}

	current += delta;

	if (check_bounds)
	{
		if (current * sensitivity / 100 < min)
			current = min * 100 / sensitivity;
		if (current * sensitivity / 100 > max)
			current = max * 100 / sensitivity;
	}

	input_analog_current_value[port] = current;
}

void scale_analog_port(int port)
{
	struct InputPort* in;
	int delta, current, sensitivity;

	in = input_analog[port];
	sensitivity = IP_GET_SENSITIVITY(in);

	delta = cpu_scale_by_cycles(input_analog_current_value[port] - input_analog_previous_value[port], Machine->gamedrv->cpu_freq[0]);// Machine->cpu[0].cpu_clock);

	current = input_analog_previous_value[port] + delta;

	input_port_value[port] &= ~in->mask;
	input_port_value[port] |= (current * sensitivity / 100) & in->mask;
}

//////////////////////////////////////////////////////
//Added for AAE Counter and Spulse ///////////////////
//////////////////////////////////////////////////////
int KeyFlip(int keynum, int state)
{
	static int keys[256];
	static int init = 0;
	int i;

	if (init == 0) //INIT
	{
		for (i = 0; i < 256; i++)
		{
			keys[i] = 0;
		}
		init = 1;
	}
	if (!keys[keynum] && state) //Return True if not in que
	{
		keys[keynum] = 1;
		return 1;
	}
	else if (keys[keynum] && !state) //Return False if in que
		keys[keynum] = 0;
	return 0;
}

int Check_Input(int in, int state)
{
	int out = 0;
	int val = in;// = p+in;
	//Is the key already pressed? Check to see if it's time for  keyup
	//LOG_INFO("Check Input Called %d port, %d state",in,state);

	if (state)
	{
		theinput[val].now = 1; //Enable the key

		if (!theinput[val].s2) //Is the latch set? if not, start the countdown.
		{
			//Set latch and start process
			theinput[val].s2 = 5; //Ramp up Time       //5
			theinput[val].s3 = 5; //Final Speed Time   //5
			theinput[val].timer = (3 * theinput[val].s2);
			out = 1; //Key is considered pressed at this point.
		}

		//Check for repeat input (PHASE 2)
		if (theinput[val].s2) //Is this a repeater?
		{
			theinput[val].timer--;
			if (theinput[val].timer < 1)
			{
				//Reset for next round;
				theinput[val].s2--;
				//Cascading counters
				if (theinput[val].s2 < 1) {
					theinput[val].s3 -= 1;
					if (theinput[val].s3 < 1)
					{
						theinput[val].s3 = 1;
					}
					theinput[val].s2 = theinput[val].s3; //Shorten time each pass
				}
				//Reset timer
				theinput[val].timer = (3 * theinput[val].s3);

				out = 1;  //Key is returned press this time.
			}
		}
	}
	else {
		theinput[val].now = 0;
		theinput[val].s2 = 0;
	} //Key is not pressed, make sure variables are clear.

	return out;
}
//////////////////////////////////////////////////////
//END of Additions.                ///////////////////
//////////////////////////////////////////////////////

void update_input_ports(void)
{
	//////////////////////////////////////////////////////
	//Added for AAE Counter and Spulse ///////////////////
	//////////////////////////////////////////////////////
	int set = 0;
	//////////////////////////////////////////////////////
	//END of Additions.                ///////////////////
	//////////////////////////////////////////////////////
	int port, ib;
	struct InputPort* in;
#define MAX_INPUT_BITS 1024
	static int impulsecount[MAX_INPUT_BITS];
	static int waspressed[MAX_INPUT_BITS];
#define MMAX_JOYSTICKS 3
#define MAX_PLAYERS 4
#ifdef MRU_JOYSTICK
	static int update_serial_number = 1;
	static int joyserial[MMAX_JOYSTICKS * MAX_PLAYERS][4];
#else
	int joystick[MMAX_JOYSTICKS * MAX_PLAYERS][4];
#endif

	/* clear all the values before proceeding */
	for (port = 0; port < MAX_INPUT_PORTS; port++)
	{
		input_port_value[port] = 0;
		input_vblank[port] = 0;
		input_analog[port] = 0;
	}

#ifndef MRU_JOYSTICK
	for (i = 0; i < 4 * MMAX_JOYSTICKS * MAX_PLAYERS; i++)
		joystick[i / 4][i % 4] = 0;
#endif

	in = Machine->input_ports;

	if (in->type == IPT_END) return; 	/* nothing to do */

	/* make sure the InputPort definition is correct */
	if (in->type != IPT_PORT)
	{
		LOG_INFO("Error in InputPort definition: expecting PORT_START\n");
		return;
	}
	else in++;

#ifdef MRU_JOYSTICK
	/* scan all the joystick ports */
	port = 0;
	while (in->type != IPT_END && port < MAX_INPUT_PORTS)
	{
		while (in->type != IPT_END && in->type != IPT_PORT)
		{
			if ((in->type & ~IPF_MASK) >= IPT_JOYSTICK_UP &&
				(in->type & ~IPF_MASK) <= IPT_JOYSTICKLEFT_RIGHT)
			{
				int key, joy;

				key = input_port_key(in);
				joy = input_port_joy(in);

				if ((key != 0 && key != IP_KEY_NONE) ||
					(joy != 0 && joy != IP_JOY_NONE))
				{
					int joynum, joydir, player;

					player = 0;
					if ((in->type & IPF_PLAYERMASK) == IPF_PLAYER2)
						player = 1;
					else if ((in->type & IPF_PLAYERMASK) == IPF_PLAYER3)
						player = 2;
					else if ((in->type & IPF_PLAYERMASK) == IPF_PLAYER4)
						player = 3;

					joynum = player * MMAX_JOYSTICKS +
						((in->type & ~IPF_MASK) - IPT_JOYSTICK_UP) / 4;
					joydir = ((in->type & ~IPF_MASK) - IPT_JOYSTICK_UP) % 4;

					if ((key != 0 && key != IP_KEY_NONE && osd_key_pressed(key)) ||
						(joy != 0 && joy != IP_JOY_NONE && osd_joy_pressed(joy)))
					{
						if (joyserial[joynum][joydir] == 0)
							joyserial[joynum][joydir] = update_serial_number;
					}
					else
						joyserial[joynum][joydir] = 0;
				}
			}
			in++;
		}

		port++;
		if (in->type == IPT_PORT) in++;
	}
	update_serial_number += 1;

	in = Machine->input_ports;

	/* already made sure the InputPort definition is correct */
	in++;
#endif

	/* scan all the input ports */
	port = 0;
	ib = 0;
	while (in->type != IPT_END && port < MAX_INPUT_PORTS)
	{
		struct InputPort* start;

		/* first of all, scan the whole input port definition and build the */
		/* default value. I must do it before checking for input because otherwise */
		/* multiple keys associated with the same input bit wouldn't work (the bit */
		/* would be reset to its default value by the second entry, regardless if */
		/* the key associated with the first entry was pressed) */
		start = in;
		while (in->type != IPT_END && in->type != IPT_PORT)
		{
			if ((in->type & ~IPF_MASK) != IPT_DIPSWITCH_SETTING &&	/* skip dipswitch definitions */
				(in->type & ~IPF_MASK) != IPT_EXTENSION)			/* skip analog extension fields */
			{
				input_port_value[port] =
					(input_port_value[port] & ~in->mask) | (in->default_value & in->mask);
			}

			in++;
		}

		/* now get back to the beginning of the input port and check the input bits. */
		for (in = start;
			in->type != IPT_END && in->type != IPT_PORT;
			in++, ib++)
		{
			if ((in->type & ~IPF_MASK) != IPT_DIPSWITCH_SETTING &&	/* skip dipswitch definitions */
				(in->type & ~IPF_MASK) != IPT_EXTENSION)		/* skip analog extension fields */
			{
				if ((in->type & ~IPF_MASK) == IPT_VBLANK)
				{
					input_vblank[port] ^= in->mask;
					input_port_value[port] ^= in->mask;
					//if (errorlog && Machine->drv->vblank_duration == 0)
					//	write_to_log("Warning: you are using IPT_VBLANK with vblank_duration = 0. You need to increase vblank_duration for IPT_VBLANK to work.\n");
				}
				/* If it's an analog control, handle it appropriately */
				else if (((in->type & ~IPF_MASK) > IPT_ANALOG_START)
					&& ((in->type & ~IPF_MASK) < IPT_ANALOG_END)) /* LBO 120897 */
				{
					input_analog[port] = in;
					/* reset the analog port on first access */
					if (input_analog_init[port])
					{
						input_analog_init[port] = 0;
						input_analog_current_value[port] = input_analog_previous_value[port]
							= in->default_value * 100 / IP_GET_SENSITIVITY(in);
					}
				}
				else
				{
					int key, joy;

					key = input_port_key(in);
					joy = input_port_joy(in);

					if ((key != 0 && key != IP_KEY_NONE && osd_key_pressed(key)) ||
						(joy != 0 && joy != IP_JOY_NONE && osd_joy_pressed(joy)))
					{
						/* skip if coin input and it's locked out
						if ((in->type & ~IPF_MASK) >= IPT_COIN1 &&
							(in->type & ~IPF_MASK) <= IPT_COIN4 &&
							coinlockedout[(in->type & ~IPF_MASK) - IPT_COIN1])
						{
							continue;
						}*/

						/* if IPF_RESET set, reset the first CPU */
						if ((in->type & IPF_RESETCPU) && waspressed[ib] == 0) 
								cpu_reset(0);

						if (in->type & IPF_IMPULSE)
						{
							//if (errorlog && IP_GET_IMPULSE(in) == 0)
							//	fprintf(errorlog,"error in input port definition: IPF_IMPULSE with length = 0\n");
							if (waspressed[ib] == 0)
								impulsecount[ib] = IP_GET_IMPULSE(in);
							/* the input bit will be toggled later */
						}
						else if (in->type & IPF_TOGGLE)
						{
							if (waspressed[ib] == 0)
							{
								in->default_value ^= in->mask;
								input_port_value[port] ^= in->mask;
							}
						}
						else if ((in->type & ~IPF_MASK) >= IPT_JOYSTICK_UP &&
							(in->type & ~IPF_MASK) <= IPT_JOYSTICKLEFT_RIGHT)
						{
#ifndef MAME_NET
							int joynum, joydir, mask, player;

							player = 0;
							if ((in->type & IPF_PLAYERMASK) == IPF_PLAYER2) player = 1;
							else if ((in->type & IPF_PLAYERMASK) == IPF_PLAYER3) player = 2;
							else if ((in->type & IPF_PLAYERMASK) == IPF_PLAYER4) player = 3;
#else
							int joynum, joydir, mask;
#endif /* !MAME_NET */
							joynum = player * MMAX_JOYSTICKS +
								((in->type & ~IPF_MASK) - IPT_JOYSTICK_UP) / 4;
							joydir = ((in->type & ~IPF_MASK) - IPT_JOYSTICK_UP) % 4;

							mask = in->mask;

#ifndef MRU_JOYSTICK
							/* avoid movement in two opposite directions */
							if (joystick[joynum][joydir ^ 1] != 0)
								mask = 0;
							else if (in->type & IPF_4WAY)
							{
								int dir;

								/* avoid diagonal movements */
								for (dir = 0; dir < 4; dir++)
								{
									if (joystick[joynum][dir] != 0)
										mask = 0;
								}
							}

							joystick[joynum][joydir] = 1;
#else
							/* avoid movement in two opposite directions */
							if (joyserial[joynum][joydir ^ 1] != 0)
								mask = 0;
							else if (in->type & IPF_4WAY)
							{
								int mru_dir = joydir;
								int mru_serial = 0;
								int dir;

								/* avoid diagonal movements, use mru button */
								for (dir = 0; dir < 4; dir++)
								{
									if (joyserial[joynum][dir] > mru_serial)
									{
										mru_serial = joyserial[joynum][dir];
										mru_dir = dir;
									}
								}

								if (mru_dir != joydir)
									mask = 0;
							}
#endif

							input_port_value[port] ^= mask;
						}
						else
							input_port_value[port] ^= in->mask;

						waspressed[ib] = 1;
					}
					else
						waspressed[ib] = 0;

					//////////////////////////////////////////////////////
					//Added for AAE Counter and Spulse ///////////////////
					//////////////////////////////////////////////////////

					if (in->type & IPF_COUNTER)
					{
						set = Check_Input(ib, waspressed[ib]);
						if (waspressed[ib] != set)
						{
							input_port_value[port] ^= in->mask;
							waspressed[ib] = set;
						}
					}

					if (in->type & IPF_SPULSE)
					{
						set = KeyFlip(ib, waspressed[ib]);
						if (waspressed[ib] != set)
						{
							input_port_value[port] ^= in->mask;
							waspressed[ib] = set;
						}
					}

					//////////////////////////////////////////////////////
					//END of Additions.                ///////////////////
					//////////////////////////////////////////////////////

					if ((in->type & IPF_IMPULSE) && impulsecount[ib] > 0)
					{
						impulsecount[ib]--;
						waspressed[ib] = 1;
						input_port_value[port] ^= in->mask;
					}
				}
			}
		}

		port++;
		if (in->type == IPT_PORT) in++;
	}
}

/* used the the CPU interface to notify that VBlank has ended, so we can update */
/* IPT_VBLANK input ports. */
void inputport_vblank_end(void)
{
	int port;
	int i;

	for (port = 0; port < MAX_INPUT_PORTS; port++)
	{
		if (input_vblank[port])
		{
			input_port_value[port] ^= input_vblank[port];
			input_vblank[port] = 0;
		}
	}

	/* poll all the analog joysticks */
	osd_poll_joysticks();

	/* update the analog devices */
	for (i = 0; i < OSD_MAX_JOY_ANALOG; i++)
	{
		/* update the analog joystick position */
		analog_previous_x[i] = analog_current_x[i];
		analog_previous_y[i] = analog_current_y[i];
		osd_analogjoy_read(i, &(analog_current_x[i]), &(analog_current_y[i]));

		/* update mouse/trackball position */
		osd_trak_read(i, &mouse_delta_x[i], &mouse_delta_y[i]);
	}

	for (i = 0; i < MAX_INPUT_PORTS; i++)
	{
		struct InputPort* in;

		in = input_analog[i];
		if (in)
		{
			update_analog_port(i);
		}
	}
}

int readinputport(int port)
{
	struct InputPort* in;

	/* Update analog ports on demand */
	in = input_analog[port];
	if (in)
	{
		scale_analog_port(port);
	}

	return input_port_value[port];
}

int port_tag_to_index(const char* tag)
{
	int port;

	/* find the matching tag */
	for (port = 0; port < MAX_INPUT_PORTS; port++)
	{
		//LOG_INFO("INPUT PORT NAME %s",input_port_tag[port]);
		if (input_port_tag[port] != NULL && !strcmp(input_port_tag[port], tag))
			return port;
	}
	return -1;
}

UINT32 readinputportbytag(const char* tag)
{
	int port = port_tag_to_index(tag);

	if (port != -1)
	{
		//LOG_INFO("Returning INPUT PORT NAME %s", input_port_tag[port]);
		return readinputport(port);
	}
	/* otherwise fail horribly */
	//osd_die("Unable to locate input port '%s'", tag);
	LOG_INFO("Unable to locate input port '%s' ------->>>>>>>CRAP", tag);
	return -1;
}

int input_port_0_r(int offset) { return readinputport(0); }
int input_port_1_r(int offset) { return readinputport(1); }
int input_port_2_r(int offset) { return readinputport(2); }
int input_port_3_r(int offset) { return readinputport(3); }
int input_port_4_r(int offset) { return readinputport(4); }
int input_port_5_r(int offset) { return readinputport(5); }
int input_port_6_r(int offset) { return readinputport(6); }
int input_port_7_r(int offset) { return readinputport(7); }
int input_port_8_r(int offset) { return readinputport(8); }
int input_port_9_r(int offset) { return readinputport(9); }
int input_port_10_r(int offset) { return readinputport(10); }
int input_port_11_r(int offset) { return readinputport(11); }
int input_port_12_r(int offset) { return readinputport(12); }
int input_port_13_r(int offset) { return readinputport(13); }
int input_port_14_r(int offset) { return readinputport(14); }
int input_port_15_r(int offset) { return readinputport(15); }

//Added for ease of compatibility to aae
UINT8 ip_port_0_r(UINT32 address, struct MemoryReadByte* psMemRead) { return readinputport(0); }
UINT8 ip_port_1_r(UINT32 address, struct MemoryReadByte* psMemRead) { return readinputport(1); }
UINT8 ip_port_2_r(UINT32 address, struct MemoryReadByte* psMemRead) { return readinputport(2); }
UINT8 ip_port_3_r(UINT32 address, struct MemoryReadByte* psMemRead) { return readinputport(3); }
UINT8 ip_port_4_r(UINT32 address, struct MemoryReadByte* psMemRead) { return readinputport(4); }
UINT8 ip_port_5_r(UINT32 address, struct MemoryReadByte* psMemRead) { return readinputport(5); }
UINT8 ip_port_6_r(UINT32 address, struct MemoryReadByte* psMemRead) { return readinputport(6); }
UINT8 ip_port_7_r(UINT32 address, struct MemoryReadByte* psMemRead) { return readinputport(7); }
UINT8 ip_port_8_r(UINT32 address, struct MemoryReadByte* psMemRead) { return readinputport(8); }
UINT8 ip_port_9_r(UINT32 address, struct MemoryReadByte* psMemRead) { return readinputport(9); }
UINT8 ip_port_10_r(UINT32 address, struct MemoryReadByte* psMemRead) { return readinputport(10); }
UINT8 ip_port_11_r(UINT32 address, struct MemoryReadByte* psMemRead) { return readinputport(11); }
UINT8 ip_port_12_r(UINT32 address, struct MemoryReadByte* psMemRead) { return readinputport(12); }
UINT8 ip_port_13_r(UINT32 address, struct MemoryReadByte* psMemRead) { return readinputport(13); }
UINT8 ip_port_14_r(UINT32 address, struct MemoryReadByte* psMemRead) { return readinputport(14); }
UINT8 ip_port_15_r(UINT32 address, struct MemoryReadByte* psMemRead) { return readinputport(15); }

UINT16 ip_port_0_word_r(UINT32 address, struct MemoryReadWord* psMemRead)
{
	return readinputport(0);
}

UINT16 ip_port_1_word_r(UINT32 address, struct MemoryReadWord* psMemRead)
{
	return readinputport(1);
}
UINT16 ip_port_2_word_r(UINT32 address, struct MemoryReadWord* psMemRead)
{
	return readinputport(2);
}
UINT16 ip_port_3_word_r(UINT32 address, struct MemoryReadWord* psMemRead)
{
	return readinputport(3);
}
UINT16 ip_port_4_word_r(UINT32 address, struct MemoryReadWord* psMemRead)
{
	return readinputport(4);
}
UINT16 ip_port_5_word_r(UINT32 address, struct MemoryReadWord* psMemRead)
{
	return readinputport(5);
}