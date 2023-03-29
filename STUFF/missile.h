#ifndef MISSILE_H
#define MISSILE_H

#include "aaemain.h"


void MissileExecByCycleCount();
void IN0_Write(UINT32 address, UINT8 data, struct MemoryWriteByte *psMemWrite);
void PokeyWriteA(UINT32 address, UINT8 data, struct MemoryWriteByte *psMemWrite);
UINT8 IN0_Read (UINT32 address , struct MemoryReadByte *psMemRead);
UINT8 IN1_Read (UINT32 address , struct MemoryReadByte *psMemRead);
UINT8 Missile_Dip1 (UINT32 address , struct MemoryReadByte *psMemRead);
UINT8 Missile_Dip2 (UINT32 address , struct MemoryReadByte *psMemRead);

/*
void MissileRamWrite(UINT32 address, UINT8 data, struct MemoryWriteByte *psMemWrite);
void MissileVideo3Write(UINT32 address, UINT8 data, struct MemoryWriteByte *psMemWrite);

void MissileWriteFull(UINT32 address, UINT8 data, struct MemoryWriteByte *psMemWrite);

void MissileVideo2Write(UINT32 address, UINT8 data, struct MemoryWriteByte *psMemWrite);

UINT8 MissileReadFull (UINT32 address , struct MemoryReadByte *psMemRead);

void pokey1_w (address, data);
void missile_video_3rd_bit_w(int address, int data);
void missile_video2_w (int offset, int data);
void missile_video_w (int address,int data);
void missile_video_mult_w (int address, int data);
void missile_flip_screen (void);
void missile_w(int address, int data);
int missile_video_r (int address);
int missile_r (int address);
int pokey1_r (int address);
int missile_IN0_r(int offset);

UINT8 MissileIN0read (UINT32 address , struct MemoryReadByte *psMemRead);
UINT8 MissileIN1read (UINT32 address , struct MemoryReadByte *psMemRead);
UINT8 MissileDSW1 (UINT32 address, struct MemoryReadByte *psMemRead);
UINT8 MissileDSW2 (UINT32 address, struct MemoryReadByte *psMemRead);
*/




#endif