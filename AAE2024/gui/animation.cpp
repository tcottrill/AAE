#include <allegro.h>
#include "alleggl.h"
#include "winalleg.h"
#include "animation.h"
#include "aae_mame_driver.h"

STAR_STRUCT stars[255];

int num_stars = 80;
int max_star_speed = 3;

//stars = (STAR_STRUCT *) malloc(num_stars * sizeof(STAR_STRUCT));//init stars
void clearstars(STAR_STRUCT stars[])
{
	int i;
	for (i = 0; i <= num_stars - 1; i++)
	{
		//Erase Old
		//circlefill(tempbg, stars[i].x,  stars[i].y ,1,0);
		//Draw New Location  + stars[i].speed
	}
}

void placestars(STAR_STRUCT stars[])
{
	int i;

	for (i = 0; i <= num_stars - 1; i++)
	{
		glPointSize(3);
		glColor4ub(stars[i].r, stars[i].g, stars[i].b, 255);
		glBegin(GL_POINTS);
		glVertex2i(stars[i].x, stars[i].y);
		glEnd();
		glPointSize(config.pointsize); //RESET ORIG POINTSIZE
	}
}

void movestars(STAR_STRUCT stars[])
{
	int i;
	for (i = 0; i <= num_stars - 1; i++)
	{
		stars[i].y -= stars[i].speed;
		if (stars[i].y < 0) {
			stars[i].y = 768;
		}
	}
}

void fillstars(STAR_STRUCT stars[])
{
	int i, r;

	for (i = 0; i <= num_stars - 1; i++)
	{
		stars[i].y = rand() % 768; stars[i].x = rand() % 1024; //- 640;
		stars[i].speed = rand() % max_star_speed + 2;
		stars[i].r = rand() % 256;
		stars[i].g = rand() % 256;
		stars[i].b = rand() % 256;
		r = rand() % 1000; if (r > 400 && r < 325) { stars[i].blink = 1; }
	}
}