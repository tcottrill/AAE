#ifndef TMS36XX_H
#define TMS36XX_H

/* subtypes */
#define MM6221AA    21      /* Phoenix (fixed melodies) */
#define TMS3615     15      /* Naughty Boy, Pleiads (13 notes, one output) */
#define TMS3617     17      /* Monster Bash (13 notes, six outputs) */

struct TMS36XXinterface
{
    int subtype;            /* MM6221AA, TMS3615, or TMS3617 */
    int basefreq;           /* base frequency of the chip */
    double decay[6];        /* decay times for the six harmonic notes */
    double speed;           /* tune speed (time between beats) */
};

/* Core Functions */
int  tms36xx_sh_start(struct TMS36XXinterface* intf);
void tms36xx_sh_stop(void);
void tms36xx_sh_update(void);

/* MM6221AA interface functions (Phoenix) */
void mm6221aa_tune_w(int tune);

/* TMS3615/17 interface functions (Pleiads) */
void tms36xx_note_w(int octave, int note);

/* TMS3617 interface functions */
void tms3617_enable_w(int enable);

#endif