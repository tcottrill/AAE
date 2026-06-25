#ifndef EAROM_H
#define EAROM_H

//Copyright "The M.A.M.E Team"
#include "aae_mame_driver.h"

//Todo, rename to match mame code.
void EaromWrite(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite);
void EaromCtrl(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite);
UINT8 EaromRead(UINT32 address, struct MemoryReadByte* psMemRead);
void atari_vg_earom_handler(void* file, int read_or_write);

#endif
