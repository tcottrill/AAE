// ============================================================================
// pleiads_audio.cpp -- Pleiads custom analog sound
//
// Full implementation of all five sound generators from the MAME reference:
//   Tone 1  - Fixed 8kHz clock divided by latch_a[3:0]
//   Tone 2/3- Upper 556 dual oscillators, voltage-swept by PB4 capacitor
//   Tone 4  - Lower 556 oscillator modulated by polybit, gated by PA5/PC5
//   Noise   - 18-bit polynomial shift register with PA6 envelope
//
// Pleiads-specific RC constants from pleiads_sh_start() in the MAME source.
// Based off of and derived from the MAME implementation.
// ============================================================================

#include "pleiads_audio.h"
#include "tms36xx.h"
#include "mixer.h"
#include "cpu_control.h"
#include "sys_log.h"
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <algorithm>

static const int CUSTOM_SND_CHANNEL = 6;
#define VMIN    0
#define VMAX    32767

// ---------------------------------------------------------------------------
// Mix level tuning knobs (0.0 = silent, 1.0 = full, 0.5 = half)
// Adjust these to balance the four analog generators against each other.
//   tone1  - Fixed-frequency beep/chirp
//   tone23 - Swept dual oscillators (engine whine)
//   tone4  - Engine rumble / shot (polybit-modulated)
//   noise  - Polynomial noise + envelope
// ---------------------------------------------------------------------------
static constexpr float MIX_TONE1 = 0.50f;
static constexpr float MIX_TONE23 = 0.50f;
static constexpr float MIX_TONE4 = 0.80f;
static constexpr float MIX_NOISE = 0.50f;

// ---------------------------------------------------------------------------
// Pleiads RC timing constants (from MAME pleiads_sh_start)
// ---------------------------------------------------------------------------
static constexpr double PA5_CHARGE_TIME = 3.3;
static constexpr double PA5_DISCHARGE_TIME = 2.2;
static constexpr double PA6_CHARGE_TIME = 0.000726;
static constexpr double PA6_DISCHARGE_TIME = 0.022;
static constexpr double PB4_CHARGE_TIME = 0.1;
static constexpr double PB4_DISCHARGE_TIME = 0.1;
static constexpr double PC4_CHARGE_TIME = 0.066;
static constexpr double PC4_DISCHARGE_TIME = 0.022;
static constexpr double PC5_CHARGE_TIME = 0.0033;
static constexpr double PC5_DISCHARGE_TIME = 0.1;

static constexpr int PA5_RESISTOR = 33;   // kohm
static constexpr int PC5_RESISTOR = 47;   // kohm
static constexpr int TONE2_MAX_FREQ = 351;
static constexpr int TONE3_MAX_FREQ = 582;
static constexpr int TONE4_MAX_FREQ = 1315;
static constexpr int POLYBIT_RESISTOR = 47;   // kohm
static constexpr int OPAMP_RESISTOR = 20;   // kohm
static constexpr int NOISE_FREQ = 1412;

#define TONE1_CLOCK 8000

// PC4 minimum level: 7/50 of VMAX (from MAME #define PC4_MIN)
static constexpr int PC4_MIN = (int)(VMAX * 7 / 50);

static struct {
    int samplerate;
    int buffer_len;
    int sample_pos;

    int latch_a, latch_b, latch_c;
    uint32_t* poly18;

    // Tone 1 state
    int t1_cnt, t1_div, t1_out;

    // PB4 capacitor (controls tone 2/3 frequency sweep)
    int pb4_cnt, pb4_lev;

    // Tone 2/3 state
    int t2_cnt, t2_out, t3_cnt, t3_out;

    // PC4 capacitor (tone 4 frequency control)
    int pc4_cnt, pc4_lev;

    // PC5 capacitor (tone 4 amplitude component)
    int pc5_cnt, pc5_lev;

    // PA5 capacitor (tone 4 amplitude component)
    int pa5_cnt, pa5_lev;

    // Tone 4 state
    int t4_cnt, t4_out;

    // Noise state
    int n_cnt, n_polyoffs;
    int n_bit;  // current polynomial output bit (shared with tone4)

    // PA6 capacitor (noise envelope)
    int pa6_cnt, pa6_lev;

    int16_t* output_buffer;
    int16_t* stream_buffer_ptr;
    int stream_buffer_len;
} snd;

// ============================================================================
// TONE 1 -- Fixed 8kHz clock divided by latch_a[3:0]
// ============================================================================
static int pleiads_tone1(int sr)
{
    if ((snd.latch_a & 15) != 15)
    {
        snd.t1_cnt -= TONE1_CLOCK;
        while (snd.t1_cnt <= 0)
        {
            snd.t1_cnt += sr;
            if (++snd.t1_div == 16)
            {
                snd.t1_div = snd.latch_a & 15;
                snd.t1_out ^= 1;
            }
        }
    }
    return snd.t1_out ? VMAX : -VMAX;
}

// ============================================================================
// PB4 capacitor charge/discharge -- controls tone 2/3 sweep
// ============================================================================
static int update_pb4(int sr)
{
    if (snd.latch_b & 0x10)
    {
        // charge
        if (snd.pb4_lev < VMAX)
        {
            snd.pb4_cnt -= (int)((VMAX - snd.pb4_lev) / PB4_CHARGE_TIME);
            if (snd.pb4_cnt <= 0)
            {
                int n = (-snd.pb4_cnt / sr) + 1;
                snd.pb4_cnt += n * sr;
                if ((snd.pb4_lev += n) > VMAX)
                    snd.pb4_lev = VMAX;
            }
        }
    }
    else
    {
        // discharge
        if (snd.pb4_lev > VMIN)
        {
            snd.pb4_cnt -= (int)((snd.pb4_lev - VMIN) / PB4_DISCHARGE_TIME);
            if (snd.pb4_cnt <= 0)
            {
                int n = (-snd.pb4_cnt / sr) + 1;
                snd.pb4_cnt += n * sr;
                if ((snd.pb4_lev -= n) < VMIN)
                    snd.pb4_lev = VMIN;
            }
        }
    }
    return snd.pb4_lev;
}

// ============================================================================
// TONE 2/3 -- Upper 556 dual oscillators, voltage-modulated by PB4
// ============================================================================
static int pleiads_tone23(int sr)
{
    int level = VMAX - update_pb4(sr);
    int sum = 0;

    // bit 5 of latch B: low = tone23 disabled
    if ((snd.latch_b & 0x20) == 0)
        return sum;

    // modulate timers with voltage from PB4
    if (level < VMAX)
    {
        snd.t2_cnt -= TONE2_MAX_FREQ * level / 32768;
        if (snd.t2_cnt <= 0)
        {
            int n = (-snd.t2_cnt / sr) + 1;
            snd.t2_cnt += n * sr;
            snd.t2_out = (snd.t2_out + n) & 1;
        }

        // tone3: 1/3 base + 2/3 modulated (MAME formula)
        snd.t3_cnt -= TONE3_MAX_FREQ * 1 / 3 + TONE3_MAX_FREQ * 2 / 3 * level / 33768;
        if (snd.t3_cnt <= 0)
        {
            // NOTE: MAME reference uses counter2 for n calc here (apparent bug in
            // original code) but counter3 for the actual add. We replicate this.
            int n = (-snd.t2_cnt / sr) + 1;
            snd.t3_cnt += sr;
            snd.t3_out = (snd.t3_out + n) & 1;
        }
    }

    sum += snd.t2_out ? VMAX : -VMAX;
    sum += snd.t3_out ? VMAX : -VMAX;

    return sum / 2;
}

// ============================================================================
// Capacitor helpers for Tone 4
// ============================================================================

// PC4: tone4 frequency control capacitor (videoreg bit 4)
static int update_c_pc4(int sr)
{
    if (snd.latch_c & 0x10)
    {
        if (snd.pc4_lev < VMAX)
        {
            snd.pc4_cnt -= (int)((VMAX - snd.pc4_lev) / PC4_CHARGE_TIME);
            if (snd.pc4_cnt <= 0)
            {
                int n = (-snd.pc4_cnt / sr) + 1;
                snd.pc4_cnt += n * sr;
                if ((snd.pc4_lev += n) > VMAX)
                    snd.pc4_lev = VMAX;
            }
        }
    }
    else
    {
        if (snd.pc4_lev > PC4_MIN)
        {
            snd.pc4_cnt -= (int)((snd.pc4_lev - PC4_MIN) / PC4_DISCHARGE_TIME);
            if (snd.pc4_cnt <= 0)
            {
                int n = (-snd.pc4_cnt / sr) + 1;
                snd.pc4_cnt += n * sr;
                if ((snd.pc4_lev -= n) < PC4_MIN)
                    snd.pc4_lev = PC4_MIN;
            }
        }
    }
    return snd.pc4_lev;
}

// PC5: tone4 gated amplitude component (videoreg bit 5)
static int update_c_pc5(int sr)
{
    if (snd.latch_c & 0x20)
    {
        if (snd.pc5_lev < VMAX)
        {
            snd.pc5_cnt -= (int)((VMAX - snd.pc5_lev) / PC5_CHARGE_TIME);
            if (snd.pc5_cnt <= 0)
            {
                int n = (-snd.pc5_cnt / sr) + 1;
                snd.pc5_cnt += n * sr;
                if ((snd.pc5_lev += n) > VMAX)
                    snd.pc5_lev = VMAX;
            }
        }
    }
    else
    {
        if (snd.pc5_lev > VMIN)
        {
            snd.pc5_cnt -= (int)((snd.pc5_lev - VMIN) / PC5_DISCHARGE_TIME);
            if (snd.pc5_cnt <= 0)
            {
                int n = (-snd.pc5_cnt / sr) + 1;
                snd.pc5_cnt += sr;
                if ((snd.pc5_lev -= n) < VMIN)
                    snd.pc5_lev = VMIN;
            }
        }
    }
    return snd.pc5_lev;
}

// PA5: tone4 gated amplitude component (latch A bit 5)
static int update_c_pa5(int sr)
{
    if (snd.latch_a & 0x20)
    {
        if (snd.pa5_lev < VMAX)
        {
            snd.pa5_cnt -= (int)((VMAX - snd.pa5_lev) / PA5_CHARGE_TIME);
            if (snd.pa5_cnt <= 0)
            {
                int n = (-snd.pa5_cnt / sr) + 1;
                snd.pa5_cnt += n * sr;
                if ((snd.pa5_lev += n) > VMAX)
                    snd.pa5_lev = VMAX;
            }
        }
    }
    else
    {
        if (snd.pa5_lev > VMIN)
        {
            snd.pa5_cnt -= (int)((snd.pa5_lev - VMIN) / PA5_DISCHARGE_TIME);
            if (snd.pa5_cnt <= 0)
            {
                int n = (-snd.pa5_cnt / sr) + 1;
                snd.pa5_cnt += sr;
                if ((snd.pa5_lev -= n) < VMIN)
                    snd.pa5_lev = VMIN;
            }
        }
    }
    return snd.pa5_lev;
}

// ============================================================================
// TONE 4 -- Lower 556 oscillator, polybit-modulated, PA5/PC5 gated
// This is the "shot" sound.
// ============================================================================
static int pleiads_tone4(int sr)
{
    int level = update_c_pc4(sr);
    int vpc5 = update_c_pc5(sr);
    int vpa5 = update_c_pa5(sr);

    // Voltage divider between polybit states:
    //   polybit=1: level biased toward VMAX
    //   polybit=0: level biased toward 0V
    if (snd.n_bit)
        level = level + (VMAX - level) * OPAMP_RESISTOR / (OPAMP_RESISTOR + POLYBIT_RESISTOR);
    else
        level = level * POLYBIT_RESISTOR / (OPAMP_RESISTOR + POLYBIT_RESISTOR);

    snd.t4_cnt -= TONE4_MAX_FREQ * level / 32768;
    if (snd.t4_cnt <= 0)
    {
        int n = (-snd.t4_cnt / sr) + 1;
        snd.t4_cnt += n * sr;
        snd.t4_out = (snd.t4_out + n) & 1;
    }

    // Mix the two gated signals through resistor divider
    int sum = vpc5 * PA5_RESISTOR / (PA5_RESISTOR + PC5_RESISTOR) +
        vpa5 * PC5_RESISTOR / (PA5_RESISTOR + PC5_RESISTOR);

    return snd.t4_out ? sum : -sum;
}

// ============================================================================
// PA6 capacitor -- Noise envelope (latch A bit 6)
// ============================================================================
static int update_c_pa6(int sr)
{
    if (snd.latch_a & 0x40)
    {
        // charge
        if (snd.pa6_lev < VMAX)
        {
            snd.pa6_cnt -= (int)((VMAX - snd.pa6_lev) / PA6_CHARGE_TIME);
            if (snd.pa6_cnt <= 0)
            {
                int n = (-snd.pa6_cnt / sr) + 1;
                snd.pa6_cnt += n * sr;
                if ((snd.pa6_lev += n) > VMAX)
                    snd.pa6_lev = VMAX;
            }
        }
    }
    else
    {
        // discharge -- ONLY when polybit is active (MAME-correct)
        if (snd.n_bit && snd.pa6_lev > VMIN)
        {
            snd.pa6_cnt -= (int)((snd.pa6_lev - VMIN) / 0.1);
            if (snd.pa6_cnt <= 0)
            {
                int n = (-snd.pa6_cnt / sr) + 1;
                snd.pa6_cnt += n * sr;
                if ((snd.pa6_lev -= n) < VMIN)
                    snd.pa6_lev = VMIN;
            }
        }
    }
    return snd.pa6_lev;
}

// ============================================================================
// NOISE -- 18-bit polynomial shift register, PA6 envelope, bit 7 gate
// ============================================================================
static int pleiads_noise(int sr)
{
    int c_pa6_level = update_c_pa6(sr);
    int sum = 0;

    // Bit 4 of latch A modulates noise clock rate
    if (snd.latch_a & 0x10)
        snd.n_cnt -= NOISE_FREQ * 2 / 3;
    else
        snd.n_cnt -= NOISE_FREQ * 1 / 3;

    if (snd.n_cnt <= 0)
    {
        int n = (-snd.n_cnt / sr) + 1;
        snd.n_cnt += n * sr;
        snd.n_polyoffs = (snd.n_polyoffs + n) & 0x3ffff;
        snd.n_bit = (snd.poly18[snd.n_polyoffs >> 5] >> (snd.n_polyoffs & 31)) & 1;
    }

    // Polynomial output gates PA6 envelope + direct bit 7
    if (snd.n_bit)
    {
        sum += c_pa6_level;
        if (snd.latch_a & 0x80)
            sum += VMAX;
    }
    else
    {
        sum -= c_pa6_level;
        if (snd.latch_a & 0x80)
            sum -= VMAX;
    }

    return sum / 2;
}

// ============================================================================
// MAIN MIX -- matches MAME: tone1/2 + tone23/2 + tone4 + noise
// ============================================================================
static void audio_render(int16_t* buffer, int length)
{
    int sr = snd.samplerate;
    while (length-- > 0)
    {
        int sum = (int)(pleiads_tone1(sr) * MIX_TONE1)
            + (int)(pleiads_tone23(sr) * MIX_TONE23)
            + (int)(pleiads_tone4(sr) * MIX_TONE4)
            + (int)(pleiads_noise(sr) * MIX_NOISE);
        *buffer++ = (int16_t)std::clamp(sum, -32768, 32767);
    }
}

// ============================================================================
// SYSTEM CORE
// ============================================================================

static void audio_doupdate(void)
{
    if (!snd.output_buffer || snd.buffer_len <= 0) return;
    int newpos = cpu_scale_by_cycles(snd.buffer_len, Machine->gamedrv->cpu[0].cpu_freq);
    newpos = std::clamp(newpos, 0, snd.buffer_len);
    int delta = newpos - snd.sample_pos;
    if (delta > 0)
    {
        audio_render(snd.output_buffer + snd.sample_pos, delta);
        snd.sample_pos = newpos;
    }
}

int pleiads_audio_sh_start(void)
{
    std::memset(&snd, 0, sizeof(snd));
    int fps = Machine->gamedrv->fps;
    if (fps <= 0) fps = 60;
    snd.buffer_len = config.samplerate / fps;
    snd.samplerate = snd.buffer_len * fps;

    // Initialize PC4 to its minimum (not zero)
    snd.pc4_lev = PC4_MIN;

    // Build 18-bit polynomial table
    snd.poly18 = (uint32_t*)std::malloc((1ul << 13) * sizeof(uint32_t));
    uint32_t shiftreg = 0;
    for (int i = 0; i < (1 << 13); i++)
    {
        uint32_t bits = 0;
        for (int j = 0; j < 32; j++)
        {
            bits = (bits >> 1) | (shiftreg << 31);
            if (((shiftreg >> 16) & 1) == ((shiftreg >> 17) & 1))
                shiftreg = (shiftreg << 1) | 1;
            else
                shiftreg <<= 1;
        }
        snd.poly18[i] = bits;
    }

    snd.output_buffer = (int16_t*)std::malloc(snd.buffer_len * sizeof(int16_t));
    std::memset(snd.output_buffer, 0, snd.buffer_len * sizeof(int16_t));
    stream_start(CUSTOM_SND_CHANNEL, 0, 16, fps);
    return 0;
}

void pleiads_audio_sh_stop(void)
{
    stream_stop(CUSTOM_SND_CHANNEL, 0);
    if (snd.poly18) std::free(snd.poly18);
    if (snd.output_buffer) std::free(snd.output_buffer);
    if (snd.stream_buffer_ptr) delete[] snd.stream_buffer_ptr;
    std::memset(&snd, 0, sizeof(snd));
}

void pleiads_audio_sh_update(void)
{
    if (!snd.output_buffer) return;
    if (snd.sample_pos < snd.buffer_len)
        audio_render(snd.output_buffer + snd.sample_pos, snd.buffer_len - snd.sample_pos);
    snd.sample_pos = 0;
    float ratio = (float)config.samplerate / (float)snd.samplerate;
    linear_interpolation_16(snd.output_buffer, snd.buffer_len,
        &snd.stream_buffer_ptr, &snd.stream_buffer_len, ratio);
    stream_update(CUSTOM_SND_CHANNEL, snd.stream_buffer_ptr);
}

// ============================================================================
// WRITE HANDLERS
// ============================================================================

WRITE_HANDLER_NS(pleiads_sound_control_a_w)
{
    LOG_INFO("SOUND CONTROL A WRITE address %x data %x", address, data);
    if (data == snd.latch_a) return;
    audio_doupdate();
    snd.latch_a = data;
}

WRITE_HANDLER_NS(pleiads_sound_control_b_w)
{
    if (data == snd.latch_b) return;
    audio_doupdate();
    snd.latch_b = data;

    // TMS3615 note: pitch selects clock, note selects tone
    int note = data & 15;
    int pitch = (data >> 6) & 3;
    if (pitch == 3) pitch = 2;  // 2 and 3 are the same
    tms36xx_note_w(pitch, note);
}

void pleiads_audio_control_c_w(int data)
{
    if (data == snd.latch_c) return;
    audio_doupdate();
    snd.latch_c = data;
}
