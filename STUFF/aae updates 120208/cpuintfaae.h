#ifndef CPUINTFAAE_H
#define CPUINTFAAE_H

#include <stdio.h>


void initz80(struct MemoryReadByte *read, struct MemoryWriteByte *write, struct z80PortRead *portread, struct z80PortWrite *portwrite,int cpunum);
void init8080(struct MemoryReadByte *read, struct MemoryWriteByte *write, struct z80PortRead *portread, struct z80PortWrite *portwrite,int cpunum);
void init6502Z(struct MemoryReadByte *read, struct MemoryWriteByte *write,int cpunum);
void init6502(struct MemoryReadByte *read, struct MemoryWriteByte *write, int cpunum);
void init68k(struct STARSCREAM_PROGRAMREGION *fetch, struct STARSCREAM_DATAREGION *readbyte, struct STARSCREAM_DATAREGION *readword, struct STARSCREAM_DATAREGION *writebyte, struct STARSCREAM_DATAREGION *writeword);
int cpu_scale_by_cycles(int val, int clock);
void init_cpu_config();
void run_cpus_to_cycles();
int return_tickcount(int reset);
int get_video_ticks(int val);
void cpu_needs_reset(int cpunum);
void cpu_resetter(int cpunum);
int get_elapsed_ticks(int cpunum);
void cpu_disable_interrupts(int val);
void set_pending_interrupt(int int_type, int cpunum);
void process_pending_interrupts(int cpunum);
void cpu_do_interrupt(int int_type, int cpunum);
void add_eterna_ticks(int cpunum, int ticks);
int get_eterna_ticks(int cpunum);
void add_hertz_ticks(int cpunum, int ticks);
int get_hertz_counter();
int get_current_cpu();

#endif
