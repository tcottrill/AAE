// -----------------------------------------------------------------------------
// Legacy MAME-Derived Module
// This file contains code or design traits originally developed as part of 
// the M.A.M.E.(TM) Project.
// Portions of this file remain under the copyright of the original MAME authors
// and contributors. It has since been adapted and merged into the AAE (Another
// Arcade Emulator) project.
//
// Integration:
//   This module is now part of the **AAE (Another Arcade Emulator)** codebase
//   and is integrated with its rendering, input, and emulation subsystems.
//
// Licensing Notice:
//   - Original portions of this code remain @ the M.A.M.E.(TM) Project and its
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
//   This file is originally part of and copyright the M.A.M.E.(TM) Project.
//   For more information about MAME licensing, see the original MAME source
//   distribution and its associated license files.
//
// -----------------------------------------------------------------------------

#ifndef CPUINTFAAE_H
#define CPUINTFAAE_H

#include <stdio.h>
#include "deftypes.h"
#include "cpu_6502.h"
#include "cpu_z80.h"
#include "cpu_m6809.h"
#include "cpu_m6800.h"
#include "cpu_i8080.h"
#include "cpu_i8085.h"
#include "cpu_i8039.h"
#include "cpu_m68000.h"



#define MAX_IRQ_LINES   8       /* maximum number of IRQ lines per CPU */

#define CLEAR_LINE		0		/* clear (a fired, held or pulsed) line */
#define ASSERT_LINE     1       /* assert an interrupt immediately */
#define HOLD_LINE       2       /* hold interrupt line until enable is true */
#define PULSE_LINE		3		/* pulse interrupt line for one instruction */
/*
*This value is passed to cpu_get_reg to retrieve the previous
* program counter value, ie.before a CPU emulation started
* to fetch opcodes and arguments for the current instrution.
*/
#define REG_PREVIOUSPC	-1

/*
 * This value is passed to cpu_get_reg/cpu_set_reg, instead of one of
 * the names from the enum a CPU core defines for it's registers,
 * to get or set the contents of the memory pointed to by a stack pointer.
 * You can specify the n'th element on the stack by (REG_SP_CONTENTS-n),
 * ie. lower negative values. The actual element size (UINT16 or UINT32)
 * depends on the CPU core.
 * This is also used to replace the cpu_geturnpc() function.
 */
#define REG_SP_CONTENTS -2


#define MAX_CPU 4

extern cpu_z80* m_cpu_z80[MAX_CPU];
extern cpu_6502* m_cpu_6502[MAX_CPU];
extern cpu_m6809* m_cpu_6809[MAX_CPU];
extern cpu_m6800* m_cpu_6800[MAX_CPU];
extern cpu_i8080* m_cpu_i8080[MAX_CPU];
extern cpu_i8085* m_cpu_i8085[MAX_CPU];
extern cpu_i8039* m_cpu_i8039[MAX_CPU];
extern cpu_m68000* m_cpu_68000[MAX_CPU];

enum
{
	CPU_NONE,
	CPU_MZ80,
	CPU_8080,
	CPU_8085,
	CPU_M6502,
	CPU_M6502Z,
	CPU_M6809,
	CPU_68000,
	CPU_CCPU,
	CPU_8039,
	CPU_8035,
	// The 6800, 6802 and 6808 are instruction-set identical and all run on the
	// cpu_m6800 core. Three names so a driver can say which part the board
	// actually carries; a 6802's on-chip RAM is mapped by the driver.
	CPU_M6800,
	CPU_M6802,
	CPU_M6808,
	CPU_COUNT
};


// These are for different translation units. Maybe bite the bullet and make them all non-static, or wrap in namespaces?
// This is a temporary solution.
#define READ_HANDLER_NS(name)   UINT8 name(UINT32 address, struct MemoryReadByte *psMemRead)
#define WRITE_HANDLER_NS(name)   void name(UINT32 address, UINT8 data, struct MemoryWriteByte *psMemWrite)

//CPU and WRITE READ HANDLERS WHY ARE THESE HERE!!!!
#define READ_HANDLER(name)  static UINT8 name(UINT32 address, struct MemoryReadByte *psMemRead)
#define WRITE_HANDLER(name)  static void name(UINT32 address, UINT8 data, struct MemoryWriteByte *psMemWrite)

// Same signatures, but with EXTERNAL linkage - for the handful of handlers a
// header exports to other translation units. Using the static macros above for
// those is a latent defect: the header promises an external symbol while the
// definition provides an internal one. MSVC accepts the contradiction; g++
// rejects it ("declared 'extern' and later 'static'").
//
// Use these ONLY when a header really does declare the handler. If a driver
// wants its own private version of an exported name, give it a driver-prefixed
// name instead - two public definitions of the same symbol is a duplicate at
// link time (see dkong_interrupt_enable_w, bwidow_avgdvg_reset_w).
#define READ_HANDLER_PUBLIC(name)  UINT8 name(UINT32 address, struct MemoryReadByte *psMemRead)
#define WRITE_HANDLER_PUBLIC(name)  void name(UINT32 address, UINT8 data, struct MemoryWriteByte *psMemWrite)

#define READ16_HANDLER(name)  static UINT16 name(UINT32 address, struct MemoryReadWord *psMemRead)
#define WRITE16_HANDLER(name)  static void name(UINT32 address, UINT16 data, struct MemoryWriteWord *psMemWrite)

#define MEM_WRITE(name) struct MemoryWriteByte name[] = {
#define MEM_READ(name)  struct MemoryReadByte name[] = {
#define MEM_WRITE16(name) struct MemoryWriteWord name[] = {
#define MEM_READ16(name)  struct MemoryReadWord name[] = {
// For 8-bit memory handlers that also specify a base pointer
#define MEM_ADDR8(start, end, routine, base) { (start), (end), (routine), (base) },
// For 16-bit memory handlers (word access)
#define MEM_ADDR16(start, end, routine, base) { (start), (end), (routine), (base) },

#define MEM_ADDR(start,end,routine) {start,end,routine},
#define MEM_END {(UINT32) -1,(UINT32) -1,NULL}};

//CPU PORT HANDLERS FOR the Z80 AGAIN, WHY ARE THESE HERE?
#define PORT_WRITE_HANDLER(name) static void name(UINT16 port, UINT8 data, struct z80PortWrite *pPW)
#define PORT_READ_HANDLER(name) static UINT16 name(UINT16 port, struct z80PortRead *pPR)
#define PORT_WRITE(name) struct z80PortWrite name[] = {
#define PORT_READ(name) struct z80PortRead name[] = {
#define PORT_ADDR(start,end,routine) {start,end,routine},
#define PORT_END {(UINT16) -1, (UINT16) -1,NULL}};




#define READ_BYTE(BASE, ADDR) (BASE)[(ADDR)^1]
#define READ_WORD(BASE, ADDR) (((BASE)[(ADDR)+1]<<8) |          \
                              (BASE)[(ADDR)])
#define READ_LONG(BASE, ADDR) (((BASE)[(ADDR)+1]<<24) |         \
                               ((BASE)[(ADDR)+0]<<16) |      \
                               ((BASE)[(ADDR)+3]<<8) |       \
                                (BASE)[(ADDR)+2])

#define WRITE_BYTE(BASE, ADDR, VAL) (BASE)[(ADDR)^1] = (VAL)&0xff
#define WRITE_WORD(BASE, ADDR, VAL) (BASE)[(ADDR)+1] = ((VAL)>>8) & 0xff;       \
                                    (BASE)[ADDR] = (VAL)&0xff
#define WRITE_LONG(BASE, ADDR, VAL) (BASE)[(ADDR)+1] = ((VAL)>>24) & 0xff;      \
                                    (BASE)[(ADDR)+0] = ((VAL)>>16)&0xff;    \
                                    (BASE)[(ADDR)+3] = ((VAL)>>8)&0xff;     \
                                    (BASE)[(ADDR)+2] = (VAL)&0xff

#define INT_TYPE_NONE 0
#define INT_TYPE_NMI  5
#define INT_TYPE_INT  6
#define INT_TYPE_68K1 1
#define INT_TYPE_68K2 2
#define INT_TYPE_68K3 3
#define INT_TYPE_68K4 4
#define INT_TYPE_68K5 5
#define INT_TYPE_68K6 6
#define INT_TYPE_68K7 7

void init6502(struct MemoryReadByte* read, struct MemoryWriteByte* write, int mem_top, int cpunum);
void init6809(struct MemoryReadByte* read, struct MemoryWriteByte* write, int cpunum);
void init6800(struct MemoryReadByte* read, struct MemoryWriteByte* write, int cpunum);
void init_z80(struct MemoryReadByte* read, struct MemoryWriteByte* write, struct z80PortRead* portread, struct z80PortWrite* portwrite, int cpunum);
void init8080(struct MemoryReadByte* read, struct MemoryWriteByte* write, struct z80PortRead* portread, struct z80PortWrite* portwrite, int cpunum);
void init8085(struct MemoryReadByte* read, struct MemoryWriteByte* write, struct z80PortRead* portread, struct z80PortWrite* portwrite, int cpunum);
void init8039(struct MemoryReadByte* read, struct MemoryWriteByte* write, struct z80PortRead* portread, struct z80PortWrite* portwrite, int cpunum);
void init8035(struct MemoryReadByte* read, struct MemoryWriteByte* write, struct z80PortRead* portread, struct z80PortWrite* portwrite, int cpunum);

// Point a CPU's opcode fetches at a decrypted buffer (MAME memory_set_opcode_base).
void memory_set_opcode_base(int cpunum, unsigned char* base);


void cpu_setOPbaseoverride(int (*f)(int));
void cpu_setOPbase16(int apc);

// New Scanline Calculation
// cpu_control.h
int aae_cpu_getscanline(void);               // 0..(LINES_PER_FRAME-1)
int aae_cpu_getscanlinecycles(void);         // cycles per scanline, CPU0
int aae_cpu_getcurrentcycles_in_frame(void); // cycles since frame start, CPU0
void aae_set_lines_per_frame(int lines);     // optional: change 256/262 etc.

// Used for cycle to time scaling.
int cpu_scale_by_cycles(int val, int clock);
// CPU Code
void init_cpu_config();
//void run_cpus_to_cycles();

void add_eterna_ticks(int cpunum, int ticks);
int get_eterna_ticks(int cpunum);
int get_video_ticks(int val);

void cpu_needs_reset(int cpunum);
void cpu_reset(int cpunum);
void cpu_reset_all();
int get_active_cpu();
int cpu_getpc();
int cpu_getppc();
int get_elapsed_ticks(int cpunum);
void cpu_disable_interrupts(int cpunum, int val);
//Get the current cpu frame number
int cpu_getcurrentframe();

void cpu_do_int_imm(int cpunum, int int_type);
//void cpu_do_interrupt(int int_type, int cpunum);

int get_exact_cyclecount(int cpu);
void cpu_clear_cyclecount(int cpunum);
void cpu_clear_cyclecount_eof();
int cpu_getcycles_cpu(int cpu);
int cpu_getcycles(int reset);
int cpu_getcycles_remaining_cpu(int cpu);

int get_current_cpu();
void set_interrupt_vector(int data);
void cpu_clear_pending_interrupts(int cpunum);
void cpu_clear_pending_int(int int_type, int cpunum);

int cpu_getiloops(void);

// IPT_VBLANK support - returns 1 during VBLANK period, 0 otherwise
int cpu_getvblank(void);

// Clears the VBLANK flag. Called by inputport_vblank_end() or the frame loop
// to signal the end of the vertical blanking period.
void cpu_clear_vblank(void);
int cpu_exec_now(int cpu, int cycles);
void cpu_run(void);
void free_cpu_memory();
void cpu_enable(int cpunum, int val);

void interrupt_enable_w(UINT32 address, UINT8 data, struct MemoryWriteByte* pMemWrite);
void interrupt_vector_w(UINT16 address, UINT8 data, struct z80PortWrite* pPW);

//Watchdog Defines from cpu_handler.cpp
extern void watchdog_reset_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite);
void watchdog_reset_w16(UINT32 address, UINT16 data, struct MemoryWriteWord* pMemWrite);
extern UINT8 watchdog_reset_r(UINT32 address, struct MemoryReadByte* psMemRead);
extern void watchdog_callback(int param);
extern UINT8 MRA_RAM(UINT32 address, struct MemoryReadByte* psMemRead);
extern UINT8 MRA_ROM(UINT32 address, struct MemoryReadByte* psMemRead);
extern void MWA_ROM(UINT32 address, UINT8 data, struct MemoryWriteByte* pMemWrite);
extern void MWA_ROM16(UINT32 address, UINT16 data, struct MemoryWriteWord* pMemWrite);
extern void MWA_RAM(UINT32 address, UINT8 data, struct MemoryWriteByte* pMemWrite);
extern void MWA_NOP(UINT32 address, UINT8 data, struct MemoryWriteByte* pMemWrite);
extern void MWA_NOP16(UINT32 address, UINT16 data, struct MemoryWriteWord* pMemWrite);
extern UINT8 MRA_NOP(UINT32 address, struct MemoryReadByte* psMemRead);


#endif
