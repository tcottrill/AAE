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




FILE* errorlog;
int logging;


int art_loaded[6];

//TEMPORARY GRAPHICS GLOBALS
//Video VARS
int sx, ex, sy, ey;
int msx, msy, esx, esy; //Main full screen adjustments for debug
int b1sx, b1sy, b2sx, b2sy; //bezel full screen adjustments
float bezelzoom;
int bezelx;
int bezely;


int in_gui;
unsigned int frames; //Global Framecounter
int frameavg;

int gamenum; //Global Gamenumber (really need this one)
int have_error; //Global Error handler
int showinfo; //Global info handler
int done; //End of emulation indicator
int paused; //Paused indicator
double fps_count; //FPS Counter
int num_games; //Total number of games ?? needed?
int num_samples; //Total number of samples for selected game

//KEY VARIABLES
int mouseb[5]; // Only used by the gui and Tempest
// Currrently  used by Major Havoc and Asteroids Video.
int total_length;

settings config;

