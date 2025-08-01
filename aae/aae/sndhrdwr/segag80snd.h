#pragma once

#ifndef SEGAG80_SND_H
#define SEGAG80_SND_H

#include "deftypes.h"

#define SEGA_SHIFT 10	/* do not use a higher value. Values will overflow */
#define NUM_SPACFURY_SPEECH 21 //31
#define NUM_ZEKTOR_SPEECH 19
#define NUM_STARTREK_SPEECH 23
extern int NUM_SPEECH_SAMPLES;


int sega_sh_start(void);
void sega_sh_stop();
void tacscan_sh_update(void);
void sega_sh_speech_w(UINT16 port, UINT8 data, struct z80PortWrite* pPW);

void sega_sh_update(void);
void elim1_sh_w(UINT16 port, UINT8 data, struct z80PortWrite* pPW);
void elim2_sh_w(UINT16 port, UINT8 data, struct z80PortWrite* pPW);
void spacfury1_sh_w(UINT16 port, UINT8 data, struct z80PortWrite* pPW);
void StarTrek_sh_w(UINT16 port, UINT8 data, struct z80PortWrite* pPW);
void spacfury2_sh_w(UINT16 port, UINT8 data, struct z80PortWrite* pPW);
void Zektor1_sh_w(UINT16 port, UINT8 data, struct z80PortWrite* pPW);
void Zektor2_sh_w(UINT16 port, UINT8 data, struct z80PortWrite* pPW);
void Zektor_AY8910_w(UINT16 port, UINT8 data, struct z80PortWrite* pPW);
void TacScan_sh_w(UINT16 port, UINT8 data, struct z80PortWrite* pPW);

#endif
