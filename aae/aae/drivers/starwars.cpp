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
// THE CODE BELOW IS FROM MAME and COPYRIGHT the MAME TEAM.
//============================================================================

#include "starwars.h"
#include "aae_mame_driver.h"
#include "driver_registry.h"
#include "mame_late_avgdvg.h"
#include "c012294_interface.h"
#include "tms5220.h"
#include "starwars_machine.h"
#include "starwars_snd.h"
#include "slapstic.h"
#include "fuzz_state.h"
#include "config.h"

#include "timer.h"
#include <cstring>   // strcmp - Star Wars vs TomCat set check

#define MASTER_CLOCK (12096000)
#define CLOCK_3KHZ  (MASTER_CLOCK / 4096)

/*************************************
 *
 *	X2212 NVRAM (256 x 4-bit) model
 *
 *	MAME 0.159 (machine/x2212.c) is built around two independent byte
 *	arrays behind the same address decode: a live SRAM the CPU actually
 *	reads/writes, and a non-volatile EEPROM side that only moves in bulk on
 *	a store (SRAM -> EEPROM) or recall (EEPROM -> SRAM) pulse. Only the
 *	EEPROM side is what a real X2212 keeps powered when the cabinet is off,
 *	so it is the half AAE persists to the .nv file (same 0x100-byte size
 *	the old flat `nvram[0x100]` array used, so existing .nv files still
 *	load).
 *
 *	Chip is 256 x 4: writes only keep the low nibble (data & 0x0f) and
 *	reads OR the unused high nibble with the floating/unmapped-bus value.
 *	AAE has no bus-noise model, so - like most X2212-driven MAME games,
 *	which mask with 0x0f anyway - the high nibble is approximated as all
 *	ones (0xf0) rather than modelled precisely.
 *
 *************************************/
static UINT8 nvram_sram[0x100];     // live side, addressed at 0x4500-0x45ff
static UINT8 nvram_eeprom[0x100];   // non-volatile side; this is what gets saved/loaded

static UINT8 x2212_store_state = 0;
static UINT8 x2212_recall_state = 0;

// Unconditional SRAM -> EEPROM copy - x2212_device::store() (the private,
// no-edge-check helper) in MAME 0.159. Used both by the edge-triggered
// store line below and by the MCFG_X2212_ADD_AUTOSAVE implicit store hook
// in starwars_nvram_handler().
static void x2212_store_now()
{
	memcpy(nvram_eeprom, nvram_sram, sizeof(nvram_eeprom));
}

// Store line (active high, rising-edge triggered) - x2212_device::store(state).
void x2212_store_line_w(int state)
{
	if (state != 0 && !x2212_store_state)
		x2212_store_now();
	x2212_store_state = (state != 0);
}

// Recall line (active high, rising-edge triggered) - x2212_device::recall(state).
void x2212_recall_line_w(int state)
{
	if (state != 0 && !x2212_recall_state)
		memcpy(nvram_sram, nvram_eeprom, sizeof(nvram_sram));
	x2212_recall_state = (state != 0);
}

int bank1 = 0x06000;
int bank2 = 0x0a000;

/*************************************
 *
 *	NVRAM handler
 *
 *************************************/

// 0x4500-0x45ff: x2212 read/write (SRAM side only - see x2212_device::write/
// read in MAME 0.159). Write keeps only the low nibble; read reports the
// unused high nibble as all-ones (see the X2212 model comment above).
WRITE_HANDLER(nvram_w)
{
	nvram_sram[address] = data & 0x0f;
}

READ_HANDLER(nvram_r)
{
	return (nvram_sram[address] & 0x0f) | 0xf0;
}

// AAE_DRIVER_NVRAM handler for all four Star Wars-hardware ROM sets. Wraps
// generic_nvram_handler() (which persists nvram_eeprom - the region
// registered via nvram_set_region() in init_starwars()/init_esb() below) to
// add the two bits of x2212 behaviour generic_nvram_handler can't express on
// its own:
//
//   - MCFG_X2212_ADD_AUTOSAVE wires /STORE to fire on power-down, so MAME's
//     x2212_device::nvram_write() does an implicit store() (SRAM -> EEPROM)
//     before writing the file. AAE calls this handler with read_or_write==1
//     to save, so that's where the implicit store is hooked in.
//   - x2212_device::nvram_default()/nvram_read() always reset the *live*
//     SRAM side to all-ones on power-up, independent of whatever the EEPROM
//     side loads as. generic_nvram_handler() only knows about the EEPROM
//     array, so nvram_sram is reset here on every load/first-boot call
//     (read_or_write==0), and both x2212 lines are dropped back to their
//     inactive/idle state to match a freshly powered-on chip.
static void starwars_nvram_handler(void* file, int read_or_write)
{
	if (read_or_write)
	{
		x2212_store_now();
		generic_nvram_handler(file, 1);
	}
	else
	{
		generic_nvram_handler(file, 0);   // loads nvram_eeprom, or fills 0xff on first boot
		memset(nvram_sram, 0xff, sizeof(nvram_sram));
		x2212_store_state = 0;
		x2212_recall_state = 0;
	}
}

////////////////////////////////////////////////////////////// SLAPSTIC
static UINT8* slapstic_source;
static UINT8* slapstic_base;
static UINT8 current_bank;

static UINT8 is_esb;

// The slapstic encryption took me forever to figure out, even using a custom compiled MAME version. I guess I am just dense, it took me a week to
// understand that only the last memory read and the change_cpu function should be sent to these routines. Not the opcode reads, not any imtermediate reads,
// Just the final memory reads for each opcode Thank you RTS (0x39) for helping drive this home.
// Not understanding how MAME does it's banking really hurt me here.

void bank_switch_read(int address, int result)
{
	int new_bank = slapstic_tweak(address);

	/* update for the new bank */
	if (new_bank != current_bank)
	{
		current_bank = new_bank;
		memcpy(slapstic_base, &slapstic_source[current_bank * 0x2000], 0x2000);
	}
}

READ_HANDLER(esb_slapstic_r)
{
	int result = slapstic_base[address];

	// Only the final DATA read of an opcode drives the slapstic, never the
	// instruction-stream fetches (opcode / postbyte / immediate). The core now
	// reports the access kind per-CPU, replacing the old global 'slapstic_en'.
	if (!m_cpu_6809[0]->in_opcode_fetch()) bank_switch_read(address, result);

	return result;
}

WRITE_HANDLER(esb_slapstic_w)
{
	// Default to the current bank, not 0: if this were ever reached during an
	// opcode fetch (in_opcode_fetch() true), new_bank would stay at its
	// initial value and force bank 0 - matching MAME's esb_slapstic_tweak,
	// which always tweaks off the live bank rather than a hardcoded default.
	int new_bank = current_bank;

	if (!m_cpu_6809[0]->in_opcode_fetch())
		new_bank = slapstic_tweak(address);

	/* update for the new bank */
	if (new_bank != current_bank)
	{
		current_bank = new_bank;
		memcpy(slapstic_base, &slapstic_source[current_bank * 0x2000], 0x2000);
	}
}

static int esb_setopbase(int address)
{
	int prevpc = cpu_getppc();

	/*
	 *	This is a slightly ugly kludge for Empire Strikes Back because it jumps
	 *	directly to code in the slapstic.
	 */

	// A hardware interrupt (NMI/FIRQ/IRQ) being TAKEN is not a program jump:
	// m_PPC (prevpc) is only updated at the start of an instruction, so if the
	// interrupt lands while the CPU is executing inside $8000-$9FFF, prevpc
	// would still read as "inside the region" and the (address & 0xe000)==0x8000
	// check below would never fire for the vector itself (interrupt vectors
	// live outside the region) -- but the "jumping out" branch below WOULD
	// fire and inject a spurious tweak keyed off the interrupted instruction's
	// address, which is not where program flow actually went. Ignore the
	// interrupt-entry notify entirely; nothing about the slapstic banking
	// changes just because an interrupt was taken.
	if (m_cpu_6809[0]->pc_change_reason() == cpu_m6809::PcChangeReason::Interrupt)
		return address;

	// RTI resumes the flow the interrupt suspended; it is not itself "jumping
	// into" wherever that flow happens to resume. Skip the region-entry tweak
	// below for the return address, but still fall through to the region-exit
	// tweak if the RTI instruction itself executed from inside the region
	// (same as a normal jump out of the region would).
	bool is_rti = (m_cpu_6809[0]->pc_change_reason() == cpu_m6809::PcChangeReason::Rti);

	 /* if we're jumping into the slapstic region, tweak the new PC */
	if (!is_rti && (address & 0xe000) == 0x8000)
	{
		// This fires from the core's PC-change hook, where in_opcode_fetch() is
		// still true, so call bank_switch_read directly (the read gate would
		// otherwise suppress it). This is the region-entry equivalent of the old
		// change_pc() forcing slapstic_en=1 before the setopbase call.
		bank_switch_read(address & 0x1fff, 0);
		return -1;
	}

	/* if we're jumping out of the slapstic region, tweak the previous PC */
	else if ((prevpc & 0xe000) == 0x8000)
	{
		if (prevpc != 0x8080 && prevpc != 0x8090 && prevpc != 0x80a0 && prevpc != 0x80b0)
		{
			// Region-exit: force the bank switch directly (see note above).
			bank_switch_read(prevpc & 0x1fff, 0);
		}
	}

	return address;
}

/// ////////////////////////////////////////////////////////////////////////

/* VGGO/VGRST are handled directly by mame_late_avgdvg's avgdvg_go_w /
 * avgdvg_reset_w, referenced in the memory maps below. */

/* Read from ROM 0. Use bankaddress as base address */
READ_HANDLER(BANK1_R)
{
	return Machine->memory_region[0][bank1 + address];
}

READ_HANDLER(BANK2_R)
{
	return Machine->memory_region[0][bank2 + address];
}

/**********************************************************/
/************** Write Handlers ****************************/
/**********************************************************/

WRITE_HANDLER(irqclr)
{
	// A write anywhere in $4660-$467f clears the latched main IRQ (MAME's
	// irq_ack_w). The core lowers the line itself once the interrupt is
	// taken, but the game also clears it explicitly from its IRQ handler.
	m_cpu_6809[0]->irq_line(false);
}

/********************************************************/

// Periodic main-CPU IRQ at the hardware rate, 3 kHz / 12 = 246.09 Hz (6144
// cycles), as in MAME 0.159 and current MAME. Atari's source agrees: the
// handler's game-frame divider is commented "12.*4.2MS==>50. MS, 20 PER
// SECOND". The game paces itself on the picture: the IRQ handler (DOVG,
// $F034) restarts the vector generator no sooner than 6 IRQs after the last
// VGGO and only once VGHALT is set, so a heavy display list stretches a
// game pass past 12 IRQs. An earlier 180 Hz setting only approximated that
// stretch; measured against the Star Wars disassembly's emulator twin, 246 Hz
// here gives the same passes per second and vector draw times, scene for
// scene. The scheduler no longer injects this (CPU0 now runs with ipf 0 /
// no int callback - see the driver entries below), so a real timer drives
// the IRQ line instead, held until irqclr() above clears it. The IRQ is
// timer-driven so frame rate does not affect game speed.
static int main_irq_timer;

static void starwars_irq_gen(int param)
{
	m_cpu_6809[0]->irq_line(true);
}

// Shared by init_starwars() and init_esb(). timer_init() (Step 13 of
// run_game, aae_emulator.cpp) wipes the whole timer store before every
// driver init runs, so allocating unconditionally here can never leak or
// double-arm a timer across game (re)loads - the previous game's slot, if
// any, is already gone. Same pattern as avgdvg_init()'s vg_halt_timer /
// vg_run_timer.
static void starwars_start_irq_timer()
{
	main_irq_timer = timer_alloc(starwars_irq_gen);
	// 1512000 / 246.09 = 6144 cycles (3 kHz / 12)
	timer_adjust(main_irq_timer, TIME_IN_CYCLES(6144, 0), 0, TIME_IN_CYCLES(6144, 0));
}

// CPU0 reset callback (registered below via post_cpu_init), matching MAME's
// starwars_state::machine_reset(): rebank to entry 0, reset the math box,
// and clear the main IRQ line; ESB additionally resets the slapstic. The
// core's own reset() already ran (and already called esb_setopbase via
// notify_pc_change) before this fires, so this just re-establishes the
// reset state on top of that.
static void starwars_machine_reset()
{
	bank1 = 0x06000;
	bank2 = 0x0a000;
	swmathbox_reset();
	m_cpu_6809[0]->irq_line(false);

	// MAME resets the 6532 RIOT (and the main<->sound latches) only on a
	// machine reset, not on the sound board's own soundrst latch-clear
	// (0x46e0) - see starwars_snd.cpp soundrst().
	starwars_riot_reset();

	if (is_esb)
	{
		slapstic_reset();
		current_bank = slapstic_bank();
		memcpy(slapstic_base, &slapstic_source[current_bank * 0x2000], 0x2000);
	}
}

static void starwars_post_cpu_init(int cpunum)
{
	cpu_set_reset_callback(cpunum, starwars_machine_reset);
}

void end_starwars()
{
	starwars_sh_stop();
}

/************ Death Star explosion FUZZ *******************/

// Star Wars runs a phase state machine: the main loop reads PHASE and jumps
// through the TPHASE table (dispatcher at ROM $6040 - LDA $41 / CMPA #$3D /
// LSLA / LDX #$6044 / JSR [A,X], with DP = $48 set at $600B). Phases $11..$16
// are the Death Star explosion, and inside the last one the game walks XP.PHS
// 1..4 - red, blue, white-from-centre, expanding white rings - with XP.CNT
// counting progress through each. Driving the effect from those bytes means no
// frame constant is needed and the effect stops when the game says it stops.
//
// Addresses are from Atari's own load maps (WSROOT.MAP) and were verified
// byte-for-byte against the shipping rev-2 ROM; PHASE = $11 is written from
// exactly one place, $6BC8 inside PHEBS, i.e. hitting the trench exhaust port.
static constexpr UINT16 SW_PHASE = 0x4841;   // current game phase
static constexpr UINT16 SW_XP_PHS = 0x48a1;  // 0 = done, 1..4 = XP.PH0..XP.PH3
static constexpr UINT16 SW_XP_CNT = 0x489f;  // 16-bit progress within a sub-phase

// Only the two Star Wars sets run this program. ESB is a different game with a
// different memory layout and no Death Star, and TomCat shares init_starwars()
// but is different software entirely.
static bool sw_fuzz_game = false;
static float sw_fuzz_level = 0.0f;

// Target fuzz for this frame, straight from the game's own explosion state.
static float sw_fuzz_target()
{
	const UINT8* ram = Machine->memory_region[0];
	const UINT8 phase = ram[SW_PHASE];

	// $13..$16 = DX2 + DX3. DX1 ($11/$12), where the Death Star scales up to
	// fill the screen, is deliberately left clean so the whiteout reads as the
	// detonation rather than the approach.
	if (phase < 0x13 || phase > 0x16) return 0.0f;

	// Two independent bytes have to agree before the effect arms. XP.PHS is 0
	// on the frame the explosion finishes.
	const UINT8 sub = ram[SW_XP_PHS];
	if (sub < 1 || sub > 4) return 0.0f;

	// Full level whenever the explosion is live. The visible ramp comes from
	// the game itself: it draws ever denser, brighter circle stacks and the
	// shader blows out whatever is locally bright, so the effect builds and
	// fades with the drawn content. The one-pole smoother in run_starwars()
	// eases the arm/disarm edges.
	return 1.0f;
}

void run_starwars()
{
	starwars_sh_update();

	// F7 fires the Death Star explosion on demand - poking PHASE is exactly
	// what the ROM does at $6BC8 when the exhaust port is hit, so everything
	// downstream (music, phase flow, next act) runs as if the shot landed.
	if (sw_fuzz_game && osd_key_pressed_memory(OSD_KEY_F7))
	{
		Machine->memory_region[0][SW_PHASE] = 0x11;
		LOG_INFO("starwars: F7 - firing the Death Star explosion (PHASE=0x11)");
	}

	const float target = (sw_fuzz_game && config.starwars_fuzz) ? sw_fuzz_target() : 0.0f;

	// One-pole smoothing. The game's counters step coarsely at 30Hz, so a fast
	// attack keeps the flash punchy while a slower release makes the fade-out
	// read smoothly instead of stair-stepping.
	const float k = (target > sw_fuzz_level) ? 0.55f : 0.22f;
	sw_fuzz_level += (target - sw_fuzz_level) * k;
	if (sw_fuzz_level < 0.002f) sw_fuzz_level = 0.0f;

	Fuzz_Set(sw_fuzz_level);
}

/////////////////////////////////////////////////

// Star Wars READ memory map
MEM_READ(starwars_readmem)
MEM_ADDR(0x4300, 0x431f, ip_port_0_r) // Memory mapped input port 0
MEM_ADDR(0x4320, 0x433f, starwars_input_1_r) // Memory mapped input port 1
MEM_ADDR(0x4340, 0x435f, ip_port_2_r) // DIP switches bank 0
MEM_ADDR(0x4360, 0x437f, ip_port_3_r) // DIP switches bank 1
MEM_ADDR(0x4380, 0x439f, starwars_adc_r)   // ADC read
MEM_ADDR(0x4400, 0x4400, main_read_r) //Sound
MEM_ADDR(0x4401, 0x4401, main_ready_flag_r) //Sound
MEM_ADDR(0x4500, 0x45ff, nvram_r) // nv_ram
MEM_ADDR(0x4700, 0x4700, swmathbx_reh_r)
MEM_ADDR(0x4701, 0x4701, swmathbx_rel_r)
MEM_ADDR(0x4703, 0x4703, swmathbx_prng_r) // pseudo random number generator
MEM_ADDR(0x4800, 0x5fff, MRA_RAM)
MEM_ADDR(0x6000, 0x7fff, BANK1_R)
//MEM_ADDR(0x8000, 0xffff, MRA_ROM)  // rest of main_rom
MEM_END

// Star Wars WRITE memory map
MEM_WRITE(starwars_writemem)
MEM_ADDR(0x0000, 0x2fff, MWA_RAM) // &vectorram
MEM_ADDR(0x3000, 0x3fff, MWA_ROM)  // vector_rom
MEM_ADDR(0x4400, 0x4400, main_wr_w)  //Sound
MEM_ADDR(0x4500, 0x45ff, nvram_w) // nv_ram
MEM_ADDR(0x4600, 0x461f, avgdvg_go_w)  // evggo(mine) or vg2_go
MEM_ADDR(0x4620, 0x463f, avgdvg_reset_w) // evgres(mine) or vg_reset
MEM_ADDR(0x4640, 0x465f, watchdog_reset_w) //  (wdclr) Watchdog clear
MEM_ADDR(0x4660, 0x467f, irqclr)  // clear periodic interrupt
MEM_ADDR(0x4680, 0x469f, starwars_out_w)
MEM_ADDR(0x46a0, 0x46bf, starwars_nstore_w)
MEM_ADDR(0x46c0, 0x46c2, starwars_adc_select_w)	// Selects which a-d control port (0-3) will be read
MEM_ADDR(0x46e0, 0x46e0, soundrst) //Sound
MEM_ADDR(0x4700, 0x4707, swmathbx_w)
MEM_ADDR(0x4800, 0x5fff, MWA_RAM) 	/* CPU and Math RAM */
MEM_ADDR(0x6000, 0xffff, MWA_ROM)		/* main_rom */
MEM_END

MEM_READ(empire_readmem)
MEM_ADDR(0x4300, 0x431f, ip_port_0_r) // Memory mapped input port 0
MEM_ADDR(0x4320, 0x433f, starwars_input_1_r) // Memory mapped input port 1
MEM_ADDR(0x4340, 0x435f, ip_port_2_r) // DIP switches bank 0
MEM_ADDR(0x4360, 0x437f, ip_port_3_r) // DIP switches bank 1
MEM_ADDR(0x4380, 0x439f, starwars_adc_r)   // ADC read
MEM_ADDR(0x4400, 0x4400, main_read_r) //Sound
MEM_ADDR(0x4401, 0x4401, main_ready_flag_r) //Sound
MEM_ADDR(0x4500, 0x45ff, nvram_r) // nv_ram
MEM_ADDR(0x4700, 0x4700, swmathbx_reh_r)
MEM_ADDR(0x4701, 0x4701, swmathbx_rel_r)
MEM_ADDR(0x4703, 0x4703, swmathbx_prng_r) // pseudo random number generator
MEM_ADDR(0x4800, 0x5fff, MRA_RAM)		/* CPU and Math RAM */
MEM_ADDR(0x6000, 0x7fff, BANK1_R)	    /* banked ROM */
MEM_ADDR(0x8000, 0x9fff, esb_slapstic_r)//, &slapstic_area },
MEM_ADDR(0xa000, 0xffff, BANK2_R)		/* banked ROM */
/* Dummy entry to set up the slapstic */
MEM_ADDR(0x14000, 0x1bfff, MRA_NOP) //, &atarigen_slapstic },
MEM_END

MEM_WRITE(empire_writemem)
MEM_ADDR(0x0000, 0x2fff, MWA_RAM)    //&vectorram
MEM_ADDR(0x3000, 0x3fff, MWA_ROM)    //vector_rom
MEM_ADDR(0x4400, 0x4400, main_wr_w)  //Sound
MEM_ADDR(0x4500, 0x45ff, nvram_w)    //nv_ram
MEM_ADDR(0x4600, 0x461f, avgdvg_go_w)      // evggo(mine) or vg2_go
MEM_ADDR(0x4620, 0x463f, avgdvg_reset_w)   // evgres(mine) or vg_reset
MEM_ADDR(0x4640, 0x465f, watchdog_reset_w) //  (wdclr) Watchdog clear
MEM_ADDR(0x4660, 0x467f, irqclr)  // clear periodic interrupt
MEM_ADDR(0x4680, 0x469f, starwars_out_w)
MEM_ADDR(0x46a0, 0x46bf, starwars_nstore_w)
MEM_ADDR(0x46c0, 0x46c2, starwars_adc_select_w)	// Selects which a-d control port (0-3) will be read
MEM_ADDR(0x46e0, 0x46e0, soundrst) //Sound
MEM_ADDR(0x4700, 0x4707, swmathbx_w)
MEM_ADDR(0x4800, 0x5fff, MWA_RAM) 	/* CPU and Math RAM */
MEM_ADDR(0x6000, 0x7fff, MWA_ROM)		/* banked ROM */
MEM_ADDR(0x8000, 0x9fff, esb_slapstic_w)		/* slapstic write */
MEM_ADDR(0xa000, 0xffff, MWA_ROM)		/* banked ROM */
MEM_END

// Star Wars Sound READ memory map
MEM_READ(starwars_audio_readmem)
MEM_ADDR(0x0800, 0x0fff, sin_r) // SIN Read
MEM_ADDR(0x1000, 0x107f, MRA_RAM)  // 6532 RAM
MEM_ADDR(0x1080, 0x109f, riot_r)   // 6532 RIOT registers
MEM_ADDR(0x4000, 0xbfff, MRA_ROM) // sound roms
MEM_ADDR(0xc000, 0xffff, MRA_ROM) // load last rom twice
MEM_END

// Star Wars sound WRITE memory map
MEM_WRITE(starwars_audio_writemem)
MEM_ADDR(0x0000, 0x07ff, sout_w)
MEM_ADDR(0x1000, 0x107f, MWA_RAM) // 6532 ram
MEM_ADDR(0x1080, 0x109f, riot_w)   // 6532 RIOT registers
MEM_ADDR(0x1800, 0x183f, quadpokey_w)//starwars_pokey_sound_w },
MEM_ADDR(0x2000, 0x27ff, MWA_RAM) // program RAM
MEM_ADDR(0x4000, 0xbfff, MWA_ROM) // sound rom
MEM_ADDR(0xc000, 0xffff, MWA_ROM) // sound rom again, for intvecs
MEM_END

int init_esb()
{
	LOG_INFO("Calling ESB Init");
	is_esb = 1;
	// Register the EEPROM side of the x2212 for load/save; fill 0xff on
	// first boot (no .nv file yet) to match x2212_device::nvram_default()
	// with no default-data region attached (MAME 0.159 does not supply one
	// for "x2212" in drivers/starwars.c).
	nvram_set_region(nvram_eeprom, 0x100, 0xff);
	/* Set up the slapstic */
	slapstic_init(101);
	slapstic_source = &memory_region(REGION_CPU1)[0x14000];
	slapstic_base = &memory_region(REGION_CPU1)[0x08000];
	cpu_setOPbaseoverride(esb_setopbase);

	slapstic_reset();
	current_bank = slapstic_bank();
	memcpy(slapstic_base, &slapstic_source[current_bank * 0x2000], 0x2000);

	starwars_sh_start();

	swmathbox_init();
	swmathbox_timer_init();
	avg_start_starwars();
	starwars_start_irq_timer();
	// RIOT lives on the audio CPU (CPU1) and must be created after
	// timer_init() has wiped the timer store for this driver load - same
	// rule as starwars_start_irq_timer() above.
	starwars_snd_init_riot(1, 1512000);

	// ESB is a different program: the Star Wars phase addresses mean nothing
	// here, and it has no Death Star to explode.
	sw_fuzz_game = false;
	sw_fuzz_level = 0.0f;

	return 0;
}

/////////////////// MAIN() for program ///////////////////////////////////////////////////
int init_starwars(void)
{
	is_esb = 0;
	// See init_esb() above for why fill is 0xff, not 0x00.
	nvram_set_region(nvram_eeprom, 0x100, 0xff);
	starwars_sh_start();
	swmathbox_init();
	swmathbox_timer_init();
	avg_start_starwars();
	starwars_start_irq_timer();
	// RIOT lives on the audio CPU (CPU1) and must be created after
	// timer_init() has wiped the timer store for this driver load - same
	// rule as starwars_start_irq_timer() above. TomCat shares this init path
	// and has no sound ROMs, but wiring the (idle) RIOT up is harmless.
	starwars_snd_init_riot(1, 1512000);

	// This entry is shared with TomCat, which is different software, so match
	// on the set name rather than assuming the phase addresses apply. All
	// three Star Wars dumps (rev 2, rev 1, and the oldest set) run the same
	// program and phase table, just from different ROM revisions.
	sw_fuzz_game = (strcmp(Machine->gamedrv->name, "starwars") == 0
	             || strcmp(Machine->gamedrv->name, "starwars1") == 0
	             || strcmp(Machine->gamedrv->name, "starwarso") == 0);
	sw_fuzz_level = 0.0f;

	LOG_INFO("Star Wars Init Completed");
	return 0;
}

INPUT_PORTS_START(starwars)
PORT_START("IN0")//// IN0
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_COIN2)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_COIN3)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_TILT)
PORT_BITX(0x10, 0x10, IPT_DIPSWITCH_NAME | IPF_TOGGLE, "Service Mode", OSD_KEY_F2, IP_JOY_NONE)
PORT_DIPSETTING(0x10, "Off")
PORT_DIPSETTING(0x00, "On")
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_UNUSED)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_START2)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_START1)

PORT_START("IN1")	// IN1
PORT_BIT(0x03, IP_ACTIVE_HIGH, IPT_UNUSED)
PORT_BITX(0x04, IP_ACTIVE_LOW, IPT_SERVICE, "Diagnostic Step", OSD_KEY_F1, IP_JOY_NONE)
PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_UNUSED)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_BUTTON2)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_BUTTON1)
// Bit 6 is VG_HALT - see machine/starwars.c
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_UNUSED)
// Bit 7 is MATH_RUN - see machine/starwars.c
PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_UNUSED)

PORT_START("DSW")	// DSW0
PORT_DIPNAME(0x03, 0x00, "Shields")
PORT_DIPSETTING(0x00, "6")
PORT_DIPSETTING(0x01, "7")
PORT_DIPSETTING(0x02, "8")
PORT_DIPSETTING(0x03, "9")
PORT_DIPNAME(0x0c, 0x04, "Difficulty")
PORT_DIPSETTING(0x00, "Easy")
PORT_DIPSETTING(0x04, "Moderate")
PORT_DIPSETTING(0x08, "Hard")
PORT_DIPSETTING(0x0c, "Hardest")
PORT_DIPNAME(0x30, 0x00, "Bonus Shields")
PORT_DIPSETTING(0x00, "0")
PORT_DIPSETTING(0x10, "1")
PORT_DIPSETTING(0x20, "2")
PORT_DIPSETTING(0x30, "3")
PORT_DIPNAME(0x40, 0x00, "Attract Music")
PORT_DIPSETTING(0x00, "On")
PORT_DIPSETTING(0x40, "Off")
PORT_DIPNAME(0x80, 0x80, "Game Mode")
PORT_DIPSETTING(0x00, "Freeze")
PORT_DIPSETTING(0x80, "Normal")

PORT_START("DS1")// DSW1
PORT_DIPNAME(0x03, 0x02, "Credits/Coin")
PORT_DIPSETTING(0x00, "Free Play")
PORT_DIPSETTING(0x01, "2")
PORT_DIPSETTING(0x02, "1")
PORT_DIPSETTING(0x03, "1/2")
PORT_BIT(0xfc, IP_ACTIVE_HIGH, IPT_UNKNOWN)

PORT_START("IN4")	// IN4
PORT_ANALOG(0xff, 0x80, IPT_AD_STICK_Y, 70, 30, 0, 255)

PORT_START("IN5")	// IN5
PORT_ANALOG(0xff, 0x80, IPT_AD_STICK_X, 50, 30, 0, 255)
INPUT_PORTS_END

INPUT_PORTS_START(esb)
PORT_START("IN0")	/* IN0 */
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_COIN2)
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_COIN1)
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_COIN3)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_TILT)
PORT_SERVICE(0x10, IP_ACTIVE_LOW)
PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_UNUSED)
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_BUTTON4)
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_BUTTON1)

PORT_START("IN1")	/* IN1 */
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_UNUSED)
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_UNUSED)
PORT_BITX(0x04, IP_ACTIVE_LOW, IPT_SERVICE, "Diagnostic Step", OSD_KEY_F1, IP_JOY_NONE)
PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_UNUSED)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_BUTTON3)
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_BUTTON2)
/* Bit 6 is VG_HALT - see machine/starwars.c */
PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_UNUSED)
/* Bit 7 is MATH_RUN - see machine/starwars.c */
PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_UNUSED)

PORT_START("DSW0")	/* DSW0 */
PORT_DIPNAME(0x03, 0x03, "Starting Shields")
PORT_DIPSETTING(0x01, "2")
PORT_DIPSETTING(0x00, "3")
PORT_DIPSETTING(0x03, "4")
PORT_DIPSETTING(0x02, "5")
PORT_DIPNAME(0x0c, 0x0c, DEF_STR(Difficulty))
PORT_DIPSETTING(0x08, "Easy")
PORT_DIPSETTING(0x0c, "Moderate")
PORT_DIPSETTING(0x00, "Hard")
PORT_DIPSETTING(0x04, "Hardest")
PORT_DIPNAME(0x30, 0x30, "Jedi-Letter Mode")
PORT_DIPSETTING(0x00, "Level Only")
PORT_DIPSETTING(0x10, "Level")
PORT_DIPSETTING(0x20, "Increment Only")
PORT_DIPSETTING(0x30, "Increment")
PORT_DIPNAME(0x40, 0x40, DEF_STR(Demo_Sounds))
PORT_DIPSETTING(0x00, DEF_STR(Off))
PORT_DIPSETTING(0x40, DEF_STR(On))
PORT_DIPNAME(0x80, 0x80, "Freeze")
PORT_DIPSETTING(0x80, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))

PORT_START("DSW1")	/* DSW1 */
PORT_DIPNAME(0x03, 0x02, DEF_STR(Coinage))
PORT_DIPSETTING(0x03, DEF_STR(2C_1C))
PORT_DIPSETTING(0x02, DEF_STR(1C_1C))
PORT_DIPSETTING(0x01, DEF_STR(1C_2C))
PORT_DIPSETTING(0x00, DEF_STR(Free_Play))
PORT_DIPNAME(0x0c, 0x00, DEF_STR(Coin_B))
PORT_DIPSETTING(0x00, "*1")
PORT_DIPSETTING(0x04, "*4")
PORT_DIPSETTING(0x08, "*5")
PORT_DIPSETTING(0x0c, "*6")
PORT_DIPNAME(0x10, 0x00, DEF_STR(Coin_A))
PORT_DIPSETTING(0x00, "*1")
PORT_DIPSETTING(0x10, "*2")
PORT_DIPNAME(0xe0, 0xe0, "Bonus Coinage")
PORT_DIPSETTING(0x20, "2 gives 1")
PORT_DIPSETTING(0x60, "4 gives 2")
PORT_DIPSETTING(0xa0, "3 gives 1")
PORT_DIPSETTING(0x40, "4 gives 1")
PORT_DIPSETTING(0x80, "5 gives 1")
PORT_DIPSETTING(0xe0, "None")
/* 0xc0 and 0x00 None */
PORT_START("IN4")	/* IN4 */
PORT_ANALOG(0xff, 0x80, IPT_AD_STICK_Y, 70, 30, 0, 255)

PORT_START("IN5")	// IN5
PORT_ANALOG(0xff, 0x80, IPT_AD_STICK_X, 50, 30, 0, 255)
INPUT_PORTS_END

ROM_START(starwars)
ROM_REGION(0x14000, REGION_CPU1, 0)
// Vector Rom
ROM_LOAD("136021-105.1l", 0x3000, 0x1000, CRC(538e7d2f) SHA1(032c933fd94a6b0b294beee29159a24494ae969b))
// Banked Roms

ROM_LOAD("136021.214.1f", 0x6000, 0x2000, CRC(4f1876e) SHA1(c1d3637cb31ece0890c25f6122d6bcd27e6ffe0c))
ROM_CONTINUE(0x10000, 0x2000)
ROM_LOAD("136021.102.1hj", 0x8000, 0x2000, CRC(f725e344) SHA1(f8943b67f2ea032ab9538084756ba86f892be5ca))
ROM_LOAD("136021.203.1jk", 0xa000, 0x2000, CRC(f6da0a00) SHA1(dd53b643be856787bbc4da63e5eb132f98f623c3))
ROM_LOAD("136021.104.1kl", 0xc000, 0x2000, CRC(7e406703) SHA1(981b505d6e06d7149f8bcb3e81e4d0c790f2fc86))
ROM_LOAD("136021.206.1m", 0xe000, 0x2000, CRC(c7e51237) SHA1(4960f4446271316e3f730eeb2531dbc702947395))

ROM_REGION(0x10000, REGION_CPU2, 0) // Audio
ROM_LOAD("136021-107.1jk", 0x4000, 0x2000, CRC(dbf3aea2) SHA1(c38661b2b846fe93487eef09ca3cda19c44f08a0))
ROM_RELOAD(0xc000, 0x2000) /* Copied again for */
ROM_LOAD("136021-208.1h", 0x6000, 0x2000, CRC(e38070a8) SHA1(c858ae1702efdd48615453ab46e488848891d139))
ROM_RELOAD(0xe000, 0x2000) /* proper int vecs */

ROM_REGION(0x1000, REGION_GFX1, 0)
// Mathbox
ROM_LOAD("136021-110.7h", 0x0000, 0x0400, CRC(810e040e) SHA1(d247cbb0afb4538d5161f8ce9eab337cdb3f2da4))
ROM_LOAD("136021-111.7j", 0x0400, 0x0400, CRC(ae69881c) SHA1(f3420c6e15602956fd94982a5d8d4ddd015ed977))
ROM_LOAD("136021-112.7k", 0x0800, 0x0400, CRC(ecf22628) SHA1(4dcf5153221feca329b8e8d199bd4fc00b151d9c))
ROM_LOAD("136021-113.7l", 0x0c00, 0x0400, CRC(83febfde) SHA1(e13541b09d1724204fdb171528e9a1c83c799c1c))

ROM_REGION(0x100, REGION_PROMS, 0)
ROM_LOAD("136021-109.4b", 0x0000, 0x0100, CRC(82fc3eb2) SHA1(184231c7baef598294860a7d2b8a23798c5c7da6)) /* AVG PROM */
ROM_END

// MAME 0.286 "Star Wars (set 2)": same bank layout as the rev-2 "starwars"
// set above, but the base main-CPU ROM is 136021.114.1f (CRC e75ff867), not
// 136021.214.1f (CRC 04f1876e, that's the rev-2 ROM loaded by "starwars").
// AAE previously (wrongly) loaded 136021.214.1f here too, making this set a
// silent duplicate of "starwars" - fixed to match MAME's dump.
ROM_START(starwars1)
ROM_REGION(0x14000, REGION_CPU1, 0)
ROM_LOAD("136021-105.1l", 0x3000, 0x1000, CRC(538e7d2f) SHA1(032c933fd94a6b0b294beee29159a24494ae969b))  /* 3000-3fff is 4k vector rom */
ROM_LOAD("136021.114.1f", 0x6000, 0x2000, CRC(e75ff867) SHA1(3a40de920c31ffa3c3e67f3edf653b79fcc5ddd7))
ROM_CONTINUE(0x10000, 0x2000)
ROM_LOAD("136021.102.1hj", 0x8000, 0x2000, CRC(f725e344) SHA1(f8943b67f2ea032ab9538084756ba86f892be5ca)) /*  8k ROM 1 bank */
ROM_LOAD("136021.203.1jk", 0xa000, 0x2000, CRC(f6da0a00) SHA1(dd53b643be856787bbc4da63e5eb132f98f623c3)) /*  8k ROM 2 bank */
ROM_LOAD("136021.104.1kl", 0xc000, 0x2000, CRC(7e406703) SHA1(981b505d6e06d7149f8bcb3e81e4d0c790f2fc86)) /*  8k ROM 3 bank */
ROM_LOAD("136021.206.1m", 0xe000, 0x2000, CRC(c7e51237) SHA1(4960f4446271316e3f730eeb2531dbc702947395))  /*  8k ROM 4 bank */

ROM_REGION(0x10000, REGION_CPU2, 0) /* Really only 32k, but it looks like 64K */
ROM_LOAD("136021-107.1jk", 0x4000, 0x2000, CRC(dbf3aea2) SHA1(c38661b2b846fe93487eef09ca3cda19c44f08a0)) /* Sound ROM 0 */
ROM_RELOAD(0xc000, 0x2000)
ROM_LOAD("136021-208.1h", 0x6000, 0x2000, CRC(e38070a8) SHA1(c858ae1702efdd48615453ab46e488848891d139))  /* Sound ROM 1 */
ROM_RELOAD(0xe000, 0x2000)

ROM_REGION(0x1000, REGION_GFX1, 0)
// Mathbox
ROM_LOAD("136021-110.7h", 0x0000, 0x0400, CRC(810e040e) SHA1(d247cbb0afb4538d5161f8ce9eab337cdb3f2da4))
ROM_LOAD("136021-111.7j", 0x0400, 0x0400, CRC(ae69881c) SHA1(f3420c6e15602956fd94982a5d8d4ddd015ed977))
ROM_LOAD("136021-112.7k", 0x0800, 0x0400, CRC(ecf22628) SHA1(4dcf5153221feca329b8e8d199bd4fc00b151d9c))
ROM_LOAD("136021-113.7l", 0x0c00, 0x0400, CRC(83febfde) SHA1(e13541b09d1724204fdb171528e9a1c83c799c1c))

ROM_REGION(0x100, REGION_PROMS, 0)
ROM_LOAD("136021-109.4b", 0x0000, 0x0100, CRC(82fc3eb2) SHA1(184231c7baef598294860a7d2b8a23798c5c7da6)) /* AVG PROM */
ROM_END

// MAME 0.286 "Star Wars (set 3)", the oldest dump: clone of "starwars".
// Same bank 0 content as starwars1 (136021-114.1f, CRC e75ff867) but under a
// distinct MAME dump filename, and bank 2 (136021-103.1jk, CRC 3fde9ccb) is
// the one ROM in the whole set that actually differs from starwars/starwars1.
ROM_START(starwarso)
ROM_REGION(0x14000, REGION_CPU1, 0)
ROM_LOAD("136021-105.1l", 0x3000, 0x1000, CRC(538e7d2f) SHA1(032c933fd94a6b0b294beee29159a24494ae969b))  /* 3000-3fff is 4k vector rom */
ROM_LOAD("136021-114.1f", 0x6000, 0x2000, CRC(e75ff867) SHA1(3a40de920c31ffa3c3e67f3edf653b79fcc5ddd7))
ROM_CONTINUE(0x10000, 0x2000)
ROM_LOAD("136021-102.1hj", 0x8000, 0x2000, CRC(f725e344) SHA1(f8943b67f2ea032ab9538084756ba86f892be5ca)) /*  8k ROM 1 bank */
ROM_LOAD("136021-103.1jk", 0xa000, 0x2000, CRC(3fde9ccb) SHA1(8d88fc7a28ac8f189f8aba08598732ac8c5491aa)) /*  8k ROM 2 bank */
ROM_LOAD("136021-104.1kl", 0xc000, 0x2000, CRC(7e406703) SHA1(981b505d6e06d7149f8bcb3e81e4d0c790f2fc86)) /*  8k ROM 3 bank */
ROM_LOAD("136021-206.1m", 0xe000, 0x2000, CRC(c7e51237) SHA1(4960f4446271316e3f730eeb2531dbc702947395))  /*  8k ROM 4 bank */

ROM_REGION(0x10000, REGION_CPU2, 0) /* Really only 32k, but it looks like 64K */
ROM_LOAD("136021-107.1jk", 0x4000, 0x2000, CRC(dbf3aea2) SHA1(c38661b2b846fe93487eef09ca3cda19c44f08a0)) /* Sound ROM 0 */
ROM_RELOAD(0xc000, 0x2000)
ROM_LOAD("136021-208.1h", 0x6000, 0x2000, CRC(e38070a8) SHA1(c858ae1702efdd48615453ab46e488848891d139))  /* Sound ROM 1 */
ROM_RELOAD(0xe000, 0x2000)

ROM_REGION(0x1000, REGION_GFX1, 0)
// Mathbox
ROM_LOAD("136021-110.7h", 0x0000, 0x0400, CRC(810e040e) SHA1(d247cbb0afb4538d5161f8ce9eab337cdb3f2da4))
ROM_LOAD("136021-111.7j", 0x0400, 0x0400, CRC(ae69881c) SHA1(f3420c6e15602956fd94982a5d8d4ddd015ed977))
ROM_LOAD("136021-112.7k", 0x0800, 0x0400, CRC(ecf22628) SHA1(4dcf5153221feca329b8e8d199bd4fc00b151d9c))
ROM_LOAD("136021-113.7l", 0x0c00, 0x0400, CRC(83febfde) SHA1(e13541b09d1724204fdb171528e9a1c83c799c1c))

ROM_REGION(0x100, REGION_PROMS, 0)
ROM_LOAD("136021-109.4b", 0x0000, 0x0100, CRC(82fc3eb2) SHA1(184231c7baef598294860a7d2b8a23798c5c7da6)) /* AVG PROM */
ROM_END

ROM_START(esb)
ROM_REGION(0x22000, REGION_CPU1, 0)
ROM_LOAD("136031-111.1l", 0x03000, 0x1000, CRC(b1f9bd12) SHA1(76f15395c9fdcd80dd241307a377031a1f44e150))    // 3000-3fff is 4k vector rom
ROM_LOAD("136031-101.1f", 0x06000, 0x2000, CRC(ef1e3ae5) SHA1(d228ff076faa7f9605badeee3b827adb62593e0a))
ROM_CONTINUE(0x10000, 0x2000)
// $8000 - $9fff : slapstic page
ROM_LOAD("136031-102.1jk", 0x0a000, 0x2000, CRC(62ce5c12) SHA1(976256acf4499dc396542a117910009a8808f448))
ROM_CONTINUE(0x1c000, 0x2000)
ROM_LOAD("136031-203.1kl", 0x0c000, 0x2000, CRC(27b0889b) SHA1(a13074e83f0f57d65096d7f49ae78f33ab00c479))
ROM_CONTINUE(0x1e000, 0x2000)
ROM_LOAD("136031-104.1m", 0x0e000, 0x2000, CRC(fd5c725e) SHA1(541cfd004b1736b6cec13836dfa813f00eedeed0))
ROM_CONTINUE(0x20000, 0x2000)

ROM_LOAD("136031-105.3u", 0x14000, 0x4000, CRC(ea9e4dce) SHA1(9363fd5b1fce62c2306b448a7766eaf7ec97cdf5)) // slapstic 0, 1
ROM_LOAD("136031-106.2u", 0x18000, 0x4000, CRC(76d07f59) SHA1(44dd018b406f95e1512ce92923c2c87f1458844f)) // slapstic 2, 3

// Sound ROMS
ROM_REGION(0x10000, REGION_CPU2, 0)
ROM_LOAD("136031-113.1jk", 0x4000, 0x2000, CRC(24ae3815) SHA1(b1a93af76de79b902317eebbc50b400b1f8c1e3c)) // Sound ROM 0
ROM_CONTINUE(0xc000, 0x2000) // Copied again for
ROM_LOAD("136031-112.1h", 0x6000, 0x2000, CRC(ca72d341) SHA1(52de5b82bb85d7c9caad2047e540d0748aa93ba5)) // Sound ROM 1
ROM_CONTINUE(0xe000, 0x2000) // proper int vecs

ROM_REGION(0x1000, REGION_GFX1, 0)
ROM_LOAD("136031-110.7h", 0x0000, 0x0400, CRC(b8d0f69d) SHA1(c196f1a592bd1ac482a81e23efa224d9dfaefc0a)) /* PROM 0 */
ROM_LOAD("136031-109.7j", 0x0400, 0x0400, CRC(6a2a4d98) SHA1(cefca71f025f92a193c5a7d8b5ab8be10db2fd44)) /* PROM 1 */
ROM_LOAD("136031-108.7k", 0x0800, 0x0400, CRC(6a76138f) SHA1(9ef7af898a3e29d03f35045901023615a6a55205)) /* PROM 2 */
ROM_LOAD("136031-107.7l", 0x0c00, 0x0400, CRC(afbf6e01) SHA1(0a6438e6c106d98e5d67a019751e1584324f5e5c)) /* PROM 3 */

ROM_REGION(0x100, REGION_PROMS, 0)
ROM_LOAD("136021-109.4b", 0x0000, 0x0100, CRC(82fc3eb2) SHA1(184231c7baef598294860a7d2b8a23798c5c7da6)) /* AVG PROM */
ROM_END


ROM_START(tomcatsw)
ROM_REGION(0x12000, REGION_CPU1, 0)
ROM_LOAD("tcavg3.1l", 0x3000, 0x1000, CRC(27188aa9) SHA1(5d9a978a7ac1913b57586e81045a1b955db27b48))
ROM_LOAD("tc6.1f", 0x6000, 0x2000, CRC(56e284ff) SHA1(a5fda9db0f6b8f7d28a4a607976fe978e62158cf))
ROM_LOAD("tc8.1hj", 0x8000, 0x2000, CRC(7b7575e3) SHA1(bdb838603ffb12195966d0ce454900253bc0f43f))
ROM_LOAD("tca.1jk", 0xa000, 0x2000, CRC(a1020331) SHA1(128745a2ec771ac818a8fbba59a08f0cf5f28e8f))
ROM_LOAD("tce.1m", 0xe000, 0x2000, CRC(4a3de8a3) SHA1(e48fc17201326358317f6b428e583ecaa3ecb881))

/* Sound ROMS */
ROM_REGION(0x10000, REGION_CPU2, 0)
//ROM_LOAD("136021-107.1jk", 0x4000, 0x2000, 0) /* Sound ROM 0 */
//ROM_RELOAD(0xc000, 0x2000)
//ROM_LOAD("136021-208.1h", 0x6000, 0x2000, 0) /* Sound ROM 0 */
//ROM_RELOAD(0xe000, 0x2000)

/* Mathbox PROMs */
ROM_REGION(0x1000, REGION_GFX1, 0)
ROM_LOAD("136021-110.7h", 0x0000, 0x0400, CRC(810e040e) SHA1(d247cbb0afb4538d5161f8ce9eab337cdb3f2da4)) /* PROM 0 */
ROM_LOAD("136021-111.7j", 0x0400, 0x0400, CRC(ae69881c) SHA1(f3420c6e15602956fd94982a5d8d4ddd015ed977)) /* PROM 1 */
ROM_LOAD("136021-112.7k", 0x0800, 0x0400, CRC(ecf22628) SHA1(4dcf5153221feca329b8e8d199bd4fc00b151d9c)) /* PROM 2 */
ROM_LOAD("136021-113.7l", 0x0c00, 0x0400, CRC(83febfde) SHA1(e13541b09d1724204fdb171528e9a1c83c799c1c)) /* PROM 3 */

ROM_REGION(0x100, REGION_PROMS, 0)
ROM_LOAD("136021-109.4b", 0x0000, 0x0100, CRC(82fc3eb2) SHA1(184231c7baef598294860a7d2b8a23798c5c7da6)) /* AVG PROM */
ROM_END

// Star Wars (Revision 2)
AAE_DRIVER_BEGIN(drv_starwars, "starwars", "Star Wars (Revision 2)")
AAE_DRIVER_ROM(rom_starwars)
AAE_DRIVER_FUNCS(&init_starwars, &run_starwars, &end_starwars)
AAE_DRIVER_INPUT(input_ports_starwars)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	// CPU0: Main 6809 @ 1.512 MHz. The main IRQ is now driven by the
	// starwars_start_irq_timer() periodic timer, not the scheduler, so
	// ipf is 0 and the int callback is nullptr (see cpu_run in
	// cpu_control.cpp: ipf 0 -> iloops = -1 -> no interrupt injection).
	AAE_CPU_ENTRY_EX(
		/*type*/     CPU_M6809,
		/*freq*/     1512000,
		/*div*/      100,
		/*ipf*/      0,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   nullptr,
		/*r8*/       starwars_readmem,
		/*w8*/       starwars_writemem,
		/*pr*/       nullptr,
		/*pw*/       nullptr,
		/*r16*/      nullptr,
		/*w16*/      nullptr,
		/*post*/     &starwars_post_cpu_init
	),
	// CPU1: Audio 6809 @ 1.512 MHz. The RIOT (clocked at this same 1.512 MHz)
	// now generates the sound IRQ itself via r6532_irq_cb -> irq_line(), so
	// like CPU0 this runs ipf 0 / nullptr int cb (see cpu_run in
	// cpu_control.cpp: ipf 0 -> iloops = -1 -> no interrupt injection).
	AAE_CPU_ENTRY(
		/*type*/     CPU_M6809,
		/*freq*/     1512000,
		/*div*/      100,
		/*ipf*/      0,
		/*int type*/ INT_TYPE_INT,
		/*int cb*/   nullptr,
		/*r8*/       starwars_audio_readmem,
		/*w8*/       starwars_audio_writemem,
		/*pr*/       nullptr,
		/*pw*/       nullptr,
		/*r16*/      nullptr,
		/*w16*/      nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(30, 0, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT)
//AAE_DRIVER_SCREEN(1024, 768, 0, 285, 0, 280)
AAE_DRIVER_SCREEN(1024, 768, 0, 250, 0, 280)
AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0x000, 0x3000)
AAE_DRIVER_NVRAM(starwars_nvram_handler)
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()

// Star Wars (Revision 1)
AAE_DRIVER_BEGIN(drv_starwars1, "starwars1", "Star Wars (Revision 1)")
AAE_DRIVER_ROM(rom_starwars1)
AAE_DRIVER_FUNCS(&init_starwars, &run_starwars, &end_starwars)
AAE_DRIVER_INPUT(input_ports_starwars)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	// CPU0: Main 6809 (main IRQ from starwars_start_irq_timer(); ipf 0 / nullptr
	// int cb disables the scheduler's own interrupt injection - see cpu_run)
	AAE_CPU_ENTRY_EX(
		CPU_M6809, 1512000, 100, 0, INT_TYPE_INT, nullptr,
		starwars_readmem, starwars_writemem,
		nullptr, nullptr, nullptr, nullptr,
		&starwars_post_cpu_init
	),
	// CPU1: Audio 6809. The RIOT (clocked at the same 1.512 MHz) now
	// generates the sound IRQ itself via r6532_irq_cb -> irq_line(), so
	// ipf 0 / nullptr int cb (see cpu_run in cpu_control.cpp).
	AAE_CPU_ENTRY(
		CPU_M6809, 1512000, 100, 0, INT_TYPE_INT, nullptr,
		starwars_audio_readmem, starwars_audio_writemem,
		nullptr, nullptr, nullptr, nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(30, 0, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT)
//AAE_DRIVER_SCREEN(1024, 768, 0, 285, 0, 280)
AAE_DRIVER_SCREEN(1024, 768, 0, 250, 0, 280)

AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0x0, 0x3000)
AAE_DRIVER_NVRAM(starwars_nvram_handler)
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_CLONE_OF("starwars")
AAE_DRIVER_END()

// Star Wars (Revision 1, older) - MAME 0.286 "Star Wars (set 3)", the
// oldest of the three dumps; differs from starwars1 by one main-CPU ROM
// (136021-103.1jk).
AAE_DRIVER_BEGIN(drv_starwarso, "starwarso", "Star Wars (Revision 1, older)")
AAE_DRIVER_ROM(rom_starwarso)
AAE_DRIVER_FUNCS(&init_starwars, &run_starwars, &end_starwars)
AAE_DRIVER_INPUT(input_ports_starwars)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	// CPU0: Main 6809 (main IRQ from starwars_start_irq_timer(); ipf 0 / nullptr
	// int cb disables the scheduler's own interrupt injection - see cpu_run)
	AAE_CPU_ENTRY_EX(
		CPU_M6809, 1512000, 100, 0, INT_TYPE_INT, nullptr,
		starwars_readmem, starwars_writemem,
		nullptr, nullptr, nullptr, nullptr,
		&starwars_post_cpu_init
	),
	// CPU1: Audio 6809. The RIOT (clocked at the same 1.512 MHz) now
	// generates the sound IRQ itself via r6532_irq_cb -> irq_line(), so
	// ipf 0 / nullptr int cb (see cpu_run in cpu_control.cpp).
	AAE_CPU_ENTRY(
		CPU_M6809, 1512000, 100, 0, INT_TYPE_INT, nullptr,
		starwars_audio_readmem, starwars_audio_writemem,
		nullptr, nullptr, nullptr, nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(30, 0, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 250, 0, 280)

AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0x0, 0x3000)
AAE_DRIVER_NVRAM(starwars_nvram_handler)
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_CLONE_OF("starwars")
AAE_DRIVER_END()

// Star Wars: Empire Strike Back
AAE_DRIVER_BEGIN(drv_esb, "esb", "Star Wars: Empire Strike Back")
AAE_DRIVER_ROM(rom_esb)
AAE_DRIVER_FUNCS(&init_esb, &run_starwars, &end_starwars)
AAE_DRIVER_INPUT(input_ports_esb)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
	// CPU0: Main 6809 (hardware-compatible with Star Wars main; main IRQ from
	// starwars_start_irq_timer(), ipf 0 / nullptr int cb - see cpu_run)
	AAE_CPU_ENTRY_EX(
		CPU_M6809, 1512000, 100, 0, INT_TYPE_INT, nullptr,
		empire_readmem, empire_writemem,
		nullptr, nullptr, nullptr, nullptr,
		&starwars_post_cpu_init
	),
	// CPU1: Audio 6809. The RIOT (clocked at the same 1.512 MHz) now
	// generates the sound IRQ itself via r6532_irq_cb -> irq_line(), so
	// ipf 0 / nullptr int cb (see cpu_run in cpu_control.cpp).
	AAE_CPU_ENTRY(
		CPU_M6809, 1512000, 100, 0, INT_TYPE_INT, nullptr,
		starwars_audio_readmem, starwars_audio_writemem,
		nullptr, nullptr, nullptr, nullptr
	),
	AAE_CPU_NONE_ENTRY(),
	AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(30, 0, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT)
//AAE_DRIVER_SCREEN(1024, 768, 0, 281, 0, 280)
AAE_DRIVER_SCREEN(1024, 768, 0, 250, 0, 280)

AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0x000, 0x3000)
AAE_DRIVER_NVRAM(starwars_nvram_handler)
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()

// Tomcat
AAE_DRIVER_BEGIN(drv_tomcat, "tomcat", "TomCat (Star Wars hardware, prototype)")
AAE_DRIVER_ROM(rom_tomcatsw)
AAE_DRIVER_FUNCS(&init_starwars, &run_starwars, &end_starwars)
AAE_DRIVER_INPUT(input_ports_starwars)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
// CPU0: Main 6809 (hardware-compatible with Star Wars main; main IRQ from
// starwars_start_irq_timer(), ipf 0 / nullptr int cb - see cpu_run)
AAE_CPU_ENTRY_EX(
CPU_M6809, 1512000, 100, 0, INT_TYPE_INT, nullptr,
starwars_readmem, starwars_writemem,
nullptr, nullptr, nullptr, nullptr,
&starwars_post_cpu_init
),
// CPU1: Audio 6809. The RIOT (clocked at the same 1.512 MHz) now
// generates the sound IRQ itself via r6532_irq_cb -> irq_line(), so
// ipf 0 / nullptr int cb (see cpu_run in cpu_control.cpp).
AAE_CPU_ENTRY(
CPU_M6809, 1512000, 100, 0, INT_TYPE_INT, nullptr,
starwars_audio_readmem, starwars_audio_writemem,
nullptr, nullptr, nullptr, nullptr
),
AAE_CPU_NONE_ENTRY(),
AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(30, 0, VIDEO_TYPE_VECTOR | VECTOR_USES_COLOR, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(1024, 768, 0, 250, 0, 280)

AAE_DRIVER_RASTER_NONE()
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0x000, 0x3000)
AAE_DRIVER_NVRAM(starwars_nvram_handler)
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_starwars)
AAE_REGISTER_DRIVER(drv_starwars1)
AAE_REGISTER_DRIVER(drv_starwarso)
AAE_REGISTER_DRIVER(drv_esb)
AAE_REGISTER_DRIVER(drv_tomcat)
//////////////////  END OF MAIN PROGRAM /////////////////////////////////////////////