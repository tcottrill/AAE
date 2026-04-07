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
// This has been lightly overhauled, (BADLY) to work with AAE. 
// STILL REMAINS COPYRIGHT THE MAME TEAM.
// ---------------------------------------------------------------------------
// z80fmly.cpp - Z80 Family IC Emulation (Refactored for AAE)
// ---------------------------------------------------------------------------

#include <stdio.h>
#include <cstring>
#include "z80fmly.h"
#include "sys_log.h"
#include "timer.h"

// ---------------------------------------------------------------------------
// Z80 CTC Emulation
// ---------------------------------------------------------------------------

struct z80ctc
{
    int vector;                 // interrupt vector
    int clock;                  // system clock
    double invclock16;          // 16/system clock
    double invclock256;         // 256/system clock
    void (*intr)(int which);    // interrupt callback
    std::function<void(int, int)> zc[4]; // zero crossing callbacks
    int notimer;                // no timer masks
    int mask[4];                // masked channel flags
    int mode[4];                // current mode
    int tconst[4];              // time constant
    int down[4];                // down counter (clock mode only)
    int extclk[4];              // current signal from the external clock
    int timer[4];               // array of active timers (ID ints for AAE)

    int int_state[4];           // interrupt status (for daisy chain)
};

static z80ctc ctcs[MAX_CTC];

#define INTERRUPT        0x80
#define INTERRUPT_ON     0x80
#define INTERRUPT_OFF    0x00

#define MODE             0x40
#define MODE_TIMER       0x00
#define MODE_COUNTER     0x40

#define PRESCALER        0x20
#define PRESCALER_256    0x20
#define PRESCALER_16     0x00

#define EDGE             0x10
#define EDGE_FALLING     0x00
#define EDGE_RISING      0x10

#define TRIGGER          0x08
#define TRIGGER_AUTO     0x00
#define TRIGGER_CLOCK    0x08

#define CONSTANT         0x04
#define CONSTANT_LOAD    0x04
#define CONSTANT_NONE    0x00

#define RESET            0x02
#define RESET_CONTINUE   0x00
#define RESET_ACTIVE     0x02

#define CONTROL          0x01
#define CONTROL_VECTOR   0x00
#define CONTROL_WORD     0x01

#define WAITING_FOR_TRIG 0x100

static void z80ctc_timercallback(int param);

void z80ctc_init(z80ctc_interface* intf)
{
    memset(ctcs, 0, sizeof(ctcs));

    for (int i = 0; i < intf->num; i++)
    {
        ctcs[i].clock = intf->baseclock[i];
        ctcs[i].invclock16 = 16.0 / (double)intf->baseclock[i];
        ctcs[i].invclock256 = 256.0 / (double)intf->baseclock[i];
        ctcs[i].notimer = intf->notimer[i];
        ctcs[i].intr = intf->intr[i];
        ctcs[i].zc[0] = intf->zc0[i];
        ctcs[i].zc[1] = intf->zc1[i];
        ctcs[i].zc[2] = intf->zc2[i];
        ctcs[i].zc[3] = nullptr;

        for (int ch = 0; ch < 4; ch++) {
            ctcs[i].timer[ch] = -1;
        }

        z80ctc_reset(i);
    }
}

double z80ctc_getperiod(int which, int ch)
{
    z80ctc* ctc = ctcs + which;
    ch &= 3;
    int mode = ctc->mode[ch];

    if ((mode & RESET) == RESET_ACTIVE) return 0;
    if ((mode & MODE) == MODE_COUNTER)
    {
        LOG_INFO("CTC %d is CounterMode : Can't calculate period\n", ch);
        return 0;
    }

    double clock = ((mode & PRESCALER) == PRESCALER_16) ? ctc->invclock16 : ctc->invclock256;
    return clock * (double)ctc->tconst[ch];
}

static void z80ctc_interrupt_check(z80ctc* ctc)
{
    int state = 0;
    for (int ch = 3; ch >= 0; ch--)
    {
        if (ctc->int_state[ch] & Z80_INT_IEO) state = ctc->int_state[ch];
        else                                  state |= ctc->int_state[ch];
    }
    if (ctc->intr) (*ctc->intr)(state);
}

void z80ctc_reset(int which)
{
    z80ctc* ctc = ctcs + which;

    for (int i = 0; i < 4; i++)
    {
        ctc->mode[i] = RESET_ACTIVE;
        ctc->tconst[i] = 0x100;
        if (ctc->timer[i] != -1)
        {
            timer_remove(ctc->timer[i]);
            ctc->timer[i] = -1;
        }
        ctc->int_state[i] = 0;
    }
    z80ctc_interrupt_check(ctc);
}

void z80ctc_0_reset(void) { z80ctc_reset(0); }
void z80ctc_1_reset(void) { z80ctc_reset(1); }

void z80ctc_w(int which, int offset, int data)
{
    z80ctc* ctc = ctcs + which;
    int ch = offset & 3;
    int mode = ctc->mode[ch];

    if ((mode & CONSTANT) == CONSTANT_LOAD)
    {
        ctc->tconst[ch] = data ? data : 0x100;
        ctc->mode[ch] &= ~CONSTANT;
        ctc->mode[ch] &= ~RESET;

        if ((mode & MODE) == MODE_TIMER)
        {
            if ((mode & TRIGGER) == TRIGGER_AUTO)
            {
                double clock = ((mode & PRESCALER) == PRESCALER_16) ? ctc->invclock16 : ctc->invclock256;
                if (ctc->timer[ch] != -1) {
                    timer_remove(ctc->timer[ch]);
                    ctc->timer[ch] = -1;
                }
                if (!(ctc->notimer & (1 << ch)))
                    ctc->timer[ch] = timer_set(clock * (double)ctc->tconst[ch], (which << 2) + ch, z80ctc_timercallback);
            }
            else {
                ctc->mode[ch] |= WAITING_FOR_TRIG;
            }
        }
        ctc->down[ch] = ctc->tconst[ch];
        return;
    }

    if ((data & CONTROL) == CONTROL_VECTOR && ch == 0)
    {
        ctc->vector = data & 0xf8;
        return;
    }

    if ((data & CONTROL) == CONTROL_WORD)
    {
        ctc->mode[ch] = data;

        if ((data & RESET) == RESET_ACTIVE)
        {
            if (ctc->timer[ch] != -1) {
                timer_remove(ctc->timer[ch]);
                ctc->timer[ch] = -1;
            }

            if (ctc->int_state[ch] != 0)
            {
                ctc->int_state[ch] = 0;
                z80ctc_interrupt_check(ctc);
            }
        }
        return;
    }
}

void z80ctc_0_w(int offset, int data) { z80ctc_w(0, offset, data); }
void z80ctc_1_w(int offset, int data) { z80ctc_w(1, offset, data); }

int z80ctc_r(int which, int ch)
{
    z80ctc* ctc = ctcs + which;
    ch &= 3;
    int mode = ctc->mode[ch];

    if ((mode & MODE) == MODE_COUNTER) {
        return ctc->down[ch];
    }
    else {
        double clock = ((mode & PRESCALER) == PRESCALER_16) ? ctc->invclock16 : ctc->invclock256;
        if (ctc->timer[ch] != -1) {
            // Divide remaining time (in sec) by the clock period (in sec) to get cycles remaining
            return ((int)(timer_timeleft(ctc->timer[ch]) / clock) + 1) & 0xff;
        }
        else {
            return 0;
        }
    }
}

int z80ctc_0_r(int offset) { return z80ctc_r(0, offset); }
int z80ctc_1_r(int offset) { return z80ctc_r(1, offset); }

int z80ctc_interrupt(int which)
{
    z80ctc* ctc = ctcs + which;
    int ch;

    for (ch = 0; ch < 4; ch++)
    {
        if (ctc->int_state[ch])
        {
            if (ctc->int_state[ch] == Z80_INT_REQ)
                ctc->int_state[ch] = Z80_INT_IEO;
            break;
        }
    }
    if (ch > 3)
    {
        LOG_INFO("CTC entry INT : non IRQ\n");
        ch = 0;
    }
    z80ctc_interrupt_check(ctc);
    return ctc->vector + ch * 2;
}

void z80ctc_reti(int which)
{
    z80ctc* ctc = ctcs + which;
    for (int ch = 0; ch < 4; ch++)
    {
        if (ctc->int_state[ch] & Z80_INT_IEO)
        {
            ctc->int_state[ch] &= ~Z80_INT_IEO;
            break;
        }
    }
    z80ctc_interrupt_check(ctc);
}

static void z80ctc_timercallback(int param)
{
    int which = param >> 2;
    int ch = param & 3;
    z80ctc* ctc = ctcs + which;

    if ((ctc->mode[ch] & INTERRUPT) == INTERRUPT_ON)
    {
        if (!(ctc->int_state[ch] & Z80_INT_REQ))
        {
            ctc->int_state[ch] |= Z80_INT_REQ;
            z80ctc_interrupt_check(ctc);
        }
    }

    if (ctc->zc[ch])
    {
        ctc->zc[ch](0, 1);
        ctc->zc[ch](0, 0);
    }
    ctc->down[ch] = ctc->tconst[ch];
}

void z80ctc_trg_w(int which, int trg, int offset, int data)
{
    z80ctc* ctc = ctcs + which;
    int ch = trg & 3;
    data = data ? 1 : 0;
    int mode = ctc->mode[ch];

    if (data != ctc->extclk[ch])
    {
        ctc->extclk[ch] = data;

        if (((mode & EDGE) == EDGE_RISING && data) || ((mode & EDGE) == EDGE_FALLING && !data))
        {
            if ((mode & WAITING_FOR_TRIG) && (mode & MODE) == MODE_TIMER)
            {
                double clock = ((mode & PRESCALER) == PRESCALER_16) ? ctc->invclock16 : ctc->invclock256;
                if (ctc->timer[ch] != -1) {
                    timer_remove(ctc->timer[ch]);
                    ctc->timer[ch] = -1;
                }
                if (!(ctc->notimer & (1 << ch)))
                    ctc->timer[ch] = timer_set(clock * (double)ctc->tconst[ch], (which << 2) + ch, z80ctc_timercallback);
            }
            ctc->mode[ch] &= ~WAITING_FOR_TRIG;

            if ((mode & MODE) == MODE_COUNTER)
            {
                ctc->down[ch]--;
                if (!ctc->down[ch])
                    z80ctc_timercallback((which << 2) + ch);
            }
        }
    }
}

void z80ctc_0_trg0_w(int offset, int data) { z80ctc_trg_w(0, 0, offset, data); }
void z80ctc_0_trg1_w(int offset, int data) { z80ctc_trg_w(0, 1, offset, data); }
void z80ctc_0_trg2_w(int offset, int data) { z80ctc_trg_w(0, 2, offset, data); }
void z80ctc_0_trg3_w(int offset, int data) { z80ctc_trg_w(0, 3, offset, data); }
void z80ctc_1_trg0_w(int offset, int data) { z80ctc_trg_w(1, 0, offset, data); }
void z80ctc_1_trg1_w(int offset, int data) { z80ctc_trg_w(1, 1, offset, data); }
void z80ctc_1_trg2_w(int offset, int data) { z80ctc_trg_w(1, 2, offset, data); }
void z80ctc_1_trg3_w(int offset, int data) { z80ctc_trg_w(1, 3, offset, data); }


// ---------------------------------------------------------------------------
// Z80 PIO Emulation
// ---------------------------------------------------------------------------

#define PIO_MODE0      0x00
#define PIO_MODE1      0x01
#define PIO_MODE2      0x02
#define PIO_MODE3      0x03
#define PIO_OP_MODE    0x0f
#define PIO_OP_INTC    0x07
#define PIO_OP_INTE    0x03
#define PIO_INT_ENABLE 0x80
#define PIO_INT_AND    0x40
#define PIO_INT_HIGH   0x20
#define PIO_INT_MASK   0x10

struct z80pio
{
    int vector[2];
    void (*intr)(int which);
    void (*rdyr[2])(int data);
    int mode[2];
    int enable[2];
    int mask[2];
    int dir[2];
    int rdy[2];
    int in[2];
    int out[2];
    int strobe[2];
    int int_state[2];
};

static z80pio pios[MAX_PIO];

static void z80pio_set_rdy(z80pio* pio, int ch, int state)
{
    pio->rdy[ch] = state;
    if (pio->rdyr[ch] != nullptr)
        pio->rdyr[ch](pio->rdy[ch]);
}

void z80pio_init(z80pio_interface* intf)
{
    memset(pios, 0, sizeof(pios));
    for (int i = 0; i < intf->num; i++)
    {
        pios[i].intr = intf->intr[i];
        pios[i].rdyr[0] = intf->rdyA[i];
        pios[i].rdyr[1] = intf->rdyB[i];
        z80pio_reset(i);
    }
}

static void z80pio_interrupt_check(z80pio* pio)
{
    int state;
    if (pio->int_state[1] & Z80_INT_IEO) state = Z80_INT_IEO;
    else                                  state = pio->int_state[1];

    if (pio->int_state[0] & Z80_INT_IEO) state = Z80_INT_IEO;
    else                                  state |= pio->int_state[0];

    if (pio->intr) (*pio->intr)(state);
}

static void z80pio_check_irq(z80pio* pio, int ch)
{
    int irq = 0;
    int data;

    if (pio->enable[ch] & PIO_INT_ENABLE)
    {
        if (pio->mode[ch] == PIO_MODE3)
        {
            data = pio->in[ch] & pio->dir[ch];
            data &= ~pio->mask[ch];
            if (!(pio->enable[ch] & PIO_INT_HIGH))
                data ^= pio->mask[ch];
            if (pio->enable[ch] & PIO_INT_AND)
            {
                if (data == pio->mask[ch]) irq = 1;
            }
            else { if (data == 0) irq = 1; }

            if (ch && (pio->mode[0] == PIO_MODE2))
            {
                if (pio->rdy[ch] == 0) irq = 1;
            }
        }
        else if (pio->rdy[ch] == 0) irq = 1;
    }

    int old_state = pio->int_state[ch];
    if (irq) pio->int_state[ch] |= Z80_INT_REQ;
    else     pio->int_state[ch] &= ~Z80_INT_REQ;

    if (old_state != pio->int_state[ch])
        z80pio_interrupt_check(pio);
}

void z80pio_reset(int which)
{
    z80pio* pio = pios + which;
    for (int i = 0; i <= 1; i++) {
        pio->mask[i] = 0xff;
        pio->enable[i] = 0x00;
        pio->mode[i] = 0x01;
        pio->dir[i] = 0x01;
        z80pio_set_rdy(pio, i, 0);
        pio->out[i] = 0x00;
        pio->int_state[i] = 0;
        pio->strobe[i] = 0;
    }
    z80pio_interrupt_check(pio);
}

void z80pio_d_w(int which, int ch, int data)
{
    z80pio* pio = pios + which;
    if (ch) ch = 1;

    pio->out[ch] = data;
    switch (pio->mode[ch]) {
    case PIO_MODE0:
    case PIO_MODE2:
        z80pio_set_rdy(pio, ch, 1);
        z80pio_check_irq(pio, ch);
        return;
    case PIO_MODE1:
    case PIO_MODE3:
        return;
    default:
        break;
    }
}

void z80pio_c_w(int which, int ch, int data)
{
    z80pio* pio = pios + which;
    if (ch) ch = 1;

    if (pio->mode[ch] == 0x13) {
        pio->dir[ch] = data;
        pio->mode[ch] = 0x03;
        return;
    }
    if (pio->enable[ch] & PIO_INT_MASK) {
        pio->mask[ch] = data;
        pio->enable[ch] &= ~PIO_INT_MASK;
        return;
    }
    switch (data & 0x0f) {
    case PIO_OP_MODE:
        pio->mode[ch] = (data >> 6);
        if (pio->mode[ch] == 0x03) pio->mode[ch] = 0x13;
        break;
    case PIO_OP_INTC:
        pio->enable[ch] = data & 0xf0;
        pio->mask[ch] = 0x00;
        break;
    case PIO_OP_INTE:
        pio->enable[ch] &= ~PIO_INT_ENABLE;
        pio->enable[ch] |= (data & PIO_INT_ENABLE);
        break;
    default:
        if (!(data & 1)) {
            pio->vector[ch] = data;
        }
    }
    z80pio_check_irq(pio, ch);
}

int z80pio_c_r(int which, int ch) {
    if (ch) ch = 1;
    return 0;
}

int z80pio_d_r(int which, int ch)
{
    z80pio* pio = pios + which;
    if (ch) ch = 1;

    switch (pio->mode[ch]) {
    case PIO_MODE0:
        return pio->out[ch];
    case PIO_MODE1:
        z80pio_set_rdy(pio, ch, 1);
        z80pio_check_irq(pio, ch);
        return pio->in[ch];
    case PIO_MODE2:
        z80pio_set_rdy(pio, 1, 1);
        z80pio_check_irq(pio, ch);
        return pio->in[ch];
    case PIO_MODE3:
        return (pio->in[ch] & pio->dir[ch]) | (pio->out[ch] & ~pio->dir[ch]);
    }
    return 0;
}

int z80pio_interrupt(int which)
{
    z80pio* pio = pios + which;
    int ch = 0;

    if (pio->int_state[0] == Z80_INT_REQ) {
        pio->int_state[0] |= Z80_INT_IEO;
    }
    else if (pio->int_state[0] == 0) {
        ch = 1;
        if (pio->int_state[1] == Z80_INT_REQ) {
            pio->int_state[1] |= Z80_INT_IEO;
        }
        else {
            ch = 0;
        }
    }
    z80pio_interrupt_check(pio);
    return pio->vector[ch];
}

void z80pio_reti(int which)
{
    z80pio* pio = pios + which;

    if (pio->int_state[0] & Z80_INT_IEO) {
        pio->int_state[0] &= ~Z80_INT_IEO;
    }
    else if (pio->int_state[1] & Z80_INT_IEO) {
        pio->int_state[1] &= ~Z80_INT_IEO;
    }
    z80pio_interrupt_check(pio);
}

void z80pio_p_w(int which, int ch, int data)
{
    z80pio* pio = pios + which;
    if (ch) ch = 1;

    pio->in[ch] = data;
    switch (pio->mode[ch]) {
    case PIO_MODE0:
        break;
    case PIO_MODE2:
        ch = 1;
    case PIO_MODE1:
        z80pio_set_rdy(pio, ch, 0);
        z80pio_check_irq(pio, ch);
        break;
    case PIO_MODE3:
        z80pio_check_irq(pio, ch);
        break;
    }
}

int z80pio_p_r(int which, int ch)
{
    z80pio* pio = pios + which;
    if (ch) ch = 1;

    switch (pio->mode[ch]) {
    case PIO_MODE2:
    case PIO_MODE0:
        z80pio_set_rdy(pio, ch, 0);
        z80pio_check_irq(pio, ch);
        break;
    case PIO_MODE1:
        break;
    case PIO_MODE3:
        return (pio->in[ch] & pio->dir[ch]) | (pio->out[ch] & ~pio->dir[ch]);
    }
    return pio->out[ch];
}

void z80pio_0_reset(void) { z80pio_reset(0); }

void z80pio_0_w(int offset, int data) {
    if (offset & 1) z80pio_c_w(0, (offset / 2) & 1, data);
    else         z80pio_d_w(0, (offset / 2) & 1, data);
}

int z80pio_0_r(int offset) {
    return (offset & 1) ? z80pio_c_r(0, (offset / 2) & 1) : z80pio_d_r(0, (offset / 2) & 1);
}

void z80pioA_0_p_w(int offset, int data) { z80pio_p_w(0, 0, data); }
void z80pioB_0_p_w(int offset, int data) { z80pio_p_w(0, 1, data); }
int z80pioA_0_p_r(int offset) { return z80pio_p_r(0, 0); }
int z80pioB_0_p_r(int offset) { return z80pio_p_r(0, 1); }

static void z80pio_update_strobe(int which, int ch, int state)
{
    z80pio* pio = pios + which;
    if (ch) ch = 1;

    switch (pio->mode[ch])
    {
    case PIO_MODE0:
        state = state & 0x01;
        if ((pio->strobe[ch] ^ state) != 0)
        {
            if (state != 0)
            {
                z80pio_set_rdy(pio, ch, 0);
                if (pio->enable[ch] & PIO_INT_ENABLE)
                {
                    pio->int_state[ch] |= Z80_INT_REQ;
                }
            }
        }
        pio->strobe[ch] = state;
        z80pio_interrupt_check(pio);
        break;
    default:
        break;
    }
}

void z80pio_astb_w(int which, int state) { z80pio_update_strobe(which, 0, state); }
void z80pio_bstb_w(int which, int state) { z80pio_update_strobe(which, 1, state); }