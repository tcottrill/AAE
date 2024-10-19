
#include "dvg.h"
#include "colordefs.h"
#include "vector.h"
#include "aae_mame_driver.h"
#include "timer.h"

static UINT8 vector_engine;
static UINT8 flipword;
static UINT8 busy;
static unsigned int colorram[32];

static int width, height;
static int xcenter, ycenter;
static int xmin, xmax;
static int ymin, ymax;

static int flip_x, flip_y, swap_xy;

unsigned char* vector_ram;
unsigned int vectorram_size;

int vector_updates; /* avgdvg_go_w()'s per Mame frame, should be 1 */


#define vector_word(address) ((vector_ram[pc+1]) | (vector_ram[pc]<<8))


int dvg_vector_timer(int scale)
{
	return scale;
}


static int dvg_generate_vector_list()
{
	int pc = 0;
	int sp = 0;
	int stack[4];
	int scale = 0;
	int done = 0;
	UINT16 firstwd, secondwd = 0;
	UINT16 opcode;
	int  x, y;
	int temp;
	int z;
	int a;
	int deltax, deltay;
	int currentx, currenty = 0;
	
	currentx = 0;
	currenty = 0;

	while (!done)
	{
		firstwd = vector_word(pc);
		opcode = firstwd & 0xf000;
		pc++;
		pc++;

		switch (opcode)
		{
		case 0xf:

			// compute raw X and Y values //
			z = (firstwd & 0xf0) >> 4;
			y = firstwd & 0x0300;
			x = (firstwd & 0x03) << 8;
			//Check Sign Values and adjust as necessary
			if (firstwd & 0x0400) { y = -y; }
			if (firstwd & 0x04) { x = -x; }
			//Invert Drawing if in Cocktal mode and Player 2 selected
			//if (GI[0x1e] != 0 && cocktail) { x = -x; y = -y; }
			temp = 2 + ((firstwd >> 2) & 0x02) + ((firstwd >> 11) & 0x01);
			temp = ((scale + temp) & 0x0f);
			if (temp > 9)	temp = -1;
			
			goto DRAWCODE;
			break;

		case 0:
		case 0x1:
		case 0x2:
		case 0x3:
		case 0x4:
		case 0x5:
		case 0x6:
		case 0x7:
		case 0x8:
		case 0x9:

			// Get Second Word
			secondwd = vector_word(pc);
			pc++;
			pc++;

			// compute raw X and Y values //
			z = secondwd >> 12;
			y = firstwd & 0x03ff;
			x = secondwd & 0x03ff;

			//Check Sign Values and adjust as necessary
			if (firstwd & 0x0400) { y = -y; }
			if (secondwd & 0x400) { x = -x; }
			//Invert Drawing if in Cocktail mode and Player 2 selected
			//if (GI[0x1e] != 0 && cocktail)	{x = -x;	y = -y;	}
			// Do overall scaling
			temp = scale + (opcode >> 12);
			temp = temp & 0x0f;

			if (temp > 9) { temp = -1; }
					
		DRAWCODE:

			deltax = (x << 16) >> (9 - temp);
			deltay = (y << 16) >> (9 - temp);
			total_length += dvg_vector_timer(temp);
							

			if (z )
			{
				if ((currentx == (currentx)+deltax) && (currenty == (currenty)-deltay))
				{
					cache_point((currentx >> 16), (currenty >> 16), z, config.gain, 0, 1.0);
				}

				cache_line((currentx >> 16), (currenty >> 16), (currentx + deltax) >> 16, (currenty - deltay) >> 16, z, config.gain, 0);
				cache_point((currentx >> 16), (currenty >> 16), z, config.gain, 0, 0);
				cache_point((currentx + deltax) >> 16, (currenty - deltay) >> 16, z, config.gain, 0, 0);
			}

			currentx += deltax;
			currenty -= deltay;
			
			break;

		case 0xa:

			secondwd = vector_word(pc);
			pc++;
			pc++;
			x = twos_comp_val(secondwd, 12);
			y = twos_comp_val(firstwd, 12);
			//Scale Y drawing as best we can
		   //Invert the screen drawing if cocktail and Player 2 selected

			scale = (secondwd >> 12) & 0x0f;

			currentx = (x - xmin) << 16;
			currenty = (ymax - y) << 16;

			//currenty = 1015 - y;
			//currentx = x;
			break;

		case 0xb:
			done = 1;
			break;

		case 0xc:

			a = ((firstwd & 0x0fff) << 1);
			stack[sp] = pc;

			if (sp == 4)
			{
				done = 1;
				sp = 0;
			}
			else
				sp = sp + 1;
			pc = a;
			break;

		case 0xd:
			sp = sp - 1;
			pc = stack[sp];
			break;

		case 0xe000:
			a = ((firstwd & 0x0fff) << 1);
			pc = a;
			break;
		}
	}
	return total_length;
}


int dvg_init()
{
	/* compute the min/max values */
	xmin = Machine->gamedrv->visible_area.min_x;
	ymin = Machine->gamedrv->visible_area.min_y;
	xmax = Machine->gamedrv->visible_area.max_x;
	ymax = Machine->gamedrv->visible_area.max_y;
	
	
	width = xmax - xmin;
	height = ymax - ymin;

	/* determine the center points */
	xcenter = ((xmax + xmin) / 2) << 16;
	ycenter = ((ymax + ymin) / 2) << 16;

	vector_ram = &GI[0][Machine->gamedrv->vectorram];
	vectorram_size = Machine->gamedrv->vectorram;

}

void dvg_update()
{
	dvg_generate_vector_list();
}

void dvg_end()
{

}


/*************************************
 *
 *	AVG execution/busy detection
 *
 ************************************/

int dvg_done(void)
{
	return !busy;
}

static void dvg_clr_busy(int dummy)
{
	busy = 0;
}

void dvg_reset(int offset, int data)
{
	//wrlog("AVG Reset 2 Called");
	dvg_clr_busy(0);
}

void dvg_go_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite)
{
	//wrlog("AVG Go 1 Called");
	dvg_go(0, 0);
}

void dvg_reset_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite)
{
	//wrlog("AVG Reset 1 Called");
	dvg_reset(0, 0);
}

void dvg_go(int offset, int data)
{
	int total_length;

	/* skip if already busy */
	if (busy)
	{
		//wrlog("Vector Busy");
		return;
	}

	/* count vector updates and mark ourselves busy */
	vector_updates++;
	busy = 1;

		total_length = dvg_generate_vector_list();
		timer_set(TIME_IN_NSEC(4500) * total_length, ONE_SHOT, dvg_clr_busy);
		
}
