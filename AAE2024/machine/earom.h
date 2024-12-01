#ifndef EAROM_H
#define EAROM_H

#include "../osd_cpu.h"
//Copyright "The M.A.M.E Team"

//Todo, rename to match mame code.
void EaromWrite(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite);
void EaromCtrl(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite);
UINT8 EaromRead(UINT32 address, struct MemoryReadByte* psMemRead);
//void LoadEarom(void);
//void SaveEarom(void);

//int atari_vg_earom_load();
//void atari_vg_earom_save();
void atari_vg_earom_handler(void* file, int read_or_write);

#endif
