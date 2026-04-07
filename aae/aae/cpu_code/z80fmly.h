// ORIGINAL COPYRIGHT:
/***************************************************************************

  Z80 FMLY.C   Z80 FAMILY CHIP EMURATOR for MAME Ver.0.1 alpha

  Support chip :  Z80PIO , Z80CTC

  Copyright(C) 1997 Tatsuyuki Satoh.

  This version are tested starforce driver.

  8/21/97 -- Heavily modified by Aaron Giles to be much more accurate for the MCR games
  8/27/97 -- Rewritten a second time by Aaron Giles, with the datasheets in hand

pending:
    Z80CTC , Counter mode & Timer with Trigrt start :not support Triger level

***************************************************************************/
// This has been lightly overhauled, (BADLY) to work with AAE. STILL 
// REMAINS COPYRIGHT THE MAME TEAM.
// ---------------------------------------------------------------------------
// z80fmly.h - Z80 Family IC Emulation (Refactored for AAE)
// ---------------------------------------------------------------------------

#ifndef Z80FMLY_H
#define Z80FMLY_H

#include <functional>
#include "deftypes.h"

// Z80 Daisy Chain Interrupt States
#define Z80_INT_REQ 0x01
#define Z80_INT_IEO 0x02

// ---------------------------------------------------------------------------
// Z80 CTC 
// ---------------------------------------------------------------------------

#define MAX_CTC 2

#define NOTIMER_0 (1<<0)
#define NOTIMER_1 (1<<1)
#define NOTIMER_2 (1<<2)
#define NOTIMER_3 (1<<3)

struct z80ctc_interface
{
    int num;                                      // number of CTCs to emulate
    int baseclock[MAX_CTC];                       // timer clock
    int notimer[MAX_CTC];                         // timer disablers
    void (*intr[MAX_CTC])(int state);             // callback when change interrupt status
    std::function<void(int, int)> zc0[MAX_CTC];   // ZC/TO0 callback
    std::function<void(int, int)> zc1[MAX_CTC];   // ZC/TO1 callback
    std::function<void(int, int)> zc2[MAX_CTC];   // ZC/TO2 callback
};

void z80ctc_init(z80ctc_interface* intf);

double z80ctc_getperiod(int which, int ch);

void z80ctc_reset(int which);
void z80ctc_0_reset(void);
void z80ctc_1_reset(void);

void z80ctc_w(int which, int offset, int data);
void z80ctc_0_w(int offset, int data);
void z80ctc_1_w(int offset, int data);

int z80ctc_r(int which, int offset);
int z80ctc_0_r(int offset);
int z80ctc_1_r(int offset);

void z80ctc_trg_w(int which, int trg, int offset, int data);
void z80ctc_0_trg0_w(int offset, int data);
void z80ctc_0_trg1_w(int offset, int data);
void z80ctc_0_trg2_w(int offset, int data);
void z80ctc_0_trg3_w(int offset, int data);
void z80ctc_1_trg0_w(int offset, int data);
void z80ctc_1_trg1_w(int offset, int data);
void z80ctc_1_trg2_w(int offset, int data);
void z80ctc_1_trg3_w(int offset, int data);

// Z80 DaisyChain control
int z80ctc_interrupt(int which);
void z80ctc_reti(int which);

// ---------------------------------------------------------------------------
// Z80 PIO 
// ---------------------------------------------------------------------------

#define MAX_PIO 1

struct z80pio_interface
{
    int num;                                      // number of PIOs to emulate
    void (*intr[MAX_PIO])(int state);             // callback when change interrupt status
    void (*rdyA[MAX_PIO])(int data);              // portA ready active callback
    void (*rdyB[MAX_PIO])(int data);              // portB ready active callback
};

void z80pio_init(z80pio_interface* intf);
void z80pio_reset(int which);
void z80pio_d_w(int which, int ch, int data);
void z80pio_c_w(int which, int ch, int data);
int z80pio_c_r(int which, int ch);
int z80pio_d_r(int which, int ch);

// set/clear /astb input
void z80pio_astb_w(int which, int state);
// set/clear /bstb input
void z80pio_bstb_w(int which, int state);

void z80pio_p_w(int which, int ch, int data);
int z80pio_p_r(int which, int ch);

// Z80 DaisyChain control
int z80pio_interrupt(int which);
void z80pio_reti(int which);

// MAME interface standard functions
void z80pio_0_reset(void);
void z80pio_0_w(int offset, int data);
int z80pio_0_r(int offset);

void z80pioA_0_p_w(int offset, int data);
void z80pioB_0_p_w(int offset, int data);
int z80pioA_0_p_r(int offset);
int z80pioB_0_p_r(int offset);

#endif