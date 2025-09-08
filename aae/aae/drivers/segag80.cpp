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
#include "driver_registry.h"


ART_START(spacfuryart)
ART_LOAD("spacfury.zip", "space_fury_bezel.png", ART_TEX, 3)
ART_END


ART_START(startrekart)
ART_LOAD("startrek.zip", "startrek.png", ART_TEX, 3)
ART_END

ART_START(tacscanart)
ART_LOAD("custom.zip", "vert_mask.png", ART_TEX, 2)
ART_END

void sega_interrupt() {
	if (input_port_5_r(0) & 0x01)
		cpu_do_int_imm(CPU0, INT_TYPE_NMI);
	else
		cpu_do_int_imm(CPU0, INT_TYPE_INT);
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

	val = cpu_getppc();

	if (val != -1)
	{
		op = MEM[val] & 0xff;
		if (op == 0x32)
		{
			bad = MEM[(val + 1)] & 0xFF;
			page = (MEM[(val + 2)] & 0xFF) << 8;
			(*sega_decrypt)(val, &bad);

			offset = (page & 0xFF00) | (bad & 0x00FF);
			//LOG_INFO("VAL %x, OP %x BAD %x PAGE %x ADDRESS %x OFFSET %x", val, op, bad, page, address, offset);
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
	sega_sh_update();
}

void run_tacscan(void)
{
	watchdog_reset_w(0, 0, 0);
	//Update Video
	sega_vh_update();
	tacscan_sh_update();
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
	//init_z80((SegaRead, SegaWrite, SpacfuryPortRead, SpacfuryPortWrite, 0);
	NUM_SPEECH_SAMPLES = NUM_SPACFURY_SPEECH;
	sega_security(64);
	sega_vh_start(0);
	init_segag80();
	return 0;
}

int init_tacscan()
{
	//init_z80((SegaRead, SegaWrite, G80SpinPortRead, TacScanPortWrite, CPU0);
	sega_vh_start(1);
	sega_security(76);
	init_segag80();
	return 0;
}

int init_zektor()
{
	//init_z80((SegaRead, SegaWrite, G80SpinPortRead, ZektorPortWrite, CPU0);
	NUM_SPEECH_SAMPLES = NUM_ZEKTOR_SPEECH;
	sega_security(82);
	sega_vh_start(0);
	init_segag80();
	return 0;
}
int init_startrek()
{
	//init_z80((SegaRead, SegaWrite, G80SpinPortRead, StarTrekPortWrite, 0);
	NUM_SPEECH_SAMPLES = NUM_STARTREK_SPEECH;
	sega_security(64);
	sega_vh_start(0);
	init_segag80();
	return 0;
}

int init_elim2()
{
	//init_z80((SegaRead, SegaWrite, Elim2PortRead, ElimPortWrite, 0);
	sega_security(70);
	sega_vh_start(0);
	init_segag80();
	return 0;
}

int init_elim4()
{
	//init_z80((SegaRead, SegaWrite, Elim4PortRead, ElimPortWrite, 0);
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


static const char* zektor_samples[] = {
	"zektor.zip",
	"zk01.wav",  /* 1 */
	"zk02.wav",
	"zk03.wav",
	"zk04.wav",
	"zk05.wav",
	"zk06.wav",
	"zk07.wav",
	"zk08.wav",
	"zk09.wav",
	"zk0a.wav",
	"zk0b.wav",
	"zk0c.wav",
	"zk0d.wav",
	"zk0e.wav",
	"zk0f.wav",
	"zk10.wav",
	"zk11.wav",
	"zk12.wav",
	"zk13.wav",
	"elim1.wav",  /* 19 fireball */
	"elim2.wav",  /* 20 bounce */
	"elim3.wav",  /* 21 Skitter */
	"elim4.wav",  /* 22 Eliminator */
	"elim5.wav",  /* 23 Electron */
	"elim6.wav",  /* 24 fire */
	"elim7.wav",  /* 25 thrust */
	"elim8.wav",  /* 26 Electron */
	"elim9.wav",  /* 27 small explosion */
	"elim10.wav", /* 28 med explosion */
	"elim11.wav", /* 29 big explosion */
	"zizzer.wav", /* Zizzer */
	"ping.wav",   /* PING */
	"cityfly.wav",/* City fly by */
	"rrings.wav", /* Rotation Rings */
	 0
};

static const char* tacscan_samples[] =
{
	"tacscan.zip",
	/* Player ship thrust sounds */
	"01.wav",
	"02.wav",
	"03.wav",
	"plaser.wav",
	"pexpl.wav",
	"pship.wav",
	"tunnelh.wav",
	"sthrust.wav",
	"slaser.wav",
	"sexpl.wav",
	"eshot.wav",
	"eexpl.wav",
	"tunnelw.wav",
	"flight1.wav",
	"flight2.wav",
	"flight3.wav",
	"flight4.wav",
	"flight5.wav",
	"formatn.wav",
	"warp.wav",
	"credit.wav",
	"1up.wav",
	 0
};

static const char* startrek_samples[] =
{ "startrek.zip",
	"st01.wav",
	"st02.wav",
	"st03.wav",
	"st04.wav",
	"st05.wav",
	"st06.wav",
	"st07.wav",
	"st08.wav",
	"st09.wav",
	"st0a.wav",
	"st0b.wav",
	"st0c.wav",
	"st0d.wav",
	"st0e.wav",
	"st0f.wav",
	"st10.wav",
	"st11.wav",
	"st12.wav",
	"st13.wav",
	"st14.wav",
	"st15.wav",
	"st16.wav",
	"st17.wav",
	// Sound samples
	"trek1.wav",
	"trek2.wav",
	"trek3.wav",
	"trek4.wav",
	"trek5.wav",
	"trek6.wav",
	"trek7.wav",
	"trek8.wav",
	"trek9.wav",
	"trek10.wav",
	"trek11.wav",
	"trek12.wav",
	"trek13.wav",
	"trek14.wav",
	"trek15.wav",
	"trek16.wav",
	"trek17.wav",
	"trek18.wav",
	"trek19.wav",
	"trek20.wav",
	"trek21.wav",
	"trek22.wav",
	"trek23.wav",
	"trek24.wav",
	"trek25.wav",
	"trek26.wav",
	"trek27.wav",
	"trek28.wav",
	 0 };

static const char* elim_samples[] = {
	"elim2p.zip",
	"elim1.wav",  // 19 fireball
	"elim2.wav",  // 20 bounce
	"elim3.wav",  // 21 Skitter
	"elim4.wav",  // 22 Eliminator
	"elim5.wav",  // 23 Electron
	"elim6.wav",  // 24 fire
	"elim7.wav",  // 25 thrust
	"elim8.wav",  // 26 Electron
	"elim9.wav",  // 27 small explosion
	"elim10.wav", // 28 med explosion
	"elim11.wav", // 29 big explosion
	"elim12.wav", // 29 big explosion
	 0 };

static const char* spacfury_samples[] = {
	"spacfury.zip",
	"SF01.WAV",
	"SF02.WAV",
	"SF03.WAV",
	"SF04.WAV",
	"SF05.WAV",
	"SF06.WAV",
	"SF07.WAV",
	"SF08.WAV",
	"SF09.WAV",
	"SF0A.WAV",
	"SF0B.WAV",
	"SF0C.WAV",
	"SF0D.WAV",
	"SF0E.WAV",
	"SF0F.WAV",
	"SF10.WAV",
	"SF11.WAV",
	"SF12.WAV",
	"SF13.WAV",
	"SF14.WAV",
	"SF15.WAV",
	"sfury1.wav",
	"sfury2.wav",
	"sfury3.wav",
	"sfury4.wav",
	"sfury5.wav",
	"sfury6.wav",
	"sfury7.wav",
	"sfury8.wav",
	"sfury9.wav",
	"sfury10.wav",
	 0 };

//////////////////////////////////////////////////////////

/*************************************
 *
 *	Port definitions
 *
 *************************************/

 /* This fake input port is used for DIP Switch 2
	for all games except Eliminato 4 players */
#define COINAGE PORT_START("DSW2") \
		PORT_DIPNAME( 0x0f, 0x0c, DEF_STR ( Coin_B ) ) \
		PORT_DIPSETTING(	0x00, DEF_STR ( 4C_1C ) ) \
		PORT_DIPSETTING(	0x08, DEF_STR ( 3C_1C ) ) \
		PORT_DIPSETTING(	0x09, "2 Coins/1 Credit 5/3 6/4" ) \
		PORT_DIPSETTING(	0x05, "2 Coins/1 Credit 4/3" ) \
		PORT_DIPSETTING(	0x04, DEF_STR ( 2C_1C ) ) \
		PORT_DIPSETTING(	0x0c, DEF_STR ( 1C_1C ) ) \
		PORT_DIPSETTING(	0x0d, "1 Coin/1 Credit 5/6" ) \
		PORT_DIPSETTING(	0x03, "1 Coin/1 Credit 4/5" ) \
		PORT_DIPSETTING(	0x0b, "1 Coin/1 Credit 2/3" ) \
		PORT_DIPSETTING(	0x02, DEF_STR ( 1C_2C ) ) \
		PORT_DIPSETTING(	0x0f, "1 Coin/2 Credits 4/9" ) \
		PORT_DIPSETTING(	0x07, "1 Coin/2 Credits 5/11" ) \
		PORT_DIPSETTING(	0x0a, DEF_STR ( 1C_3C ) ) \
		PORT_DIPSETTING(	0x06, DEF_STR ( 1C_4C ) ) \
		PORT_DIPSETTING(	0x0e, DEF_STR ( 1C_5C ) ) \
		PORT_DIPSETTING(	0x01, DEF_STR ( 1C_6C ) ) \
		PORT_DIPNAME( 0xf0, 0xc0, DEF_STR ( Coin_A ) ) \
		PORT_DIPSETTING(	0x00, DEF_STR ( 4C_1C ) ) \
		PORT_DIPSETTING(	0x80, DEF_STR ( 3C_1C ) ) \
		PORT_DIPSETTING(	0x90, "2 Coins/1 Credit 5/3 6/4" ) \
		PORT_DIPSETTING(	0x50, "2 Coins/1 Credit 4/3" ) \
		PORT_DIPSETTING(	0x40, DEF_STR ( 2C_1C ) ) \
		PORT_DIPSETTING(	0xc0, DEF_STR ( 1C_1C ) ) \
		PORT_DIPSETTING(	0xd0, "1 Coin/1 Credit 5/6" ) \
		PORT_DIPSETTING(	0x30, "1 Coin/1 Credit 4/5" ) \
		PORT_DIPSETTING(	0xb0, "1 Coin/1 Credit 2/3" ) \
		PORT_DIPSETTING(	0x20, DEF_STR ( 1C_2C ) ) \
		PORT_DIPSETTING(	0xf0, "1 Coin/2 Credits 4/9" ) \
		PORT_DIPSETTING(	0x70, "1 Coin/2 Credits 5/11" ) \
		PORT_DIPSETTING(	0xa0, DEF_STR ( 1C_3C ) ) \
		PORT_DIPSETTING(	0x60, DEF_STR ( 1C_4C ) ) \
		PORT_DIPSETTING(	0xe0, DEF_STR ( 1C_5C ) ) \
		PORT_DIPSETTING(	0x10, DEF_STR ( 1C_6C ) )

INPUT_PORTS_START(spacfury)
PORT_START("IN0")	/* IN0 - port 0xf8 */
/* The next bit is referred to as the Service switch in the self test - it just adds a credit */
PORT_BIT_IMPULSE(0x20, IP_ACTIVE_LOW, IPT_COIN3, 3)
PORT_BIT_IMPULSE(0x40, IP_ACTIVE_LOW, IPT_COIN2, 3)
PORT_BIT_IMPULSE(0x80, IP_ACTIVE_LOW, IPT_COIN1, 3)

PORT_START("IN1")	/* IN1 - port 0xf9 */
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_START2)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_START1)
PORT_BIT(0xc0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("IN2")	/* IN2 - port 0xfa */
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT)
PORT_BIT(0xc0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("IN3")	/* IN3 - port 0xfb */
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_BUTTON2)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_BUTTON1)
PORT_BIT(0xc0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("IN4")	/* IN4 - FAKE - lazy way to move the self-test fake input port to 5 */
PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("FAKE1")	/* IN5 - FAKE */
/* This fake input port is used to get the status of the F2 key, */
/* and activate the test mode, which is triggered by a NMI */
PORT_BITX(0x01, IP_ACTIVE_HIGH, IPT_SERVICE, DEF_STR(Service_Mode), OSD_KEY_F2, IP_JOY_NONE)

PORT_START("DSW1")	/* FAKE */
/* This fake input port is used for DIP Switch 1 */
/* This fake input port is used for DIP Switch 1 */
PORT_DIPNAME(0x03, 0x01, DEF_STR(Bonus_Life))
PORT_DIPSETTING(0x00, "10000")
PORT_DIPSETTING(0x02, "20000")
PORT_DIPSETTING(0x01, "30000")
PORT_DIPSETTING(0x03, "40000")
PORT_DIPNAME(0x0c, 0x00, DEF_STR(Difficulty))
PORT_DIPSETTING(0x00, "Easy")
PORT_DIPSETTING(0x08, "Normal")
PORT_DIPSETTING(0x04, "Hard")
PORT_DIPSETTING(0x0c, "Very Hard")
PORT_DIPNAME(0x30, 0x30, DEF_STR(Lives))
PORT_DIPSETTING(0x00, "2")
PORT_DIPSETTING(0x20, "3")
PORT_DIPSETTING(0x10, "4")
PORT_DIPSETTING(0x30, "5")
PORT_DIPNAME(0x40, 0x00, DEF_STR(Demo_Sounds))
PORT_DIPSETTING(0x40, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x80, 0x80, DEF_STR(Cabinet))
PORT_DIPSETTING(0x80, DEF_STR(Upright))
PORT_DIPSETTING(0x00, DEF_STR(Cocktail))

COINAGE

PORT_START("IN8")	/* IN8 - port 0xfc */
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_BUTTON2 | IPF_PLAYER2)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_PLAYER2)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_PLAYER2)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_PLAYER2)
INPUT_PORTS_END

INPUT_PORTS_START(zektor)
PORT_START("IN0")	/* IN0 - port 0xf8 */
/* The next bit is referred to as the Service switch in the self test - it just adds a credit */
PORT_BIT_IMPULSE(0x20, IP_ACTIVE_LOW, IPT_COIN3, 3)
PORT_BIT_IMPULSE(0x40, IP_ACTIVE_LOW, IPT_COIN2, 3)
PORT_BIT_IMPULSE(0x80, IP_ACTIVE_LOW, IPT_COIN1, 3)
PORT_START("IN1")	/* IN1 - port 0xf9 */
PORT_BIT(0xf0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("IN2")	/* IN2 - port 0xfa */
PORT_BIT(0xf0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("IN3")	/* IN3 - port 0xfb */
PORT_BIT(0xf0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("IN4")	/* IN4 - port 0xfc - read in machine/sega.c */
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_START1)
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_START2)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_BUTTON1)
PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_BUTTON2)
PORT_BIT(0xf0, IP_ACTIVE_HIGH, IPT_UNUSED)

PORT_START("FAKE1")	/* IN5 - FAKE */
/* This fake input port is used to get the status of the F2 key, */
/* and activate the test mode, which is triggered by a NMI */
PORT_BITX(0x01, IP_ACTIVE_HIGH, IPT_SERVICE, DEF_STR(Service_Mode), OSD_KEY_F2, IP_JOY_NONE)

PORT_START("DSW1")	/* FAKE */
/* This fake input port is used for DIP Switch 1 */
PORT_DIPNAME(0x03, 0x01, DEF_STR(Bonus_Life))
PORT_DIPSETTING(0x03, "10000")
PORT_DIPSETTING(0x01, "20000")
PORT_DIPSETTING(0x02, "30000")
PORT_DIPSETTING(0x00, "None")
PORT_DIPNAME(0x0c, 0x00, DEF_STR(Difficulty))
PORT_DIPSETTING(0x00, "Easy")
PORT_DIPSETTING(0x08, "Normal")
PORT_DIPSETTING(0x04, "Hard")
PORT_DIPSETTING(0x0c, "Very Hard")
PORT_DIPNAME(0x30, 0x30, DEF_STR(Lives))
PORT_DIPSETTING(0x00, "2")
PORT_DIPSETTING(0x20, "3")
PORT_DIPSETTING(0x10, "4")
PORT_DIPSETTING(0x30, "5")
PORT_DIPNAME(0x40, 0x00, DEF_STR(Demo_Sounds))
PORT_DIPSETTING(0x40, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x80, 0x80, DEF_STR(Cabinet))
PORT_DIPSETTING(0x80, DEF_STR(Upright))
PORT_DIPSETTING(0x00, DEF_STR(Cocktail))

COINAGE

PORT_START("IN8")		/* IN8 - FAKE port for the dial */
PORT_ANALOG(0xff, 0x00, IPT_DIAL | IPF_CENTER, 100, 10, 0, 0, 0)
INPUT_PORTS_END

INPUT_PORTS_START(startrek)
PORT_START("IN0")	/* IN0 - port 0xf8 */
/* The next bit is referred to as the Service switch in the self test - it just adds a credit */
PORT_BIT_IMPULSE(0x20, IP_ACTIVE_LOW, IPT_COIN3, 3)
PORT_BIT_IMPULSE(0x40, IP_ACTIVE_LOW, IPT_COIN2, 3)
PORT_BIT_IMPULSE(0x80, IP_ACTIVE_LOW, IPT_COIN1, 3)

PORT_START("IN1")	/* IN1 - port 0xf9 */
PORT_BIT(0xf0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("IN2")	/* IN2 - port 0xfa */
PORT_BIT(0xf0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("IN3")	/* IN3 - port 0xfb */
PORT_BIT(0xf0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("IN4")	/* IN4 - port 0xfc - read in machine/sega.c */
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_START1)
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_START2)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_BUTTON2)
PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_BUTTON1)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_BUTTON3)
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_BUTTON4)
PORT_BIT(0xc0, IP_ACTIVE_HIGH, IPT_UNUSED)

PORT_START("FAKE1")	/* IN5 - FAKE */
/* This fake input port is used to get the status of the F2 key, */
/* and activate the test mode, which is triggered by a NMI */
PORT_BITX(0x01, IP_ACTIVE_HIGH, IPT_SERVICE, DEF_STR(Service_Mode), OSD_KEY_F2, IP_JOY_NONE)

PORT_START("DSW1")	/* FAKE */
/* This fake input port is used for DIP Switch 1 */
PORT_DIPNAME(0x03, 0x01, DEF_STR(Bonus_Life))
PORT_DIPSETTING(0x00, "10000")
PORT_DIPSETTING(0x02, "20000")
PORT_DIPSETTING(0x01, "30000")
PORT_DIPSETTING(0x03, "40000")
PORT_DIPNAME(0x0c, 0x00, DEF_STR(Difficulty))
PORT_DIPSETTING(0x00, "Easy")
PORT_DIPSETTING(0x08, "Medium")
PORT_DIPSETTING(0x04, "Hard")
PORT_DIPSETTING(0x0c, "Tournament")
PORT_DIPNAME(0x30, 0x30, "Photon Torpedoes")
PORT_DIPSETTING(0x00, "1")
PORT_DIPSETTING(0x20, "2")
PORT_DIPSETTING(0x10, "3")
PORT_DIPSETTING(0x30, "4")
PORT_DIPNAME(0x40, 0x00, "Demo Sounds?")
PORT_DIPSETTING(0x40, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x80, 0x80, DEF_STR(Cabinet))
PORT_DIPSETTING(0x80, DEF_STR(Upright))
PORT_DIPSETTING(0x00, DEF_STR(Cocktail))

COINAGE

PORT_START("FAKE2")		/* IN8 - dummy port for the dial */
PORT_ANALOG(0xff, 0x00, IPT_DIAL | IPF_CENTER, 100, 10, 0, 0, 0)
INPUT_PORTS_END

INPUT_PORTS_START(tacscan)
PORT_START("IN0")	/* IN0 - port 0xf8 */
/* The next bit is referred to as the Service switch in the self test - it just adds a credit */
PORT_BIT_IMPULSE(0x20, IP_ACTIVE_LOW, IPT_COIN3, 3)
PORT_BIT_IMPULSE(0x40, IP_ACTIVE_LOW, IPT_COIN2, 3)
PORT_BIT_IMPULSE(0x80, IP_ACTIVE_LOW, IPT_COIN1, 3)

PORT_START("IN1")	/* IN1 - port 0xf9 */
PORT_BIT(0xf0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("IN2")	/* IN2 - port 0xfa */
PORT_BIT(0xf0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("IN3")	/* IN3 - port 0xfb */
PORT_BIT(0xf0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("IN4")	/* IN4 - port 0xfc - read in machine/sega.c */
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_START1)
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_START2)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_BUTTON1)
PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_BUTTON2)
PORT_BIT(0xf0, IP_ACTIVE_HIGH, IPT_UNUSED)

PORT_START("FAKE1")	/* IN5 - FAKE */
/* This fake input port is used to get the status of the F2 key, */
/* and activate the test mode, which is triggered by a NMI */
PORT_BITX(0x01, IP_ACTIVE_HIGH, IPT_SERVICE, DEF_STR(Service_Mode), OSD_KEY_F2, IP_JOY_NONE)

PORT_START("DSW1")	/* FAKE */
/* This fake input port is used for DIP Switch 1 */
PORT_DIPNAME(0x03, 0x01, DEF_STR(Bonus_Life))
PORT_DIPSETTING(0x03, "10000")
PORT_DIPSETTING(0x01, "20000")
PORT_DIPSETTING(0x02, "30000")
PORT_DIPSETTING(0x00, "None")
PORT_DIPNAME(0x0c, 0x00, DEF_STR(Difficulty))
PORT_DIPSETTING(0x00, "Easy")
PORT_DIPSETTING(0x08, "Normal")
PORT_DIPSETTING(0x04, "Hard")
PORT_DIPSETTING(0x0c, "Very Hard")
PORT_DIPNAME(0x30, 0x30, "Number of Ships")
PORT_DIPSETTING(0x00, "2")
PORT_DIPSETTING(0x20, "4")
PORT_DIPSETTING(0x10, "6")
PORT_DIPSETTING(0x30, "8")
PORT_DIPNAME(0x40, 0x00, "Demo Sounds?")
PORT_DIPSETTING(0x40, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_DIPNAME(0x80, 0x80, DEF_STR(Cabinet))
PORT_DIPSETTING(0x80, DEF_STR(Upright))
PORT_DIPSETTING(0x00, DEF_STR(Cocktail))

COINAGE

PORT_START("FAKE2")		/* IN8 - FAKE port for the dial */
PORT_ANALOG(0xff, 0x00, IPT_DIAL | IPF_CENTER, 100, 10, 0, 0, 0)
INPUT_PORTS_END

INPUT_PORTS_START(elim2)
PORT_START("IN0")	/* IN0 - port 0xf8 */
/* The next bit is referred to as the Service switch in the self test - it just adds a credit */
PORT_BIT_IMPULSE(0x20, IP_ACTIVE_LOW, IPT_COIN3, 3)
PORT_BIT_IMPULSE(0x40, IP_ACTIVE_LOW, IPT_COIN2, 3)
PORT_BIT_IMPULSE(0x80, IP_ACTIVE_LOW, IPT_COIN1, 3)

PORT_START("IN1")	/* IN1 - port 0xf9 */
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_START2)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_START1)
PORT_BIT(0xc0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("IN2")	/* IN2 - port 0xfa */
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_BUTTON2)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("IN3")	/* IN3 - port 0xfb */
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_PLAYER2)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_BUTTON1)
PORT_BIT(0xc0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("IN4")	/* IN4 - port 0xfc - read in machine/sega.c */
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_BUTTON1 | IPF_PLAYER2)
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_BUTTON2 | IPF_PLAYER2)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT | IPF_PLAYER2)
PORT_BIT(0xf8, IP_ACTIVE_HIGH, IPT_UNUSED)

PORT_START("FAKE1")	/* IN5 - FAKE */
/* This fake input port is used to get the status of the F2 key, */
/* and activate the test mode, which is triggered by a NMI */
PORT_BITX(0x01, IP_ACTIVE_HIGH, IPT_SERVICE, DEF_STR(Service_Mode), OSD_KEY_F2, IP_JOY_NONE)

PORT_START("DSW1")	/* FAKE */
/* This fake input port is used for DIP Switch 1 */
PORT_DIPNAME(0x03, 0x02, DEF_STR(Bonus_Life))
PORT_DIPSETTING(0x01, "10000")
PORT_DIPSETTING(0x02, "20000")
PORT_DIPSETTING(0x00, "30000")
PORT_DIPSETTING(0x03, "None")
PORT_DIPNAME(0x0c, 0x00, DEF_STR(Difficulty))
PORT_DIPSETTING(0x00, "Easy")
PORT_DIPSETTING(0x08, "Normal")
PORT_DIPSETTING(0x04, "Hard")
PORT_DIPSETTING(0x0c, "Very Hard")
PORT_DIPNAME(0x30, 0x20, DEF_STR(Lives))
PORT_DIPSETTING(0x20, "3")
PORT_DIPSETTING(0x10, "4")
PORT_DIPSETTING(0x00, "5")
/* 0x30 gives 5 Lives */
PORT_DIPNAME(0x80, 0x80, DEF_STR(Cabinet))
PORT_DIPSETTING(0x80, DEF_STR(Upright))
PORT_DIPSETTING(0x00, DEF_STR(Cocktail))

COINAGE
INPUT_PORTS_END

INPUT_PORTS_START(elim4)
PORT_START("IN0")	/* IN0 - port 0xf8 */
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_BUTTON1)
/* The next bit is referred to as the Service switch in the self test - it just adds a credit */
PORT_BIT_IMPULSE(0x20, IP_ACTIVE_LOW, IPT_COIN1, 3)
PORT_BIT_IMPULSE(0x40, IP_ACTIVE_LOW, IPT_COIN2, 3)
PORT_BIT_IMPULSE(0x80, IP_ACTIVE_LOW, IPT_COIN3, 3)

PORT_START("IN1")	/* IN1 - port 0xf9 */
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_BUTTON2)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_BUTTON2 | IPF_PLAYER2)
PORT_BIT(0xc0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("IN2")	/* IN2 - port 0xfa */
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_PLAYER2)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_PLAYER2)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("IN3")	/* IN3 - port 0xfb */
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT | IPF_PLAYER2)
PORT_BIT(0xc0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("IN4")	/* IN4 - port 0xfc - read in machine/sega.c */
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_BUTTON1 | IPF_PLAYER3)
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_BUTTON2 | IPF_PLAYER3)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT | IPF_PLAYER3)
PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT | IPF_PLAYER3)
PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_BUTTON1 | IPF_PLAYER4)
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_BUTTON2 | IPF_PLAYER4)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT | IPF_PLAYER4)
PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT | IPF_PLAYER4)

PORT_START("FAKE1")	/* IN5 - FAKE */
/* This fake input port is used to get the status of the F2 key, */
/* and activate the test mode, which is triggered by a NMI */
PORT_BITX(0x01, IP_ACTIVE_HIGH, IPT_SERVICE, DEF_STR(Service_Mode), OSD_KEY_F2, IP_JOY_NONE)

PORT_START("DSW1")	/* FAKE */
/* This fake input port is used for DIP Switch 1 */
PORT_DIPNAME(0x03, 0x02, DEF_STR(Bonus_Life))
PORT_DIPSETTING(0x01, "10000")
PORT_DIPSETTING(0x02, "20000")
PORT_DIPSETTING(0x00, "30000")
PORT_DIPSETTING(0x03, "None")
PORT_DIPNAME(0x0c, 0x00, DEF_STR(Difficulty))
PORT_DIPSETTING(0x00, "Easy")
PORT_DIPSETTING(0x08, "Normal")
PORT_DIPSETTING(0x04, "Hard")
PORT_DIPSETTING(0x0c, "Very Hard")
PORT_DIPNAME(0x30, 0x30, DEF_STR(Lives))
PORT_DIPSETTING(0x20, "3")
PORT_DIPSETTING(0x10, "4")
PORT_DIPSETTING(0x00, "5")
/* 0x30 gives 5 Lives */
PORT_DIPNAME(0x80, 0x80, DEF_STR(Cabinet))
PORT_DIPSETTING(0x80, DEF_STR(Upright))
PORT_DIPSETTING(0x00, DEF_STR(Cocktail))

PORT_START("IN7")  /* That is the coinage port in all the other games */
PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNKNOWN)

PORT_START("FAKE2")		/* IN8 - FAKE - port 0xfc - read in machine/sega.c */
PORT_BIT_IMPULSE(0x01, IP_ACTIVE_HIGH, IPT_COIN1, 3)
PORT_BIT_IMPULSE(0x02, IP_ACTIVE_HIGH, IPT_COIN2, 3)
PORT_BIT_IMPULSE(0x04, IP_ACTIVE_HIGH, IPT_COIN3, 3)
PORT_BIT_IMPULSE(0x08, IP_ACTIVE_HIGH, IPT_COIN4, 3)
PORT_BIT(0xf0, IP_ACTIVE_HIGH, IPT_UNUSED)
INPUT_PORTS_END


/////////////// SEGA G80 ROMS
//
ROM_START(zektor)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("1611.cpu-u25", 0x0000, 0x0800, CRC(6245aa23) SHA1(815f3c7edad9c290b719a60964085e90e7268112))
ROM_LOAD("1586.prom-u1", 0x0800, 0x0800, CRC(efeb4fb5) SHA1(b337179c01870c953b8d38c20263802e9a7936d3))
ROM_LOAD("1587.prom-u2", 0x1000, 0x0800, CRC(daa6c25c) SHA1(061e390775b6dd24f85d51951267bca4339a3845))
ROM_LOAD("1588.prom-u3", 0x1800, 0x0800, CRC(62b67dde) SHA1(831bad0f5a601d6859f69c70d0962c970d92db0e))
ROM_LOAD("1589.prom-u4", 0x2000, 0x0800, CRC(c2db0ba4) SHA1(658773f2b56ea805d7d678e300f9bbc896fbf176))
ROM_LOAD("1590.prom-u5", 0x2800, 0x0800, CRC(4d948414) SHA1(f60d295b0f8f798126dbfdc197943d8511238390))
ROM_LOAD("1591.prom-u6", 0x3000, 0x0800, CRC(b0556a6c) SHA1(84b481cc60dc3df3a1cf18b1ece4c70bcc7bb5a1))
ROM_LOAD("1592.prom-u7", 0x3800, 0x0800, CRC(750ecadf) SHA1(83ddd482230fbf6cf78a054fb4abd5bc8aec3ec8))
ROM_LOAD("1593.prom-u8", 0x4000, 0x0800, CRC(34f8850f) SHA1(d93594e529aca8d847c9f1e9055f1840f6069fb2))
ROM_LOAD("1594.prom-u9", 0x4800, 0x0800, CRC(52b22ab2) SHA1(c8f822a1a54081cfc88149c97b4dc19aa745a8d5))
ROM_LOAD("1595.prom-u10", 0x5000, 0x0800, CRC(a704d142) SHA1(95c1249a8efd1a69972ffd7a4da76a0bca5095d9))
ROM_LOAD("1596.prom-u11", 0x5800, 0x0800, CRC(6975e33d) SHA1(3f12037edd6f1b803b5f864789f4b88958ac9578))
ROM_LOAD("1597.prom-u12", 0x6000, 0x0800, CRC(d48ab5c2) SHA1(3f4faf4b131b120b30cd4e73ff34d5cd7ef6c47a))
ROM_LOAD("1598.prom-u13", 0x6800, 0x0800, CRC(ab54a94c) SHA1(9dd57b4b6e46d46922933128d9786df011c6133d))
ROM_LOAD("1599.prom-u14", 0x7000, 0x0800, CRC(c9d4f3a5) SHA1(8516914b49fad85222cbdd9a43609834f5d0f13d))
ROM_LOAD("1600.prom-u15", 0x7800, 0x0800, CRC(893b7dbc) SHA1(136135f0be2e8dddfa0d21a5f4119ee4685c4866))
ROM_LOAD("1601.prom-u16", 0x8000, 0x0800, CRC(867bdf4f) SHA1(5974d32d878206abd113f74ba20fa5276cf21a6f))
ROM_LOAD("1602.prom-u17", 0x8800, 0x0800, CRC(bd447623) SHA1(b8d255aeb32096891379330c5b8adf1d151d70c2))
ROM_LOAD("1603.prom-u18", 0x9000, 0x0800, CRC(9f8f10e8) SHA1(ffe9d872d9011b3233cb06d966852319f9e4cd01))
ROM_LOAD("1604.prom-u19", 0x9800, 0x0800, CRC(ad2f0f6c) SHA1(494a224905b1dac58b3b50f65a8be986b68b06f2))
ROM_LOAD("1605.prom-u20", 0xa000, 0x0800, CRC(e27d7144) SHA1(5b82fda797d86e11882d1f9738a59092c5e3e7d8))
ROM_LOAD("1606.prom-u21", 0xa800, 0x0800, CRC(7965f636) SHA1(5c8720beedab4979a813ce7f0e8961c863973ff7))

ROM_REGION(0x0420, REGION_PROMS, 0)
ROM_LOAD("s-c.xyt-u39", 0x0000, 0x0400, CRC(56484d19) SHA1(61f43126fdcfc230638ed47085ae037a098e6781))  // sine table
ROM_LOAD("pr-82.cpu-u15", 0x0400, 0x0020, CRC(c609b79e) SHA1(49dbcbb607079a182d7eb396c0da097166ea91c9))  // CPU board addressing
ROM_END

ROM_START(tacscan)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("1711a.cpu-u25", 0x0000, 0x0800, CRC(da13158) SHA1(256c5441a4841441501c9b7bcf09e0e99e8dd671))
ROM_LOAD("1670c.prom-u1", 0x0800, 0x0800, CRC(98de6fd5) SHA1(f22c215d7558e00366fec5092abb51c670468f8c))
ROM_LOAD("1671a.prom-u2", 0x1000, 0x0800, CRC(dc400074) SHA1(70093ef56e0784173a06da1ac781bb9d8c4e7fc5))
ROM_LOAD("1672a.prom-u3", 0x1800, 0x0800, CRC(2caf6f7e) SHA1(200119260f78bb1c5389707b3ceedfbc1ae43549))
ROM_LOAD("1673a.prom-u4", 0x2000, 0x0800, CRC(1495ce3d) SHA1(3189f8061961d90a52339c855c06e81f4537fb2b))
ROM_LOAD("1674a.prom-u5", 0x2800, 0x0800, CRC(ab7fc5d9) SHA1(b2d9241d83d175ead4da36d7311a41a5f972e06a))
ROM_LOAD("1675a.prom-u6", 0x3000, 0x0800, CRC(cf5e5016) SHA1(78a3f1e4a905515330d4737ac38576ac6e0d8611))
ROM_LOAD("1676a.prom-u7", 0x3800, 0x0800, CRC(b61a3ab3) SHA1(0f4ef5c7fe299ad20fa4637260282a733f1cf461))
ROM_LOAD("1677a.prom-u8", 0x4000, 0x0800, CRC(bc0273b1) SHA1(8e8d8830f17b9fa6d45d98108ca02d90c29de574))
ROM_LOAD("1678b.prom-u9", 0x4800, 0x0800, CRC(7894da98) SHA1(2de7c121ad847e51a10cb1b81aec84cc44a3d04c))
ROM_LOAD("1679a.prom-u10", 0x5000, 0x0800, CRC(db865654) SHA1(db4d5675b53ff2bbaf70090fd064e98862f4ad33))
ROM_LOAD("1680a.prom-u11", 0x5800, 0x0800, CRC(2c2454de) SHA1(74101806439c9faeba88ffe573fa4f93ffa0ba3c))
ROM_LOAD("1681a.prom-u12", 0x6000, 0x0800, CRC(77028885) SHA1(bc981620ebbfbe4e32b3b4d00504475634454c57))
ROM_LOAD("1682a.prom-u13", 0x6800, 0x0800, CRC(babe5cf1) SHA1(26219b7a26f818fee2fe579ec6fb0b16c6bf056f))
ROM_LOAD("1683a.prom-u14", 0x7000, 0x0800, CRC(1b98b618) SHA1(19854cb2741ba37c11ae6d429fa6c17ff930f5e5))
ROM_LOAD("1684a.prom-u15", 0x7800, 0x0800, CRC(cb3ded3b) SHA1(f1e886f4f71b0f6f2c11fb8b4921c3452fc9b2c0))
ROM_LOAD("1685a.prom-u16", 0x8000, 0x0800, CRC(43016a79) SHA1(ee22c1fe0c8df90d9215175104f8a796c3d2aed3))
ROM_LOAD("1686a.prom-u17", 0x8800, 0x0800, CRC(a4397772) SHA1(cadc95b869f5bf5dba7f03dfe5ae64a50899cced))
ROM_LOAD("1687a.prom-u18", 0x9000, 0x0800, CRC(2f3bc4) SHA1(7f3795a05d5651c90cdcd4d00c46d05178b433ea))
ROM_LOAD("1688a.prom-u19", 0x9800, 0x0800, CRC(326d87a) SHA1(3a5ea4526db417b9e00b24b019c1c6016773c9e7))
ROM_LOAD("1709a.prom-u20", 0xa000, 0x0800, CRC(f35ed1ec) SHA1(dce95a862af0c6b67fb76b99fee0523d53b7551c))
ROM_LOAD("1710a.prom-u21", 0xa800, 0x0800, CRC(6203be22) SHA1(89731c7c88d0125a11368d707f566eb53c783266))

ROM_REGION(0x0420, REGION_PROMS, 0)
ROM_LOAD("s-c.xyt-u39", 0x0000, 0x0400, CRC(56484d19) SHA1(61f43126fdcfc230638ed47085ae037a098e6781))  // sine table
ROM_LOAD("pr-82.cpu-u15", 0x0400, 0x0020, CRC(c609b79e) SHA1(49dbcbb607079a182d7eb396c0da097166ea91c9))  // CPU board addressing
ROM_END

ROM_START(spacfura)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("969a.cpu-u25", 0x0000, 0x0800, CRC(896a615c) SHA1(542386196eca9fd822e36508e173201ee8a962ed))
ROM_LOAD("960a.prom-u1", 0x0800, 0x0800, CRC(e1ea7964) SHA1(9c84c525973fcf1437b062d98195272723249d02))
ROM_LOAD("961a.prom-u2", 0x1000, 0x0800, CRC(cdb04233) SHA1(6f8d2fe6d46d04ebe94b7943006d63b24c88ed5a))
ROM_LOAD("962a.prom-u3", 0x1800, 0x0800, CRC(5f03e632) SHA1(c6e8d72ba13ab05ec01a78502948a73c21e0fd69))
ROM_LOAD("963a.prom-u4", 0x2000, 0x0800, CRC(45a77b44) SHA1(91f4822b89ec9c16c67c781a11fabfa4b9914660))
ROM_LOAD("964a.prom-u5", 0x2800, 0x0800, CRC(ba008f8b) SHA1(24f5bef240ae2bcfd5b1d95f51b3599f79518b56))
ROM_LOAD("965a.prom-u6", 0x3000, 0x0800, CRC(78677d31) SHA1(ed5810aa4bddbfe36a6ff9992dd0cb58cce66836))
ROM_LOAD("966a.prom-u7", 0x3800, 0x0800, CRC(a8a51105) SHA1(f5e0fa662552f50fa6905f579d4c678b790ffa96))
ROM_LOAD("967a.prom-u8", 0x4000, 0x0800, CRC(d60f667d) SHA1(821271ec1918e22ed29a5b1f4b0182765ef5ba10))
ROM_LOAD("968a.prom-u9", 0x4800, 0x0800, CRC(aea85b6a) SHA1(8778ff0be34cd4fd5b8f6f76c64bfca68d4d240e))
ROM_REGION(0x0420, REGION_PROMS, 0)
ROM_LOAD("s-c.xyt-u39", 0x0000, 0x0400, CRC(56484d19) SHA1(61f43126fdcfc230638ed47085ae037a098e6781))  // sine table
ROM_LOAD("pr-82.cpu-u15", 0x0400, 0x0020, CRC(c609b79e) SHA1(49dbcbb607079a182d7eb396c0da097166ea91c9))  // CPU board addressing
ROM_END

ROM_START(spacfury)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("969c.cpu-u25", 0x0000, 0x0800, CRC(411207f2) SHA1(2a082be4052b5d8f365abd0a51ea805d270d1189))
ROM_LOAD("960c.prom-u1", 0x0800, 0x0800, CRC(d071ab7e) SHA1(c7d2429e4fa77988d7ac62bc68f876ffb7467838))
ROM_LOAD("961c.prom-u2", 0x1000, 0x0800, CRC(aebc7b97) SHA1(d0a0328ed34de9bd2c83da4ddc2d017e2b5a8bdc))
ROM_LOAD("962c.prom-u3", 0x1800, 0x0800, CRC(dbbba35e) SHA1(0400d1ba09199d19f5b8aa5bb1a85ed27930822d))
ROM_LOAD("963c.prom-u4", 0x2000, 0x0800, CRC(d9e9eadc) SHA1(1ad228d65dca48d084bbac358af80882886e7a40))
ROM_LOAD("964c.prom-u5", 0x2800, 0x0800, CRC(7ed947b6) SHA1(c0fd7ed74a87cc422a42e2a4f9eb947f5d5d9fed))
ROM_LOAD("965c.prom-u6", 0x3000, 0x0800, CRC(d2443a22) SHA1(45e5d43eae89e25370bb8e8db2b664642a238eb9))
ROM_LOAD("966c.prom-u7", 0x3800, 0x0800, CRC(1985ccfc) SHA1(8c5931519b976c82a94d17279cc919b4baad5bb7))
ROM_LOAD("967c.prom-u8", 0x4000, 0x0800, CRC(330f0751) SHA1(07ae52fdbfa2cc326f88dc76c3dc8e145b592863))
ROM_LOAD("968c.prom-u9", 0x4800, 0x0800, CRC(8366eadb) SHA1(8e4cb30a730237da2e933370faf5eaa1a41cacbf))

ROM_REGION(0x0420, REGION_PROMS, 0)
ROM_LOAD("s-c.xyt-u39", 0x0000, 0x0400, CRC(56484d19) SHA1(61f43126fdcfc230638ed47085ae037a098e6781))  // sine table
ROM_LOAD("pr-82.cpu-u15", 0x0400, 0x0020, CRC(c609b79e) SHA1(49dbcbb607079a182d7eb396c0da097166ea91c9))  // CPU board addressing
ROM_END

ROM_START(spacfurb)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("969a.cpu-u25", 0x0000, 0x0800, CRC(896a615c) SHA1(542386196eca9fd822e36508e173201ee8a962ed))
ROM_LOAD("960b.prom-u1", 0x0800, 0x0800, CRC(8a99b63f) SHA1(4b9ec152e0fad50afeea11f5d61331f3211da606))
ROM_LOAD("961b.prom-u2", 0x1000, 0x0800, CRC(c72c1609) SHA1(b489423b52a15275b63f6b01b9aa75ad1ce777b5))
ROM_LOAD("962b.prom-u3", 0x1800, 0x0800, CRC(7ffc338d) SHA1(2c37049657305c465e3a5301e0be9f1afc9333c0))
ROM_LOAD("963b.prom-u4", 0x2000, 0x0800, CRC(4fe0bd88) SHA1(d1902c8b2c2577fb49894aeac4c9d6b8cf38f2f6))
ROM_LOAD("964b.prom-u5", 0x2800, 0x0800, CRC(9b359db) SHA1(e1d6af48680dc0f34068ee6e916650dac738e280))
ROM_LOAD("965b.prom-u6", 0x3000, 0x0800, CRC(7c1f9b71) SHA1(ebe29a558e7239b4f0bc49a1fe92e5f1903edce3))
ROM_LOAD("966b.prom-u7", 0x3800, 0x0800, CRC(8933b852) SHA1(dabb219195a668893c82ccc80ed09989f7fd83af))
ROM_LOAD("967b.prom-u8", 0x4000, 0x0800, CRC(82b5768d) SHA1(823d8c0a537bad62e8186f88f8d02a0f3dc6da0f))
ROM_LOAD("968b.prom-u9", 0x4800, 0x0800, CRC(fea68f02) SHA1(83bef40dfaac014b7929239d81075335ff8fd506))

ROM_REGION(0x0420, REGION_PROMS, 0)
ROM_LOAD("s-c.xyt-u39", 0x0000, 0x0400, CRC(56484d19) SHA1(61f43126fdcfc230638ed47085ae037a098e6781))  // sine table
ROM_LOAD("pr-82.cpu-u15", 0x0400, 0x0020, CRC(c609b79e) SHA1(49dbcbb607079a182d7eb396c0da097166ea91c9))  // CPU board addressing
ROM_END

ROM_START(startrek)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("1873.cpu-u25", 0x0000, 0x0800, CRC(be46f5d9) SHA1(fadf13042d31b0dacf02a3166545c946f6fd3f33))
ROM_LOAD("1848.prom-u1", 0x0800, 0x0800, CRC(65e3baf3) SHA1(0c081ed6c8be0bb5eb3d5769ac1f0b8fe4735d11))
ROM_LOAD("1849.prom-u2", 0x1000, 0x0800, CRC(8169fd3d) SHA1(439d4b857083ae40df7d7f53c36ec13b05d86a86))
ROM_LOAD("1850.prom-u3", 0x1800, 0x0800, CRC(78fd68dc) SHA1(fb56567458807d9becaacac11091931af9889620))
ROM_LOAD("1851.prom-u4", 0x2000, 0x0800, CRC(3f55ab86) SHA1(f75ce0c56e22e8758dd1f5ce9ac00f5f41b13465))
ROM_LOAD("1852.prom-u5", 0x2800, 0x0800, CRC(2542ecfb) SHA1(7cacee44670768e9fae1024f172b867193d2ea4a))
ROM_LOAD("1853.prom-u6", 0x3000, 0x0800, CRC(75c2526a) SHA1(6e86b30fcdbe7622ab873092e7a7a46d8bad790f))
ROM_LOAD("1854.prom-u7", 0x3800, 0x0800, CRC(96d75d0) SHA1(26e90c296b00239a6cde4ec5e80cccd7bb36bcbd))
ROM_LOAD("1855.prom-u8", 0x4000, 0x0800, CRC(bc7b9a12) SHA1(6dc60e380dc5790cd345b06c064ea7d69570aadb))
ROM_LOAD("1856.prom-u9", 0x4800, 0x0800, CRC(ed9fe2fb) SHA1(5d56e8499cb4f54c5e76a9231c53d95777777e05))
ROM_LOAD("1857.prom-u10", 0x5000, 0x0800, CRC(28699d45) SHA1(c133eb4fc13987e634d3789bfeaf9e03196f8fd3))
ROM_LOAD("1858.prom-u11", 0x5800, 0x0800, CRC(3a7593cb) SHA1(7504f960507579d043b7ee20fb8fd2610399ff4b))
ROM_LOAD("1859.prom-u12", 0x6000, 0x0800, CRC(5b11886b) SHA1(b0fb6e912953822242501943f7214e4af6ab7891))
ROM_LOAD("1860.prom-u13", 0x6800, 0x0800, CRC(62eb96e6) SHA1(51d1f5e48e3e21147584ace61b8832ad892cb6e2))
ROM_LOAD("1861.prom-u14", 0x7000, 0x0800, CRC(99852d1d) SHA1(eaea6a99f0a7f0292db3ea19649b5c1be45b9507))
ROM_LOAD("1862.prom-u15", 0x7800, 0x0800, CRC(76ce27b2) SHA1(8fa8d73aa4dcf3709ecd057bad3278fac605988c))
ROM_LOAD("1863.prom-u16", 0x8000, 0x0800, CRC(dd92d187) SHA1(5a11cdc91bb7b36ea98503892847d8dbcedfe95a))
ROM_LOAD("1864.prom-u17", 0x8800, 0x0800, CRC(e37d3a1e) SHA1(15d949989431dcf1e0406f1e3745f3ee91012ff5))
ROM_LOAD("1865.prom-u18", 0x9000, 0x0800, CRC(b2ec8125) SHA1(70982c614471614f6b490ae2d65faec0eff2ac37))
ROM_LOAD("1866.prom-u19", 0x9800, 0x0800, CRC(6f188354) SHA1(e99946467090b68559c2b54ad2e85204b71a459f))
ROM_LOAD("1867.prom-u20", 0xa000, 0x0800, CRC(b0a3eae8) SHA1(51a0855753dc2d4fe1a05bd54fa958beeab35299))
ROM_LOAD("1868.prom-u21", 0xa800, 0x0800, CRC(8b4e2e07) SHA1(11f7de6327abf88012854417224b38a2352a9dc7))
ROM_LOAD("1869.prom-u22", 0xb000, 0x0800, CRC(e5663070) SHA1(735944c2b924964f72f3bb3d251a35ea2aef3d15))
ROM_LOAD("1870.prom-u23", 0xb800, 0x0800, CRC(4340616d) SHA1(e93686a29377933332523425532d102e30211111))

ROM_REGION(0x0420, REGION_PROMS, 0)
ROM_LOAD("s-c.xyt-u39", 0x0000, 0x0400, CRC(56484d19) SHA1(61f43126fdcfc230638ed47085ae037a098e6781))  // sine table
ROM_LOAD("pr-82.cpu-u15", 0x0400, 0x0020, CRC(c609b79e) SHA1(49dbcbb607079a182d7eb396c0da097166ea91c9))  // CPU board addressing
ROM_END

ROM_START(elim2)
ROM_REGION(0x10000, REGION_CPU1, 0)
ROM_LOAD("969.cpu-u25", 0x0000, 0x0800, CRC(411207f2) SHA1(2a082be4052b5d8f365abd0a51ea805d270d1189))
ROM_LOAD("1333.prom-u1", 0x0800, 0x0800, CRC(fd2a2916) SHA1(431d340c0c9257d66f5851a591861bcefb600cec))
ROM_LOAD("1334.prom-u2", 0x1000, 0x0800, CRC(79eb5548) SHA1(d951de5c0ab94fdb6e58207ee9a147674dd74220))
ROM_LOAD("1335.prom-u3", 0x1800, 0x0800, CRC(3944972e) SHA1(59c84cf23898adb7e434c5802dbb821c79099890))
ROM_LOAD("1336.prom-u4", 0x2000, 0x0800, CRC(852f7b4d) SHA1(6db45b9d11374f4cadf185aec81f33c0040bc001))
ROM_LOAD("1337.prom-u5", 0x2800, 0x0800, CRC(cf932b08) SHA1(f0b61ca8266fd3de7522244c9b1587eecd24a4f1))
ROM_LOAD("1338.prom-u6", 0x3000, 0x0800, CRC(99a3f3c9) SHA1(aa7d4805c70311ebe24ff70fcc32c0e2a7c4488a))
ROM_LOAD("1339.prom-u7", 0x3800, 0x0800, CRC(d35f0fa3) SHA1(752f14b298604a9b91e94cd6d5d291ef33f27ec0))
ROM_LOAD("1340.prom-u8", 0x4000, 0x0800, CRC(8fd4da21) SHA1(f30627dd1fbcc12bb587742a9072bbf38ba48401))
ROM_LOAD("1341.prom-u9", 0x4800, 0x0800, CRC(629c9a28) SHA1(cb7df14ea1bb2d7997bfae1ca70b47763c73298a))
ROM_LOAD("1342.prom-u10", 0x5000, 0x0800, CRC(643df651) SHA1(80c5da44b5d2a7d97c7ba0067f773eb645a9d432))
ROM_LOAD("1343.prom-u11", 0x5800, 0x0800, CRC(d29d70d2) SHA1(ee2cd752b99ebd522eccf5e71d02c31479acfdf5))
ROM_LOAD("1344.prom-u12", 0x6000, 0x0800, CRC(c5e153a3) SHA1(7e805573aeed01e3d4ed477870800dd7ecad7a1b))
ROM_LOAD("1345.prom-u13", 0x6800, 0x0800, CRC(40597a92) SHA1(ee1ae2b424c38b40d2cbeda4aba3328e6d3f9c81))

ROM_REGION(0x0420, REGION_PROMS, 0)
ROM_LOAD("s-c.xyt-u39", 0x0000, 0x0400, CRC(56484d19) SHA1(61f43126fdcfc230638ed47085ae037a098e6781))  // sine table
ROM_LOAD("pr-82.cpu-u15", 0x0400, 0x0020, CRC(c609b79e) SHA1(49dbcbb607079a182d7eb396c0da097166ea91c9))  // CPU board addressing
ROM_END

ROM_START(elim2a)
ROM_REGION(0xc000, REGION_CPU1, 0)
ROM_LOAD("969.cpu-u25", 0x0000, 0x0800, CRC(411207f2) SHA1(2a082be4052b5d8f365abd0a51ea805d270d1189))
ROM_LOAD("1158.prom-u1", 0x0800, 0x0800, CRC(a40ac3a5) SHA1(9cf707e3439def17390ae16b49552fb1996a6335))
ROM_LOAD("1159.prom-u2", 0x1000, 0x0800, CRC(ff100604) SHA1(1636337c702473b5a567832a622b0c09bd1e2aba))
ROM_LOAD("1160a.prom-u3", 0x1800, 0x0800, CRC(ebfe33bd) SHA1(226da36becd278d34030f564fef61851819d2324))
ROM_LOAD("1161a.prom-u4", 0x2000, 0x0800, CRC(03d41db3) SHA1(da9e618314c01b56b9d66abd14f1e5bf928fff54))
ROM_LOAD("1162a.prom-u5", 0x2800, 0x0800, CRC(f2c7ece3) SHA1(86a9099ce97439cd849dc32ed2c164a1be14e4e7))
ROM_LOAD("1163a.prom-u6", 0x3000, 0x0800, CRC(1fc58b00) SHA1(732c57781cd45cd301b2337b6879ff811d9692f3))
ROM_LOAD("1164a.prom-u7", 0x3800, 0x0800, CRC(f37480d1) SHA1(3d7fac05d60083ddcd51c0190078c89a39f79a91))
ROM_LOAD("1165a.prom-u8", 0x4000, 0x0800, CRC(328819f8) SHA1(ed5e3488399b4481e69f623404a28515524af60a))
ROM_LOAD("1166a.prom-u9", 0x4800, 0x0800, CRC(1b8e8380) SHA1(d56ccc4fac9c8149ebef4939ba401372d69bf022))
ROM_LOAD("1167a.prom-u10", 0x5000, 0x0800, CRC(16aa3156) SHA1(652a547ff1cb4ede507418b392e28f30a3cc179c))
ROM_LOAD("1168a.prom-u11", 0x5800, 0x0800, CRC(3c7c893a) SHA1(73d2835833a6d40f6a9b0a87364af48a449d9674))
ROM_LOAD("1169a.prom-u12", 0x6000, 0x0800, CRC(5cee23b1) SHA1(66f6fc6163148608296e3d25abb194559a2f5179))
ROM_LOAD("1170a.prom-u13", 0x6800, 0x0800, CRC(8cdacd35) SHA1(f24f8a74cb4b8452ddbd42e61d3b0366bbee7f98))

ROM_REGION(0x0420, REGION_PROMS, 0)
ROM_LOAD("s-c.xyt-u39", 0x0000, 0x0400, CRC(56484d19) SHA1(61f43126fdcfc230638ed47085ae037a098e6781))  // sine table
ROM_LOAD("pr-82.cpu-u15", 0x0400, 0x0020, CRC(c609b79e) SHA1(49dbcbb607079a182d7eb396c0da097166ea91c9))  // CPU board addressing
ROM_END

ROM_START(elim2c)
ROM_REGION(0xc000, REGION_CPU1, 0)
ROM_LOAD("969t.cpu-u25", 0x0000, 0x0800, CRC(896a615c) SHA1(542386196eca9fd822e36508e173201ee8a962ed))
ROM_LOAD("1200.prom-u1", 0x0800, 0x0800, CRC(590beb6a) SHA1(307c33cbc0b90f290aac302366e3ce4f70e5265e))
ROM_LOAD("1201.prom-u2", 0x1000, 0x0800, CRC(fed32b30) SHA1(51fba99d3bf543318ebe70ee1aa91e3171767d6f))
ROM_LOAD("1202.prom-u3", 0x1800, 0x0800, CRC(0a2068d0) SHA1(90acf1e78f5c3266d1fbc31470ad4d6a8cb43fe8))
ROM_LOAD("1203.prom-u4", 0x2000, 0x0800, CRC(1f593aa2) SHA1(aaad927174fa806d2c602b5672b1396eb9ec50fa))
ROM_LOAD("1204.prom-u5", 0x2800, 0x0800, CRC(046f1030) SHA1(632ac37b84007f169ce72877d8089538413ba20b))
ROM_LOAD("1205.prom-u6", 0x3000, 0x0800, CRC(8d10b870) SHA1(cc91a06c6b0e1697c399700bc351384360ecd5a3))
ROM_LOAD("1206.prom-u7", 0x3800, 0x0800, CRC(7f6c5afa) SHA1(0e684c654cfe2365e7d21e7bccb25f1ddb883770))
ROM_LOAD("1207.prom-u8", 0x4000, 0x0800, CRC(6cc74d62) SHA1(3392e5cd177885be7391a2699164f39302554d26))
ROM_LOAD("1208.prom-u9", 0x4800, 0x0800, CRC(cc37a631) SHA1(084ecc6b0179fe4f984131d057d5de5382443911))
ROM_LOAD("1209.prom-u10", 0x5000, 0x0800, CRC(844922f8) SHA1(0ad201fce2eaa7dde77d8694d226aad8b9a46ea7))
ROM_LOAD("1210.prom-u11", 0x5800, 0x0800, CRC(7b289783) SHA1(5326ca94b5197ef99db4ea3b28051090f0d7a9ce))
ROM_LOAD("1211.prom-u12", 0x6000, 0x0800, CRC(17349db7) SHA1(8e7ee1fbf153a36a13f3252ca4c588df531b56ec))
ROM_LOAD("1212.prom-u13", 0x6800, 0x0800, CRC(152cf376) SHA1(56c3141598b8bac81e85b1fc7052fdd19cd95609))

ROM_REGION(0x0420, REGION_PROMS, 0)
ROM_LOAD("s-c.xyt-u39", 0x0000, 0x0400, CRC(56484d19) SHA1(61f43126fdcfc230638ed47085ae037a098e6781))   // sine table
ROM_LOAD("pr-82.cpu-u15", 0x0400, 0x0020, CRC(c609b79e) SHA1(49dbcbb607079a182d7eb396c0da097166ea91c9))   // CPU board addressing
ROM_END

ROM_START(elim4)
ROM_REGION(0xc000, REGION_CPU1, 0)
ROM_LOAD("1390.cpu-u25", 0x0000, 0x0800, CRC(97010c3e) SHA1(b07db05abf48461b633bbabe359a973a5bc6da13))
ROM_LOAD("1347.prom-u1", 0x0800, 0x0800, CRC(657d7320) SHA1(ef8a637d94dfa8b9dfa600269d914d635e597a9c))
ROM_LOAD("1348.prom-u2", 0x1000, 0x0800, CRC(b15fe578) SHA1(d53773a5f7ec3c130d4ff75a5348a9f37c82c7c8))
ROM_LOAD("1349.prom-u3", 0x1800, 0x0800, CRC(0702b586) SHA1(9847172872419c475d474ff09612c38b867e15af))
ROM_LOAD("1350.prom-u4", 0x2000, 0x0800, CRC(4168dd3b) SHA1(1f26877c63cd7983dfa9a869e0442e8a213f382f))
ROM_LOAD("1351.prom-u5", 0x2800, 0x0800, CRC(c950f24c) SHA1(497a9aa7b9d040a4ff7b3f938093edec2218120d))
ROM_LOAD("1352.prom-u6", 0x3000, 0x0800, CRC(dc8c91cc) SHA1(c99224c7e57dfce9440771f78ce90ea576feed2a))
ROM_LOAD("1353.prom-u7", 0x3800, 0x0800, CRC(11eda631) SHA1(8ba926268762d491d28d5629d5a310b1accca47d))
ROM_LOAD("1354.prom-u8", 0x4000, 0x0800, CRC(b9dd6e7a) SHA1(ab6780f0abe99a5ef76746d45384e80399c6d611))
ROM_LOAD("1355.prom-u9", 0x4800, 0x0800, CRC(c92c7237) SHA1(18aad6618df51a1980775a3aaa4447205453a8af))
ROM_LOAD("1356.prom-u10", 0x5000, 0x0800, CRC(889b98e3) SHA1(23661149e7ffbdbc2c95920d13e9b8b24f86cd9a))
ROM_LOAD("1357.prom-u11", 0x5800, 0x0800, CRC(d79248a5) SHA1(e58062d5c4e5f6fe8d70dd9b55d46a57137c9a64))
ROM_LOAD("1358.prom-u12", 0x6000, 0x0800, CRC(c5dabc77) SHA1(2dc59e627f40fefefc206f2e9d070a62606e44fc))
ROM_LOAD("1359.prom-u13", 0x6800, 0x0800, CRC(24c8e5d8) SHA1(d0ae6e1dfd05d170c49837760369f04df4eaa14f))
ROM_LOAD("1360.prom-u14", 0x7000, 0x0800, CRC(96d48238) SHA1(76a7b49081cd2d0dd1976077aa66b6d5ae5b2b43))

ROM_REGION(0x0420, REGION_PROMS, 0)
ROM_LOAD("s-c.xyt-u39", 0x0000, 0x0400, CRC(56484d19) SHA1(61f43126fdcfc230638ed47085ae037a098e6781))   // sine table
ROM_LOAD("pr-82.cpu-u15", 0x0400, 0x0020, CRC(c609b79e) SHA1(49dbcbb607079a182d7eb396c0da097166ea91c9))   // CPU board addressing
ROM_END

ROM_START(elim4p)
ROM_REGION(0xc000, REGION_CPU1, 0)
ROM_LOAD("1390.cpu-u25", 0x0000, 0x0800, CRC(97010c3e) SHA1(b07db05abf48461b633bbabe359a973a5bc6da13))
ROM_LOAD("sw1.prom-u1", 0x0800, 0x0800, CRC(5350b8eb) SHA1(def9192971d1943e45cea1845b1d8c8e2a01bc38))
ROM_LOAD("sw2.prom-u2", 0x1000, 0x0800, CRC(44f45465) SHA1(e3139878602864509803dabc0f9c278e4b856431))
ROM_LOAD("sw3.prom-u3", 0x1800, 0x0800, CRC(5b692c3c) SHA1(6cd1361e9f063af1f175baed466cc2667b776a52))
ROM_LOAD("sw4.prom-u4", 0x2000, 0x0800, CRC(0b78dd00) SHA1(f48c4bdd5fc2e818107b036aa6eddebf46a0e964))
ROM_LOAD("sw5.prom-u5", 0x2800, 0x0800, CRC(8b3795f1) SHA1(1bcd12791e45dd14c7541e6fe3798a8159b6c11b))
ROM_LOAD("sw6.prom-u6", 0x3000, 0x0800, CRC(4304b503) SHA1(2bc7a702d43092818ecb713fa0bac476c272e3a0))
ROM_LOAD("sw7.prom-u7", 0x3800, 0x0800, CRC(3cb4a604) SHA1(868c3c1bead99c2e6857d1c2eef02d84e0e87f29))
ROM_LOAD("sw8.prom-u8", 0x4000, 0x0800, CRC(bdc55223) SHA1(47ca7485c9e2878cbcb92d93a022f7d74a6d13df))
ROM_LOAD("sw9.prom-u9", 0x4800, 0x0800, CRC(f6ca1bf1) SHA1(e4dc6bd6486dff2d0e8a93e5c7649093107cde46))
ROM_LOAD("swa.prom-u10", 0x5000, 0x0800, CRC(12373f7f) SHA1(685c1202345ae8ef53fa61b7254ce04efd94a12b))
ROM_LOAD("swb.prom-u11", 0x5800, 0x0800, CRC(d1effc6b) SHA1(b72cd14642f26ac50fbe6199d121b0278588ca22))
ROM_LOAD("swc.prom-u12", 0x6000, 0x0800, CRC(bf361ab3) SHA1(23e3396dc937c0a19d0d312d1de3443b43807d91))
ROM_LOAD("swd.prom-u13", 0x6800, 0x0800, CRC(ae2c88e5) SHA1(b0833051f543529502e05fb183effa9f817757fb))
ROM_LOAD("swe.prom-u14", 0x7000, 0x0800, CRC(ec4cc343) SHA1(00e107eaf530ce6bec2afffd7d7bedd7763cfb17))

ROM_REGION(0x0420, REGION_PROMS, 0)
ROM_LOAD("s-c.xyt-u39", 0x0000, 0x0400, CRC(56484d19) SHA1(61f43126fdcfc230638ed47085ae037a098e6781))   // sine table
ROM_LOAD("pr-82.cpu-u15", 0x0400, 0x0020, CRC(c609b79e) SHA1(49dbcbb607079a182d7eb396c0da097166ea91c9))   // CPU board addressing
ROM_END


// Zektor
AAE_DRIVER_BEGIN(drv_zektor, "zektor", "Zektor")
AAE_DRIVER_ROM(rom_zektor)
AAE_DRIVER_FUNCS(&init_zektor, &run_segag80, &end_segag80)
AAE_DRIVER_INPUT(input_ports_zektor)
AAE_DRIVER_SAMPLES(zektor_samples)
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		/*type*/     CPU_MZ80,
		/*freq*/     3000000,
		/*div*/      1,
		/*ipf*/      1,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   &sega_interrupt,
		/*r8*/       SegaRead,
		/*w8*/       SegaWrite,
		/*pr*/       G80SpinPortRead,
		/*pw*/       ZektorPortWrite,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(40, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 512, 1536, 552, 1464)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()

// Tac/Scan
AAE_DRIVER_BEGIN(drv_tacscan, "tacscan", "Tac/Scan")
AAE_DRIVER_ROM(rom_tacscan)
AAE_DRIVER_FUNCS(&init_tacscan, &run_tacscan, &end_segag80)
AAE_DRIVER_INPUT(input_ports_tacscan)
AAE_DRIVER_SAMPLES(tacscan_samples)
AAE_DRIVER_ART(tacscanart)

AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		/*type*/     CPU_MZ80,
		/*freq*/     3000000,
		/*div*/      1,
		/*ipf*/      1,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   &sega_interrupt,
		/*r8*/       SegaRead,
		/*w8*/       SegaWrite,
		/*pr*/       G80SpinPortRead,
		/*pw*/       TacScanPortWrite,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(40, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_ROTATE_270)
AAE_DRIVER_SCREEN(1024, 768, 512, 1536, 552, 1464)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()

// Star Trek
AAE_DRIVER_BEGIN(drv_startrek, "startrek", "Star Trek")
AAE_DRIVER_ROM(rom_startrek)
AAE_DRIVER_FUNCS(&init_startrek, &run_segag80, &end_segag80)
AAE_DRIVER_INPUT(input_ports_startrek)
AAE_DRIVER_SAMPLES(startrek_samples)
AAE_DRIVER_ART(startrekart)

AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		/*type*/     CPU_MZ80,
		/*freq*/     3000000,
		/*div*/      1,
		/*ipf*/      1,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   &sega_interrupt,
		/*r8*/       SegaRead,
		/*w8*/       SegaWrite,
		/*pr*/       G80SpinPortRead,
		/*pw*/       StarTrekPortWrite,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(40, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 512, 1536, 552, 1464)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()

// Space Fury (Revision C)
AAE_DRIVER_BEGIN(drv_spacfury, "spacfury", "Space Fury (Revision C)")
AAE_DRIVER_ROM(rom_spacfury)
AAE_DRIVER_FUNCS(&init_spacfury, &run_segag80, &end_segag80)
AAE_DRIVER_INPUT(input_ports_spacfury)
AAE_DRIVER_SAMPLES(spacfury_samples)
AAE_DRIVER_ART(spacfuryart)

AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		/*type*/     CPU_MZ80,
		/*freq*/     3000000,
		/*div*/      1,
		/*ipf*/      1,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   &sega_interrupt,
		/*r8*/       SegaRead,
		/*w8*/       SegaWrite,
		/*pr*/       SpacfuryPortRead,
		/*pw*/       SpacfuryPortWrite,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(40, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 512, 1536, 552, 1464)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()

// Space Fury (Revision A)
AAE_DRIVER_BEGIN(drv_spacfura, "spacfura", "Space Fury (Revision A)")
AAE_DRIVER_ROM(rom_spacfura)
AAE_DRIVER_FUNCS(&init_spacfury, &run_segag80, &end_segag80)
AAE_DRIVER_INPUT(input_ports_spacfury)
AAE_DRIVER_SAMPLES(spacfury_samples)
AAE_DRIVER_ART(spacfuryart)

AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		/*type*/     CPU_MZ80,
		/*freq*/     3000000,
		/*div*/      1,
		/*ipf*/      1,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   &sega_interrupt,
		/*r8*/       SegaRead,
		/*w8*/       SegaWrite,
		/*pr*/       SpacfuryPortRead,
		/*pw*/       SpacfuryPortWrite,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(40, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 512, 1536, 552, 1464)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()

// Space Fury (Revision B)
AAE_DRIVER_BEGIN(drv_spacfurb, "spacfurb", "Space Fury (Revision B)")
AAE_DRIVER_ROM(rom_spacfurb)
AAE_DRIVER_FUNCS(&init_spacfury, &run_segag80, &end_segag80)
AAE_DRIVER_INPUT(input_ports_spacfury)
AAE_DRIVER_SAMPLES(spacfury_samples)
AAE_DRIVER_ART(spacfuryart)

AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		/*type*/     CPU_MZ80,
		/*freq*/     3000000,
		/*div*/      1,
		/*ipf*/      1,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   &sega_interrupt,
		/*r8*/       SegaRead,
		/*w8*/       SegaWrite,
		/*pr*/       SpacfuryPortRead,
		/*pw*/       SpacfuryPortWrite,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(40, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 512, 1536, 552, 1464)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()

// Eliminator (2 Player Set 1)
AAE_DRIVER_BEGIN(drv_elim2, "elim2", "Eliminator (2 Player Set 1)")
AAE_DRIVER_ROM(rom_elim2)
AAE_DRIVER_FUNCS(&init_elim2, &run_segag80, &end_segag80)
AAE_DRIVER_INPUT(input_ports_elim2)
AAE_DRIVER_SAMPLES(elim_samples)
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		/*type*/     CPU_MZ80,
		/*freq*/     3000000,
		/*div*/      1,
		/*ipf*/      1,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   &sega_interrupt,
		/*r8*/       SegaRead,
		/*w8*/       SegaWrite,
		/*pr*/       Elim2PortRead,
		/*pw*/       ElimPortWrite,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(40, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 512, 1536, 552, 1464)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()

// Eliminator (2 Player Set 2A)
AAE_DRIVER_BEGIN(drv_elim2a, "elim2a", "Eliminator (2 Player Set 2A)")
AAE_DRIVER_ROM(rom_elim2a)
AAE_DRIVER_FUNCS(&init_elim2, &run_segag80, &end_segag80)
AAE_DRIVER_INPUT(input_ports_elim2)
AAE_DRIVER_SAMPLES(elim_samples)
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		/*type*/     CPU_MZ80,
		/*freq*/     3000000,
		/*div*/      1,
		/*ipf*/      1,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   &sega_interrupt,
		/*r8*/       SegaRead,
		/*w8*/       SegaWrite,
		/*pr*/       Elim2PortRead,
		/*pw*/       ElimPortWrite,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(40, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 512, 1536, 552, 1464)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()

// Eliminator (2 Player Set 2C)
AAE_DRIVER_BEGIN(drv_elim2c, "elim2c", "Eliminator (2 Player Set 2C)")
AAE_DRIVER_ROM(rom_elim2c)
AAE_DRIVER_FUNCS(&init_elim2, &run_segag80, &end_segag80)
AAE_DRIVER_INPUT(input_ports_elim2)
AAE_DRIVER_SAMPLES(elim_samples)
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		/*type*/     CPU_MZ80,
		/*freq*/     3000000,
		/*div*/      1,
		/*ipf*/      1,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   &sega_interrupt,
		/*r8*/       SegaRead,
		/*w8*/       SegaWrite,
		/*pr*/       Elim2PortRead,
		/*pw*/       ElimPortWrite,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(40, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 512, 1536, 552, 1464)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()

// Eliminator (4 Player)
AAE_DRIVER_BEGIN(drv_elim4, "elim4", "Eliminator (4 Player)")
AAE_DRIVER_ROM(rom_elim4)
AAE_DRIVER_FUNCS(&init_elim4, &run_segag80, &end_segag80)
AAE_DRIVER_INPUT(input_ports_elim4)
AAE_DRIVER_SAMPLES(elim_samples)
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		/*type*/     CPU_MZ80,
		/*freq*/     3000000,
		/*div*/      1,
		/*ipf*/      1,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   &sega_interrupt,
		/*r8*/       SegaRead,
		/*w8*/       SegaWrite,
		/*pr*/       Elim4PortRead,
		/*pw*/       ElimPortWrite,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(40, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 512, 1536, 552, 1464)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()

// Eliminator (4 Player Prototype)
AAE_DRIVER_BEGIN(drv_elim4p, "elim4p", "Eliminator (4 Player Prototype)")
AAE_DRIVER_ROM(rom_elim4p)
AAE_DRIVER_FUNCS(&init_elim4, &run_segag80, &end_segag80)
AAE_DRIVER_INPUT(input_ports_elim4)
AAE_DRIVER_SAMPLES(elim_samples)
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		/*type*/     CPU_MZ80,
		/*freq*/     3000000,
		/*div*/      1,
		/*ipf*/      1,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   &sega_interrupt,
		/*r8*/       SegaRead,
		/*w8*/       SegaWrite,
		/*pr*/       Elim4PortRead,
		/*pw*/       ElimPortWrite,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(40, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 512, 1536, 552, 1464)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_zektor)
AAE_REGISTER_DRIVER(drv_tacscan)
AAE_REGISTER_DRIVER(drv_startrek)
AAE_REGISTER_DRIVER(drv_spacfury)
AAE_REGISTER_DRIVER(drv_spacfura)
AAE_REGISTER_DRIVER(drv_spacfurb)
AAE_REGISTER_DRIVER(drv_elim2)
AAE_REGISTER_DRIVER(drv_elim2a)
AAE_REGISTER_DRIVER(drv_elim2c)
AAE_REGISTER_DRIVER(drv_elim4)
AAE_REGISTER_DRIVER(drv_elim4p)