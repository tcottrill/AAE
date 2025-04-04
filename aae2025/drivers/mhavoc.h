#ifndef MHAVOC_H
#define MHAVOC_H

void mhavoc_interrupt();
void run_mhavoc();
int init_mhavoc();
void end_mhavoc();




void MH_generate_vector_list(void);
extern void mhavoc_nvram_handler(void* file, int read_or_write);

#endif