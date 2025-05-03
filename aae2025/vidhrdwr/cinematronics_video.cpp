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

/***************************************************************************

	Cinematronics vector hardware

***************************************************************************/

#include "aae_mame_driver.h"
#include "ccpu.h"
#include "cinematronics_video.h"

UINT8 bSwapXY=0;
UINT8 bFlipX=0;
UINT8 bFlipY=0;


#define SWAPV(x,y) {int t;t=x;x=y;y=t;}
/* an rgb_t is a single combined R,G,B (and optionally alpha) value */
typedef UINT32 rgb_t;

/* an rgb15_t is a single combined 15-bit R,G,B value */
typedef UINT16 rgb15_t;

/***************************************************************************
	MACROS
***************************************************************************/

/* macros to assemble rgb_t values */
#define MAKE_RGB(r,g,b) 	((((rgb_t)(r) & 0xff) << 16) | (((rgb_t)(g) & 0xff) << 8) | ((rgb_t)(b) & 0xff))
#define MAKE_ARGB(a,r,g,b)	(MAKE_RGB(r,g,b) | (((rgb_t)(a) & 0xff) << 24))

/* macros to extract components from rgb_t values */
#define RGB_ALPHA(rgb)		(((rgb) >> 24) & 0xff)
#define RGB_RED(rgb)		(((rgb) >> 16) & 0xff)
#define RGB_GREEN(rgb)		(((rgb) >> 8) & 0xff)
#define RGB_BLUE(rgb)		((rgb) & 0xff)

/* common colors */
#define RGB_BLACK			(MAKE_RGB(0,0,0))
#define RGB_WHITE			(MAKE_RGB(255,255,255))

/*************************************
 *
 *  Local variables
 *
 *************************************/
static rgb_t vector_color = 0;
static int color_mode;
static INT16 lastx, lasty;
static UINT8 last_control;

/*************************************
 *
 *  Vector rendering
 *
 *************************************/

void cinemat_vector_callback(INT16 sx, INT16 sy, INT16 ex, INT16 ey, UINT8 shift)
{
	sx = sx - Machine->drv->visible_area.min_x;
	ex = ex - Machine->drv->visible_area.min_x;
	sy = sy - Machine->drv->visible_area.min_y;
	ey = ey - Machine->drv->visible_area.min_y;

	int intensity = 0xff;
	
	if (bSwapXY)
	{
		SWAPV(sx, sy);
		SWAPV(ex, ey);
	}

	if (sx == ex && sy == ey) 
	{
		intensity = 0x1ff * shift / 8;
	} 

	if (sx != lastx || sy != lasty)
		add_line(sx, sy, ex, ey, intensity, vector_color);

	add_line(sx, sy, ex, ey, intensity, vector_color);

	lastx = ex;
	lasty = ey;
}

/*************************************
 *
 *  Vector color handling
 *
 *************************************/

void vec_control_write(int data)
{
	int r, g, b, i;

	//wrlog("Video control write");

	switch (color_mode)
	{
	case COLOR_BILEVEL:
		/* color is either bright or dim, selected by the value sent to the port */
		vector_color = (data & 1) ? MAKE_RGB(0x80, 0x80, 0x80) : MAKE_RGB(0xff, 0xff, 0xff);
		break;

	case COLOR_16LEVEL:
		/* on the rising edge of the data value, latch bits 0-3 of the */
		/* X register as the intensity */
		if (data != last_control && data)
		{
			int xval = cpunum_get_reg(0, CCPU_X) & 0x0f;
			i = (xval + 1) * 255 / 16;
			vector_color = MAKE_RGB(i, i, i);
		}
		break;

	case COLOR_64LEVEL:
		/* on the rising edge of the data value, latch bits 2-7 of the */
		/* X register as the intensity */
		if (data != last_control && data)
		{
			int xval = cpunum_get_reg(0, CCPU_X);
			xval = (~xval >> 2) & 0x3f;
			i = (xval + 1) * 255 / 64;
			vector_color = MAKE_RGB(i, i, i);
		}
		break;

	case COLOR_RGB:
		/* on the rising edge of the data value, latch the X register */
		/* as 4-4-4 BGR values */
		if (data != last_control && data)
		{
			int xval = cpunum_get_reg(0, CCPU_X);
			r = (~xval >> 0) & 0x0f;
			r = r * 255 / 15;
			g = (~xval >> 4) & 0x0f;
			g = g * 255 / 15;
			b = (~xval >> 8) & 0x0f;
			b = b * 255 / 15;
			vector_color = MAKE_RGB(r, g, b);
		}
		break;

	case COLOR_QB3:
	{
		static int lastx, lasty;

		/* on the falling edge of the data value, remember the original X,Y values */
		/* they will be restored on the rising edge; this is to simulate the fact */
		/* that the Rockola color hardware did not overwrite the beam X,Y position */
		/* on an IV instruction if data == 0 here */
		if (data != last_control && !data)
		{
			lastx = cpunum_get_reg(0, CCPU_X);
			lasty = cpunum_get_reg(0, CCPU_Y);
		}

		/* on the rising edge of the data value, latch the Y register */
		/* as 2-3-3 BGR values */
		if (data != last_control && data)
		{
			int yval = cpunum_get_reg(0, CCPU_Y);
			r = (~yval >> 0) & 0x07;
			r = r * 255 / 7;
			g = (~yval >> 3) & 0x07;
			g = g * 255 / 7;
			b = (~yval >> 6) & 0x03;
			b = b * 255 / 3;
			vector_color = MAKE_RGB(r, g, b);

			/* restore the original X,Y values */
			cpunum_set_reg(0, CCPU_X, lastx);
			cpunum_set_reg(0, CCPU_Y, lasty);
		}
	}
	break;
	}

	/* remember the last value */
	last_control = data;
}

/*************************************
 *
 *  Video startup
 *
 *************************************/

void video_type_set(int type, int swap_xy)
{
	color_mode = type;
	bSwapXY = swap_xy;
}

/*
VIDEO_START( cinemat_bilevel )
{
	color_mode = COLOR_BILEVEL;
	VIDEO_START_CALL(vector);
}

VIDEO_START( cinemat_16level )
{
	color_mode = COLOR_16LEVEL;
	VIDEO_START_CALL(vector);
}

VIDEO_START( cinemat_64level )
{
	color_mode = COLOR_64LEVEL;
	VIDEO_START_CALL(vector);
}

VIDEO_START( cinemat_color )
{
	color_mode = COLOR_RGB;
	VIDEO_START_CALL(vector);
}

VIDEO_START( cinemat_qb3color )
{
	color_mode = COLOR_QB3;
	VIDEO_START_CALL(vector);
}
*/

/*************************************
 *
 *  End-of-frame
 *
 *************************************/

int cinevid_update()
{
	//VIDEO_UPDATE_CALL(vector);
	//vector_clear_list();
	//cpuintrf_push_context(0);
	ccpu_wdt_timer_trigger();
	//cpuintrf_pop_context();
	return 0;
}

/*************************************
 *
 *  Space War update
 *
 ************************************

VIDEO_UPDATE( spacewar )
{
	int sw_option = readinputportbytag("INPUTS");

	VIDEO_UPDATE_CALL(cinemat);

	// set the state of the artwork
	output_set_value("pressed3", (~sw_option >> 0) & 1);
	output_set_value("pressed8", (~sw_option >> 1) & 1);
	output_set_value("pressed4", (~sw_option >> 2) & 1);
	output_set_value("pressed9", (~sw_option >> 3) & 1);
	output_set_value("pressed1", (~sw_option >> 4) & 1);
	output_set_value("pressed6", (~sw_option >> 5) & 1);
	output_set_value("pressed2", (~sw_option >> 6) & 1);
	output_set_value("pressed7", (~sw_option >> 7) & 1);
	output_set_value("pressed5", (~sw_option >> 10) & 1);
	output_set_value("pressed0", (~sw_option >> 11) & 1);
	return 0;
}
*/