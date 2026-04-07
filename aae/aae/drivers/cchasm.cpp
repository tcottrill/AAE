// ---------------------------------------------------------------------------
// cchasm.cpp - Cosmic Chasm Driver for AAE
//
// Cinematronics / GCE, 1983
// Original MAME driver by Mathis Rosenhauer, adapted for AAE.
//
// Hardware:
//   Main CPU:  Motorola 68000 @ 8 MHz
//   Sound CPU: Zilog Z80 @ 3.584229 MHz
//   Sound:     2x AY-3-8910 PSG @ 1.818182 MHz
//              2x CTC-driven square wave tone channels (custom sound)
//   Timer:     MC6840 PTM on the 68000 side
//   Video:     Color X-Y vector (custom refresh processor in VRAM)
//   CTC:       1x Z80 CTC on sound board, daisy-chained for IM2
//              CTC ch1/ch2 zero-count outputs drive the square-wave tones
//              CTC ch0 and ch2 trigger inputs used for handshaking
//
// Inter-CPU communication uses four sound latches:
//   soundlatch  (68000 -> Z80, command byte)
//   soundlatch2 (68000 -> Z80, data byte + triggers NMI + CTC trg2)
//   soundlatch3 (Z80 -> 68000, status byte)
//   soundlatch4 (Z80 -> 68000, data byte + triggers 68000 IRQ1)
//
// FPS: 40 Hz (per original MAME driver)
// Orientation: ROT270 (vertical monitor, rotated 270 degrees)
// ---------------------------------------------------------------------------

// This is a bit of a hack right now, I can't get the Z80 CTC callbacks to work without screwing everything else
// up, so I added some samples as a work around. I'll get this fixed at a later date.

// Also, I am skipping the mame_vector.cpp code here, it looks better without the scaling imposed there. video,ini required for flipping.

#ifndef CCHASM_CPP
#define CCHASM_CPP

#include "cchasm.h"
#include "./68000/m68k.h"
#include "./68000/m68kcpu.h"
#include "aae_mame_driver.h"
#include "driver_registry.h"
#include "cpu_control.h"
#include "AY8910.H"
#include "timer.h"
#include "emu_vector_draw.h"
#include "z80fmly.h"

// ---------------------------------------------------------------------------
// Build option: set to 1 to enable verbose MC6840 logging
// ---------------------------------------------------------------------------
#define CCHASM_6840_LOG 0

#if CCHASM_6840_LOG
#define LOG6840(...) LOG_INFO(__VA_ARGS__)
#else
#define LOG6840(...) ((void)0)
#endif

// ===========================================================================
// Vector refresh processor opcodes
// ===========================================================================
#define VEC_HALT   0
#define VEC_JUMP   1
#define VEC_CCOLOR  2
#define VEC_SCALEY 3
#define VEC_POSY   4
#define VEC_SCALEX 5
#define VEC_POSX   6
#define VEC_LENGTH 7
#define VEC_SHIFT  16

// ===========================================================================
// RAM / ROM buffers
// ===========================================================================
static unsigned char cchasm_program_rom[0x10000];   // 68000 program ROM (64K)
static unsigned char cchasm_main_ram[0x5000];        // 68000 work RAM (0xFFB000-0xFFFFFF)
static UINT16* cchasm_ram = nullptr;                 // pointer into main_ram for vector list

static unsigned char cchasm_sound_ram1[0x400];       // Z80 RAM at 0x4000-0x43FF
static unsigned char cchasm_sound_ram2[0x400];       // Z80 RAM at 0x5000-0x53FF

// ===========================================================================
// Video state
// ===========================================================================
static int xcenter = 0;
static int ycenter = 0;

// ===========================================================================
// MC6840 PTM state (68000 side timer)
// ===========================================================================
#define M6840_CYCLE 1250  // 800 kHz base cycle time in nanoseconds

static int m6840_cr_select = 2;        // CR1/CR3 write select
static int m6840_timerLSB[3];          // timer latch LSB
static int m6840_timerMSB[3];          // timer latch MSB (also buffer)
static int m6840_status;               // status register (bits 0-2: timer flags, bit 7: composite)
static int m6840_cr[3];                // control registers

// ===========================================================================
// Inter-CPU communication: four sound latches
// ===========================================================================
static int soundlatch = 0;   // 68000 -> Z80 (command)
static int soundlatch2 = 0;   // 68000 -> Z80 (data, triggers NMI + CTC trg2)
static int soundlatch3 = 0;   // Z80 -> 68000 (status)
static int soundlatch4 = 0;   // Z80 -> 68000 (data, triggers 68000 IRQ1)

static int sound_flags = 0;   // handshaking flags
// bit 6: set by Z80 when writing soundlatch4 (cleared by 68000 read)
// bit 7: set by 68000 when writing soundlatch2 (cleared by Z80 read)

// ===========================================================================
// CTC square-wave tone output state
// ===========================================================================
static int ctc_tone_output[2] = { 0, 0 };
static int ctc_tone_active[2] = { 0, 0 };

// ---------------------------------------------------------------------------
// CTC square-wave audio via WAV samples
//
// CTC channels 1 and 2 drive two independent tone generators used for the
// firing and thrust sound effects. Instead of synthesizing square waves,
// we use pre-recorded WAV samples that loop while the CTC tone is active.
//
// Playback channels 2 and 3 are used (0 and 1 are the two AY-3-8910 chips).
// Sample indices match the order in cchasm_samples[] (0-based from first WAV).
// ---------------------------------------------------------------------------
#define CTC_PLAY_CH0     2    // mixer playback channel for CTC tone 0
#define CTC_PLAY_CH1     3    // mixer playback channel for CTC tone 1
#define CTC_SAMPLE_FIRE  0    // sample index: firing tone WAV
#define CTC_SAMPLE_THRUST 1   // sample index: thrust tone WAV

// Track whether each tone is currently playing to avoid redundant start/stop
static bool ctc_tone_playing[2] = { false, false };

// ===========================================================================
// AY-3-8910 interface (2 chips @ 1.818182 MHz)
// ===========================================================================
static struct AY8910interface cchasm_ay8910_intf =
{
	2,              // num: 2 chips
	1818182,        // baseclock: 1.818182 MHz
	{ 25, 25 },    // vol: per-chip master gain
	{ 0, 0 },      // volshift: no shift
	{ 0, 0 },      // portAread callbacks
	{ 0, 0 },      // portBread callbacks
	{ 0, 0 },      // portAwrite callbacks
	{ 0, 0 }       // portBwrite callbacks
};

static int fire_toggle = 0;
static int thrust_toggle = 0;

// ===========================================================================
// WAV samples for CTC square-wave tones
// These replace the synthesized square waves with pre-recorded sound effects.
// Place these WAVs in a cchasm.zip file in the samples directory.
// ===========================================================================
static const char* cchasm_samples[] =
{
	"cchasm.zip",
	"fire.wav",      // CTC_SAMPLE_FIRE  (0) - firing sound
	"thrust.wav",    // CTC_SAMPLE_THRUST (1) - thrust sound
	0
};

// ===========================================================================
// MC6840 PTM - 68000 side timer / interrupt
// ===========================================================================
static void cchasm_6840_irq(int state)
{
	if (state == ASSERT_LINE)
		cpu_do_int_imm(CPU0, INT_TYPE_68K4);
}

static void timer_2_timeout(int dummy)
{
	if (m6840_cr[1] & 0x40)
	{
		m6840_status |= 0x82;
		cchasm_6840_irq(ASSERT_LINE);
	}
}

static int cchasm_6840_read_reg(int reg)
{
	switch (reg)
	{
	case 0x0:
		LOG6840("MC6840: Read from unimplemented register 0");
		return 0;
	case 0x1: return m6840_status;
	case 0x2: return m6840_timerMSB[0];
	case 0x3: return m6840_timerLSB[0];
	case 0x4: return m6840_timerMSB[1];
	case 0x5: return m6840_timerLSB[1];
	case 0x6: return m6840_timerMSB[2];
	case 0x7: return m6840_timerLSB[2];
	}
	return 0;
}

static void cchasm_6840_write_reg(int reg, int val)
{
	val &= 0xff;
	switch (reg)
	{
	case 0x0:
		m6840_cr[m6840_cr_select] = val;
		if (m6840_cr_select == 0 && (val & 0x01)) {
			for (int i = 0; i < 3; i++) {
				m6840_timerLSB[i] = 255;
				m6840_timerMSB[i] = 255;
			}
		}
		break;
	case 0x1:
		m6840_cr[1] = val;
		m6840_cr_select = (val & 0x01) ? 0 : 2;
		break;
	case 0x2:
		m6840_timerMSB[0] = val;
		m6840_status &= ~0x01;
		break;
	case 0x3:
		m6840_status &= ~0x01;
		m6840_timerLSB[0] = val;
		break;
	case 0x4:
		m6840_status &= ~0x02;
		cchasm_6840_irq(CLEAR_LINE);
		m6840_timerMSB[1] = val;
		if ((m6840_cr[1] & 0x38) == 0)
		{
			// The timer is re-armed each time the 68000 writes the MSB register.
			double period = TIME_IN_NSEC(M6840_CYCLE) * ((m6840_timerMSB[1] << 8) | m6840_timerLSB[1]);
			timer_set(period, ONE_SHOT, 0, timer_2_timeout);
		}
		break;
	case 0x5:
		m6840_status &= ~0x02;
		cchasm_6840_irq(CLEAR_LINE);
		m6840_timerLSB[1] = val;
		break;
	case 0x6:
		m6840_status &= ~0x04;
		m6840_timerMSB[2] = val;
		break;
	case 0x7:
		m6840_status &= ~0x04;
		m6840_timerLSB[2] = val;
		break;
	}
}

READ16_HANDLER(cchasm_6840_r16) { return (UINT16)cchasm_6840_read_reg(address >> 1); }
WRITE16_HANDLER(cchasm_6840_w16) { cchasm_6840_write_reg(address >> 1, data & 0xff); }
READ_HANDLER(cchasm_6840_r8) { return (UINT8)cchasm_6840_read_reg(address >> 1); }
WRITE_HANDLER(cchasm_6840_w8) { cchasm_6840_write_reg(address >> 1, data); }

// ===========================================================================
// Vector refresh processor
// ===========================================================================
static void cchasm_refresh_end(int dummy)
{
	cpu_do_int_imm(CPU0, INT_TYPE_68K2);
}


static void cchasm_refresh(void)
{
	int pc = 0;
	int finished = 0;
	int opcode, opdata;
	int currentx = 0, currenty = 0;
	int scalex = 0, scaley = 0;
	int color = 0;
	int total_length = 1;
	int move = 0;

	vector_clear_list();
	cache_clear();

	while (!finished)
	{
		UINT16 word = cchasm_ram[pc];
		opcode = word >> 12;
		opdata = word & 0xfff;

		if ((opcode > VEC_CCOLOR) && (opdata & 0x800))
			opdata |= 0xfffff000;

		pc++;

		switch (opcode)
		{
		case VEC_HALT:
			finished = 1;
			break;
		case VEC_JUMP:
			pc = opdata - 0xb00;
			break;
		case VEC_CCOLOR:
			color = VECTOR_COLOR444(opdata ^ 0xfff);
			break;
		case VEC_SCALEY:
			scaley = opdata << 5;
			break;
		case VEC_POSY:
			move = 1;
			currenty = ycenter + (opdata << VEC_SHIFT);
			break;
		case VEC_SCALEX:
			scalex = opdata << 5;
			break;
		case VEC_POSX:
			move = 1;
			currentx = xcenter - (opdata << VEC_SHIFT);
			break;
		case VEC_LENGTH:
			// Skipping the use of mame_vector.cpp code here due to visual issues caused by the scaling.
			if (move)
			{
				float sx = (float)(currenty >> VEC_SHIFT);
				float sy = 1024.0f - (float)(currentx >> VEC_SHIFT);
				add_line(sx * 1.33f, sy, sx * 1.33f, sy, 0, 0);
				move = 0;
			}
			{
				float prev_x = (float)(currenty >> VEC_SHIFT);
				float prev_y = 1024.0f - (float)(currentx >> VEC_SHIFT);

				currentx -= opdata * scalex;
				currenty += opdata * scaley;
				total_length += abs(opdata);

				float end_x = (float)(currenty >> VEC_SHIFT);
				float end_y = 1024.0f - (float)(currentx >> VEC_SHIFT);

				if (color) {
					add_line(prev_x * 1.33f, prev_y, end_x * 1.33f, end_y, 0xff, color);
				}
				else {
					move = 1;
				}
			}
			break;

		default:
			LOG_INFO("cchasm: Unknown vector opcode 0x%x data 0x%x at pc=%d", opcode, opdata, pc - 1);
			finished = 1;
			break;
		}
	}

	timer_pulse(TIME_IN_NSEC(166) * total_length, CPU0, cchasm_refresh_end);
}

WRITE16_HANDLER(cchasm_refresh_control_w16)
{
	int cmd = (data >> 8) & 0xff;
	if (cmd == 0x37)
	{
		cchasm_refresh();
	}
}

WRITE_HANDLER(cchasm_refresh_control_w8)
{
	// This code isn't actually called.
	if ((address & 1) == 0) { if (data == 0x37) { cchasm_refresh(); } }
}

// ===========================================================================
// CTC Interrupt & Handshaking via z80fmly
// ===========================================================================

// State change callback passed to z80ctc_interface
static void cchasm_ctc_interrupt(int state)
{
	// Guard: z80ctc_init() calls z80ctc_reset() which fires this callback
	// before the Z80 CPU object has been created by the scheduler.
	// During init, state is always 0 (all int_states cleared), so skipping is safe.
	if (m_cpu_z80[CPU1] == nullptr)
		return;

	// state contains any pending IRQ level across the CTC (Z80_INT_REQ)
	if (state) {
		m_cpu_z80[CPU1]->mz80AssertInt();
	}
	else {
		m_cpu_z80[CPU1]->mz80ClearPendingInterrupt();
	}
}

static void ctc_timer_1_w(int ch, int data_rising)
{
	LOG_INFO("CTC TIMER 1 WRITE");
	if (data_rising)
	{
		LOG_INFO("SOUND 1 PLAYING");
		ctc_tone_output[0] ^= 0x7f;
		ctc_tone_active[0] = 1;
	}
}

static void ctc_timer_2_w(int ch, int data_rising)
{
	LOG_INFO("CTC TIMER 2 WRITE");
	if (data_rising)
	{
		LOG_INFO("SOUND 2 PLAYING");
		ctc_tone_output[1] ^= 0x7f;
		ctc_tone_active[1] = 1;
	}
}

static void cchasm_sound_frame_update()
{
	if ((readinputportbytag("IN3") & 0x70) != 0x70)
	{
		z80ctc_0_trg0_w(0, 1);
	}
}

// ===========================================================================
// 68000 I/O region: 0xF80000 - 0xF800FF
// ===========================================================================

static int cchasm_io_read_reg(int reg)
{
	switch (reg)
	{
	case 0x0: return soundlatch3;
	case 0x1:
		sound_flags &= ~0x40;
		return soundlatch4;
	case 0x2:
	{
		// Return sound handshaking flags + coin/test inputs + bit 3 always set
		int coin = (input_port_3_r(0) >> 4) & 0x7;
		if (coin != 0x7) coin |= 0x8;
		return (sound_flags | (input_port_3_r(0) & 0x07) | 0x08);
	}
	case 0x5:
	{
		int fire = (input_port_2_r(0) & 0x04);
		int thrust = (input_port_2_r(0) & 0x08);

		if (fire) { fire_toggle = 0; }
		if (fire == 0 && fire_toggle == 0) {
			fire_toggle = 1; sample_start(CTC_PLAY_CH0, CTC_SAMPLE_FIRE, 0);
		}

		if (thrust) {
			thrust_toggle = 0; sample_stop(CTC_PLAY_CH1);
		}
		if (thrust == 0 && thrust_toggle == 0) {
			thrust_toggle = 1;
			//sample_start(CTC_PLAY_CH1, CTC_SAMPLE_THRUST, 1);
		}

		return input_port_2_r(0);
	}
	case 0x8: return input_port_1_r(0);
	default: return 0xff;
	}
}

static void cchasm_io_write_reg(int reg, int val)
{
	switch (reg)
	{
	case 0:
		soundlatch = val;
		break;
	case 1:
		soundlatch2 = val;
		sound_flags |= 0x80;
		z80ctc_0_trg2_w(0, 1);
		cpu_do_int_imm(CPU1, INT_TYPE_NMI);
		break;
	case 2:
		break;
	}
}

READ16_HANDLER(cchasm_io_r16)
{
	int reg = (address >> 1) & 0xf;
	return (UINT16)(cchasm_io_read_reg(reg) << 8);
}

WRITE16_HANDLER(cchasm_io_w16)
{
	int reg = (address >> 1) & 0xf;
	int val = (data >> 8) & 0xff;
	cchasm_io_write_reg(reg, val);
}

READ_HANDLER(cchasm_io_r8)
{
	if ((address & 1) == 0) {
		int reg = (address >> 1) & 0xf;
		return (UINT8)cchasm_io_read_reg(reg);
	}
	return 0;
}

WRITE_HANDLER(cchasm_io_w8)
{
	if ((address & 1) == 0) {
		int reg = (address >> 1) & 0xf;
		cchasm_io_write_reg(reg, data);
	}
}

// ===========================================================================
// Input / DSW / LED / Watchdog
// ===========================================================================

READ16_HANDLER(cchasm_input_port_0_r16) { return input_port_0_r(0); }
READ_HANDLER(cchasm_input_port_0_r8) { return (UINT8)input_port_0_r(0); }

WRITE16_HANDLER(cchasm_led_w16) { (void)data; }
WRITE_HANDLER(cchasm_led_w8) { (void)data; }

WRITE16_HANDLER(cchasm_watchdog_w16) { watchdog_reset_w(0, 0, 0); }
WRITE_HANDLER(cchasm_watchdog_w8) { watchdog_reset_w(0, 0, 0); }

// ===========================================================================
// Z80 Sound CPU - Memory-mapped I/O at 0x6000-0x6FFF
// ===========================================================================

READ_HANDLER(cchasm_snd_io_r)
{
	int sel = address & 0x61;
	switch (sel)
	{
	case 0x00:
	{
		// Coin inputs: bits 4-6 of port 3, shifted down.
		// If any coin is inserted (bits not all 1), set bit 3.
		int coin = (input_port_3_r(0) >> 4) & 0x7;
		if (coin != 0x7) coin |= 0x8;
		return sound_flags | coin;
	}
	case 0x01: return (UINT8)AY8910Read(0);
	case 0x21: return (UINT8)AY8910Read(1);
	case 0x40: return soundlatch;
	case 0x41:
		sound_flags &= ~0x80;
		z80ctc_0_trg2_w(0, 0);
		return soundlatch2;
	default:
		LOG_INFO("cchasm Z80: Read from unmapped snd_io at 0x%04x (sel=0x%02x)", address, sel);
		return 0;
	}
}

WRITE_HANDLER(cchasm_snd_io_w)
{
	int sel = address & 0x61;
	switch (sel)
	{
	case 0x00: AY8910Write(0, 0, data); break;
	case 0x01: AY8910Write(0, 1, data); break;
	case 0x20: AY8910Write(1, 0, data); break;
	case 0x21: AY8910Write(1, 1, data); break;
	case 0x40: soundlatch3 = data; break;
	case 0x41:
		sound_flags |= 0x40;
		soundlatch4 = data;
		cpu_do_int_imm(CPU0, INT_TYPE_68K1);
		break;
	case 0x61:
		// Pulse CTC trigger 0: original MAME just does a single falling-edge call.
		// The CTC trigger is edge-sensitive, so this clocks the counter.
		z80ctc_0_trg0_w(0, 0);
		break;
	default:
		LOG_INFO("cchasm Z80: Write 0x%02x to unmapped snd_io at 0x%04x (sel=0x%02x)", data, address, sel);
		break;
	}
}

// ===========================================================================
// Z80 Sound CPU - CTC I/O Port Handlers (Z80 I/O ports 0x00-0x03)
// ===========================================================================

PORT_WRITE_HANDLER(cchasm_ctc_port_w)
{
	z80ctc_0_w(port & 0x03, data);
}

PORT_READ_HANDLER(cchasm_ctc_port_r)
{
	return z80ctc_0_r(port & 0x03);
}

// ===========================================================================
// Z80 Sound CPU - Memory and Port Maps
// ===========================================================================

MEM_READ(CchasmSoundMemRead)
MEM_ADDR(0x0000, 0x0fff, MRA_ROM)
MEM_ADDR(0x4000, 0x43ff, MRA_RAM)
MEM_ADDR(0x5000, 0x53ff, MRA_RAM)
MEM_ADDR(0x6000, 0x6fff, cchasm_snd_io_r)
MEM_END

MEM_WRITE(CchasmSoundMemWrite)
MEM_ADDR(0x0000, 0x0fff, MWA_ROM)
MEM_ADDR(0x4000, 0x43ff, MWA_RAM)
MEM_ADDR(0x5000, 0x53ff, MWA_RAM)
MEM_ADDR(0x6000, 0x6fff, cchasm_snd_io_w)
MEM_END

PORT_READ(CchasmSoundPortRead)
PORT_ADDR(0x00, 0x03, cchasm_ctc_port_r)
PORT_END

PORT_WRITE(CchasmSoundPortWrite)
PORT_ADDR(0x00, 0x03, cchasm_ctc_port_w)
PORT_END

// ===========================================================================
// 68000 Main CPU - Memory Maps
// ===========================================================================

MEM_READ(CchasmReadByte)
MEM_ADDR8(0x000000, 0x00ffff, NULL, cchasm_program_rom)
MEM_ADDR8(0x040000, 0x04000f, cchasm_6840_r8, NULL)
MEM_ADDR8(0x060000, 0x060001, cchasm_input_port_0_r8, NULL)
MEM_ADDR8(0xf80000, 0xf800ff, cchasm_io_r8, NULL)
MEM_ADDR8(0xffb000, 0xffffff, NULL, cchasm_main_ram)
MEM_END

MEM_WRITE(CchasmWriteByte)
MEM_ADDR(0x000000, 0x00ffff, MWA_ROM)
MEM_ADDR8(0x040000, 0x04000f, cchasm_6840_w8, NULL)
MEM_ADDR8(0x050000, 0x050001, cchasm_refresh_control_w8, NULL)
MEM_ADDR8(0x060000, 0x060001, cchasm_led_w8, NULL)
MEM_ADDR8(0x070000, 0x070001, cchasm_watchdog_w8, NULL)
MEM_ADDR8(0xf80000, 0xf800ff, cchasm_io_w8, NULL)
MEM_ADDR8(0xffb000, 0xffffff, NULL, cchasm_main_ram)
MEM_END

MEM_READ16(CchasmReadWord)
MEM_ADDR16(0x000000, 0x00ffff, NULL, cchasm_program_rom)
MEM_ADDR16(0x040000, 0x04000f, cchasm_6840_r16, NULL)
//MEM_ADDR16(0x060000, 0x060001, cchasm_input_port_0_r16, NULL)
//MEM_ADDR16(0xf80000, 0xf800ff, cchasm_io_r16, NULL)
MEM_ADDR16(0xffb000, 0xffffff, NULL, cchasm_main_ram)
MEM_END

MEM_WRITE16(CchasmWriteWord)
MEM_ADDR16(0x000000, 0x00ffff, MWA_ROM16, NULL)
MEM_ADDR16(0x040000, 0x04000f, cchasm_6840_w16, NULL)
MEM_ADDR16(0x050000, 0x050001, cchasm_refresh_control_w16, NULL)
MEM_ADDR16(0x060000, 0x060001, cchasm_led_w16, NULL)
MEM_ADDR16(0x070000, 0x070001, cchasm_watchdog_w16, NULL)
MEM_ADDR16(0xf80000, 0xf800ff, cchasm_io_w16, NULL)
MEM_ADDR16(0xffb000, 0xffffff, NULL, cchasm_main_ram)
MEM_END

// ===========================================================================
// 68000 IRQ acknowledge callback
// ===========================================================================

static int cchasm_irq_callback(int irqline)
{
	return M68K_INT_ACK_AUTOVECTOR;
}

void cchasm_interrupt() {}
void cchasm_sound_interrupt() {}

// ===========================================================================
// Init / Run / End
// ===========================================================================

static bool cchasm_reti_hooked = false;

int init_cchasm()
{
	LOG_INFO("cchasm: Starting init");

	memset(cchasm_program_rom, 0x00, sizeof(cchasm_program_rom));
	memset(cchasm_main_ram, 0x00, sizeof(cchasm_main_ram));
	memset(cchasm_sound_ram1, 0x00, sizeof(cchasm_sound_ram1));
	memset(cchasm_sound_ram2, 0x00, sizeof(cchasm_sound_ram2));

	memcpy(cchasm_program_rom, Machine->memory_region[CPU0], 0x10000);
	byteswap(cchasm_program_rom, 0x10000);

	cchasm_ram = (UINT16*)cchasm_main_ram;

	m6840_cr_select = 2;
	m6840_status = 0;
	for (int i = 0; i < 3; i++) {
		m6840_timerLSB[i] = 0xff;
		m6840_timerMSB[i] = 0xff;
		m6840_cr[i] = 0;
	}

	soundlatch = 0;
	soundlatch2 = 0;
	soundlatch3 = 0;
	soundlatch4 = 0;
	sound_flags = 0;

	ctc_tone_output[0] = 0;
	ctc_tone_output[1] = 0;
	ctc_tone_active[0] = 0;
	ctc_tone_active[1] = 0;

	// Configure Z80 CTC using the z80fmly struct and standard functions
	z80ctc_interface ctc_cfg = {};
	ctc_cfg.num = 1;
	ctc_cfg.baseclock[0] = 3584229;
	ctc_cfg.notimer[0] = 0;
	ctc_cfg.intr[0] = cchasm_ctc_interrupt;
	ctc_cfg.zc0[0] = nullptr;
	ctc_cfg.zc1[0] = ctc_timer_1_w;
	ctc_cfg.zc2[0] = ctc_timer_2_w;
	z80ctc_init(&ctc_cfg);

	AY8910_sh_start(&cchasm_ay8910_intf);

	// Reset CTC tone playback state
	// WAV samples are loaded automatically by load_samples_batch() from cchasm_samples[]
	ctc_tone_playing[0] = false;
	ctc_tone_playing[1] = false;

	xcenter = ((1023 + 0) / 2) << VEC_SHIFT;
	ycenter = ((767 + 0) / 2) << VEC_SHIFT;

	cchasm_reti_hooked = false;

	LOG_INFO("cchasm: Init complete");
	return 0;
}

void run_cchasm()
{
	// Per-frame sound update: pulse CTC trigger 0 on coin insertion
	cchasm_sound_frame_update();

	// Hook RETI and IRQ callback on the first frame execution
	if (!cchasm_reti_hooked && m_cpu_z80[CPU1] != nullptr)
	{
		// Wire RETI to notify the CTC daisy chain so it can clear IEO state
		m_cpu_z80[CPU1]->reti_hook = []() {
			z80ctc_reti(0);
			};

		// Fetch the interrupt vector dynamically from the CTC daisy chain
		// when the Z80 actually acknowledges the interrupt (IFF1=1, ready).
		// z80ctc_interrupt() returns the vector and advances the channel
		// from INT_REQ to INT_IEO, ensuring proper daisy-chain sequencing.
		m_cpu_z80[CPU1]->int_ack_fn = []() -> int {
			return z80ctc_interrupt(0);
			};

		cchasm_reti_hooked = true;
	}
	/*
	// CTC square-wave tones via WAV samples
	// Start looping the sample when the CTC tone becomes active,
	// stop it when the CTC stops toggling for a frame.
	if (ctc_tone_active[0] && !ctc_tone_playing[0])
	{
		LOG_INFO("SAMPLE_START FIRE");
		sample_start(CTC_PLAY_CH0, CTC_SAMPLE_FIRE, 0);
		ctc_tone_playing[0] = true;
	}
	else if (!ctc_tone_active[0] && ctc_tone_playing[0])
	{
		sample_stop(CTC_PLAY_CH0);
		ctc_tone_playing[0] = false;
	}

	if (ctc_tone_active[1] && !ctc_tone_playing[1])
	{
		sample_start(CTC_PLAY_CH1, CTC_SAMPLE_THRUST, 1);
		ctc_tone_playing[1] = true;
	}
	else if (!ctc_tone_active[1] && ctc_tone_playing[1])
	{
		sample_stop(CTC_PLAY_CH1);
		ctc_tone_playing[1] = false;
	}

	// Clear active flags; if the CTC keeps toggling, it will set them again
	ctc_tone_active[0] = 0;
	ctc_tone_active[1] = 0;
	*/
	// Update AY-3-8910 audio output
	AY8910_sh_update();

	// Reset watchdog
	watchdog_reset_w(0, 0, 0);
}

void end_cchasm()
{
	AY8910clear();

	// Stop CTC tone samples if playing
	if (ctc_tone_playing[0]) { sample_stop(CTC_PLAY_CH0); ctc_tone_playing[0] = false; }
	if (ctc_tone_playing[1]) { sample_stop(CTC_PLAY_CH1); ctc_tone_playing[1] = false; }
	ctc_tone_active[0] = 0;
	ctc_tone_active[1] = 0;
	ctc_tone_output[0] = 0;
	ctc_tone_output[1] = 0;

	cchasm_reti_hooked = false;
	cchasm_ram = nullptr;

	LOG_INFO("cchasm: End game cleanup complete");
}

// ===========================================================================
// Input Port Definitions
// ===========================================================================

INPUT_PORTS_START(cchasm)
PORT_START("DSW")  // Port 0: DIP switches
PORT_DIPNAME(0x01, 0x01, "Lives")
PORT_DIPSETTING(0x01, "3")
PORT_DIPSETTING(0x00, "5")
PORT_DIPNAME(0x06, 0x06, "Bonus Life")
PORT_DIPSETTING(0x06, "40000")
PORT_DIPSETTING(0x04, "60000")
PORT_DIPSETTING(0x02, "80000")
PORT_DIPSETTING(0x00, "100000")
PORT_DIPNAME(0x08, 0x08, "Difficulty")
PORT_DIPSETTING(0x00, "Easy")
PORT_DIPSETTING(0x08, "Hard")
PORT_DIPNAME(0x10, 0x10, "Bonus Life Freq")
PORT_DIPSETTING(0x00, "Once")
PORT_DIPSETTING(0x10, "Every")
PORT_DIPNAME(0x20, 0x00, "Demo Sounds")
PORT_DIPSETTING(0x20, "Off")
PORT_DIPSETTING(0x00, "On")
PORT_DIPNAME(0x40, 0x40, "Coinage")
PORT_DIPSETTING(0x00, "2C/1C")
PORT_DIPSETTING(0x40, "1C/1C")
// PORT_BITX(0x80, 0x80, IPT_SERVICE, "Service Mode", OSD_KEY_F2, IP_JOY_NONE)
PORT_SERVICE(0x80, IP_ACTIVE_LOW)

PORT_START("IN1")  // Port 1: Spinner / dial
PORT_ANALOG(0xff, 0, IPT_DIAL, 100, 10, 0, 0)

PORT_START("IN2")  // Port 2: Buttons and start
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_BUTTON3)   // Shield
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_BUTTON1)   // Fire
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_BUTTON2)   // Thrust
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_START1)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_START2)
PORT_START("IN3")
PORT_BITX(0x01, IP_ACTIVE_LOW, 0, "Test 1", OSD_KEY_F1, IP_JOY_NONE)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_UNUSED)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_COIN2)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_COIN3)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNUSED)

INPUT_PORTS_END

// ===========================================================================
// ROM Definitions
// ===========================================================================

ROM_START(cchasm)
ROM_REGION(0x010000, REGION_CPU1, 0)
ROM_LOAD16_BYTE("chasm.u4", 0x000000, 0x001000, CRC(19244f25) SHA1(79deaae82da8d1b16d05bbac43ba900c4b1d9f26))
ROM_LOAD16_BYTE("chasm.u12", 0x000001, 0x001000, CRC(5d702c7d) SHA1(cbdceed45a1112594fbcbeb6976edc932b32d518))
ROM_LOAD16_BYTE("chasm.u8", 0x002000, 0x001000, CRC(56a7ce8a) SHA1(14c790dcddb78d3b81b5a65fe3529e42c9708273))
ROM_LOAD16_BYTE("chasm.u16", 0x002001, 0x001000, CRC(2e192db0) SHA1(1a8ff983295ab52b5099c089b3142cdc56d28aee))
ROM_LOAD16_BYTE("chasm.u3", 0x004000, 0x001000, CRC(9c71c600) SHA1(900526eaff7483fc478ebfb3f14796ff8fd1d01f))
ROM_LOAD16_BYTE("chasm.u11", 0x004001, 0x001000, CRC(a4eb59a5) SHA1(a7bb3ca8f1f000f224def6342ca9d1eabcb210e6))
ROM_LOAD16_BYTE("chasm.u7", 0x006000, 0x001000, CRC(8308dd6e) SHA1(82ad7c27e9a41af5280ecd975d3530ff2ed27ad4))
ROM_LOAD16_BYTE("chasm.u15", 0x006001, 0x001000, CRC(9d3abf97) SHA1(476d684182d92d66263df82e1b5c4ff24b6814e8))
ROM_LOAD16_BYTE("u2", 0x008000, 0x001000, CRC(4e076ae7) SHA1(a72f5425b256785b810ee5f23917b44f778cfcd3))
ROM_LOAD16_BYTE("u10", 0x008001, 0x001000, CRC(cc9e19ca) SHA1(6c46ec265c2cc0683470ed1df978b96b577c5ca1))
ROM_LOAD16_BYTE("chasm.u6", 0x00a000, 0x001000, CRC(a96525d2) SHA1(1c41bc3bf051cf1830182cbde6fba4e56db7e431))
ROM_LOAD16_BYTE("chasm.u14", 0x00a001, 0x001000, CRC(8e426628) SHA1(2d70a7717b18cc892332b9d5d2de3ceba6c1481d))
ROM_LOAD16_BYTE("u1", 0x00c000, 0x001000, CRC(88b71027) SHA1(49fa676d7838c643d642fbc70579ce29e76ba724))
ROM_LOAD16_BYTE("chasm.u9", 0x00c001, 0x001000, CRC(d90c9773) SHA1(4033f0579f0782db2157f6cbece53b0d74e61d4f))
ROM_LOAD16_BYTE("chasm.u5", 0x00e000, 0x001000, CRC(e4a58b7d) SHA1(0e5f948cd110804e6119fafb4e3fa5904dd1390f))
ROM_LOAD16_BYTE("chasm.u13", 0x00e001, 0x001000, CRC(877e849c) SHA1(bdeb97fcb7488e7f0866dd651204c362d2ec9f4f))

ROM_REGION(0x10000, REGION_CPU2, 0)  // Z80 sound ROM
ROM_LOAD("2732.bin", 0x0000, 0x1000, CRC(715adc4a) SHA1(426be4f3334ef7f2e8eb4d533e64276c30812aa3))
ROM_END

ROM_START(cchasm1)
ROM_REGION(0x010000, REGION_CPU1, 0)
ROM_LOAD16_BYTE("chasm.u4", 0x000000, 0x001000, CRC(19244f25) SHA1(79deaae82da8d1b16d05bbac43ba900c4b1d9f26))
ROM_LOAD16_BYTE("chasm.u12", 0x000001, 0x001000, CRC(5d702c7d) SHA1(cbdceed45a1112594fbcbeb6976edc932b32d518))
ROM_LOAD16_BYTE("chasm.u8", 0x002000, 0x001000, CRC(56a7ce8a) SHA1(14c790dcddb78d3b81b5a65fe3529e42c9708273))
ROM_LOAD16_BYTE("chasm.u16", 0x002001, 0x001000, CRC(2e192db0) SHA1(1a8ff983295ab52b5099c089b3142cdc56d28aee))
ROM_LOAD16_BYTE("chasm.u3", 0x004000, 0x001000, CRC(9c71c600) SHA1(900526eaff7483fc478ebfb3f14796ff8fd1d01f))
ROM_LOAD16_BYTE("chasm.u11", 0x004001, 0x001000, CRC(a4eb59a5) SHA1(a7bb3ca8f1f000f224def6342ca9d1eabcb210e6))
ROM_LOAD16_BYTE("chasm.u7", 0x006000, 0x001000, CRC(8308dd6e) SHA1(82ad7c27e9a41af5280ecd975d3530ff2ed27ad4))
ROM_LOAD16_BYTE("chasm.u15", 0x006001, 0x001000, CRC(9d3abf97) SHA1(476d684182d92d66263df82e1b5c4ff24b6814e8))
ROM_LOAD16_BYTE("chasm.u2", 0x008000, 0x001000, CRC(008b26ef) SHA1(6758d77bf48f466b8692bf7c678a597792d8cfdb))
ROM_LOAD16_BYTE("chasm.u10", 0x008001, 0x001000, CRC(c2c532a3) SHA1(d29d40d42a2f69de0b1e2ee6a32633468a94fd85))
ROM_LOAD16_BYTE("chasm.u6", 0x00a000, 0x001000, CRC(a96525d2) SHA1(1c41bc3bf051cf1830182cbde6fba4e56db7e431))
ROM_LOAD16_BYTE("chasm.u14", 0x00a001, 0x001000, CRC(8e426628) SHA1(2d70a7717b18cc892332b9d5d2de3ceba6c1481d))
ROM_LOAD16_BYTE("chasm.u1", 0x00c000, 0x001000, CRC(e02293f8) SHA1(136757b3c9e0ebfde6c13c57ac52f5fdbf5fd65b))
ROM_LOAD16_BYTE("chasm.u9", 0x00c001, 0x001000, CRC(d90c9773) SHA1(4033f0579f0782db2157f6cbece53b0d74e61d4f))
ROM_LOAD16_BYTE("chasm.u5", 0x00e000, 0x001000, CRC(e4a58b7d) SHA1(0e5f948cd110804e6119fafb4e3fa5904dd1390f))
ROM_LOAD16_BYTE("chasm.u13", 0x00e001, 0x001000, CRC(877e849c) SHA1(bdeb97fcb7488e7f0866dd651204c362d2ec9f4f))

ROM_REGION(0x10000, REGION_CPU2, 0)
ROM_LOAD("2732.bin", 0x0000, 0x1000, CRC(715adc4a) SHA1(426be4f3334ef7f2e8eb4d533e64276c30812aa3))
ROM_END

// ===========================================================================
// AAE Driver Table Entries
// ===========================================================================

AAE_DRIVER_BEGIN(drv_cchasm, "cchasm", "Cosmic Chasm (set 2)")
AAE_DRIVER_ROM(rom_cchasm)
AAE_DRIVER_FUNCS(&init_cchasm, &run_cchasm, &end_cchasm)
AAE_DRIVER_INPUT(input_ports_cchasm)
AAE_DRIVER_SAMPLES(cchasm_samples)
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	// CPU0: MC68000 @ 8 MHz
	// 6840 PTM handles the main game timing interrupt (IRQ4).
	// Vector refresh generates IRQ2. Z80 sends IRQ1 via soundlatch4.
	AAE_CPU_ENTRY(
		/*type*/     CPU_68000,
		/*freq*/     8000000,
		/*div*/      5,
		/*ipf*/      1,
		/*int type*/ INT_TYPE_68K4,
		/*int cb*/   &cchasm_interrupt,
		/*r8*/       CchasmReadByte,
		/*w8*/       CchasmWriteByte,
		/*pr*/       nullptr,
		/*pw*/       nullptr,
		/*r16*/      CchasmReadWord,
		/*w16*/      CchasmWriteWord
	),
	// CPU1: Z80 @ 3.584229 MHz, sound CPU
	// Interrupts come exclusively from the CTC daisy chain (IM2).
	// NMI is fired explicitly by the 68000 I/O write handler.
	AAE_CPU_ENTRY(
		/*type*/     CPU_MZ80,
		/*freq*/     3584229,
		/*div*/      1,
		/*ipf*/      0,
		/*int type*/ INT_TYPE_NONE,
		/*int cb*/   nullptr,
		/*r8*/       CchasmSoundMemRead,
		/*w8*/       CchasmSoundMemWrite,
		/*pr*/       CchasmSoundPortRead,
		/*pw*/       CchasmSoundPortWrite,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(40, 0, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_ROTATE_270)
AAE_DRIVER_SCREEN(1024, 768, 0, 1023, 0, 767)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()

AAE_DRIVER_BEGIN(drv_cchasm1, "cchasm1", "Cosmic Chasm (set 1)")
AAE_DRIVER_ROM(rom_cchasm1)
AAE_DRIVER_FUNCS(&init_cchasm, &run_cchasm, &end_cchasm)
AAE_DRIVER_INPUT(input_ports_cchasm)
AAE_DRIVER_SAMPLES(cchasm_samples)
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	AAE_CPU_ENTRY(
		CPU_68000, 8000000, 5, 1, INT_TYPE_68K4, &cchasm_interrupt,
		CchasmReadByte, CchasmWriteByte, nullptr, nullptr,
		CchasmReadWord, CchasmWriteWord
	),
	AAE_CPU_ENTRY(
		CPU_MZ80, 3584229, 1, 0, INT_TYPE_NONE, nullptr,
		CchasmSoundMemRead, CchasmSoundMemWrite,
		CchasmSoundPortRead, CchasmSoundPortWrite,
		nullptr, nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(40, 0, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_ROTATE_270)
AAE_DRIVER_SCREEN(1024, 768, 0, 1023, 0, 767)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_cchasm)
AAE_REGISTER_DRIVER(drv_cchasm1)

#endif // CCHASM_CPP