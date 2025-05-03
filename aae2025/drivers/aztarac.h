#ifndef AZTARAC_H
#define AZTARAC_H


int init_aztarac();
void end_aztarac();
void run_aztarac();

void  aztarac_interrupt();
void  aztarac_sound_interrupt();

void aztarac_nvram_handler(void* file, int read_or_write);
#endif
