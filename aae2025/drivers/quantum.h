#ifndef QUANTUM_H
#define QUANTUM_H


int init_quantum();
void end_quantum();
void run_quantum();

void  quantum_interrupt();
void quantum_nvram_handler(void* file, int read_or_write);
#endif
