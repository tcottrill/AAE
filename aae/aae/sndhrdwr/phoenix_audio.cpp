// ============================================================================
// phoenix_audio.cpp -- Phoenix custom analog sound
// Based off of and derived from the MAME implementation.
//
// Split from phoenix_pleiads_audio.cpp for independent troubleshooting.
// Contains only the Phoenix tone and noise generators.
// ============================================================================

#include "phoenix_audio.h"
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

static struct {
    int samplerate;
    int buffer_len;
    int sample_pos;

    int latch_a, latch_b;
    uint32_t* poly18;

    // --- PHOENIX States ---
    int p_vco1_out, p_vco1_cnt, p_vco1_lev;
    int p_vco2_out, p_vco2_cnt, p_vco2_lev;
    int p_vco_cnt, p_vco_lev, p_vco_rate, p_vco_charge;
    int p_t1_cnt, p_t1_div, p_t1_out;
    int p_c24_cnt, p_c24_lev, p_c25_cnt, p_c25_lev;
    int p_n_cnt, p_n_poly;
    int p_n_lp_cnt, p_n_lp_bit;

    int tone1_level;

    int16_t* output_buffer;
    int16_t* stream_buffer_ptr;
    int stream_buffer_len;
} snd;

// =============================================================================
// PHOENIX ANALOG CORE (Exact PCB Restoration)
// =============================================================================

static int phoenix_tone1_vco1(int samplerate) {
#define R40 47000
#define R41 100000
    static double C18[4] = { 0.01e-6, 0.48e-6, 1.01e-6, 1.48e-6 };
    double r0 = VMAX * 2.0 / 3.0 / (0.693 * (R40 + R41) * C18[snd.latch_a >> 4 & 3]);
    double r1 = VMAX * 2.0 / 3.0 / (0.693 * R41 * C18[snd.latch_a >> 4 & 3]);

    if (snd.p_vco1_out) {
        if (snd.p_vco1_lev > VMAX / 3) {
            snd.p_vco1_cnt -= (int)r1;
            while (snd.p_vco1_cnt <= 0) { snd.p_vco1_cnt += samplerate; if (--snd.p_vco1_lev <= VMAX / 3) { snd.p_vco1_out = 0; break; } }
        }
    }
    else {
        if (snd.p_vco1_lev < VMAX * 2 / 3) {
            snd.p_vco1_cnt -= (int)r0;
            while (snd.p_vco1_cnt <= 0) { snd.p_vco1_cnt += samplerate; if (++snd.p_vco1_lev >= VMAX * 2 / 3) { snd.p_vco1_out = 1; break; } }
        }
    }
    return snd.p_vco1_out;
}

static int phoenix_tone1_vco2(int samplerate) {
    if (snd.p_vco2_out) {
        if (snd.p_vco2_lev > VMIN) {
            snd.p_vco2_cnt -= (int)(VMAX * 2.0 / 3.0 / (0.693 * 570000.0 * 10.0e-6));
            while (snd.p_vco2_cnt <= 0) { snd.p_vco2_cnt += samplerate; if (--snd.p_vco2_lev <= VMAX / 3) { snd.p_vco2_out = 0; break; } }
        }
    }
    else {
        if (snd.p_vco2_lev < VMAX) {
            snd.p_vco2_cnt -= (int)(VMAX * 2.0 / 3.0 / (0.693 * 1140000.0 * 10.0e-6));
            while (snd.p_vco2_cnt <= 0) { snd.p_vco2_cnt += samplerate; if (++snd.p_vco2_lev >= VMAX * 2 / 3) { snd.p_vco2_out = 1; break; } }
        }
    }
    return snd.p_vco2_out;
}

static int phoenix_tone1(int samplerate) {
    int v1 = phoenix_tone1_vco1(samplerate);
    int v2 = phoenix_tone1_vco2(samplerate);
    int voltage;

    if (snd.p_vco_lev != snd.p_vco_charge) {
        snd.p_vco_cnt -= snd.p_vco_rate;
        while (snd.p_vco_cnt <= 0) {
            snd.p_vco_cnt += samplerate;
            if (snd.p_vco_lev < snd.p_vco_charge) { if (++snd.p_vco_lev == snd.p_vco_charge) break; }
            else { if (--snd.p_vco_lev == snd.p_vco_charge) break; }
        }
    }

    if (v2) {
        if (v1) {
            snd.p_vco_charge = VMAX;
            snd.p_vco_rate = (int)((double)(VMAX - snd.p_vco_lev) / (27777.0 * 100.0e-6));
            voltage = snd.p_vco_lev + (int)((double)(VMAX - snd.p_vco_lev) * 51000.0 / 61000.0);
        }
        else {
            snd.p_vco_charge = VMAX * 27 / 50;
            if (snd.p_vco_charge >= snd.p_vco_lev) snd.p_vco_rate = (int)((double)(snd.p_vco_charge - snd.p_vco_lev) / (51000.0 * 100.0e-6));
            else snd.p_vco_rate = (int)((double)(snd.p_vco_lev - snd.p_vco_charge) / (61000.0 * 100.0e-6));
            voltage = (int)((double)snd.p_vco_lev * 10000.0 / 61000.0);
        }
    }
    else {
        if (v1) {
            snd.p_vco_charge = VMAX * 23 / 50;
            if (snd.p_vco_charge >= snd.p_vco_lev) snd.p_vco_rate = (int)((double)(snd.p_vco_charge - snd.p_vco_lev) / (61000.0 * 100.0e-6));
            else snd.p_vco_rate = (int)((double)(snd.p_vco_lev - snd.p_vco_charge) / (51000.0 * 100.0e-6));
            voltage = snd.p_vco_lev + (int)((double)(VMAX - snd.p_vco_lev) * 51000.0 / 61000.0);
        }
        else {
            snd.p_vco_charge = VMIN;
            snd.p_vco_rate = (int)((double)(snd.p_vco_lev - VMIN) / (27777.0 * 100.0e-6));
            voltage = (int)((double)snd.p_vco_lev * 10000.0 / 61000.0);
        }
    }

    int freq = 24000 * 1 / 3 + 24000 * 2 / 3 * voltage / 32768;
    if ((snd.latch_a & 15) != 15 && freq > 0) {
        snd.p_t1_cnt -= freq;
        while (snd.p_t1_cnt <= 0) {
            snd.p_t1_cnt += samplerate;
            if (++snd.p_t1_div == 16) { snd.p_t1_div = snd.latch_a & 15; snd.p_t1_out ^= 1; }
        }
    }
    return snd.p_t1_out ? snd.tone1_level : -snd.tone1_level;
}

static int phoenix_noise(int samplerate) {
    if (snd.latch_a & 0x40) { if (snd.p_c24_lev > VMIN) { snd.p_c24_cnt -= (int)((double)(snd.p_c24_lev - VMIN) / (20000.0 * 6.8e-6)); while (snd.p_c24_cnt <= 0) { snd.p_c24_cnt += samplerate; if (--snd.p_c24_lev <= VMIN) break; } } }
    else { if (snd.p_c24_lev < VMAX) { snd.p_c24_cnt -= (int)((double)(VMAX - snd.p_c24_lev) / ((330.0 + 1000.0) * 6.8e-6)); while (snd.p_c24_cnt <= 0) { snd.p_c24_cnt += samplerate; if (++snd.p_c24_lev >= VMAX) break; } } }

    if (snd.latch_a & 0x80) { if (snd.p_c25_lev < VMAX) { snd.p_c25_cnt -= (int)((double)(VMAX - snd.p_c25_lev) / ((1000.0 + 330.0) * 6.8e-6)); while (snd.p_c25_cnt <= 0) { snd.p_c25_cnt += samplerate; if (++snd.p_c25_lev >= VMAX) break; } } }
    else { if (snd.p_c25_lev > VMIN) { snd.p_c25_cnt -= (int)((double)(snd.p_c25_lev - VMIN) / (47000.0 * 6.8e-6)); while (snd.p_c25_cnt <= 0) { snd.p_c25_cnt += samplerate; if (--snd.p_c25_lev <= VMIN) break; } } }

    int vc24 = VMAX - snd.p_c24_lev;
    int vc25 = snd.p_c25_lev;
    int level = (vc24 < vc25) ? (vc24 + (vc25 - vc24) / 2) : (vc25 + (vc24 - vc25) / 2);
    int freq = 588 + 6325 * level / 32768;
    if (freq > 0) {
        snd.p_n_cnt -= freq;
        while (snd.p_n_cnt <= 0) { snd.p_n_cnt += samplerate; snd.p_n_poly = (snd.p_n_poly + 1) & 0x3ffff; }
    }
    int polybit = (snd.poly18[snd.p_n_poly >> 5] >> (snd.p_n_poly & 31)) & 1;
    int sum = (!polybit) ? vc24 : 0;

    snd.p_n_lp_cnt -= 400;
    if (snd.p_n_lp_cnt <= 0) { snd.p_n_lp_cnt += samplerate; snd.p_n_lp_bit = polybit; }
    if (!snd.p_n_lp_bit) sum += vc25;

    return sum;
}

// =============================================================================
// SYSTEM CORE
// =============================================================================

static void audio_render(int16_t* buffer, int length) {
    while (length-- > 0) {
        int sum = (phoenix_tone1(snd.samplerate) + phoenix_noise(snd.samplerate)) / 2;
        *buffer++ = (int16_t)std::clamp(sum, -32768, 32767);
    }
}

static void audio_doupdate(void) {
    if (!snd.output_buffer || snd.buffer_len <= 0) return;
    int newpos = cpu_scale_by_cycles(snd.buffer_len, Machine->gamedrv->cpu[0].cpu_freq);
    newpos = std::clamp(newpos, 0, snd.buffer_len);
    int delta = newpos - snd.sample_pos;
    if (delta > 0) { audio_render(snd.output_buffer + snd.sample_pos, delta); snd.sample_pos = newpos; }
}

int phoenix_audio_sh_start(void) {
    std::memset(&snd, 0, sizeof(snd));
    int fps = Machine->gamedrv->fps;
    if (fps <= 0) fps = 60;
    snd.buffer_len = config.samplerate / fps;
    snd.samplerate = snd.buffer_len * fps;

    snd.poly18 = (uint32_t*)std::malloc((1ul << 13) * sizeof(uint32_t));
    uint32_t shiftreg = 0;
    for (int i = 0; i < (1 << 13); i++) {
        uint32_t bits = 0;
        for (int j = 0; j < 32; j++) {
            bits = (bits >> 1) | (shiftreg << 31);
            if (((shiftreg >> 16) & 1) == ((shiftreg >> 17) & 1)) shiftreg = (shiftreg << 1) | 1;
            else shiftreg <<= 1;
        }
        snd.poly18[i] = bits;
    }

    snd.tone1_level = VMAX / 2;

    snd.output_buffer = (int16_t*)std::malloc(snd.buffer_len * sizeof(int16_t));
    std::memset(snd.output_buffer, 0, snd.buffer_len * sizeof(int16_t));
    stream_start(CUSTOM_SND_CHANNEL, 0, 16, fps);
    return 0;
}

void phoenix_audio_sh_stop(void) {
    stream_stop(CUSTOM_SND_CHANNEL, 0);
    if (snd.poly18) std::free(snd.poly18);
    if (snd.output_buffer) std::free(snd.output_buffer);
    if (snd.stream_buffer_ptr) delete[] snd.stream_buffer_ptr;
    std::memset(&snd, 0, sizeof(snd));
}

void phoenix_audio_sh_update(void) {
    if (!snd.output_buffer) return;
    if (snd.sample_pos < snd.buffer_len) audio_render(snd.output_buffer + snd.sample_pos, snd.buffer_len - snd.sample_pos);
    snd.sample_pos = 0;
    float ratio = (float)config.samplerate / (float)snd.samplerate;
    linear_interpolation_16(snd.output_buffer, snd.buffer_len, &snd.stream_buffer_ptr, &snd.stream_buffer_len, ratio);
    stream_update(CUSTOM_SND_CHANNEL, snd.stream_buffer_ptr);
}

WRITE_HANDLER_NS(phoenix_sound_control_a_w) { audio_doupdate(); snd.latch_a = data; }
WRITE_HANDLER_NS(phoenix_sound_control_b_w) { audio_doupdate(); snd.latch_b = data; mm6221aa_tune_w(data >> 6); }
