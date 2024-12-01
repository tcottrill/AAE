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

#include <allegro.h>

//Note: Move these to cpu_control.h

#include "starcpu.h"
#include "log.h"
#include "inptport.h"
#include "cpu_control.h"
#include "mame_fileio.h"
#include "aae_fileio.h"
#include "acommon.h"
#include "osdepend.h"
#include "config.h"

extern FILE* errorlog;


#define MAX_SOUND 4
#define MAX_MEMORY_REGIONS 10

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

//CPU and WRITE READ HANDLERS WHY ARE THESE HERE!!!!
#define READ_HANDLER(name)  static UINT8 name(UINT32 address, struct MemoryReadByte *psMemRead)
#define WRITE_HANDLER(name)  static void name(UINT32 address, UINT8 data, struct MemoryWriteByte *psMemWrite)
#define MEM_WRITE(name) struct MemoryWriteByte name[] = {
#define MEM_READ(name)  struct MemoryReadByte name[] = {
#define MEM_ADDR(start,end,routine) {start,end,routine},
#define MEM_END {(UINT32) -1,(UINT32) -1,NULL}};

//CPU PORT HANDLERS FOR the Z80 AGAIN, WHY ARE THESE HERE?
#define PORT_WRITE_HANDLER(name) static void name(UINT16 port, UINT8 data, struct z80PortWrite *pPW)
#define PORT_READ_HANDLER(name) static UINT16 name(UINT16 port, struct z80PortRead *pPR)
#define PORT_WRITE(name) struct z80PortWrite name[] = {
#define PORT_READ(name) struct z80PortRead name[] = {
#define PORT_ADDR(start,end,routine) {start,end,routine},
#define PORT_END {(UINT16) -1, (UINT16) -1,NULL}};

////////////////////////////////
//PALETTE SETTINGS
#define VEC_BW_BI  0
#define VEC_BW_16  1
#define VEC_BW_64  2
#define VEC_BW_256 3
#define VEC_COLOR  4
#define RASTER_32  5

// STRUCTS AND GLOBAL VARIABLES START HERE

/*
void swap(int* x, int* y) {
	int temp = *x;
	*x = *y;
	*y = temp;
}
*/
struct gamerect
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

typedef struct {
	int next; // index of next entry in array
	int prev; // previous entry (if double-linked)
	int  gamenum; 		//Short Name of game
	char glname[128];	    //Display name for Game
	int extopt;   //Any extra options for each game
	//int numbertag;
} glist;                      //Only one gamelist at a time

extern glist gamelist[256];
/*
struct MachineCPU
{
	int cpu_type;	
	int cpu_clock;	
	int memory_region;
	struct MemoryReadByte* memory_read;
	struct MemoryWriteByte* memory_write;
	struct z80PortRead* port_read;
	struct z80PortWrite* port_write;
	void (*interrupt)(int); //Special Interrupt handler
	int interrupts_cycles;	//Number of cycles (hertz) between reocurring interrupts
};
*/
struct AAEDriver
{
	const char* name;
	const char* desc;
	const struct RomModule* rom;

	int (*init_game)();
	void (*pre_run) (); //Things to set before each CPU run.
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
	const int vid_type;
	const int rotation;
	struct gamerect visible_area;

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
	unsigned char* memory_region[MAX_MEMORY_REGIONS]; //TBD
	//unsigned int memory_region_length[MAX_MEMORY_REGIONS];	/* some drivers might find this useful */
	//struct GfxElement *gfx[MAX_GFX_ELEMENTS];	/* graphic sets (chars, sprites) */
	const struct AAEDriver* gamedrv;	/* contains the definition of the game machine */
	//const struct MachineDriver* drv;	/* same as gamedrv->drv */

	//struct GameSamples* samples;	/* samples loaded from disk */
	struct InputPort* input_ports;	/* the input ports definition from the driver */
	/* is copied here and modified (load settings from disk, */
	/* remove cheat commands, and so on) */
	int orientation;	/* see #defines in driver.h */
	//int vector_width;
	//int vector_height;
	int vectortype;
	//struct myrectangle absolute_visible_area;
	int video_attributes;
};

extern struct RunningMachine* Machine;

//RAM Variables
extern unsigned char* membuffer;
extern unsigned char vec_ram[0x4000];
extern unsigned char* GI[5]; //Global 6502/Z80/6809 GameImage




extern int art_loaded[6];
extern int index;
//TEMPORARY GRAPHICS GLOBALS
extern int msx, msy, esx, esy; //Main full screen adjustments for debug
extern int b1sx, b1sy, b2sx, b2sy; //bezel full screen adjustments
extern float bezelzoom;
extern int bezelx;
extern int bezely;
extern float overalpha;
extern struct game_rect GameRect;

//GLOBAL AUDIO VARIABLES , should be in generic.cpp
extern int gammaticks; //Needed for Pokey Sound for Major Havoc
extern SAMPLE* game_sounds[60]; //Global Samples
extern int chip;  //FOR POKEY
extern int gain;  //FOR POKEY
extern int BUFFER_SIZE;  //FOR POKEY
extern AUDIOSTREAM* stream1; //Global Streaming Sound 1
//extern AUDIOSTREAM* stream2; //Global Streaming Sound 2
extern unsigned char* soundbuffer;
extern signed char* aybuffer;

extern int in_gui;
extern unsigned int frames; //Global Framecounter
extern int frameavg;
extern int testsw; //testswitch for many games

//Shared variable for GUI

extern int gamenum; //Global Gamenumber (really need this one)
extern int have_error; //Global Error handler
extern int showinfo; //Global info handler
extern int done; //End of emulation indicator
extern int paused; //Paused indicator
extern double fps_count; //FPS Counter
//extern int showfps;   //ShowFPS Toggle
//extern int show_menu; //ShowMenu Toggle

//extern int gamefps; //GAME REQUIRED FPS
extern int num_games; //Total number of games ?? needed?
extern int num_samples; //Total number of samples for selected game

//KEY VARIABLES
extern int mouseb[5];
extern int WATCHDOG;
//extern int menulevel;//Top Level
//extern int menuitem; //TOP VAL
//extern int key_set_flag;
extern int total_length;

//Video VARS
extern int sx, ex, sy, ey;

typedef struct {
	char rompath[256];
	char samplepath[256];
	//int ksfps;
	//int kquit;
	int kreset;
	int ktest;
	int ktestadv;
	//int kmenu;

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
	int kbleds;
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
	//Lunar Lander Hardware
	LLANDER1,
	LLANDER,
	//Asteroids Hardware
	METEORTS,
	ASTEROCK,
	ASTEROIB,
	ASTEROI1,
	ASTEROID,
	ASTDELU1,
	ASTDELU2,
	ASTDELUX,
	//Midway Omega Race Hardware
	OMEGRACE,
	DELTRACE,
	//BattleZone Hardware
	BZONE,
	BZONE2,
	BZONEC,
	BZONEP,
	REDBARON,
	BRADLEY,
	//Spacduel Hardware
	SPACDUEL,
	BWIDOW,
	GRAVITAR,
	GRAVITR2,
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
	ALIENST,
	VBREAK,
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
	MHAVOCP,
	ALPHAONE,
	ALPHAONA,

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
	//Quantum Hardware
	QUANTUM1,
	QUANTUM,
	QUANTUMP,
	//Star Wars Hardware
	STARWARS,
	STARWAR1,
	GALAGA
};


#endif