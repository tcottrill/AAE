#include "old_mame_vecsim_dvg.h"
#include "vector.h"
#include "timer.h"
#include "aae_avg.h"
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

// Some Code Also from Eric Smith's VECSIM Emulator.

// Variables
int vector_updates;
static int busy;
static UINT8 vector_engine;
unsigned char* vectorram;
unsigned int vectorram_size;
static int ASTEROID_DVG = 0;
static int yval = 1130;

static int dvg_timer_val = -1;

#define vecmemrdwd(address) ((vectorram[pc]) | (vectorram[pc+1]<<8))


static void set_color_palette()
{
	int i = 0;

	vec_colors[0].r = 0;
	vec_colors[0].g = 0;
	vec_colors[0].b = 0;

	for (i = 1; i < 17; i++)
	{
		vec_colors[i].r = i * 16;
		vec_colors[i].g = i * 16;
		vec_colors[i].b = i * 16;
	}
}

int dvg_done(void)
{
	return { !busy };
}

static void dvg_clr_busy(int dummy)
{
	busy = 0;
}

void dvg_reset(int offset, int data)
{
	dvg_clr_busy(0);
}

void dvg_reset_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite)
{
	dvg_reset(0, 0);
}

int dvg_vector_timer(int scale)
{
	return scale;
}

int dvg_generate_vector_list(void)
{
	int pc = 0;
	int sp = 0;
	int stack[8] = {0};
	int scale = 0;
	int done = 0;
	UINT16 firstwd =0;
	UINT16 secondwd = 0;
	UINT16 opcode;
	int  x, y;
	int temp;
	int z;
	int a;
	int  deltax, deltay;
	int  currentx = 0;
	int  currenty = 0;
	int total_length = 1;
	//wrlog("AVG VIDEO UPDATE-----------");
	cache_clear();
	while (!done)
	{
		firstwd = vecmemrdwd(pc); pc += 2;
		opcode = firstwd >> 12;
		if (opcode < 0x0b) { secondwd = vecmemrdwd(pc); pc += 2; }

		switch (opcode)
		{
		case 0:
		case 1:
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
		case 7:
		case 8:
		case 9:
			// compute raw X and Y values and intensity //
			z = secondwd >> 12;
			y = firstwd & 0x03ff;
			x = secondwd & 0x03ff;

			//Check Sign Values and adjust as necessary
			if (firstwd & 0x0400) { y = -y; }
			if (secondwd & 0x400) { x = -x; }

			// Do overall scaling
			temp = ((scale + opcode) & 0x0f);
			if (temp > 9)
				temp = -1;
			deltax = (x << VEC_SHIFT) >> (9 - temp);
			deltay = (y << VEC_SHIFT) >> (9 - temp);
			total_length += dvg_vector_timer(temp);

			if ((currentx == (currentx)+deltax) && (currenty == (currenty)-deltay))
			{
				if (ASTEROID_DVG) {
					if (z > 14) cache_txt(currentx >> VEC_SHIFT, currenty >> VEC_SHIFT, config.fire_point_size, 255);
					else cache_point(currentx, currenty, z, 0, 0, 1.0);
				}
				else {
					cache_point(currentx, currenty, z, 0, 0, 1.0);
				}
			}
			cache_line(currentx >> VEC_SHIFT, currenty >> VEC_SHIFT, (currentx + deltax) >> VEC_SHIFT, (currenty - deltay) >> VEC_SHIFT, z, config.gain, 0);
			cache_point(currentx >> VEC_SHIFT, currenty >> VEC_SHIFT, z, config.gain, 0, 0);
			cache_point((currentx + deltax) >> VEC_SHIFT, (currenty - deltay) >> VEC_SHIFT, z, config.gain, 0, 0);

			currentx += deltax;
			currenty -= deltay;
			break;

		case 0xa:
			//secondwd = memrdwd(pc); pc += 2;
			x = twos_comp_val(secondwd, 12);
			y = twos_comp_val(firstwd, 12);

			//Do overall draw scaling
			scale = (secondwd >> 12) & 0x0f;
			currenty = (yval - y) << VEC_SHIFT;
			currentx = x << VEC_SHIFT;
			break;

		case 0xb: done = 1; break;

		case 0xc: a = (firstwd & 0x0fff) << 1;
			stack[sp] = pc;
			if (sp == 4) { done = 1; sp = 0; }
			else { sp = sp + 1; pc = a; }
			break;

		case 0xd:
			sp = sp - 1;
			pc = stack[sp];
			break;

		case 0xe:
			a = (firstwd & 0x0fff) << 1;
			pc = a;
			break;

		case 0xf:
			// compute raw X and Y values //
			z = (firstwd & 0xf0) >> 4;
			y = firstwd & 0x0300;
			x = (firstwd & 0x03) << 8;

			//Check Sign Values and adjust as necessary
			if (firstwd & 0x0400) { y = -y; }
			if (firstwd & 0x04) { x = -x; }

			temp = 2 + ((firstwd >> 2) & 0x02) + ((firstwd >> 11) & 0x01);
			temp = ((scale + temp) & 0x0f);
			if (temp > 9) temp = -1;
			deltax = (x << VEC_SHIFT) >> (9 - temp);
			deltay = (y << VEC_SHIFT) >> (9 - temp);
			total_length += dvg_vector_timer(temp);
			
			if ((currentx == (currentx)+deltax) && (currenty == (currenty)-deltay))
			{
				if (ASTEROID_DVG) {
					if (z == 7) { cache_txt(currentx >> VEC_SHIFT, currenty >> VEC_SHIFT, config.explode_point_size, 255); }
					else { cache_point(currentx >> VEC_SHIFT, currenty >> VEC_SHIFT, z, config.gain, 0, 1.0); }
				}
				else { cache_point(currentx >> VEC_SHIFT, currenty >> VEC_SHIFT, z, config.gain, 0, 1.0); }
			}

			else
			{
				if (ASTEROID_DVG) { if (z < 2)  z = 0; }
				cache_line(currentx >> VEC_SHIFT, currenty >> VEC_SHIFT, (currentx + deltax) >> VEC_SHIFT, (currenty - deltay) >> VEC_SHIFT, z, config.gain, 0);
				cache_point(currentx >> VEC_SHIFT, currenty >> VEC_SHIFT, z, config.gain, 0, 0);
				cache_point((currentx + deltax) >> VEC_SHIFT, (currenty - deltay) >> VEC_SHIFT, z, config.gain, 0, 0);
			}

			currentx += deltax;
			currenty -= deltay;
			break;
		}
	}
	//wrlog("AVG DUPDATE END");
	return total_length;
}

void dvg_go(int offset, int data)
{
	int total_length;

	//wrlog("DVG GO CALLED --------------");

	/* skip if already busy */
	if (busy) { return; } //wrlog("Vector Busy");

	/* count vector updates and mark ourselves busy */
	vector_updates++;
	busy = 1;

	/* DVG case */
	if (vector_engine == USE_DVG)
	{
		total_length = dvg_generate_vector_list();
		dvg_timer_val=timer_pulse(TIME_IN_NSEC(4500) * total_length, CPU0, dvg_clr_busy);
		//wrlog("Setting time at %f on timer %d", 1512000 * (TIME_IN_NSEC(4500) * total_length), dvg_timer_val);
	}

	/* AVG case
	else
	{
		total_length = avg_generate_vector_list();

		// for Major Havoc, we need to look for empty frames
		if (total_length > 1)
		{
			timer_set(TIME_IN_NSEC(1500) * total_length, ONE_SHOT, avgdvg_clr_busy);
		}
		else
		{
			vector_updates--;
			busy = 0;
		}
	}
	 */
}

void dvg_go_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite)
{
	dvg_go(0, 0);
}

int dvg_init()
{
	// Move these to avg_dvg init						// TBD
	vectorram = &GI[CPU0][driver[gamenum].vectorram];   //&memory_region(REGION_CPU1)[Machine->drv->vectorram];
	vectorram_size = driver[gamenum].vectorram_size;    //Machine->drv->vectorram_size;
	//
	set_color_palette();
	vector_engine = USE_DVG;
	busy = 0;
	vector_updates = 0;
	dvg_timer_val = -1;
	return 1;
}

int dvg_start_asteroid(void)
{
	ASTEROID_DVG = 1;
	yval = 1130;
	return dvg_init();
}

int dvg_start(void)
{
	ASTEROID_DVG = 0;
	yval = 1024;
	return dvg_init();
}

int dvg_end()
{
	busy = 0;
	vector_updates = 0;
	cache_clear();
	return 1;
}

void test_clear_busy()
{
	busy = 0;
	vector_updates = 0;
}