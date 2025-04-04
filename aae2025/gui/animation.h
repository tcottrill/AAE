//#include "aaemain.h"

#ifndef ANIMATION_H
#define ANIMATION_H

typedef struct star_struct STAR_STRUCT;

struct star_struct
{
	int x;
	int y;
	int r;
	int g;
	int b;
	int speed;
	int blinkrate;
	int blink;
};

extern STAR_STRUCT stars[255];

void fillstars(STAR_STRUCT stars[]);
void movestars(STAR_STRUCT stars[]);
void placestars(STAR_STRUCT stars[]);
void clearstars(STAR_STRUCT stars[]);

#endif 