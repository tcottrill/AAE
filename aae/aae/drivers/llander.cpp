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

/* Lunar Lander Emu */

#include "aae_mame_driver.h"
#include "driver_registry.h"    // AAE_REGISTER_DRIVER
#include "llander.h"
#include "old_mame_vecsim_dvg.h"

static int ldvggo = 0;
static int thrust = 0;
static int pic = 0;
static int lamp0 = 0;
static int lastlamp0;
static int a, b, c;

static const char* llander_samples[] = {
	"llander.zip",
	"lthrust.wav",
	"beep.wav",
	"lexplode.wav",
	"lander6k.wav",
	 0 };

void llander_interrupt()
{
	// Turn off interrupts if self-test is enabled
	if (readinputport(0) & 0x02)
	{
		LOG_INFO("LANDER INT CALLED");
		cpu_do_int_imm(CPU0, INT_TYPE_NMI);
	}
}

READ_HANDLER(llander_zeropage_r)
{
	return Machine->memory_region[CPU0][address];
}

READ_HANDLER(llander_DSW1_r)
{
	int res;
	int res1;

	res1 = readinputportbytag("DSW1");

	res = 0xfc | ((res1 >> (2 * (3 - (address & 0x3)))) & 0x3);
	return res;
}

WRITE_HANDLER(llander_zeropage_w)
{
	Machine->memory_region[CPU0][address] = data;
	Machine->memory_region[CPU0][0x0100 + address] = data;
}

READ_HANDLER(llander_IN0_r)
{
	int val = readinputportbytag("IN0");

	if (dvg_done())
		val |= 0x01;
	if (get_eterna_ticks(0) & 0x100)
		val |= 0x40;

	return val;
}

READ_HANDLER(llander_IN1_r)
{
	int res;
	int bitmask;

	res = readinputportbytag("IN1");

	bitmask = (1 << address);

	if (res & bitmask)
		res = 0x80;
	else
		res = ~0x80;
	return (res);
}

WRITE_HANDLER(llander_snd_reset_w)
{
	;
}

WRITE_HANDLER(llander_sounds_w)
{
	int tvol;
	//volume    = data & 0x07;
	//tone_3khz = data & 0x10;
	//tone_6khz = data & 0x20; //Only used in Selftest
	//llander_explosion = data & 0x08;

	if (data == 0) { sample_set_volume(1, 2); }
	if (data == 1) { sample_set_volume(1, 10); }
	if (data & 0x07)
	{
		tvol = Machine->memory_region[CPU0][0x01] << 4;
		if (tvol < 10) { tvol = 10; }

		sample_set_volume(1, tvol);
		//LOG_INFO("tvol here %d", tvol);
	}

	if (data & 0x10)
	{
		if (sample_playing(3) == 0)
		{
			sample_start(3, 1, 0);
		}
	}

	if (data & 0x20)
	{
		if (sample_playing(4) == 0)
		{
			sample_start(4, 3, 0);
		}
	}
	if (data & 0x08)
	{
		if (sample_playing(2) == 0)
		{
			sample_start(2, 2, 0);
		}
	}
}

///////////////////  /READ KEYS FROM PIA 1 //////////////////////////////////////
/* Lunar lander LED port seems to be mapped thus:

   NNxxxxxx - Apparently unused
   xxNxxxxx - Unknown gives 4 high pulses of variable duration when coin put in ?
   xxxNxxxx - Start    Lamp ON/OFF == 0/1
   xxxxNxxx - Training Lamp ON/OFF == 1/0
   xxxxxNxx - Cadet    Lamp ON/OFF
   xxxxxxNx - Prime    Lamp ON/OFF
   xxxxxxxN - Command  Lamp ON/OFF

   Selection lamps seem to all be driver 50/50 on/off during attract mode ?

*/

///////////////////////////////////////////////////////////////////////////////
WRITE_HANDLER(ll_led_write)
{
	data = data & 0xff;
	lamp0 = ((data >> (4 - 0)) & 1);
	b += ((data >> (4 - 1)) & 1);
	c += ((data >> (4 - 2)) & 1);

	data = data & 0x0f;
	if (data == 0x0f) { pic = 1; }
	if (data == 0x08) { pic = 2; }
	if (data == 0x04) { pic = 3; }
	if (data == 0x02) { pic = 4; }
	if (data == 0x01) { pic = 5; }
}

MEM_WRITE(LlanderWrite)
MEM_ADDR(0x0000, 0x00ff, llander_zeropage_w)
MEM_ADDR(0x3000, 0x3000, dvg_go_w)
MEM_ADDR(0x3200, 0x3200, ll_led_write)
MEM_ADDR(0x3400, 0x3400, watchdog_reset_w)
MEM_ADDR(0x3c00, 0x3c00, llander_sounds_w)
MEM_ADDR(0x3e00, 0x3e00, llander_snd_reset_w)
MEM_ADDR(0x4000, 0x47ff, MWA_RAM)
MEM_ADDR(0x4800, 0x5fff, MWA_ROM)
MEM_ADDR(0x6000, 0x7fff, MWA_ROM)
MEM_END

MEM_READ(LlanderRead)
MEM_ADDR(0x0000, 0x00ff, llander_zeropage_r)
MEM_ADDR(0x2000, 0x2000, llander_IN0_r)
MEM_ADDR(0x2400, 0x2407, llander_IN1_r)
MEM_ADDR(0x2800, 0x2803, llander_DSW1_r)
MEM_ADDR(0x2c00, 0x2c00, ip_port_3_r)
MEM_ADDR(0x4000, 0x47ff, MRA_RAM)
MEM_ADDR(0x4800, 0x5fff, MRA_ROM) /* vector rom */
MEM_ADDR(0x6000, 0x7fff, MRA_ROM)
MEM_END

void run_llander()
{
	/*
		 if (config.artwork){
		 count++;b=0;c=0;
		 if (count==10){count=0;blink^=1;}

			blit_any_tex(&art_tex[0],6,1,0,0,1024,164); //bright //6
			if (b==6 && c==6) {
			 if (blink ){
				  blit_any_tex(&art_tex[3],1,1,0,0,1023,164);
				  blit_any_tex(&art_tex[4],1,1,0,0,1023,164);
				  blit_any_tex(&art_tex[5],1,1,0,0,1023,164);
				  blit_any_tex(&art_tex[6],1,1,0,0,1023,164);
					 }
							 }
			else {
				switch(pic){
					 case 1:blit_any_tex(&art_tex[2],1,1,0,0,1024,164);break;
					 case 2:blit_any_tex(&art_tex[3],1,1,0,0,1024,164);break;
					 case 3:blit_any_tex(&art_tex[4],1,1,0,0,1024,164);break;
					 case 4:blit_any_tex(&art_tex[5],1,1,0,0,1024,164);break;
					 case 5:blit_any_tex(&art_tex[6],1,1,0,0,1024,164);break;
					  }
				}
		 if (lamp0){blit_any_tex(&art_tex[2],1,1,0,0,1024,164);}
		blit_any_tex(&art_tex[1],15,.70,0,0,1024,164);//dim
		 }

		*/
	watchdog_reset_w(0, 0, 0);
}

/////////////////// MAIN() for program ///////////////////////////////////////////////////
int init_llander()
{
	//init6502(LlanderRead, LlanderWrite, 0x7fff, CPU0);

	/*
	if (config.artwork){
	make_single_bitmap( &art_tex[0],"lunaroff.png","llander.zip");
	make_single_bitmap( &art_tex[1],"lunaron.png","llander.zip");
	make_single_bitmap( &art_tex[2],"lunarins.png","llander.zip");
	make_single_bitmap( &art_tex[3],"lunartrn.png","llander.zip");
	make_single_bitmap( &art_tex[4],"lunarcad.png","llander.zip");
	make_single_bitmap( &art_tex[5],"lunarprm.png","llander.zip");
	make_single_bitmap( &art_tex[6],"lunarcom.png","llander.zip");
	}
	*/
	sample_start(1, 0, 1);
	sample_set_volume(1, 5);

	dvg_start();
	return 0;
}

void end_llander()
{
}


//Lunar Lander Input Ports
INPUT_PORTS_START(llander)
PORT_START("IN0") /* IN0 */
/* Bit 0 is VG_HALT, handled in the machine dependant part */
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_BITX(0x02, 0x02, IPT_DIPSWITCH_NAME | IPF_TOGGLE, DEF_STR(Service_Mode), OSD_KEY_F2, IP_JOY_NONE)
PORT_DIPSETTING(0x02, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_TILT)
/* Of the rest, Bit 6 is the 3KHz source. 3,4 and 5 are unknown */
PORT_BIT(0x78, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BITX(0x80, IP_ACTIVE_LOW, IPT_SERVICE, "Diagnostic Step", OSD_KEY_F1, IP_JOY_NONE)

PORT_START("IN1") /* IN1 */
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_START1)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_COIN2)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_COIN3)
PORT_BITX(0x10, IP_ACTIVE_HIGH, IPT_START2, "Select Game", IP_KEY_DEFAULT, IP_JOY_DEFAULT)
PORT_BITX(0x20, IP_ACTIVE_HIGH, IPT_BUTTON1, "Abort", IP_KEY_DEFAULT, IP_JOY_DEFAULT)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT | IPF_2WAY)
PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT | IPF_2WAY)

PORT_START("DSW1") /* DSW1 */
PORT_DIPNAME(0x03, 0x01, "Right Coin")
PORT_DIPSETTING(0x00, "*1")
PORT_DIPSETTING(0x01, "*4")
PORT_DIPSETTING(0x02, "*5")
PORT_DIPSETTING(0x03, "*6")
PORT_DIPNAME(0x0c, 0x00, "Language")
PORT_DIPSETTING(0x00, "English")
PORT_DIPSETTING(0x04, "French")
PORT_DIPSETTING(0x08, "Spanish")
PORT_DIPSETTING(0x0c, "German")
PORT_DIPNAME(0x20, 0x00, DEF_STR(Coinage))
PORT_DIPSETTING(0x00, "Normal")
PORT_DIPSETTING(0x20, DEF_STR(Free_Play))
PORT_DIPNAME(0xd0, 0x80, "Fuel units")
PORT_DIPSETTING(0x00, "450")
PORT_DIPSETTING(0x40, "600")
PORT_DIPSETTING(0x80, "750")
PORT_DIPSETTING(0xc0, "900")
PORT_DIPSETTING(0x10, "1100")
PORT_DIPSETTING(0x50, "1300")
PORT_DIPSETTING(0x90, "1550")
PORT_DIPSETTING(0xd0, "1800")

/* The next one is a potentiometer */
PORT_START("IN3")/* IN3 */
PORT_ANALOGX(0xff, 0x00, IPT_PADDLE | IPF_REVERSE, 100, 10, 0, 0, 255, OSD_KEY_UP, OSD_KEY_DOWN, OSD_JOY_UP, OSD_JOY_DOWN)
INPUT_PORTS_END

INPUT_PORTS_START(llander1)
PORT_START("IN0") /* IN0 */
/* Bit 0 is VG_HALT, handled in the machine dependant part */
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_BITX(0x02, 0x02, IPT_DIPSWITCH_NAME | IPF_TOGGLE, DEF_STR(Service_Mode), OSD_KEY_F2, IP_JOY_NONE)
PORT_DIPSETTING(0x02, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_TILT)
/* Of the rest, Bit 6 is the 3KHz source. 3,4 and 5 are unknown */
PORT_BIT(0x78, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BITX(0x80, IP_ACTIVE_LOW, IPT_SERVICE, "Diagnostic Step", OSD_KEY_F1, IP_JOY_NONE)

PORT_START("IN1") /* IN1 */
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_START1)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_COIN2)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_COIN3)
PORT_BITX(0x10, IP_ACTIVE_HIGH, IPT_START2, "Select Game", IP_KEY_DEFAULT, IP_JOY_DEFAULT)
PORT_BITX(0x20, IP_ACTIVE_HIGH, IPT_BUTTON1, "Abort", IP_KEY_DEFAULT, IP_JOY_DEFAULT)
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT | IPF_2WAY)
PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT | IPF_2WAY)

PORT_START("DSW1") /* DSW1 */
PORT_DIPNAME(0x03, 0x01, "Right Coin")
PORT_DIPSETTING(0x00, "*1")
PORT_DIPSETTING(0x01, "*4")
PORT_DIPSETTING(0x02, "*5")
PORT_DIPSETTING(0x03, "*6")
PORT_DIPNAME(0x0c, 0x00, "Language")
PORT_DIPSETTING(0x00, "English")
PORT_DIPSETTING(0x04, "French")
PORT_DIPSETTING(0x08, "Spanish")
PORT_DIPSETTING(0x0c, "German")
PORT_DIPNAME(0x10, 0x00, DEF_STR(Coinage))
PORT_DIPSETTING(0x00, "Normal")
PORT_DIPSETTING(0x10, DEF_STR(Free_Play))
PORT_DIPNAME(0xc0, 0x80, "Fuel units")
PORT_DIPSETTING(0x00, "450")
PORT_DIPSETTING(0x40, "600")
PORT_DIPSETTING(0x80, "750")
PORT_DIPSETTING(0xc0, "900")

/* The next one is a potentiometer */
PORT_START("IN3") /* IN3 */
PORT_ANALOGX(0xff, 0x00, IPT_PADDLE | IPF_REVERSE, 100, 10, 0, 0, 255, OSD_KEY_UP, OSD_KEY_DOWN, OSD_JOY_UP, OSD_JOY_DOWN)
INPUT_PORTS_END

// Lunar Lander ROMSETS
ROM_START(llander)
ROM_REGION(0x10000, REGION_CPU1, 0)
// Vector ROM
ROM_LOAD("034599-01.r3", 0x4800, 0x0800, CRC(355a9371) SHA1(6ecb40169b797d9eb623bcb17872f745b1bf20fa))
ROM_LOAD("034598-01.np3", 0x5000, 0x0800, CRC(9c4ffa68) SHA1(eb4ffc289d254f699f821df3146aa2c6cd78597f))
ROM_LOAD("034597-01.m3", 0x5800, 0x0800, CRC(ebb744f2) SHA1(e685b094c1261a351e4e82dfb487462163f136a4)) // Built from original Atari source code

ROM_LOAD("034572-02.f1", 0x6000, 0x0800, CRC(b8763eea) SHA1(5a15eaeaf825ccdf9ce013a6789cf51da20f785c))
ROM_LOAD("034571-02.de1", 0x6800, 0x0800, CRC(77da4b2f) SHA1(4be6cef5af38734d580cbfb7e4070fe7981ddfd6))
ROM_LOAD("034570-01.c1", 0x7000, 0x0800, CRC(2724e591) SHA1(ecf4430a0040c227c896aa2cd81ee03960b4d641))
ROM_LOAD("034569-02.b1", 0x7800, 0x0800, CRC(72837a4e) SHA1(9b21ba5e1518079c326ca6e15b9993e6c4483caa))
ROM_RELOAD(0xf800, 0x0800)	/* for reset/interrupt vectors */
// DVG PROM
ROM_REGION(0x100, REGION_PROMS, 0)
ROM_LOAD("034602-01.c8", 0x0000, 0x0100, CRC(97953db8) SHA1(8cbded64d1dd35b18c4d5cece00f77e7b2cab2ad))
ROM_END

ROM_START(llander1)
ROM_REGION(0x10000, REGION_CPU1, 0)
// Vector ROM
ROM_LOAD("034599-01.r3", 0x4800, 0x0800, CRC(355a9371) SHA1(6ecb40169b797d9eb623bcb17872f745b1bf20fa))
ROM_LOAD("034598-01.np3", 0x5000, 0x0800, CRC(9c4ffa68) SHA1(eb4ffc289d254f699f821df3146aa2c6cd78597f))
ROM_LOAD("034597-01.m3", 0x5800, 0x0800, CRC(ebb744f2) SHA1(e685b094c1261a351e4e82dfb487462163f136a4)) // Built from original Atari source code

ROM_LOAD("034572-01.f1", 0x6000, 0x0800, CRC(2aff3140) SHA1(4fc8aae640ce655417c11d9a3121aae9a1238e7c))
ROM_LOAD("034571-01.de1", 0x6800, 0x0800, CRC(493e24b7) SHA1(125a2c335338ccabababef12fd7096ef4b605a31))
ROM_LOAD("034570-01.c1", 0x7000, 0x0800, CRC(2724e591) SHA1(ecf4430a0040c227c896aa2cd81ee03960b4d641))
ROM_LOAD("034569-01.b1", 0x7800, 0x0800, CRC(b11a7d01) SHA1(8f2935dbe04ee68815d69ea9e71853b5a145d7c3))
ROM_RELOAD(0xf800, 0x0800)	/* for reset/interrupt vectors */
// DVG PROM
ROM_REGION(0x100, REGION_PROMS, 0)
ROM_LOAD("034602-01.c8", 0x0000, 0x0100, CRC(97953db8) SHA1(8cbded64d1dd35b18c4d5cece00f77e7b2cab2ad))
ROM_END

AAE_DRIVER_BEGIN(llander, "llander", "Lunar Lander")
AAE_DRIVER_ROM(rom_llander)
AAE_DRIVER_FUNCS(init_llander, run_llander, end_llander)
AAE_DRIVER_INPUT(input_ports_llander)
AAE_DRIVER_SAMPLES(llander_samples)
AAE_DRIVER_ART(nullptr)
AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(CPU_M6502, 1512000, 100, 6, INT_TYPE_NMI, llander_interrupt,
		LlanderRead, LlanderWrite, nullptr, nullptr, nullptr, nullptr),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)
// Vector video
AAE_DRIVER_VIDEO_CORE(40, VIDEO_TYPE_VECTOR | VECTOR_USES_BW, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 1044, -80, 780)
// Vector game => no raster decode/palette conversion
AAE_DRIVER_RASTER_NONE()
// No hiscore yet
AAE_DRIVER_HISCORE_NONE()
// Vector RAM base/size (match your current values)
AAE_DRIVER_VECTORRAM(0x4000, 0x800)
// No NVRAM handler
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()


AAE_DRIVER_BEGIN(llander1, "llander1", "Lunar Lander (Revision 1)")
AAE_DRIVER_ROM(rom_llander1)
AAE_DRIVER_FUNCS(init_llander, run_llander, end_llander)
AAE_DRIVER_INPUT(input_ports_llander)
AAE_DRIVER_SAMPLES(llander_samples)
AAE_DRIVER_ART(nullptr)
AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(CPU_M6502, 1512000, 100, 6, INT_TYPE_NMI, llander_interrupt,
		LlanderRead, LlanderWrite, nullptr, nullptr, nullptr, nullptr),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)
// Vector video
AAE_DRIVER_VIDEO_CORE(40, VIDEO_TYPE_VECTOR | VECTOR_USES_BW, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 1044, -80, 780)
// Vector game => no raster decode/palette conversion
AAE_DRIVER_RASTER_NONE()
// No hiscore yet
AAE_DRIVER_HISCORE_NONE()
// Vector RAM base/size (match your current values)
AAE_DRIVER_VECTORRAM(0x4000, 0x800)
// No NVRAM handler
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(llander)
AAE_REGISTER_DRIVER(llander1)
//////////////////  END OF MAIN PROGRAM /////////////////////////////////////////////