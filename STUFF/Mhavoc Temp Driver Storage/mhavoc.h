#ifndef MHAVOC_H
#define MHAVOC_H

#include "aaemain.h"


extern char *gamename[];

int init_mhavoc();
void run_mhavoc();
void end_mhavoc();

 void MH_generate_vector_list(void);
void MHled_write(UINT32 address, UINT8 data, struct MemoryWriteByte *psMemWrite);

UINT8 MHDSW1 (UINT32 address, struct MemoryReadByte *psMemRead);
UINT8 MHDSW2 (UINT32 address, struct MemoryReadByte *psMemRead);
UINT8 MHControls2 (UINT32 address, struct MemoryReadByte *psMemRead);

UINT8 MHIN0read (UINT32 address, struct MemoryReadByte *psMemRead);

#endif