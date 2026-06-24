// ---------------------------------------------------------------------------
// vertigo.cpp - Exidy Vertigo (Top Gunner) driver for AAE
//
// Exidy, 1986. Original MAME driver by Mathis Rosenhauer; adapted for AAE.
//
// Hardware:
//   Main CPU:  MC68000 @ 8 MHz
//   Vector:    custom 4 x AM2901 bit-slice vector processor (vertigo_video.cpp)
//   Timer:     Intel 8254 PIT (machine/pit8253) - ch0 -> IRQ4, ch1 -> IRQ3
//   IRQ:       74148 priority encoder feeding the 68000 IPL pins
//   Sound:     Exidy 440 audio board (6809) - STUBBED in this first pass
//   Motor:     MC6805 motor controller - not emulated (as in MAME)
//
// First-pass scope: 68000 + vector processor + PIT + 74148 IRQ logic + I/O +
// NVRAM-as-RAM. Sound and the motor MCU are stubbed. Analog inputs (stick/
// paddle) are stubbed to centre via the ADC; digital inputs (GIO/COIN) are
// wired. The Vertigo ROM set uses MAME ROMX_LOAD/ROM_COPY/ROM_REGION*_BE macros
// that AAE does not have, so the microcode is assembled by hand in
// vertigo_vproc_init() and the program/vector ROMs are laid out compactly here.
// ---------------------------------------------------------------------------

#ifndef VERTIGO_CPP
#define VERTIGO_CPP

#include "vertigo.h"
#include "aae_mame_driver.h"
#include "driver_registry.h"
#include "cpu_control.h"
#include "memory.h"
#include "timer.h"
#include "inptport.h"
#include "pit8253.h"
#include "74148.h"
#include "cpu_m6809.h"          // sound CPU (CPU1): firq_line / exec
#include "exidy440_sound.h"     // Exidy 440 sound board
#include <cstring>

// ===========================================================================
// RAM / ROM buffers (68000 address space)
// ===========================================================================
static unsigned char vertigo_prog_rom[0x20000];  // 0x800000-0x81ffff program + vector data
static unsigned char vertigo_ram[0x2000];        // 0x000000-0x001fff work RAM (reset vector seeded low)
static unsigned char vertigo_vram[0x2000];       // 0x002000-0x003fff vector RAM (shared with vproc)
static unsigned char vertigo_nvram[0x400];       // 0x007000-0x0073ff NVRAM (plain RAM, no save-state)

// Sound 6809 (CPU1) address space: program ROM at 0xe000-0xffff (from REGION_CPU2)
// and work RAM at 0xa000-0xa7ff. I/O (0x8000-0x9800) goes through handlers.
static unsigned char vertigo_snd_rom[0x2000];    // 0xe000-0xffff  sound program
static unsigned char vertigo_snd_ram[0x800];     // 0xa000-0xa7ff  sound work RAM

// ===========================================================================
// IRQ priority encoder state (74148 -> 68000 IPL)
//
// The 74148 input index for a given 68000 IRQ level IS that level: input n is
// pulled active, the encoder outputs (7 - n), and update_irq() recovers the
// level as (output ^ 7) == n. Higher level = higher-numbered input = higher
// priority, matching the 68000 (IRQ6 > IRQ4 > IRQ3 > IRQ2).
// ===========================================================================
#define ENC_IRQ2 2   // 74148 input 2 -> 68000 IRQ level 2
#define ENC_IRQ3 3   // 74148 input 3 -> 68000 IRQ level 3
#define ENC_IRQ4 4   // 74148 input 4 -> 68000 IRQ level 4
#define ENC_IRQ6 6   // 74148 input 6 -> 68000 IRQ level 6

static double irq4_time;     // emulated time of the last INTL4 change (seconds)
static UINT8  irq_state;     // last 74148 encoded output
static UINT8  adc_result;    // last ADC channel sampled
static int    vertigo_clock = -1;  // free-running monotonic seconds source

// --- debug instrumentation (per-frame pipeline trace; see run_vertigo) -------
static unsigned dbg_irq4_calls   = 0;   // PIT ch0 -> v_irq4_w edges this frame
static unsigned dbg_irq4_rising  = 0;   // ...of which asserted IRQ4 (level != 0)
static unsigned dbg_vproc_cycles = 0;   // total 2901 cycles handed to the vproc this frame

static double vertigo_now(void)
{
	return (vertigo_clock >= 0) ? timer_timeelapsed(vertigo_clock) : 0.0;
}

// 74148 output-change callback: drive the 68000 IPL to the encoded level.
static void update_irq(void)
{
	irq_state = (UINT8)TTL74148_output_r(0);
	int level = (irq_state < 7) ? (irq_state ^ 7) : 0;   // 0 = no interrupt
	if (m_cpu_68000[CPU0])
	{
		// The 74148 is the priority arbiter; its output already reflects the
		// single highest active source. cpu_m68000::irq_line() is ADDITIVE (a
		// pending-level bitmask that clears only on service or on irq_line(0)),
		// so assert *exactly* the current level: clear first, then set. Otherwise
		// a higher level that has since deasserted lingers in irq_pending and
		// fires as a phantom IRQ, wedging the CPU after the first frame.
		m_cpu_68000[CPU0]->irq_line(0);
		if (level > 0)
			m_cpu_68000[CPU0]->irq_line(level);
	}
}

static void update_irq_encoder(int line, int state)
{
	TTL74148_input_line_w(0, line, !state);
	TTL74148_update(0);
}

// ===========================================================================
// PIT 8254 output callbacks (ch0 -> INTL4 / vector CPU, ch1 -> INTL3 / sound)
// ===========================================================================
static void v_irq4_w(int level)
{
	update_irq_encoder(ENC_IRQ4, level);

	// DEBUG: trace each ch0 OUT transition + the resulting 68000 IRQ state, so we
	// can see a rising IRQ4 being cancelled by a falling edge before it's taken.
	if (m_cpu_68000[CPU0])
		LOG_INFO("VTG OUT4=%d irqp=%02X mask=%u PC=%06X", level,
			(unsigned)m_cpu_68000[CPU0]->irq_pending, (unsigned)m_cpu_68000[CPU0]->int_mask,
			(unsigned)m_cpu_68000[CPU0]->GetPC());

	// The vector CPU runs for the time elapsed since the last INTL4 change.
	double now = vertigo_now();
	int cyc = TIME_TO_CYCLES(0, now - irq4_time);
	// AAE's emulated clock advances per CPU time-slice, not per instruction. The INTL4
	// FALLING edge fires mid-instruction (the 68000 re-arms the PIT inside its IRQ4
	// handler), so 'now' is still the previous slice's value and the elapsed count reads
	// ~0 -- yet that is exactly the edge on which the microcode draws the frame, so it
	// would run zero cycles and draw nothing. Give it enough to finish the draw; the
	// microcode's own INTL4 idle-loop detection caps any surplus. (MAME doesn't need
	// this because its clock is cycle-exact.)
	if (cyc < 60000) cyc = 60000;
	vertigo_vproc(cyc, level);
	irq4_time = now;

	dbg_irq4_calls++;
	if (level)   dbg_irq4_rising++;
	if (cyc > 0) dbg_vproc_cycles += (unsigned)cyc;
}

static void v_irq3_w(int level)
{
	// PIT channel 1's output drives the main CPU's IRQ3 (via the 74148) and,
	// on a rising edge, the sound 6809's IRQ0 -- matching MAME's vertigo machine
	// glue (vertigo_machine.c). This is the real sound-timing source; the sound
	// ROM clears it via the 0x9800 write (exidy440_sound_interrupt_clear_w).
	if (level && m_cpu_6809[CPU1])
		m_cpu_6809[CPU1]->irq_line(true);
	update_irq_encoder(ENC_IRQ3, level);
}

// Sub-cycle clock source for the PIT. The power-on self-test polls a PIT
// counter (ch2, free-running, no IRQ) in a tight loop and checks the values
// vary - the hardware random source. AAE's timer clock only advances per CPU
// time-slice, so without this every read in that loop returns the same value
// ("random number generator failure"). Feeding the 68000's intra-slice progress
// lets the PIT counter tick within a slice. Only affects counter *reads*; the
// IRQ4/IRQ3 OUT scheduling is driven by timer_update and is unchanged.
static int vertigo_pit_subcycles(void)
{
	return m_cpu_68000[CPU0] ? m_cpu_68000[CPU0]->cycles_run_this_slice() : 0;
}

static struct pit8253_config pit8254_config =
{
	TYPE8254,
	{
		{ 240000, v_irq4_w, NULL },
		{ 240000, v_irq3_w, NULL },
		{ 240000, NULL,     NULL }
	}
};

static struct TTL74148_interface irq_encoder = { update_irq };

// ===========================================================================
// 68000 I/O block 0x004000-0x00407f (mirrored at 0x005000), decoded by group:
//   0 0x4000 io_convert   1 0x4010 io_adc    2 0x4020 coin   3 0x4030 GIO
//   4 0x4040 sio          5 0x4050 audio_w   6 0x4060 motor  7 0x4070 wsot
// ===========================================================================
static int vertigo_io_read(UINT32 addr)
{
	int group = (addr >> 4) & 0x7;
	int chan  = (addr >> 1) & 0x7;   // word offset within the 16-byte group

	switch (group)
	{
	case 0: // io_convert: start an ADC conversion on channel 'chan', raise IRQ2.
		// Channels 0/1/2 are stick X / stick Y / paddle = input ports IN0/IN1/IN2;
		// higher channels read 0. readinputport() returns the live analog value
		// (auto-updated each frame from the keys/stick). Matches MAME's
		// vertigo_io_convert: adc_result = readinputport(offset).
		adc_result = (chan < 3) ? (UINT8)readinputport(chan) : 0x00;
		update_irq_encoder(ENC_IRQ2, 1);
		return 0;

	case 1: // io_adc: return the converted value, clear IRQ2
		update_irq_encoder(ENC_IRQ2, 0);
		return adc_result;

	case 2: // coin: clear IRQ6, return the coin port
		update_irq_encoder(ENC_IRQ6, 0);
		return readinputportbytag("COIN");

	case 3: // GIO (service / start / buttons) - plain read, no IRQ (matches MAME's
		// input_port_3_word_r; IRQ2 belongs to the ADC convert/read pair only).
		return readinputportbytag("GIO");

	case 4: // sio: sound command handshake. 0xfc = the sound 6809 has read the
		// last command (ack), 0xfd = still pending. Driven by the real sound CPU.
		return exidy440_sound_command_ack_r() ? 0xfc : 0xfd;

	default:
		return 0xffff;
	}
}

static void vertigo_io_write(UINT32 addr, int data)
{
	int group = (addr >> 4) & 0x7;
	switch (group)
	{
	case 5: // audio_w: hand a command to the sound 6809 (sets command + FIRQs it)
		exidy440_sound_command_w(data);
		// AAE runs each CPU fully to its scheduler target before switching, so the
		// 6809 would not ack until CPU0's chunk ends - but the main CPU polls the
		// ack inside that same chunk and gives up ("unrecoverable system error").
		// Give the sound CPU an immediate slice to take the FIRQ and ack now (the
		// AAE stand-in for MAME's cpu_boost_interleave). Harmless while the 6809
		// still has FIRQ masked during its own init.
		if (m_cpu_6809[CPU1])
			m_cpu_6809[CPU1]->exec(500);
		break;

	case 7: // wsot_w: bit 1 = sound-CPU reset line (active low). Act on changes only.
		{
			static int snd_reset_held = -1;
			int held = ((data & 2) == 0) ? 1 : 0;
			if (held != snd_reset_held)
			{
				snd_reset_held = held;
				if (held)
					cpu_enable(CPU1, 0);               // hold the 6809 in reset
				else
				{
					cpu_needs_reset(CPU1);             // release: reset, then run
					cpu_enable(CPU1, 1);
				}
			}
		}
		break;

	case 6: // motor_w - MC6805 motor controller, not emulated (as in MAME)
	default:
		break;
	}
}

READ_HANDLER(vertigo_io_r8)    { return (UINT8)vertigo_io_read(address); }
WRITE_HANDLER(vertigo_io_w8)   { vertigo_io_write(address, data); }
READ16_HANDLER(vertigo_io_r16) { return (UINT16)vertigo_io_read(address); }
WRITE16_HANDLER(vertigo_io_w16){ vertigo_io_write(address, data & 0xff); }

// PIT byte accessors (the word bus uses pit8253_0_lsb_r/w directly).
READ_HANDLER(vertigo_pit_r8)   { return (UINT8)pit8253_read(0, address >> 1); }
WRITE_HANDLER(vertigo_pit_w8)  { pit8253_write(0, address >> 1, data); }

// ===========================================================================
// Periodic (per-frame) interrupt: coin inputs raise IRQ6
// ===========================================================================
void vertigo_interrupt()
{
	if ((readinputportbytag("COIN") & 0x7) < 0x7)
		update_irq_encoder(ENC_IRQ6, 1);
}

// ===========================================================================
// 68000 memory maps  (byte + word tables, RAM/ROM via base pointers)
// ===========================================================================
MEM_READ(VertigoReadByte)
MEM_ADDR8(0x000000, 0x001fff, NULL, vertigo_ram)
MEM_ADDR8(0x010000, 0x011fff, NULL, vertigo_ram)   // work-RAM mirror (MAME AM_MIRROR(0x010000))
MEM_ADDR8(0x002000, 0x003fff, NULL, vertigo_vram)
MEM_ADDR8(0x004000, 0x00407f, vertigo_io_r8, NULL)
MEM_ADDR8(0x005000, 0x00507f, vertigo_io_r8, NULL)
MEM_ADDR8(0x006000, 0x006007, vertigo_pit_r8, NULL)
MEM_ADDR8(0x007000, 0x0073ff, NULL, vertigo_nvram)
MEM_ADDR8(0x800000, 0x81ffff, NULL, vertigo_prog_rom)
MEM_END

MEM_WRITE(VertigoWriteByte)
MEM_ADDR8(0x000000, 0x001fff, NULL, vertigo_ram)
MEM_ADDR8(0x010000, 0x011fff, NULL, vertigo_ram)   // work-RAM mirror (MAME AM_MIRROR(0x010000))
MEM_ADDR8(0x002000, 0x003fff, NULL, vertigo_vram)
MEM_ADDR8(0x004000, 0x00407f, vertigo_io_w8, NULL)
MEM_ADDR8(0x005000, 0x00507f, vertigo_io_w8, NULL)
MEM_ADDR8(0x006000, 0x006007, vertigo_pit_w8, NULL)
MEM_ADDR8(0x007000, 0x0073ff, NULL, vertigo_nvram)
MEM_ADDR(0x800000, 0x81ffff, MWA_ROM)
MEM_END

MEM_READ16(VertigoReadWord)
MEM_ADDR16(0x000000, 0x001fff, NULL, vertigo_ram)
MEM_ADDR16(0x010000, 0x011fff, NULL, vertigo_ram)   // work-RAM mirror (MAME AM_MIRROR(0x010000))
MEM_ADDR16(0x002000, 0x003fff, NULL, vertigo_vram)
MEM_ADDR16(0x004000, 0x00407f, vertigo_io_r16, NULL)
MEM_ADDR16(0x005000, 0x00507f, vertigo_io_r16, NULL)
MEM_ADDR16(0x006000, 0x006007, pit8253_0_lsb_r, NULL)
MEM_ADDR16(0x007000, 0x0073ff, NULL, vertigo_nvram)
MEM_ADDR16(0x800000, 0x81ffff, NULL, vertigo_prog_rom)
MEM_END

MEM_WRITE16(VertigoWriteWord)
MEM_ADDR16(0x000000, 0x001fff, NULL, vertigo_ram)
MEM_ADDR16(0x010000, 0x011fff, NULL, vertigo_ram)   // work-RAM mirror (MAME AM_MIRROR(0x010000))
MEM_ADDR16(0x002000, 0x003fff, NULL, vertigo_vram)
MEM_ADDR16(0x004000, 0x00407f, vertigo_io_w16, NULL)
MEM_ADDR16(0x005000, 0x00507f, vertigo_io_w16, NULL)
MEM_ADDR16(0x006000, 0x006007, pit8253_0_lsb_w, NULL)
MEM_ADDR16(0x007000, 0x0073ff, NULL, vertigo_nvram)
MEM_ADDR16(0x800000, 0x81ffff, MWA_ROM16, NULL)
MEM_END

// ===========================================================================
// Sound 6809 (CPU1) memory map + Exidy 440 board glue.
// AAE's 6809 passes (addr - block base) to handlers, matching MAME's READ8/
// WRITE8 offset convention, so these thin wrappers pass it straight through.
// ===========================================================================
READ_HANDLER(vsnd_m6844_r)   { return exidy440_m6844_r(address); }
READ_HANDLER(vsnd_cmd_r)     { return exidy440_sound_command_r(address); }
READ_HANDLER(vsnd_vol_r)     { return exidy440_sound_volume_r(address); }
READ_HANDLER(vsnd_banks_r)   { return exidy440_sound_banks_r(address); }
WRITE_HANDLER(vsnd_m6844_w)  { exidy440_m6844_w(address, data); }
WRITE_HANDLER(vsnd_vol_w)    { exidy440_sound_volume_w(address, data); }
WRITE_HANDLER(vsnd_banks_w)  { exidy440_sound_banks_w(address, data); }
WRITE_HANDLER(vsnd_irqclr_w) { exidy440_sound_interrupt_clear_w(address, data); }

MEM_READ(VertigoSnd6809Read)
MEM_ADDR8(0x8000, 0x8016, vsnd_m6844_r, NULL)
MEM_ADDR8(0x8400, 0x8407, vsnd_vol_r,   NULL)
MEM_ADDR8(0x8800, 0x8800, vsnd_cmd_r,   NULL)
MEM_ADDR8(0x9400, 0x9403, vsnd_banks_r, NULL)
MEM_ADDR8(0xa000, 0xa7ff, NULL,         vertigo_snd_ram)
MEM_ADDR8(0xe000, 0xffff, NULL,         vertigo_snd_rom)
MEM_END

MEM_WRITE(VertigoSnd6809Write)
MEM_ADDR8(0x8000, 0x8016, vsnd_m6844_w,  NULL)
MEM_ADDR8(0x8400, 0x8407, vsnd_vol_w,    NULL)
MEM_ADDR8(0x9400, 0x9403, vsnd_banks_w,  NULL)
MEM_ADDR8(0x9800, 0x9800, vsnd_irqclr_w, NULL)
MEM_ADDR8(0xa000, 0xa7ff, NULL,          vertigo_snd_ram)
MEM_END

// The sound 6809's IRQ comes from PIT channel 1 (asserted in v_irq3_w), not the
// per-frame scheduler, so this per-CPU interrupt callback does nothing.
static void vertigo_snd_noint() {}

// ===========================================================================
// Init / Run / End
// ===========================================================================
int init_vertigo()
{
	LOG_INFO("vertigo: Starting init");

	memset(vertigo_prog_rom, 0, sizeof(vertigo_prog_rom));
	memset(vertigo_ram,      0, sizeof(vertigo_ram));
	memset(vertigo_vram,     0, sizeof(vertigo_vram));
	memset(vertigo_nvram,    0, sizeof(vertigo_nvram));
	nvram_set_region(vertigo_nvram, sizeof(vertigo_nvram), 0x00);   // persist 0x7000 NVRAM (MAME generic_0fill)

	// 68000 program + vector data ROM. Loaded compactly into REGION_CPU1 so that
	// offset 0 is the boot code; this image is what the CPU sees at 0x800000,
	// and its first 8 bytes are the 0x000000 reset vector.
	memcpy(vertigo_prog_rom, memory_region(REGION_CPU1), 0x14000);
	byteswap(vertigo_prog_rom, 0x14000);       // 68000 is big-endian
	memcpy(vertigo_ram, vertigo_prog_rom, 8);  // seed reset vector at 0x000000

	// Vector ROM is 16-bit big-endian; byteswap so the vproc's UINT16* reads
	// see the correct words (matches MAME's ROM_REGION16_BE).
	byteswap(memory_region(REGION_USER1), 0x10000);

	// Vector RAM shared with the bit-slice CPU.
	vertigo_vectorram = (UINT16 *)vertigo_vram;

	// Free-running clock used to size each vector-CPU run.
	vertigo_clock = timer_set_elapsed(CPU0);

	// IRQ priority encoder: all inputs inactive (high) to start.
	TTL74148_config(0, &irq_encoder);
	TTL74148_enable_input_w(0, 0);
	for (int i = 0; i < 8; i++)
		TTL74148_input_line_w(0, i, 1);
	TTL74148_update(0);

	// 8254 PIT (one chip) and the vector processor (assembles microcode here).
	pit8253_init(1, &pit8254_config);
	pit8253_set_subcycle_source(vertigo_pit_subcycles);   // fine-grained counter reads (RNG self-test)
	vertigo_vproc_init();

	// Sound 6809 program (REGION_CPU2, mapped at 0xe000-0xffff) + Exidy 440 engine.
	memset(vertigo_snd_rom, 0, sizeof(vertigo_snd_rom));
	memset(vertigo_snd_ram, 0, sizeof(vertigo_snd_ram));
	memcpy(vertigo_snd_rom, memory_region(REGION_CPU2) + 0xe000, sizeof(vertigo_snd_rom));
	exidy440_sound_init(1000000 / 16);   // CVSD FCLK = 62500 Hz

	irq4_time = vertigo_now();
	irq_state = 7;
	adc_result = 0;

	LOG_INFO("vertigo: Init complete");
	return 0;
}

void run_vertigo()
{
	// Vector generation and interrupts are driven by the PIT callbacks during
	// 68000 execution; nothing extra is needed per frame for this first pass.
	watchdog_reset_w(0, 0, 0);

	// Advance the sound board's MC6844 DMA channels by one frame of samples.
	exidy440_sound_update();

	//vertigo_vproc(2000, 0);

	// ---- DEBUG: per-frame pipeline trace (set VTG_TRACE 0 to silence) -------
	// Tells us, each frame, whether the 68000 is making progress (PC advancing,
	// not halted), whether the IRQ bitmask is accumulating stale levels
	// (irq_pending should be a single level or 0 with the 74148 fix), and
	// whether the PIT / vector CPU are still firing (irq4 edges + vproc cycles).
#define VTG_TRACE 1
#if VTG_TRACE
	static unsigned frame = 0;
	static unsigned last_pc = 0xffffffffu;
	static unsigned stuck = 0;
	if (m_cpu_68000[CPU0])
	{
		cpu_m68000* c = m_cpu_68000[CPU0];
		unsigned pc = (unsigned)c->GetPC();
		if (pc == last_pc) stuck++; else { stuck = 0; last_pc = pc; }

		// Detail for the first frames, then a few times a second, plus a one-shot
		// flag the moment the PC looks wedged (same PC ~half a second).
		if (frame < 10 || (frame % 15) == 0 || stuck == 30)
			LOG_INFO("VTG f=%u PC=%06X irqp=%02X mask=%u halt=%d stop=%d | irq4=%u rise=%u vpcyc=%u%s",
				frame, pc, (unsigned)c->irq_pending, (unsigned)c->int_mask,
				(int)c->halted, (int)c->stopped,
				dbg_irq4_calls, dbg_irq4_rising, dbg_vproc_cycles,
				(stuck >= 30) ? "  <-- PC STUCK" : "");
	}
	frame++;
	dbg_irq4_calls = dbg_irq4_rising = dbg_vproc_cycles = 0;
#endif
}

void end_vertigo()
{
	exidy440_sound_stop();
	vertigo_clock = -1;
	vertigo_vectorram = nullptr;
	LOG_INFO("vertigo: End game cleanup complete");
}

// ===========================================================================
// Input Port Definitions
//
// Order matters: readinputport(0..2) feeds the ADC (analog, stubbed); GIO and
// COIN are read by tag. Analog conventions still to be refined.
// ===========================================================================
INPUT_PORTS_START(vertigo)
PORT_START("IN0")  // ADC ch0: stick X 
PORT_ANALOG(0xff, 0x80, IPT_AD_STICK_X | IPF_CENTER, 100, 10, 0, 0xff)

PORT_START("IN1")  // ADC ch1: stick Y 
PORT_ANALOG(0xff, 0x80, IPT_AD_STICK_Y | IPF_CENTER | IPF_REVERSE, 100, 10, 0, 0xff)

PORT_START("IN2")  // ADC ch2: paddle
PORT_ANALOGX(0xff, 0x80, IPT_PADDLE, 100, 10, 0, 0xff, OSD_KEY_Z, OSD_KEY_X, 0, 0)

PORT_START("GIO")
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_UNKNOWN)
//PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_SERVICE)
PORT_BITX(0x10, IP_ACTIVE_LOW, IPT_SERVICE, "Service Mode", OSD_KEY_F2, IP_JOY_NONE)

PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_START1)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_BUTTON2)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_BUTTON1)

PORT_START("COIN")
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_COIN2)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_UNKNOWN)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_UNKNOWN)
INPUT_PORTS_END

// ===========================================================================
// ROM Definitions
// ===========================================================================
ROM_START(topgunnr)
// 68000 program + vector data (compact layout; offset 0 = boot/reset vector).
ROM_REGION(0x14000, REGION_CPU1, 0)
ROM_LOAD16_BYTE("tgl-2.9p",  0x000000, 0x002000, CRC(1d10b31e) SHA1(c66f11d2bee81a51baccf96f8e8335fc86dc20e4))
ROM_LOAD16_BYTE("tgl-2.10p", 0x000001, 0x002000, CRC(9c80b387) SHA1(aa7b770ddfaf65fd26959e7f9a3f15ba60979e50))
ROM_LOAD16_BYTE("tgl-2.9r",  0x004000, 0x002000, CRC(74454ac9) SHA1(4cf1e5373d5940ed81fe7d07324abb10667df097))
ROM_LOAD16_BYTE("tgl-2.10r", 0x004001, 0x002000, CRC(f5c28223) SHA1(16bf122f289129b50545e463f685f517cb9baca7))
ROM_LOAD16_BYTE("tgl-2.9t",  0x008000, 0x002000, CRC(d415d189) SHA1(3b726815292365a9206b83d1f2f5e314fcb24e73))
ROM_LOAD16_BYTE("tgl-2.10t", 0x008001, 0x002000, CRC(7f6a735c) SHA1(15abe2f705ed95a0f84c0305300e3aea720be906))
ROM_LOAD16_BYTE("tgl-2.9u",  0x00c000, 0x002000, CRC(723aea0c) SHA1(0f74fce22a832400906a886073f1252de327d85e))
ROM_LOAD16_BYTE("tgl-2.10u", 0x00c001, 0x002000, CRC(a28994ad) SHA1(4bba76670b7bfeaa3709b205baa83d51226c5db5))
ROM_LOAD16_BYTE("vgl-2.v9",  0x010000, 0x002000, CRC(bcfa709c) SHA1(575bba7471621f3f9cdf3c748500be5a5baf235d))
ROM_LOAD16_BYTE("vgl-2.v10", 0x010001, 0x002000, CRC(59d061b4) SHA1(154671746f79142cd6757793c71fb30661fc04f0))

// Vector ROM (16-bit BE; byteswapped in init).
ROM_REGION(0x10000, REGION_USER1, 0)
ROM_LOAD16_BYTE("tgl-2.1e", 0x0000, 0x2000, CRC(25832d56) SHA1(6dfd85f5e1c1d30be540b306851016328bb1cc00))
ROM_LOAD16_BYTE("tgl-2.2e", 0x0001, 0x2000, CRC(8746431f) SHA1(9e749e0e3aba51ba76e243e4c54b151dee9ff637))
ROM_LOAD16_BYTE("tgl-2.1d", 0x4000, 0x2000, CRC(639cab24) SHA1(ae97efa07054130413bf4230b89c03fa3d0d5e41))
ROM_LOAD16_BYTE("tgl-2.2d", 0x4001, 0x2000, CRC(10de7f77) SHA1(845e1dd7eb49116f0ba9332f27bf245f7625a598))
ROM_LOAD16_BYTE("tgl-2.1b", 0x8000, 0x2000, CRC(9671b463) SHA1(8716c299e983f13ed0e82a17bd25cb9ff5cfd43f))
ROM_LOAD16_BYTE("tgl-2.2b", 0x8001, 0x2000, CRC(258d507c) SHA1(16315039060d695c8278f544fbfa10ed1a0db3bc))
ROM_LOAD16_BYTE("tgl-2.1a", 0xc000, 0x2000, CRC(0f7b2123) SHA1(17287ff5fb3be2a4d145daf10f9fa2c93a19fcc5))
ROM_LOAD16_BYTE("tgl-2.2a", 0xc001, 0x2000, CRC(6edc8a05) SHA1(c257a845ecece072a9c1702e59edb2c65f9f4c02))

// Microcode PROMs: loaded raw (0x200 stride) and packed into the 64-bit
// microcode in vertigo_vproc_init(). The order below MUST match that code.
ROM_REGION(0x1a00, REGION_PROMS, 0)
ROM_LOAD("vuc.10", 0x0000, 0x200, CRC(8122e934) SHA1(a9bc0003f9597904fde49862c3d9f28522472b63))
ROM_LOAD("vuc.09", 0x0200, 0x200, CRC(5aa2240f) SHA1(c922961acfdefca67ba5555a1345d0a1c6cce526))
ROM_LOAD("vuc.13", 0x0400, 0x200, CRC(616aa606) SHA1(df985813ab35b98bd5b272b6e898c31b7bc16a5f))
ROM_LOAD("vuc.07", 0x0600, 0x200, CRC(b126c612) SHA1(1b9e22618b2cf68fac7d24ac87acc1f084af0f84))
ROM_LOAD("vuc.08", 0x0800, 0x200, CRC(5eb2f89f) SHA1(1c141da5abfd0a0899082ed5953b22f6ae3bb06d))
ROM_LOAD("vuc.05", 0x0a00, 0x200, CRC(d54cab61) SHA1(05d0548ceb292e11a64c101ff0638bc8a406c29a))
ROM_LOAD("vuc.06", 0x0c00, 0x200, CRC(c1b007a3) SHA1(c084c3767d5e6c0f995e33f3f1a642ad971301f4))
ROM_LOAD("vuc.11", 0x0e00, 0x200, CRC(1417c4c6) SHA1(7809b288611db8095d51f4d8a4dc51d3b67ff1c4))
ROM_LOAD("vuc.12", 0x1000, 0x200, CRC(9e6e1f2e) SHA1(9b7ff0617f001c409680e5950dae055148590a55))
ROM_LOAD("vuc.01", 0x1200, 0x200, CRC(aae009c2) SHA1(7e73dc6106a772525d737ebdeeb9a3520d02ecd7))
ROM_LOAD("vuc.02", 0x1400, 0x200, CRC(3c340a9a) SHA1(b0bcf81a417ddab848b9b4d4c4e279c8ff24a874))
ROM_LOAD("vuc.03", 0x1600, 0x200, CRC(23c1f136) SHA1(0eb959aa8fb6028dd97bdaa28981cec16652bf2d))
ROM_LOAD("vuc.04", 0x1800, 0x200, CRC(a5389228) SHA1(922d49c949e31413bbbff118c04965b649864a67))

// Sound 6809 program (Exidy 440 board); mapped at 0xe000-0xffff.
ROM_REGION(0x10000, REGION_CPU2, 0)
ROM_LOAD("vga1_7.g7", 0x0e000, 0x2000, CRC(db109b19) SHA1(c3fbb28cb4679c021bc48f844097add39a2208a5))

// CVSD sample ROMs - loaded now, decoded + mixed by the sound engine in Stage 2.
ROM_REGION(0x20000, REGION_SOUND1, 0)
ROM_LOAD("vga1_7.l6",  0x00000, 0x2000, CRC(20cbf97a) SHA1(13e138b08ba3328db6a2fba95a369422455d1c5c))
ROM_LOAD("vga1_7.m6",  0x02000, 0x2000, CRC(76197050) SHA1(d26701ba83a34384348fa34e3de78cc69dc5362e))
ROM_LOAD("vga1_7.n6",  0x04000, 0x2000, CRC(b93d7cbb) SHA1(1a4d05e03765b66ff20b963c5a0b5f7c3d5a360c))
ROM_LOAD("vga1_7.p6",  0x06000, 0x2000, CRC(b5bdb067) SHA1(924d76ff09dc173b582f84d1bb7ecd0a60cc1ab4))
ROM_LOAD("vga1_7.rs6", 0x08000, 0x2000, CRC(772f13a8) SHA1(87a6247ba58c006d1a062a7ac338c34e85d5cd01))
ROM_LOAD("vga1_7.st6", 0x0a000, 0x2000, CRC(a86f2178) SHA1(203fe71e2d42db4fb968c4e529eec7de0788aec1))
ROM_LOAD("vga1_7.tu6", 0x0c000, 0x2000, CRC(c1ab1d39) SHA1(ada43570ecf4ae76030dab4a916c53536e41606d))
ROM_LOAD("vga1_7.uv6", 0x0e000, 0x2000, CRC(95a05700) SHA1(e9f16408ac9a0ed28af74bfd8419a58e7b0f599a))
ROM_LOAD("vga1_7.l7",  0x10000, 0x2000, CRC(183ba71d) SHA1(03b4dc21094d5911b6f964e060cbe4450ecb71e6))
ROM_LOAD("vga1_7.m7",  0x12000, 0x2000, CRC(4866b4b7) SHA1(fa28d602b1e0a47528b710602bb32d5cc52c8db8))
ROM_END

// ===========================================================================
// AAE Driver Table Entry
// ===========================================================================
AAE_DRIVER_BEGIN(drv_topgunnr, "topgunnr", "Top Gunner")
AAE_DRIVER_ROM(rom_topgunnr)
AAE_DRIVER_FUNCS(&init_vertigo, &run_vertigo, &end_vertigo)
AAE_DRIVER_INPUT(input_ports_vertigo)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	// CPU0: MC68000 @ 8 MHz. Interrupts are managed by the 74148/PIT chain;
	// the per-frame callback only services coin inputs (IRQ6).
	AAE_CPU_ENTRY(
		/*type*/     CPU_68000,
		/*freq*/     8000000,
		/*div*/      200,
		/*ipf*/      1,
		/*int type*/ INT_TYPE_NONE,
		/*int cb*/   &vertigo_interrupt,
		/*r8*/       VertigoReadByte,
		/*w8*/       VertigoWriteByte,
		/*pr*/       nullptr,
		/*pw*/       nullptr,
		/*r16*/      VertigoReadWord,
		/*w16*/      VertigoWriteWord
	),
	// CPU1: MC6809 @ 1 MHz - Exidy 440 sound board. FIRQ-driven by sound commands
	// (no periodic interrupt), so the per-frame callback is a no-op.
	AAE_CPU_ENTRY(
		/*type*/     CPU_M6809,
		/*freq*/     1000000,
		/*div*/      200,
		/*ipf*/      1,
		/*int type*/ INT_TYPE_NONE,
		/*int cb*/   &vertigo_snd_noint,
		/*r8*/       VertigoSnd6809Read,
		/*w8*/       VertigoSnd6809Write,
		/*pr*/       nullptr,
		/*pw*/       nullptr,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(60, 0, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, 0)
AAE_DRIVER_SCREEN(0, 1024, 0, 512, 0, 512)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM(generic_nvram_handler)
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_topgunnr)

#endif // VERTIGO_CPP
