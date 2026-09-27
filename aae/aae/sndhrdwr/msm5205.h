/*****************************************************************************
  MSM5205 ADPCM decoder emulation
  Ported from MAME 0.57 sound/msm5205.c to AAE (Another Arcade Emulator).

  Differences from the MAME original:
  - No MachineSound wrapper; MSM5205_sh_start() takes the interface directly.
  - MAME's stream_init/stream_update(chip,0) push-on-change model is replaced
    with the AAE per-frame push model used by dac.cpp: each signal change
    writes the OLD level into the frame buffer up to the CPU's current
    position (cpu_scale_by_cycles), and MSM5205_sh_update() - called once per
    video frame - fills the tail and pushes the buffer to the mixer.
  - The VCLK timer uses AAE's timer_pulse()/timer_remove().

  First user: the Irem M52 sound board (Moon Patrol).
 *****************************************************************************/

#ifndef MSM5205_H
#define MSM5205_H

#define MAX_MSM5205 2

/* an interface for the MSM5205 and similar chips */

/* prescaler selector defines (as on the hardware S1/S2 + 3B/4B pins) */
#define MSM5205_S96_3B 0     /* prescaler 1/96, 3-bit ADPCM */
#define MSM5205_S48_3B 1     /* prescaler 1/48, 3-bit ADPCM */
#define MSM5205_S64_3B 2     /* prescaler 1/64, 3-bit ADPCM */
#define MSM5205_SEX_3B 3     /* external VCLK,  3-bit ADPCM */
#define MSM5205_S96_4B 4     /* prescaler 1/96, 4-bit ADPCM */
#define MSM5205_S48_4B 5     /* prescaler 1/48, 4-bit ADPCM */
#define MSM5205_S64_4B 6     /* prescaler 1/64, 4-bit ADPCM */
#define MSM5205_SEX_4B 7     /* external VCLK,  4-bit ADPCM */

struct MSM5205interface
{
	int num;                                  /* number of chips              */
	int baseclock;                            /* master clock (e.g. 384000)   */
	void (*vclk_interrupt[MAX_MSM5205])(int); /* VCLK callback (may be null)  */
	int select[MAX_MSM5205];                  /* prescaler / bitwidth select  */
	int mixing_level[MAX_MSM5205];            /* mixer volume 0..255          */
};

int  MSM5205_sh_start(const struct MSM5205interface* intf);
void MSM5205_sh_stop(void);
void MSM5205_sh_update(void);   /* call once per video frame */
void MSM5205_sh_reset(void);

void MSM5205_reset_w(int num, int reset);      /* 1 = reset ON */
void MSM5205_data_w(int num, int data);        /* next ADPCM nibble */
void MSM5205_vclk_w(int num, int vclk);        /* external VCLK (SEX mode) */
void MSM5205_playmode_w(int num, int select);  /* prescaler/bitwidth select */

#endif // MSM5205_H
