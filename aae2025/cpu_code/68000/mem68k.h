
#ifndef _MEM68K_H_
#define _MEM68K_H_
#include "deftypes.h"


/* Function prototypes */
unsigned int m68k_read_bus_8(unsigned int address);
unsigned int m68k_read_bus_16(unsigned int address);
void m68k_unused_w(unsigned int address, unsigned int value);

void m68k_lockup_w_8(unsigned int address, unsigned int value);
void m68k_lockup_w_16(unsigned int address, unsigned int value);
unsigned int m68k_lockup_r_8(unsigned int address);
unsigned int m68k_lockup_r_16(unsigned int address);


extern unsigned int m68k_read_memory_8(unsigned int address);
extern unsigned int m68k_read_memory_16(unsigned int address);
extern unsigned int m68k_read_memory_32(unsigned int address);
extern void m68k_write_memory_8(unsigned int address, unsigned int value);
extern void m68k_write_memory_16(unsigned int address, unsigned int value);
extern void m68k_write_memory_32(unsigned int address, unsigned int value);




#endif /* _MEM68K_H_ */
