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
#include "samples.h"
#include "AY8910.H"
#include "old_mame_vecsim_dvg.h"

static struct AY8910interface ay8910_interface =
{
	2,
	1500000,
	{ 25, 25 }, //7
	{ 0, 0 }, //2
	{ 0 },
	{ 0 },
	{ 0 },
	{ 0 }
};

static unsigned char orace_nvram[256];
int scrflip = 0;
int angle = 1;
int angle2 = 1;

int soundlatch = 0;


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


static int cpu1_counter = 0;
static int cpu2_counter = 0;

void  omega_interrupt()
{
	cpu1_counter++; if ((cpu1_counter & 4) == 4) {
		cpu_do_int_imm(CPU0, INT_TYPE_INT); cpu1_counter = 0;
		//wrlog("Omega Race Interrupt");
	}
}


void  omega_nmi_interrupt()
{
	cpu2_counter++; if ((cpu2_counter & 4) == 4) {
		cpu_do_int_imm(CPU1, INT_TYPE_NMI); cpu2_counter = 0; 
		//wrlog("Omega Race NMI Interrupt");
	}
}
void nvram_handler(void* file, int read_or_write)
{
	if (read_or_write)
	{
		wrlog("Writing NVRAM File");
		osd_fwrite(file, orace_nvram, 0xff);
	}
	else
	{
		if (file)
		{
			wrlog("Reading NVRAM File");
			osd_fread(file, orace_nvram, 0xff);
		}
		else
		{
			wrlog("Creating NVRAM File");
			memset(orace_nvram, 0, 0xff);
		}
	}
}


PORT_WRITE_HANDLER(omegrace_soundlatch_w)
{
	soundlatch = data;
	cpu_do_int_imm(CPU1, INT_TYPE_INT);
}
PORT_READ_HANDLER(omegrace_soundlatch_r)
{
	return soundlatch;
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
	wrlog("DVG reset called");
}

PORT_READ_HANDLER(omegrace_watchdog_r)
{
	watchdog_reset_w(0, 0, 0);
	return 0;
}


PORT_READ_HANDLER(omegrace_vg_go)
{
	dvg_go_w(0, 0, 0);
	return 0;
}


PORT_READ_HANDLER(omegrace_vg_status_r)
{
	if (dvg_done())
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
	/*
	bits 2 to 5 are the start leds (4 and 5 cocktail only)
	set_led_status(0, ~data & 0x04);
	set_led_status(1, ~data & 0x08);
	set_led_status(2, ~data & 0x10);
	set_led_status(3, ~data & 0x20);
	*/
	// I have only 3 LEDS to work with
	set_aae_leds(~data & 0x04, ~data & 0x08, ~data & 0x10);
	// Flip Screen in cocktail mode
	if (!(data & 0x40)) scrflip = 1; else scrflip = 0;
}

/////////////////////////////VECTOR GENERATOR//////////////////////////////////

///////////////////////  MAIN LOOP /////////////////////////////////////
void run_omega()
{

	AY8910_sh_update();
}

MEM_READ(OmegaRead)
//{ 0x0000, 0x3fff, MRA_ROM },
//{ 0x4000, 0x4bff, MRA_RAM },
MEM_ADDR(0x5c00, 0x5cff, nvram_r) /* NVRAM */
//{ 0x8000, 0x8fff, MRA_RAM, &vectorram, &vectorram_size },
//{ 0x9000, 0x9fff, MRA_ROM }, /* vector rom */
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
PORT_ADDR(0x21, 0x21, omega_reset)
PORT_ADDR(0x13, 0x13, omegrace_leds_w) // coin counters, leds, flip screen
PORT_ADDR(0x14, 0x14, omegrace_soundlatch_w) //Sound command
PORT_END

PORT_READ(SoundPortRead)
PORT_ADDR(0x00, 0x00, omegrace_soundlatch_r)
PORT_END

PORT_WRITE(SoundPortWrite)
PORT_ADDR(0x00, 0x00, AY8910_control_port_0_w)
PORT_ADDR(0x01, 0x01, AY8910_write_port_0_w)
PORT_ADDR(0x02, 0x02, AY8910_control_port_1_w)
PORT_ADDR(0x03, 0x03, AY8910_write_port_1_w)
PORT_END

/////////////////// MAIN() for program ///////////////////////////////////////////////////
int init_omega()
{
	init_z80(OmegaRead, OmegaWrite, OmegaPortRead, OmegaPortWrite, 0);
	init_z80(SoundMemRead, SoundMemWrite, SoundPortRead, SoundPortWrite, 1);
	dvg_start();
	AY8910_sh_start(&ay8910_interface);

	wrlog("End of Omega Race Driver Init");
	return 0;
}

void end_omega()
{
	AY8910clear();
}