#include "cpu_control.h"
#include "inptport.h"
#include "aae_mame_driver.h"
#include "ccpu.h"
#include "timer.h"

static int cyclecount[4];
static int reset_cpu_status[4];
//Time counters (To Be Removed)
static int tickcount[4];
static int eternaticks[4];
static int vid_tickcount;
//Interrupt Variables
static int interrupt_enable[4];
static int interrupt_vector[4] = { 0xff,0xff,0xff,0xff };
static int interrupt_pending[4];

// ---------------------------------------------------------------------------
// IPT_VBLANK state
// Tracks whether we are currently inside the VBLANK period.
// Games that use IPT_VBLANK poll an input bit to detect vertical blank.
// Set to 1 inside cpu_run() when CPU0 reaches the vblank_start_cycle,
// cleared by cpu_clear_vblank() called from inputport_vblank_end().
// ---------------------------------------------------------------------------
static int vblank = 0;

static int s_lines_per_frame = 262; // VIC Dual visible raster; use 262 for NTSC-ish timing
static int cpu_framecounter = 0; //This is strictly for the cinematronics games.
// We currently don't use this anywhere else.

//static int cpu_enabled[MAX_CPU];
static int cpurunning[MAX_CPU];
static int ran_this_frame[MAX_CPU];
static int current_slice;
/* (needed by cpu_getfcount) */
static int iloops[MAX_CPU];
//
// End of new code.

//New
static int active_cpu = 0;
static int totalcpu = 0;
static int watchdog_timer = 0;
static int watchdog_counter = 0;

// CPU instances

cpu_m6809* m_cpu_6809[MAX_CPU];
cpu_m6800* m_cpu_6800[MAX_CPU];
cpu_i8080* m_cpu_i8080[MAX_CPU];
cpu_z80* m_cpu_z80[MAX_CPU];
cpu_6502* m_cpu_6502[MAX_CPU];
cpu_i8085* m_cpu_i8085[MAX_CPU];
cpu_i8039* m_cpu_i8039[MAX_CPU];
cpu_m68000* m_cpu_68000[MAX_CPU];

/* override OP base handler */
static int (*setOPbasefunc)(int);

//Machine->gamedrv->cpu_type[0]

void init_z80(struct MemoryReadByte* read, struct MemoryWriteByte* write, struct z80PortRead* portread, struct z80PortWrite* portwrite, int cpunum)
{
	LOG_INFO("Z80 Init Started");
	//active_cpu = cpunum;
	m_cpu_z80[cpunum] = new cpu_z80(Machine->memory_region[cpunum],
		read,
		write,
		portread,
		portwrite,
		0xffff,
		cpunum);
	m_cpu_z80[cpunum]->mz80reset();
	LOG_INFO("Z80 Init Ended");
}

void init8080(struct MemoryReadByte* read, struct MemoryWriteByte* write, struct z80PortRead* portread, struct z80PortWrite* portwrite, int cpunum)
{
	//active_cpu = cpunum;
	m_cpu_i8080[cpunum] = new cpu_i8080(Machine->memory_region[cpunum],
		read,
		write,
		portread,
		portwrite,
		0);
	m_cpu_i8080[cpunum]->reset();
}

void init8085(struct MemoryReadByte* read, struct MemoryWriteByte* write, struct z80PortRead* portread, struct z80PortWrite* portwrite, int cpunum)
{
	m_cpu_i8085[cpunum] = new cpu_i8085(Machine->memory_region[cpunum],
		read,
		write,
		portread,
		portwrite,
		0);
	m_cpu_i8085[cpunum]->reset();
}

void init8039(struct MemoryReadByte* read, struct MemoryWriteByte* write, struct z80PortRead* portread, struct z80PortWrite* portwrite, int cpunum)
{
	m_cpu_i8039[cpunum] = new cpu_i8039(Machine->memory_region[cpunum],
		read,
		write,
		portread,
		portwrite,
		0);
	// AAE drivers use the MAME-style 8039 port map (P1=0x101 .. BUS=0x120,
	// MOVX through ports 0x00..0xFF), so enable MAME-compat behaviour.
	m_cpu_i8039[cpunum]->set_mame_compat(true);
	m_cpu_i8039[cpunum]->reset();
}

// The 8035 is the ROM-less 8048: the same MCS-48 core as the 8039, differing
// only in internal RAM size (64 bytes vs the 8039's 128). Reuse the 8039 core
// and clamp the RAM to 64 bytes. (Donkey Kong / Donkey Kong Jr. drive their
// sound hardware with an 8035.)
void init8035(struct MemoryReadByte* read, struct MemoryWriteByte* write, struct z80PortRead* portread, struct z80PortWrite* portwrite, int cpunum)
{
	init8039(read, write, portread, portwrite, cpunum);
	m_cpu_i8039[cpunum]->set_ram_size(64);
}

void init6502(struct MemoryReadByte* read, struct MemoryWriteByte* write, int mem_top, int cpunum)
{
	//active_cpu = cpunum;
	m_cpu_6502[cpunum] = new cpu_6502(Machine->memory_region[cpunum], read, write, mem_top, cpunum);
	m_cpu_6502[cpunum]->reset6502();
	LOG_INFO("Finished Configuring CPU");
}

void init6809(struct MemoryReadByte* read, struct MemoryWriteByte* write, int cpunum)
{
	active_cpu = cpunum;
	LOG_INFO("Start Configuring CPU %d", cpunum);
	m_cpu_6809[cpunum] = new cpu_m6809(Machine->memory_region[cpunum], read, write, cpunum);

	// Hand the main 6809 any PC-change override that was already registered
	// (e.g. the Star Wars slapstic). Covers the case where the driver called
	// cpu_setOPbaseoverride() before the CPU object existed. Only CPU 0 carries
	// it, matching the historical "slapstic is main-CPU only" rule.
	if (cpunum == 0)
		m_cpu_6809[cpunum]->opbase_override = setOPbasefunc;

	LOG_INFO("RESET");
	m_cpu_6809[cpunum]->reset6809();
	LOG_INFO("Finished Configuring CPU %d", cpunum);
}

// Shared by CPU_M6800 / CPU_M6802 / CPU_M6808: one core covers all three, since
// they differ only in on-chip clock and RAM, never in the instruction set.
void init6800(struct MemoryReadByte* read, struct MemoryWriteByte* write, int cpunum)
{
	active_cpu = cpunum;
	LOG_INFO("Start Configuring CPU %d", cpunum);
	m_cpu_6800[cpunum] = new cpu_m6800(Machine->memory_region[cpunum], read, write, cpunum);
	m_cpu_6800[cpunum]->reset6800();
	LOG_INFO("Finished Configuring CPU %d", cpunum);
}

void special_tickcount_update_6502(int ticks, int cpu_num)
{
	cyclecount[cpu_num] += ticks;
}

// -----------------------------------------------------------------------------
// get_exact_cyclecount
// Return scheduler-owned cycles for the requested CPU, plus any core-reported
// "pending" cycles since the last scheduler accounting (non-destructive peek).
//
// -----------------------------------------------------------------------------
int get_exact_cyclecount(int cpu)
{
	int pending = 0;

	switch (Machine->gamedrv->cpu[cpu].cpu_type)
	{
	case CPU_MZ80:  pending = m_cpu_z80[cpu]->mz80GetElapsedTicks(0);   break;
	case CPU_M6502: pending = m_cpu_6502[cpu]->get6502ticks(0);         break;
	case CPU_8080:  pending = m_cpu_i8080[cpu]->get_ticks(0);           break;
	case CPU_8085: pending = m_cpu_i8085[cpu]->get_ticks(0);			break;
	case CPU_8039:
	case CPU_8035: pending = m_cpu_i8039[cpu]->get_ticks(0);			break;
	case CPU_M6809: pending = m_cpu_6809[cpu]->get6809ticks(0);         break;
	case CPU_M6800:
	case CPU_M6802:
	case CPU_M6808: pending = m_cpu_6800[cpu]->get6800ticks(0);         break;
	case CPU_68000: pending = 0; // avoid double count with Musashi
		break;
	default:        pending = 0; break;
	}
	return cyclecount[cpu] + pending;
}

// **************************************************************************
// Still used by several games
void add_eterna_ticks(int cpunum, int ticks)
{
	eternaticks[cpunum] += ticks;
	eternaticks[cpunum] &= 0xffffff;
}
int get_eterna_ticks(int cpunum)
{
	return eternaticks[cpunum];
}
// *************************************************************************

// This is also still needed, OmegaRace, AVG Code.
int get_elapsed_ticks(int cpunum)
{
	return tickcount[cpunum];
}

// -----------------------------------------------------------------------------
// get_video_ticks
// Return scheduler-owned video tickcount only (avoid 68000 double count).
// -----------------------------------------------------------------------------

// This one is needed, OmegaRace, AVG Code and Asteroids.
int get_video_ticks(int reset)
{
	int v = vid_tickcount;
	if (reset) {
		vid_tickcount = 0;
	}
	return v;
}

void aae_set_lines_per_frame(int lines) {
	if (lines > 0) s_lines_per_frame = lines;
}

// cycles-per-frame for CPU0 (unchanged)
static inline int aae_cycles_per_frame_cpu0(void) {
	const int fps = Machine->gamedrv->fps;
	const int cpu_hz = Machine->gamedrv->cpu[0].cpu_freq;
	if (fps <= 0 || cpu_hz <= 0) return 1;
	int cpf = cpu_hz / fps;
	return (cpf > 0) ? cpf : 1;
}

// approximate cycles per scanline  kept as a convenience for callers
int aae_cpu_getscanlinecycles(void) {
	int cpf = aae_cycles_per_frame_cpu0();
	int cpl = cpf / s_lines_per_frame;
	return (cpl > 0) ? cpl : 1;
}

// cycles since the beginning of THIS frame (CPU0-based)
// Uses ran_this_frame which is the scheduler's definitive count,
// avoiding double-count from pending CPU ticks outside execution.
int aae_cpu_getcurrentcycles_in_frame(void) {
	const int cpf = aae_cycles_per_frame_cpu0();
	if (cpf <= 0) return 0;
	return ran_this_frame[0] % cpf;
}

// current scanline: multiply-first to avoid truncation drift,
// clamped to valid range
int aae_cpu_getscanline(void) {
	const int cpf = aae_cycles_per_frame_cpu0();
	if (cpf <= 0) return 0;
	const int cyc = aae_cpu_getcurrentcycles_in_frame();
	int line = (cyc * s_lines_per_frame) / cpf;
	if (line >= s_lines_per_frame) line = s_lines_per_frame - 1;
	return line;
}

/* cpu change op-code memory base */
void cpu_setOPbaseoverride(int (*f)(int))
{
	setOPbasefunc = f;

	// Push the override straight into the main 6809 core so it can fire on every
	// non-sequential PC change (the core no longer calls cpu_setOPbase16). A null
	// f (e.g. Mappy) clears it. Covers the case where the CPU already exists when
	// the driver registers the handler.
	if (m_cpu_6809[0])
		m_cpu_6809[0]->opbase_override = f;
}

/* Need to called after CPU or PC changed (JP,JR,BRA,CALL,RET) */
void cpu_setOPbase16(int apc)
{
	//LOG_INFO("we're here PC before %x", apc);
	/* ASG 970206 -- allow overrides */
	if (setOPbasefunc)
	{
		if (apc == -1)
			return;

		uint16_t something = setOPbasefunc(apc);
	}
}

int cpu_getppc()
{
	//Run cycles depending on which cpu
	switch (Machine->gamedrv->cpu[active_cpu].cpu_type)
	{
	case CPU_MZ80:
		return m_cpu_z80[active_cpu]->GetPPC();
		break;

	case CPU_M6502:
		return m_cpu_6502[active_cpu]->get_ppc();
		break;

	case CPU_M6809:
		return m_cpu_6809[active_cpu]->get_ppc();
		break;

	case CPU_M6800:
	case CPU_M6802:
	case CPU_M6808:
		return m_cpu_6800[active_cpu]->get_ppc();
		break;
	}
	return 0;
}

int cpu_getpc()
{
	//Run cycles depending on which cpu
	switch (Machine->gamedrv->cpu[active_cpu].cpu_type)
	{
	case CPU_8080:
		return m_cpu_i8080[active_cpu]->reg_PC;
		break;

	case CPU_8085:
		return m_cpu_i8085[active_cpu]->reg_PC;
		break;

	case CPU_8039:
	case CPU_8035:
		return m_cpu_i8039[active_cpu]->reg_PC;
		break;

	case CPU_MZ80:
		return m_cpu_z80[active_cpu]->GetPC();
		break;

	case CPU_M6502:
		return m_cpu_6502[active_cpu]->get_pc();
		break;

	case CPU_M6809:
		return m_cpu_6809[active_cpu]->get_pc();
		break;

	case CPU_M6800:
	case CPU_M6802:
	case CPU_M6808:
		return m_cpu_6800[active_cpu]->get_pc();
		break;

	case CPU_68000:
		LOG_INFO("PC:%08X\tSP:%08X\n", m_cpu_68000[active_cpu]->GetPC(), m_cpu_68000[active_cpu]->GetSP());
		break;
	}
	return 0;
}

void cpu_needs_reset(int cpunum)
{
	reset_cpu_status[cpunum] = 1;
}

void cpu_enable(int cpunum, int val)
{
	cpurunning[cpunum] = val;
}

int get_active_cpu()
{
	return active_cpu;
}

int cpu_getcurrentframe()
{
	return cpu_framecounter;
}

int cpu_getcycles(int reset) //Only returns cycles from current context cpu
{
	return cyclecount[active_cpu];
}

int cpu_getcycles_cpu(int cpu) //Only returns cycles from current context cpu
{
	return cyclecount[cpu];
}

// -----------------------------------------------------------------------------
// cpu_getcycles_remaining_cpu
// Cycles left in THIS frame for the given 'cpu'.
// -----------------------------------------------------------------------------
int cpu_getcycles_remaining_cpu(int cpu)
{
	const int per_frame = Machine->gamedrv->cpu[cpu].cpu_freq / Machine->gamedrv->fps;
	const int elapsed = ran_this_frame[cpu];
	return (per_frame > elapsed) ? (per_frame - elapsed) : 0;
}

void cpu_clear_cyclecount(int cpunum)
{
	cyclecount[cpunum] = 0;
}

void cpu_clear_cyclecount_eof()
{
	int x;

	for (x = 0; x < totalcpu; x++)
	{
		if (config.debug_profile_code) {
			LOG_INFO("Clear CPU#: %d count at clear is: %d", x, cyclecount[x]);
		}
		cyclecount[x] = 0;
	}
}

int get_current_cpu()
{
	return active_cpu;
}

void interrupt_enable_w(UINT32 address, UINT8 data, struct MemoryWriteByte* pMemWrite)
{
	int cpunum = (active_cpu < 0) ? 0 : active_cpu;
	interrupt_enable[cpunum] = data & 1;

	/* make sure there are no queued interrupts */
	if (data == 0) cpu_clear_pending_interrupts(cpunum);
}

void interrupt_vector_w(UINT16 address, UINT8 data, struct z80PortWrite* pPW)
{
	int cpunum = (active_cpu < 0) ? 0 : active_cpu;
	if (interrupt_vector[cpunum] != data)
	{
		//LOG_INFO("CPU#%d interrupt_vector_w $%02x\n", cpunum, data);
		interrupt_vector[cpunum] = data;

		/* make sure there are no queued interrupts */
		cpu_clear_pending_interrupts(cpunum);
	}
}

void cpu_disable_interrupts(int cpunum, int val)
{
	interrupt_enable[cpunum] = val;
}

void cpu_clear_pending_interrupts(int cpunum)
{
	interrupt_pending[cpunum] = 0;
}

void set_interrupt_vector(int data)
{
	int cpunum = get_current_cpu();
	if (interrupt_vector[cpunum] != data)
	{
		interrupt_vector[cpunum] = data;

		// make sure there are no queued interrupts
		cpu_clear_pending_interrupts(cpunum);
	}
}

void cpu_do_int_imm(int cpunum, int int_type)
{
	// Respect per-CPU interrupt enable
	if (!interrupt_enable[cpunum]) return;

	// Some cores (6502 NMI, 8080/8085/8039 INT) push state through the memory
	// handlers immediately, and generic MRA_RAM/MWA_RAM index by active_cpu.
	// Run the dispatch in the target CPU's context, then restore the caller's
	// -- same hazard and same pattern as cpu_reset(). Latching cores (Z80,
	// 6809, 68000, 6502 IRQ) touch no memory here, so this is harmless there.
	const int prev_active_cpu = active_cpu;
	active_cpu = cpunum;

	switch (Machine->gamedrv->cpu[cpunum].cpu_type)
	{
	case CPU_8080:
		// 8080 only has maskable INTR (no true NMI in standard parts)
		if (int_type == INT_TYPE_INT) {
			m_cpu_i8080[cpunum]->interrupt(interrupt_vector[cpunum]);
		}
		// else: silently ignore unsupported NMI on 8080
		break;

	case CPU_8085:
		if (int_type == INT_TYPE_INT) {
			m_cpu_i8085[cpunum]->interrupt(interrupt_vector[cpunum]);
		}
		break;

	case CPU_8039:
	case CPU_8035:
		// MCS-48 external interrupt pin (always vectors to 0x003); the
		// timer interrupt is generated internally by the core. A plain
		// INT request asserts the external line.
		if (int_type == INT_TYPE_INT) {
			m_cpu_i8039[cpunum]->interrupt(interrupt_vector[cpunum]);
		}
		break;

	case CPU_MZ80:
		if (int_type == INT_TYPE_NMI) m_cpu_z80[cpunum]->mz80nmi();
		else                          m_cpu_z80[cpunum]->mz80int(interrupt_vector[cpunum]);
		break;

	case CPU_M6502:
		if (int_type == INT_TYPE_NMI) m_cpu_6502[cpunum]->nmi6502();
		else                          m_cpu_6502[cpunum]->irq6502();
		break;

	case CPU_M6809:
		m_cpu_6809[cpunum]->m6809_Cause_Interrupt(
			(int_type == INT_TYPE_NMI) ? M6809_INT_NMI : M6809_INT_IRQ
		);
		break;

	case CPU_M6800:
	case CPU_M6802:
	case CPU_M6808:
		m_cpu_6800[cpunum]->m6800_Cause_Interrupt(
			(int_type == INT_TYPE_NMI) ? M6800_INT_NMI : M6800_INT_IRQ
		);
		break;

	case CPU_68000:
		m_cpu_68000[cpunum]->irq_line(int_type);
		break;

	default:
		// Unknown/unsupported CPU type: do nothing
		break;
	}

	active_cpu = prev_active_cpu;
}

// Point a CPU's instruction-stream fetches at a decrypted-opcode buffer.
// AAE equivalent of MAME's memory_set_opcode_base. Supported by the 6809
// (konami1 opcode scramble in Gyruss)
// Call AFTER the CPU has been created by init_6809.
void memory_set_opcode_base(int cpunum, unsigned char* base)
{
	if (cpunum < 0 || cpunum >= MAX_CPU) return;
	switch (Machine->gamedrv->cpu[cpunum].cpu_type)
	{
	case CPU_M6809:
		if (m_cpu_6809[cpunum]) m_cpu_6809[cpunum]->set_opcode_base(base);
		break;

	case CPU_MZ80:
		if (m_cpu_z80[cpunum]) m_cpu_z80[cpunum]->set_opcode_base(base);
		break;
	default:
		LOG_ERROR("memory_set_opcode_base: CPU %d type %d does not support opcode base",
			cpunum, Machine->gamedrv->cpu[cpunum].cpu_type);
		break;
	}
}

//***************************************************************************
int cpu_getiloops(void)
{
	return iloops[active_cpu];
}

// ---------------------------------------------------------------------------
// cpu_getvblank
// Returns 1 if we are currently in the VBLANK period, 0 otherwise.
// ---------------------------------------------------------------------------
int cpu_getvblank(void)
{
	return vblank;
}

// ---------------------------------------------------------------------------
// cpu_clear_vblank
// Called by inputport_vblank_end() or the frame loop to end the VBLANK
// period. Clears the flag so cpu_getvblank() returns 0 until the next
// frame's VBLANK boundary is reached inside cpu_run().
// ---------------------------------------------------------------------------
void cpu_clear_vblank(void)
{
	vblank = 0;
}

/***************************************************************************

  Use this function to cause an interrupt immediately (don't have to wait
  until the next call to the interrupt handler)

***************************************************************************/

void cpu_cause_interrupt(int cpu, int type)
{
	if (Machine->gamedrv->cpu[cpu].int_cpu) {
		Machine->gamedrv->cpu[cpu].int_cpu();
	}
}

int cpu_exec_now(int cpu, int cycles)
{
	int ticks = 0;

	//Run cycles depending on which cpu
	switch (Machine->gamedrv->cpu[cpu].cpu_type)
	{
	case CPU_MZ80:
		m_cpu_z80[cpu]->mz80exec(cycles);
		ticks = m_cpu_z80[cpu]->mz80GetElapsedTicks(0xff);
		break;

	case CPU_M6502:
		m_cpu_6502[cpu]->exec6502(cycles);
		ticks = m_cpu_6502[cpu]->get6502ticks(0xff);
		break;

	case CPU_8080:
		m_cpu_i8080[cpu]->exec(cycles);
		ticks = m_cpu_i8080[cpu]->get_ticks(0xff);
		timer_update(ticks, active_cpu);
		break;

	case CPU_8085:
		m_cpu_i8085[cpu]->exec(cycles);
		ticks = m_cpu_i8085[cpu]->get_ticks(0xff);
		timer_update(ticks, active_cpu);
		break;

	case CPU_8039:
	case CPU_8035:
		m_cpu_i8039[cpu]->exec(cycles);
		ticks = m_cpu_i8039[cpu]->get_ticks(0xff);
		timer_update(ticks, active_cpu);
		break;

	case CPU_M6809:
		m_cpu_6809[cpu]->exec6809(cycles);
		ticks = m_cpu_6809[cpu]->get6809ticks(0xff);
		break;

	case CPU_M6800:
	case CPU_M6802:
	case CPU_M6808:
		m_cpu_6800[cpu]->exec6800(cycles);
		ticks = m_cpu_6800[cpu]->get6800ticks(0xff);
		break;

	case CPU_68000:
		ticks = m_cpu_68000[cpu]->exec(cycles);
		break;
	case CPU_CCPU:
		ticks = run_ccpu(cycles);
		break;
	}
	// Update the cyclecount and the interrupt timers.
	cyclecount[cpu] += ticks;
	// NOTE THE CPU CODE ITSELF IS UPDATING THE TIMERS NOW, except for the 8080/8085/8039
	return ticks;
}

// ---------------------------------------------------------------------------
// cpu_run
// Main per-frame CPU scheduler.
//
// IPT_VBLANK handling:
//   In original MAME, the VBLANK bit is held active for vblank_duration
//   microseconds at the END of the frame (after the visible region).
//   We replicate this by:
//     1. Setting vblank=1 when CPU0 reaches (cpf - vblank_cycles).
//     2. The VBLANK bit stays set for the remainder of the frame.
//     3. inputport_vblank_end() clears it (via cpu_clear_vblank()) at
//        the top of the next frame.
//
//   If vblank_duration is 0, no VBLANK timing is done inside cpu_run.
//   Games that don't use IPT_VBLANK are completely unaffected.
// ---------------------------------------------------------------------------
void cpu_run(void)
{
	int ran, target_cycles, next_interrupt_cycles;

	// reset per-frame accumulators
	tickcount[0] = tickcount[1] = tickcount[2] = tickcount[3] = 0;
	ran_this_frame[0] = ran_this_frame[1] = ran_this_frame[2] = ran_this_frame[3] = 0;

	active_cpu = 0;

	// Per-CPU frame timing (32-bit)
	int cycles_per_frame[4] = { 0,0,0,0 };
	int divisions[4] = { 0,0,0,0 };
	int intpasses[4] = { 0,0,0,0 };

	// Initialize per-CPU loop counts and timing
	for (active_cpu = 0; active_cpu < totalcpu; active_cpu++)
	{
		if (reset_cpu_status[active_cpu]) {
			cpu_reset(active_cpu);
		}
		//LOG_INFO("Running CPU %d", active_cpu);
		const int freq = Machine->gamedrv->cpu[active_cpu].cpu_freq;
		const int fps = Machine->gamedrv->fps;
		cycles_per_frame[active_cpu] = (fps > 0) ? (freq / fps) : 0;

		divisions[active_cpu] = Machine->gamedrv->cpu[active_cpu].cpu_divisions;
		intpasses[active_cpu] = Machine->gamedrv->cpu[active_cpu].cpu_intpass_per_frame;

		if (cpurunning[active_cpu]) {
			iloops[active_cpu] = (intpasses[active_cpu] > 0) ? (intpasses[active_cpu] - 1) : -1;
		}
		else {
			iloops[active_cpu] = -1;
		}
	}

	// Use the maximum divisions across all CPUs as the global slice count
	int max_divisions = 1;
	for (int i = 0; i < totalcpu; i++) {
		if (Machine->gamedrv->cpu[i].cpu_type != CPU_NONE) {
			if (divisions[i] > max_divisions) max_divisions = divisions[i];
		}
	}

	// -----------------------------------------------------------------------
	// IPT_VBLANK: compute the CPU0 cycle at which VBLANK begins.
	// vblank_duration is in microseconds. Convert to CPU0 cycles.
	// VBLANK starts at (cpf - vblank_cycles) and lasts until end of frame.
	//
	// If vblank_duration == 0, boundary is past end-of-frame so VBLANK
	// never triggers inside cpu_run (backward compatible with all existing
	// drivers that use DEFAULT_60HZ_VBLANK_DURATION which is 0).
	// -----------------------------------------------------------------------
	const int vblank_duration_us = Machine->gamedrv->vblank_duration;
	int vblank_start_cycle = cycles_per_frame[0] + 1; // default: never triggers
	bool vblank_handled_this_frame = false;

	if (vblank_duration_us > 0 && cycles_per_frame[0] > 0)
	{
		// Convert microseconds to CPU0 cycles
		const int cpu0_freq = Machine->gamedrv->cpu[0].cpu_freq;
		int vblank_cycles = (int)((double)cpu0_freq * (double)vblank_duration_us * 0.000001);
		if (vblank_cycles < 1) vblank_cycles = 1;
		if (vblank_cycles > cycles_per_frame[0]) vblank_cycles = cycles_per_frame[0];

		// VBLANK begins this many cycles before end of frame
		vblank_start_cycle = cycles_per_frame[0] - vblank_cycles;
		if (vblank_start_cycle < 0) vblank_start_cycle = 0;
	}

	// Track, per CPU, which per-CPU slice index we have already advanced to
	int last_idx_for_cpu[4] = { 0,0,0,0 };

	// Global slice loop: 0 .. max_divisions-1 (stable bound)
	for (current_slice = 0; current_slice < max_divisions; current_slice++)
	{
		// Iterate CPUs each global slice
		for (active_cpu = 0; active_cpu < totalcpu; active_cpu++)
		{
			if (!cpurunning[active_cpu]) continue;
			if (Machine->gamedrv->cpu[active_cpu].cpu_type == CPU_NONE) continue;

			const int divs = divisions[active_cpu];
			if (divs <= 0) continue; // nothing scheduled for this CPU

			// Map global slice to this CPU per-CPU slice index in [1..divs]
			const int next_idx_for_cpu = (int)(((current_slice + 1) * divs) / max_divisions);
			if (next_idx_for_cpu <= last_idx_for_cpu[active_cpu]) continue;

			// Target cycles for THIS CPU at end of its next per-CPU slice
			target_cycles = (divs > 0) ? ((cycles_per_frame[active_cpu] * next_idx_for_cpu) / divs) : 0;

			// Compute the next interrupt boundary (in cycles since frame start)
			if (iloops[active_cpu] >= 0 && intpasses[active_cpu] > 0) {
				next_interrupt_cycles =
					(cycles_per_frame[active_cpu] * (intpasses[active_cpu] - iloops[active_cpu])) / intpasses[active_cpu];
			}
			else {
				next_interrupt_cycles = target_cycles;
			}

			// Run until we reach the per-CPU slice target
			while (ran_this_frame[active_cpu] < target_cycles)
			{
				int running;

				// For CPU0, also respect the VBLANK start boundary so we
				// can set the VBLANK flag at the right cycle count.
				int effective_boundary = target_cycles;

				if (ran_this_frame[active_cpu] < next_interrupt_cycles && next_interrupt_cycles < effective_boundary)
					effective_boundary = next_interrupt_cycles;

				// For CPU0 only: clamp to VBLANK start if not set yet
				if (active_cpu == 0 && !vblank_handled_this_frame &&
					ran_this_frame[0] < vblank_start_cycle && vblank_start_cycle < effective_boundary)
				{
					effective_boundary = vblank_start_cycle;
				}

				running = effective_boundary - ran_this_frame[active_cpu];

				// Safety: avoid zero/negative runs
				if (running <= 0) break;

				ran = cpu_exec_now(active_cpu, running);

				// Scheduler-owned accounting
				tickcount[active_cpu] += ran;
				add_eterna_ticks(active_cpu, ran);
				if (active_cpu == 0) { vid_tickcount += ran; }

				ran_this_frame[active_cpu] += ran;

				// IPT_VBLANK: check if CPU0 has reached the VBLANK boundary
				if (active_cpu == 0 && !vblank_handled_this_frame &&
					ran_this_frame[0] >= vblank_start_cycle &&
					vblank_start_cycle <= cycles_per_frame[0])
				{
					// Enter VBLANK period now. Flip IPT_VBLANK bits at the
					// actual VBLANK boundary, not at frame start.
					vblank = 1;
					inputport_vblank_begin();
					vblank_handled_this_frame = true;
				}

				// Handle interrupt pass boundary
				if (iloops[active_cpu] >= 0 && intpasses[active_cpu] > 0 &&
					ran_this_frame[active_cpu] >= next_interrupt_cycles)
				{
					if (Machine->gamedrv->cpu[active_cpu].int_cpu) {
						Machine->gamedrv->cpu[active_cpu].int_cpu();
					}
					iloops[active_cpu]--;

					// Recompute next interrupt boundary (if more left this frame)
					if (iloops[active_cpu] >= 0) {
						next_interrupt_cycles =
							(cycles_per_frame[active_cpu] * (intpasses[active_cpu] - iloops[active_cpu])) / intpasses[active_cpu];
					}
					else {
						next_interrupt_cycles = target_cycles;
					}
				}
			}

			last_idx_for_cpu[active_cpu] = next_idx_for_cpu;
		} // for each CPU
	} // for each global slice
	// Restore active_cpu to 0 after the scheduler loop. Anything that touches
	// guest memory outside the loop (F3 reset in msg_loop, the watchdog, UI code)
	// indexes memory_region[active_cpu]; leaving it at totalcpu reads a null region.
	active_cpu = 0;
	// End of CPU Update, update and check frame counter
	cpu_framecounter++;
}

void cpu_reset(int cpunum)
{
	LOG_INFO("CPU RESET CALLED ON cpu %d !!----------", cpunum);

	// Reset runs in the context of the CPU being reset: the core fetches its reset
	// vector through the memory handlers, which index memory_region[active_cpu].
	// cpu_reset() is also called from OUTSIDE the scheduler loop (F3 via msg_loop,
	// and the watchdog), where active_cpu is left at totalcpu -- for a single-CPU
	// game that points memory_region[] at a null region and crashes the fetch.
	// Restored below: when one CPU resets another mid-slice (mhavoc gamma reset,
	// IPF_RESETCPU), the caller must finish its slice in its own memory context.
	const int prev_active_cpu = active_cpu;
	active_cpu = cpunum;

	switch (Machine->gamedrv->cpu[cpunum].cpu_type)
	{
	case CPU_MZ80:
		m_cpu_z80[cpunum]->mz80reset();
		break;

	case CPU_M6502:
		m_cpu_6502[cpunum]->reset6502();
		break;

	case CPU_8080:
		m_cpu_i8080[cpunum]->reset();
		break;

	case CPU_8085:
		m_cpu_i8085[cpunum]->reset();
		break;

	case CPU_8039:
	case CPU_8035:
		m_cpu_i8039[cpunum]->reset();
		break;

	case CPU_68000:
		m_cpu_68000[cpunum]->reset();
		break;

	case CPU_M6809:
		m_cpu_6809[cpunum]->reset6809();
		break;

	case CPU_M6800:
	case CPU_M6802:
	case CPU_M6808:
		m_cpu_6800[cpunum]->reset6800();
		break;

	case CPU_CCPU:
		ccpu_reset();
		break;
	}
	//Clear CPU Cyclecount.
	cyclecount[cpunum] = 0;
	//Clear CPU Reset Status
	LOG_INFO("Cpu reset status is %d", reset_cpu_status[cpunum]);
	reset_cpu_status[cpunum] = 0;
	if (cpunum == 0)vid_tickcount = 0;
	//Reset any timers on that CPU.
	timer_cpu_reset(cpunum);
	active_cpu = prev_active_cpu;
}

void cpu_reset_all()
{
	for (int x = 0; x < totalcpu; x++)
	{
		cpu_reset(x);
	}
}

// -----------------------------------------------------------------------------
// cpu_clear_pending_int
// Clear pending interrupt state for the SPECIFIED CPU (not the active one).
// -----------------------------------------------------------------------------
void cpu_clear_pending_int(int int_type, int cpunum)
{
	switch (Machine->gamedrv->cpu[cpunum].cpu_type)
	{
	case CPU_MZ80:  m_cpu_z80[cpunum]->mz80ClearPendingInterrupt(); break;
	case CPU_M6502: m_cpu_6502[cpunum]->m6502clearpendingint();     break;
	case CPU_8039:
	case CPU_8035:  m_cpu_i8039[cpunum]->clear_pending_interrupts(); break;
	case CPU_M6800:
	case CPU_M6802:
	case CPU_M6808: m_cpu_6800[cpunum]->m6800_Clear_Pending_Interrupts(); break;
	default: break;
	}
}

// -----------------------------------------------------------------------------
// cpu_scale_by_cycles
// Scale 'val' based on % of frame elapsed for the current active CPU,
// or an explicit 'clock' if provided (>0).
// -----------------------------------------------------------------------------
int cpu_scale_by_cycles(int val, int clock)
{
	const int cpu = active_cpu;
	const int sclock = (clock > 0) ? clock : Machine->gamedrv->cpu[cpu].cpu_freq;
	const int max = (sclock > 0 && Machine->gamedrv->fps > 0)
		? (sclock / Machine->gamedrv->fps) : 1;
	const int cur = ran_this_frame[cpu];
	double t = (max > 0) ? (double)cur / (double)max : 0.0;
	if (t > 1.0) t = .99;
	return (int)(val * t);
}

// -----------------------------------------------------------------------------
// free_cpu_memory
// Pair delete with new.
// -----------------------------------------------------------------------------
void free_cpu_memory()
{
	LOG_INFO("Freeing allocated CPU Cores. Totalcpu = %d", totalcpu);

	for (int x = 0; x < totalcpu; x++)
	{
		switch (Machine->gamedrv->cpu[x].cpu_type)
		{
		case CPU_MZ80:   delete m_cpu_z80[x];    m_cpu_z80[x] = nullptr; break;
		case CPU_M6502:  delete m_cpu_6502[x];   m_cpu_6502[x] = nullptr; break;
		case CPU_8080:   delete m_cpu_i8080[x];  m_cpu_i8080[x] = nullptr; break;
		case CPU_8085:   delete m_cpu_i8085[x];  m_cpu_i8085[x] = nullptr; break;
		case CPU_8039:
		case CPU_8035:   delete m_cpu_i8039[x];  m_cpu_i8039[x] = nullptr; break;
		case CPU_M6809:  delete m_cpu_6809[x];   m_cpu_6809[x] = nullptr; break;
		case CPU_M6800:
		case CPU_M6802:
		case CPU_M6808:  delete m_cpu_6800[x];   m_cpu_6800[x] = nullptr; break;
		case CPU_68000:  delete m_cpu_68000[x];  m_cpu_68000[x] = nullptr; break;
		default: break;
		}
	}
}

void init_cpu_config()
{
	int x;
	LOG_INFO("Starting CPU init");
	totalcpu = 0;
	active_cpu = 0;

	for (x = 0; x < 4; x++)
	{
		reset_cpu_status[x] = 0;
		cpurunning[x] = 1;
		interrupt_pending[x] = 0;
		interrupt_enable[x] = 1;
		interrupt_vector[x] = 0xff;
		cyclecount[x] = 0;
		if (Machine->gamedrv->cpu[x].cpu_type)  totalcpu++;
	}

	for (int i = 0; i < MAX_CPU; ++i)
	{
		const MachineCPU& C = Machine->gamedrv->cpu[i];
		if (C.cpu_type == CPU_NONE || C.cpu_freq <= 0)
			continue;

		switch (C.cpu_type)
		{
		case CPU_68000:
			LOG_INFO("Init 68000 %d called", i);
			m_cpu_68000[i] = new cpu_m68000(C.memory_read, C.memory_write, C.read16, C.write16, i);
			break;

		case CPU_MZ80:
			LOG_INFO("Init z80 %d called", i);
			init_z80(C.memory_read, C.memory_write, C.port_read, C.port_write, i);
			break;

		case CPU_M6502:
			// You said: "I am going to just change the mem_top code so that it's always 0xffff"
			LOG_INFO("Init 6502 %d called", i);
			init6502(C.memory_read, C.memory_write, /*mem_top*/0xFFFF, i);
			break;

		case CPU_8080:
			init8080(C.memory_read, C.memory_write, C.port_read, C.port_write, i);
			break;

		case CPU_8085:
			LOG_INFO("Init 8085 %d called", i);
			init8085(C.memory_read, C.memory_write, C.port_read, C.port_write, i);
			break;

		case CPU_8039:
			LOG_INFO("Init 8039 %d called", i);
			init8039(C.memory_read, C.memory_write, C.port_read, C.port_write, i);
			break;

		case CPU_8035:
			LOG_INFO("Init 8035 %d called", i);
			init8035(C.memory_read, C.memory_write, C.port_read, C.port_write, i);
			break;

		case CPU_M6809:
			LOG_INFO("Init 6809 %d called", i);
			init6809(C.memory_read, C.memory_write, i);
			break;

		case CPU_M6800:
		case CPU_M6802:
		case CPU_M6808:
			LOG_INFO("Init 6800-family CPU %d called (type %d)", i, C.cpu_type);
			init6800(C.memory_read, C.memory_write, i);
			break;

		default:
			// Unknown/unsupported CPU type in driver.
			break;
		}

		// Fire the optional post-init callback if the driver provided one.
		// This runs after the CPU core is fully created and reset, so the
		// driver can configure per-instance settings like opfetch mode,
		// bank switching, debug flags, etc.
		if (C.post_cpu_init)
			C.post_cpu_init(i);
	}

	cpu_framecounter = 0;
	vblank = 0; // Clear VBLANK state on game init
	vid_tickcount = 0;//Initalize video tickcount;

	watchdog_timer = timer_set(TIME_IN_HZ(4), 0, watchdog_callback);
	LOG_INFO("NUMBER OF CPU'S to RUN: %d ", totalcpu);
	LOG_INFO("Finished starting up cpu settings, defaults");
}

/***************************************************************************

Use this function to initialize, and later maintain, the watchdog. For
convenience, when the machine is reset, the watchdog is disabled. If you
call this function, the watchdog is initialized, and from that point
onwards, if you don't call it at least once every 10 video frames, the
machine will be reset.

*************************************************************************/
void watchdog_callback(int param)
{
	watchdog_counter++;
	if (watchdog_counter > 2) {
		LOG_INFO("warning: reset caused by the watchdog\n");
		cpu_reset_all();
		watchdog_counter = 0;
	}
}

void watchdog_reset_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite)
{
	timer_reset(watchdog_timer, TIME_IN_HZ(4));
}

// PUBLIC: cpu_control.h declares this extern (and it is the only definition
// anywhere), so it must have external linkage to match.
READ_HANDLER_PUBLIC(watchdog_reset_r)
{
	timer_reset(watchdog_timer, TIME_IN_HZ(4));
	return 0;
}

// Write Rom
void watchdog_reset_w16(UINT32 address, UINT16 data, struct MemoryWriteWord* psMemWrite)
{
	timer_reset(watchdog_timer, TIME_IN_HZ(4));
}

//Read Ram
UINT8 MRA_RAM(UINT32 address, struct MemoryReadByte* psMemRead)
{
	//LOG_INFO("Active CPU here is %d", active_cpu);
	//LOG_INFO("Address here is %x reading address %x data %x", address, address + psMemRead->lowAddr, Machine->memory_region[active_cpu][address + psMemRead->lowAddr]);
	return Machine->memory_region[active_cpu][address + psMemRead->lowAddr];
}

//Write Ram
void MWA_RAM(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite)
{
	//LOG_INFO("Address here is %x Writing address %x ", address, address + psMemWrite->lowAddr);

	Machine->memory_region[active_cpu][address + psMemWrite->lowAddr] = data;
	//LOG_INFO("Active CPU here is %d", active_cpu);
}

// Read Rom
UINT8 MRA_ROM(UINT32 address, struct MemoryReadByte* psMemRead)
{
	//LOG_INFO("Active CPU here is %d", active_cpu);
	//LOG_INFO("Address here is %x reading address %x data %x", address, address + psMemRead->lowAddr, Machine->memory_region[active_cpu][address + psMemRead->lowAddr]);
	return Machine->memory_region[active_cpu][address + psMemRead->lowAddr];
}

UINT8 MRA_NOP(UINT32 address, struct MemoryReadByte* psMemRead)
{
	//If logging add here
	return 0;
}

// Write Rom
void MWA_NOP16(UINT32 address, UINT16 data, struct MemoryWriteWord* pMemWrite)
{
	//If logging add here
}

// Write Rom
void MWA_ROM16(UINT32 address, UINT16 data, struct MemoryWriteWord* pMemWrite)
{
	//If logging add here
}

// Write Rom
void MWA_ROM(UINT32 address, UINT8 data, struct MemoryWriteByte* pMemWrite)
{
	//If logging add here
	//LOG_INFO("Attempted Rom Write? ");
}

void MWA_NOP(UINT32 address, UINT8 data, struct MemoryWriteByte* pMemWrite)
{
	//If logging add here
}

// 68000 memory bridges (m68k_read_memory_*, m68k_write_memory_*, m68k_lockup_*,
// m68k_read_bus_*, m68k_unused_*) live in cpu_m68000.cpp.