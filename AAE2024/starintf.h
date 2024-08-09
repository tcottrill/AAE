
#ifndef _STARINTF_H_
#define _STARINTF_H_

#include "starcpu.h"

#define CP                  m68k_context[m68k_context_index]

#define MAX_REGION          (16+1)  /* Total number of regions */
#define MAX_68000           (3)     /* # of 68k's we can emulate at once */

#define INDEX_FETCH         (0)     /* Offsets into the index array */
#define INDEX_READBYTE      (1)
#define INDEX_READWORD      (2)
#define INDEX_WRITEBYTE     (3)
#define INDEX_WRITEWORD     (4)


/* Holds all the information for a single 68000 CPU. */
typedef struct
{
    unsigned char *context;
    unsigned char index[5];
    struct STARSCREAM_PROGRAMREGION fetch[MAX_REGION];
    struct STARSCREAM_DATAREGION readbyte[MAX_REGION];
    struct STARSCREAM_DATAREGION readword[MAX_REGION];
    struct STARSCREAM_DATAREGION writebyte[MAX_REGION];
    struct STARSCREAM_DATAREGION writeword[MAX_REGION];
}t_m68k;

/* Global data */
extern int m68k_context_index;
extern t_m68k m68k_context[MAX_68000];

/* Function prototypes */

int m68k_init(void);
void m68k_switch(int num);

int m68k_reset(void);
int m68k_execute(int cycles);
int m68k_cause_interrupt(int level);
void m68k_flush_interrupts(void);
int m68k_readpc(void);
void m68k_stop(void);
void m68k_clear_cycles(void);
int m68k_get_elapsed_cycles(void);
int m68k_fetch(int address);

/* Add a new entry to the fetch or data regions */
void m68k_add_fetch(unsigned lowaddr, unsigned highaddr, unsigned offset);
void m68k_add_readbyte(unsigned lowaddr, unsigned highaddr, void *memorycall, void *userdata);
void m68k_add_readword(unsigned lowaddr, unsigned highaddr, void *memorycall, void *userdata);
void m68k_add_writebyte(unsigned lowaddr, unsigned highaddr, void *memorycall, void *userdata);
void m68k_add_writeword(unsigned lowaddr, unsigned highaddr, void *memorycall, void *userdata);
void m68k_add_write(unsigned lowaddr, unsigned highaddr, void *memorycall, void *userdata);
void m68k_add_read(unsigned lowaddr, unsigned highaddr, void *memorycall, void *userdata);
void m68k_add_readwrite(unsigned lowaddr, unsigned highaddr, void *memorycall, void *userdata);

/* Modify the data pointers for a fetch or data region */
void m68k_modify_fetch_data(unsigned lowaddr, unsigned highaddr, unsigned offset);
void m68k_modify_readbyte_data(unsigned lowaddr, unsigned highaddr, void *memorycall, void *userdata);
void m68k_modify_readword_data(unsigned lowaddr, unsigned highaddr, void *memorycall, void *userdata);
void m68k_modify_writebyte_data(unsigned lowaddr, unsigned highaddr, void *memorycall, void *userdata);
void m68k_modify_writeword_data(unsigned lowaddr, unsigned highaddr, void *memorycall, void *userdata);
void m68k_modify_write_data(unsigned lowaddr, unsigned highaddr, void *memorycall, void *userdata);
void m68k_modify_read_data(unsigned lowaddr, unsigned highaddr, void *memorycall, void *userdata);
void m68k_modify_readwrite_data(unsigned lowaddr, unsigned highaddr, void *memorycall, void *userdata);

/* Modify the addresss for the data pointers in a fetch or data region */
void m68k_modify_fetch_addr(unsigned lowaddr, unsigned highaddr, unsigned offset);
void m68k_modify_readbyte_addr(unsigned lowaddr, unsigned highaddr, void *memorycall, void *userdata);
void m68k_modify_readword_addr(unsigned lowaddr, unsigned highaddr, void *memorycall, void *userdata);
void m68k_modify_writebyte_addr(unsigned lowaddr, unsigned highaddr, void *memorycall, void *userdata);
void m68k_modify_writeword_addr(unsigned lowaddr, unsigned highaddr, void *memorycall, void *userdata);
void m68k_modify_write_addr(unsigned lowaddr, unsigned highaddr, void *memorycall, void *userdata);
void m68k_modify_read_addr(unsigned lowaddr, unsigned highaddr, void *memorycall, void *userdata);
void m68k_modify_readwrite_addr(unsigned lowaddr, unsigned highaddr, void *memorycall, void *userdata);
 
#endif /* _STARINTF_H_ */
