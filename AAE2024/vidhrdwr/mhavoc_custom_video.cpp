#include "mhavoc.h"
#include "aae_mame_driver.h"
#include "aae_avg.h"
#include "loaders.h"
#include "emu_vector_draw.h"

// Video Variables
static int width, height;
static int xcenter, ycenter;
static int xmin, xmax;
static int ymin, ymax;
static int flip_x;
static int flip_y;
static int swap_xy;

static int scale_factor = 2;

void mhavoc_colorram_w(UINT32 address, UINT8 data, struct MemoryWriteByte* pMemWrite)
{
	int i = (data & 4) ? 0x0f : 0x08;
	int r = (data & 8) ? 0x00 : i;
	int g = (data & 2) ? 0x00 : i;
	int b = (data & 1) ? 0x00 : i;

	vec_colors[address].r = r;
	vec_colors[address].g = g;
	vec_colors[address].b = b;
}

void avg_set_flip_x(int flip)
{
	if (flip)
		flip_x = 1;
}

void avg_set_flip_y(int flip)
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

void avg_add_point(int x, int y, int ex, int ey, int r, int g, int b, int p)
{
	avg_apply_flipping_and_swapping(&x, &y);

	r = (r << 4) | 0x0f;
	g = (g << 4) | 0x0f;
	b = (b << 4) | 0x0f;

	if (p)
	{
		avg_apply_flipping_and_swapping(&ex, &ey);
		//add_color_line(x, y, ex, ey, r, g, b);
		add_line(x, y, ex, ey, MAKE_BGR(r, g, b), MAKE_BGR(r, g, b));
	}
	else add_line(x, y, x, y, MAKE_BGR(r, g, b), MAKE_BGR(r, g, b));
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
	int red, green, blue;
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

	firstwd = memrdwd(pc);
	pc++; pc++;
	secondwd = memrdwd(pc);
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
		firstwd = memrdwd(pc);
		opcode = firstwd >> 13;
		pc++; pc++;

		if (opcode == VCTR) //Get the second word if it's a draw command
		{
			secondwd = memrdwd(pc); pc++; pc++;
		}

		if ((opcode == STAT) && ((firstwd & 0x1000) != 0))
			opcode = SCAL;

		switch (opcode)
		{
		case VCTR:

			x = twos_comp_val(secondwd, 13);
			y = twos_comp_val(firstwd, 13);
			z = (secondwd >> 12) & 0x0e;
			goto DRAWCODE;

			break;

		case SVEC:
			x = twos_comp_val(firstwd, 5) << 1;
			y = twos_comp_val(firstwd >> 8, 5) << 1;
			z = ((firstwd >> 4) & 0x0e);

		DRAWCODE:
			if (z == 2) { z = statz; }if (z) { z = (z << 4) | 0x1f; }
			deltax = x * scale;
			if (xflip) deltax = -deltax;
			deltay = y * scale;

			total_length += vector_timer(deltax, deltay);
			//	wrlog("Total length here is %d ---------------->", vector_timer(deltax, deltay));
			if (z)
			{
				if (vec_colors[color].r) red = z;   else red = 0;
				if (vec_colors[color].g) green = z; else green = 0;
				if (vec_colors[color].b) blue = z;  else blue = 0;

				ey = ((currenty - deltay) >> VEC_SHIFT);
				sy = currenty >> VEC_SHIFT;
				sx = (currentx >> VEC_SHIFT);
				ex = (currentx + deltax) >> VEC_SHIFT;

				if (sparkle)
				{
					one = 0;
					two = 0;
					// Get direction
					if (ex > sx) one = 1;
					if (ey > sy) two = 1;

					color = 0xf + (((spkl_shift & 1) << 3) | (spkl_shift & 4)
						| ((spkl_shift & 0x10) >> 3) | ((spkl_shift & 0x40) >> 6));
					if (vec_colors[color].r) red = z; else red = 0;
					if (vec_colors[color].g) green = z; else green = 0;
					if (vec_colors[color].b) blue = z; else blue = 0;

					while (ex != sx || ey != sy)
					{
						color = 0xf + (((spkl_shift & 1) << 3) | (spkl_shift & 4)
							| ((spkl_shift & 0x10) >> 3) | ((spkl_shift & 0x40) >> 6));

						/*
						int bit3 = (~color >> 3) & 1;
						int bit2 = (~color >> 2) & 1;
						int bit1 = (~color >> 1) & 1;
						int bit0 = (~color >> 0) & 1;
						ar = bit3 * 0xcb + bit2 * 0x34;
						ag = bit1 * 0xcb;
						ab = bit0 * 0xcb;
						wrlog("Red %d Green %d BLUE %d", ar, ag, ab);
						*/
						if (vec_colors[color].r) red = z; else red = 0;
						if (vec_colors[color].g) green = z; else green = 0;
						if (vec_colors[color].b) blue = z; else blue = 0;

						if (sx != ex) {
							if (one) { sx += 1; }
							else { sx -= 1; }
						}

						if (sy != ey) {
							if (two) { sy += 1; }
							else { sy -= 1; }
						}
						// Add the point for the line
						avg_add_point(sx, sy, 0, 0, red, green, blue, 0);

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
						avg_add_point(sx, sy, ex, ey, red, green, blue, 1);
					}
				}
			}
			currentx += deltax;
			currenty -= deltay;

			break;

		case STAT:
			color = firstwd & 0x0f;
			statz = (firstwd >> 4) & 0x0f;
			sparkle = firstwd & 0x0800;
			xflip = firstwd & 0x0400;
			vectorbank = ((firstwd >> 8) & 3) * 0x2000;

			if (lastbank != vectorbank) {
				lastbank = vectorbank;
				//wrlog("Vector Bank Switch %x", ((firstwd >> 8) & 3) * 0x2000);
				memcpy(Machine->memory_region[CPU0] + 0x6000, Machine->memory_region[CPU2] + vectorbank, 0x2000);
			}
			break;

		case SCAL:
			b = ((firstwd >> 8) & 0x07) + 8;
			l = (~firstwd) & 0xff;
			scale = (l << VEC_SHIFT) >> b;
			//scalef = scale;
			//Triple the scale for 1024x768 resolution, double for AlphaOne
			scale = scale * scale_factor;

			if (firstwd & 0x0800)
			{
				if (ywindow == 0)
				{
					ywindow = 1;
					clip = currenty >> VEC_SHIFT;
				}
				else
				{
					ywindow = 0;
				}
			}
			break;

		case CNTR:
			d = firstwd & 0xff;
			currentx = 500 << VEC_SHIFT;
			currenty = 512 << VEC_SHIFT;

			break;

		case RTSL:

			if (sp == 0)
			{
				wrlog("*** Vector generator stack underflow! ***");
				done = 1;
				sp = MAXSTACK - 1;
			}
			else
			{
				sp--;
				pc = stack[sp];
			}
			break;

		case HALT:
			done = 1;
			break;

		case JMPL:
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

		case JSRL:
			a = 0x4000 + ((firstwd & 0x1fff) << 1);
			/* if a = 0x0000, treat as HALT */
			if (a == 0x4000)
			{
				done = 1;
			}
			else
			{
				stack[sp] = pc;
				if (sp == (MAXSTACK - 1))
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
	scale_factor = scale;
	// Start with clear video cache
	cache_clear();

	/* compute the min/max values */
	xmin = 0;
	ymin = 0;
	xmax = 1024;
	ymax = 812;
	width = xmax - xmin;
	height = ymax - ymin;

	/* determine the center points */
	xcenter = ((xmax + xmin) / 2);// << 16;
	ycenter = ((ymax + ymin) / 2);// << 16;

	/* initialize to no avg flipping */
	flip_x = flip_y = 0;
	swap_xy = 0;
}