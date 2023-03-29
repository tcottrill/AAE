#ifndef _M8080_H_
#define _M8080_H_

#include "aaemain.h"

static int iInvadersShiftData1, iInvadersShiftData2 , iInvadersShiftAmount;
UINT8 port0;
UINT8 port1;
UINT8 port2;


UINT8 bInvadersPlayer;
UINT8 bInvadersDipswitch;
UINT8 flip;
int screenmem [224][256];
int c;
UINT8 scolor;
int horiz;
int screenflip;
int screen_support_red;
int screen_red;
int player2;
int background;
int aoverlay; 
int special;
int lrt_voice;

void InvadersOut(UINT32 addr, UINT8 data, struct MemoryWriteByte *pMemWrite);

UINT16 InvadersShiftDataRead(UINT16 port, struct z80PortRead *pPR);
UINT16 InvadersPlayerRead(UINT16 port, struct z80PortRead *pPR);
UINT16 InvadersDipswitchRead(UINT16 port, struct z80PortRead *pPR);
UINT16 ClownsPlayerRead(UINT16 port, struct z80PortRead *pPR);
UINT16 GmissilePlayerRead(UINT16 port, struct z80PortRead *pPR);
UINT16 AstroPalPlayerRead(UINT16 port, struct z80PortRead *pPR);
UINT16 BootHillShiftDataRead(UINT16 port, struct z80PortRead *pPR);
UINT16 boothill_port_0_r(UINT16 port, struct z80PortRead *pPR);
UINT16 boothill_port_1_r(UINT16 port, struct z80PortRead *pPR);
void InvadersShiftAmountWrite(UINT16 port, UINT8 data, struct z80PortWrite *pPW);
void InvadersShiftDataWrite(UINT16 port, UINT8 data, struct z80PortWrite *pPW);
void InvadersSoundPort3Write(UINT16 port, UINT8 data, struct z80PortWrite *pPW);
void InvadersSoundPort5Write(UINT16 port, UINT8 data, struct z80PortWrite *pPW);
void InvadersSoundPort1Write(UINT16 port, UINT8 data, struct z80PortWrite *pPW);
void LrescueSoundPort3Write(UINT16 port, UINT8 data, struct z80PortWrite *pPW);
void LrescueSoundPort5Write(UINT16 port, UINT8 data, struct z80PortWrite *pPW);
void Drawscreen(void);


int init_si(void);
void message (void);
void reset_all(void);
void imageLoader(void);
void soundInit(void);
void DrawSpecial(void);
//void gameparse(int argc, char *argv[]);
#define BITM(x,n) (((x)>>(n))&1)
#define SHIFT  (((((iInvadersShiftData1 << 8) | iInvadersShiftData2) << (iInvadersShiftAmount & 0x07)) >> 8) & 0xff)
#define BITSWAP8(val,B7,B6,B5,B4,B3,B2,B1,B0) \
		((BITM(val,B7) << 7) | \
		 (BITM(val,B6) << 6) | \
		 (BITM(val,B5) << 5) | \
		 (BITM(val,B4) << 4) | \
		 (BITM(val,B3) << 3) | \
		 (BITM(val,B2) << 2) | \
		 (BITM(val,B1) << 1) | \
		 (BITM(val,B0) << 0))

static const int BootHillTable[8] = {
	0x00, 0x40, 0x60, 0x70, 0x30, 0x10, 0x50, 0x50
};



#endif