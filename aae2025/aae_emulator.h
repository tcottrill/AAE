#pragma once

void emulator_init(int argc, char** argv);
void emulator_run();
void emulator_end();



typedef struct {
	int next; // index of next entry in array
	int prev; // previous entry (if double-linked)
	int  gamenum; 		//Short Name of game
	char glname[128];	    //Display name for Game
	int extopt;   //Any extra options for each game
	//int numbertag;
} glist;                      //Only one gamelist at a time

extern glist gamelist[256];

