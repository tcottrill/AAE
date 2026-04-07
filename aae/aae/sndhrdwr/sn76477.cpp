/*****************************************************************************
  SN76477 Complex Sound Generation chip emulation
  Ported from MAME 0.36 to AAE (Another Arcade Emulator)

 *****************************************************************************/

 //============================================================================
 // AAE is a poorly written M.A.M.E (TM) derivitave based on early MAME
 // code, 0.29 through .90 mixed with code of my own. This emulator was
 // created solely for my amusement and learning and is provided only
 // as an archival experience.
 //
 // All MAME code used and abused in this emulator remains the copyright
 // of the dedicated people who spend countless hours creating it. All
 // MAME code should be annotated as belonging to the MAME TEAM.
 //
 // SOME CODE BELOW IS FROM MAME and COPYRIGHT the MAME TEAM.
 //============================================================================

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

#include "sn76477.h"
#include "mixer.h"           /* stream_start, stream_update, stream_stop */
#include "timer.h"           /* timer_set, timer_pulse, timer_remove, TIME_IN_HZ */
#include "aae_mame_driver.h" /* Machine */

/* Set VERBOSE to 0 to silence all diagnostic output. */
#define VERBOSE 1

#if VERBOSE
#define LOG(n, x) do { if (VERBOSE >= (n)) { printf x; } } while(0)
#else
#define LOG(n, x) do {} while(0)
#endif

/* -------------------------------------------------------------------------
   Range check macros (active only when VERBOSE > 0).
   CHECK_CHIP_NUM        - guards against chip index out of range.
   CHECK_CHIP_NUM_AND_RANGE - also masks and warns on out-of-range data bits.
   ------------------------------------------------------------------------- */
#if VERBOSE
#define CHECK_CHIP_NUM                                                    \
    if (chip < 0 || chip >= intf->num) {                                 \
        LOG(0, ("SN76477 #%d: fatal, only %d chips defined!\n",          \
                chip, intf->num));                                        \
        return;                                                           \
    }

#define CHECK_CHIP_NUM_AND_RANGE(BITS, FUNC)                             \
    CHECK_CHIP_NUM                                                        \
    if (data != (data & (BITS))) {                                       \
        LOG(0, ("SN76477 #%d: warning %s called with data=$%02X\n",     \
                chip, #FUNC, data));                                      \
    }                                                                     \
    data &= (BITS);
#else
#define CHECK_CHIP_NUM
#define CHECK_CHIP_NUM_AND_RANGE(BITS, FUNC)
#endif

/* Volume range: 0x0000 (silent) to 0x7fff (full scale) */
#define VMIN 0x0000
#define VMAX 0x7fff

/* -------------------------------------------------------------------------
   Per-chip state structure
   ------------------------------------------------------------------------- */
struct SN76477 {
    /* AAE streaming channel id returned by stream_start() */
    int channel;

    /* Sample rate in Hz (== SYS_FREQ from mixer, set at init) */
    int samplerate;

    /* Per-frame PCM buffer fed to stream_update() each call to sh_update() */
    int16_t *frame_buf;   /* allocated in sh_start, size = samplerate/fps */
    int      frame_len;   /* number of int16 samples per frame */

    /* Attack / decay volume envelope state */
    int vol;        /* current volume level (VMIN..VMAX) */
    int vol_count;  /* fractional accumulator for rate stepping */
    int vol_rate;   /* steps per second (derived from attack/decay time) */
    int vol_step;   /* +1 for attack, -1 for decay */

    /* SLF (super-low-frequency oscillator) */
    double slf_count;  /* fractional phase accumulator */
    double slf_freq;   /* frequency in Hz = 0.64 / (r_slf * c_slf) */
    double slf_level;  /* triangular wave value (0.0 .. 5.0 V) */
    int    slf_dir;    /* triangle direction: 0 = falling, 1 = rising */
    int    slf_out;    /* rectangular output: 0 or 1 */

    /* VCO (voltage-controlled oscillator) */
    double vco_count;  /* fractional phase accumulator */
    double vco_freq;   /* base frequency in Hz = 0.64 / (r_vco * c_vco) */
    double vco_step;   /* current modulated frequency (Hz), changes per sample */
    int    vco_out;    /* rectangular output: 0 or 1 */

    /* Noise generator */
    int noise_count;   /* fractional phase accumulator */
    int noise_clock;   /* external clock input state (pin 3) */
    int noise_freq;    /* filter frequency in samples/second */
    int noise_poly;    /* 17-bit LFSR state */
    int noise_out;     /* filtered noise output: 0 or 1 */

    /* Envelope timers */
    int envelope_timer;   /* AAE timer id for VCO-paced envelope; -1 = inactive */
    int envelope_state;   /* 0 = decay phase, 1 = attack phase */

    /* One-shot timer */
    int oneshot_timer;    /* AAE timer id; -1 = inactive */

    /* Timing derived from RC values */
    double attack_time;   /* seconds from 0 to full volume */
    double decay_time;    /* seconds from full volume to 0 */
    double oneshot_time;  /* one-shot pulse width in seconds */

    /* Pin state */
    int    envelope;           /* pins 1, 28 (2-bit enum 0..3) */
    double noise_res;          /* pin  4 */
    double filter_res;         /* pin  5 */
    double filter_cap;         /* pin  6 */
    double decay_res;          /* pin  7 */
    double attack_decay_cap;   /* pin  8 */
    int    enable;             /* pin  9: 0=enabled, 1=inhibited */
    double attack_res;         /* pin 10 */
    double amplitude_res;      /* pin 11 */
    double feedback_res;       /* pin 12 */
    double vco_voltage;        /* pin 16 */
    double vco_cap;            /* pin 17 */
    double vco_res;            /* pin 18 */
    double pitch_voltage;      /* pin 19 */
    double slf_res;            /* pin 20 */
    double slf_cap;            /* pin 21 */
    int    vco_select;         /* pin 22: 0=external, 1=SLF */
    double oneshot_cap;        /* pin 23 */
    double oneshot_res;        /* pin 24 */
    int    mixer;              /* pins 25,26,27 (3-bit, 0..7) */

    /* Precomputed lookup: vol index -> signed 16-bit output amplitude */
    int16_t vol_lookup[VMAX + 1 - VMIN];
};

/* -------------------------------------------------------------------------
   Module-level state
   ------------------------------------------------------------------------- */
static const struct SN76477interface *intf = nullptr;
static struct SN76477 *sn76477[MAX_SN76477];

/* -------------------------------------------------------------------------
   Diagnostic strings (used only when VERBOSE is non-zero)
   ------------------------------------------------------------------------- */
#if VERBOSE
static const char *mixer_mode[8] = {
    "VCO",
    "SLF",
    "Noise",
    "VCO/Noise",
    "SLF/Noise",
    "SLF/VCO/Noise",
    "SLF/VCO",
    "Inhibit"
};

static const char *envelope_mode[4] = {
    "VCO",
    "One-Shot",
    "Mixer only",
    "VCO with alternating Polarity"
};
#endif

/* =========================================================================
   Internal helpers: envelope timer callbacks
   ========================================================================= */

/*
 * attack_decay - toggle between attack and decay phases.
 * Called by the envelope timers.
 */
static void attack_decay(int param)
{
    struct SN76477 *sn = sn76477[param];
    sn->envelope_state ^= 1;
    if (sn->envelope_state)
    {
        /* Start ATTACK: volume ramps up */
        sn->vol_rate = (sn->attack_time > 0.0)
                       ? (int)(VMAX / sn->attack_time)
                       : VMAX;
        sn->vol_step = +1;
        LOG(2, ("SN76477 #%d: ATTACK rate %d/sec\n", param,
                sn->vol_rate / (sn->samplerate ? sn->samplerate : 1)));
    }
    else
    {
        /* Start DECAY: volume ramps down from full */
        sn->vol      = VMAX;
        sn->vol_rate = (sn->decay_time > 0.0)
                       ? (int)(VMAX / sn->decay_time)
                       : VMAX;
        sn->vol_step = -1;
        LOG(2, ("SN76477 #%d: DECAY rate %d/sec\n", param,
                sn->vol_rate / (sn->samplerate ? sn->samplerate : 1)));
    }
}

/*
 * vco_envelope_cb - repeating timer for VCO-paced envelope (envelope mode 0 and 3).
 * In AAE, timer_set() is the repeating form.
 */
static void vco_envelope_cb(int param)
{
    attack_decay(param);
}

/*
 * oneshot_envelope_cb - one-shot timer fires to end the one-shot pulse.
 * In AAE, timer_pulse() is the one-shot form.
 */
static void oneshot_envelope_cb(int param)
{
    struct SN76477 *sn = sn76477[param];
    sn->oneshot_timer = -1;
    attack_decay(param);
}

/* =========================================================================
   Control input writes
   ========================================================================= */

/*
 * SN76477_mixer_w - set all three mixer select lines at once.
 * data bits 2:0 select one of eight mix modes:
 *   0=VCO, 1=SLF, 2=Noise, 3=VCO+Noise,
 *   4=SLF+Noise, 5=SLF+VCO+Noise, 6=SLF+VCO, 7=Inhibit
 */
void SN76477_mixer_w(int chip, int data)
{
    struct SN76477 *sn = sn76477[chip];
    CHECK_CHIP_NUM_AND_RANGE(7, SN76477_mixer_w);
    if (data == sn->mixer)
        return;
    sn->mixer = data;
    LOG(1, ("SN76477 #%d: MIXER mode %d [%s]\n",
            chip, sn->mixer, mixer_mode[sn->mixer]));
}

void SN76477_mixer_a_w(int chip, int data)
{
    struct SN76477 *sn = sn76477[chip];
    CHECK_CHIP_NUM_AND_RANGE(1, SN76477_mixer_a_w);
    data = data ? 1 : 0;
    if (data == (sn->mixer & 1))
        return;
    sn->mixer = (sn->mixer & ~1) | data;
    LOG(1, ("SN76477 #%d: MIXER mode %d [%s]\n",
            chip, sn->mixer, mixer_mode[sn->mixer]));
}

void SN76477_mixer_b_w(int chip, int data)
{
    struct SN76477 *sn = sn76477[chip];
    CHECK_CHIP_NUM_AND_RANGE(1, SN76477_mixer_b_w);
    data = data ? 2 : 0;
    if (data == (sn->mixer & 2))
        return;
    sn->mixer = (sn->mixer & ~2) | data;
    LOG(1, ("SN76477 #%d: MIXER mode %d [%s]\n",
            chip, sn->mixer, mixer_mode[sn->mixer]));
}

void SN76477_mixer_c_w(int chip, int data)
{
    struct SN76477 *sn = sn76477[chip];
    CHECK_CHIP_NUM_AND_RANGE(1, SN76477_mixer_c_w);
    data = data ? 4 : 0;
    if (data == (sn->mixer & 4))
        return;
    sn->mixer = (sn->mixer & ~4) | data;
    LOG(1, ("SN76477 #%d: MIXER mode %d [%s]\n",
            chip, sn->mixer, mixer_mode[sn->mixer]));
}

/* -------------------------------------------------------------------------
   Envelope select
   ------------------------------------------------------------------------- */
void SN76477_envelope_w(int chip, int data)
{
    struct SN76477 *sn = sn76477[chip];
    CHECK_CHIP_NUM_AND_RANGE(3, SN76477_envelope_w);
    if (data == sn->envelope)
        return;
    sn->envelope = data;
    LOG(1, ("SN76477 #%d: ENVELOPE mode %d [%s]\n",
            chip, sn->envelope, envelope_mode[sn->envelope]));
}

void SN76477_envelope_1_w(int chip, int data)
{
    struct SN76477 *sn = sn76477[chip];
    CHECK_CHIP_NUM_AND_RANGE(1, SN76477_envelope_1_w);
    if (data == (sn->envelope & 1))
        return;
    sn->envelope = (sn->envelope & ~1) | data;
    LOG(1, ("SN76477 #%d: ENVELOPE mode %d [%s]\n",
            chip, sn->envelope, envelope_mode[sn->envelope]));
}

void SN76477_envelope_2_w(int chip, int data)
{
    struct SN76477 *sn = sn76477[chip];
    CHECK_CHIP_NUM_AND_RANGE(1, SN76477_envelope_2_w);
    data <<= 1;   /* bit 0 of the argument maps to bit 1 of envelope */
    if (data == (sn->envelope & 2))
        return;
    sn->envelope = (sn->envelope & ~2) | data;
    LOG(1, ("SN76477 #%d: ENVELOPE mode %d [%s]\n",
            chip, sn->envelope, envelope_mode[sn->envelope]));
}

/* -------------------------------------------------------------------------
   VCO select
   ------------------------------------------------------------------------- */
void SN76477_vco_w(int chip, int data)
{
    struct SN76477 *sn = sn76477[chip];
    CHECK_CHIP_NUM_AND_RANGE(1, SN76477_vco_w);
    if (data == sn->vco_select)
        return;
    sn->vco_select = data;
    LOG(1, ("SN76477 #%d: VCO select %d [%s]\n", chip, sn->vco_select,
            sn->vco_select ? "Internal (SLF)" : "External (Pin 16)"));
}

/* -------------------------------------------------------------------------
   Enable line
   When enable goes low (0) the chip starts producing sound according to the
   current envelope and mixer settings.
   When enable goes high (1) the chip is inhibited and timers are cleared.
   ------------------------------------------------------------------------- */
void SN76477_enable_w(int chip, int data)
{
    struct SN76477 *sn = sn76477[chip];
    CHECK_CHIP_NUM_AND_RANGE(1, SN76477_enable_w);

    if (data == sn->enable)
        return;

    sn->enable        = data;
    sn->envelope_state = data;

    /* Cancel any running envelope timers */
    if (sn->envelope_timer >= 0) {
        timer_remove(sn->envelope_timer);
        sn->envelope_timer = -1;
    }
    if (sn->oneshot_timer >= 0) {
        timer_remove(sn->oneshot_timer);
        sn->oneshot_timer = -1;
    }

    if (sn->enable == 0)
    {
        /* Chip just enabled - begin the appropriate envelope sequence */
        switch (sn->envelope)
        {
        case 0: /* VCO-paced envelope */
            if (sn->vco_res > 0.0 && sn->vco_cap > 0.0)
                /* In AAE timer_set is repeating, same as MAME timer_pulse */
                sn->envelope_timer = timer_set(
                    TIME_IN_HZ(0.64 / (sn->vco_res * sn->vco_cap)),
                    chip, vco_envelope_cb);
            else
                oneshot_envelope_cb(chip);
            break;

        case 1: /* One-shot envelope */
            oneshot_envelope_cb(chip);
            if (sn->oneshot_time > 0.0)
                /* In AAE timer_pulse is one-shot, same as MAME timer_set */
                sn->oneshot_timer = timer_pulse(
                    sn->oneshot_time, chip, oneshot_envelope_cb);
            break;

        case 2: /* Mixer only: immediately at full volume */
            sn->vol = VMAX;
            break;

        default: /* VCO with alternating polarity */
            if (sn->vco_res > 0.0 && sn->vco_cap > 0.0)
                sn->envelope_timer = timer_set(
                    TIME_IN_HZ(0.64 / (sn->vco_res * sn->vco_cap) / 2.0),
                    chip, vco_envelope_cb);
            else
                oneshot_envelope_cb(chip);
            break;
        }
    }
    else
    {
        /* Chip just inhibited - for VCO envelope modes we still run the
           timer so the chip follows the envelope when re-enabled. */
        switch (sn->envelope)
        {
        case 0:
            if (sn->vco_res > 0.0 && sn->vco_cap > 0.0)
                sn->envelope_timer = timer_set(
                    TIME_IN_HZ(0.64 / (sn->vco_res * sn->vco_cap)),
                    chip, vco_envelope_cb);
            else
                oneshot_envelope_cb(chip);
            break;

        case 1:
            oneshot_envelope_cb(chip);
            break;

        case 2:
            sn->vol = VMIN;
            break;

        default:
            if (sn->vco_res > 0.0 && sn->vco_cap > 0.0)
                sn->envelope_timer = timer_set(
                    TIME_IN_HZ(0.64 / (sn->vco_res * sn->vco_cap) / 2.0),
                    chip, vco_envelope_cb);
            else
                oneshot_envelope_cb(chip);
            break;
        }
    }
    LOG(1, ("SN76477 #%d: ENABLE line %d [%s]\n", chip, sn->enable,
            sn->enable ? "Inhibited" : "Enabled"));
}

/* -------------------------------------------------------------------------
   Noise external clock (pin 3)
   ------------------------------------------------------------------------- */
void SN76477_noise_clock_w(int chip, int data)
{
    struct SN76477 *sn = sn76477[chip];
    CHECK_CHIP_NUM_AND_RANGE(1, SN76477_noise_clock_w);
    if (data == sn->noise_clock)
        return;
    sn->noise_clock = data;
    /* Shift the LFSR on the rising clock edge */
    if (sn->noise_clock)
        sn->noise_poly = ((sn->noise_poly << 7)
                         + (sn->noise_poly >> 10)
                         + 0x18000) & 0x1ffff;
}

/* =========================================================================
   RC parameter setters
   All of these recompute any derived frequency/time values when the RC
   product changes.  No stream flush is needed - sh_update renders each frame
   from current state.
   ========================================================================= */

void SN76477_set_noise_res(int chip, double res)
{
    struct SN76477 *sn = sn76477[chip];
    CHECK_CHIP_NUM;
    sn->noise_res = res;
}

void SN76477_set_filter_res(int chip, double res)
{
    struct SN76477 *sn = sn76477[chip];
    CHECK_CHIP_NUM;
    if (res == sn->filter_res)
        return;
    sn->filter_res = res;
    if (sn->filter_res > 0.0 && sn->filter_cap > 0.0)
    {
        sn->noise_freq = (int)(1.28 / (sn->filter_res * sn->filter_cap));
        LOG(1, ("SN76477 #%d: NOISE FILTER frequency %d Hz\n",
                chip, sn->noise_freq));
    }
    else
        sn->noise_freq = sn->samplerate;
}

void SN76477_set_filter_cap(int chip, double cap)
{
    struct SN76477 *sn = sn76477[chip];
    CHECK_CHIP_NUM;
    if (cap == sn->filter_cap)
        return;
    sn->filter_cap = cap;
    if (sn->filter_res > 0.0 && sn->filter_cap > 0.0)
    {
        sn->noise_freq = (int)(1.28 / (sn->filter_res * sn->filter_cap));
        LOG(1, ("SN76477 #%d: NOISE FILTER frequency %d Hz\n",
                chip, sn->noise_freq));
    }
    else
        sn->noise_freq = sn->samplerate;
}

void SN76477_set_decay_res(int chip, double res)
{
    struct SN76477 *sn = sn76477[chip];
    CHECK_CHIP_NUM;
    if (res == sn->decay_res)
        return;
    sn->decay_res  = res;
    sn->decay_time = sn->decay_res * sn->attack_decay_cap;
    LOG(1, ("SN76477 #%d: DECAY time %f s\n", chip, sn->decay_time));
}

void SN76477_set_attack_decay_cap(int chip, double cap)
{
    struct SN76477 *sn = sn76477[chip];
    CHECK_CHIP_NUM;
    if (cap == sn->attack_decay_cap)
        return;
    sn->attack_decay_cap = cap;
    sn->decay_time       = sn->decay_res  * sn->attack_decay_cap;
    sn->attack_time      = sn->attack_res * sn->attack_decay_cap;
    LOG(1, ("SN76477 #%d: ATTACK time %f s\n", chip, sn->attack_time));
    LOG(1, ("SN76477 #%d: DECAY  time %f s\n", chip, sn->decay_time));
}

void SN76477_set_attack_res(int chip, double res)
{
    struct SN76477 *sn = sn76477[chip];
    CHECK_CHIP_NUM;
    if (res == sn->attack_res)
        return;
    sn->attack_res  = res;
    sn->attack_time = sn->attack_res * sn->attack_decay_cap;
    LOG(1, ("SN76477 #%d: ATTACK time %f s\n", chip, sn->attack_time));
}

/*
 * SN76477_set_amplitude_res - rebuilds the vol_lookup table.
 * The lookup maps a linear 0..VMAX volume index to a signed 16-bit PCM
 * amplitude.  Formula from the MAME original:
 *   amplitude = 3.4 * Rfeedback / Ramplitude  (ratio of resistors at the amp)
 * Scaled to int16 range and then weighted by mixing_level (0..100 percent).
 */
void SN76477_set_amplitude_res(int chip, double res)
{
    struct SN76477 *sn = sn76477[chip];
    CHECK_CHIP_NUM;
    if (res == sn->amplitude_res)
        return;
    sn->amplitude_res = res;
    if (sn->amplitude_res > 0.0)
    {
        for (int i = 0; i <= VMAX; i++)
        {
            int vol = (int)((3.4 * sn->feedback_res / sn->amplitude_res)
                            * 32767.0 * i / (VMAX + 1));
            if (vol > 32767) vol = 32767;
            /* Scale by mixing_level percent */
            sn->vol_lookup[i] = (int16_t)(vol * intf->mixing_level[chip] / 100);
        }
        LOG(1, ("SN76477 #%d: vol range -%d..+%d\n",
                chip, sn->vol_lookup[VMAX], sn->vol_lookup[VMAX]));
    }
    else
    {
        memset(sn->vol_lookup, 0, sizeof(sn->vol_lookup));
    }
}

/*
 * SN76477_set_feedback_res - rebuilds vol_lookup because both Rfeedback and
 * Ramplitude appear in the gain formula.
 */
void SN76477_set_feedback_res(int chip, double res)
{
    struct SN76477 *sn = sn76477[chip];
    CHECK_CHIP_NUM;
    if (res == sn->feedback_res)
        return;
    sn->feedback_res = res;
    if (sn->amplitude_res > 0.0)
    {
        for (int i = 0; i <= VMAX; i++)
        {
            int vol = (int)((3.4 * sn->feedback_res / sn->amplitude_res)
                            * 32767.0 * i / (VMAX + 1));
            if (vol > 32767) vol = 32767;
            sn->vol_lookup[i] = (int16_t)(vol * intf->mixing_level[chip] / 100);
        }
        LOG(1, ("SN76477 #%d: vol range -%d..+%d\n",
                chip, sn->vol_lookup[VMAX], sn->vol_lookup[VMAX]));
    }
    else
    {
        memset(sn->vol_lookup, 0, sizeof(sn->vol_lookup));
    }
}

void SN76477_set_pitch_voltage(int chip, double voltage)
{
    struct SN76477 *sn = sn76477[chip];
    CHECK_CHIP_NUM;
    if (voltage == sn->pitch_voltage)
        return;
    sn->pitch_voltage = voltage;
    LOG(1, ("SN76477 #%d: VCO pitch voltage %f\n", chip, sn->pitch_voltage));
}

void SN76477_set_vco_res(int chip, double res)
{
    struct SN76477 *sn = sn76477[chip];
    CHECK_CHIP_NUM;
    if (res == sn->vco_res)
        return;
    sn->vco_res = res;
    if (sn->vco_res > 0.0 && sn->vco_cap > 0.0)
    {
        sn->vco_freq = 0.64 / (sn->vco_res * sn->vco_cap);
        LOG(1, ("SN76477 #%d: VCO frequency %f Hz\n", chip, sn->vco_freq));
    }
    else
        sn->vco_freq = 0.0;
}

void SN76477_set_vco_cap(int chip, double cap)
{
    struct SN76477 *sn = sn76477[chip];
    CHECK_CHIP_NUM;
    if (cap == sn->vco_cap)
        return;
    sn->vco_cap = cap;
    if (sn->vco_res > 0.0 && sn->vco_cap > 0.0)
    {
        sn->vco_freq = 0.64 / (sn->vco_res * sn->vco_cap);
        LOG(1, ("SN76477 #%d: VCO frequency %f Hz\n", chip, sn->vco_freq));
    }
    else
        sn->vco_freq = 0.0;
}

void SN76477_set_vco_voltage(int chip, double voltage)
{
    struct SN76477 *sn = sn76477[chip];
    CHECK_CHIP_NUM;
    if (voltage == sn->vco_voltage)
        return;
    sn->vco_voltage = voltage;
    LOG(1, ("SN76477 #%d: VCO ext. voltage %f -> %f Hz\n", chip,
            sn->vco_voltage,
            sn->vco_freq * 10.0 * (5.0 - sn->vco_voltage) / 5.0));
}

void SN76477_set_slf_res(int chip, double res)
{
    struct SN76477 *sn = sn76477[chip];
    CHECK_CHIP_NUM;
    if (res == sn->slf_res)
        return;
    sn->slf_res = res;
    if (sn->slf_res > 0.0 && sn->slf_cap > 0.0)
    {
        sn->slf_freq = 0.64 / (sn->slf_res * sn->slf_cap);
        LOG(1, ("SN76477 #%d: SLF frequency %f Hz\n", chip, sn->slf_freq));
    }
    else
        sn->slf_freq = 0.0;
}

void SN76477_set_slf_cap(int chip, double cap)
{
    struct SN76477 *sn = sn76477[chip];
    CHECK_CHIP_NUM;
    if (cap == sn->slf_cap)
        return;
    sn->slf_cap = cap;
    if (sn->slf_res > 0.0 && sn->slf_cap > 0.0)
    {
        sn->slf_freq = 0.64 / (sn->slf_res * sn->slf_cap);
        LOG(1, ("SN76477 #%d: SLF frequency %f Hz\n", chip, sn->slf_freq));
    }
    else
        sn->slf_freq = 0.0;
}

void SN76477_set_oneshot_res(int chip, double res)
{
    struct SN76477 *sn = sn76477[chip];
    CHECK_CHIP_NUM;
    if (res == sn->oneshot_res)
        return;
    sn->oneshot_res  = res;
    sn->oneshot_time = 0.8 * sn->oneshot_res * sn->oneshot_cap;
    LOG(1, ("SN76477 #%d: ONE-SHOT time %f s\n", chip, sn->oneshot_time));
}

void SN76477_set_oneshot_cap(int chip, double cap)
{
    struct SN76477 *sn = sn76477[chip];
    CHECK_CHIP_NUM;
    if (cap == sn->oneshot_cap)
        return;
    sn->oneshot_cap  = cap;
    sn->oneshot_time = 0.8 * sn->oneshot_res * sn->oneshot_cap;
    LOG(1, ("SN76477 #%d: ONE-SHOT time %f s\n", chip, sn->oneshot_time));
}

/* =========================================================================
   Per-sample oscillator/noise update macros
   These are identical to the MAME originals; they run once per output sample.
   ========================================================================= */

/*
 * UPDATE_SLF - advance the super-low-frequency oscillator by one sample.
 * The SLF has both a rectangular output (slf_out) used by the mixer
 * and a triangular analog level (slf_level) used to modulate the VCO.
 * frequency = 0.64 / (r_slf * c_slf)
 */
#define UPDATE_SLF                                               \
    sn->slf_count -= sn->slf_freq;                               \
    while (sn->slf_count <= 0.0)                                 \
    {                                                            \
        sn->slf_count += (double)sn->samplerate;                 \
        sn->slf_out   ^= 1;                                      \
    }

/*
 * UPDATE_VCO - advance the VCO by one sample.
 * When vco_select==1 the VCO frequency is modulated by the SLF triangle.
 * When vco_select==0 the VCO frequency is set by the external voltage pin.
 * frequency range is approximately 10:1 over the voltage range 0..5 V.
 */
#define UPDATE_VCO                                                          \
    if (sn->vco_select)                                                     \
    {                                                                       \
        /* VCO controlled by SLF triangular wave */                         \
        if (sn->slf_dir == 0)                                               \
        {                                                                   \
            sn->slf_level -= sn->slf_freq * 2.0 * 5.0 / sn->samplerate;    \
            if (sn->slf_level <= 0.0)                                       \
            {                                                               \
                sn->slf_level = 0.0;                                        \
                sn->slf_dir   = 1;                                          \
            }                                                               \
        }                                                                   \
        else                                                                \
        {                                                                   \
            sn->slf_level += sn->slf_freq * 2.0 * 5.0 / sn->samplerate;    \
            if (sn->slf_level >= 5.0)                                       \
            {                                                               \
                sn->slf_level = 5.0;                                        \
                sn->slf_dir   = 0;                                          \
            }                                                               \
        }                                                                   \
        sn->vco_step = sn->vco_freq * sn->slf_level;                       \
    }                                                                       \
    else                                                                    \
    {                                                                       \
        /* VCO controlled by external voltage */                            \
        sn->vco_step = sn->vco_freq * sn->vco_voltage;                     \
    }                                                                       \
    sn->vco_count -= sn->vco_step;                                          \
    while (sn->vco_count <= 0.0)                                            \
    {                                                                       \
        sn->vco_count += (double)sn->samplerate;                            \
        sn->vco_out   ^= 1;                                                 \
    }

/*
 * UPDATE_NOISE - advance the noise LFSR and its low-pass filter by one sample.
 * The 17-bit polynomial is: next = (poly << 7) + (poly >> 10) + 0x18000
 * The noise_freq value controls how often noise_out is sampled from the LFSR.
 */
#define UPDATE_NOISE                                              \
    if (sn->noise_res > 0.0)                                      \
        sn->noise_poly = ((sn->noise_poly << 7)                   \
                         + (sn->noise_poly >> 10)                 \
                         + 0x18000) & 0x1ffff;                    \
    sn->noise_count -= sn->noise_freq;                            \
    while (sn->noise_count <= 0)                                  \
    {                                                             \
        sn->noise_count += sn->samplerate;                        \
        sn->noise_out = sn->noise_poly & 1;                       \
    }

/*
 * UPDATE_VOLUME - advance the attack/decay volume envelope by one sample.
 * vol_rate is in steps per second; we accumulate into vol_count and step
 * whenever the count underflows.
 */
#define UPDATE_VOLUME                                                      \
    sn->vol_count -= sn->vol_rate;                                         \
    if (sn->vol_count <= 0)                                                \
    {                                                                      \
        int n = (-sn->vol_count / sn->samplerate) + 1;                    \
        sn->vol_count += n * sn->samplerate;                               \
        sn->vol       += n * sn->vol_step;                                 \
        if (sn->vol < VMIN) sn->vol = VMIN;                               \
        if (sn->vol > VMAX) sn->vol = VMAX;                               \
    }

/* =========================================================================
   Per-frame render functions, one per mixer mode.
   Each fills 'length' int16 samples into 'buffer'.
   Output is +vol_lookup[vol] when oscillator is high, -vol_lookup[vol] low.
   ========================================================================= */

/* Mixer mode 0: VCO only */
static void SN76477_update_0(int chip, int16_t *buf, int length)
{
    struct SN76477 *sn = sn76477[chip];
    while (length--)
    {
        UPDATE_VCO;
        UPDATE_VOLUME;
        *buf++ = sn->vco_out
                 ?  sn->vol_lookup[sn->vol - VMIN]
                 : -sn->vol_lookup[sn->vol - VMIN];
    }
}

/* Mixer mode 1: SLF only */
static void SN76477_update_1(int chip, int16_t *buf, int length)
{
    struct SN76477 *sn = sn76477[chip];
    while (length--)
    {
        UPDATE_SLF;
        UPDATE_VOLUME;
        *buf++ = sn->slf_out
                 ?  sn->vol_lookup[sn->vol - VMIN]
                 : -sn->vol_lookup[sn->vol - VMIN];
    }
}

/* Mixer mode 2: Noise only */
static void SN76477_update_2(int chip, int16_t *buf, int length)
{
    struct SN76477 *sn = sn76477[chip];
    while (length--)
    {
        UPDATE_NOISE;
        UPDATE_VOLUME;
        *buf++ = sn->noise_out
                 ?  sn->vol_lookup[sn->vol - VMIN]
                 : -sn->vol_lookup[sn->vol - VMIN];
    }
}

/* Mixer mode 3: VCO AND Noise (both must be high) */
static void SN76477_update_3(int chip, int16_t *buf, int length)
{
    struct SN76477 *sn = sn76477[chip];
    while (length--)
    {
        UPDATE_VCO;
        UPDATE_NOISE;
        UPDATE_VOLUME;
        *buf++ = (sn->vco_out & sn->noise_out)
                 ?  sn->vol_lookup[sn->vol - VMIN]
                 : -sn->vol_lookup[sn->vol - VMIN];
    }
}

/* Mixer mode 4: SLF AND Noise */
static void SN76477_update_4(int chip, int16_t *buf, int length)
{
    struct SN76477 *sn = sn76477[chip];
    while (length--)
    {
        UPDATE_SLF;
        UPDATE_NOISE;
        UPDATE_VOLUME;
        *buf++ = (sn->slf_out & sn->noise_out)
                 ?  sn->vol_lookup[sn->vol - VMIN]
                 : -sn->vol_lookup[sn->vol - VMIN];
    }
}

/* Mixer mode 5: SLF AND VCO AND Noise */
static void SN76477_update_5(int chip, int16_t *buf, int length)
{
    struct SN76477 *sn = sn76477[chip];
    while (length--)
    {
        UPDATE_SLF;
        UPDATE_VCO;
        UPDATE_NOISE;
        UPDATE_VOLUME;
        *buf++ = (sn->vco_out & sn->slf_out & sn->noise_out)
                 ?  sn->vol_lookup[sn->vol - VMIN]
                 : -sn->vol_lookup[sn->vol - VMIN];
    }
}

/* Mixer mode 6: SLF AND VCO */
static void SN76477_update_6(int chip, int16_t *buf, int length)
{
    struct SN76477 *sn = sn76477[chip];
    while (length--)
    {
        UPDATE_SLF;
        UPDATE_VCO;
        UPDATE_VOLUME;
        *buf++ = (sn->vco_out & sn->slf_out)
                 ?  sn->vol_lookup[sn->vol - VMIN]
                 : -sn->vol_lookup[sn->vol - VMIN];
    }
}

/* Mixer mode 7: Inhibit - output silence */
static void SN76477_update_7(int /*chip*/, int16_t *buf, int length)
{
    memset(buf, 0, length * sizeof(int16_t));
}

/* -------------------------------------------------------------------------
   SN76477_render_chip - render one chip into buf for 'length' samples.
   If the chip is inhibited (enable==1) output silence unconditionally.
   ------------------------------------------------------------------------- */
static void SN76477_render_chip(int chip, int16_t *buf, int length)
{
    struct SN76477 *sn = sn76477[chip];
    if (sn->enable)
    {
        SN76477_update_7(chip, buf, length);
        return;
    }
    switch (sn->mixer)
    {
    case 0: SN76477_update_0(chip, buf, length); break;
    case 1: SN76477_update_1(chip, buf, length); break;
    case 2: SN76477_update_2(chip, buf, length); break;
    case 3: SN76477_update_3(chip, buf, length); break;
    case 4: SN76477_update_4(chip, buf, length); break;
    case 5: SN76477_update_5(chip, buf, length); break;
    case 6: SN76477_update_6(chip, buf, length); break;
    default: SN76477_update_7(chip, buf, length); break;
    }
}

/* =========================================================================
   Lifecycle
   ========================================================================= */

/*
 * SN76477_sh_start - initialize all chips declared in the interface.
 * Call once from the driver's machine_init (or equivalent) after mixer_init().
 *
 * Unlike MAME's stream_init() which registers a callback, AAE streams are
 * push-based: sh_update() is called once per frame and pushes the rendered
 * buffer via stream_update().
 *
 * Returns 0 on success, 1 on any allocation failure.
 */
int SN76477_sh_start(const struct SN76477interface *intf_in)
{
    int i;
    intf = intf_in;

    for (i = 0; i < intf->num; i++)
    {
        sn76477[i] = (struct SN76477 *)malloc(sizeof(struct SN76477));
        if (!sn76477[i])
        {
            printf("SN76477 #%d: malloc failed\n", i);
            return 1;
        }
        memset(sn76477[i], 0, sizeof(struct SN76477));

        /* Timer ids of -1 indicate no active timer */
        sn76477[i]->envelope_timer = -1;
        sn76477[i]->oneshot_timer  = -1;

        /* frame_len = samples per frame, matching stream_start's allocation */
        int fps = Machine->gamedrv->fps;
        if (fps <= 0) fps = 60;
        sn76477[i]->samplerate = config.samplerate; 
        sn76477[i]->frame_len  = sn76477[i]->samplerate / fps;

        sn76477[i]->frame_buf = (int16_t *)malloc(
            sn76477[i]->frame_len * sizeof(int16_t));
        if (!sn76477[i]->frame_buf)
        {
            printf("SN76477 #%d: frame buffer malloc failed\n", i);
            free(sn76477[i]);
            sn76477[i] = nullptr;
            return 1;
        }
        memset(sn76477[i]->frame_buf, 0,
               sn76477[i]->frame_len * sizeof(int16_t));

        /* Register the streaming channel with the mixer.
           stream_start(chanid, stream_unused, bits, frame_rate) */
        sn76477[i]->channel = i; /* use chip index as chanid */
        stream_start(sn76477[i]->channel, 0, 16, fps, /*stereo=*/false);

        /* Apply interface defaults using the setter functions so that all
           derived values (frequencies, times, lookup table) are computed. */
        SN76477_set_noise_res(i,        intf->noise_res[i]);
        SN76477_set_filter_res(i,       intf->filter_res[i]);
        SN76477_set_filter_cap(i,       intf->filter_cap[i]);
        SN76477_set_decay_res(i,        intf->decay_res[i]);
        SN76477_set_attack_decay_cap(i, intf->attack_decay_cap[i]);
        SN76477_set_attack_res(i,       intf->attack_res[i]);
        SN76477_set_amplitude_res(i,    intf->amplitude_res[i]);
        SN76477_set_feedback_res(i,     intf->feedback_res[i]);
        SN76477_set_oneshot_res(i,      intf->oneshot_res[i]);
        SN76477_set_oneshot_cap(i,      intf->oneshot_cap[i]);
        SN76477_set_pitch_voltage(i,    intf->pitch_voltage[i]);
        SN76477_set_slf_res(i,          intf->slf_res[i]);
        SN76477_set_slf_cap(i,          intf->slf_cap[i]);
        SN76477_set_vco_res(i,          intf->vco_res[i]);
        SN76477_set_vco_cap(i,          intf->vco_cap[i]);
        SN76477_set_vco_voltage(i,      intf->vco_voltage[i]);

        /* Start with mixing inhibited and chip disabled, matching hardware POR */
        SN76477_mixer_w(i,   0x07); /* Inhibit */
        SN76477_envelope_w(i, 0x03);
        SN76477_enable_w(i,   0x01); /* Inhibited */
    }
    return 0;
}

/*
 * SN76477_sh_stop - free all chip state and stop the streaming channels.
 * Call from the driver's machine_stop (or equivalent).
 */
void SN76477_sh_stop(void)
{
    if (!intf)
        return;
    for (int i = 0; i < intf->num; i++)
    {
        if (!sn76477[i])
            continue;

        /* Cancel any live timers */
        if (sn76477[i]->envelope_timer >= 0)
        {
            timer_remove(sn76477[i]->envelope_timer);
            sn76477[i]->envelope_timer = -1;
        }
        if (sn76477[i]->oneshot_timer >= 0)
        {
            timer_remove(sn76477[i]->oneshot_timer);
            sn76477[i]->oneshot_timer = -1;
        }

        stream_stop(sn76477[i]->channel, 0);

        free(sn76477[i]->frame_buf);
        free(sn76477[i]);
        sn76477[i] = nullptr;
    }
    intf = nullptr;
}

/*
 * SN76477_sh_update - render one frame for every active chip and push the
 * buffer to the mixer via stream_update().
 *
 * Call once per emulated video frame, after timers have been processed
 * (so that envelope state reflects the current frame's timer callbacks).
 */
void SN76477_sh_update(void)
{
    if (!intf)
        return;
    for (int i = 0; i < intf->num; i++)
    {
        if (!sn76477[i])
            continue;
        SN76477_render_chip(i, sn76477[i]->frame_buf, sn76477[i]->frame_len);
        stream_update(sn76477[i]->channel, sn76477[i]->frame_buf);
    }
}
