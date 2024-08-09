#include "aae_mame_driver.h"
#include "allegro.h"

//Video VARS
int sx, ex, sy, ey;

FILE* errorlog;

//RAM Variables
unsigned char* membuffer;
unsigned char vec_ram[0x4000];
unsigned char* GI[5]; //Global 6502/Z80/6809 GameImage

int art_loaded[6];

//TEMPORARY GRAPHICS GLOBALS
int msx, msy, esx, esy; //Main full screen adjustments for debug
int b1sx, b1sy, b2sx, b2sy; //bezel full screen adjustments
float bezelzoom;
int bezelx;
int bezely;
float overalpha;
struct game_rect GameRect;

//GLOBAL AUDIO VARIABLES
int gammaticks; //Needed for Pokey Sound for Major Havoc
SAMPLE* game_sounds[60]; //Global Samples
int chip;  //FOR POKEY
int gain;  //FOR POKEY
int BUFFER_SIZE;  //FOR POKEY
AUDIOSTREAM* stream; //Global Streaming Sound 1
AUDIOSTREAM* stream2; //Global Streaming Sound 2
unsigned char* soundbuffer;
signed char* aybuffer;

int in_gui;
unsigned int frames; //Global Framecounter
int frameavg;
int testsw; //testswitch for many games

// Shared variable for GUI

int gamenum; //Global Gamenumber (really need this one)
int have_error; //Global Error handler
int showinfo; //Global info handler
int done; //End of emulation indicator
int paused; //Paused indicator
double fps_count; //FPS Counter
//int showfps;   //ShowFPS Toggle
//int show_menu; //ShowMenu Toggle

//int gamefps; //GAME REQUIRED FPS
int num_games; //Total number of games ?? needed?
int num_samples; //Total number of samples for selected game

//KEY VARIABLES
int mouseb[5];
int WATCHDOG;
//int menulevel;//Top Level
//int menuitem; //TOP VAL
//int key_set_flag;
int total_length;

//CPU Contexts
CONTEXTM6502* c6502[MAX_CPU];
CONTEXTMZ80 cMZ80[MAX_CPU];
struct S68000CONTEXT c68k[MAX_CPU];

settings config;
int index;	/* avoid a common "shadows global declaration" warning in the DOS build */

//GAMEKEYS* MK;
//GAMEKEYS* GK;
//GAMEKEYS* FOO;
glist gamelist[256];