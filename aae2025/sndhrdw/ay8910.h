#ifndef _AY8910_H_
#define _AY8910_H_

//==========================================================================
// AAE is a poorly written M.A.M.E (TM) derivitave based on early MAME 
// code, 0.29 through .90 mixed with code of my own. This emulator was 
// created solely for my amusement and learning and is provided only 
// as an archival experience. 
// 
// All MAME code used and abused in this emulator remains the copyright 
// of the dedicated people who spend countless hours creating it. All
// MAME code should be annotated as belonging to the MAME TEAM.
// 
// THE CODE BELOW IS FROM MAME and COPYRIGHT the MAME TEAM.  
//==========================================================================

/***************************************************************************

  ay8910.h/c


  Emulation of the AY-3-8910 sound chip.

  Based on various code snippets by Ville Hallik, Michael Cuddy,
  Tatsuyuki Satoh, Fabrice Frances, Nicola Salmoria.

***************************************************************************/

#include "aae_mame_driver.h"

#define MAX_8910 4
#define ALL_8910_CHANNELS -1


typedef unsigned char BYTE;   // 8-bit unsigned entity.
typedef BYTE *        pBYTE;  // Pointer to BYTE.

struct AY8910interface
{
   int num;
	int baseclock;
   int vol[MAX_8910];
   int volshift[MAX_8910];
   int (*portAread[MAX_8910])(void);
   int (*portBread[MAX_8910])(void);
	void (*portAwrite[MAX_8910])(int offset,int data);
	void (*portBwrite[MAX_8910])(int offset,int data);
};

void AY8910_reset(int chip);

void AY8910_set_clock(int chip,int _clock);
void AY8910_set_volume(int chip,int channel,int volume);


void AY8910Write(int chip,int a,int data);
int AY8910Read(int chip);


UINT16 AY8910_read_port_0_r(UINT16 offset, struct z80PortRead *zpr);
UINT16 AY8910_read_port_1_r(UINT16 offset, struct z80PortRead *zpr);
UINT16 AY8910_read_port_2_r(UINT16 offset, struct z80PortRead *zpr);
UINT16 AY8910_read_port_3_r(UINT16 offset, struct z80PortRead *zpr);
UINT16 AY8910_read_port_4_r(UINT16 offset, struct z80PortRead *zpr);

void AY8910_control_port_0_w(UINT16 offset, UINT8 value, struct z80PortWrite *zpw);
void AY8910_control_port_1_w(UINT16 offset, UINT8 value, struct z80PortWrite *zpw);
void AY8910_control_port_2_w(UINT16 offset, UINT8 value, struct z80PortWrite *zpw);
void AY8910_control_port_3_w(UINT16 offset, UINT8 value, struct z80PortWrite *zpw);
void AY8910_control_port_4_w(UINT16 offset, UINT8 value, struct z80PortWrite *zpw);

void AY8910_write_port_0_w(UINT16 offset, UINT8 value, struct z80PortWrite *zpw);
void AY8910_write_port_1_w(UINT16 offset, UINT8 value, struct z80PortWrite *zpw);
void AY8910_write_port_2_w(UINT16 offset, UINT8 value, struct z80PortWrite *zpw);
void AY8910_write_port_3_w(UINT16 offset, UINT8 value, struct z80PortWrite *zpw);
void AY8910_write_port_4_w(UINT16 offset, UINT8 value, struct z80PortWrite *zpw);

// MEM Port Handlers
void AY8910_control_port_0_w(UINT32 offset, UINT8 value, struct MemoryWriteByte* mwb);
void AY8910_control_port_1_w(UINT32 offset, UINT8 value, struct MemoryWriteByte* mwb);
void AY8910_control_port_2_w(UINT32 offset, UINT8 value, struct MemoryWriteByte* mwb);
void AY8910_control_port_3_w(UINT32 offset, UINT8 value, struct MemoryWriteByte* mwb);
void AY8910_write_port_0_w(UINT32 offset, UINT8 value, struct MemoryWriteByte* mwb);
void AY8910_write_port_1_w(UINT32 offset, UINT8 value, struct MemoryWriteByte* mwb);
void AY8910_write_port_2_w(UINT32 offset, UINT8 value, struct MemoryWriteByte* mwb);
void AY8910_write_port_3_w(UINT32 offset, UINT8 value, struct MemoryWriteByte* mwb);

UINT8 AY8910_read_port_0_r(UINT32 address, struct MemoryReadByte* psMemRead);
UINT8 AY8910_read_port_1_r(UINT32 address, struct MemoryReadByte* psMemRead);
UINT8 AY8910_read_port_2_r(UINT32 address, struct MemoryReadByte* psMemRead);
UINT8 AY8910_read_port_3_r(UINT32 address, struct MemoryReadByte* psMemRead);
UINT8 AY8910_read_port_4_r(UINT32 address, struct MemoryReadByte* psMemRead);

int AY8910_sh_start(struct AY8910interface *ayinterface);
void AY8910_sh_update(void);
void AY8910clear(void);
void AY8910partupdate(int chip);
#endif
