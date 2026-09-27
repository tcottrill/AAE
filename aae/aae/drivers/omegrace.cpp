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
// THE CODE BELOW IS FROM MAME and COPYRIGHT the MAME TEAM.
//==========================================================================

/***************************************************************************

This driver is dedicated to my loving wife Natalia Wiebelt
									  and my daughter Lara Anna Maria
Summer 1997 Bernd Wiebelt

Many thanks to Al Kossow for the original sources and the solid documentation.
Without him, I could never had completed this driver.

--------

Most of the info here comes from the wiretap archive at:
http://www.spies.com/arcade/simulation/gameHardware/

Omega Race Memory Map
Version 1.1 (Jul 24,1997)
---------------------
0000 - 3fff	PROM
4000 - 4bff	RAM (3k)
5c00 - 5cff	NVRAM (256 x 4bits)
8000 - 8fff	Vec RAM (4k)
9000 - 9fff	Vec ROM (4k)

15 14 13 12 11 10
--+--+--+--+--+--
0  0  0  0                       M8 - 2732  (4k)
0  0  0  1                       L8 - 2732
0  0  1  0                       K8 - 2732
0  0  1  1                       J8 - 2732

0  1  -  0  0  0                 RAM (3k)
0  1  -  0  0  1
0  1  -  0  1  0

0  1  -  1  1  1                 4 Bit BB RAM (d0-d3)

1  -  -  0  0                    Vec RAM (4k)
1  -  -  0  1
1  -  -  1  0			 Vec ROM (2k) E1
1  -  -  1  1                    Vec ROM (2k) F1

I/O Ports

8	Start/ (VG start)
9	WDOG/  (Reset watchdog)
A	SEQRES/ (VG stop/reset?)
B	RDSTOP/ d7 = stop (VG running if 0)

10 I	DIP SW C4 (game ship settings)

	6 5  4 3  2 1
					  1st bonus ship at
		| |  | |  0 0  40,000
		| |  | |  0 1  50,000
		| |  | |  1 0  70,000
		| |  | |  1 1 100,000
		| |  | |      2nd and  3rd bonus ships
		| |  0 0      150,000   250,000
		| |  0 1      250,000   500,000
		| |  1 0      500,000   750,000
		| |  1 1      750,000 1,500,000
		| |           ships per credit
		0 0           1 credit = 2 ships / 2 credits = 4 ships
		0 1           1 credit = 2 ships / 2 credits = 5 ships
		1 0           1 credit = 3 ships / 2 credits = 6 ships
		1 1           1 credit = 3 ships / 2 credits = 7 ships

11 I	7 = Test
	6 = P1 Fire
	5 = P1 Thrust
	4 = Tilt

	1 = Coin 2
	0 = Coin 1

12 I	7 = 1P1CR
	6 = 1P2CR

	3 = 2P2CR -+
	2 = 2P1CR  |
	1 = P2Fire |
	0 = P2Thr -+ cocktail only

13 O   7 =
		6 = screen reverse
		5 = 2 player 2 credit start LED
		4 = 2 player 1 credit start LED
		3 = 1 player 1 credit start LED
		2 = 1 player 1 credit start LED
		1 = coin meter 2
		0 = coin meter 1

14 O	sound command (interrupts sound Z80)

15 I	encoder 1 (d7-d2)

	The encoder is a 64 position Grey Code encoder, or a
	pot and A to D converter.

	Unlike the quadrature inputs on Atari and Sega games,
		Omega Race's controller is an absolute angle.

	0x00, 0x04, 0x14, 0x10, 0x18, 0x1c, 0x5c, 0x58,
	0x50, 0x54, 0x44, 0x40, 0x48, 0x4c, 0x6c, 0x68,
	0x60, 0x64, 0x74, 0x70, 0x78, 0x7c, 0xfc, 0xf8,
	0xf0, 0xf4, 0xe4, 0xe0, 0xe8, 0xec, 0xcc, 0xc8,
	0xc0, 0xc4, 0xd4, 0xd0, 0xd8, 0xdc, 0x9c, 0x98,
	0x90, 0x94, 0x84, 0x80, 0x88, 0x8c, 0xac, 0xa8,
	0xa0, 0xa4, 0xb4, 0xb0, 0xb8, 0xbc, 0x3c, 0x38,
	0x30, 0x34, 0x24, 0x20, 0x28, 0x2c, 0x0c, 0x08

16 I	encoder 2 (d5-d0)

	The inputs aren't scrambled as they are on the 1 player
		encoder

17 I	DIP SW C6 (coin/cocktail settings)

		8  7  6 5 4  3 2 1
							 coin switch 1
		|  |  | | |  0 0 0   1 coin  2 credits
		|  |  | | |  0 0 1   1 coin  3 credits
		|  |  | | |  0 1 0   1 coin  5 credits
		|  |  | | |  0 1 1   4 coins 5 credits
		|  |  | | |  1 0 0   3 coins 4 credits
		|  |  | | |  1 0 1   2 coins 3 credits
		|  |  | | |  1 1 0   2 coins 1 credit
		|  |  | | |  1 1 1   1 coin  1 credit
		|  |  | | |
		|  |  | | |          coin switch 2
		|  |  0 0 0          1 coin  2 credits
		|  |  0 0 1          1 coin  3 credits
		|  |  0 1 0          1 coin  5 credits
		|  |  0 1 1          4 coins 5 credits
		|  |  1 0 0          3 coins 4 credits
		|  |  1 0 1          2 coins 3 credits
		|  |  1 1 0          2 coins 1 credit
		|  |  1 1 1          1 coin  1 credit
		|  |
		|  0                 coin play
		|  1                 free play
		|
		0                    normal
		1                    cocktail

display list format: (4 byte opcodes)

+------+------+------+------+------+------+------+------+
|DY07   DY06   DY05   DY04   DY03   DY02   DY01   DY00  | 0
+------+------+------+------+------+------+------+------+
|OPCD3  OPCD2  OPCD1  OPCD0  DY11   DY10   DY09   DY08  | 1 OPCD 1111 = ABBREV/
+------+------+------+------+------+------+------+------+
|DX07   DX06   DX05   DX04   DX03   DX02   DX01   DX00  | 2
+------+------+------+------+------+------+------+------+
|INTEN3 INTEN2 INTEN1 INTEN0 DX11   DX10   DX09   DX08  | 3
+------+------+------+------+------+------+------+------+

	Draw relative vector       0x80      1000YYYY YYYYYYYY IIIIXXXX XXXXXXXX
	Draw relative vector
	and load scale             0x90      1001YYYY YYYYYYYY SSSSXXXX XXXXXXXX
	Beam to absolute
	screen position            0xA0      1010YYYY YYYYYYYY ----XXXX XXXXXXXX
	Halt                       0xB0      1011---- --------
	Jump to subroutine         0xC0      1100AAAA AAAAAAAA
	Return from subroutine     0xD0      1101---- --------
	Jump to new address        0xE0      1110AAAA AAAAAAAA
	Short vector draw          0xF0      1111YYYY IIIIXXXX

Sound Z80 Memory Map

0000 ROM
1000 RAM

15 14 13 12 11 10
			0           2k prom (K5)
			1           2k prom (J5)
		 1              1k RAM  (K4,J4)

I/O (write-only)

0,1 			8912 (K3)
2,3			8912 (J3)

I/O (read-only)
0                       input port from main CPU.
						main CPU writing port generated INT
Sound Commands:
0 - reset sound CPU
***************************************************************************/

#include "omegrace.h"
#include "aae_mame_driver.h"
#include "driver_registry.h"
#include "ay8910.h"
#include "sound_latch.h"
#include "mame_late_avgdvg.h"
#include "old_mame_vecsim_dvg.h"
#include "config.h"
#include "timer.h"      // timer_set / TIME_IN_HZ: the 244.140625 Hz INT/NMI timers

// DVG engine selection ([main] dvg_engine=mame|vecsim, per-game overridable),
// same scheme as drivers/asteroid.cpp. Latched from config.dvg_engine in
// init_omega; honored by omegrace and the deltrace bootleg.
static int use_vecsim = 0;

// ---- omegrace60: frame-locked 60 fps entry ---------------------------------
// The ROM has no frame limiter: every main-loop pass polls the VG busy bit
// (port 0x0B) and restarts the VG the moment it reads done. The 60 fps entry
// therefore reports "done" exactly once per presented frame (set in
// run_omega60, cleared on VG go), so the ROM kicks once per 60 Hz frame.
// Design: docs/superpowers/specs/2026-09-14-omegrace60-driver-design.md
static int frame_lock = 0;      // 1 while the omegrace60 entry is running
static int vg_frame_done = 1;   // frame-locked "VG done" flag
static int lock_kicks = 0;      // debug: VG kicks since the last 60-frame log
static int lock_frames = 0;     // debug: frames since the last 60-frame log


ART_START(omegarace_art)
ART_LOAD("omegrace_aae.zip", "omegbkdp3.png", ART_TEX, 0)
ART_LOAD("omegrace_aae.zip", "omegrace_overlay.png", ART_TEX, 1)
ART_LOAD("omegrace_aae.zip", "omegbezlcroped.png", ART_TEX, 3)
ART_END

static AY8910Config ay8910_cfg =
{
    2,                                          // num_chips
    1512000,                                    // base_clock Hz
    { 128, 128, 0, 0 },                           // mixing_level
    { nullptr, nullptr, nullptr, nullptr },     // port_a_read
    { nullptr, nullptr, nullptr, nullptr },     // port_b_read
    { nullptr, nullptr, nullptr, nullptr },     // port_a_write
    { nullptr, nullptr, nullptr, nullptr }      // port_b_write
};

static unsigned char orace_nvram[256];
int scrflip = 0;
int angle = 1;
int angle2 = 1;



/*
 * Encoder bit mappings
 * The encoder is a 64 way switch, with the inputs scrambled
 * on the input port (and shifted 2 bits to the left for the
 * 1 player encoder
 *
 * 3 6 5 4 7 2 for encoder 1 (shifted two bits left..)
 *
 *
 * 5 4 3 2 1 0 for encoder 2 (not shifted..)
 */

static unsigned char spinnerTable[64] = {
	0x00, 0x04, 0x14, 0x10, 0x18, 0x1c, 0x5c, 0x58,
	0x50, 0x54, 0x44, 0x40, 0x48, 0x4c, 0x6c, 0x68,
	0x60, 0x64, 0x74, 0x70, 0x78, 0x7c, 0xfc, 0xf8,
	0xf0, 0xf4, 0xe4, 0xe0, 0xe8, 0xec, 0xcc, 0xc8,
	0xc0, 0xc4, 0xd4, 0xd0, 0xd8, 0xdc, 0x9c, 0x98,
	0x90, 0x94, 0x84, 0x80, 0x88, 0x8c, 0xac, 0xa8,
	0xa0, 0xa4, 0xb4, 0xb0, 0xb8, 0xbc, 0x3c, 0x38,
	0x30, 0x34, 0x24, 0x20, 0x28, 0x2c, 0x0c, 0x08 };


// Board timebase: the schematic's INT divider is 12 MHz / 49152 = 244.140625 Hz
// (12288 Z80 cycles at 3 MHz), and the sound board's NMI is the SAME net
// (GBNMI, J4-14), not a separate 250 Hz source. (The old ipf 25 with a
// divide-by-4 was 250 Hz, 2.4% fast, and the divider chain cannot make 250.)
//
// - omegrace / deltrace: exact rate from a periodic timer on each CPU
//   (OMEGA_IRQ_HZ, armed in init_omega); ipf 0, no ipf callback. AAE timers
//   accumulate across frames, so the rate is exact over time.
// - omegrace60: ipf 4 at 60 fps firing these on every pass = 240 Hz at fixed
//   cycle positions. 1.7% slow by design: the frame lock needs exactly four
//   ticks per presented frame.
#define OMEGA_IRQ_HZ 244.140625

void omega_interrupt()
{
	cpu_do_int_imm(CPU0, INT_TYPE_INT);
}

void omega_nmi_interrupt()
{
	cpu_do_int_imm(CPU1, INT_TYPE_NMI);
}


// Generic soundlatch 0 behind the board's z80 I/O ports; the IRQ kick on
// write is the board wiring and stays here.
PORT_WRITE_HANDLER(omegrace_soundlatch_w)
{
	soundlatch_set(0, data);
	cpu_do_int_imm(CPU1, INT_TYPE_INT);
}
PORT_READ_HANDLER(omegrace_soundlatch_r)
{
	return soundlatch_get(0);
}


WRITE_HANDLER(nvram_w)
{
	orace_nvram[address] = data;
}

READ_HANDLER(nvram_r)
{
	return orace_nvram[address];
}

PORT_WRITE_HANDLER(omega_reset)
{
	/* VGRST: the real DVG state machine must be reset to a known state
	 * before the next VGGO (MAME 0.111 maps port 0x0a to avgdvg_reset_w). */
	if (use_vecsim)
		vecsim_dvg_reset();
	else
		avgdvg_reset(0, 0);
}

PORT_READ_HANDLER(omegrace_watchdog_r)
{
	watchdog_reset_w(0, 0, 0);
	return 0;
}


PORT_READ_HANDLER(omegrace_vg_go)
{
	if (frame_lock)
	{
		vg_frame_done = 0;
		lock_kicks++;
		if (use_vecsim)
		{
			/* the legacy engine draws the whole list inside its go but refuses
			 * a go while its early-clearing busy timer is armed */
			vecsim_test_clear_busy();
			vecsim_dvg_go();
		}
		else
		{
			avgdvg_go(0, 0);
			avgdvg_run_to_halt();   /* draw the whole list now, not over the frame */
		}
		return 0;
	}
	if (use_vecsim)
		vecsim_dvg_go();
	else
		avgdvg_go(0, 0);
	return 0;
}


PORT_READ_HANDLER(omegrace_vg_status_r)
{
	if (frame_lock)
		return vg_frame_done ? 0 : 0x80;
	if (use_vecsim ? vecsim_dvg_done() : avgdvg_done())
		return 0;
	else
		return 0x80;
}

int oomegrace_spinner1_r(int offset)
{
	return (spinnerTable[readinputportbytag("IN4") & 0x3f]);
}

PORT_READ_HANDLER(o_input_port_0_r)
{
	return readinputportbytag("SW0");
}
PORT_READ_HANDLER(o_input_port_1_r)
{
	return readinputportbytag("SW1");
}
PORT_READ_HANDLER(o_input_port_2_r)
{
	return readinputportbytag("IN2");
}
PORT_READ_HANDLER(o_input_port_3_r)
{
	return readinputportbytag("IN3");
}
PORT_READ_HANDLER(omegrace_spinner1_r)
{
	return oomegrace_spinner1_r(0);
}
PORT_READ_HANDLER(o_input_port_5_r)
{
	return readinputportbytag("IN5");
}

PORT_WRITE_HANDLER(omegrace_leds_w)
{
	/* bits 0 and 1 are coin counters */
	//coin_counter_w(0,data & 1);
	//coin_counter_w(1,data & 2);
	
	//bits 2 to 5 are the start leds (4 and 5 cocktail only)
	set_led_status(0, ~data & 0x04);
	set_led_status(1, ~data & 0x08);
	set_led_status(2, ~data & 0x10);
	set_led_status(3, ~data & 0x20);
	
	// Flip Screen in cocktail mode
	if (!(data & 0x40)) scrflip = 1; else scrflip = 0;
}

/////////////////////////////VECTOR GENERATOR//////////////////////////////////

///////////////////////  MAIN LOOP /////////////////////////////////////
void run_omega()
{
	ay8910_sh_update();
	/* debug: the ROM's IRQ handler increments 0x4017 once per INT, so ticks
	 * per fps frames is the delivered INT rate (expect 244 at 40 fps). */
	if (config.debug_profile_code)
	{
		static int frames = 0, ticks = 0;
		static UINT8 prev = 0;
		UINT8 now = Machine->memory_region[CPU0][0x4017];
		ticks += (UINT8)(now - prev);
		prev = now;
		if (++frames >= Machine->gamedrv->fps)
		{
			LOG_INFO("omegrace: INT ticks in the last %d frames = %d (expect %.1f)",
				frames, ticks, OMEGA_IRQ_HZ);
			frames = 0; ticks = 0;
		}
	}
}

// omegrace60 per-frame hook: AAE calls this after cpu_run and before render.
void run_omega60()
{
	ay8910_sh_update();
	vg_frame_done = 1;
	if (config.debug_profile_code && ++lock_frames >= 60)
	{
		LOG_INFO("omegrace60: VG kicks in the last 60 frames = %d (expect 60)", lock_kicks);
		lock_kicks = 0;
		lock_frames = 0;
	}
}

MEM_READ(OmegaRead)
//MEM_ADDR(0x0000, 0x3fff, MRA_ROM)
//MEM_ADDR(0x4000, 0x4bff, MRA_RAM)
MEM_ADDR(0x5c00, 0x5cff, nvram_r) /* NVRAM */
//MEM_ADDR(0x8000, 0x8fff, MRA_RAM)	/* MAME: &vectorram, &vectorram_size */
//MEM_ADDR(0x9000, 0x9fff, MRA_ROM)	/* vector rom */
/* 9000-9fff is ROM, hopefully there are no writes to it */
MEM_END

MEM_WRITE(OmegaWrite)
MEM_ADDR(0x0000, 0x3fff, MWA_ROM)
MEM_ADDR(0x5c00, 0x5cff, nvram_w)  /* NVRAM */
//MEM_ADDR(0x8000, 0x8fff, VectorRam)
MEM_ADDR(0x9000, 0x9fff, MWA_ROM)
MEM_END

MEM_READ(SoundMemRead)
MEM_END

MEM_WRITE(SoundMemWrite)
MEM_ADDR(0x0000, 0x07ff, MWA_ROM)
MEM_END

PORT_READ(OmegaPortRead)
PORT_ADDR(0x08, 0x08, omegrace_vg_go)
PORT_ADDR(0x09, 0x09, omegrace_watchdog_r)
PORT_ADDR(0x0b, 0x0b, omegrace_vg_status_r) /* vg_halt */
PORT_ADDR(0x10, 0x10, o_input_port_0_r) /* DIP SW C4 */
PORT_ADDR(0x17, 0x17, o_input_port_1_r) /* DIP SW C6 */
PORT_ADDR(0x11, 0x11, o_input_port_2_r) /* Player 1 input */
PORT_ADDR(0x12, 0x12, o_input_port_3_r) /* Player 2 input */
PORT_ADDR(0x15, 0x15, omegrace_spinner1_r) /* 1st controller */
PORT_ADDR(0x16, 0x16, o_input_port_5_r) /* 2nd controller (cocktail) */
PORT_END

PORT_WRITE(OmegaPortWrite)
PORT_ADDR(0x0A, 0x0A, omega_reset)
PORT_ADDR(0x13, 0x13, omegrace_leds_w) // coin counters, leds, flip screen
PORT_ADDR(0x14, 0x14, omegrace_soundlatch_w) //Sound command
PORT_END

PORT_READ(SoundPortRead)
PORT_ADDR(0x00, 0x00, omegrace_soundlatch_r)
PORT_END

PORT_WRITE(SoundPortWrite)
PORT_ADDR(0x00, 0x00, ay8910_0_control_port_w)
PORT_ADDR(0x01, 0x01, ay8910_0_data_port_w)
PORT_ADDR(0x02, 0x02, ay8910_1_control_port_w)
PORT_ADDR(0x03, 0x03, ay8910_1_data_port_w)
PORT_END

/////////////////// MAIN() for program ///////////////////////////////////////////////////
#define BITSWAP8(val,B7,B6,B5,B4,B3,B2,B1,B0) \
		(((((val) >> (B7)) & 1) << 7) | \
		 ((((val) >> (B6)) & 1) << 6) | \
		 ((((val) >> (B5)) & 1) << 5) | \
		 ((((val) >> (B4)) & 1) << 4) | \
		 ((((val) >> (B3)) & 1) << 3) | \
		 ((((val) >> (B2)) & 1) << 2) | \
		 ((((val) >> (B1)) & 1) << 1) | \
		 ((((val) >> (B0)) & 1) << 0))

// Shared by init_omega (stock 40 fps entries) and init_omega60.
static int init_omega_common()
{
	//init_z80(OmegaRead, OmegaWrite, OmegaPortRead, OmegaPortWrite, 0);
	////init_z80((SoundMemRead, SoundMemWrite, SoundPortRead, SoundPortWrite, 1);
	nvram_set_region(orace_nvram, 0xff, 0x00);

	/* Omega Race has two pairs of the state PROM output lines swapped
	 * before going into the decoder. Since all other avg/dvg games
	 * connect the PROM in a consistent way to the decoder, swap the
	 * bits here so dvgprom.bin becomes a standard DVG state PROM.
	 * Must run BEFORE dvg_start() latches the PROM pointer. (The vecsim
	 * engine is behavioral and never reads the PROM - the swap is
	 * harmless there, so it runs unconditionally.) */
	{
		UINT8 *prom = Machine->memory_region[REGION_PROMS];
		int i;
		for (i = 0; i < 0x100; i++)
			prom[i] = BITSWAP8(prom[i], 7, 6, 5, 4, 1, 0, 3, 2);
	}

	use_vecsim = (config.dvg_engine == DVG_ENGINE_VECSIM);
	LOG_INFO("Omega Race DVG engine: %s", use_vecsim ? "vecsim (legacy)" : "mame");
	if (use_vecsim)
		vecsim_dvg_start();
	else
		dvg_start();
	ay8910_sh_start(&ay8910_cfg);

	LOG_INFO("End of Omega Race Driver Init");
	return 0;
}

// Stock entries: exact 244.140625 Hz INT (CPU0) and sound NMI (CPU1) from
// periodic timers, the same one net on the board. The CPU entries carry ipf 0.
int init_omega()
{
	int r = init_omega_common();
	timer_set(TIME_IN_HZ(OMEGA_IRQ_HZ), CPU0, [](int) { omega_interrupt(); });
	timer_set(TIME_IN_HZ(OMEGA_IRQ_HZ), CPU1, [](int) { omega_nmi_interrupt(); });
	LOG_INFO("Omega Race: INT/NMI timers at %.6f Hz", OMEGA_IRQ_HZ);
	return r;
}

int init_omega60()
{
	int r = init_omega_common();
	frame_lock = 1;
	vg_frame_done = 1;      /* the first kick must go through */
	lock_kicks = 0;
	lock_frames = 0;
	LOG_INFO("Omega Race 60 fps entry: frame-locked VG done, 240 Hz tick");
	return r;
}

void end_omega()
{
	frame_lock = 0;
	LOG_INFO("OMEGA RACE END CALLED");
	if (use_vecsim)
		vecsim_dvg_end();
	ay8910_sh_stop();
}


INPUT_PORTS_START(omegrace)
PORT_START("SW0") /* SW0 */
PORT_DIPNAME(0x03, 0x03, "1st Bonus Life")
PORT_DIPSETTING(0x00, "40k")
PORT_DIPSETTING(0x01, "50k")
PORT_DIPSETTING(0x02, "70k")
PORT_DIPSETTING(0x03, "100k")
PORT_DIPNAME(0x0c, 0x0c, "2nd & 3rd Bonus Life")
PORT_DIPSETTING(0x00, "150k 250k")
PORT_DIPSETTING(0x04, "250k 500k")
PORT_DIPSETTING(0x08, "500k 750k")
PORT_DIPSETTING(0x0c, "750k 1500k")
PORT_DIPNAME(0x30, 0x30, "Credit(s)/Ships")
PORT_DIPSETTING(0x00, "1C/2S 2C/4S")
PORT_DIPSETTING(0x10, "1C/2S 2C/5S")
PORT_DIPSETTING(0x20, "1C/3S 2C/6S")
PORT_DIPSETTING(0x30, "1C/3S 2C/7S")
PORT_DIPNAME(0x40, 0x40, DEF_STR(Unused))
PORT_DIPSETTING(0x00, DEF_STR(Off))
PORT_DIPSETTING(0x40, DEF_STR(On))
PORT_DIPNAME(0x80, 0x40, DEF_STR(Unused))
PORT_DIPSETTING(0x00, DEF_STR(Off))
PORT_DIPSETTING(0x80, DEF_STR(On))

PORT_START("SW1") /* SW1 */
PORT_DIPNAME(0x07, 0x07, DEF_STR(Coin_A))
PORT_DIPSETTING(0x06, DEF_STR(2C_1C))
PORT_DIPSETTING(0x07, DEF_STR(1C_1C))
PORT_DIPSETTING(0x03, "4 Coins/5 Credits")
PORT_DIPSETTING(0x04, DEF_STR(3C_4C))
PORT_DIPSETTING(0x05, DEF_STR(2C_3C))
PORT_DIPSETTING(0x00, DEF_STR(1C_2C))
PORT_DIPSETTING(0x01, DEF_STR(1C_3C))
PORT_DIPSETTING(0x02, DEF_STR(1C_5C))
PORT_DIPNAME(0x38, 0x38, DEF_STR(Coin_B))
PORT_DIPSETTING(0x30, DEF_STR(2C_1C))
PORT_DIPSETTING(0x38, DEF_STR(1C_1C))
PORT_DIPSETTING(0x18, "4 Coins/5 Credits")
PORT_DIPSETTING(0x20, DEF_STR(3C_4C))
PORT_DIPSETTING(0x28, DEF_STR(2C_3C))
PORT_DIPSETTING(0x00, DEF_STR(1C_2C))
PORT_DIPSETTING(0x08, DEF_STR(1C_3C))
PORT_DIPSETTING(0x10, DEF_STR(1C_5C))
PORT_DIPNAME(0x40, 0x00, DEF_STR(Free_Play))
PORT_DIPSETTING(0x00, DEF_STR(Off))
PORT_DIPSETTING(0x40, DEF_STR(On))
PORT_DIPNAME(0x80, 0x00, DEF_STR(Cabinet))
PORT_DIPSETTING(0x00, DEF_STR(Upright))
PORT_DIPSETTING(0x80, DEF_STR(Cocktail))

PORT_START("IN2") /* IN2 -port 0x11 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_COIN2)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_TILT)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_BUTTON2)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_BUTTON1)
PORT_BITX(0x80, 0x80, IPT_DIPSWITCH_NAME | IPF_TOGGLE, DEF_STR(Service_Mode), OSD_KEY_F2, IP_JOY_NONE)
PORT_DIPSETTING(0x80, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))

PORT_START("IN3") /* IN3 - port 0x12 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_BUTTON2 | IPF_COCKTAIL)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_COCKTAIL)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_START3 | IPF_COCKTAIL)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_START4 | IPF_COCKTAIL)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_START1)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_START2)

PORT_START("IN4") /* IN4 - port 0x15 - spinner */
PORT_ANALOG(0x3f, 0x00, IPT_DIAL, 12, 10, 0, 0)

PORT_START("IN5") /* IN5 - port 0x16 - second spinner */
PORT_ANALOG(0x3f, 0x00, IPT_DIAL | IPF_COCKTAIL, 12, 10, 0, 0)
INPUT_PORTS_END

// omegrace60 port list: identical to omegrace except the two dials' keyboard/
// joystick-button delta. That delta is added once per FRAME, so at 60 fps the
// stock 10 spins the ship 1.5x faster than at 40 fps; 7 (= 10 * 40/60, rounded)
// restores the stock units-per-second. Sensitivity stays 12 so mouse / real
// spinner input (per real time, not per frame) is unchanged.
INPUT_PORTS_START(omegrace60)
PORT_START("SW0") /* SW0 */
PORT_DIPNAME(0x03, 0x03, "1st Bonus Life")
PORT_DIPSETTING(0x00, "40k")
PORT_DIPSETTING(0x01, "50k")
PORT_DIPSETTING(0x02, "70k")
PORT_DIPSETTING(0x03, "100k")
PORT_DIPNAME(0x0c, 0x0c, "2nd & 3rd Bonus Life")
PORT_DIPSETTING(0x00, "150k 250k")
PORT_DIPSETTING(0x04, "250k 500k")
PORT_DIPSETTING(0x08, "500k 750k")
PORT_DIPSETTING(0x0c, "750k 1500k")
PORT_DIPNAME(0x30, 0x30, "Credit(s)/Ships")
PORT_DIPSETTING(0x00, "1C/2S 2C/4S")
PORT_DIPSETTING(0x10, "1C/2S 2C/5S")
PORT_DIPSETTING(0x20, "1C/3S 2C/6S")
PORT_DIPSETTING(0x30, "1C/3S 2C/7S")
PORT_DIPNAME(0x40, 0x40, DEF_STR(Unused))
PORT_DIPSETTING(0x00, DEF_STR(Off))
PORT_DIPSETTING(0x40, DEF_STR(On))
PORT_DIPNAME(0x80, 0x40, DEF_STR(Unused))
PORT_DIPSETTING(0x00, DEF_STR(Off))
PORT_DIPSETTING(0x80, DEF_STR(On))

PORT_START("SW1") /* SW1 */
PORT_DIPNAME(0x07, 0x07, DEF_STR(Coin_A))
PORT_DIPSETTING(0x06, DEF_STR(2C_1C))
PORT_DIPSETTING(0x07, DEF_STR(1C_1C))
PORT_DIPSETTING(0x03, "4 Coins/5 Credits")
PORT_DIPSETTING(0x04, DEF_STR(3C_4C))
PORT_DIPSETTING(0x05, DEF_STR(2C_3C))
PORT_DIPSETTING(0x00, DEF_STR(1C_2C))
PORT_DIPSETTING(0x01, DEF_STR(1C_3C))
PORT_DIPSETTING(0x02, DEF_STR(1C_5C))
PORT_DIPNAME(0x38, 0x38, DEF_STR(Coin_B))
PORT_DIPSETTING(0x30, DEF_STR(2C_1C))
PORT_DIPSETTING(0x38, DEF_STR(1C_1C))
PORT_DIPSETTING(0x18, "4 Coins/5 Credits")
PORT_DIPSETTING(0x20, DEF_STR(3C_4C))
PORT_DIPSETTING(0x28, DEF_STR(2C_3C))
PORT_DIPSETTING(0x00, DEF_STR(1C_2C))
PORT_DIPSETTING(0x08, DEF_STR(1C_3C))
PORT_DIPSETTING(0x10, DEF_STR(1C_5C))
PORT_DIPNAME(0x40, 0x00, DEF_STR(Free_Play))
PORT_DIPSETTING(0x00, DEF_STR(Off))
PORT_DIPSETTING(0x40, DEF_STR(On))
PORT_DIPNAME(0x80, 0x00, DEF_STR(Cabinet))
PORT_DIPSETTING(0x00, DEF_STR(Upright))
PORT_DIPSETTING(0x80, DEF_STR(Cocktail))

PORT_START("IN2") /* IN2 -port 0x11 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_COIN2)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_TILT)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_BUTTON2)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_BUTTON1)
PORT_BITX(0x80, 0x80, IPT_DIPSWITCH_NAME | IPF_TOGGLE, DEF_STR(Service_Mode), OSD_KEY_F2, IP_JOY_NONE)
PORT_DIPSETTING(0x80, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))

PORT_START("IN3") /* IN3 - port 0x12 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_BUTTON2 | IPF_COCKTAIL)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_COCKTAIL)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_START3 | IPF_COCKTAIL)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_START4 | IPF_COCKTAIL)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_START1)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_START2)

PORT_START("IN4") /* IN4 - port 0x15 - spinner; key delta 7 for 60 fps */
PORT_ANALOG(0x3f, 0x00, IPT_DIAL, 12, 7, 0, 0)

PORT_START("IN5") /* IN5 - port 0x16 - second spinner; key delta 7 for 60 fps */
PORT_ANALOG(0x3f, 0x00, IPT_DIAL | IPF_COCKTAIL, 12, 7, 0, 0)
INPUT_PORTS_END


ROM_START(omegrace)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("omega.m7", 0x0000, 0x1000, CRC(0424d46e) SHA1(cc1ac6c06ba6f6e8466fa08286a0c70b5335af33))
ROM_LOAD("omega.l7", 0x1000, 0x1000, CRC(edcd7a7d) SHA1(5d142de2f48b01d563578a54fd5540e5d0ac8f4c))
ROM_LOAD("omega.k7", 0x2000, 0x1000, CRC(6d10f197) SHA1(9609a0cbeeef2efa10d49cde9f0afdca96e9c2f8))
ROM_LOAD("omega.j7", 0x3000, 0x1000, CRC(8e8d4b54) SHA1(944192c0f6f0cdb25d492ee9f33959d38a1062f2))
// Vector Roms
ROM_LOAD("omega.e1", 0x9000, 0x0800, CRC(1d0fdf3a) SHA1(3333397a9745874cea1dd6a1bda783cc59393b55))
ROM_LOAD("omega.f1", 0x9800, 0x0800, CRC(d44c0814) SHA1(2f216ee6de88bbe09775619003aee2d5aa8c554d))
ROM_REGION(0x10000, REGION_CPU2, 0)
ROM_LOAD("sound.k5", 0x0000, 0x0800, CRC(7d426017) SHA1(370f0fb5608819de873c845f6010cbde75a9818e))
// DVG Prom
ROM_REGION(0x100, REGION_PROMS, 0)
ROM_LOAD("dvgprom.bin", 0x0000, 0x0100, CRC(d481e958) SHA1(d8790547dc539e25984807573097b61ec3ffe614))
ROM_END

ROM_START(deltrace)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("omega.m7", 0x0000, 0x1000, CRC(0424d46e) SHA1(cc1ac6c06ba6f6e8466fa08286a0c70b5335af33))
ROM_LOAD("omega.l7", 0x1000, 0x1000, CRC(edcd7a7d) SHA1(5d142de2f48b01d563578a54fd5540e5d0ac8f4c))
ROM_LOAD("omega.k7", 0x2000, 0x1000, CRC(6d10f197) SHA1(9609a0cbeeef2efa10d49cde9f0afdca96e9c2f8))
ROM_LOAD("delta.j7", 0x3000, 0x1000, CRC(8ef9541e) SHA1(89e34f50a958ac60c5f223bcb6c1c14796b903c7))
// Vector Roms
ROM_LOAD("omega.e1", 0x9000, 0x0800, CRC(1d0fdf3a) SHA1(3333397a9745874cea1dd6a1bda783cc59393b55))
ROM_LOAD("omega.f1", 0x9800, 0x0800, CRC(d44c0814) SHA1(2f216ee6de88bbe09775619003aee2d5aa8c554d))
ROM_REGION(0x10000, REGION_CPU2, 0)
ROM_LOAD("sound.k5", 0x0000, 0x0800, CRC(7d426017) SHA1(370f0fb5608819de873c845f6010cbde75a9818e))
ROM_REGION(0x100, REGION_PROMS, 0)
ROM_LOAD("dvgprom.bin", 0x0000, 0x0100, CRC(d481e958) SHA1(d8790547dc539e25984807573097b61ec3ffe614))
ROM_END

// Omega Race
AAE_DRIVER_BEGIN(drv_omegrace, "omegrace", "Omega Race")
AAE_DRIVER_ROM(rom_omegrace)
AAE_DRIVER_FUNCS(&init_omega, &run_omega, &end_omega)
AAE_DRIVER_INPUT(input_ports_omegrace)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART(omegarace_art)

AAE_DRIVER_CPUS(
	// CPU0: Main Z80 @ 3.020 MHz, 100 divs; INT from the 244.140625 Hz timer (init_omega)
	AAE_CPU_ENTRY(
		/*type*/     CPU_MZ80,
		/*freq*/     3020000,
		/*div*/      100,
		/*ipf*/      0,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   nullptr,           // timer drives omega_interrupt
		/*r8*/       OmegaRead,
		/*w8*/       OmegaWrite,
		/*pr*/       OmegaPortRead,
		/*pw*/       OmegaPortWrite,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	// CPU1: Sound Z80 @ 1.512 MHz, 100 divs; NMI from the 244.140625 Hz timer (init_omega)
	AAE_CPU_ENTRY(
		/*type*/     CPU_MZ80,
		/*freq*/     1512000,
		/*div*/      100,
		/*ipf*/      0,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   nullptr,           // timer drives omega_nmi_interrupt
		/*r8*/       SoundMemRead,
		/*w8*/       SoundMemWrite,
		/*pr*/       SoundPortRead,
		/*pw*/       SoundPortWrite,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(40,0, VIDEO_TYPE_VECTOR | VECTOR_USES_BW | VECTOR_USES_OVERLAY1, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 1044, 0, 1024)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0x8000, 0x1000)
AAE_DRIVER_NVRAM(generic_nvram_handler)
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()

// Delta Race (Omega Race Bootleg)
AAE_DRIVER_BEGIN(drv_deltrace, "deltrace", "Delta Race (Omega Race Bootleg)")
AAE_DRIVER_ROM(rom_deltrace)
AAE_DRIVER_FUNCS(&init_omega, &run_omega, &end_omega)
AAE_DRIVER_INPUT(input_ports_omegrace)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART(omegarace_art)

AAE_DRIVER_CPUS(
	// CPU0: Main Z80 (same as Omega Race: INT from the 244.140625 Hz timer)
	AAE_CPU_ENTRY(
		/*type*/     CPU_MZ80,
		/*freq*/     3020000,
		/*div*/      100,
		/*ipf*/      0,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   nullptr,           // timer drives omega_interrupt
		/*r8*/       OmegaRead,
		/*w8*/       OmegaWrite,
		/*pr*/       OmegaPortRead,
		/*pw*/       OmegaPortWrite,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	// CPU1: Sound Z80 @ 1.512 MHz -- identical to Omega Race. The bootleg copies
	// only the main board; the sound board, sound.k5 and the sound-engine NMI
	// (the board's 244.140625 Hz INT net; MAME models it as 250 Hz) are the same.
	AAE_CPU_ENTRY(
		/*type*/     CPU_MZ80,
		/*freq*/     1512000,
		/*div*/      100,
		/*ipf*/      0,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   nullptr,           // timer drives omega_nmi_interrupt
		/*r8*/       SoundMemRead,
		/*w8*/       SoundMemWrite,
		/*pr*/       SoundPortRead,
		/*pw*/       SoundPortWrite,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(40,0, VIDEO_TYPE_VECTOR | VECTOR_USES_BW | VECTOR_USES_OVERLAY1, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 1044, 0, 1024)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0x8000, 0x1000)
AAE_DRIVER_NVRAM(generic_nvram_handler)
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_CLONE_OF("omegrace")
AAE_DRIVER_END()

// Omega Race (60 fps): same ROM set (rom_omegrace -> omegrace.zip), ports and
// art as omegrace; 60 fps video core, ipf 4 with every-pass callbacks (240 Hz
// tick), frame-locked VG done via init_omega60/run_omega60.
AAE_DRIVER_BEGIN(drv_omegrace60, "omegrace60", "Omega Race (60 fps)")
AAE_DRIVER_ROM(rom_omegrace)
AAE_DRIVER_FUNCS(&init_omega60, &run_omega60, &end_omega)
AAE_DRIVER_INPUT(input_ports_omegrace60)   /* dial key delta scaled for 60 fps */
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART(omegarace_art)

AAE_DRIVER_CPUS(
	// CPU0: Main Z80 @ 3.020 MHz, 100 divs, 4 int passes = 4 INTs per 60 Hz frame
	AAE_CPU_ENTRY(
		/*type*/     CPU_MZ80,
		/*freq*/     3020000,
		/*div*/      100,
		/*ipf*/      4,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   &omega_interrupt,
		/*r8*/       OmegaRead,
		/*w8*/       OmegaWrite,
		/*pr*/       OmegaPortRead,
		/*pw*/       OmegaPortWrite,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	// CPU1: Sound Z80 @ 1.512 MHz, 4 int passes = 4 NMIs per 60 Hz frame
	AAE_CPU_ENTRY(
		/*type*/     CPU_MZ80,
		/*freq*/     1512000,
		/*div*/      100,
		/*ipf*/      4,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   &omega_nmi_interrupt,
		/*r8*/       SoundMemRead,
		/*w8*/       SoundMemWrite,
		/*pr*/       SoundPortRead,
		/*pw*/       SoundPortWrite,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(60, 0, VIDEO_TYPE_VECTOR | VECTOR_USES_BW | VECTOR_USES_OVERLAY1, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 1044, 0, 1024)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0x8000, 0x1000)
AAE_DRIVER_NVRAM(generic_nvram_handler)
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()

// Registrations
AAE_REGISTER_DRIVER(drv_omegrace)
AAE_REGISTER_DRIVER(drv_deltrace)
AAE_REGISTER_DRIVER(drv_omegrace60)
