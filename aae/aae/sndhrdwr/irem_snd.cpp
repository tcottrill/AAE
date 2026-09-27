//==========================================================================
// Irem sound board (M52 sound-C class) - AAE port of MAME 0.57
// sndhrdw/irem.c. See irem_snd.h for the wiring summary.
//
// THE CODE IS DERIVED FROM MAME and COPYRIGHT the MAME TEAM.
//==========================================================================

#include "irem_snd.h"
#include "aae_mame_driver.h"
#include "cpu_control.h"
#include "cpu_m6800.h"
#include "sound_latch.h"
#include "ay8910.h"
#include "msm5205.h"
#include "sys_log.h"

// ---------------------------------------------------------------------------
// Main-CPU side: sound command
// ---------------------------------------------------------------------------
WRITE_HANDLER_PUBLIC(irem_sound_cmd_w)
{
	if ((data & 0x80) == 0)
		soundlatch_set(0, data & 0x7f);
	else
		cpu_do_int_imm(CPU1, INT_TYPE_INT);   // 6803 IRQ1
}

// ---------------------------------------------------------------------------
// 6803 port glue: port 1 is the AY data bus, port 2 carries the strobes.
//   port2 bit 0: write latch (falling edge commits port1 to the selected AY)
//   port2 bit 2: 1 = control (address) write, 0 = data write
//   port2 bit 3: select PSG 0
//   port2 bit 4: select PSG 1
// ---------------------------------------------------------------------------
static UINT8 irem_port1 = 0;
static UINT8 irem_port2 = 0;

static uint8_t irem_m6803_port_in(int port)
{
	if (port == 1)
	{
		// reading the AY data bus
		if (irem_port2 & 0x08) return ay8910_read(0);
		if (irem_port2 & 0x10) return ay8910_read(1);
		return 0xff;
	}
	return 0;   // port 2 reads as 0 (matches MAME's irem_port2_r)
}

static void irem_m6803_port_out(int port, uint8_t data)
{
	if (port == 1)
	{
		irem_port1 = data;
		return;
	}

	// port 2: commit on the falling edge of bit 0
	if ((irem_port2 & 0x01) && !(data & 0x01))
	{
		if (irem_port2 & 0x04)
		{
			// control (register address) port
			if (irem_port2 & 0x08) ay8910_write(0, 0, irem_port1);
			if (irem_port2 & 0x10) ay8910_write(1, 0, irem_port1);
		}
		else
		{
			// data port
			if (irem_port2 & 0x08) ay8910_write(0, 1, irem_port1);
			if (irem_port2 & 0x10) ay8910_write(1, 1, irem_port1);
		}
	}
	irem_port2 = data;
}

void irem_sound_post_cpu_init(int cpunum)
{
	if (m_cpu_6800[cpunum])
		m_cpu_6800[cpunum]->set_m6803_ports(irem_m6803_port_in, irem_m6803_port_out);
}

// ---------------------------------------------------------------------------
// AY-8910 callbacks
// ---------------------------------------------------------------------------
static uint8_t irem_soundlatch_read(void)
{
	return soundlatch_get(0);
}

// AY #0 port B write: bits 2-4 select the MSM5205 clock and 3b/4b playback
// mode, bits 0-1 reset the two chips.
static void irem_msm5205_ctrl_w(uint8_t data)
{
	MSM5205_playmode_w(0, (data >> 2) & 7);
	MSM5205_playmode_w(1, ((data >> 2) & 4) | 3);   // #1 always in slave mode

	MSM5205_reset_w(0, data & 1);
	MSM5205_reset_w(1, data & 2);
}

// AY #1 port A write: the "analog" effect triggers (unemulated in MAME 0.57
// too - it just logged them in debug builds).
static void irem_analog_w(uint8_t data)
{
	(void)data;
}

// ---------------------------------------------------------------------------
// MSM5205 interrupt: chip 0's VCLK edge NMIs the 6803 and clocks chip 1.
// ---------------------------------------------------------------------------
static void irem_adpcm_int(int data)
{
	(void)data;
	cpu_do_int_imm(CPU1, INT_TYPE_NMI);

	// the first MSM5205 clocks the second
	MSM5205_vclk_w(1, 1);
	MSM5205_vclk_w(1, 0);
}

// ---------------------------------------------------------------------------
// Sound CPU (CPU1) memory maps. The 6803 core handles $0000-$001F (on-chip
// registers) internally; $0080-$00FF internal RAM and the $4000-$FFFF ROM
// fall through to the flat region.
// ---------------------------------------------------------------------------
WRITE_HANDLER(irem_adpcm_data_w)
{
	// address is the offset within 0x0801-0x0802: 0 = chip 0, 1 = chip 1
	MSM5205_data_w(address, data);
}

MEM_READ(IremSoundRead)
MEM_END

MEM_WRITE(IremSoundWrite)
MEM_ADDR(0x0800, 0x0800, MWA_NOP)             // IACK
MEM_ADDR(0x0801, 0x0802, irem_adpcm_data_w)
MEM_ADDR(0x9000, 0x9000, MWA_NOP)             // IACK
MEM_ADDR(0x4000, 0xffff, MWA_ROM)
MEM_END

// ---------------------------------------------------------------------------
// Chip configuration + lifecycle
// ---------------------------------------------------------------------------
static const AY8910Config irem_ay8910_cfg =
{
	2,                       // 2 chips
	3579545 / 4,             // 894886 Hz
	{ 60, 60 },              // mixing levels
	{ irem_soundlatch_read, nullptr },   // port A reads
	{ nullptr, nullptr },                // port B reads
	{ nullptr, irem_analog_w },          // port A writes
	{ irem_msm5205_ctrl_w, nullptr },    // port B writes
};

static const struct MSM5205interface irem_msm5205_intf =
{
	2,                       // 2 chips
	384000,                  // 384 kHz
	{ irem_adpcm_int, nullptr },         // VCLK interrupt (chip 0 only)
	{ MSM5205_S96_4B, MSM5205_SEX_4B },  // default 4 kHz; changed at run time
	{ 100, 100 },
};

int irem_sound_start(void)
{
	irem_port1 = 0;
	irem_port2 = 0;

	if (ay8910_sh_start(&irem_ay8910_cfg) != 0)
		return 1;
	if (MSM5205_sh_start(&irem_msm5205_intf) != 0)
		return 1;

	LOG_INFO("Irem sound board started (M6803 + 2xAY8910 + 2xMSM5205)");
	return 0;
}

void irem_sound_update(void)
{
	ay8910_sh_update();
	MSM5205_sh_update();
}

void irem_sound_stop(void)
{
	MSM5205_sh_stop();
	ay8910_sh_stop();
}
