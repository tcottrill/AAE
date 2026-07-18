//==========================================================================
// AAE is a poorly written M.A.M.E (TM) derivitave based on early MAME
// code, 0.29 through .90 mixed with code of my own. This emulator was
// created solely for my amusement and learning and is provided only
// as an archival experience.
//
// All MAME code used and abused in this emulator remains the copyright
// of the dedicated people who spend countless hours creating it. All
// MAME code should be annotated as belonging to the MAME TEAM.
//
// THE CODE BELOW IS FROM MAME 0.36b11 and COPYRIGHT the MAME TEAM.
//==========================================================================
//
// Pong (Atari 1972) -- discrete logic simulation, no CPU and no ROMs.
// Ported from J. Buchmueller's November '99 gate-level simulation
// (MAME 0.36b11: drivers/pong.c, vidhrdw/pong.c/.h, sndhrdw/pong.c).
//
// AAE adaptations:
//  * MAME ran the sim via CPU_GENSYNC calling pong_vh_scanline() as a
//    per-scanline interrupt. AAE has no GENSYNC core; the driver has zero
//    CPUs and run_pong() sweeps all PONG_MAX_V scanlines each frame, then
//    does the screen refresh -- same call order, same math.
//  * The two NE555 monoflops (serve and score-sound timers) were MAME
//    timer_set() callbacks. AAE's timer queue advances on CPU cycles, and
//    there is no CPU here, so they are frame countdowns instead.
//  * The three tone generators (hit / vblank / score) were looped mixer
//    samples volume-gated per frame. Here they are synthesized square
//    waves pushed through one 16-bit mixer stream per frame.
//==========================================================================

#include "aae_mame_driver.h"
#include "driver_registry.h"
#include "old_mame_raster.h"
#include "mixer.h"

// Regression guard: this file must never see OpenGL headers. If this fires,
// a render header re-leaked glew.h -- fix the header, not this guard.
#ifdef __glew_h__
#error "OpenGL headers leaked into a non-render translation unit"
#endif

/* The video synchronization layout, derived from the schematics */
#define PONG_MAX_H	(256+128+64+4+2)	/* 454 pixel clocks per scanline */
#define PONG_MAX_V	(256+4+1)			/* 261 scanlines per frame */
#define PONG_HBLANK (64+16)
#define PONG_VBLANK 16
#define PONG_FPS	60

/* NE555 monoflop durations, converted to whole frames:
   serve (F4): 1.2 * 330k * 4.7uF ~= 1.86 s;  score (G4): 1.2 * 220k * 1uF ~= 0.26 s */
#define SERVE_TIMER_FRAMES ((int)(1.2 * 330000.0 * 4.7 * PONG_FPS / 1e6 + 0.5))
#define SCORE_TIMER_FRAMES ((int)(1.2 * 220000.0 * 1.0 * PONG_FPS / 1e6 + 0.5))

/* Tone frequencies from sndhrdw/pong.c (HJB 99/11/22 corrected clocks) */
#define HIT_CLOCK		((PONG_MAX_V-PONG_VBLANK) * PONG_FPS / 16.0 / 2.0)
#define VBLANK_CLOCK	((PONG_MAX_V-PONG_VBLANK) * PONG_FPS / 16.0 / 4.0)
#define SCORE_CLOCK 	(PONG_MAX_V * PONG_FPS / 32.0)

//==========================================================================
// Simulation state (names follow the schematic chips, as in the original)
//==========================================================================

static int V;
static int VRESET;

static int ATRACT;
static int STOP_G;

static int vpad1_timer; 	/* NE555/B9 */
static int vpad1_count; 	/* 7493/B8 */
#define VPAD1 (vpad1_count < 15)
static int score1;			/* 7490/C7 + 74107/C8.1 */

static int vpad2_timer; 	/* NE555/A9 */
static int vpad2_count; 	/* 7493/A8 */
#define VPAD2 (vpad2_count < 15)
static int score2;			/* 7490/D7 + 74107/C8.2 */

static int speed;			/* 7493/F1 */
static int speed_q1;		/* 74107/H2.2*/
static int speed_q2;		/* 74107/H2.1 */

static int hpos_a;			/* 9316/G7 */
static int hpos_b;			/* 9316/H7 */
static int hpos_c;			/* 74107/G6.2 */
static int HVID;			/* 7420/H6.2 */

static int vpos_a;			/* 9316/B3 */
static int vpos_b;			/* 9316/A3 */
static int VVID;			/* 7402/D2.4 */

static int vvel_b;			/* 7474/A5.2 */
static int vvel_c;			/* 7474/A5.1 */
static int vvel_d;			/* 7474/B5.1 */
static int hit_vbl_q;		/* 74107/A2.1 */
static int hit_vbl_0;		/* direction during last frame */

static int v_velocity;		/* 7483/B4 */

static int score_q; 		/* 74107/H3.2 */
#define L	(score_q == 1)
#define R	(score_q == 0)

static int hit_a;			/* 7400/H4.3 */
static int hit_b;			/* 7400/H4.2 */

static int hit_vblank;		/* 74107/F3.1 */

static int hit_sound;		/* 7474/C2.1 */

/* NE555 monoflops as frame countdowns: <0 = idle, >=0 = frames remaining */
static int score_sound_timer = -1;	/* NE555/G4 */
static int serve_timer = -1;		/* NE555/F4 */

static int pong_hit_sound = 0;
static int pong_vblank_sound = 0;
static int pong_score_sound = 0;

static void pong_hit_detector(void);
static void pong_vertical_velocity(void);
static void pong_7seg(int h0, int n);

//==========================================================================
// Sound: three square waves gated by the pong_*_sound flags
//==========================================================================

static int pong_channel = -1;
static int16_t* pong_frame_buf = nullptr;
static int pong_frame_len = 0;
static double pong_phase[3] = { 0, 0, 0 };

static int pong_sh_start(void)
{
	pong_frame_len = config.samplerate / PONG_FPS;

	pong_channel = mixer_alloc_channel(MIXER_CHIP_STREAM_RANGE_LOW, MIXER_FIRST_RESERVED_CHANNEL);
	if (pong_channel < 0)
	{
		LOG_INFO("Pong: no free mixer channel in chip stream range");
		return 1;
	}

	pong_frame_buf = (int16_t*)malloc(pong_frame_len * sizeof(int16_t));
	if (!pong_frame_buf)
		return 1;
	memset(pong_frame_buf, 0, pong_frame_len * sizeof(int16_t));

	stream_start(pong_channel, 0, 16, PONG_FPS, /*stereo=*/false);
	return 0;
}

static void pong_sh_stop(void)
{
	if (pong_channel >= 0)
		stream_stop(pong_channel, 0);
	pong_channel = -1;
	free(pong_frame_buf);
	pong_frame_buf = nullptr;
}

static void pong_sh_update(void)
{
	static const double freq[3] = { HIT_CLOCK, VBLANK_CLOCK, SCORE_CLOCK };
	const int gate[3] = { pong_hit_sound, pong_vblank_sound, pong_score_sound };
	const int amp = 6000;	/* per-voice, comfortably clear of clipping when all three play */

	if (pong_channel < 0)
		return;

	for (int s = 0; s < pong_frame_len; s++)
	{
		int v = 0;
		for (int t = 0; t < 3; t++)
		{
			pong_phase[t] += freq[t] / (double)config.samplerate;
			if (pong_phase[t] >= 1.0) pong_phase[t] -= 1.0;
			if (gate[t])
				v += (pong_phase[t] < 0.5) ? amp : -amp;
		}
		pong_frame_buf[s] = (int16_t)v;
	}
	stream_update(pong_channel, pong_frame_buf);
}

//==========================================================================
// Video / game logic (verbatim port of vidhrdw/pong.c)
//==========================================================================

static int pong_vh_start(void)
{
	ATRACT = 0;
	STOP_G = 1;

	vpad1_timer = 0;
	vpad1_count = 0;
	score1 = 0;

	vpad2_timer = 0;
	vpad2_count = 0;
	score2 = 0;

	speed = 0;
	speed_q1 = 0;
	speed_q2 = 0;

	score_q = 0;

	hpos_a = 0;
	hpos_b = 0;
	hpos_c = 0;

	vpos_a = 0;
	vpos_b = 0;

	vvel_b = 0;
	vvel_c = 0;
	vvel_d = 0;
	hit_vbl_q = 0;

	v_velocity = 0;

	hit_a = 0;
	hit_b = 1;

	hit_sound = 0;

	score_sound_timer = -1;
	serve_timer = -1;

	V = 0;
	VRESET = 1;

	pong_hit_detector();

	tmpbitmap = osd_create_bitmap(PONG_MAX_H, PONG_MAX_V);
	if (!tmpbitmap)
		return 1;
	osd_clearbitmap(tmpbitmap);

	return 0;
}

static void pong_vh_stop(void)
{
	if (tmpbitmap) { osd_free_bitmap(tmpbitmap); tmpbitmap = nullptr; }
}

static void pong_vh_screenrefresh(void)
{
	if (readinputportbytag("IN0") & 1)
	{
		if (STOP_G)
		{
			STOP_G = 0;
			ATRACT = 1;
			score1 = 0;
			score2 = 0;
			if (serve_timer < 0)
			{
				/* monoflop (NE555/F4) with a 330 kOhms resistor and 4.7 uF capacitor */
				serve_timer = SERVE_TIMER_FRAMES;
			}
		}
	}

	copybitmap(main_bitmap, tmpbitmap, 0, 0, 0, 0, &Machine->drv->visible_area, TRANSPARENCY_NONE, 0);
	fillbitmap(tmpbitmap, Machine->pens[0], &Machine->drv->visible_area);

	/* reset the VPAD timers */
	vpad1_timer = 0;
	vpad2_timer = 0;

	/* save state of the vblank flip-flop 74107/A2 */
	hit_vbl_0 = hit_vbl_q;

	V = 0;
	VRESET = 1;
}

#define H4		(H&4)
#define H8		(H&8)
#define H16 	(H&16)
#define H32 	(H&32)
#define H64 	(H&64)
#define H128	(H&128)
#define H256	(H&256)

#define HBLANK	(H < PONG_HBLANK)

#define V4		(V&4)
#define V8		(V&8)
#define V16 	(V&16)
#define V32 	(V&32)
#define V64 	(V&64)
#define V128	(V&128)

#define VBLANK	(V < PONG_VBLANK)

/* The VERTICAL SYNC counters are incremented once per scanline.
   We emulate this by calling pong_vh_scanline() 261 times per frame. */
static void pong_vh_scanline(void)
{
	int HRESET, H;
	int pen1 = Machine->pens[1];

	if (V >= PONG_MAX_V)
		return;

	/* THE NET: one pixel wide at H==256, dashed every four scanlines */
	if (!V4 && !VBLANK)
		plot_pixel(tmpbitmap, 256, V, pen1);

	/* THE SCORE: 7-segment decode into scanlines 32..63 */
	if (V32 && !V64 && !V128)
	{
		pong_7seg(128, score1 > 9 ? 1 : 15);
		pong_7seg(128 + 32, score1 % 10);
		pong_7seg(256 + 32, score2 > 9 ? 1 : 15);
		pong_7seg(256 + 32 + 32, score2 % 10);
	}

	/* THE ATRACT MODE: pads and ball only drawn while a game is running */
	if (ATRACT)
	{
		/* check for the left VPAD */
		if (VPAD1)
		{
			plot_pixel(tmpbitmap, 128 + 0, V, pen1);
			plot_pixel(tmpbitmap, 128 + 1, V, pen1);
			plot_pixel(tmpbitmap, 128 + 2, V, pen1);
			plot_pixel(tmpbitmap, 128 + 3, V, pen1);
			plot_pixel(tmpbitmap, 128 + 4, V, pen1);
			plot_pixel(tmpbitmap, 128 + 5, V, pen1);
			plot_pixel(tmpbitmap, 128 + 6, V, pen1);
			plot_pixel(tmpbitmap, 128 + 7, V, pen1);
			vpad1_count += 1;
		}
		else if (vpad1_timer == 0)
		{
			vpad1_timer = (V >= (int)readinputportbytag("IN1"));
			if (vpad1_timer)
				vpad1_count = 0;
		}

		/* check for the right VPAD */
		if (VPAD2)
		{
			plot_pixel(tmpbitmap, 256 + 128 + 0, V, pen1);
			plot_pixel(tmpbitmap, 256 + 128 + 1, V, pen1);
			plot_pixel(tmpbitmap, 256 + 128 + 2, V, pen1);
			plot_pixel(tmpbitmap, 256 + 128 + 3, V, pen1);
			plot_pixel(tmpbitmap, 256 + 128 + 4, V, pen1);
			plot_pixel(tmpbitmap, 256 + 128 + 5, V, pen1);
			plot_pixel(tmpbitmap, 256 + 128 + 6, V, pen1);
			plot_pixel(tmpbitmap, 256 + 128 + 7, V, pen1);
			vpad2_count += 1;
		}
		else if (vpad2_timer == 0)
		{
			vpad2_timer = (V >= (int)readinputportbytag("IN2"));
			if (vpad2_timer)
				vpad2_count = 0;
		}

		if (VRESET)
		{
			int a, b, c;
			a = ((speed & 2) || (speed & 8)) ? 1 : 0;
			b = ((speed & 2) && (speed & 8)) ? 0 : 1;
			c = (a && b) ? 0 : 1;
			if (c == 1) speed_q1 = 0;
			if (a == 1) speed_q2 = 0;
		}
		else
		{
			if (speed_q1)
			{
				if (!speed_q2)
					speed_q1 ^= 1;
				speed_q2 = 1;
			}
			else
			{
				speed_q1 ^= 1;
			}
		}

		/* VERTICAL POSITION counters 9316/B3 + 9316/A3 */
		if (vpos_a == 15 && !VBLANK && vpos_b == 15)
		{
			pong_vertical_velocity();
			vpos_a = v_velocity;
			vpos_b = 0;
		}
		else
		{
			if (vpos_a == 15 && !VBLANK)
			{
				vpos_b = (vpos_b + 1) % 16;
				if (vpos_b == 15)
				{
					/* eventually enable hit sound */
					pong_hit_sound = hit_sound;
					hit_sound = 0;
				}
			}
		}
		if (!VBLANK)
		{
			vpos_a = (vpos_a + 1) % 16;
		}

		if (V == PONG_VBLANK)
		{
			/* eventually enable hit VBLANK sound */
			if (hit_vblank > 0)
				hit_vblank--;
			pong_vblank_sound = hit_vblank;
		}

		VVID = (vpos_a >= 12 && vpos_b == 15) ? 1 : 0;

		if (serve_timer < 0)
		{
			if (VVID && V == VBLANK)	/* VBLANK just went low ? */
			{
				hit_vbl_q = hit_vbl_0 ^ 1;
				hit_vblank = 2;
			}

			/* sweep the HORIZONTAL SYNC counters: ball position + hit detection */
			for (HRESET = 1, H = 0; H < PONG_MAX_H; HRESET ? HRESET = 0 : H++)
			{
				if (!HBLANK)
				{
					if (hpos_a == 15 && hpos_b == 15 && hpos_c)
					{
						pong_hit_detector();
						hpos_a = 8 + hit_b * 2 + hit_a;
						hpos_b = 8;
						hpos_c ^= 1;
					}
					else
					{
						if (hpos_a == 15)
						{
							hpos_b = (hpos_b + 1) % 16;
							if (hpos_b == 0)
								hpos_c ^= 1;
						}
						hpos_a = (hpos_a + 1) % 16;
					}
				}
				HVID = (hpos_a >= 12 && hpos_b == 15 && hpos_c) ? 1 : 0;

				if (HVID && VVID)
				{
					if (!VBLANK)
						plot_pixel(tmpbitmap, H, V, pen1);

					if (VPAD1 && H >= 128 && H < 128 + 8)
					{
						/* 7474/H3.2 is cleared by the HIT1 signal */
						score_q = 0;
						if (hit_sound == 0)
						{
							pong_hit_sound = hit_sound = 1;
							if (speed < 10)
								speed += 1;
						}
						vvel_b = (vpad1_count & 2) ? 0 : 1;
						vvel_c = (vpad1_count & 4) ? 0 : 1;
						vvel_d = (vpad1_count & 8) ? 0 : 1;
						hit_vbl_q = 0;
					}
					else if (VPAD2 && H >= 256 + 128 && H < 256 + 128 + 8)
					{
						/* 7474/H3.2 is set by the HIT2 signal */
						score_q = 1;
						if (hit_sound == 0)
						{
							pong_hit_sound = hit_sound = 1;
							if (speed < 10)
								speed += 1;
						}
						vvel_b = (vpad2_count & 2) ? 0 : 1;
						vvel_c = (vpad2_count & 4) ? 0 : 1;
						vvel_d = (vpad2_count & 8) ? 0 : 1;
						hit_vbl_q = 0;
					}
					else if (HRESET)  /* no hit and HBLANK starts ? */
					{
						/* RESET SPEED */
						speed = 0;
						/* start the SCORE SOUND TIMER NE555/G4 */
						if (ATRACT && score_sound_timer < 0)
						{
							score_sound_timer = SCORE_TIMER_FRAMES;
							pong_score_sound = 1;
							/* raising edge of SC toggles D-type flip-flop 7474/H3.2 */
							score_q ^= 1;
							if (L)
							{
								if (++score1 == ((readinputportbytag("IN0") & 2) ? 15 : 11))
								{
									STOP_G = 1;
									ATRACT = 0;
								}
							}
							if (R)
							{
								if (++score2 == ((readinputportbytag("IN0") & 2) ? 15 : 11))
								{
									STOP_G = 1;
									ATRACT = 0;
								}
							}
						}
						if (serve_timer < 0)
						{
							serve_timer = SERVE_TIMER_FRAMES;
						}
					}
				}
			}
		}	/* !serve_timer */
	}	/* ATRACT */

	VRESET = 0;
	V++;
}

/* HIT DETECTOR (7493/F1 + H2 flip-flops + H4.1) */
static void pong_hit_detector(void)
{
	int q, a;

	q = (speed_q1 == 1 && speed_q2 == 1) ? 0 : 1;
	a = (L && q) ? 0 : 1;
	hit_b = (R && q) ? 0 : 1;
	hit_a = (a == 0 || hit_b == 0) ? 1 : 0;
}

/* VERTICAL VELOCITY (latched pad-counter bits vs the vblank flip-flop) */
static void pong_vertical_velocity(void)
{
	int a = 0;	/* A4 is tied to GND */
	int b = 6;	/* B2 and B3 are tied to Vcc, B4 tied to GND */

	if (vvel_b != hit_vbl_q)
		a += 1;
	if (vvel_c != hit_vbl_q)
		a += 2;
	if (vvel_d == hit_vbl_q)
		b += 1;
	else
		a += 4;

	v_velocity = (a + b) & 15;
}

/* Decode a value into its active 7-segment bars, per the E2/E3/F2 gates */
static void pong_7seg(int h0, int n)
{
	static const UINT8 decode_7seg[16][7] = {
		{1,  1,  1,  1,  1,  1,  0},	/*	1 (sic -- table order from MAME) */
		{0,  1,  1,  0,  0,  0,  0},	/*	0 */
		{1,  1,  0,  1,  1,  0,  1},	/*	2 */
		{1,  1,  1,  1,  0,  0,  1},	/*	3 */
		{0,  1,  1,  0,  0,  1,  1},	/*	4 */
		{1,  0,  1,  1,  0,  1,  1},	/*	5 */
		{1,  0,  1,  1,  1,  1,  1},	/*	6 */
		{1,  1,  1,  0,  0,  0,  0},	/*	7 */
		{1,  1,  1,  1,  1,  1,  1},	/*	8 */
		{1,  1,  1,  1,  0,  1,  1},	/*	9 */
		{0,  0,  0,  1,  1,  0,  1},	/* 10 */
		{0,  0,  1,  1,  0,  1,  1},	/* 11 */
		{0,  1,  0,  0,  0,  1,  1},	/* 12 */
		{1,  0,  0,  1,  0,  1,  1},	/* 13 */
		{0,  0,  0,  1,  1,  1,  1},	/* 14 */
		{0,  0,  0,  0,  0,  0,  0},	/* 15 */
	};
	int H;

	n %= 16;

	for (H = h0; H < h0 + 32; H += 4)
	{
		int f = (decode_7seg[n][5] && (!H4 && !H8 && !!H16) && !V16) ? 0 : 1;
		int e = (decode_7seg[n][4] && V16 && (!H4 && !H8 && !!H16)) ? 0 : 1;
		int b = (decode_7seg[n][1] && (!!(H4 && H8) && !!H16) && !V16) ? 0 : 1;
		int c = (decode_7seg[n][2] && (!!(H4 && H8) && !!H16) && V16) ? 0 : 1;
		int a = (decode_7seg[n][0] && !V16 && (H16 && !V8 && !V4)) ? 0 : 1;
		int g = (decode_7seg[n][6] && !V16 && (V8 && V4 && H16)) ? 0 : 1;
		int d = (decode_7seg[n][3] && V16 && (V8 && V4 && H16)) ? 0 : 1;

		int score = (!a || !b || !c || !d || !e || !f || !g);

		if (score)
		{
			int pen2 = Machine->pens[2];
			plot_pixel(tmpbitmap, H + 0, V, pen2);
			plot_pixel(tmpbitmap, H + 1, V, pen2);
			plot_pixel(tmpbitmap, H + 2, V, pen2);
			plot_pixel(tmpbitmap, H + 3, V, pen2);
		}
	}
}

#undef VBLANK
#undef HBLANK

//==========================================================================
// Palette
//==========================================================================

static const unsigned char pong_palette[] =
{
	0x00,0x00,0x00, /* black */
	0xff,0xff,0xff, /* white (1k resistor) */
	0xd4,0xd4,0xd4, /* slightly darker white (1.2k resistor) */
};

static void pong_init_palette(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom)
{
	memcpy(palette, pong_palette, sizeof(pong_palette));
}

//==========================================================================
// Driver glue
//==========================================================================

int init_pong(void)
{
	LOG_INFO("INIT: Pong Driver Init (discrete logic simulation, no CPU)");

	if (pong_vh_start())
		return 1;
	pong_sh_start();

	return 0;
}

void run_pong(void)
{
	/* tick the NE555 monoflops once per frame */
	if (serve_timer >= 0 && --serve_timer < 0)
	{
		/* falling edge of serve timer (output of NE555/F4) */
		hpos_a = 0;
		hpos_b = 0;
		hpos_c = 0;
	}
	if (score_sound_timer >= 0 && --score_sound_timer < 0)
	{
		/* disable score sound */
		pong_score_sound = 0;
	}

	/* one field: the vertical counter sweeps every scanline... */
	for (int line = 0; line < PONG_MAX_V; line++)
		pong_vh_scanline();

	/* ...then the frame is presented (start button, bitmap flip, V reset) */
	pong_vh_screenrefresh();

	pong_sh_update();
}

void end_pong(void)
{
	pong_sh_stop();
	pong_vh_stop();
}

INPUT_PORTS_START(pong)
PORT_START("IN0")	/* IN0 buttons */
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_START1)
PORT_DIPNAME(0x02, 0x00, "Ending Score")
PORT_DIPSETTING(0x00, "11")
PORT_DIPSETTING(0x02, "15")
PORT_BIT(0xfc, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("IN1")	/* control 1: scanline where the left pad starts */
PORT_ANALOG(0xff, (PONG_MAX_V - 15) / 2, IPT_AD_STICK_Y, 100, 5, PONG_VBLANK - 12, 243)

PORT_START("IN2")	/* control 2 */
PORT_ANALOG(0xff, (PONG_MAX_V - 15) / 2, IPT_AD_STICK_Y | IPF_PLAYER2, 100, 5, PONG_VBLANK - 12, 243)
INPUT_PORTS_END

AAE_DRIVER_BEGIN(drv_pong, "pong", "Pong")
AAE_DRIVER_ROM(nullptr)		/* no ROMs -- the real hardware had none either */
AAE_DRIVER_FUNCS(&init_pong, &run_pong, &end_pong)
AAE_DRIVER_INPUT(input_ports_pong)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	AAE_CPU_NONE_ENTRY(),	/* discrete logic: there is no CPU */
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(60, 0, VIDEO_TYPE_RASTER_BW, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(PONG_MAX_H, PONG_MAX_V, PONG_HBLANK, PONG_MAX_H - 1, PONG_VBLANK, PONG_MAX_V - 1)
AAE_DRIVER_RASTER(0, 3, 6, pong_init_palette)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_pong)
