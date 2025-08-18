#ifndef INPTPORT_H
#define INPTPORT_H
#include "osdepend.h"
#include "aae_mame_driver.h"
#include "osd_cpu.h"

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
// THE CODE BELOW IS FROM MAME and COPYRIGHT the MAME TEAM.  
//============================================================================

// -----------------------------------------------------------------------------
// Legacy MAME-Derived Module
// This file contains code originally developed as part of the M.A.M.E.� Project.
// Portions of this file remain under the copyright of the original MAME authors
// and contributors. It has since been adapted and merged into the AAE (Another
// Arcade Emulator) project, with modernizations and enhancements applied.
//
// Integration:
//   This module is now part of the **AAE (Another Arcade Emulator)** codebase
//   and is integrated with its rendering, input, and emulation subsystems.
//
// Licensing Notice:
//   - Original portions of this code remain � the M.A.M.E.� Project and its
//     respective contributors under their original terms of distribution.
//   - Modifications, enhancements, and new code are � 2025 Tim Cottrill and
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
//   This file is originally part of and copyright the M.A.M.E.� Project.
//   For more information about MAME licensing, see the original MAME source
//   distribution and its associated license files.
//
// -----------------------------------------------------------------------------

//Updated with Custom Counter and Single Pulse Key Settings for the GUI

/***************************************************************************

Don't confuse this with the I/O ports in memory.h. This is used to handle game
inputs (joystick, coin slots, etc). Typically, you will read them using
input_port_[n]_r(), which you will associate to the appropriate memory
address or I/O port.

***************************************************************************/
struct InputPort
{
	UINT16 mask;			/* bits affected */
	UINT16 default_value;	/* default value for the bits affected */
	/* you can also use one of the IP_ACTIVE defines below */
	UINT32 type;			/* see defines below */
	const char* name;		/* name to display */
	UINT16 keyboard;		/* key affecting the input bits */
	UINT16 joystick;		/* joystick command affecting the input bits */
	const char* tag;        /* used to tag PORT_START declarations */
};

struct ipd
{
	UINT32 type;
	const char* name;
	UINT16 keyboard;
	UINT16 joystick;
};

#define IP_ACTIVE_HIGH 0x0000
#define IP_ACTIVE_LOW 0xffff

extern struct ipd inputport_defaults[];

enum {
	IPT_END = 1, IPT_PORT,
	/* use IPT_JOYSTICK for panels where the player has one single joystick */
	IPT_JOYSTICK_UP, IPT_JOYSTICK_DOWN, IPT_JOYSTICK_LEFT, IPT_JOYSTICK_RIGHT,
	/* use IPT_JOYSTICKLEFT and IPT_JOYSTICKRIGHT for dual joystick panels */
	IPT_JOYSTICKRIGHT_UP, IPT_JOYSTICKRIGHT_DOWN, IPT_JOYSTICKRIGHT_LEFT, IPT_JOYSTICKRIGHT_RIGHT,
	IPT_JOYSTICKLEFT_UP, IPT_JOYSTICKLEFT_DOWN, IPT_JOYSTICKLEFT_LEFT, IPT_JOYSTICKLEFT_RIGHT,
	IPT_BUTTON1, IPT_BUTTON2, IPT_BUTTON3, IPT_BUTTON4,	/* action buttons */
	IPT_BUTTON5, IPT_BUTTON6, IPT_BUTTON7, IPT_BUTTON8, IPT_BUTTON9, IPT_BUTTON10,

	/* analog inputs */
	/* the "arg" field contains the default sensitivity expressed as a percentage */
	/* (100 = default, 50 = half, 200 = twice) */
	IPT_ANALOG_START,
	IPT_PADDLE, IPT_DIAL, IPT_TRACKBALL_X, IPT_TRACKBALL_Y, IPT_AD_STICK_X, IPT_AD_STICK_Y,
	IPT_ANALOG_END,

	IPT_COIN1, IPT_COIN2, IPT_COIN3, IPT_COIN4,	/* coin slots */
	IPT_START1, IPT_START2, IPT_START3, IPT_START4,	/* start buttons */
	IPT_SERVICE, IPT_TILT,
	IPT_DIPSWITCH_NAME, IPT_DIPSWITCH_SETTING,
	/* Many games poll an input bit to check for vertical blanks instead of using */
	/* interrupts. This special value allows you to handle that. If you set one of the */
	/* input bits to this, the bit will be inverted while a vertical blank is happening. */
	IPT_VBLANK,
	IPT_UNKNOWN,
	IPT_EXTENSION,	/* this is an extension on the previous InputPort, not a real inputport. */
	/* It is used to store additional parameters for analog inputs */

/* the following are special codes for user interface handling - not to be used by drivers! */
IPT_UI_PAUSE,
//////////////////////////////////////////////////////
//Added for AAE GUI ///////////////////
//////////////////////////////////////////////////////
IPT_EXIT
//////////////////////////////////////////////////////
//END of Additions.                ///////////////////
//////////////////////////////////////////////////////
};

#define IPT_UNUSED     IPF_UNUSED

#define IPF_MASK       0xffffff00
#define IPF_UNUSED     0x80000000	/* The bit is not used by this game, but is used */
/* by other games running on the same hardware. */
/* This is different from IPT_UNUSED, which marks */
/* bits not connected to anything. */
#define IPF_COCKTAIL   IPF_PLAYER2	/* the bit is used in cocktail mode only */

#define IPF_CHEAT      0x40000000	/* Indicates that the input bit is a "cheat" key */
/* (providing invulnerabilty, level advance, and */
/* so on). MAME will not recognize it when the */
/* -nocheat command line option is specified. */

#define IPF_PLAYERMASK 0x00030000	/* use IPF_PLAYERn if more than one person can */
#define IPF_PLAYER1    0         	/* play at the same time. The IPT_ should be the same */
#define IPF_PLAYER2    0x00010000	/* for all players (e.g. IPT_BUTTON1 | IPF_PLAYER2) */
#define IPF_PLAYER3    0x00020000	/* IPF_PLAYER1 is the default and can be left out to */
#define IPF_PLAYER4    0x00030000	/* increase readability. */

#define IPF_8WAY       0         	/* Joystick modes of operation. 8WAY is the default, */
#define IPF_4WAY       0x00080000	/* it prevents left/right or up/down to be pressed at */
#define IPF_2WAY       0         	/* the same time. 4WAY prevents diagonal directions. */
/* 2WAY should be used for joysticks wich move only */
/* on one axis (e.g. Battle Zone) */

#define IPF_IMPULSE    0x00100000	/* When this is set, when the key corrisponding to */
/* the input bit is pressed it will be reported as */
/* pressed for a certain number of video frames and */
/* then released, regardless of the real status of */
/* the key. This is useful e.g. for some coin inputs. */
/* The number of frames the signal should stay active */
/* is specified in the "arg" field. */

//////////////////////////////////////////////////////
//Added for AAE Counter and Spulse ///////////////////
//////////////////////////////////////////////////////
#define IPF_COUNTER    0x04000000	// Countdown Timer Key Input
#define IPF_SPULSE     0x08000000   // SINGLE Pulse Setting for Key Input
//////////////////////////////////////////////////////
//END of Additions.                ///////////////////
//////////////////////////////////////////////////////

#define IPF_TOGGLE     0x00200000	/* When this is set, the key acts as a toggle - press */
/* it once and it goes on, press it again and it goes off. */
/* useful e.g. for some Test Mode dip switches. */
#define IPF_REVERSE    0x00400000	/* By default, analog inputs like IPT_TRACKBALL increase */
/* when going right/up. This flag inverts them. */

#define IPF_CENTER     0x00800000	/* always preload in->default, autocentering the STICK/TRACKBALL */

#define IPF_CUSTOM_UPDATE 0x01000000 /* normally, analog ports are updated when they are accessed. */
/* When this flag is set, they are never updated automatically, */
/* it is the responsibility of the driver to call */
/* update_analog_port(int port). */

#define IPF_RESETCPU   0x02000000	/* when the key is pressed, reset the first CPU */

/* The "arg" field contains 4 bytes fields */
#define IPF_SENSITIVITY(percent)	((percent & 0xff) << 8)
#define IPF_DELTA(val)				((val & 0xff) << 16)
#define IPF_CLIP(clip)				((clip & 0xff) << 24)

#define IP_GET_IMPULSE(port) (((port)->type >> 8) & 0xff)
#define IP_GET_SENSITIVITY(port) ((((port)+1)->type >> 8) & 0xff)
#define IP_SET_SENSITIVITY(port,val) ((port)+1)->type = ((port+1)->type & 0xffff00ff)|((val&0xff)<<8)
#define IP_GET_DELTA(port) ((((port)+1)->type >> 16) & 0xff)
#define IP_SET_DELTA(port,val) ((port)+1)->type = ((port+1)->type & 0xff00ffff)|((val&0xff)<<16)
#define IP_GET_CLIP(port) ((((port)+1)->type >> 24) & 0xff)
#define IP_GET_MIN(port) (((port)+1)->mask)
#define IP_GET_MAX(port) (((port)+1)->default_value)

//#define PORT_START_TAG(tag_) { 0, 0, IPT_PORTTAG, tag_ },//	port->start.tag = (tag_)
//#define PORT_START_TAG(tag_) (((port)->tag = (tag_)
#define IP_NAME_DEFAULT ((const char *)-1)

#define IP_KEY_DEFAULT	0xffff
#define IP_KEY_NONE		0xfffe
#define IP_KEY_PREVIOUS	0xfffd	/* use the same key as the previous input bit */

#define IP_JOY_DEFAULT	0xffff
#define IP_JOY_NONE		0xfffe
#define IP_JOY_PREVIOUS	0xfffd	/* use the same joy as the previous input bit */

/* start of table */
#define INPUT_PORTS_START(name) static struct InputPort input_ports_##name[] = {
/* end of table */
#define INPUT_PORTS_END { 0, 0, IPT_END, 0, 0, 0 ,0} };
/* start of a new input port */
#define PORT_START(tag_) { 0, 0, IPT_PORT, 0, 0, 0 ,tag_ },
//#define PORT_START { 0, 0, IPT_PORT, 0, 0, 0 },

/* input bit definition */
#define PORT_BIT(mask,default,type) { mask, default, type, IP_NAME_DEFAULT, IP_KEY_DEFAULT, IP_JOY_DEFAULT },
/* impulse input bit definition */
#define PORT_BIT_IMPULSE(mask,default,type,duration) { mask, default, \
		type | IPF_IMPULSE | ((duration & 0xff) << 8), IP_NAME_DEFAULT, IP_KEY_DEFAULT, IP_JOY_DEFAULT },
/* input bit definition with extended fields */
#define PORT_BITX(mask,default,type,name,key,joy) { mask, default, type, name, key, joy },
/* analog input */
#define PORT_ANALOG(mask,default,type,sensitivity,delta,clip,min,max) \
	{ mask, default, type, IP_NAME_DEFAULT, IP_KEY_DEFAULT, IP_JOY_DEFAULT }, \
	{ min, max, IPT_EXTENSION | IPF_SENSITIVITY(sensitivity) | IPF_DELTA(delta) | IPF_CLIP(clip), \
			IP_NAME_DEFAULT, IP_KEY_DEFAULT, IP_JOY_DEFAULT },
/* analog input with extended fields for defining default keys & sensitivities */
#define PORT_ANALOGX(mask,default,type,sensitivity,delta,clip,min,max,keydec,keyinc,joydec,joyinc) \
	{ mask, default, type, IP_NAME_DEFAULT, keydec, joydec }, \
	{ min, max, IPT_EXTENSION | IPF_SENSITIVITY(sensitivity) | IPF_DELTA(delta) | IPF_CLIP(clip), \
			IP_NAME_DEFAULT, keyinc, joyinc },

#define PORT_SERVICE(mask,default)	\
	PORT_BITX(    mask, mask & default, IPT_DIPSWITCH_NAME | IPF_TOGGLE, DEF_STR( Service_Mode ), OSD_KEY_F2, IP_JOY_NONE )	\
	PORT_DIPSETTING(    mask & default, DEF_STR( Off ) )	\
	PORT_DIPSETTING(    mask &~default, DEF_STR( On ) )

/* dip switch definition */
#define PORT_DIPNAME(mask,default,name) { mask, default, IPT_DIPSWITCH_NAME, name, IP_KEY_NONE, IP_JOY_NONE },
#define PORT_DIPSETTING(default,name) { 0, default, IPT_DIPSWITCH_SETTING, name, IP_KEY_NONE, IP_JOY_NONE },

#define MAX_DEFSTR_LEN 20
extern char ipdn_defaultstrings[][MAX_DEFSTR_LEN];

enum {
	STR_Off,
	STR_On,
	STR_No,
	STR_Yes,
	STR_Lives,
	STR_Bonus_Life,
	STR_Difficulty,
	STR_Demo_Sounds,
	STR_Coinage,
	STR_Coin_A,
	STR_Coin_B,
	STR_9C_1C,
	STR_8C_1C,
	STR_7C_1C,
	STR_6C_1C,
	STR_5C_1C,
	STR_4C_1C,
	STR_3C_1C,
	STR_2C_1C,
	STR_3C_2C,
	STR_4C_3C,
	STR_1C_1C,
	STR_3C_4C,
	STR_2C_3C,
	STR_1C_2C,
	STR_2C_5C,
	STR_1C_3C,
	STR_1C_4C,
	STR_1C_5C,
	STR_1C_6C,
	STR_1C_7C,
	STR_1C_8C,
	STR_1C_9C,
	STR_Free_Play,
	STR_Cabinet,
	STR_Upright,
	STR_Cocktail,
	STR_Flip_Screen,
	STR_Service_Mode,
	STR_Pause,
	STR_Test,
	STR_Tilt,
	STR_Version,
	STR_Region,
	STR_International,
	STR_Japan,
	STR_USA,
	STR_Europe,
	STR_Asia,
	STR_World,
	STR_Hispanic,
	STR_Language,
	STR_English,
	STR_Japanese,
	STR_German,
	STR_French,
	STR_Italian,
	STR_Spanish,
	STR_Very_Easy,
	STR_Easy,
	STR_Normal,
	STR_Medium,
	STR_Hard,
	STR_Harder,
	STR_Hardest,
	STR_Very_Hard,
	STR_Very_Low,
	STR_Low,
	STR_High,
	STR_Higher,
	STR_Highest,
	STR_Very_High,
	STR_Players,
	STR_Controls,
	STR_Dual,
	STR_Single,
	STR_Game_Time,
	STR_Continue_Price,
	STR_Controller,
	STR_Light_Gun,
	STR_Joystick,
	STR_Trackball,
	STR_Continues,
	STR_Allow_Continue,
	STR_Level_Select,
	STR_Infinite,
	STR_Stereo,
	STR_Mono,
	STR_Unused,
	STR_Unknown,
	STR_TOTAL
};

#define DEF_STR(str_num) (ipdn_defaultstrings[STR_##str_num])

#define MAX_INPUT_PORTS 16

int load_input_port_settings(void);
void save_input_port_settings(void);

const char* input_port_name(const struct InputPort* in);
int input_port_type_key(int type);
int input_port_type_joy(int type);
int input_port_key(const struct InputPort* in);
int input_port_joy(const struct InputPort* in);

void update_analog_port(int port);
void update_input_ports(void);	/* called by cpuintrf.c - not for external use */
void inputport_vblank_end(void);	/* called by cpuintrf.c - not for external use */

UINT32 readinputportbytag(const char* tag);
int port_tag_to_index(const char* tag);

UINT8 ip_port_0_r(UINT32 address, struct MemoryReadByte* psMemRead);
UINT8 ip_port_1_r(UINT32 address, struct MemoryReadByte* psMemRead);
UINT8 ip_port_2_r(UINT32 address, struct MemoryReadByte* psMemRead);
UINT8 ip_port_3_r(UINT32 address, struct MemoryReadByte* psMemRead);
UINT8 ip_port_4_r(UINT32 address, struct MemoryReadByte* psMemRead);
UINT8 ip_port_5_r(UINT32 address, struct MemoryReadByte* psMemRead);

UINT16 ip_port_0_word_r(UINT32 address, struct MemoryReadWord* psMemRead);
UINT16 ip_port_1_word_r(UINT32 address, struct MemoryReadWord* psMemRead);
UINT16 ip_port_2_word_r(UINT32 address, struct MemoryReadWord* psMemRead);
UINT16 ip_port_3_word_r(UINT32 address, struct MemoryReadWord* psMemRead);
UINT16 ip_port_4_word_r(UINT32 address, struct MemoryReadWord* psMemRead);
UINT16 ip_port_5_word_r(UINT32 address, struct MemoryReadWord* psMemRead);

int readinputport(int port);
int input_port_0_r(int offset);
int input_port_1_r(int offset);
int input_port_2_r(int offset);
int input_port_3_r(int offset);
int input_port_4_r(int offset);
int input_port_5_r(int offset);
int input_port_6_r(int offset);
int input_port_7_r(int offset);
int input_port_8_r(int offset);
int input_port_9_r(int offset);
int input_port_10_r(int offset);
int input_port_11_r(int offset);
int input_port_12_r(int offset);
int input_port_13_r(int offset);
int input_port_14_r(int offset);
int input_port_15_r(int offset);


#endif
