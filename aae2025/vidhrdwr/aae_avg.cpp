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
// THE CODE BELOW IS FROM MAME and COPYRIGHT the MAME TEAM.
//============================================================================


#include "aae_avg.h"
#include "timer.h"
#include "cohen_sutherland_clipping.h"

#pragma warning( disable : 4996 4244)

UINT8* tempest_colorram;
UINT16 quantum_colorram[0x20];
UINT16* quantum_vectorram;
unsigned char* vec_mem;

static int vector_engine = USE_AVG;
static int pc = 0;

static int XFLIP = 0;
static int YFLIP = 0;
static int AVG_BUSY = 0;
static int TOTAL_LENGTH = 0;
static int scale_adj = 2;

static int vectorbank = 0x18000;
static int lastbank = 0;
static float sweep = 0;

static int NO_CACHE = 0;

static int (*opcode_handler)(int) = nullptr;
static void (*draw_handler)(int, int, int, int, int, int) = nullptr;
static void (*stat_handler)(int) = nullptr;


#define VCTR 0
#define HALT 1
#define SVEC 2
#define STAT 3
#define CNTR 4
#define JSRL 5
#define RTSL 6
#define JMPL 7
#define SCAL 8

#define MAXSTACK 8

#define memrdwd_flip(address) ((vec_mem[pc+1]) | (vec_mem[pc]<<8))
#define memrdwd(address)  ((vec_mem[pc]) | (vec_mem[pc+1]<<8))

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


//
//
// Specialized Handlers
//
//

static int get_opcode_avg(int avg_pc)
{
	int opcode = memrdwd(avg_pc);
	pc += 2;
	return opcode;
}

static int get_opcode_starwars(int avg_pc)
{
	int opcode = memrdwd_flip(avg_pc);
	pc += 2;
	return opcode;
}

static void draw_bzone(int sx, int sy, int ex, int ey, int z, int color)
{
	int clip;
	int BZ_CLIP = 768 - 50;

	z = (z & 0xe) << 4;

	set_clip_rect(0, 0, 1024, BZ_CLIP);

	if (vector_engine == USE_AVG_RBARON)
		add_line(sx, sy, ex, ey, z, MAKE_BGR(z, z, z));
	else
	{
		if (color) add_line(sx, sy, ex, ey, z, MAKE_BGR(z, z, z));
		else
		{
			if (clip = ClipLine(&sx, &sy, &ex, &ey))
			{
				add_line(sx, sy, ex, ey, z, MAKE_BGR(z, z, z));
			}
		}
	}
}

static void draw_avg(int sx, int sy, int ex, int ey, int z, int color)
{
	add_line(sx, sy, ex, ey, (z & 0xe) << 4, VECTOR_COLOR111(color));
}

static void draw_starwars(int sx, int sy, int ex, int ey, int z, int color)
{
	add_line(sx, sy, ex, ey, z, VECTOR_COLOR111(color));
}

static void draw_tempest(int sx, int sy, int ex, int ey, int z, int color)
{
	uint8_t data = tempest_colorram[color];
	int r, g, b;
	int bit3 = (~data >> 3) & 1;
	int bit2 = (~data >> 2) & 1;
	int bit1 = (~data >> 1) & 1;
	int bit0 = (~data >> 0) & 1;

	r = bit1 * 0xf3 + bit0 * 0x0c;
	g = bit3 * 0xf3;
	b = bit2 * 0xf3;

	set_clip_rect(230, 240, 780, 830);
	int clip = ClipLine(&sx, &sy, &ex, &ey);
	if (clip)
	{
		add_line(sx, sy, ex, ey, z << 4, MAKE_RGB(r, g, b));
	}
}

static void draw_quantum(int sx, int sy, int ex, int ey, int z, int color)
{
	UINT16 data;
	int r, g, b;
	data = quantum_colorram[color];
	int bit3 = (~data >> 3) & 1;
	int bit2 = (~data >> 2) & 1;
	int bit1 = (~data >> 1) & 1;
	int bit0 = (~data >> 0) & 1;

	g = bit1 * 0xaa + bit0 * 0x54;
	b = bit2 * 0xce;
	r = bit3 * 0xce;

	add_line(sx, sy, ex, ey, z << 4, MAKE_RGB(r, g, b));
}

static void draw_mhavoc(int sx, int sy, int ex, int ey, int z, int color)
{
	add_line(sx, sy, ex, ey, (z & 0xe) << 4, VECTOR_COLOR111(color));
}

static void stat_avg(int word)
{
}

static void stat_swars(int word)
{
}
static void stat_tempest(int word)
{
}

static void stat_bzone(int word)
{
}

static void stat_mhavoc(int word)
{
}

void AVG_RUN(void)
{
	int sp;
	int stack[8];
	int flipword = 0;
	int scale = 0;
	int statz = 0;
	int xflip = 0;
	static int color;
	static int  sparkle = 0;
	static int spkl_shift = 0;
	int currentx = 0;
	int currenty = 0;
	int done = 0;
	int firstwd = 0;
	int secondwd = 0;
	int opcode;
	int x, y, z = 0, b, l, d, a;
	int deltax, deltay = 0;
	int COMPSHFT = 13;

	int ywindow = 1;
	int clip = 0;
	int oldscale = 0;
	int intensity = 0;

	//int TEMP_CLIP = 240;
	int sy = 0;
	int ey = 0;
	int sx = 0;
	int ex = 0;
	int data = 0;

	pc = 0;// Machine->gamedrv->vectorram;

	if (vector_engine == USE_AVG_QUANTUM) COMPSHFT = 12;
	sp = 0;
	statz = 0;
	scale = 0;
	total_length = 0;
	if (NO_CACHE) cache_clear();

	while (!done)
	{
		firstwd = opcode_handler(pc);
		opcode = firstwd >> 13;

		//Get the second word if it's a draw command
		if (opcode == VCTR)
			secondwd = opcode_handler(pc);

		// SCAL is a variant of STAT; convert it here
		else if (opcode == STAT && (firstwd & 0x1000))
			opcode = SCAL;

		switch (opcode)
		{
		case VCTR:
			x = twos_comp_val(secondwd, COMPSHFT);
			y = twos_comp_val(firstwd, COMPSHFT);
			z = (secondwd >> 12) & 0x0e;
			if (sparkle) { color = rand() & 0xf; }
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

			sx = currentx >> VEC_SHIFT;
			sy = currenty >> VEC_SHIFT;
			ex = (currentx + deltax) >> VEC_SHIFT;
			ey = ((currenty - deltay) >> VEC_SHIFT);

			if (vector_engine == USE_AVG_SWARS)
			{
				z = (z * statz) / 8;
				if (z > 0xff)
					z = 0xff;
			}
			else { if (z == 2)  z = statz; }

			if (z)
			{
				draw_handler(sx, sy, ex, ey, z, color);
			}

			currentx += deltax; currenty -= deltay;

			break;

		case STAT: //(AVG STROBE2)

			if (vector_engine == USE_AVG_BZONE)
			{
				statz = (firstwd >> 4) & 0xf;
				color = (firstwd) & 0x7;
			}

			if (vector_engine == USE_AVG_SWARS)
			{
				color = (firstwd >> 8) & 7;
				statz = (firstwd) & 0xff;
			}
			else {
				statz = (firstwd >> 4) & 0xf; // This is intensity
				color = (firstwd) & 0x7;
			}
			if (vector_engine == USE_AVG_TEMPEST)
			{
				sparkle = !(firstwd & 0x0800);
				color = firstwd & 0xf;
				statz = (firstwd >> 4) & 0xf;
			}
			if (vector_engine == USE_AVG_MHAVOC)
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
			scale *= scale_adj;
			if (vector_engine == USE_AVG_MHAVOC)
			{
				if (firstwd & 0x0800)
				{
					if (ywindow == 0) { ywindow = 1; clip = currenty >> VEC_SHIFT; }
					else { ywindow = 0; }
				}
			}
			break;

			// STROBE3
		case CNTR:
			d = firstwd & 0xff;
			if (vector_engine == USE_AVG_SWARS)
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
		case JMPL: a = ((firstwd & 0x1fff) << 1); if (a == 0) { done = 1; }
				 else { pc = a; }break;
		case JSRL: a = ((firstwd & 0x1fff) << 1);
			if (a == 0) { done = 1; }
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
	return { !AVG_BUSY };
}

void avgdvg_go_word_w(UINT32 address, UINT16 data, struct MemoryWriteWord* psMemWrite)
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

void avgdvg_reset_word_w(UINT32 address, UINT16 data, struct MemoryWriteWord* pMemWrite)
{
	avg_clear();
}

int avg_init(int type)
{
	AVG_BUSY = 0;
	XFLIP = 0;
	YFLIP = 0;

	vector_engine = type;
	opcode_handler = get_opcode_avg;

	switch (type)
	{
	case USE_AVG:
	{
		vec_mem = &Machine->memory_region[CPU0][Machine->gamedrv->vectorram];
		NO_CACHE = 1;
		scale_adj = 2;
		draw_handler = draw_avg;
		stat_handler = stat_avg;
		break;
	}

	case USE_AVG_BZONE:
	{
		YFLIP = 1;
		AVG_BUSY = 1;
		scale_adj = 2;
		NO_CACHE = 1;
		vec_mem = &Machine->memory_region[CPU0][Machine->gamedrv->vectorram];
		draw_handler = draw_bzone;
		stat_handler = stat_bzone;
		break;
	}

	case USE_AVG_RBARON:
	{
		YFLIP = 1;
		AVG_BUSY = 1;
		scale_adj = 2;
		NO_CACHE = 1;
		vec_mem = &Machine->memory_region[CPU0][Machine->gamedrv->vectorram];
		draw_handler = draw_bzone;
		stat_handler = stat_bzone;
		break;
	}

	case USE_AVG_TEMPEST:
	{
		vec_mem = &Machine->memory_region[CPU0][Machine->gamedrv->vectorram];
		scale_adj = 1; 
		NO_CACHE = 1;
		tempest_colorram = &memory_region(REGION_CPU1)[0x800];
		draw_handler = draw_tempest;
		break;
	}
	case USE_AVG_QUANTUM:
	{
		vec_mem = &vec_ram[0];
		NO_CACHE = 1;
		scale_adj = 1;
		opcode_handler = get_opcode_avg;
		draw_handler = draw_quantum;
		break;
	}
	case USE_AVG_SWARS:
	{
		vec_mem = &Machine->memory_region[CPU0][Machine->gamedrv->vectorram];
		YFLIP = 1;
		NO_CACHE = 1;
		scale_adj = 3;
		opcode_handler = get_opcode_starwars;
		draw_handler = draw_starwars;
		break;
	}
	case USE_AVG_MHAVOC:
	{
		vec_mem = &Machine->memory_region[CPU0][Machine->gamedrv->vectorram];
		scale_adj = 2;
		draw_handler = draw_mhavoc;
		break;
	}
	}

	avg_clear();
	return 1;
}

/*************************************
 *
 *	Video startup
 *
 ************************************/

int avg_start(void)
{
	return avg_init(USE_AVG);
}

int avg_start_starwars(void)
{
	return avg_init(USE_AVG_SWARS);
}

int avg_start_tempest(void)
{
	return avg_init(USE_AVG_TEMPEST);
}

int avg_start_mhavoc(void)
{
	return avg_init(USE_AVG_MHAVOC);
}

int avg_start_alphaone(void)
{
	return avg_init(USE_AVG_ALPHAONE);
}

int avg_start_bzone(void)
{
	return avg_init(USE_AVG_BZONE);
}

int avg_start_quantum(void)
{
	return avg_init(USE_AVG_QUANTUM);
}

int avg_start_redbaron(void)
{
	return avg_init(USE_AVG_RBARON);
}

void avg_stop(void)
{
	AVG_BUSY = 0;
	avg_clear();
}