// -----------------------------------------------------------------------------
// galsnd_stream.cpp — Galaxian-style streaming (tone + LFO,  NE555 path)
// Mono S16, integer FPS.
//
// Tone path is :
//   - GalStep/GalShift = 32/5
//   - 4-level ladder, partial updates honored
//
// LFO
//   - 4 DAC bits (0x6004..0x6007 via galaxian_lfo_freq_w) select an NE555 RC net
//   - We compute r0/r1 from 330k and (1M, 470k, 220k, 100k) per bit, then
//       rx = 100k + 2M * r0/(r0 + r1)
//     exactly like the legacy code you quoted.
//   - The 555 astable period is T = 0.693 * (Ra + 2*Rb) * C, with C = 1uF.
//     We use rx as (Ra + 2*Rb) and distribute that period evenly across the
//     sweep range [MINFREQ..MAXFREQ], stepping freq by 1 per "tick".
//   - Three square-wave background lines run at freq × ratios (as in original).
//   - Background enable bits gate the three lines independently.
//   - A tunable runtime sweep multiplier lets you speed up/slow down the sweep.
//
// -----------------------------------------------------------------------------

#define NOMINMAX
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "galsnd_stream.h"
#include "aae_mame_driver.h"   // Machine, cpu_scale_by_cycles
#include "mixer.h"             // stream_start/stop/update()
#include "framework.h"         // config.samplerate

// -----------------------------------------------------------------------------
// Constants (legacy-compatible)
// -----------------------------------------------------------------------------
static constexpr int TOOTHSAW_LENGTH = 16;
static constexpr int TOOTHSAW_AMPLITUDE = 64;

// Sweep range for 'freq' that drives the LFO sweep counter.
static constexpr int MINFREQ = (139 - 139 / 3);    // ~93
static constexpr int MAXFREQ = 170;//(139 + 139 / 3);    // ~185

// Ratios for the three background lines (as in legacy code)
//static constexpr double LFO_RATIO_0 = 1.0;
//static constexpr double LFO_RATIO_1 = (100.0 + 2.0 * 330.0) / (100.0 + 2.0 * 470.0);
//static constexpr double LFO_RATIO_2 = (100.0 + 2.0 * 220.0) / (100.0 + 2.0 * 470.0);

static constexpr double LFO_RATIO_0 = .9;
static constexpr double LFO_RATIO_1 = 4.0 / 3.0;   // ≈ 1.3333
static constexpr double LFO_RATIO_2 = 5.0 / 3.0;   // ≈ 1.6667
static constexpr int    LFO_OUT_DIVIDE = 8;        // try 8 (or 9) for closer loudness
// Live-tunable working copy (defaults to compile-time constants)
static double g_ratio[3] = { LFO_RATIO_0, LFO_RATIO_1, LFO_RATIO_2 };

// 555 timing constant (T = K_555 * (Ra + 2*Rb) * C). With C = 1e-6 F:
// legacy comments used 0.693; earlier variants sometimes used 0.639 empirically.
// We'll use the textbook 0.693 to match your note.
static constexpr double K_555 = 0.693;

// -----------------------------------------------------------------------------
// Stream state
// -----------------------------------------------------------------------------
static int         g_fps_rounded = 60;
static int         g_sys_rate = 44100;
static int         g_buffer_len = 0;
static int         g_sample_pos = 0;
static std::vector<int16_t> g_output_buffer;

static int         g_stream_chan_id = 12;      // choose a free mixer channel

// -----------------------------------------------------------------------------
// Tone ladder (LOCKED): LUTs + registers + fixed GalStep/GalShift
// -----------------------------------------------------------------------------
static std::array<std::array<int8_t, TOOTHSAW_LENGTH>, 4> g_tone_lut{};
static int  g_pitch = 0xff;  // 0xff => mute
static int  g_vol = 0;     // two 1-bit writes -> 0..3

static int  g_GalStep = 32;   // LOCKED
static int  g_GalShift = 5;    // LOCKED

// tone phase (galsnd.c style)
static int  t_counter = 0;
static int  t_countdown = 0;

// -----------------------------------------------------------------------------
// LFO (legacy NE555 path)
// -----------------------------------------------------------------------------
static std::array<int, 3> g_bg_enable{ 0,0,0 }; // individual enables

// Debug controls/state
static bool   g_dbg_enabled = false;
static int    g_dbg_every = 60;     // log once per N video frames
static int    g_dbg_frame_no = 0;
static double g_dbg_fhz[3] = { 0.0, 0.0, 0.0 }; // last computed line Hz
static double g_dbg_tick_s = 0.0;               // last computed tick period

// DAC state from writes to 0x6004..0x6007 (LSB..MSB)
static int g_lfobit[4] = { 0,0,0,0 };

// Sweep 'freq' and timer integration
static int    g_freq = MAXFREQ; // starts at top
static double g_lfo_tick_period_s = 0.0;     // seconds per "freq--" step (post-scale)
static double g_lfo_tick_accum_s = 0.0;     // accumulated time since last step

// Runtime sweep multiplier (tunable): 1.0 = stock, >1 faster, <1 slower
static double g_lfo_speed_mult = 1.0;

// Square-wave phases & steps for 3 lines (8.24 fixed)
static uint32_t g_lfo_phase[3] = { 0,0,0 };
static constexpr uint32_t PHASE_ONE = 1u << 24;
static uint32_t g_lfo_step[3] = { 0,0,0 };  // per-sample phase step for each line

// Forward decls
static void recompute_lfo_timer_from_bits_legacy();
static void recompute_lfo_line_steps();

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------
static inline int16_t sat16(int32_t v)
{
    if (v < -32768) return -32768;
    if (v > 32767) return  32767;
    return (int16_t)v;
}

// ------------------------ Debug & live tuning API ------------------------------
void galaxian_lfo_debug_enable(int enabled, int everyNFrames)
 {
    g_dbg_enabled = (enabled != 0);
    g_dbg_every = std::max(1, everyNFrames);
    g_dbg_frame_no = 0;
    }

void galaxian_lfo_set_ratios(double r0, double r1, double r2)
 {
    if (r0 > 0.0) g_ratio[0] = r0;
    if (r1 > 0.0) g_ratio[1] = r1;
    if (r2 > 0.0) g_ratio[2] = r2;
    recompute_lfo_line_steps(); // apply immediately
    }


static void build_tone_tables()
{
    for (int i = 0; i < TOOTHSAW_LENGTH; ++i)
    {
        auto V = [](double r0, double r1) -> int8_t {
            const double out = 2.0 * TOOTHSAW_AMPLITUDE * (r0 / (r0 + r1)) - TOOTHSAW_AMPLITUDE;
            return (int8_t)std::lround(out);
            };

        double r0a = 1.0 / 1e12, r1a = 1.0 / 1e12;
        double r0b = 1.0 / 1e12, r1b = 1.0 / 1e12;

        // base: R51=33k, R50=22k
        if (i & 1) { r1a += 1.0 / 33000; r1b += 1.0 / 33000; }
        else { r0a += 1.0 / 33000; r0b += 1.0 / 33000; }
        if (i & 4) { r1a += 1.0 / 22000; r1b += 1.0 / 22000; }
        else { r0a += 1.0 / 22000; r0b += 1.0 / 22000; }
        g_tone_lut[0][i] = V(1.0 / r0a, 1.0 / r1a);

        // VOL1=1 (add 10k on QC)
        if (i & 4) r1a += 1.0 / 10000; else r0a += 1.0 / 10000;
        g_tone_lut[1][i] = V(1.0 / r0a, 1.0 / r1a);

        // VOL2=1 (add 15k on QD)
        if (i & 8) r1b += 1.0 / 15000; else r0b += 1.0 / 15000;
        g_tone_lut[2][i] = V(1.0 / r0b, 1.0 / r1b);

        // VOL1+VOL2=1 (extra 10k on other leg)
        if (i & 4) r0b += 1.0 / 10000; else r1b += 1.0 / 10000;
        g_tone_lut[3][i] = V(1.0 / r0b, 1.0 / r1b);
    }
}

// galsnd.c-style tone update for a span (render into dst)
static void tone_update_to_buffer(int16_t* dst, int length)
{
    if (length <= 0) return;

    if (g_pitch != 0xff)
    {
        const int8_t* w = g_tone_lut[std::clamp(g_vol, 0, 3)].data();
        for (int i = 0; i < length; ++i)
        {
            int acc = 0;
            for (int j = 0; j < g_GalStep; ++j)
            {
                if (t_countdown >= 256)
                {
                    t_counter = (t_counter + 1) & (TOOTHSAW_LENGTH - 1);
                    t_countdown = g_pitch;   // reload from register
                }
                ++t_countdown;
                acc += w[t_counter];         // -64..+63
            }
            const int32_t s = (acc >> g_GalShift); // ~ -64..+63
            dst[i] = sat16(s << 8);               // to S16
        }
    }
    else
    {
        std::fill(dst, dst + length, 0);
    }
}

// -----------------------------------------------------------------------------
// LFO computation (legacy NE555 path, NOT NEW_LFO)
// -----------------------------------------------------------------------------

// Compute the tick period from current lfobit[] using the exact legacy path:
//
//   r0 starts with 330k to GND; r1 starts "open" (~infinite)
//   For each bit (1M, 470k, 220k, 100k):
//     if bit==1, resistor shunts to r1; else to r0
//   Effective resistors:
//     r0 = 1 / r0, r1 = 1 / r1
//     rx = 100k + 2M * r0/(r0 + r1)
//
// Then 555 period T = 0.693 * (Ra + 2*Rb) * C,
// we take C = 1e-6 F and use rx as (Ra + 2*Rb).
// We distribute T evenly across the sweep span so each "tick" decrements
// g_freq by 1; i.e. tick_period = T / (MAXFREQ - MINFREQ).
static void recompute_lfo_timer_from_bits_legacy()
{
    // Start with conductances (1/R), then invert back to ohms at the end
    double r0c = 1.0 / 330000.0; // 330k to ground
    double r1c = 1.0 / 1e12;     // "open"

    // R18 1M
    if (g_lfobit[0]) r1c += 1.0 / 1000000.0; else r0c += 1.0 / 1000000.0;
    // R17 470k
    if (g_lfobit[1]) r1c += 1.0 / 470000.0;  else r0c += 1.0 / 470000.0;
    // R16 220k
    if (g_lfobit[2]) r1c += 1.0 / 220000.0;  else r0c += 1.0 / 220000.0;
    // R15 100k
    if (g_lfobit[3]) r1c += 1.0 / 100000.0;  else r0c += 1.0 / 100000.0;

    const double r0 = 1.0 / r0c;  // ohms
    const double r1 = 1.0 / r1c;  // ohms

    // Legacy effective (Ra + 2*Rb) in ohms:
    // rx = 100k + 2M * r0/(r0 + r1)
    const double rx = 100000.0 + 2000000.0 * (r0 / (r0 + r1));

    // 555 astable period for C=1uF: T = 0.693 * rx * 1e-6 seconds
    const double T_s = K_555 * rx * 1e-6;

    // Distribute evenly across sweep span to get per-step "tick" period
    const double span = double(MAXFREQ - MINFREQ);
    const double base_tick_s = (span > 0.0) ? (T_s / span) : 0.01;

    // Apply runtime multiplier (1.0 = stock; >1 = faster => smaller period)
    const double mult = std::clamp(g_lfo_speed_mult, 0.0625, 16.0);
    g_lfo_tick_period_s = base_tick_s / mult;
    g_dbg_tick_s = g_lfo_tick_period_s; // expose to logger

    if (!(g_lfo_tick_period_s > 0.0)) g_lfo_tick_period_s = 0.01; // safety
}

// Update the per-sample phase steps for the 3 background lines from current freq
static void recompute_lfo_line_steps()
{
    // Each line runs at base Hz = freq * ratio
    const double f0 = std::max(1.0, double(g_freq) * g_ratio[0]);
    const double f1 = std::max(1.0, double(g_freq) * g_ratio[1]);
    const double f2 = std::max(1.0, double(g_freq) * g_ratio[2]);
        // For debug: stash the most recent instantaneous line Hz
        g_dbg_fhz[0] = f0; g_dbg_fhz[1] = f1; g_dbg_fhz[2] = f2;

    const double step0 = (f0 / double(g_sys_rate)) * double(PHASE_ONE);
    const double step1 = (f1 / double(g_sys_rate)) * double(PHASE_ONE);
    const double step2 = (f2 / double(g_sys_rate)) * double(PHASE_ONE);

    g_lfo_step[0] = (uint32_t)std::llround(step0);
    g_lfo_step[1] = (uint32_t)std::llround(step1);
    g_lfo_step[2] = (uint32_t)std::llround(step2);
}

// Advance the LFO sweep once per video frame (like the legacy timer callback)
static void lfo_advance_one_frame()
{
    const double frame_s = 1.0 / double(g_fps_rounded);
    g_lfo_tick_accum_s += frame_s;

    if (g_lfo_tick_period_s <= 0.0) return;

    const double ticks = g_lfo_tick_accum_s / g_lfo_tick_period_s;
    int steps = (int)std::floor(ticks);
    if (steps > 0)
    {
        g_lfo_tick_accum_s -= double(steps) * g_lfo_tick_period_s;

        // emulate lfo timer: freq--, wrap MIN->MAX
        while (steps--)
        {
            if (g_freq > MINFREQ) g_freq--;
            else                  g_freq = MAXFREQ;
        }

        // update square wave step sizes to reflect new freq
        recompute_lfo_line_steps();
    }
}

// -----------------------------------------------------------------------------
// Mix a span into g_output_buffer at the current frame (tone + 3 LFO lines)
// -----------------------------------------------------------------------------
static void mix_frame(int16_t* out, int len)
{
    if (len <= 0) return;

    static std::vector<int32_t> acc;
    if ((int)acc.size() < len) acc.resize(len);
    std::fill(acc.begin(), acc.begin() + len, 0);

    // ----- Tone (LOCKED) -----
    {
        static std::vector<int16_t> tmp;
        if ((int)tmp.size() < len) tmp.resize(len);
        tone_update_to_buffer(tmp.data(), len);
        for (int i = 0; i < len; ++i) acc[i] += tmp[i];
    }

    // ----- LFO background lines (square waves) -----
    if (g_bg_enable[0] || g_bg_enable[1] || g_bg_enable[2])
    {
        for (int i = 0; i < len; ++i)
        {
            int32_t s = 0;

            // Line 0
            if (g_bg_enable[0]) {
                g_lfo_phase[0] += g_lfo_step[0];
                const bool hi0 = (g_lfo_phase[0] & (PHASE_ONE >> 1)) != 0; // 50% duty
                s += (hi0 ? +0x4000 : -0x4000) / LFO_OUT_DIVIDE;
            }
            // Line 1
            if (g_bg_enable[1]) {
                g_lfo_phase[1] += g_lfo_step[1];
                const bool hi1 = (g_lfo_phase[1] & (PHASE_ONE >> 1)) != 0;
                s += (hi1 ? +0x4000 : -0x4000) / LFO_OUT_DIVIDE;
            }
            // Line 2
            if (g_bg_enable[2]) {
                g_lfo_phase[2] += g_lfo_step[2];
                const bool hi2 = (g_lfo_phase[2] & (PHASE_ONE >> 1)) != 0;
                s += (hi2 ? +0x4000 : -0x4000) / LFO_OUT_DIVIDE;
            }

            acc[i] += s;
        }
    }

    // Final clamp
    for (int i = 0; i < len; ++i) out[i] = sat16(acc[i]);
}

// -----------------------------------------------------------------------------
// Lifecycle
// -----------------------------------------------------------------------------
int galaxian_sh_start_stream(const MachineSound* /*msound*/)
{
    g_sys_rate = std::max(8000, config.samplerate);
    g_fps_rounded = Machine->gamedrv->fps;          // integer FPS in your tree
    g_buffer_len = std::max(1, g_sys_rate / g_fps_rounded);

    g_output_buffer.assign(g_buffer_len, 0);
    g_sample_pos = 0;

    build_tone_tables();

    // Tone stepping fixed (LOCKED)
    g_GalStep = 32;
    g_GalShift = 5;
    // ADJUSTMENT for LFO Speed!!!!!!!!!!!!!!!!!!!
    galaxian_set_lfo_speed_scale(1.25 / 1.35);
    // Start stream
    stream_start(g_stream_chan_id, 0, /*bits*/16, g_fps_rounded);

    //Debug
    galaxian_lfo_debug_enable(/*enabled*/0, /*everyNFrames*/6); // log ~10x/sec at 60fps
    galaxian_background_enable_w(0, 1);
    galaxian_background_enable_w(1, 1);
    galaxian_background_enable_w(2, 1);
    // Mute tone ladder:
    galaxian_pitch_w(0xff);
    galaxian_lfo_set_ratios(1.00, 1.31, 1.62);

    // Defaults
    g_pitch = 0xff;
    g_vol = 0;

    g_bg_enable = { 0,0,0 };
    g_freq = MAXFREQ;

    // LFO init (legacy resistor net, 1.0× speed by default)
    g_lfobit[0] = g_lfobit[1] = g_lfobit[2] = g_lfobit[3] = 0;
    g_lfo_tick_accum_s = 0.0;
    g_lfo_speed_mult = 1.0;   // stock sweep rate
    recompute_lfo_timer_from_bits_legacy();
    recompute_lfo_line_steps();

    // phases
    g_lfo_phase[0] = g_lfo_phase[1] = g_lfo_phase[2] = 0;

    t_counter = 0;
    t_countdown = 0;

    return 0;
}

void galaxian_sh_stop_stream(void)
{
    g_output_buffer.clear();
    stream_stop(g_stream_chan_id, 0);
}

void galaxian_sh_update_stream(void)
{
    // render remaining span for this frame
    if (g_buffer_len > g_sample_pos)
        mix_frame(g_output_buffer.data() + g_sample_pos, g_buffer_len - g_sample_pos);

    g_sample_pos = 0;

    // Advance LFO timer & sweeper once per video frame
    lfo_advance_one_frame();

    // Optional periodic log (once per N video frames) of instantaneous params
    if (g_dbg_enabled && ((g_dbg_frame_no++ % g_dbg_every) == 0)) {
                // Bits order: MSB..LSB to match schematic-style reading
           const int b3 = g_lfobit[3], b2 = g_lfobit[2], b1 = g_lfobit[1], b0 = g_lfobit[0];
        LOG_INFO("LFO DBG: base=%d Hz | lines={%.2f, %.2f, %.2f} Hz | ratios={%.5f, %.5f, %.5f} | tick=%.6f s | en={%d,%d,%d} | bits(MSB..LSB)=%d%d%d%d",
          g_freq,
          g_dbg_fhz[0], g_dbg_fhz[1], g_dbg_fhz[2],
          g_ratio[0], g_ratio[1], g_ratio[2],
          g_dbg_tick_s,
          g_bg_enable[0], g_bg_enable[1], g_bg_enable[2],
          b3, b2, b1, b0
            );
        
    }

    // Push to mixer at end of video frame
    stream_update(g_stream_chan_id, g_output_buffer.data());
}

void galaxian_doupdate_stream(void)
{
    if (g_buffer_len <= 0) return;

    const int cpu0_hz = Machine->gamedrv->cpu[0].cpu_freq;
    const int newpos = cpu_scale_by_cycles(g_buffer_len, cpu0_hz);
    const int delta = newpos - g_sample_pos;

    if (delta > 0) {
        mix_frame(g_output_buffer.data() + g_sample_pos, delta);
        g_sample_pos = newpos;
    }
}

// -----------------------------------------------------------------------------
// Register writes
// -----------------------------------------------------------------------------
void galaxian_pitch_w(int data)
{
    galaxian_doupdate_stream();
    g_pitch = 0xff;//data & 0xff; // 0xff => mute
}

void galaxian_vol_w(int offset, int data)
{
    galaxian_doupdate_stream();
    const int bit = (data & 1);
    if (offset == 0) g_vol = (g_vol & ~1) | bit;          // 0x6806
    else             g_vol = (g_vol & ~2) | (bit << 1);   // 0x6807
}

void galaxian_background_enable_w(int offset, int data)
{
    galaxian_doupdate_stream();
    if (offset >= 0 && offset < 3) g_bg_enable[offset] = (data & 1) ? 1 : 0;
}

// 4 DAC-controlled inputs for LFO resistor net (0x6004..0x6007)
void galaxian_lfo_freq_w(int offset, int data)
{
    if (offset < 0 || offset > 3) return;
    galaxian_doupdate_stream();

    const int newbit = (data & 1);
    if (g_lfobit[offset] == newbit) return; // only act on changes, like legacy
    g_lfobit[offset] = newbit;

    // Recompute tick period using the legacy (non-NEW_LFO) resistor math
    recompute_lfo_timer_from_bits_legacy();
    // Note: per-line steps are recomputed only when freq actually steps
}

// -----------------------------------------------------------------------------
// Tuning: runtime sweep-speed multiplier (kept separate from resistor math)
// -----------------------------------------------------------------------------
void galaxian_set_lfo_speed_scale(double scale)
{
    if (!std::isfinite(scale)) return;
    g_lfo_speed_mult = std::clamp(scale, 0.25, 8.0); // sane range
    // Apply immediately to the current timer period without touching r0/r1
    recompute_lfo_timer_from_bits_legacy();
}
