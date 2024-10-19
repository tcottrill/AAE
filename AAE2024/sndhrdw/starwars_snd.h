#ifndef STARWARS_SND_H
#define STARWARS_SND_H


#include "deftypes.h"


int  starwars_sh_start(void);
void starwars_sh_stop(void);
void starwars_sh_update(void);


void starwars_snd_interrupt();

UINT8 main_read_r(UINT32 address, struct MemoryReadByte* psMemRead);
UINT8 main_ready_flag_r(UINT32 address, struct MemoryReadByte* psMemRead);

void main_wr_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite);
void soundrst(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite);


UINT8 m6532_r(UINT32 address, struct MemoryReadByte* psMemRead);
UINT8 sin_r(UINT32 address, struct MemoryReadByte* psMemRead);


void sout_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite);
void m6532_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite);



#endif
