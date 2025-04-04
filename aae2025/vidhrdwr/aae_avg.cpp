// This is an insane mess, working on a rewrite, or switch to modern AVG drawing code.

#include "aae_avg.h"
#include "timer.h"

#pragma warning( disable : 4996 4244)

static int vector_type = 0;

static int XFLIP = 0;
static int YFLIP = 0;
static int TYPE_BZ = 0;
static int TYPE_TEMP = 0;
static int TYPE_GV = 0;
static int TYPE_MH = 0;
static int TYPE_QU = 0;
static int TYPE_SD = 0;
static int TYPE_SW = 0;
static int pc = 0;
static int AVG_BUSY = 0;
static int TOTAL_LENGTH = 0;
static int SCALEADJ = 2;
static int vectorbank = 0x18000;
static float sweep = 0;
static int PCTOP = 0x2000;
static int NO_CACHE = 0;

#define TIME_IN_NSEC(us)      ((double)(us) * (1.0 / 1000000000.0))
static UINT8 busy;

int vector_timer(int deltax, int deltay)
{
	deltax = abs(deltax);
	deltay = abs(deltay);

	if (deltax > deltay)
		return deltax >> VEC_SHIFT;
	else
		return deltay >> VEC_SHIFT;
}


static void avg_clr_busy(int dummy)
{
	AVG_BUSY = 0;
}


static void calc_sweep()
{
	//sweep = 3.268 * total_length;
	sweep = (TIME_IN_NSEC(1600) * total_length) * 1512000; //was 1600NS, changed back to 1500 driver[gamenum].cpu_freq[get_current_cpu()];
	//wrlog("SWEEP CALC HERE is %f", (TIME_IN_NSEC(1600) * total_length));
}

static void set_bw_colors()
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

static void BZ_DRAW(int currentx, int currenty, int deltax, int deltay, int z, int color)
{
	int sy = 0;
	int ey = 0;
	int sx = 0;
	int ex = 0;
	int noline = 0;
	int BZ_CLIP = 726;

	ey = ((currenty - deltay) >> VEC_SHIFT);
	sy = currenty >> VEC_SHIFT;
	sx = (currentx >> VEC_SHIFT);
	ex = (currentx + deltax) >> VEC_SHIFT;
	
	if (gamenum != REDBARON)
	{
		//Line color 0 clipping
		if (sy > BZ_CLIP && ey > BZ_CLIP && color == 0) { noline = 1; }
		if (sy > BZ_CLIP && sy > ey && color == 0) { sx = ((BZ_CLIP - sy) * ((ex - sx) / (ey - sy))) + sx; sy = BZ_CLIP; }
		if (ey > BZ_CLIP && ey > sy && color == 0) { ex = ((BZ_CLIP - ey) * ((sx - ex) / (sy - ey))) + ex; ey = BZ_CLIP; }
	}

	if (noline == 0) {
		//cache_line(sx, sy, ex, ey, gc, config.gain, 0);
		add_line(sx, sy, ex, ey, MAKE_BGR(z, z, z), MAKE_BGR(z, z, z));
	}
}

void AVG_RUN(void)
{
	int pc = 0x0000;
	int sp;
	int stack[8];
	int flipword = 0;
	int scale = 0;
	int statz = 0;
	int xflip = 0;
	int color = 0;
	static int  sparkle = 0;
	static int spkl_shift = 0;
	int currentx = 0;
	int currenty = 0;
	int done = 0;
	int firstwd, secondwd;
	int opcode;
	int x, y, z = 0, b, l, d, a;
	int deltax, deltay = 0;
	int red, green, blue;
	int COMPSHFT = 13;
	static int lastbank = 0;
	int ywindow = 1;
	int clip = 0;
	int oldscale = 0;
	int intensity = 0;

	int TEMP_CLIP = 240;
	int sy = 0;
	int ey = 0;
	int sx = 0;
	int ex = 0;

	int draw = 1;

	pc = driver[gamenum].vectorram;

	if (TYPE_QU) COMPSHFT = 12;
	sp = 0;
	statz = 0;
	color = 0;
	scale = 0;
	total_length = 0;
	if (NO_CACHE) cache_clear();

	//wrlog("Starting AVG RUN");

	while (!done)
	{
		if (TYPE_QU) firstwd = memrdwdf(pc);
		else if (TYPE_SW) firstwd = memrdwd_flip(pc);
		else firstwd = memrdwd(pc);

		opcode = firstwd >> 13;
		pc++; pc++;
		//wrlog("FirstWord is  %x",firstwd);
		if (opcode == VCTR) //Get the second word if it's a draw command
		{
			if (TYPE_QU) secondwd = memrdwdf(pc);
			else if (TYPE_SW) secondwd = memrdwd_flip(pc);
			else secondwd = memrdwd(pc);
			pc++; pc++;
		}

		if ((opcode == STAT) && ((firstwd & 0x1000) != 0))opcode = SCAL;
		switch (opcode)
		{
		case VCTR: 
			x = twos_comp_val(secondwd, COMPSHFT);
			y = twos_comp_val(firstwd, COMPSHFT);
			z = (secondwd >> 12) & 0x0e;//~0x01;

			goto DRAWCODE;
			break;

		case SVEC:
			x = twos_comp_val(firstwd, 5) << 1;
			y = twos_comp_val(firstwd >> 8, 5) << 1;
			z = ((firstwd >> 4) & 0x0e);

		DRAWCODE:

			
			total_length += vector_timer(x * oldscale, y * oldscale);

			deltax = x * scale;
			deltay = y * scale;
			//total_length += vector_timer(deltax, deltay);
			if (xflip) deltax = -deltax;
			if (YFLIP) deltay = -deltay;
			if (XFLIP) deltax = -deltax;
			

			sx = (currentx >> VEC_SHIFT);
			sy = currenty >> VEC_SHIFT;
			ex = (currentx + deltax) >> VEC_SHIFT;
			ey = ((currenty - deltay) >> VEC_SHIFT);

			if (z> 1)
			{
				if (TYPE_SW)
				{
					z = (z * statz) / 8; // Brightness / Translucency here
					if (z > 0xff)	z = 0xff;
				}
				else
				{
					if (z == 2) { z = statz; }
					 z = (z << 4) | 0x1f; 
					 if (z > 0xff)	z = 0xff;
				}

				if (TYPE_BZ) BZ_DRAW(currentx, currenty, deltax, deltay, z, color);
				else 
				{
					if (sparkle && TYPE_MH)
					{
						color = 0xf + (((spkl_shift & 1) << 3) | (spkl_shift & 4) | ((spkl_shift & 0x10) >> 3) | ((spkl_shift & 0x40) >> 6));
					}

					//Gravitar, Space Duel, Black Widow and Star Wars
					if (color & 0x04) red = z; else red = 0;
					if (color & 0x02) green = z; else green = 0;
					if (color & 0x01) blue = z; else blue = 0;
					
					//if (TYPE_GV && testsw) { if (color == 0) { green = z; blue = z; } } //Gravitar Test Mode Fix
					
					if (TYPE_TEMP)
					{
						if (sparkle) { color = rand() & 0x07; }

						red = vec_colors[color].r;
						green = vec_colors[color].g;
						blue = vec_colors[color].b;

						if (sy < TEMP_CLIP && ey < TEMP_CLIP) { draw = 0; }
						else { draw = 1; }
						if (ey < TEMP_CLIP && ey < sy) { ex = ((TEMP_CLIP - ey) * ((ex - sx) / (ey - sy))) + ex; ey = TEMP_CLIP; }
				  	    if (sy < TEMP_CLIP && sy < ey) { sx = ((TEMP_CLIP - sy) * ((sx - ex) / (sy - ey))) + sx; sy = TEMP_CLIP; }
					}

					if (TYPE_QU)
					{
						red = vec_colors[color].r;
						green = vec_colors[color].g;
						blue = vec_colors[color].b;
					}
					
					if ((currentx == (currentx)+deltax) && (currenty == (currenty)-deltay))
					{
						if (draw) {
							//add_color_point(sx, sy, red, green, blue);
							add_line(sx, sy, ex, ey, MAKE_BGR(red, green, blue), MAKE_BGR(red, green, blue));
						}
					}

					else {
						if (draw) {
							add_line(sx, sy, ex, ey, MAKE_BGR(red, green, blue), MAKE_BGR(red, green, blue));
							//add_color_line(sx, sy, ex, ey, red, green, blue);
						}
					}
				}
			}
			currentx += deltax; currenty -= deltay;

			break;

		case STAT:

			if (TYPE_SW)
			{
				color = (char)((firstwd & 0x0700) >> 8);
				statz = (firstwd) & 0xff;
			}
			else {
				statz = (firstwd >> 4) & 0x000f; // This is intensity
				color = (firstwd) & 0x000f;
				}
			if (TYPE_TEMP) { sparkle = !(firstwd & 0x0800); }
			if (TYPE_MH)
			{
				sparkle = (firstwd & 0x0800);
				xflip = firstwd & 0x0400;
				vectorbank = 0x18000 + ((firstwd >> 8) & 3) * 0x2000;
				if (lastbank != vectorbank)
				{
					lastbank = vectorbank;
					//wrlog("Vector Bank Switch %x",0x18000 + ((firstwd >> 8) & 3) * 0x2000);
					memcpy(Machine->memory_region[CPU0] + 0x6000, Machine->memory_region[CPU0] + vectorbank, 0x2000);
				}
			}
			break;

		case SCAL:
			b = ((firstwd >> 8) & 0x07) + 8;
			l = (~firstwd) & 0xff;
			scale = (l << VEC_SHIFT) >> b;
			oldscale = scale; //Double the scale for 1024x768 resolution
			scale *= SCALEADJ;
			if (TYPE_MH)
			{
				if (firstwd & 0x0800)
				{
					if (ywindow == 0) { ywindow = 1; clip = currenty >> VEC_SHIFT; }
					else { ywindow = 0; }
				}
			}
			break;

		case CNTR: d = firstwd & 0xff;
			if (TYPE_SW)
			{
				currentx = 379 << VEC_SHIFT;
				currenty = 410 << VEC_SHIFT;
			}
			else
			{
				currentx = 512 << VEC_SHIFT;
				currenty = 512 << VEC_SHIFT;
			}
			break;
		case RTSL:

			if (sp == 0)
			{
				wrlog("AVG Stack Underflow");
				done = 1; sp = MAXSTACK - 1;
			}
			else { sp--; pc = stack[sp]; }
			break;

		case HALT: done = 1; break;
		case JMPL: a = PCTOP + ((firstwd & 0x1fff) << 1); if (a == PCTOP) { done = 1; }
				 else { pc = a; }break;
		case JSRL: a = PCTOP + ((firstwd & 0x1fff) << 1);
			if (a == PCTOP) { done = 1; }
			else {
				stack[sp] = pc;

				if (sp == (MAXSTACK - 1))
				{
					wrlog("AVG Stack Overflow"); done = 1; sp = 0;
				}
				else { sp++; pc = a; }
			}break;

		default: wrlog("Some sort of Error in AVG engine, max stack reached.");
		}
	}
}

int avg_go()
{
	//wrlog("AVG GO CALLED");

	if (AVG_BUSY) 
	{ 
		//wrlog("AVG call with AVG Busy, returning and doing nothing.");
		return 1; 
	}
	else {
		AVG_RUN();
		if (total_length > 1) 
		{
			if (config.debug_profile_code) {
				wrlog("Total AVG Draw Length here is %d at cpu0 cycles ran: %d, video_ticks %d", total_length, get_elapsed_ticks(CPU0), get_video_ticks(0));
			}
			AVG_BUSY = 1; 
			get_video_ticks(0xff); 
			calc_sweep(); 
			timer_pulse(TIME_IN_NSEC(1500) * total_length, CPU0, avg_clr_busy);
		}
		else 
		{
			//wrlog("Erronious AVG Busy Clear, this should never happen.");
			AVG_BUSY = 0;
		}
	}

	return 0;
}

int avg_clear()
{
	AVG_BUSY = 0;
	return 0;
}

int avg_check()
{
	//if ((get_video_ticks(0) > sweep) && AVG_BUSY) { avg_clear(); wrlog("Clearing Busy Flag"); }
	//wrlog("Returning %d for AVG BUSY", AVG_BUSY);
	return {!AVG_BUSY};
}


void avgdvg_go_word_w(unsigned int offset, unsigned int data)
{
	avg_go();
}


void advdvg_go_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite)
{
	avg_go();
}

void avgdvg_reset_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite)
{
	//wrlog("AVG Reset 1 Called");
	//avgdvg_reset(0, 0);
	avg_clear();
}

void avg_init()
{
	TYPE_BZ = 0;
	TYPE_TEMP = 0;
	TYPE_GV = 0;
	TYPE_SD = 0;
	TYPE_MH = 0;
	TYPE_QU = 0;
	TYPE_SD = 0;
	TYPE_SW = 0;
	AVG_BUSY = 0;
	XFLIP = 0;
	YFLIP = 0;

	if (gamenum == BZONE ||
		gamenum == BZONE2 ||
		gamenum == BZONEP ||
		gamenum == BZONEC ||
		gamenum == BRADLEY ||
		gamenum == REDBARON)
	{
		TYPE_BZ = 1;
		set_bw_colors();
		YFLIP = 1;
		AVG_BUSY = 1;
		SCALEADJ = 2;
		NO_CACHE = 1;
		PCTOP = driver[gamenum].vectorram;
	}

	else if (gamenum == SPACDUEL ||
		gamenum == BWIDOW ||
		gamenum == LUNARBAT ||
		gamenum == LUNARBA1)
	{
		TYPE_SD = 1;
		PCTOP = 0x2000;
		NO_CACHE = 1;
		SCALEADJ = 2;
	}
	else if (gamenum == GRAVITAR ||
		gamenum == GRAVITR2 ||
		gamenum == GRAVP) {
		TYPE_GV = 1; PCTOP = 0x2000;  NO_CACHE = 1;
	}
	else if (gamenum == TEMPEST ||
		gamenum == TEMPESTM ||
		gamenum == TEMPEST1 ||
		gamenum == TEMPEST2 ||
		gamenum == TEMPEST3 ||
		gamenum == TEMPTUBE ||
		gamenum == VBRAKOUT ||
		gamenum == VORTEX ||
		gamenum == ALIENSV) {
		TYPE_TEMP = 1; PCTOP = 0x2000; SCALEADJ = 1; NO_CACHE = 1;
		
	}
	else if (gamenum == MHAVOC ||
		gamenum == MHAVOC2 ||
		gamenum == MHAVOCRV ||
		gamenum == MHAVOCP ||
		gamenum == ALPHAONA ||
		gamenum == ALPHAONE) {
		TYPE_MH = 1; PCTOP = 0x4000;
		SCALEADJ = 2;
	}

	else if (gamenum == QUANTUM ||
		gamenum == QUANTUM1 ||
		gamenum == QUANTUMP) {
		TYPE_QU = 1;
		PCTOP = 0;
		NO_CACHE = 1;
		SCALEADJ = 1;
	}
	else if (gamenum == STARWARS || gamenum == STARWAR1)
	{
		TYPE_SW = 1;
		PCTOP = 0;
		YFLIP = 1;
		NO_CACHE = 1;
		SCALEADJ = 3;
	}

	//TYPE_SD=1;PCTOP=0x2000;set_sd_colors();NO_CACHE=1;
}

/*************************************
 *
 *	Video startup
 *
 ************************************/
/*
int dvg_start(void)
{
	//return avgdvg_init(USE_DVG);
}

int dvg_start_asteroid(void)
{
	//return avgdvg_init(USE_DVG_ASTEROID);
}
*/
int avg_start(void)
{
	//return avgdvg_init(USE_AVG);
}

int avg_start_starwars(void)
{
	//return avgdvg_init(USE_AVG_SWARS);
}

int avg_start_tempest(void)
{
	//return avgdvg_init(USE_AVG_TEMPEST);
}

int avg_start_mhavoc(void)
{
	//return avgdvg_init(USE_AVG_MHAVOC);
}

int avg_start_alphaone(void)
{
	//return avgdvg_init(USE_AVG_ALPHAONE);
}

int avg_start_bzone(void)
{
	//return avgdvg_init(USE_AVG_BZONE);
}

int avg_start_quantum(void)
{
	//return avgdvg_init(USE_AVG_QUANTUM);
}

int avg_start_redbaron(void)
{
	//return avgdvg_init(USE_AVG_RBARON);
}

void avg_stop(void)
{
	//busy = 0;
	//vector_clear_list();
}
/*
void dvg_stop(void)
{
	//busy = 0;
	//vector_clear_list();
}
*/