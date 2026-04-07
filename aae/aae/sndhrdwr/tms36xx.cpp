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

#include "tms36xx.h"
#include "aae_mame_driver.h"
#include "mixer.h"
#include "sys_log.h"

#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <cmath>

// ---- Stream channel ----
static const int TMS36XX_CHANNEL = 5;

// ---- Internal synthesis constants ----
#define VMIN    0x0000
#define VMAX    0x7fff
#define FSCALE  1024

// ---- Internal chip state (MAME 0.57 structure) ----
static struct {
    const char* subtype_name;
    int samplerate;
    int basefreq;
    int octave;
    int speed;
    int tune_counter;
    int note_counter;
    int voices;
    int shift;
    int vol[12];
    int vol_counter[12];
    int decay[12];
    int counter[12];
    int frequency[12];
    int output;
    int enable;
    int tune_num;
    int tune_ofs;
    int tune_max;
} tms;

// ---- Audio Buffering State ----
static int      emulation_rate = 0;
static int      buffer_len = 0;
static int      sample_pos = 0;
static int16_t* output_buffer = nullptr;      // Managed by malloc/free
static int16_t* stream_buffer_ptr = nullptr;  // Managed by mixer.cpp (new[]/delete[])
static int      stream_buffer_len = 0;

// =============================================================================
// Note Frequency Tables (MAME 0.57)
// =============================================================================
#define C(n)    (int)((FSCALE<<(n-1))*1.18921)
#define Cx(n)   (int)((FSCALE<<(n-1))*1.25992)
#define D(n)    (int)((FSCALE<<(n-1))*1.33484)
#define Dx(n)   (int)((FSCALE<<(n-1))*1.41421)
#define E(n)    (int)((FSCALE<<(n-1))*1.49831)
#define F(n)    (int)((FSCALE<<(n-1))*1.58740)
#define Fx(n)   (int)((FSCALE<<(n-1))*1.68179)
#define G(n)    (int)((FSCALE<<(n-1))*1.78180)
#define Gx(n)   (int)((FSCALE<<(n-1))*1.88775)
#define A(n)    (int)((FSCALE<<n))
#define Ax(n)   (int)((FSCALE<<n)*1.05946)
#define B(n)    (int)((FSCALE<<n)*1.12246)

static int tune1[96 * 6] = { C(3),0,0,C(2),0,0, G(3),0,0,0,0,0, C(3),0,0,0,0,0, G(3),0,0,0,0,0, C(3),0,0,0,0,0, G(3),0,0,0,0,0, C(3),0,0,0,0,0, G(3),0,0,0,0,0, C(3),0,0,C(4),0,0, G(3),0,0,0,0,0, C(3),0,0,0,0,0, G(3),0,0,0,0,0, C(3),0,0,0,0,0, G(3),0,0,0,0,0, C(3),0,0,0,0,0, G(3),0,0,0,0,0, C(3),0,0,C(2),0,0, G(3),0,0,0,0,0, C(3),0,0,0,0,0, G(3),0,0,0,0,0, C(3),0,0,0,0,0, G(3),0,0,0,0,0, C(3),0,0,0,0,0, G(3),0,0,0,0,0, C(3),0,0,C(4),0,0, G(3),0,0,0,0,0, C(3),0,0,0,0,0, G(3),0,0,0,0,0, C(3),0,0,0,0,0, G(3),0,0,0,0,0, C(3),0,0,0,0,0, G(3),0,0,0,0,0 };
static int tune2[96 * 6] = { D(3),D(4),D(5),0,0,0, Cx(3),Cx(4),Cx(5),0,0,0, D(3),D(4),D(5),0,0,0, Cx(3),Cx(4),Cx(5),0,0,0, D(3),D(4),D(5),0,0,0, A(2),A(3),A(4),0,0,0, C(3),C(4),C(5),0,0,0, Ax(2),Ax(3),Ax(4),0,0,0, G(2),G(3),G(4),0,0,0, D(1),D(2),D(3),0,0,0, G(1),G(2),G(3),0,0,0, Ax(1),Ax(2),Ax(3),0,0,0, D(2),D(3),D(4),0,0,0, G(2),G(3),G(4),0,0,0, A(2),A(3),A(4),0,0,0, D(1),D(2),D(3),0,0,0, A(1),A(2),A(3),0,0,0, D(2),D(3),D(4),0,0,0, Fx(2),Fx(3),Fx(4),0,0,0, A(2),A(3),A(4),0,0,0, Ax(2),Ax(3),Ax(4),0,0,0, D(1),D(2),D(3),0,0,0, G(1),G(2),G(3),0,0,0, Ax(1),Ax(2),Ax(3),0,0,0, D(3),D(4),D(5),0,0,0, Cx(3),Cx(4),Cx(5),0,0,0, D(3),D(4),D(5),0,0,0, Cx(3),Cx(4),Cx(5),0,0,0, D(3),D(4),D(5),0,0,0, A(2),A(3),A(4),0,0,0, C(3),C(4),C(5),0,0,0, Ax(2),Ax(3),Ax(4),0,0,0, G(2),G(3),G(4),0,0,0, D(1),D(2),D(3),0,0,0, G(1),G(2),G(3),0,0,0, Ax(1),Ax(2),Ax(3),0,0,0, D(2),D(3),D(4),0,0,0, G(2),G(3),G(4),0,0,0, A(2),A(3),A(4),0,0,0, D(1),D(2),D(3),0,0,0, A(1),A(2),A(3),0,0,0, D(2),D(3),D(4),0,0,0, Ax(2),Ax(3),Ax(4),0,0,0, A(2),A(3),A(4),0,0,0, 0,0,0,G(2),G(3),G(4), D(1),D(2),D(3),0,0,0, G(1),G(2),G(3),0,0,0, 0,0,0,0,0,0 };
static int tune3[96 * 6] = { A(2),A(3),A(4),D(1),D(2),D(3), 0,0,0,0,0,0, A(2),A(3),A(4),0,0,0, 0,0,0,0,0,0, A(2),A(3),A(4),0,0,0, 0,0,0,0,0,0, A(2),A(3),A(4),A(1),A(2),A(3), 0,0,0,0,0,0, G(2),G(3),G(4),0,0,0, 0,0,0,0,0,0, F(2),F(3),F(4),0,0,0, 0,0,0,0,0,0, F(2),F(3),F(4),F(1),F(2),F(3), 0,0,0,0,0,0, E(2),E(3),E(4),F(1),F(2),F(3), 0,0,0,0,0,0, D(2),D(3),D(4),F(1),F(2),F(3), 0,0,0,0,0,0, D(2),D(3),D(4),A(1),A(2),A(3), 0,0,0,0,0,0, F(2),F(3),F(4),0,0,0, 0,0,0,0,0,0, A(2),A(3),A(4),0,0,0, 0,0,0,0,0,0, D(3),D(4),D(5),D(1),D(2),D(3), 0,0,0,0,0,0, 0,0,0,D(1),D(2),D(3), 0,0,0,F(1),F(2),F(3), 0,0,0,A(1),A(2),A(3), 0,0,0,D(2),D(2),D(2), D(3),D(4),D(5),D(1),D(2),D(3), 0,0,0,0,0,0, C(3),C(4),C(5),0,0,0, 0,0,0,0,0,0, Ax(2),Ax(3),Ax(4),0,0,0, 0,0,0,0,0,0, Ax(2),Ax(3),Ax(4),Ax(1),Ax(2),Ax(3), 0,0,0,0,0,0, A(2),A(3),A(4),0,0,0, 0,0,0,0,0,0, G(2),G(3),G(4),0,0,0, 0,0,0,0,0,0, G(2),G(3),G(4),G(1),G(2),G(3), 0,0,0,0,0,0, A(2),A(3),A(4),0,0,0, 0,0,0,0,0,0, Ax(2),Ax(3),Ax(4),0,0,0, 0,0,0,0,0,0, A(2),A(3),A(4),A(1),A(2),A(3), 0,0,0,0,0,0, Ax(2),Ax(3),Ax(4),0,0,0, 0,0,0,0,0,0, A(2),A(3),A(4),0,0,0, 0,0,0,0,0,0, Cx(3),Cx(4),Cx(5),A(1),A(2),A(3), 0,0,0,0,0,0, Ax(2),Ax(3),Ax(4),0,0,0, 0,0,0,0,0,0, A(2),A(3),A(4),0,0,0, 0,0,0,0,0,0, A(2),A(3),A(4),F(1),F(2),F(3), 0,0,0,0,0,0, G(2),G(3),G(4),0,0,0, 0,0,0,0,0,0, F(2),F(3),F(4),0,0,0, 0,0,0,0,0,0, F(2),F(3),F(4),D(1),D(2),D(3), 0,0,0,0,0,0, E(2),E(3),E(4),0,0,0, 0,0,0,0,0,0, D(2),D(3),D(4),0,0,0, 0,0,0,0,0,0, E(2),E(3),E(4),E(1),E(2),E(3), 0,0,0,0,0,0, E(2),E(3),E(4),0,0,0, 0,0,0,0,0,0, E(2),E(3),E(4),0,0,0, 0,0,0,0,0,0, E(2),E(3),E(4),Ax(1),Ax(2),Ax(3), 0,0,0,0,0,0, F(2),F(3),F(4),0,0,0, 0,0,0,0,0,0, E(2),E(3),E(4),F(1),F(2),F(3), 0,0,0,0,0,0, D(2),D(3),D(4),D(1),D(2),D(3), 0,0,0,0,0,0, F(2),F(3),F(4),A(1),A(2),A(3), 0,0,0,0,0,0, A(2),A(3),A(4),F(1),F(2),F(3), 0,0,0,0,0,0, D(3),D(4),D(5),D(1),D(2),D(3), 0,0,0,0,0,0, 0,0,0,0,0,0, 0,0,0,0,0,0, 0,0,0,0,0,0, 0,0,0,0,0,0 };
static int tune4[13 * 6] = { B(0),B(1),Dx(2),B(2),Dx(3),B(3), C(1),C(2),E(2),C(3),E(3),C(4), Cx(1),Cx(2),F(2),Cx(3),F(3),Cx(4), D(1),D(2),Fx(2),D(3),Fx(3),D(4), Dx(1),Dx(2),G(2),Dx(3),G(3),Dx(4), E(1),E(2),Gx(2),E(3),Gx(3),E(4), F(1),F(2),A(2),F(3),A(3),F(4), Fx(1),Fx(2),Ax(2),Fx(3),Ax(3),Fx(4), G(1),G(2),B(2),G(3),B(3),G(4), Gx(1),Gx(2),C(3),Gx(3),C(4),Gx(4), A(1),A(2),Cx(3),A(3),Cx(4),A(4), Ax(1),Ax(2),D(3),Ax(3),D(4),Ax(4), B(1),B(2),Dx(3),B(3),Dx(4),B(4) };

static int* tunes[] = { nullptr, tune1, tune2, tune3, tune4 };

// =============================================================================
// Synthesis Macros (MAME 0.57)
// =============================================================================
#define DECAY(voice)                                            \
    if( tms.vol[voice] > VMIN )                                 \
    {                                                           \
        tms.vol_counter[voice] -= tms.decay[voice];             \
        while( tms.vol_counter[voice] <= 0 )                    \
        {                                                       \
            tms.vol_counter[voice] += tms.samplerate;           \
            if( tms.vol[voice]-- <= VMIN )                      \
            {                                                   \
                tms.frequency[voice] = 0;                       \
                tms.vol[voice] = VMIN;                          \
                break;                                          \
            }                                                   \
        }                                                       \
    }

#define RESTART(voice)                                          \
    if( tunes[tms.tune_num][tms.tune_ofs*6+voice] )             \
    {                                                           \
        tms.frequency[tms.shift+voice] =                        \
            tunes[tms.tune_num][tms.tune_ofs*6+voice] *         \
            (tms.basefreq << tms.octave) / FSCALE;              \
        tms.vol[tms.shift+voice] = VMAX;                        \
    }

#define TONE(voice)                                             \
    if( (tms.enable & (1<<voice)) && tms.frequency[voice] )     \
    {                                                           \
        tms.counter[voice] -= tms.frequency[voice];             \
        while( tms.counter[voice] <= 0 )                        \
        {                                                       \
            tms.counter[voice] += tms.samplerate;               \
            tms.output ^= 1 << voice;                           \
        }                                                       \
        if (tms.output & (1 << voice))                          \
            sum += tms.vol[voice];                              \
    }

// -----------------------------------------------------------------------------
// Core Synthesis Loop
// -----------------------------------------------------------------------------
static void tms36xx_sound_update_internal(int16_t* buffer, int length)
{
    if (!tunes[tms.tune_num] || tms.voices == 0)
    {
        std::memset(buffer, 0, length * sizeof(int16_t));
        return;
    }

    while (length-- > 0)
    {
        int sum = 0;

        DECAY(0) DECAY(1) DECAY(2) DECAY(3) DECAY(4) DECAY(5)
            DECAY(6) DECAY(7) DECAY(8) DECAY(9) DECAY(10) DECAY(11)

            tms.tune_counter -= tms.speed;
        if (tms.tune_counter <= 0)
        {
            int n = (-tms.tune_counter / tms.samplerate) + 1;
            tms.tune_counter += n * tms.samplerate;

            if ((tms.note_counter -= n) <= 0)
            {
                tms.note_counter += VMAX;
                if (tms.tune_ofs < tms.tune_max)
                {
                    tms.shift ^= 6;
                    RESTART(0) RESTART(1) RESTART(2)
                        RESTART(3) RESTART(4) RESTART(5)
                        tms.tune_ofs++;
                }
            }
        }

        TONE(0) TONE(1) TONE(2) TONE(3) TONE(4) TONE(5)
            TONE(6) TONE(7) TONE(8) TONE(9) TONE(10) TONE(11)

            // FIX: Ensure voices is at least 1 to avoid crash/silence
            int divisor = (tms.voices > 0) ? tms.voices : 1;
        *buffer++ = (int16_t)(sum / divisor);
    }
}

static void tms36xx_doupdate(void)
{
    if (!output_buffer) return;

    // Use first CPU to determine current frame progress
    int newpos = cpu_scale_by_cycles(buffer_len, Machine->gamedrv->cpu[0].cpu_freq);
    if (newpos < 0) newpos = 0;
    if (newpos > buffer_len) newpos = buffer_len;

    int delta = newpos - sample_pos;
    if (delta > 0)
    {
        tms36xx_sound_update_internal(output_buffer + sample_pos, delta);
        sample_pos = newpos;
    }
}

// =============================================================================
// Public Interface
// =============================================================================

void mm6221aa_tune_w(int tune)
{
    tune &= 3;
    if (tune == tms.tune_num) return;

    tms36xx_doupdate();

    tms.tune_num = tune;
    tms.tune_ofs = 0;
    tms.tune_max = 96;
    LOG_INFO("TMS36XX Tune: %d", tune);
}

void tms36xx_note_w(int octave, int note)
{
    octave &= 3;
    note &= 15;
    if (note > 12) return;

    tms36xx_doupdate();

    // Reset counters (MAME 0.57 logic)
    tms.tune_counter = 0;
    tms.note_counter = 0;
    std::memset(tms.vol_counter, 0, sizeof(tms.vol_counter));
    std::memset(tms.counter, 0, sizeof(tms.counter));

    tms.octave = octave;
    tms.tune_num = 4;
    tms.tune_ofs = note;
    tms.tune_max = note + 1;
}

// -----------------------------------------------------------------------------
// tms3617_enable_w -- enable/disable harmonic voices (TMS3617)
// -----------------------------------------------------------------------------
void tms3617_enable_w(int enable)
{
    // Duplicate the 6 voice enable bits (matching MAME 0.57 logic)
    enable = (enable & 0x3f) | ((enable & 0x3f) << 6);

    // Safety: if the new enable mask results in 0 voices, we must handle it
    // to avoid divide-by-zero in the update loop.
    int bits = 0;
    for (int i = 0; i < 6; i++)
    {
        if (enable & (1 << i))
            bits += 2;  /* each voice has two instances */
    }

    if (enable == tms.enable && bits == tms.voices)
        return;

    tms36xx_doupdate();

    tms.enable = enable;
    tms.voices = bits;

    LOG_INFO("TMS36XX (%s) enable: 0x%03X voices: %d", tms.subtype_name, enable, bits);
}

// -----------------------------------------------------------------------------
// tms36xx_sh_start
// -----------------------------------------------------------------------------
int tms36xx_sh_start(struct TMS36XXinterface* intf)
{
    if (!intf)
        return 1;

    // Fully clear internal state
    std::memset(&tms, 0, sizeof(tms));

    if (intf->subtype == MM6221AA)
        tms.subtype_name = "MM6221AA";
    else if (intf->subtype == TMS3615)
        tms.subtype_name = "TMS3615";
    else
        tms.subtype_name = "TMS3617";

    int fps = Machine->gamedrv->fps;
    if (fps <= 0) fps = 60;

    buffer_len = config.samplerate / fps;
    emulation_rate = buffer_len * fps;

    tms.samplerate = emulation_rate;
    tms.basefreq = intf->basefreq;

    /* set up decay rates and initial enable mask */
    int enable = 0;
    for (int j = 0; j < 6; j++)
    {
        // FIX: The crash was here. Remove (int) cast from the divisor.
        // We divide VMAX (int) by decay (double), then cast the RESULT to int.
        if (intf->decay[j] > 0.0)
        {
            tms.decay[j] = tms.decay[j + 6] = (int)((double)VMAX / intf->decay[j]);
            enable |= 0x41 << j;
        }
    }

    // Same logic for speed: divide first, cast result.
    tms.speed = (intf->speed > 0.0) ? (int)((double)VMAX / intf->speed) : VMAX;

    // Initialize voices
    tms3617_enable_w(enable);

    // Primary buffer
    output_buffer = (int16_t*)std::malloc(buffer_len * sizeof(int16_t));
    if (!output_buffer)
    {
        LOG_ERROR("tms36xx_sh_start: failed to allocate output_buffer");
        return 1;
    }
    std::memset(output_buffer, 0, buffer_len * sizeof(int16_t));

    // Reset stream state
    sample_pos = 0;
    stream_buffer_ptr = nullptr;
    stream_buffer_len = 0;

    stream_start(TMS36XX_CHANNEL, 0, 16, fps);

    LOG_INFO("TMS36XX (%s) started: rate=%d base=%d speed=%d",
        tms.subtype_name, tms.samplerate, tms.basefreq, tms.speed);

    return 0;
}

void tms36xx_sh_stop(void)
{
    stream_stop(TMS36XX_CHANNEL, 0);

    if (output_buffer)
    {
        std::free(output_buffer);
        output_buffer = nullptr;
    }

    // IMPORTANT FIX: mixer.cpp uses new[] for this pointer. Use delete[] here!
    if (stream_buffer_ptr)
    {
        delete[] stream_buffer_ptr;
        stream_buffer_ptr = nullptr;
    }
    stream_buffer_len = 0;
}

void tms36xx_sh_update(void)
{
    if (!output_buffer) return;

    // 1. Finish the frame
    if (sample_pos < buffer_len)
    {
        tms36xx_sound_update_internal(output_buffer + sample_pos, buffer_len - sample_pos);
    }
    sample_pos = 0;

    // 2. Resample to system rate
    const float ratio = (float)config.samplerate / (float)emulation_rate;

    // This calls the version in mixer.cpp which handles new[] allocation automatically
    linear_interpolation_16(
        output_buffer,
        buffer_len,
        &stream_buffer_ptr,
        &stream_buffer_len,
        ratio
    );

    // 3. Push to mixer
    stream_update(TMS36XX_CHANNEL, stream_buffer_ptr);
}