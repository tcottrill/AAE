#pragma once

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

#ifndef GLOBALS_H
#define GLOBALS_H

#include "framework.h"

//Note: Move these to cpu_control.h

#include "log.h"
#include "inptport.h"
#include "cpu_control.h"
#include "mame_fileio.h"
#include "aae_fileio.h"
#include "acommon.h"
#include "osdepend.h"
#include "config.h"
#include "memory.h"
#include "mixer.h"
#include "emu_vector_draw.h"


typedef struct {
	int next; // index of next entry in array
	int prev; // previous entry (if double-linked)
	int  gamenum; 		//Short Name of game
	char glname[128];	    //Display name for Game
	int extopt;   //Any extra options for each game
	//int numbertag;
} glist;                      //Only one gamelist at a time

extern glist gamelist[256];


extern int logging;

extern FILE* errorlog;
extern char* gamename[];
extern int gamenum;

// Raster Defines, new. 
#define MAX_GFX_ELEMENTS 10
#define MAX_MEMORY_REGIONS 16
#define MAX_PENS 256	/* can't handle more than 256 colors on screen */
#define MAX_LAYERS 4	/* MAX_LAYERS is the maximum number of gfx layers */
/* which we can handle. Currently, 4 is enough. */
#define MAX_SOUND 4


#define str_eq(s1,s2)  (!strcmp ((s1),(s2))); //Equate string1 and sring2 true is equal

//#define EPSILON 0.0001   // Define your own tolerance
//#define FLOAT_EQ(x,v) (((v - EPSILON) < x) && (x <( v + EPSILON)))

//Replace this with the correct one.
#define twos_comp_val(num,bits) ((num&(1<<(bits-1)))?(num|~((1<<bits)-1)):(num&((1<<bits)-1)))

#define VECTOR 500
#define RASTER 501

#define CPU0 0
#define CPU1 1
#define CPU2 2
#define CPU3 3
#define CPU4 4

#define FUN_TEX   5
#define ART_TEX  10
#define GAME_TEX 15


#define NORMAL      1
#define FLIP        2
#define RRIGHT      3
#define RLEFT       4
// GENERAL FUNCTION DEFINES
#define SWAP(a, b)  {a ^= b; b ^= a; a ^= b;}

//These should be somewhere else as well. 
#define bitget(p,m) ((p) & (m))
#define bitset(p,m) ((p) |= (m))
#define bitclr(p,m) ((p) &= ~(m))
#define bitflp(p,m) ((p) ^= (m))
#define bit_write(c,p,m) (c ? bit_set(p,m) : bit_clear(p,m))
#define BIT(x) (0x01 << (x))
#define LONGBIT(x) ((unsigned long)0x00000001 << (x))

// These are for different translation units. Maybe bite the bullet and make them all non-static, or wrap in namespaces?
// This is a temporary solution.
#define READ_HANDLER_NS(name)   UINT8 name(UINT32 address, struct MemoryReadByte *psMemRead)
#define WRITE_HANDLER_NS(name)   void name(UINT32 address, UINT8 data, struct MemoryWriteByte *psMemWrite)

//CPU and WRITE READ HANDLERS WHY ARE THESE HERE!!!!
#define READ_HANDLER(name)  static UINT8 name(UINT32 address, struct MemoryReadByte *psMemRead)
#define WRITE_HANDLER(name)  static void name(UINT32 address, UINT8 data, struct MemoryWriteByte *psMemWrite)

#define READ16_HANDLER(name)  static UINT16 name(UINT32 address, struct MemoryReadWord *psMemRead)
#define WRITE16_HANDLER(name)  static void name(UINT32 address, UINT16 data, struct MemoryWriteWord *psMemWrite)

#define MEM_WRITE(name) struct MemoryWriteByte name[] = {
#define MEM_READ(name)  struct MemoryReadByte name[] = {

#define MEM_WRITE16(name) struct MemoryWriteWord name[] = {
#define MEM_READ16(name)  struct MemoryReadWord name[] = {

#define MEM_ADDR(start,end,routine) {start,end,routine},
#define MEM_END {(UINT32) -1,(UINT32) -1,NULL}};

//CPU PORT HANDLERS FOR the Z80 AGAIN, WHY ARE THESE HERE?
#define PORT_WRITE_HANDLER(name) static void name(UINT16 port, UINT8 data, struct z80PortWrite *pPW)
#define PORT_READ_HANDLER(name) static UINT16 name(UINT16 port, struct z80PortRead *pPR)
#define PORT_WRITE(name) struct z80PortWrite name[] = {
#define PORT_READ(name) struct z80PortRead name[] = {
#define PORT_ADDR(start,end,routine) {start,end,routine},
#define PORT_END {(UINT16) -1, (UINT16) -1,NULL}};

/* flags for video_attributes */

/* bit 0 of the video attributes indicates raster or vector video hardware */
#define	VIDEO_TYPE_RASTER			0x0000
#define	VIDEO_TYPE_VECTOR			0x0001

/* bit 1 of the video attributes indicates whether or not dirty rectangles will work */
#define	VIDEO_SUPPORTS_DIRTY		0x0002

/* bit 2 of the video attributes indicates whether or not the driver modifies the palette */
#define	VIDEO_MODIFIES_PALETTE	0x0004
////////////////////////////////
//PALETTE SETTINGS
#define VEC_BW_BI  0
#define VEC_BW_16  1
#define VEC_BW_64  2
#define VEC_BW_256 3
#define VEC_COLOR  4
#define RASTER_32  5

/* values for the flags field */
#define NOT_A_DRIVER			0x4000	/* set by the fake "root" driver_ and by "containers" */
#define GAME_NOT_WORKING		0x0001
#define GAME_WRONG_COLORS		0x0002	/* colors are totally wrong */
#define GAME_IMPERFECT_COLORS	0x0004	/* colors are not 100% accurate, but close */
#define GAME_NO_SOUND			0x0008	/* sound is missing */
#define GAME_IMPERFECT_SOUND	0x0010	/* sound is known to be wrong */
#define VECTOR_USES_OVERLAY1	0x0100  // Blending type 1 overlay
#define VECTOR_USES_OVERLAY2	0x0400  // An overlay that is visible like a gel. 
#define VECTOR_USES_BW				0x1000
#define VECTOR_USES_COLOR			0x2000
#define VECTOR_DEFAULT_SCALE_2		0x4000
#define VECTOR_DEFAULT_SCALE_3		0x8000

#define ORIENTATION_MASK        	0x0007
#define	ORIENTATION_FLIP_X			0x0001	// mirror everything in the X direction 
#define	ORIENTATION_FLIP_Y			0x0002	// mirror everything in the Y direction 
#define ORIENTATION_SWAP_XY			0x0004	// mirror along the top-left/bottom-right diagonal 
//#define VECTOR_USES_COLOR           0x0008

#define	ROT0	0
#define	ROT90	(ORIENTATION_SWAP_XY|ORIENTATION_FLIP_X)	/* rotate clockwise 90 degrees */
#define	ROT180	(ORIENTATION_FLIP_X|ORIENTATION_FLIP_Y)		/* rotate 180 degrees */
#define	ROT270	(ORIENTATION_SWAP_XY|ORIENTATION_FLIP_Y)	/* rotate counter-clockwise 90 degrees */

#define	ORIENTATION_DEFAULT		0x00
#define	ORIENTATION_FLIP_X		0x01	/* mirror everything in the X direction */
#define	ORIENTATION_FLIP_Y		0x02	/* mirror everything in the Y direction */
#define ORIENTATION_SWAP_XY		0x04	/* mirror along the top-left/bottom-right diagonal */
#define	ORIENTATION_ROTATE_90	(ORIENTATION_SWAP_XY|ORIENTATION_FLIP_X)	/* rotate clockwise 90 degrees */
#define	ORIENTATION_ROTATE_180	(ORIENTATION_FLIP_X|ORIENTATION_FLIP_Y)	/* rotate 180 degrees */
#define	ORIENTATION_ROTATE_270	(ORIENTATION_SWAP_XY|ORIENTATION_FLIP_Y)	/* rotate counter-clockwise 90 degrees */
/* IMPORTANT: to perform more than one transformation, DO NOT USE |, use ^ instead. */
/* For example, to rotate 90 degrees counterclockwise and flip horizontally, use: */
/* ORIENTATION_ROTATE_270 ^ ORIENTATION_FLIP_X*/
/* Always remember that FLIP is performed *after* SWAP_XY. */

#define DEFAULT_60HZ_VBLANK_DURATION 0
#define DEFAULT_30HZ_VBLANK_DURATION 0
/* If you use IPT_VBLANK, you need a duration different from 0. */
#define DEFAULT_REAL_60HZ_VBLANK_DURATION 2500
#define DEFAULT_REAL_30HZ_VBLANK_DURATION 2500

/* flags for video_attributes */

/* bit 0 of the video attributes indicates raster or vector video hardware */
#define	VIDEO_TYPE_RASTER			0x0000
#define	VIDEO_TYPE_VECTOR			0x0001

/* bit 1 of the video attributes indicates whether or not dirty rectangles will work */
#define	VIDEO_SUPPORTS_DIRTY		0x0002

/* bit 2 of the video attributes indicates whether or not the driver modifies the palette */
#define	VIDEO_MODIFIES_PALETTE	0x0004

/* ASG 980417 - added: */
/* bit 4 of the video attributes indicates that the driver wants its refresh after */
/*       the VBLANK instead of before. */
#define	VIDEO_UPDATE_BEFORE_VBLANK	0x0000
#define	VIDEO_UPDATE_AFTER_VBLANK	0x0010

// STRUCTS AND GLOBAL VARIABLES START HERE

/*
void swap(int* x, int* y) {
	int temp = *x;
	*x = *y;
	*y = temp;
}
*/
struct rectangle
{
	int min_x, max_x;
	int min_y, max_y;
};

const struct artworks
{
	const char* zipfile;
	const char* filename;
	int type;
	int target;
};


struct AAEDriver
{
	const char* name;
	const char* desc;
	const struct RomModule* rom;

	int (*init_game)();
	//void (*pre_run) (); //Things to set before each CPU run.
	void (*run_game)();
	void (*end_game)();

	struct InputPort* input_ports;

	const char** game_samples;
	const struct artworks* artwork;

	int cpu_type[4];               //6502, etc...
	int cpu_freq[4];               // CPU Frequency
	int cpu_divisions[4];          // Divisions per frame cycle
	int cpu_intpass_per_frame[4];  // Passes from above before interrupt called.(Interrupt Period)
	int cpu_int_type[4];           // Main type of CPU interrupt
	void (*int_cpu[4])(); //Interrupt Handler CPU 0/4

	const int fps;
	const int video_attributes;
	const int rotation;
	int screen_width, screen_height;
	struct rectangle visible_area;
	// Raster code requirements are below.
	struct GfxDecodeInfo* gfxdecodeinfo;
	unsigned int total_colors;	/* palette is 3*total_colors bytes long */
	unsigned int color_table_len;	/* length in shorts of the color lookup table */
	void (*vh_convert_color_prom)(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom);
	//
	int (*hiscore_load)();	// will be called every vblank until it returns nonzero
	void (*hiscore_save)();	// will not be called if hiscore_load() hasn't yet
	// returned nonzero, to avoid saving an invalid table
	int vectorram; //Vectorram start address
	unsigned int vectorram_size;
	void (*nvram_handler)(void* file, int read_or_write);
};

extern struct AAEDriver driver[];

struct RunningMachine
{
	unsigned char* memory_region[MAX_MEMORY_REGIONS]; 
	unsigned int memory_region_length[MAX_MEMORY_REGIONS];	/* some drivers might find this useful */
	
	struct GfxElement* gfx[MAX_GFX_ELEMENTS];	/* graphic sets (chars, sprites) */
	struct osd_bitmap* scrbitmap;	/* bitmap to draw into */
	unsigned char pens[MAX_PENS];	/* remapped palette pen numbers. When you write */
	/* directly to a bitmap, never use absolute values, */
	/* use this array to get the pen number. For example, */
	/* if you want to use color #6 in the palette, use */
	/* pens[6] instead of just 6. */
	unsigned short* colortable;	/* lookup table used to map gfx pen numbers */
	/* to color numbers */
	unsigned short* remapped_colortable;	/* the above, already remapped through */

	const struct AAEDriver* gamedrv;	/* contains the definition of the game machine */
	const struct AAEDriver*drv;	/* same as gamedrv->drv */
	//const struct MachineDriver* drv;	/* same as gamedrv->drv */
	//struct GameSamples* samples;	/* samples loaded from disk */
	struct InputPort* input_ports;	/* the input ports definition from the driver */
	/* is copied here and modified (load settings from disk, */
	/* remove cheat commands, and so on) */
	int orientation;	/* see #defines in driver.h */
	int vectortype;
	//struct myrectangle absolute_visible_area;
	int video_attributes;
};

extern struct RunningMachine* Machine;

extern int art_loaded[6];


//Video VARS
extern int sx, ex, sy, ey;
//TEMPORARY GRAPHICS GLOBALS
extern int msx, msy, esx, esy; //Main full screen adjustments for debug
extern int b1sx, b1sy, b2sx, b2sy; //bezel full screen adjustments
extern float bezelzoom;
extern int bezelx;
extern int bezely;


extern int in_gui;
extern unsigned int frames; //Global Framecounter
extern int frameavg;
//Shared variable for GUI

extern int gamenum; //Global Gamenumber (really need this one)
extern int have_error; //Global Error handler
extern int showinfo; //Global info handler
extern int done; //End of emulation indicator
extern int paused; //Paused indicator
extern double fps_count; //FPS Counter

//extern int gamefps; //GAME REQUIRED FPS
extern int num_games; //Total number of games ?? needed?
extern int num_samples; //Total number of samples for selected game

//KEY VARIABLES
extern int mouseb[5];
extern int total_length;

typedef struct {
	char rompath[256];
	char samplepath[256];
	//int ksfps;
	//int kquit;
	int kreset;
	int ktest;
	int ktestadv;
	int kpause;
	int ksnap;

	int drawzero;
	int widescreen;
	int overlay;
	int colordepth;
	int screenw;
	int screenh;
	int windowed;
	int language;
	int translucent;
	float translevel;
	int lives;

	int m_line;
	int m_point;
	int monitor;

	float linewidth;
	float pointsize;

	int gamma;
	int bright;
	int contrast;
	int gain;
	int fire_point_size;
	int explode_point_size;
	//int colorhack;
	int shotsize;
	int cocktail;
	int mainvol;
	int pokeyvol;
	int artwork;
	int bezel;
	int burnin;
	int artcrop;
	int vid_rotate;
	int vecglow;
	int vectrail;

	int psnoise;
	int hvnoise;
	int pshiss;
	int noisevol;
	int snappng;

	char* aspect;
	int prescale;
	int anisfilter;
	int priority;
	int forcesync;
	int dblbuffer;
	int showinfo; //Show readme info message
	char* exrompath; //optional path for roms
	int hack;
	int debug;
	int debug_profile_code;
	int audio_force_resample;
	int kbleds;
	int samplerate;
}settings;

extern settings config;

struct GameOptions {
	
	int cheat;
	int gui_host;

	int samplerate;
	int samplebits;
	int use_samples;
	int norotate;
	int ror;
	int rol;
	int flipx;
	int flipy;
	int beam;
	int flicker;
	int translucency;
	int antialias;
	int use_artwork;
	int use_overlay;
	int use_bezel;
	int bezel_crop;
	int gl_line_width;
	int vector_flicker;
};

extern struct GameOptions options;


////////////////////////////////
//GAME DEFINES
////////////////////////////////
enum GameDef {
	AAEGUI,
	//Asteroids Hardware
	METEORTS,
	ASTEROCK,
	ASTEROIDB,
	ASTEROID1,
	ASTEROID,
	ASTDELUX1,
	ASTDELUX2,
	ASTDELUX,
	//Lunar Lander Hardware
	LLANDER1,
	LLANDER,
   //Midway Omega Race Hardware
	OMEGRACE,
	DELTRACE,
	//BattleZone Hardware
	BZONE,
	BZONEA,
	BZONEC,
	BZONEP,
	REDBARON,
	BRADLEY,
	
	//Spacduel Hardware
	SPACDUEL,
	SPACDUEL0,
	SPACDUEL1,
	BWIDOW,
	GRAVITAR,
	GRAVITAR2,
	GRAVP,
	LUNARBAT,
	LUNARBA1,
	//Tempest Hardware
	TEMPESTM,
	TEMPEST,
	TEMPEST3,
	TEMPEST2,
	TEMPEST1,
	TEMPTUBE,
	ALIENSV,
	VBRAKOUT,
	VORTEX,
	//Sega G80 Vector Hardware
	ZEKTOR,
	TACSCAN,
	STARTREK,
	SPACFURY,
	SPACFURA,
	SPACFURB,
	ELIM2,
	ELIM2A,
	ELIM2C,
	ELIM4,
	ELIM4P,
	//Major Havoc Hardware
	MHAVOC,
	MHAVOC2,
	MHAVOCRV,
	MHAVOCPE,
	MHAVOCP,
	ALPHAONE,
	ALPHAONEA,
	//Cinematronics Hardware
	SOLARQ,
	STARCAS,
	RIPOFF,
	ARMORA,
	BARRIER,
	SUNDANCE,
	WARRIOR,
	TAILG,
	STARHAWK,
	SPACEWAR,
	SPEEDFRK,
	DEMON,
	BOXINGB,
	WOTW,
	QB3,
	//Quantum Hardware
	QUANTUM1,
	QUANTUM,
	QUANTUMP,
	//Star Wars Hardware
	STARWARS,
	STARWARS1,
	ESB,
	//Cinematronics Hardware
	AZTARAC,
	//Space Invaders Hardware 
	INVADERS,
	INVADDLX,
	// Midway Raster
	RALLYX
};

/* this allows to leave the INIT field empty in the GAME() macro call */
#define init_0 0

extern const struct GameDriver* drivers[];


#endif