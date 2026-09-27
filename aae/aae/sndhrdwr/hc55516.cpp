/*****************************************************************************
  Harris HC55516 CVSD speech decoder
  Decoder math ported verbatim from MAME 0.53 sound/hc55516.c
  (hc55516_clock_w); stream plumbing follows AAE's push model, same pattern
  as dac.cpp (frame buffer + cpu_scale_by_cycles mid-frame catch-up).

  Differences from the MAME original:
  - No MachineSound/hc55516_interface wrapper; hc55516_sh_start() takes a
    volume directly (single chip - that's all Williams boards need).
  - No stream_init() callback with per-sample interpolation between
    curr_value/next_value. AAE's push model instead catches up the frame
    buffer to the CPU's cycle position on every clock edge (dac.cpp's
    sample-and-hold idiom) and lets hc55516_sh_update() fill the tail once
    per video frame.
 *****************************************************************************/

#include <cstring>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include "hc55516.h"
#include "mixer.h"
#include "aae_mame_driver.h"
#include "cpu_control.h"

#define INTEGRATOR_LEAK_TC  0.001
#define FILTER_DECAY_TC     0.004
#define FILTER_CHARGE_TC    0.004
#define FILTER_MIN          0.0416
#define FILTER_MAX          1.0954
#define SAMPLE_GAIN         10000.0

static int      cvsd_channel = -1;
static int16_t* cvsd_frame_buf = nullptr;
static int      cvsd_frame_len = 0;
static int      cvsd_write_pos = 0;

static uint8_t  last_clock, databit, shiftreg;
static int16_t  curr_sample;
static double   filter_state, integrator;
static double   charge, decay, leak;

// dac.cpp's dac_catch_up pattern: hold the current level from the
// last cursor to the CPU's position in the frame.
static void cvsd_catch_up(void)
{
    if (!cvsd_frame_buf || cvsd_frame_len <= 0) return;
    int pos = cpu_scale_by_cycles(cvsd_frame_len, 0);
    if (pos > cvsd_frame_len) pos = cvsd_frame_len;
    if (pos < cvsd_write_pos) pos = cvsd_write_pos;
    for (int s = cvsd_write_pos; s < pos; s++)
        cvsd_frame_buf[s] = curr_sample;
    cvsd_write_pos = pos;
}

int hc55516_sh_start(int volume)
{
    // Time constants assume the nominal ~16 kHz Williams CVSD bit clock.
    charge = pow(exp(-1.0), 1.0 / (FILTER_CHARGE_TC * 16000.0));
    decay  = pow(exp(-1.0), 1.0 / (FILTER_DECAY_TC  * 16000.0));
    leak   = pow(exp(-1.0), 1.0 / (INTEGRATOR_LEAK_TC * 16000.0));

    last_clock = 0; databit = 0; shiftreg = 0;
    curr_sample = 0; filter_state = FILTER_MIN; integrator = 0.0;

    int fps = Machine->gamedrv->fps;
    if (fps <= 0) fps = 60;
    /* frame_len = samples per frame, matching the allocation in stream_start() */
    cvsd_frame_len = config.samplerate / fps;   /* 44100 == SYS_FREQ in mixer.cpp */
    cvsd_write_pos = 0;

    cvsd_channel = mixer_alloc_channel(MIXER_CHIP_STREAM_RANGE_LOW, MIXER_FIRST_RESERVED_CHANNEL);
    if (cvsd_channel < 0) return 1;
    cvsd_frame_buf = (int16_t*)calloc(cvsd_frame_len, sizeof(int16_t));
    if (!cvsd_frame_buf) { cvsd_channel = -1; return 1; }
    stream_start(cvsd_channel, 0, 16, fps, /*stereo=*/false);
    sample_set_volume(cvsd_channel, std::clamp(volume, 0, 255));
    return 0;
}

void hc55516_sh_stop(void)
{
    if (cvsd_channel >= 0) { stream_stop(cvsd_channel, 0); cvsd_channel = -1; }
    free(cvsd_frame_buf); cvsd_frame_buf = nullptr;
}

void hc55516_sh_update(void)
{
    if (!cvsd_frame_buf || cvsd_channel < 0) return;
    for (int s = cvsd_write_pos; s < cvsd_frame_len; s++)
        cvsd_frame_buf[s] = curr_sample;
    stream_update(cvsd_channel, cvsd_frame_buf);
    cvsd_write_pos = 0;
}

void hc55516_digit_w(int data) { databit = data & 1; }

void hc55516_clock_w(int state)
{
    int clock = state & 1;
    int diff = clock ^ last_clock;
    last_clock = (uint8_t)clock;
    if (!(diff && clock)) return;   // rising edge only

    cvsd_catch_up();                // flush the old level up to now

    // --- decoder core, MAME 0.53 hc55516.c hc55516_clock_w, unchanged ---
    double integ = integrator, temp;
    if (databit) { shiftreg = ((shiftreg << 1) | 1) & 7; integ += filter_state; }
    else         { shiftreg =  (shiftreg << 1) & 7;      integ -= filter_state; }
    integ *= leak;
    if (shiftreg == 0 || shiftreg == 7) {
        filter_state = FILTER_MAX - ((FILTER_MAX - filter_state) * charge);
        if (filter_state > FILTER_MAX) filter_state = FILTER_MAX;
    } else {
        filter_state *= decay;
        if (filter_state < FILTER_MIN) filter_state = FILTER_MIN;
    }
    temp = integ * SAMPLE_GAIN;
    integrator = integ;
    if (temp < 0) curr_sample = (int16_t)(temp / (-temp * (1.0 / 32768.0) + 1.0));
    else          curr_sample = (int16_t)(temp / ( temp * (1.0 / 32768.0) + 1.0));
}
