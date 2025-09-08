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

#include "quantum.h"
#include "./68000/m68k.h"
#include "aae_mame_driver.h"
#include "driver_registry.h"
#include "cpu_control.h"
#include "aae_avg.h"
#include "earom.h"
#include "aae_pokey.h"
#include "math.h"
#include "timer.h"

#pragma warning(disable : 4838)

/*
QUANTUM MEMORY MAP (per schem):

000000-003FFF	ROM0
004000-004FFF	ROM1
008000-00BFFF	ROM2
00C000-00FFFF	ROM3
010000-013FFF	ROM4

018000-01BFFF	RAM0
01C000-01CFFF	RAM1

940000			TRACKBALL
948000			SWITCHES
950000			COLORRAM
958000			CONTROL (LED and coin control)
960000-970000	RECALL (nvram read)
968000			VGRST (vector reset)
970000			VGGO (vector go)
978000			WDCLR (watchdog)
900000			NVRAM (nvram write)
840000			I/OS (sound and dip switches)
800000-801FFF	VMEM (vector display list)
940000			I/O (shematic label really - covered above)
900000			DTACK1
*/

//#define BYTESWAP(x) ((((uint16_t)(x))>>8) | (((uint16_t)(x))<<8))

ART_START(quantumart)
ART_LOAD("custom.zip", "vert_mask.png", ART_TEX, 2)
ART_END

unsigned char program_rom[0x14000];
unsigned char main_ram[0x5000];
unsigned char nv_ram[0x200];

void  quantum_interrupt()
{
	cpu_do_int_imm(CPU0, INT_TYPE_68K1);
}

void quantum_nvram_handler(void* file, int read_or_write)
{
	if (read_or_write)
	{
		osd_fwrite(file, nv_ram, 0x200);
	}
	else
	{
		if (file)
		{
			osd_fread(file, nv_ram, 0x200);
		}
		else
		{
			memset(nv_ram, 0, 0x200);
		}
	}
}

READ16_HANDLER(quantum_trackball_r)
{
	return (readinputportbytag("IN2") << 4) | readinputportbytag("IN3");
}

READ16_HANDLER(quantum_switches_r)
{
	return (input_port_0_r(0) | (avg_check() ? 1 : 0));
}

static int quantum_input_1_r(int offset)
{
	return (readinputportbytag("DSW0") << (7 - (offset - POT0_C))) & 0x80;
}
static int quantum_input_2_r(int offset)
{
	return (readinputportbytag("DSW1") << (7 - (offset - POT0_C))) & 0x80;
}

WRITE16_HANDLER(quantum_led_write)
{
	if (data & 0xff)
	{
		/* bits 0 and 1 are coin counters */
		//coin_counter_w(0, data & 2);
		//coin_counter_w(1, data & 1);
		/* bit 3 = select second trackball for cocktail mode? */

		/* bits 4 and 5 are LED controls */
		set_led_status(0, data & 0x10);
		set_led_status(1, data & 0x20);
		/* bits 6 and 7 flip screen */
		//vector_set_flip_x (data & 0x40);
		//vector_set_flip_y (data & 0x80);
		//vector_set_swap_xy(1);	/* vertical game */
	}
}

READ16_HANDLER(MRA_NOP16)
{
	//LOG_INFO("--------------------Unhandled Read, %x data: %x", address);
	return 0;
}

READ16_HANDLER(quantum_snd_read)
{
	if (address & 0x20)
		return pokey2_r((address >> 1) % 0x10);
	else
		return pokey1_r((address >> 1) % 0x10);
}

WRITE16_HANDLER(quantum_snd_write)
{
	unsigned int data1;
	unsigned int data2;

	data1 = (data & 0xff00) >> 8;
	data2 = data & 0x00ff;

	if (address & 0x1) {
		pokey1_w((address >> 1) & 0xf, data1 & 0xff);
	}
	else {
		pokey2_w((address >> 1) & 0xf, data2 & 0xff);
	}
}

static struct POKEYinterface pokey_interface =
{
	2,			/* 2 chips */
	600000,
	200,	/* volume */
	6, //POKEY_DEFAULT_GAIN/2
	NO_CLIP,
	/* The 8 pot handlers */
	/* The 8 pot handlers */
	{ quantum_input_1_r,quantum_input_2_r },
	{ quantum_input_1_r,quantum_input_2_r },
	{ quantum_input_1_r,quantum_input_2_r },
	{ quantum_input_1_r,quantum_input_2_r },
	{ quantum_input_1_r,quantum_input_2_r },
	{ quantum_input_1_r,quantum_input_2_r },
	{ quantum_input_1_r,quantum_input_2_r },
	{ quantum_input_1_r,quantum_input_2_r },
	/* The allpot handler */
	{ 0, 0 },
};

MEM_READ(QuantumReadByte)
MEM_ADDR8(0x000000, 0x013fff, NULL, program_rom)
MEM_ADDR8(0x018000, 0x01cfff, NULL, main_ram)
MEM_END

MEM_WRITE(QuantumWriteByte)
MEM_ADDR8(0x000000, 0x013fff, MWA_ROM, NULL)
MEM_ADDR8(0x018000, 0x01cfff, NULL, main_ram)
MEM_END

MEM_READ16(QuantumReadWord)
MEM_ADDR16(0x000000, 0x013fff, NULL, program_rom)
MEM_ADDR16(0x018000, 0x01cfff, NULL, main_ram)
MEM_ADDR16(0x800000, 0x801fff, NULL, vec_ram)
MEM_ADDR16(0x840000, 0x84003f, quantum_snd_read, NULL)
MEM_ADDR16(0x900000, 0x9001ff, NULL, nv_ram)
MEM_ADDR16(0x940000, 0x940001, quantum_trackball_r, NULL)
MEM_ADDR16(0x948000, 0x948001, quantum_switches_r, NULL)
MEM_ADDR16(0x950000, 0x95001f, NULL, quantum_colorram)
MEM_ADDR16(0x960000, 0x9601ff, MRA_NOP16, NULL)
MEM_ADDR16(0x978000, 0x978001, MRA_NOP16, NULL)
MEM_END

MEM_WRITE16(QuantumWriteWord)
MEM_ADDR16(0x000000, 0x013fff, MWA_ROM16, NULL)
MEM_ADDR16(0x018000, 0x01cfff, NULL, main_ram)
MEM_ADDR16(0x800000, 0x801fff, NULL, vec_ram)
MEM_ADDR16(0x840000, 0x84003f, quantum_snd_write, NULL)
MEM_ADDR16(0x900000, 0x9001ff, NULL, nv_ram)
MEM_ADDR16(0x950000, 0x95001f, NULL, quantum_colorram)
MEM_ADDR16(0x958000, 0x958001, quantum_led_write, NULL)
MEM_ADDR16(0x960000, 0x960001, MWA_NOP16, NULL)
MEM_ADDR16(0x968000, 0x968001, avgdvg_reset_word_w, NULL)
MEM_ADDR16(0x978000, 0x978001, avgdvg_go_word_w, NULL) // service mode quirk
MEM_END

void run_quantum()
{
	watchdog_reset_w(0, 0, 0);
	pokey_sh_update();
}

int init_quantum()
{
	LOG_INFO("Starting Quantum Init");
	memset(main_ram, 0x00, 0x4fff);
	memset(vec_ram, 0x00, 0x1fff);
	memset(program_rom, 0x00, 0x13fff);
	memset(nv_ram, 0x00, 0x200);

	memcpy(program_rom, Machine->memory_region[CPU0], 0x14000);
	byteswap(program_rom, 0x14000);

	init68k(QuantumReadByte, QuantumWriteByte, QuantumReadWord, QuantumWriteWord, CPU0);
	avg_start_quantum();

	//timer_set(TIME_IN_HZ(246), 0, quantum_interrupt);
	pokey_sh_start(&pokey_interface);
	LOG_INFO("End Quantum Init");
	return 0;
}

void end_quantum()
{
	pokey_sh_stop();
}

INPUT_PORTS_START(quantum)
PORT_START("IN0")	/* IN0 */
/* YHALT here MUST BE ALWAYS 0  */
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_UNKNOWN)	/* vg YHALT */
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_COIN3)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_START1)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_START2)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_COIN2)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BITX(0x80, 0x80, IPT_DIPSWITCH_NAME | IPF_TOGGLE, DEF_STR(Service_Mode), OSD_KEY_F2, IP_JOY_NONE)
PORT_DIPSETTING(0x80, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))

/* first POKEY is SW2, second is SW1 -- more confusion! */
PORT_START("DSW0") /* DSW0 */
PORT_DIPNAME(0xc0, 0x00, DEF_STR(Coinage))
PORT_DIPSETTING(0x80, DEF_STR(2C_1C))
PORT_DIPSETTING(0x00, DEF_STR(1C_1C))
PORT_DIPSETTING(0xc0, DEF_STR(1C_2C))
PORT_DIPSETTING(0x40, DEF_STR(Free_Play))
PORT_DIPNAME(0x30, 0x00, "Right Coin")
PORT_DIPSETTING(0x00, "*1")
PORT_DIPSETTING(0x20, "*4")
PORT_DIPSETTING(0x10, "*5")
PORT_DIPSETTING(0x30, "*6")
PORT_DIPNAME(0x08, 0x00, "Left Coin")
PORT_DIPSETTING(0x00, "*1")
PORT_DIPSETTING(0x08, "*2")
PORT_DIPNAME(0x07, 0x00, "Bonus Coins")
PORT_DIPSETTING(0x00, "None")
PORT_DIPSETTING(0x01, "1 each 5")
PORT_DIPSETTING(0x02, "1 each 4")
PORT_DIPSETTING(0x05, "1 each 3")
PORT_DIPSETTING(0x06, "2 each 4")
PORT_START("DSW1") /* DSW1 */
PORT_BIT(0xff, IP_ACTIVE_HIGH, IPT_UNKNOWN)
/*
PORT_START("IN2")     // IN2
PORT_ANALOG(0x0f, 0, IPT_TRACKBALL_Y | IPF_REVERSE, 20, 10, 7, 0, 0)
PORT_START("IN3")      // IN3
PORT_ANALOG(0x0f, 0, IPT_TRACKBALL_X, 20, 10, 7, 0, 0)
INPUT_PORTS_END
*/

PORT_START("IN2")     // IN2
PORT_ANALOG(0x0f, 0, IPT_TRACKBALL_X | IPF_REVERSE, 20, 10, 7, 0, 0)
PORT_START("IN3")      // IN3
PORT_ANALOG(0x0f, 0, IPT_TRACKBALL_Y, 20, 10, 7, 0, 0)
INPUT_PORTS_END

ROM_START(quantum)
ROM_REGION(0x14000, REGION_CPU1, 0)
ROM_LOAD16_BYTE("136016-201.2e", 0x000000, 0x002000, CRC(7e7be63a) SHA1(11b2d0168cdbaa7a48656b77abc0bcbe9408fe84))
ROM_LOAD16_BYTE("136016-206.3e", 0x000001, 0x002000, CRC(2d8f5759) SHA1(54b0388ef44b5d34e621b48b465566aa16887e8f))
ROM_LOAD16_BYTE("136016-102.2f", 0x004000, 0x002000, CRC(408d34f4) SHA1(9a30debd1240b9c103134701943c94d6b48b926d))
ROM_LOAD16_BYTE("136016-107.3f", 0x004001, 0x002000, CRC(63154484) SHA1(c098cdbc339c9ea291c4c4fb203c60b3284e894a))
ROM_LOAD16_BYTE("136016-203.2hj", 0x008000, 0x002000, CRC(bdc52fad) SHA1(c8ede54a4f7f555adffa5b4bfea6bf646a0d02d4))
ROM_LOAD16_BYTE("136016-208.3hj", 0x008001, 0x002000, CRC(dab4066b) SHA1(dbb82df8e6de4e0f9f6e7ddd5f07618864fce8f9))
ROM_LOAD16_BYTE("136016-104.2k", 0x00C000, 0x002000, CRC(bf271e5c) SHA1(012edb947f1437932b9283e49d025a7794c45669))
ROM_LOAD16_BYTE("136016-109.3k", 0x00C001, 0x002000, CRC(d2894424) SHA1(5390025136b677b66d948c8cf6ea5e20203a4bae))
ROM_LOAD16_BYTE("136016-105.2l", 0x010000, 0x002000, CRC(13ec512c) SHA1(22a0395135b83ba47eacb5129f34fc97aa1b70a1))
ROM_LOAD16_BYTE("136016-110.3l", 0x010001, 0x002000, CRC(acb50363) SHA1(9efa9ca88efdd2d5e212bd537903892b67b4fe53))
/* AVG PROM */
ROM_REGION(0x100, REGION_PROMS, 0)
ROM_LOAD("136002-125.6h", 0x0000, 0x0100, CRC(5903af03) SHA1(24bc0366f394ad0ec486919212e38be0f08d0239))
ROM_END

ROM_START(quantum1)
ROM_REGION(0x14000, REGION_CPU1, 0)
ROM_LOAD16_BYTE("136016-101.2e", 0x000000, 0x002000, CRC(5af0bd5b) SHA1(f6e46fbebbf52294e78ae240fe2628c6b29b8dea))
ROM_LOAD16_BYTE("136016-106.3e", 0x000001, 0x002000, CRC(f9724666) SHA1(1bb073135029c92bef9afc9ccd910e0ab3302c8a))
ROM_LOAD16_BYTE("136016-102.2f", 0x004000, 0x002000, CRC(408d34f4) SHA1(9a30debd1240b9c103134701943c94d6b48b926d))
ROM_LOAD16_BYTE("136016-107.3f", 0x004001, 0x002000, CRC(63154484) SHA1(c098cdbc339c9ea291c4c4fb203c60b3284e894a))
ROM_LOAD16_BYTE("136016-103.2hj", 0x008000, 0x002000, CRC(948f228b) SHA1(878ac96173a793997cc88be469ec1ccdf833a7e8))
ROM_LOAD16_BYTE("136016-108.3hj", 0x008001, 0x002000, CRC(e4c48e4e) SHA1(caaf9d20741fcb961d590b634250a44a166cc33a))
ROM_LOAD16_BYTE("136016-104.2k", 0x00C000, 0x002000, CRC(bf271e5c) SHA1(012edb947f1437932b9283e49d025a7794c45669))
ROM_LOAD16_BYTE("136016-109.3k", 0x00C001, 0x002000, CRC(d2894424) SHA1(5390025136b677b66d948c8cf6ea5e20203a4bae))
ROM_LOAD16_BYTE("136016-105.2l", 0x010000, 0x002000, CRC(13ec512c) SHA1(22a0395135b83ba47eacb5129f34fc97aa1b70a1))
ROM_LOAD16_BYTE("136016-110.3l", 0x010001, 0x002000, CRC(acb50363) SHA1(9efa9ca88efdd2d5e212bd537903892b67b4fe53))
/* AVG PROM */
ROM_REGION(0x100, REGION_PROMS, 0)
ROM_LOAD("136002-125.6h", 0x0000, 0x0100, CRC(5903af03) SHA1(24bc0366f394ad0ec486919212e38be0f08d0239))
ROM_END

ROM_START(quantump)
ROM_REGION(0x14000, REGION_CPU1, 0)
ROM_LOAD("quantump.2e", 0x0000, 0x2000, CRC(176d73d3) SHA1(b887ee50af5db6f6d43cc6ba57451173f996dedc))
ROM_LOAD("quantump.3e", 0x0001, 0x2000, CRC(12fc631f) SHA1(327a44da897199536f43e5f792cb4a18d9055ac4))
ROM_LOAD("quantump.2f", 0x4000, 0x2000, CRC(b64fab48) SHA1(d5a77a367d4f652261c381e6bdd55c2175ace857))
ROM_LOAD("quantump.3f", 0x4001, 0x2000, CRC(a52a9433) SHA1(33787adb04864efebb04483353bbc96c966ec607))
ROM_LOAD("quantump.2h", 0x8000, 0x2000, CRC(5b29cba3) SHA1(e83b68907bc397994ed51a39dfa241430a0adb0c))
ROM_LOAD("quantump.3h", 0x8001, 0x2000, CRC(c64fc03a) SHA1(ab6cd710d01bc85432cc52021f27fd8f2a5e3168))
ROM_LOAD("quantump.2k", 0xc000, 0x2000, CRC(854f9c09) SHA1(d908b8c7f6837e511004cbd45a8883c6c7b155dd))
ROM_LOAD("quantump.3k", 0xc001, 0x2000, CRC(1aac576c) SHA1(28bdb5fcbd8cccc657d6e00ace3c083c21015564))
ROM_LOAD("quantump.2l", 0x10000, 0x2000, CRC(1285b5e7) SHA1(0e01e361da2d9cf1fac1896f8f44c4c2e75a3061))
ROM_LOAD("quantump.3l", 0x10001, 0x2000, CRC(e19de844) SHA1(cb4f9d80807b26d6b95405b2d830799984667f54))
/* AVG PROM */
ROM_REGION(0x100, REGION_PROMS, 0)
ROM_LOAD("136002-125.6h", 0x0000, 0x0100, CRC(5903af03) SHA1(24bc0366f394ad0ec486919212e38be0f08d0239))
ROM_END

// Quantum (Revision 1)
AAE_DRIVER_BEGIN(drv_quantum1, "quantum1", "Quantum (Revision 1)")
AAE_DRIVER_ROM(rom_quantum1)
AAE_DRIVER_FUNCS(&init_quantum, &run_quantum, &end_quantum)
AAE_DRIVER_INPUT(input_ports_quantum)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART(quantumart)

AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		/*type*/     CPU_68000,
		/*freq*/     6048000,
		/*div*/      100,
		/*ipf*/      3,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   &quantum_interrupt,
		/*r8*/       QuantumReadByte,
		/*w8*/       QuantumWriteByte,
		/*pr*/       nullptr,
		/*pw*/       nullptr,
		/*r16*/      QuantumReadWord,
		/*w16*/      QuantumWriteWord
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(60, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_ROTATE_270)
AAE_DRIVER_SCREEN(1024, 768, 0, 900, 0, 620)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0x0, 0x2000)
AAE_DRIVER_NVRAM(quantum_nvram_handler)
AAE_DRIVER_END()

// Quantum (Revision 2)
AAE_DRIVER_BEGIN(drv_quantum, "quantum", "Quantum (Revision 2)")
AAE_DRIVER_ROM(rom_quantum)
AAE_DRIVER_FUNCS(&init_quantum, &run_quantum, &end_quantum)
AAE_DRIVER_INPUT(input_ports_quantum)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART(quantumart)

AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		/*type*/     CPU_68000,
		/*freq*/     6000000,
		/*div*/      100,
		/*ipf*/      3,
		/*int type*/ INT_TYPE_68K1,
		/*int cb*/   &quantum_interrupt,
		/*r8*/       QuantumReadByte,
		/*w8*/       QuantumWriteByte,
		/*pr*/       nullptr,
		/*pw*/       nullptr,
		/*r16*/      QuantumReadWord,
		/*w16*/      QuantumWriteWord
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(60, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_ROTATE_270)
AAE_DRIVER_SCREEN(1024, 768, 0, 900, 0, 620)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0x0, 0x2000)
AAE_DRIVER_NVRAM(quantum_nvram_handler)
AAE_DRIVER_END()

// Quantum (Prototype)
AAE_DRIVER_BEGIN(drv_quantump, "quantump", "Quantum (Prototype)")
AAE_DRIVER_ROM(rom_quantump)
AAE_DRIVER_FUNCS(&init_quantum, &run_quantum, &end_quantum)
AAE_DRIVER_INPUT(input_ports_quantum)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART(quantumart)

AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		/*type*/     CPU_68000,
		/*freq*/     6048000,
		/*div*/      100,
		/*ipf*/      3,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   &quantum_interrupt,
		/*r8*/       QuantumReadByte,
		/*w8*/       QuantumWriteByte,
		/*pr*/       nullptr,
		/*pw*/       nullptr,
		/*r16*/      QuantumReadWord,
		/*w16*/      QuantumWriteWord
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(60, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_ROTATE_270)
AAE_DRIVER_SCREEN(1024, 768, 0, 900, 0, 620)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0x0, 0x2000)
AAE_DRIVER_NVRAM(quantum_nvram_handler)
AAE_DRIVER_END()
AAE_REGISTER_DRIVER(drv_quantum1)
AAE_REGISTER_DRIVER(drv_quantum)
AAE_REGISTER_DRIVER(drv_quantump)