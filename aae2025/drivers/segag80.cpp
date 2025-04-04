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

//NOTE: SegaG80 decryption fixed with new changes to the z80 ppc handling.

#include "segag80.h"
#include "SegaG80vid.h"
#include "SegaG80crypt.h"
#include "segag80snd.h"
#include "aae_mame_driver.h"

void sega_interrupt() {
	if (input_port_5_r(0) & 0x01)
		cpu_do_interrupt(INT_TYPE_NMI, CPU0);
	else
		cpu_do_interrupt(INT_TYPE_INT, CPU0);
}

extern void (*sega_decrypt)(int, unsigned int*);

int NUM_SPEECH_SAMPLES;

extern char* gamename[];
extern int gamenum;


unsigned char* sega_vectorram;

//INPUT VARIABLES
unsigned char mult1;
unsigned short result;
unsigned char ioSwitch;



/***************************************************************************

  Stop the video hardware emulation.

***************************************************************************/
/*
static int coin_routine(int bits)
{
   if (coin_in > 0 && coin_in < 4){ bitclr(bits, 0x20);  coin_in++;}

  return bits;
}
*/


READ_HANDLER(SoundRam)
{
	return 0x00;
}

WRITE_HANDLER(sega_mem_w)
{
	int op = 0;
	int page = 0;
	int val = 0;
	int offset = 0;
	unsigned int bad = 0;

	uint8_t *MEM = Machine->memory_region[CPU0];

	val = m_cpu_z80[CPU0]->GetPPC();

	if (val != -1)
	{
		op = MEM[val] & 0xff;
		if (op == 0x32)
		{
			bad = MEM[(val + 1)] & 0xFF;
			page = (MEM[(val + 2)] & 0xFF) << 8;
			(*sega_decrypt)(val, &bad);

			offset = (page & 0xFF00) | (bad & 0x00FF);
			//wrlog("VAL %x, OP %x BAD %x PAGE %x ADDRESS %x OFFSET %x", val, op, bad, page, address, offset);
			address = offset;
		}
	}
	if ((address >= 0x0000) && (address <= 0xbfff)) { ; }
	else if (address >= 0xc800 && address <= 0xefff) { MEM[address] = data; }
}


PORT_READ_HANDLER(sega_sh_r)
{
	if (sample_playing(0))
		return 0x81;
	else
		return 0x80;
}


PORT_READ_HANDLER(sega_mult_r)
{
	int c;

	c = result & 0xff;
	result >>= 8;
	return (c);
}

/***************************************************************************

  The Sega games store the DIP switches in a very mangled format that's
  not directly useable by MAME.  This function mangles the DIP switches
  into a format that can be used.

  Original format:
  Port 0 - 2-4, 2-8, 1-4, 1-8
  Port 1 - 2-3, 2-7, 1-3, 1-7
  Port 2 - 2-2, 2-6, 1-2, 1-6
  Port 3 - 2-1, 2-5, 1-1, 1-5
  MAME format:
  Port 6 - 1-1, 1-2, 1-3, 1-4, 1-5, 1-6, 1-7, 1-8
  Port 7 - 2-1, 2-2, 2-3, 2-4, 2-5, 2-6, 2-7, 2-8
***************************************************************************/

PORT_READ_HANDLER(sega_ports_r)
{
	int dip1, dip2;

	dip1 = input_port_6_r(port);
	dip2 = input_port_7_r(port);

	switch (port)
	{
	case 0xf8:
		return ((input_port_0_r(0) & 0xF0) | ((dip2 & 0x08) >> 3) |
			((dip2 & 0x80) >> 6) | ((dip1 & 0x08) >> 1) | ((dip1 & 0x80) >> 4));
	case 0xf9:
		return ((input_port_1_r(0) & 0xF0) | ((dip2 & 0x04) >> 2) |
			((dip2 & 0x40) >> 5) | ((dip1 & 0x04) >> 0) | ((dip1 & 0x40) >> 3));
	case 0xfa:
		return ((input_port_2_r(0) & 0xF0) | ((dip2 & 0x02) >> 1) |
			((dip2 & 0x20) >> 4) | ((dip1 & 0x02) << 1) | ((dip1 & 0x20) >> 2));
	case 0xfb:
		return ((input_port_3_r(0) & 0xF0) | ((dip2 & 0x01) >> 0) |
			((dip2 & 0x10) >> 3) | ((dip1 & 0x01) << 2) | ((dip1 & 0x10) >> 1));
	}

	return 0;
}

PORT_READ_HANDLER(elim_in2_r)
{
	return input_port_4_r(port);
}

PORT_READ_HANDLER(sega_IN4_r) {
	/*
	 * The values returned are always increasing.  That is, regardless of whether
	 * you turn the spinner left or right, the self-test should always show the
	 * number as increasing. The direction is only reflected in the least
	 * significant bit.
	 */

	int delta;
	static int sign;
	static int spinner;

	if (ioSwitch & 1) /* ioSwitch = 0x01 or 0xff */
		return readinputport(4);

	/* else ioSwitch = 0xfe */

	/* I'm sure this can be further simplified ;-) BW */
	delta = readinputport(8);
	if (delta != 0)
	{
		sign = delta >> 7;
		if (sign)
			delta = 0x80 - delta;
		spinner += delta;
	}
	return (~((spinner << 1) | sign));
}

PORT_READ_HANDLER(elim4_IN4_r)
{
	/* If the ioPort ($f8) is 0x1f, we're reading the 4 coin inputs.    */
	/* If the ioPort ($f8) is 0x1e, we're reading player 3 & 4 controls.*/

	if (ioSwitch == 0x1e)
		return readinputport(4);
	if (ioSwitch == 0x1f)
		return readinputport(8);
	return (0);
}

PORT_WRITE_HANDLER(sega_switch_w)
{
	ioSwitch = data;
}

PORT_WRITE_HANDLER(sega_mult1_w)
{
	mult1 = data;
}

PORT_WRITE_HANDLER(sega_mult2_w)
{
	result = mult1 * data;
}

PORT_WRITE_HANDLER(sega_coin_counter_w)
{
	;
}

///////////////////////  MAIN LOOP /////////////////////////////////////
void run_segag80(void)
{
	watchdog_reset_w(0, 0, 0);
	//Update Video
	sega_vh_update();

	//Update Sound
	if (gamenum == TACSCAN)
	{
		tacscan_sh_update();
	}
	else sega_sh_update();
}

MEM_WRITE(SegaWrite)
MEM_ADDR( 0x0000,  0xc7ff, MWA_ROM )
MEM_ADDR( 0x0000,  0xffff, sega_mem_w )
MEM_END

MEM_READ(SegaRead)
MEM_ADDR( 0xe000, 0xefff, MRA_RAM )
MEM_ADDR( 0xd000, 0xdfff, SoundRam )
MEM_ADDR( 0xf000, 0xffff, SoundRam )
MEM_END

PORT_READ(SpacfuryPortRead)
PORT_ADDR(0x3f, 0x3f, sega_sh_r)
PORT_ADDR(0xbe, 0xbe, sega_mult_r)
PORT_ADDR(0xf8, 0xfc, sega_ports_r)
PORT_END

PORT_READ(G80SpinPortRead)
PORT_ADDR(0x3f, 0x3f, sega_sh_r)
PORT_ADDR(0xbe, 0xbe, sega_mult_r)
PORT_ADDR(0xf8, 0xfb, sega_ports_r)
PORT_ADDR(0xfc, 0xfc, sega_IN4_r)
PORT_END

PORT_READ(Elim2PortRead)
PORT_ADDR(0x3f, 0x3f, sega_sh_r)
PORT_ADDR(0xbe, 0xbe, sega_mult_r)
PORT_ADDR(0xf8, 0xfb, sega_ports_r)
PORT_ADDR(0xfc, 0xfc, elim_in2_r)
PORT_END

PORT_READ(Elim4PortRead)
PORT_ADDR(0x3f, 0x3f, sega_sh_r)
PORT_ADDR(0xbe, 0xbe, sega_mult_r)
PORT_ADDR(0xf8, 0xfb, sega_ports_r)
PORT_ADDR(0xfc, 0xfc, elim4_IN4_r)
PORT_END

PORT_WRITE(SpacfuryPortWrite)
PORT_ADDR(0x38, 0x38, sega_sh_speech_w)
PORT_ADDR(0x3e, 0x3e, spacfury1_sh_w)
PORT_ADDR(0x3f, 0x3f, spacfury2_sh_w)
PORT_ADDR(0xbd, 0xbd, sega_mult1_w)
PORT_ADDR(0xbe, 0xbe, sega_mult2_w)
PORT_ADDR(0xf8, 0xf8, sega_coin_counter_w) /* 0x80 = enable, 0x00 = disable */
PORT_ADDR(0xf9, 0xf9, sega_coin_counter_w) /* 0x80 = enable, 0x00 = disable */
PORT_END

PORT_WRITE(ElimPortWrite)
PORT_ADDR(0x38, 0x38, sega_sh_speech_w)
PORT_ADDR(0x3e, 0x3e, elim1_sh_w)
PORT_ADDR(0x3f, 0x3f, elim2_sh_w)
PORT_ADDR(0xbd, 0xbd, sega_mult1_w)
PORT_ADDR(0xbe, 0xbe, sega_mult2_w)
PORT_ADDR(0xf8, 0xf8, sega_switch_w)
PORT_ADDR(0xf9, 0xf9, sega_coin_counter_w) /* 0x80 = enable, 0x00 = disable */
PORT_END

PORT_WRITE(ZektorPortWrite)
PORT_ADDR(0x38, 0x38, sega_sh_speech_w)
PORT_ADDR(0x3c, 0x3c, Zektor_AY8910_w)
//PORT_ADDR(0x3d, 0x3d, Zektor_AY8910_w, NULL)
PORT_ADDR(0x3e, 0x3e, Zektor1_sh_w)
PORT_ADDR(0x3f, 0x3f, Zektor2_sh_w)
PORT_ADDR(0xbd, 0xbd, sega_mult1_w)
PORT_ADDR(0xbe, 0xbe, sega_mult2_w)
PORT_ADDR(0xf8, 0xf8, sega_switch_w)
PORT_ADDR(0xf9, 0xf9, sega_coin_counter_w) /* 0x80 = enable, 0x00 = disable */
PORT_END

PORT_WRITE(StarTrekPortWrite)
PORT_ADDR(0x38, 0x38, sega_sh_speech_w)
PORT_ADDR(0x3f, 0x3f, StarTrek_sh_w)
PORT_ADDR(0xbd, 0xbd, sega_mult1_w)
PORT_ADDR(0xbe, 0xbe, sega_mult2_w)
PORT_ADDR(0xf8, 0xf8, sega_switch_w)
PORT_ADDR(0xf9, 0xf9, sega_coin_counter_w) /* 0x80 = enable, 0x00 = disable */
PORT_END

PORT_WRITE(TacScanPortWrite)
PORT_ADDR(0x38, 0x38, sega_sh_speech_w)
PORT_ADDR(0x3f, 0x3f, TacScan_sh_w)
PORT_ADDR(0xbd, 0xbd, sega_mult1_w)
PORT_ADDR(0xbe, 0xbe, sega_mult2_w)
PORT_ADDR(0xf8, 0xf8, sega_switch_w)
PORT_ADDR(0xf9, 0xf9, sega_coin_counter_w) /* 0x80 = enable, 0x00 = disable */
PORT_END

//Init Functions for driver

int init_spacfury()
{
	init_z80(SegaRead, SegaWrite, SpacfuryPortRead, SpacfuryPortWrite, 0);
	NUM_SPEECH_SAMPLES = NUM_SPACFURY_SPEECH;
	sega_security(64);
	sega_vh_start(0);
	init_segag80();
	return 0;
}

int init_tacscan()
{
	init_z80(SegaRead, SegaWrite, G80SpinPortRead, TacScanPortWrite, CPU0);
	sega_vh_start(1);
	sega_security(76);
	init_segag80();
	return 0;
}

int init_zektor()
{
	init_z80(SegaRead, SegaWrite, G80SpinPortRead, ZektorPortWrite, CPU0);
	NUM_SPEECH_SAMPLES = NUM_ZEKTOR_SPEECH;
	sega_security(82);
	sega_vh_start(0);
	init_segag80();
	return 0;
}
int init_startrek()
{
	init_z80(SegaRead, SegaWrite, G80SpinPortRead, StarTrekPortWrite, 0);
	NUM_SPEECH_SAMPLES = NUM_STARTREK_SPEECH;
	sega_security(64);
	sega_vh_start(0);
	init_segag80();
	return 0;
}

int init_elim2()
{
	init_z80(SegaRead, SegaWrite, Elim2PortRead, ElimPortWrite, 0);
	sega_security(70);
	sega_vh_start(0);
	init_segag80();
	return 0;
}

int init_elim4()
{
	init_z80(SegaRead, SegaWrite, Elim4PortRead, ElimPortWrite, 0);
	sega_security(76);
	sega_vh_start(0);
	init_segag80();
	return 0;
}

int init_segag80(void)
{
	sega_vectorram = &Machine->memory_region[0][0xe000];
	sega_sh_start();
	return 0;
}

void end_segag80(void)
{
	sega_vh_stop();
	sega_sh_stop();
}