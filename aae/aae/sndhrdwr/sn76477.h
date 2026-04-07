/*****************************************************************************
  SN76477 Complex Sound Generation chip emulation
  Ported from MAME 0.36 to AAE (Another Arcade Emulator)

  Pin assignments and corresponding interface fields:

                              SN76477_envelope_w()
                             /                    \
                    [ 1] ENV SEL 1           ENV SEL 2 [28]
                    [ 2] GND                  MIXER C [27] \
  SN76477_noise_clock_w() [ 3] NOISE EXT OSC   MIXER A [26]  > SN76477_mixer_w()
            noise_res    [ 4] RES NOISE OSC     MIXER B [25] /
           filter_res    [ 5] NOISE FILTER RES  O/S RES [24] oneshot_res
           filter_cap    [ 6] NOISE FILTER CAP  O/S CAP [23] oneshot_cap
            decay_res    [ 7] DECAY RES         VCO SEL [22] SN76477_vco_w()
   attack_decay_cap      [ 8] A/D CAP           SLF CAP [21] slf_cap
   SN76477_enable_w()    [ 9] ENABLE            SLF RES [20] slf_res
           attack_res    [10] ATTACK RES          PITCH [19] pitch_voltage
        amplitude_res    [11] AMP              VCO RES [18] vco_res
         feedback_res    [12] FEEDBACK         VCO CAP [17] vco_cap
                    [13] OUTPUT           VCO EXT CONT [16] vco_voltage
                    [14] Vcc              +5V REG OUT [15]

  All resistor values in Ohms.
  All capacitor values in Farads.
  Use RES_K, RES_M and CAP_U, CAP_N, CAP_P macros to convert
  magnitudes, e.g. 220k = RES_K(220), 47nF = CAP_N(47)

 *****************************************************************************/
#ifndef SN76477_SOUND_H
#define SN76477_SOUND_H

#define MAX_SN76477 4

/* Magnitude conversion helpers */
#define RES_K(res) ((double)(res)*1e3)
#define RES_M(res) ((double)(res)*1e6)
#define CAP_U(cap) ((double)(cap)*1e-6)
#define CAP_N(cap) ((double)(cap)*1e-9)
#define CAP_P(cap) ((double)(cap)*1e-12)

/* Interface structure passed to SN76477_sh_start() */
struct SN76477interface {
    int    num;                           /* number of chips (max MAX_SN76477) */
    int    mixing_level[MAX_SN76477];     /* volume 0..100 per chip */
    double noise_res[MAX_SN76477];        /* pin  4 */
    double filter_res[MAX_SN76477];       /* pin  5 */
    double filter_cap[MAX_SN76477];       /* pin  6 */
    double decay_res[MAX_SN76477];        /* pin  7 */
    double attack_decay_cap[MAX_SN76477]; /* pin  8 */
    double attack_res[MAX_SN76477];       /* pin 10 */
    double amplitude_res[MAX_SN76477];    /* pin 11 */
    double feedback_res[MAX_SN76477];     /* pin 12 */
    double vco_voltage[MAX_SN76477];      /* pin 16 */
    double vco_cap[MAX_SN76477];          /* pin 17 */
    double vco_res[MAX_SN76477];          /* pin 18 */
    double pitch_voltage[MAX_SN76477];    /* pin 19 */
    double slf_res[MAX_SN76477];          /* pin 20 */
    double slf_cap[MAX_SN76477];          /* pin 21 */
    double oneshot_cap[MAX_SN76477];      /* pin 23 */
    double oneshot_res[MAX_SN76477];      /* pin 24 */
};

/* Noise external clock write (useful only when noise_res == 0) */
void SN76477_noise_clock_w(int chip, int data);

/* Enable line: 0 = enabled, 1 = inhibited. Also resets the one-shot. */
void SN76477_enable_w(int chip, int data);

/* Mixer select: three input lines packed into bits 2:0 of data (0..7) */
void SN76477_mixer_w(int chip, int data);

/* Individual mixer line writes */
void SN76477_mixer_a_w(int chip, int data);
void SN76477_mixer_b_w(int chip, int data);
void SN76477_mixer_c_w(int chip, int data);

/* Envelope select: two input lines packed into bits 1:0 of data (0..3) */
void SN76477_envelope_w(int chip, int data);

/* Individual envelope line writes */
void SN76477_envelope_1_w(int chip, int data);
void SN76477_envelope_2_w(int chip, int data);

/* VCO select: 0 = external voltage (pin 16), 1 = SLF controlled */
void SN76477_vco_w(int chip, int data);

/* Dynamic parameter setters - can be called after init */
void SN76477_set_noise_res(int chip, double res);
void SN76477_set_filter_res(int chip, double res);
void SN76477_set_filter_cap(int chip, double cap);
void SN76477_set_decay_res(int chip, double res);
void SN76477_set_attack_decay_cap(int chip, double cap);
void SN76477_set_attack_res(int chip, double res);
void SN76477_set_amplitude_res(int chip, double res);
void SN76477_set_feedback_res(int chip, double res);
void SN76477_set_slf_res(int chip, double res);
void SN76477_set_slf_cap(int chip, double cap);
void SN76477_set_oneshot_res(int chip, double res);
void SN76477_set_oneshot_cap(int chip, double cap);
void SN76477_set_vco_res(int chip, double res);
void SN76477_set_vco_cap(int chip, double cap);
void SN76477_set_pitch_voltage(int chip, double voltage);
void SN76477_set_vco_voltage(int chip, double voltage);

/* Lifecycle: call start once at game init, update once per frame, stop on game exit */
int  SN76477_sh_start(const struct SN76477interface *intf_in);
void SN76477_sh_stop(void);
void SN76477_sh_update(void);

#endif /* SN76477_SOUND_H */
