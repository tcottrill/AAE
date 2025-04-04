/******************************************************************************
 *
 * vector.c
 *
 *
 * Copyright 1997,1998 by the M.A.M.E. Project
 *
 * Modified for Opengl Rendering
 **************************************************************************** */

#include <math.h>
#include "vector.h"
#include "log.h"
#include <stdlib.h>
#include "deftypes.h"
#include "emu_vector_draw.h"

#define VECTOR_WIDTH_DENOM			512

#define MAX_POINTS	10000

#define VECTOR_TEAM \
	"-* Vector Heads *-\n" \
	"Brad Oliver\n" \
	"Aaron Giles\n" \
	"Bernd Wiebelt\n" \
	"Allard van der Bas\n" \
	"Al Kossow (VECSIM)\n" \
	"Hedley Rainnie (VECSIM)\n" \
	"Eric Smith (VECSIM)\n" \
	"Neil Bradley (technical advice)\n" \
	"Andrew Caldwell (anti-aliasing)\n" \
	"- *** -\n"

#define VCLEAN  0
#define VDIRTY  1
#define VCLIP   2
#define VTEX    3

 // The vertices are buffered here
typedef struct
{
	int x; int y;
	unsigned int col;
	int intensity;
	int arg1; int arg2; // start/end in pixel array or clipping info //
	int status;         // for dirty and clipping handling //
} point;

typedef struct _render_bounds render_bounds;
struct _render_bounds
{
	float				x0;					/* leftmost X coordinate */
	float				y0;					/* topmost Y coordinate */
	float				x1;					/* rightmost X coordinate */
	float				y1;					/* bottommost Y coordinate */
};

int translucency;
static int flicker;                              /* beam flicker value     */
static float flicker_correction = 0.0f;
static float beam_width;
static point* vector_list;
static int vector_index;
static int prev_vector_index = 0;

static int vecshift;

void vector_set_flicker(float _flicker)
{
	flicker_correction = _flicker;
	flicker = (int)(flicker_correction * 2.55);
}

float vector_get_flicker(void)
{
	return flicker_correction;
}

void vector_set_beam(float _beam)
{
	beam_width = _beam;
}

float vector_get_beam(void)
{
	return beam_width;
}

/*
 * Setup scaling. Currently the Sega games are stuck at VECSHIFT 15
 * and the the AVG games at VECSHIFT 16
 */
void vector_set_shift(int shift)
{
	vecshift = shift;
}

/*
 * Initializes vector game video emulation
 */

int VECTOR_START()
{
	beam_width = 1.0;//options_get_float(mame_options(), OPTION_BEAM);

	/* Grab the settings for this session */
	vector_set_flicker(0);//options_get_float(mame_options(), OPTION_FLICKER));

	vector_index = 0;

	/* allocate memory for tables */
	vector_list = (point*)malloc(MAX_POINTS * sizeof(vector_list[0]));

	return 1;
}

int render_clip_line(render_bounds* bounds, const render_bounds* clip)
{
	/* loop until we get a final result */
	while (1)
	{
		UINT8 code0 = 0, code1 = 0;
		UINT8 thiscode;
		float x, y;

		/* compute Cohen Sutherland bits for first coordinate */
		if (bounds->y0 > clip->y1)
			code0 |= 1;
		if (bounds->y0 < clip->y0)
			code0 |= 2;
		if (bounds->x0 > clip->x1)
			code0 |= 4;
		if (bounds->x0 < clip->x0)
			code0 |= 8;

		/* compute Cohen Sutherland bits for second coordinate */
		if (bounds->y1 > clip->y1)
			code1 |= 1;
		if (bounds->y1 < clip->y0)
			code1 |= 2;
		if (bounds->x1 > clip->x1)
			code1 |= 4;
		if (bounds->x1 < clip->x0)
			code1 |= 8;

		/* trivial accept: just return FALSE */
		if ((code0 | code1) == 0)
			return 0;

		/* trivial reject: just return TRUE */
		if ((code0 & code1) != 0)
			return 1;

		/* fix one of the OOB cases */
		thiscode = code0 ? code0 : code1;

		/* off the bottom */
		if (thiscode & 1)
		{
			x = bounds->x0 + (bounds->x1 - bounds->x0) * (clip->y1 - bounds->y0) / (bounds->y1 - bounds->y0);
			y = clip->y1;
		}

		/* off the top */
		else if (thiscode & 2)
		{
			x = bounds->x0 + (bounds->x1 - bounds->x0) * (clip->y0 - bounds->y0) / (bounds->y1 - bounds->y0);
			y = clip->y0;
		}

		/* off the right */
		else if (thiscode & 4)
		{
			y = bounds->y0 + (bounds->y1 - bounds->y0) * (clip->x1 - bounds->x0) / (bounds->x1 - bounds->x0);
			x = clip->x1;
		}

		/* off the left */
		else
		{
			y = bounds->y0 + (bounds->y1 - bounds->y0) * (clip->x0 - bounds->x0) / (bounds->x1 - bounds->x0);
			x = clip->x0;
		}

		/* fix the appropriate coordinate */
		if (thiscode == code0)
		{
			bounds->x0 = x;
			bounds->y0 = y;
		}
		else
		{
			bounds->x1 = x;
			bounds->y1 = y;
		}
	}
}

/*
 * Adds a line end point to the vertices list. The vector processor emulation
 * needs to call this.
 */
void vector_add_point(int x, int y, unsigned int color, int intensity)
{
	point* newpoint;
	int tex = 0;

	//Overloaded intensity for asteroids and deluxe, to determine textured shots.
	if (intensity > 0x1000)
	{
		intensity = intensity >> 8;
		tex = 1;
	}

	if (intensity > 0xff) intensity = 0xff;

	// if (flicker && (intensity > 0))
  //	{
  //		intensity += (intensity * (0x80-(rand()&0xff)) * flicker) >> 16;
  //		if (intensity < 0)	intensity = 0;
  //		if (intensity > 0xff) intensity = 0xff;
  //	}
	newpoint = &vector_list[vector_index];
	newpoint->x = x;
	newpoint->y = y;
	newpoint->col = color;
	newpoint->intensity = intensity;
	newpoint->status = VDIRTY; /* mark identical lines as clean later */

	if (tex)
	{
		newpoint->status = VTEX;
		tex = 0;
	}

	vector_index++;
	if (vector_index >= MAX_POINTS)
	{
		vector_index--;
		wrlog("*** Warning! Vector list overflow!\n");
	}
}

/*
 * Add new clipping info to the list
 */

void vector_add_clip(int x1, int yy1, int x2, int y2)
{
	point* newpoint;

	newpoint = &vector_list[vector_index];
	newpoint->x = x1;
	newpoint->y = yy1;
	newpoint->arg1 = x2;
	newpoint->arg2 = y2;
	newpoint->status = VCLIP;

	vector_index++;
	if (vector_index >= MAX_POINTS)
	{
		vector_index--;
		wrlog("*** Warning! Vector list overflow!\n");
	}
}

/*
 * The vector CPU creates a new display list. We save the old display list,
 * but only once per refresh.
 */
void vector_clear_list()
{
	prev_vector_index = vector_index;
	vector_index = 0;
}

void vector_reset_list()
{
	vector_index = prev_vector_index;
}
int VECTOR_UPDATE()
{
	float xscale = 1.0f / (65536 * (Machine->gamedrv->visible_area.max_x - Machine->gamedrv->visible_area.min_x));
	float yscale = 1.0f / (65536 * (Machine->gamedrv->visible_area.max_y - Machine->gamedrv->visible_area.min_y));
	float xoffs = (float)Machine->gamedrv->visible_area.min_x;
	float yoffs = (float)Machine->gamedrv->visible_area.min_y;

	point* curpoint;
	render_bounds clip;
	int lastx = 0, lasty = 0;
	int i;

	curpoint = vector_list;

	clip.x0 = clip.y0 = 0.0f;
	clip.x1 = clip.y1 = 1.0f;

	for (i = 0; i < vector_index; i++)
	{
		render_bounds coords;

		if (curpoint->status == VCLIP)
		{
			coords.x0 = ((float)curpoint->x - xoffs) * xscale;
			coords.y0 = ((float)curpoint->y - yoffs) * yscale;
			coords.x1 = ((float)curpoint->arg1 - xoffs) * xscale;
			coords.y1 = ((float)curpoint->arg2 - yoffs) * yscale;

			clip.x0 = (coords.x0 > 0.0f) ? coords.x0 : 0.0f;
			clip.y0 = (coords.y0 > 0.0f) ? coords.y0 : 0.0f;
			clip.x1 = (coords.x1 < 1.0f) ? coords.x1 : 1.0f;
			clip.y1 = (coords.y1 < 1.0f) ? coords.y1 : 1.0f;
		}
		else
		{
			coords.x0 = ((float)lastx - xoffs) * xscale;
			coords.y0 = ((float)lasty - yoffs) * yscale;
			coords.x1 = ((float)curpoint->x - xoffs) * xscale;
			coords.y1 = ((float)curpoint->y - yoffs) * yscale;

			if (curpoint->intensity != 0) {
				if (!render_clip_line(&coords, &clip))

					if (curpoint->status == VTEX)
					{
						screen_add_tex(coords.x0, coords.y0, coords.x1, coords.y1, curpoint->intensity, curpoint->col);
					}
					else {
						//screen_add_line(coords.x0, coords.y0,coords.x1, coords.y1 , curpoint->intensity, curpoint->col );
						screen_add_line(coords.x0, coords.y0, coords.x1 + .00001, coords.y1 + .00001, curpoint->intensity, curpoint->col);
						// wrlog("Line from: %f %f to %f %f intensity %d",coords.x0,coords.y0,coords.x1,coords.y1,curpoint->intensity);
					}
			}
			lastx = curpoint->x;
			lasty = curpoint->y;
		}
		curpoint++;
	}
	screen_draw_all();
	return 0;
}

//	render_screen_add_line(screen, coords.x0, coords.y0, coords.x1, coords.y1,
//	beam_width * (1.0f / (float)VECTOR_WIDTH_DENOM),
//			(curpoint->intensity << 24) | (curpoint->col & 0xffffff),
//			flags);