#ifndef OMEGRACE_H
#define OMEGRACE_H

int init_omega();
void run_omega();
void end_omega();

extern void  omega_interrupt();
extern void  omega_nmi_interrupt();
extern void nvram_handler(void* file, int read_or_write);

#endif