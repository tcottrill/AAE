// -----------------------------------------------------------------------------
// Legacy MAME-Derived Module
// This file contains code originally developed as part of the M.A.M.E.™ Project.
// Portions of this file remain under the copyright of the original MAME authors
// and contributors. It has since been adapted and merged into the AAE (Another
// Arcade Emulator) project.
//
// Integration:
//   This module is now part of the **AAE (Another Arcade Emulator)** codebase
//   and is integrated with its rendering, input, and emulation subsystems.
//
// Licensing Notice:
//   - Original portions of this code remain © the M.A.M.E.™ Project and its
//     respective contributors under their original terms of distribution.
//   - Redistribution must preserve both this notice and the original MAME
//     copyright acknowledgement.
//
// License:
//   This program is free software: you can redistribute it and/or modify
//   it under the terms of the GNU General Public License as published by
//   the Free Software Foundation, either version 3 of the License, or
//   (at your option) any later version.
//
//   This program is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
//   GNU General Public License for more details.
//
//   You should have received a copy of the GNU General Public License
//   along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// Original Copyright:
//   This file is originally part of and copyright the M.A.M.E.™ Project.
//   For more information about MAME licensing, see the original MAME source
//   distribution and its associated license files.
//
// -----------------------------------------------------------------------------

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

// Note, thanks to Charles McDonald for the skeleton code and how to for the 68000 emulation

#include "cpu_control.h"
#include "./68000/m68k.h"
#include "aae_mame_driver.h"
#include "cpu_i8080.h"
#include "ccpu.h"
#include "timer.h"

static int cpu_configured = 0;
static int cyclecount[4];
static int reset_cpu_status[4];
//Time counters (To Be Removed)
static int tickcount[4];
static int eternaticks[4];
static int vid_tickcount;
//Interrupt Variables
static int interrupt_enable[4];
static int interrupt_vector[4] = { 0xff,0xff,0xff,0xff };
static int interrupt_count[4];
static int interrupt_pending[4];
static int intcnt = 0;

static int s_lines_per_frame = 262; // VIC Dual visible raster; use 262 for NTSC-ish timing
static int cpu_framecounter = 0; //This is strictly for the cinematronics games.
// We currently don't use this anywhere else.


//static int cpu_enabled[MAX_CPU];
static int cpurunning[MAX_CPU];
static int totalcycles[MAX_CPU];
static int ran_this_frame[MAX_CPU];
static int current_slice;
static int running;	/* number of cycles that the CPU emulation was requested to run */
/* (needed by cpu_getfcount) */
static int next_interrupt;	/* cycle count (relative to start of frame) when next interrupt will happen */
static int iloops[MAX_CPU];
//
// End of new code.

//New
static int active_cpu = 0;
static int totalcpu = 0;
static int watchdog_timer = 0;
static int watchdog_counter = 0;

// CPU Contexts

int cpu_context_size;
uint8_t* cpu_context[2]; // 68000 cpu
cpu_6809* m_cpu_6809[MAX_CPU];
cpu_i8080* m_cpu_i8080[MAX_CPU];
cpu_z80* m_cpu_z80[MAX_CPU];
cpu_6502* m_cpu_6502[MAX_CPU];

/* override OP base handler */
static int (*setOPbasefunc)(int);

//68000 Memory Constructs
// 8 bit
MemoryReadByte* M_MemoryRead8 = nullptr;
MemoryWriteByte* M_MemoryWrite8 = nullptr;
// 16 bit
MemoryReadWord* M_MemoryRead16 = nullptr;
MemoryWriteWord* M_MemoryWrite16 = nullptr;
//32 bit is handled

//Machine->gamedrv->cpu_type[0]

void init_z80(struct MemoryReadByte* read, struct MemoryWriteByte* write, struct z80PortRead* portread, struct z80PortWrite* portwrite, int cpunum)
{
	LOG_INFO("Z80 Init Started");
	active_cpu = cpunum;
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
	active_cpu = cpunum;
	m_cpu_i8080[cpunum] = new cpu_i8080(Machine->memory_region[cpunum],
		read,
		write,
		portread,
		portwrite,
		0);
	m_cpu_i8080[cpunum]->reset();
}

void init6502(struct MemoryReadByte* read, struct MemoryWriteByte* write, int mem_top, int cpunum)
{
	active_cpu = cpunum;
	m_cpu_6502[cpunum] = new cpu_6502(Machine->memory_region[cpunum], read, write, mem_top, cpunum);

	//m_cpu_6502[cpunum]->enableDirectStackPage(true);
	//m_cpu_6502[cpunum]->enableDirectZeroPage(true);

	m_cpu_6502[cpunum]->reset6502();
	LOG_INFO("Finished Configuring CPU");
}

void init6809(struct MemoryReadByte* read, struct MemoryWriteByte* write, int cpunum)
{
	active_cpu = cpunum;
	LOG_INFO("Start Configuring CPU %d", cpunum);
	m_cpu_6809[cpunum] = new cpu_6809(Machine->memory_region[cpunum], read, write, 0xffff, cpunum);

	LOG_INFO("RESET");
	m_cpu_6809[cpunum]->reset6809();
	LOG_INFO("Finished Configuring CPU %d", cpunum);
}

void init68k(struct MemoryReadByte* read, struct MemoryWriteByte* write, struct MemoryReadWord* read16, struct MemoryWriteWord* write16, int cpunum)
{
	M_MemoryRead8 = read;
	M_MemoryWrite8 = write;
	M_MemoryRead16 = read16;
	M_MemoryWrite16 = write16;
	active_cpu = cpunum;
	m68k_set_cpu_type(M68K_CPU_TYPE_68000);
	cpu_context_size = m68k_context_size();
	cpu_context[0] = (unsigned char*)malloc(cpu_context_size);
	m68k_pulse_reset();
	m68k_get_context(cpu_context[0]);
	m68k_set_context(cpu_context[0]);
	LOG_INFO("PC:%08X\tSP:%08X\n", m68k_get_reg(NULL, M68K_REG_PC), m68k_get_reg(NULL, M68K_REG_SP));
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
// Notes:
// - Musashi does not expose a non-destructive "elapsed since last query" API,
//   so for 68000 we do NOT add a pending component (avoid double counting).
// -----------------------------------------------------------------------------
int get_exact_cyclecount(int cpu)
{
	int pending = 0;

	switch (Machine->gamedrv->cpu[cpu].cpu_type)
	{
	case CPU_MZ80:  pending = m_cpu_z80[cpu]->mz80GetElapsedTicks(0);   break;
	case CPU_M6502: pending = m_cpu_6502[cpu]->get6502ticks(0);         break;
	case CPU_8080:  pending = m_cpu_i8080[cpu]->get_ticks(0);           break;
	case CPU_M6809: pending = m_cpu_6809[cpu]->get6809ticks(0);         break;
	case CPU_68000: pending = 0; // avoid double count with Musashi
		break;
	default:        pending = 0; break;
	}
	return tickcount[cpu] + pending;
}

// **************************************************************************
// Used by several games
void add_eterna_ticks(int cpunum, int ticks)
{
	eternaticks[cpunum] += ticks;
	if (eternaticks[cpunum] > 0xffffff) eternaticks[cpunum] = 0;
}

int get_eterna_ticks(int cpunum)
{
	return eternaticks[cpunum];
}
// *************************************************************************

// THis one is needed, OmegaRace, AVG Code.
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
	int temp = 0;
	int v = vid_tickcount;
	if (reset) {
		vid_tickcount = 0;
	}
	else {
		switch (Machine->gamedrv->cpu[0].cpu_type) {
		case CPU_MZ80:  temp = m_cpu_z80[0]->mz80GetElapsedTicks(0); break;
		case CPU_M6502: temp = m_cpu_6502[0]->get6502ticks(0); break;
		case CPU_68000: temp = m68k_cycles_run(); break;
		case CPU_M6809: temp = m_cpu_6809[0]->get6809ticks(0); break;
		}
	}
	return v + temp;
}

void aae_set_lines_per_frame(int lines) {
	if (lines > 0) s_lines_per_frame = lines;
}

static inline int aae_cycles_per_frame_cpu0(void) {
	const int fps = Machine->gamedrv->fps;          // frames per second
	const int cpu_hz = Machine->gamedrv->cpu[0].cpu_freq;  // CPU0 clock
	if (fps <= 0 || cpu_hz <= 0) return 1;
	int cpf = cpu_hz / fps;
	return (cpf > 0) ? cpf : 1;
}

int aae_cpu_getscanlinecycles(void) {
	int cpf = aae_cycles_per_frame_cpu0();
	int cpl = cpf / s_lines_per_frame;
	return (cpl > 0) ? cpl : 1;
}

// cycles since the beginning of THIS frame (CPU0-based)
int aae_cpu_getcurrentcycles_in_frame(void) {
	const int cpf = aae_cycles_per_frame_cpu0();
	const int now = get_exact_cyclecount(0);   // scheduler’s monotonic CPU0 cycle counter
	//LOG_INFO("Now with get_elapsed_ticks %d with exact_ticks %d", now, get_exact_cyclecount(0));
	if (cpf <= 0) return 0;
	return now % cpf;                       // phase-within-frame in cycles
}

int aae_cpu_getscanline(void) {
	const int cpl = aae_cpu_getscanlinecycles();
	const int cyc = aae_cpu_getcurrentcycles_in_frame();
	return cyc / cpl;                       // 0 .. s_lines_per_frame-1
}

/* cpu change op-code memory base */
void cpu_setOPbaseoverride(int (*f)(int))
{
	setOPbasefunc = f;
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

	case CPU_MZ80:
		return m_cpu_z80[active_cpu]->GetPC();
		break;

	case CPU_M6502:
		return m_cpu_6502[active_cpu]->get_pc();
		break;

	case CPU_M6809:
		return m_cpu_6809[active_cpu]->get_pc();
		break;

	case CPU_68000:
		LOG_INFO("PC:%08X\tSP:%08X\n", m68k_get_reg(NULL, M68K_REG_PC), m68k_get_reg(NULL, M68K_REG_SP));
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

void cpu_setcontext(int cpunum)
{
	// Disabled for now, I have only single CPU 68000 games, and this was causing issues.
	//switch (Machine->gamedrv->cpu_type[cpunum])
	//{
	//case CPU_68000: m68k_set_context(cpu_context[0]); break;
	//}
}

void cpu_getcontext(int cpunum)
{
	// Disabled for now, I have only single CPU 68000 games, and this was causing issues.
	//	switch (Machine->gamedrv->cpu_type[cpunum])
	//	{
	//	case CPU_68000: m68k_get_context(cpu_context[0]); break;
	//	}
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

	switch (Machine->gamedrv->cpu[cpunum].cpu_type)
	{
	case CPU_8080:
		// 8080 only has maskable INTR (no true NMI in standard parts)
		if (int_type == INT_TYPE_INT) {
			m_cpu_i8080[cpunum]->interrupt(interrupt_vector[cpunum]);
		}
		// else: silently ignore unsupported NMI on 8080
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

	case CPU_68000: {
		// m68k_set_irq expects a level 1..7; clamp anything outside that range.
		int level = int_type;
		if (level < 1) level = 1;
		if (level > 7) level = 7;
		m68k_set_irq(level);
		break;
	}

	default:
		// Unknown/unsupported CPU type: do nothing
		break;
	}
}
/*
void cpu_do_interrupt(int int_type, int cpunum)
{
	if (interrupt_enable[cpunum] == 0) return;

	interrupt_count[cpunum]++;

	if (interrupt_count[cpunum] == Machine->gamedrv->cpu_intpass_per_frame[cpunum])
	{
		switch (Machine->gamedrv->cpu_type[cpunum])
		{
		case CPU_MZ80:
			if (int_type == INT_TYPE_NMI) m_cpu_z80[cpunum]->mz80nmi();
			else                          m_cpu_z80[cpunum]->mz80int(interrupt_vector[cpunum]);
			break;
		case CPU_M6502:
			if (int_type == INT_TYPE_NMI) m_cpu_6502[cpunum]->nmi6502();
			else                          m_cpu_6502[cpunum]->irq6502();
			break;
		case CPU_M6809:
			if (int_type == INT_TYPE_NMI) m_cpu_6809[cpunum]->m6809_Cause_Interrupt(M6809_INT_NMI);
			else                          m_cpu_6809[cpunum]->m6809_Cause_Interrupt(M6809_INT_IRQ);
			break;
		case CPU_68000:
			m68k_set_irq(int_type);
			break;
		}
		interrupt_count[cpunum] = 0;
	}
}
*/
//***************************************************************************
int cpu_getiloops(void)
{
	return iloops[active_cpu];
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

//TBD SOON AS POSSIBLE, add a check to make sure every scheduled interrupt per pass per cpu has been taken, if not take it at the end of the run.
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
		ticks = m_cpu_6502[active_cpu]->get6502ticks(0xff);
		break;

	case CPU_8080:
		m_cpu_i8080[cpu]->exec(cycles);
		ticks = m_cpu_i8080[cpu]->get_ticks(0xff);
		timer_update(ticks, active_cpu);
		break;

	case CPU_M6809:
		m_cpu_6809[cpu]->exec6809(cycles);
		ticks = m_cpu_6809[cpu]->get6809ticks(0xff);
		break;
	case CPU_68000:
		ticks = m68k_execute(cycles);
		//LOG_INFO("Cycles Executed here %d", cycles);
		//LOG_INFO("PC:%08X\tSP:%08X\n", m68k_get_reg(NULL, M68K_REG_PC), m68k_get_reg(NULL, M68K_REG_SP));
		timer_update(ticks, active_cpu);
		break;
	case CPU_CCPU:
		ticks = run_ccpu(cycles);
		break;
	}
	// Update the cyclecount and the interrupt timers.

	//if (Machine->gamedrv->cpu_type[active_cpu] != CPU_M6502)
	cyclecount[cpu] += ticks;
	// NOTE THE CPU CODE ITSELF IS UPDATING THE TIMERS NOW, except for the 68000 and the 8080
	//timer_update(ticks, active_cpu);
	return ticks;
}

void cpu_run(void)
{
	int ran, target_cycles, next_interrupt_cycles;

	// reset per-frame accumulators
	tickcount[0] = tickcount[1] = tickcount[2] = tickcount[3] = 0;
	totalcycles[0] = totalcycles[1] = totalcycles[2] = totalcycles[3] = 0;
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

	// Track, per CPU, which per-CPU slice index we've already advanced to
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

			// Map global slice to this CPU's per-CPU slice index in [1..divs]
			// We only run when this mapped index advances beyond what we've already done.
			const int next_idx_for_cpu = (int)(((current_slice + 1) * divs) / max_divisions);
			if (next_idx_for_cpu <= last_idx_for_cpu[active_cpu]) continue;

			// Target cycles for THIS CPU at end of its next per-CPU slice
			target_cycles = (divs > 0) ? ((cycles_per_frame[active_cpu] * next_idx_for_cpu) / divs) : 0;

			if (totalcpu > 1) { cpu_setcontext(active_cpu); }

			// Compute the next interrupt boundary (in cycles since frame start) for THIS CPU
			// Only meaningful if we have interrupt passes this frame
			if (iloops[active_cpu] >= 0 && intpasses[active_cpu] > 0) {
				next_interrupt_cycles =
					(cycles_per_frame[active_cpu] * (intpasses[active_cpu] - iloops[active_cpu])) / intpasses[active_cpu];
			}
			else {
				// No interrupt expected; push boundary past our target so it won’t trigger
				next_interrupt_cycles = target_cycles;
			}

			// Run until we reach the per-CPU slice target, respecting the next interrupt boundary
			while (ran_this_frame[active_cpu] < target_cycles)
			{
				int running;
				if (ran_this_frame[active_cpu] < next_interrupt_cycles && next_interrupt_cycles < target_cycles)
					running = next_interrupt_cycles - ran_this_frame[active_cpu];
				else
					running = target_cycles - ran_this_frame[active_cpu];

				// Safety: avoid zero/negative runs
				if (running <= 0) break;

				ran = cpu_exec_now(active_cpu, running);

				// Scheduler-owned accounting
				tickcount[active_cpu] += ran;
				add_eterna_ticks(active_cpu, ran);
				if (active_cpu == 0) { vid_tickcount += ran; }

				ran_this_frame[active_cpu] += ran;
				totalcycles[active_cpu] += ran;

				// Handle interrupt pass boundary
				if (iloops[active_cpu] >= 0 && intpasses[active_cpu] > 0 &&
					ran_this_frame[active_cpu] >= next_interrupt_cycles)
				{
					if (Machine->gamedrv->cpu[active_cpu].int_cpu){
						Machine->gamedrv->cpu[active_cpu].int_cpu();
					}
					iloops[active_cpu]--;

					// Recompute next interrupt boundary for this CPU (if more left this frame)
					if (iloops[active_cpu] >= 0) {
						next_interrupt_cycles =
							(cycles_per_frame[active_cpu] * (intpasses[active_cpu] - iloops[active_cpu])) / intpasses[active_cpu];
					}
					else {
						next_interrupt_cycles = target_cycles;
					}
				}
			}

			if (totalcpu > 1) { cpu_getcontext(active_cpu); }

			// We have advanced this CPU to its mapped per-CPU slice index
			last_idx_for_cpu[active_cpu] = next_idx_for_cpu;
		} // for each CPU
	} // for each global slice

	// End of CPU Update, update and check frame counter
	cpu_framecounter++;
	if (cpu_framecounter > 0xffff) { cpu_framecounter = 0; }
}

void cpu_reset(int cpunum)
{
	LOG_INFO("CPU RESET CALLED!!----------");

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
	case CPU_68000:  m68k_pulse_reset();
		break;
	case CPU_M6809:
		m_cpu_6809[cpunum]->reset6809();
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
	const int cur = get_exact_cyclecount(cpu);
	double t = (max > 0) ? (double)cur / (double)max : 0.0;
	if (t > 1.0) t = 0.99;
	return (int)(val * t);
}

// -----------------------------------------------------------------------------
// free_cpu_memory
// Pair delete with new; also free Musashi context.
// -----------------------------------------------------------------------------
void free_cpu_memory()
{
	LOG_INFO("Freeing allocated CPU Cores. Totalcpu = %d", totalcpu);

	for (int x = 0; x < totalcpu; x++)
	{
		switch (Machine->gamedrv->cpu[x].cpu_type)
		{
		case CPU_MZ80:   delete m_cpu_z80[x];   m_cpu_z80[x] = nullptr; break;
		case CPU_M6502:  delete m_cpu_6502[x];  m_cpu_6502[x] = nullptr; break;
		case CPU_8080:   delete m_cpu_i8080[x]; m_cpu_i8080[x] = nullptr; break;
		case CPU_M6809:  delete m_cpu_6809[x];  m_cpu_6809[x] = nullptr; break;
		case CPU_68000:  /* nothing allocated here per-CPU */             break;
		default: break;
		}
	}

	if (cpu_context[0]) { free(cpu_context[0]); cpu_context[0] = nullptr; }
}

void init_cpu_config()
{
	int x;
	LOG_INFO("Starting CPU init");
	totalcpu = 0;
	cpu_configured = 0;
	active_cpu = 0;

	for (x = 0; x < 4; x++)
	{
		reset_cpu_status[x] = 0;
		cpurunning[x] = 1;
		interrupt_count[x] = 0;
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
			init68k(C.memory_read, C.memory_write, C.read16, C.write16, i);
			break;

		case CPU_MZ80:
			LOG_INFO("Init z80 %d called", i);
			init_z80(C.memory_read, C.memory_write, C.port_read, C.port_write, i);
			break;

		case CPU_M6502:
			// You said: "I am going to just change the mem_top code so that it's always 0xffff"
			LOG_INFO("Init 6502 %d called",i);
			init6502(C.memory_read, C.memory_write, /*mem_top*/0xFFFF, i);
			break;

		case CPU_8080:
			init8080(C.memory_read, C.memory_write, C.port_read, C.port_write, i);
			break;

		case CPU_M6809:
			LOG_INFO("Init 6809 %d called", i);
			init6809(C.memory_read, C.memory_write, i);
			break;

		default:
			// Unknown/unsupported CPU type in driver.
			break;
		}
	}

	cpu_framecounter = 0;
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

READ_HANDLER(watchdog_reset_r)
{
	timer_reset(watchdog_timer, TIME_IN_HZ(4));
	return 0;
}

// Write Rom
void watchdog_reset_w16(UINT32 address, UINT16 data, struct MemoryWriteWord* pMemWrite)
{
	timer_reset(watchdog_timer, TIME_IN_HZ(4));
}

//Read Ram
UINT8 MRA_RAM(UINT32 address, struct MemoryReadByte* psMemRead)
{
	return Machine->memory_region[active_cpu][address + psMemRead->lowAddr];
}

//Write Ram
void MWA_RAM(UINT32 address, UINT8 data, struct MemoryWriteByte* pMemWrite)
{
	//LOG_INFO("Address here is %x Writing address %x ", address, address + pMemWrite->lowAddr);

	Machine->memory_region[active_cpu][address + pMemWrite->lowAddr] = data;
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

/*--------------------------------------------------------------------------*/
/* 68000 Memory handlers                                                          */
/*--------------------------------------------------------------------------*/

unsigned int m68k_read_bus_8(unsigned int address)
{
	return 0;
}

unsigned int m68k_read_bus_16(unsigned int address)
{
	return 0;
}

void m68k_unused_w(unsigned int address, unsigned int value)
{
	//error("Unused %08X = %08X (%08X)\n", address, value, Turbo68KReadPC());
}

void m68k_unused_8_w(unsigned int address, unsigned int value)
{
	//error("Unused %08X = %02X (%08X)\n", address, value, Turbo68KReadPC());
}

void m68k_unused_16_w(unsigned int address, unsigned int value)
{
	//error("Unused %08X = %04X (%08X)\n", address, value, Turbo68KReadPC());
}

void m68k_lockup_w_8(unsigned int address, unsigned int value)
{
	m68k_end_timeslice();
}

void m68k_lockup_w_16(unsigned int address, unsigned int value)
{
	m68k_end_timeslice();
}

unsigned int m68k_lockup_r_8(unsigned int address)
{
	m68k_end_timeslice();
	return -1;
}

unsigned int m68k_lockup_r_16(unsigned int address)
{
	m68k_end_timeslice();
	return -1;
}

/*--------------------------------------------------------------------------*/
/* 68000 memory handlers                                                    */
/*--------------------------------------------------------------------------*/

unsigned int m68k_read_memory_8(unsigned int address)
{
	MemoryReadByte* MemRead = M_MemoryRead8;

	while (MemRead->lowAddr != 0xffffffff)
	{
		if (address >= MemRead->lowAddr && address <= MemRead->highAddr)
		{
			if (MemRead->memoryCall)
			{
				return (UINT8)(MemRead->memoryCall(address - MemRead->lowAddr, MemRead));
			}
			else
			{
				return (UINT8)READ_BYTE((unsigned char*)MemRead->pUserArea, address - MemRead->lowAddr);
			}
		}
		++MemRead;
	}

	LOG_INFO("Unhandled Memory 8 Read: addr: %x", address);
	return 0;
}

void m68k_write_memory_8(unsigned int address, unsigned int value)
{
	//int k=0;
	MemoryWriteByte* MemWrite = M_MemoryWrite8;
	//LOG_INFO("Memory 8 Write: addr: %x value %x", address, value);
	while (MemWrite->lowAddr != 0xffffffff)
	{
		if (address >= MemWrite->lowAddr && address <= MemWrite->highAddr)
		{
			if (MemWrite->memoryCall)
			{
				//k = 1;
				MemWrite->memoryCall(address - MemWrite->lowAddr, (UINT8)value, MemWrite);
			}
			else
			{
				//k = 1;
				WRITE_BYTE((unsigned char*)MemWrite->pUserArea, address - MemWrite->lowAddr, (UINT8)value);
			}
		}
		MemWrite++;
	}
	//if (!k) { LOG_INFO("Unhandled Memory 8 Write: addr: %x value %x", address, value); }
}

unsigned int m68k_read_memory_16(unsigned int address)
{
	MemoryReadWord* MemRead = M_MemoryRead16;

	while (MemRead->lowAddr != 0xffffffff)
	{
		if (address >= MemRead->lowAddr && address <= MemRead->highAddr)
		{
			if (MemRead->memoryCall)
			{
				//LOG_INFO("Handler 16 Read: addr: %x", address);
				return (UINT16)(MemRead->memoryCall(address - MemRead->lowAddr, MemRead));
			}
			else
			{
				//LOG_INFO("MEM 16 Read: addr: %x", address);
				return (UINT16)READ_WORD((unsigned char*)MemRead->pUserArea, address - MemRead->lowAddr);
			}
		}

		++MemRead;
	}

	LOG_INFO("Unhandled Read 16: %x ", address); //exit(1);
	return 0;
}

void m68k_write_memory_16(unsigned int address, unsigned int value)
{
	//LOG_INFO("Write Memory 16, addr: %x, data %x", address, value);
//	int k = 0;
	MemoryWriteWord* MemWrite = M_MemoryWrite16;

	while (MemWrite->lowAddr != 0xffffffff)
	{
		if (address >= MemWrite->lowAddr && address <= MemWrite->highAddr)
		{
			if (MemWrite->memoryCall)
			{
				//	k = 1;
					//LOG_INFO("Write Handler 16, addr: %x, data %x", address, value);
				MemWrite->memoryCall(address - MemWrite->lowAddr, (UINT16)value, MemWrite);
			}
			else {
				//k = 1;
				//LOG_INFO("Write Memory 16, addr: %x, data %x", address, value);
				WRITE_WORD((unsigned char*)MemWrite->pUserArea, address - MemWrite->lowAddr, (UINT16)value);
			}
		}
		MemWrite++;
	}
	//if (!k)
	//{
	//	LOG_INFO("Unhandled Memory 16 Write: addr: %x data: %x", address, value);
		//exit(1);
	//}
}

unsigned int m68k_read_memory_32(unsigned int address)
{
	//LOG_INFO("Reading Memory 32, addr: %x", address);

	/* Split into 2 reads */
	return (UINT32)(m68k_read_memory_16(address + 0) << 16 | m68k_read_memory_16(address + 2));
}

void m68k_write_memory_32(unsigned int address, unsigned int value)
{
	//LOG_INFO("Write Memory 32, addr: %x, data %x\n", address, value);
	/* Split into 2 writes */
	m68k_write_memory_16(address, (value >> 16) & 0xFFFF);
	m68k_write_memory_16(address + 2, value & 0xFFFF);
}