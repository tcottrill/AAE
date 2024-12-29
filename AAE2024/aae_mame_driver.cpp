//= ========================================================================== =
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
//struct game_rect GameRect;

int in_gui_sentinel;

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



settings config;
//int index;	

glist gamelist[256];