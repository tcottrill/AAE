#include "mhavoc.h"
#include "aae_mame_driver.h"
#include "loaders.h"
#include "emu_vector_draw.h"

UINT8* mhavoc_colorram;

#pragma warning( disable : 4996 4244)

#define memrdwd_mh(address) ((Machine->memory_region[CPU0][pc]) | (Machine->memory_region[CPU0][pc+1]<<8)) /* LBO 062797 */

// Video Variables
static int width, height;
static int xcenter, ycenter;
static int xmin, xmax;
static int ymin, ymax;
static int flip_x;
static int flip_y;
static int swap_xy;

static int scale_adj = 3;

int vector_timer_mh(int deltax, int deltay)
{
	deltax = abs(deltax);
	deltay = abs(deltay);

	if (deltax > deltay)
		return deltax >> 16;
	else
		return deltay >> 16;
}

void mhavoc_colorram_w(UINT32 address, UINT8 data, struct MemoryWriteByte* pMemWrite)
{
	if (scale_adj == 3)
		Machine->memory_region[CPU0][address + 0x1400] = data;
	else
		Machine->memory_region[CPU0][address + 0x10e0] = data;
}

void avg_set_flip_x_mh(int flip)
{
	if (flip)
		flip_x = 1;
}

void avg_set_flip_y_mh(int flip)
{
	if (flip)
		flip_y = 1;
}

void avg_apply_flipping_and_swapping(int* x, int* y)
{
	if (flip_x)
		*x += (xcenter - *x) << 1;
	if (flip_y)
		*y += (ycenter - *y) << 1;

	if (swap_xy)
	{
		int temp = *x;
		*x = *y - ycenter + xcenter;
		*y = temp - xcenter + ycenter;
	}
}

void avg_add_point_mh(int x, int y, int ex, int ey, int intensity, int color, int p)
{
	avg_apply_flipping_and_swapping(&x, &y);

	if (p)
	{
		avg_apply_flipping_and_swapping(&ex, &ey);
		add_line(x, y, ex, ey, intensity, color);
	}
	else add_line(x, y, x, y, intensity, color);
}

/////////////////////////////VECTOR GENERATOR//////////////////////////////////
void mhavoc_video_update(void)
{
	int pc = 0x4000;
	int sp;
	int stack[8];
	int flipword = 0;
	int scale = 0;
	int statz = 0;
	int sparkle = 0;
	int xflip = 0;
	int color = 0;
	int currentx, currenty = 0;
	int done = 0;
	int firstwd, secondwd;
	int opcode;
	int x, y, z = 0, b, l, d, a;
	int deltax, deltay = 0;

	int vectorbank = 0;
	static int lastbank = 0;
	
	int ywindow = 1;
	int clip = 0;
	float sy = 0;
	float ey = 0;
	float sx = 0;
	float ex = 0;
	int nocache = 0;
	int draw = 1;
	int one = 0;
	int two = 0;
	static int spkl_shift = 0;
	sp = 0;
	statz = 0;
	color = 0;
	scale = 0;
	int data = 0;
	int oldscale = 0;
	int bit3;
	int bit2;
	int bit1;
	int bit0;
	int ar;
	int ag;
	int ab;

	firstwd = memrdwd_mh(pc);
	pc++; pc++;
	secondwd = memrdwd_mh(pc);
	total_length = 0;
	if ((firstwd == 0) && (secondwd == 0))
	{
		//wrlog("VGO with zeroed vector memory at %x\n",pc);
		return;
	}
	if (firstwd == 0xafe2) {
		//wrlog("EMPTY FRAME");
		total_length = 1; return;
	}
	pc = 0x4000;
	cache_clear();
	while (!done)
	{
		firstwd = memrdwd_mh(pc);
		opcode = firstwd >> 13;
		pc++; pc++;

		if (opcode == 0) //Get the second word if it's a draw command
		{
			secondwd = memrdwd_mh(pc); pc++; pc++;
		}

		if ((opcode == 3) && ((firstwd & 0x1000) != 0))
			opcode = 8;

		switch (opcode)
		{
		case 0:

			x = twos_comp_val(secondwd, 13);
			y = twos_comp_val(firstwd, 13);
			z = (secondwd >> 12) & 0x0e;
			goto DRAWCODE;

			break;

		case 2:
			x = twos_comp_val(firstwd, 5) << 1;
			y = twos_comp_val(firstwd >> 8, 5) << 1;
			z = ((firstwd >> 4) & 0x0e);

		DRAWCODE:
			if (z == 2) { z = statz; }if (z) { z = z << 4; }
 
			deltax = x * scale;
			if (xflip) deltax = -deltax;
			deltay = y * scale;

			//total_length += vector_timer_mh(deltax, deltay);
			total_length += vector_timer(x * oldscale, y * oldscale);
			//	wrlog("Total length here is %d ---------------->", vector_timer_mh(deltax, deltay));
			if (z)
			{
				ey = ((currenty - deltay) >> 16);
				sy = currenty >> 16;
				sx = (currentx >> 16);
				ex = (currentx + deltax) >> 16;

				if (sparkle)
				{
					one = 0;
					two = 0;
					// Get direction
					if (ex > sx) one = 1;
					if (ey > sy) two = 1;

					while (ex != sx || ey != sy)
					{
						data = mhavoc_colorram[0xf +
							(((spkl_shift & 1) << 3)
								| (spkl_shift & 4)
								| ((spkl_shift & 0x10) >> 3)
								| ((spkl_shift & 0x40) >> 6))];
						bit3 = (~data >> 3) & 1;
						bit2 = (~data >> 2) & 1;
						bit1 = (~data >> 1) & 1;
						bit0 = (~data >> 0) & 1;
						ar = bit3 * 0xcb + bit2 * 0x34;
						ag = bit1 * 0xcb;
						ab = bit0 * 0xcb;

						if (sx != ex) {
							if (one) { sx += 1; }
							else { sx -= 1; }
						}

						if (sy != ey) {
							if (two) { sy += 1; }
							else { sy -= 1; }
						}
						// Add the point for the line
						avg_add_point_mh(sx, sy, ex, ey, z,MAKE_BGR(ar, ag, ab), 0);

						spkl_shift = (((spkl_shift & 0x40) >> 6) ^ ((spkl_shift & 0x20) >> 5) ^ 1) | (spkl_shift << 1);
						if ((spkl_shift & 0x7f) == 0x7f) spkl_shift = 0;
					}
				}
				else
				{
					draw = 1;
					if (ywindow)
					{
						//Y-Window clipping
						if (sy < clip && ey < clip) { draw = 0; }
						if (ey < clip && ey < sy) { ex = ((clip - ey) * ((ex - sx) / (ey - sy))) + ex; ey = clip; }
						if (sy < clip && sy < ey) { sx = ((clip - sy) * ((sx - ex) / (sy - ey))) + sx; sy = clip; }
					}

					if (draw)
					{
						data = mhavoc_colorram[color];

						bit3 = (~data >> 3) & 1;
						bit2 = (~data >> 2) & 1;
						bit1 = (~data >> 1) & 1;
						bit0 = (~data >> 0) & 1;
						ar = bit3 * 0xcb + bit2 * 0x34;
						ag = bit1 * 0xcb;
						ab = bit0 * 0xcb;

						avg_add_point_mh(sx, sy, ex, ey, z,MAKE_BGR(ar, ag, ab), 1);
					}
				}
			}
			currentx += deltax;
			currenty -= deltay;

			break;

		case 3: // STROBE2
			color = firstwd & 0x0f;
			statz = (firstwd >> 4) & 0x0f; // Intensity
			sparkle = firstwd & 0x0800;
			
			if (sparkle)
			{
				spkl_shift = ((firstwd >> 3) & 1)
					| ((firstwd >> 1) & 2)
					| ((firstwd << 1) & 4)
					| ((firstwd << 2) & 8)
					| ((rand() & 0x7) << 4);
			}

			xflip = firstwd & 0x0400;
			vectorbank = ((firstwd >> 8) & 3) * 0x2000;

			if (lastbank != vectorbank) {
				lastbank = vectorbank;
				//wrlog("Vector Bank Switch %x", ((firstwd >> 8) & 3) * 0x2000);
				memcpy(Machine->memory_region[CPU0] + 0x6000, Machine->memory_region[CPU2] + vectorbank, 0x2000);
			}
			break;

		case 8:
			b = ((firstwd >> 8) & 0x07) + 8;
			l = (~firstwd) & 0xff;
			scale = (l << 16) >> b;
			//scalef = scale;
			//Triple the scale for 1024x768 resolution, double for AlphaOne
			oldscale = scale;
			scale = scale * scale_adj;

			if (firstwd & 0x0800)
			{
				if (ywindow == 0)
				{
					ywindow = 1;
					clip = currenty >> 16;
				}
				else
				{
					ywindow = 0;
				}
			}
			break;

		case 4:
			d = firstwd & 0xff;
			currentx = xcenter;
			currenty = ycenter;

			break;

		case 6:

			if (sp == 0)
			{
				wrlog("*** Vector generator stack underflow! ***");
				done = 1;
				sp = 8 - 1;
			}
			else
			{
				sp--;
				pc = stack[sp];
			}
			break;

		case 1:
			done = 1;
			break;

		case 7:
			a = 0x4000 + ((firstwd & 0x1fff) << 1);
			/* if a = 0x0000, treat as HALT */
			if (a == 0x4000)
			{
				done = 1;
			}
			else
			{
				pc = a;
			}
			break;

		case 5:
			a = 0x4000 + ((firstwd & 0x1fff) << 1);
			/* if a = 0x0000, treat as HALT */
			if (a == 0x4000)
			{
				done = 1;
			}
			else
			{
				stack[sp] = pc;
				if (sp == (8 - 1))
				{
					wrlog("--- Passed MAX STACK (BAD!!) ---");
					done = 1;
					sp = 0;
				}
				else
				{
					sp++;
					pc = a;
				}
			}
			break;

		default: wrlog("Error in AVG engine");
		}
	}
}

// ***************************************  END CODE BELOW ************************************************

void mhavoc_video_init(int scale)
{
	scale_adj = scale;
	// Start with clear video cache
	cache_clear();

	xmin = Machine->drv->visible_area.min_x;
	ymin = Machine->drv->visible_area.min_y;
	xmax = Machine->drv->visible_area.max_x * scale;
	ymax = (Machine->drv->visible_area.max_y * scale) * 1.3;

	xcenter = ((xmax + xmin) / 2) << 16;
	ycenter = ((ymax + ymin) / 2) << 16;


	/* initialize to no avg flipping */
	flip_x = flip_y = 0;
	swap_xy = 0;
	mhavoc_colorram = &memory_region(REGION_CPU1)[0x1400];
	if (scale_adj == 2) { mhavoc_colorram = &memory_region(REGION_CPU1)[0x10e0]; }
}