#ifndef MHAVOC_H
#define MHAVOC_H

#include "aaemain.h"

void mhavoc_interrupt();
void run_mhavoc();
extern int cpu_scale_by_cycles_mh(int val);
//int MHscale_by_cycles(int val, int clock);
int init_mhavoc();
void end_mhavoc();
void mhavoc_interrupt();
void MH_generate_vector_list(void);

extern void mhavoc_nvram_handler(void* file, int read_or_write);

#endif