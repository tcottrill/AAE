#ifndef _AY8910_H_
#define _AY8910_H_

#define MAX_8910 3
#define ALL_8910_CHANNELS -1
#define SAMPLESSOFAR  100 // (int)(actsamples*getcyclesthisframe())/getcyclesperframe()

#include "..\osd_cpu.h"
typedef unsigned char BYTE;
typedef unsigned short int WORD;
typedef BYTE* pBYTE;
typedef WORD* pWORD;

struct AY8910interface
{
	int num;
	int baseclock;
	int vol[MAX_8910];
	int volshift[MAX_8910];
	int (*portAread[MAX_8910])(void);
	int (*portBread[MAX_8910])(void);
	void (*portAwrite[MAX_8910])(int offset, int data);
	void (*portBwrite[MAX_8910])(int offset, int data);
};

void AY8910_reset(int chip);

void AY8910_set_clock(int chip, int _clock);
void AY8910_set_volume(int chip, int channel, int volume);

void AY8910Write(int chip, int a, int data);
int AY8910Read(int chip);

UINT16 AY8910_read_port_0_r(UINT16 offset, struct z80PortRead* zpr);
UINT16 AY8910_read_port_1_r(UINT16 offset, struct z80PortRead* zpr);
UINT16 AY8910_read_port_2_r(UINT16 offset, struct z80PortRead* zpr);
UINT16 AY8910_read_port_3_r(UINT16 offset, struct z80PortRead* zpr);
UINT16 AY8910_read_port_4_r(UINT16 offset, struct z80PortRead* zpr);

void AY8910_control_port_0_w(UINT16 offset, UINT8 value, struct z80PortWrite* zpw);
void AY8910_control_port_1_w(UINT16 offset, UINT8 value, struct z80PortWrite* zpw);
void AY8910_control_port_2_w(UINT16 offset, UINT8 value, struct z80PortWrite* zpw);
void AY8910_control_port_3_w(UINT16 offset, UINT8 value, struct z80PortWrite* zpw);
void AY8910_control_port_4_w(UINT16 offset, UINT8 value, struct z80PortWrite* zpw);

void AY8910_write_port_0_w(UINT16 offset, UINT8 value, struct z80PortWrite* zpw);
void AY8910_write_port_1_w(UINT16 offset, UINT8 value, struct z80PortWrite* zpw);
void AY8910_write_port_2_w(UINT16 offset, UINT8 value, struct z80PortWrite* zpw);
void AY8910_write_port_3_w(UINT16 offset, UINT8 value, struct z80PortWrite* zpw);
void AY8910_write_port_4_w(UINT16 offset, UINT8 value, struct z80PortWrite* zpw);

void memAY8910_control_port_0_w(UINT32 offset, UINT8 value, struct MemoryWriteByte* mwb);
void memAY8910_write_port_0_w(UINT32 offset, UINT8 value, struct MemoryWriteByte* mwb);
void memAY8910_control_port_1_w(UINT32 offset, UINT8 value, struct MemoryWriteByte* mwb);
void memAY8910_write_port_1_w(UINT32 offset, UINT8 value, struct MemoryWriteByte* mwb);

int AY8910_sh_start(const struct AY8910interface* ayinterface);
void AY8910_sh_update(void);
void AY8910clear(void);
void AY8910partupdate(int chip);
//void AY8910Update(int chip,unsigned char *buffer,int length);
#endif