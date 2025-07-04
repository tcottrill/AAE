
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

#ifndef CPUINTFAAE_H
#define CPUINTFAAE_H

#include <stdio.h>
#include "deftypes.h"
#include "cpu_6502.h"
#include "cpu_z80.h"
#include "cpu_6809.h"
#include "cpu_i8080.h"


#define NEW_INTERRUPT_SYSTEM    1
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
extern cpu_6809* m_cpu_6809[MAX_CPU];
extern cpu_i8080* m_cpu_i8080[MAX_CPU];
extern int cpu_context_size;
extern uint8_t* cpu_context[2];

enum
{
	CPU_NONE,
	CPU_MZ80,
	CPU_8080,
	CPU_M6502,
	CPU_M6502Z,
	CPU_M6809,
	CPU_68000,
	CPU_CCPU,
	CPU_COUNT
};



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
void init8080(struct MemoryReadByte* read, struct MemoryWriteByte* write, struct z80PortRead* portread, struct z80PortWrite* portwrite, int cpunum);
void init6809(struct MemoryReadByte* read, struct MemoryWriteByte* write, int cpunum);
void init_z80(struct MemoryReadByte* read, struct MemoryWriteByte* write, struct z80PortRead* portread, struct z80PortWrite* portwrite, int cpunum);
void init8080(struct MemoryReadByte* read, struct MemoryWriteByte* write, struct z80PortRead* portread, struct z80PortWrite* portwrite, int cpunum);
void init68k(struct MemoryReadByte* read, struct MemoryWriteByte* write, struct MemoryReadWord* read16, struct MemoryWriteWord* write16, int cpunum);

void cpu_setOPbaseoverride(int (*f)(int));
void cpu_setOPbase16(int apc);

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
void cpu_do_interrupt(int int_type, int cpunum);

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
int cpu_exec_now(int cpu, int cycles);
void cpu_run(void);
void free_cpu_memory();
void cpu_enable(int cpunum, int val);

void interrupt_enable_w(UINT32 address, UINT8 data, struct MemoryWriteByte* pMemWrite);
void interrupt_vector_w(UINT32 address, UINT8 data, struct MemoryWriteByte* pMemWrite);

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
