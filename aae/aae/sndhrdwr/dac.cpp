/*****************************************************************************
  DAC (Digital-to-Analog Converter) sound emulation
  Ported from MAME 0.36 to AAE (Another Arcade Emulator)

  Differences from the MAME original:
  - No MachineSound wrapper; DAC_sh_start() takes a DACinterface* directly.
  - No stream_init() callback.  AAE uses a push model: DAC_sh_update() is
    called once per video frame, fills each channel's frame buffer with the
    current DC output level, then calls stream_update().
  - stream_update() in AAE takes (chanid, int16_t*) rather than (chanid, 0).
    The old "flush pending samples" idiom in DAC_data_w / DAC_signed_data_w
    is removed; the next sh_update() will pick up the new level.
  - DAC_sh_stop() added to mirror sh_start() for proper cleanup.
  - WRITE_HANDLER expands to the AAE signature:
      static void name(UINT32 address, UINT8 data, struct MemoryWriteByte *p)
    Only 'data' is used from those arguments.

 *****************************************************************************/

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "dac.h"
#include "mixer.h"           /* stream_start, stream_update, stream_stop */
#include "aae_mame_driver.h" /* Machine, WRITE_HANDLER */
#include "cpu_control.h"     /* cpu_scale_by_cycles - mid-frame write position */

/* -------------------------------------------------------------------------
   Module-level state
   ------------------------------------------------------------------------- */
static const struct DACinterface *intf = nullptr;

/* Per-channel mixer stream channel id returned by stream_start() */
static int dac_channel[MAX_DAC];

/* Per-channel current DC output level (int16) */
static int dac_output[MAX_DAC];

/* Per-channel frame buffer: the reconstructed waveform for the current frame */
static int16_t *dac_frame_buf[MAX_DAC];

/* Per-channel fill cursor: how far into this frame's buffer we've written.
   Advanced on each level change (mid-frame catch-up) and flushed to the end
   of the buffer in DAC_sh_update(). */
static int dac_write_pos[MAX_DAC];

/* Number of int16 samples per frame, shared across all channels */
static int dac_frame_len = 0;

/* Pre-built linear volume lookup tables */
static int UnsignedVolTable[256]; /* 0..255  -> 0..32767     */
static int SignedVolTable[256];   /* 0..255  -> -32768..32767 */

/* -------------------------------------------------------------------------
   DAC_build_voltable - fill both lookup tables.
   These are linear maps exactly as in the MAME original.
   ------------------------------------------------------------------------- */
static void DAC_build_voltable(void)
{
    for (int i = 0; i < 256; i++)
    {
        UnsignedVolTable[i] = i * 0x101 / 2;       /* 0 -> 0, 255 -> 32767 */
        SignedVolTable[i]   = i * 0x101 - 0x8000;  /* 0 -> -32768, 255 -> 32767 */
    }
}

/* =========================================================================
   Data write functions
   These are called by the CPU emulation whenever the game writes to the
   DAC register.  They update the current output level immediately; the new
   value will be picked up by the next DAC_sh_update() call.
   ========================================================================= */

/*
 * dac_catch_up - write the CURRENT (about-to-change) output level into the
 * frame buffer from the last cursor position up to the CPU's current position
 * within the frame, then advance the cursor. This is the AAE equivalent of
 * MAME's "stream_update() before changing the register": it samples-and-holds
 * the old level for exactly the time it was active, so a CPU that writes the
 * DAC many times per frame (e.g. the Gyruss i8039) reconstructs a real
 * waveform instead of being decimated to one sample per frame.
 */
static void dac_catch_up(int num)
{
    if (!dac_frame_buf[num] || dac_frame_len <= 0)
        return;

    /* Current sample position within the frame for the writing CPU. clock=0
       lets cpu_scale_by_cycles derive it from the active CPU's own clock. */
    int pos = cpu_scale_by_cycles(dac_frame_len, 0);
    if (pos > dac_frame_len) pos = dac_frame_len;
    if (pos < dac_write_pos[num]) pos = dac_write_pos[num];   /* monotonic */

    int16_t level = (int16_t)dac_output[num];
    for (int s = dac_write_pos[num]; s < pos; s++)
        dac_frame_buf[num][s] = level;
    dac_write_pos[num] = pos;
}

/* Flush the old level up to now, then latch the new one (MAME order). */
static void dac_set_level(int num, int out)
{
    if (dac_output[num] != out)
    {
        dac_catch_up(num);
        dac_output[num] = out;
    }
}

/* DAC_data_w - unsigned 8-bit; 0..255 -> 0..32767. */
void DAC_data_w(int num, int data) { dac_set_level(num, UnsignedVolTable[data & 0xff]); }

/* DAC_signed_data_w - signed 8-bit (two's complement) -> -32768..32767. */
void DAC_signed_data_w(int num, int data) { dac_set_level(num, SignedVolTable[data & 0xff]); }

/* DAC_data_16_w - unsigned 16-bit; 0..65535 -> 0..32767 (top bit dropped). */
void DAC_data_16_w(int num, int data) { dac_set_level(num, data >> 1); }

/* DAC_signed_data_16_w - signed 16-bit; 0..65535 -> -32768..32767. */
void DAC_signed_data_16_w(int num, int data) { dac_set_level(num, data - 0x8000); }

/* =========================================================================
   Convenience write handlers for use directly in memory maps.
   WRITE_HANDLER expands to:
     static void name(UINT32 address, UINT8 data, struct MemoryWriteByte *p)
   The address and psMemWrite arguments are unused.
   ========================================================================= */

WRITE_HANDLER(DAC_0_data_w)        { DAC_data_w(0, data); }
WRITE_HANDLER(DAC_1_data_w)        { DAC_data_w(1, data); }
WRITE_HANDLER(DAC_0_signed_data_w) { DAC_signed_data_w(0, data); }
WRITE_HANDLER(DAC_1_signed_data_w) { DAC_signed_data_w(1, data); }

/* =========================================================================
   Lifecycle
   ========================================================================= */

/*
 * DAC_sh_start - initialise all DAC channels declared in the interface.
 * Call once from the driver's machine_init (or equivalent) after mixer_init().
 *
 * Returns 0 on success, 1 on any failure.
 */
int DAC_sh_start(const struct DACinterface *intf_in)
{
    intf = intf_in;

    DAC_build_voltable();

    int fps = Machine->gamedrv->fps;
    if (fps <= 0) fps = 60;

    /* frame_len = samples per frame, matching the allocation in stream_start() */
    dac_frame_len = config.samplerate / fps;   /* 44100 == SYS_FREQ in mixer.cpp */

    for (int i = 0; i < intf->num; i++)
    {
        dac_output[i]    = 0;
        dac_write_pos[i] = 0;

        /* Allocate a mixer channel out of the chip-stream range. */
        dac_channel[i] = mixer_alloc_channel(MIXER_CHIP_STREAM_RANGE_LOW, MIXER_FIRST_RESERVED_CHANNEL);
        if (dac_channel[i] < 0) {
            LOG_DEBUG("DAC #%d: no free mixer channel in chip stream range", i);
            return 1;
        }

        dac_frame_buf[i] = (int16_t *)malloc(dac_frame_len * sizeof(int16_t));
        if (!dac_frame_buf[i])
        {
            LOG_DEBUG("DAC #%d: frame buffer malloc failed", i);
            return 1;
        }
        memset(dac_frame_buf[i], 0, dac_frame_len * sizeof(int16_t));

        /* Register mono 16-bit stream with the mixer */
        stream_start(dac_channel[i], 0, 16, fps, /*stereo=*/false);
    }

    return 0;
}

/*
 * DAC_sh_stop - free per-channel state and stop the streaming channels.
 * Call from the driver's machine_stop (or equivalent).
 */
void DAC_sh_stop(void)
{
    if (!intf)
        return;

    for (int i = 0; i < intf->num; i++)
    {
        if (dac_channel[i] >= 0) {
            stream_stop(dac_channel[i], 0);
            dac_channel[i] = -1;
        }

        if (dac_frame_buf[i])
        {
            free(dac_frame_buf[i]);
            dac_frame_buf[i] = nullptr;
        }
        dac_output[i]  = 0;
    }

    intf = nullptr;
}

/*
 * DAC_sh_update - push one frame of audio to the mixer for every DAC channel.
 * Call once per emulated video frame.
 *
 * Any mid-frame level changes were already written into the buffer by
 * dac_catch_up(). Here we fill the remainder (from the last cursor to the end
 * of the frame) with the current level - sample-and-hold for the tail of the
 * frame - then push the buffer and reset the cursor for the next frame.
 */
void DAC_sh_update(void)
{
    if (!intf)
        return;

    for (int i = 0; i < intf->num; i++)
    {
        if (!dac_frame_buf[i] || dac_channel[i] < 0)
            continue;

        /* Hold the current level from the last write to end-of-frame. */
        int16_t level = (int16_t)dac_output[i];
        for (int s = dac_write_pos[i]; s < dac_frame_len; s++)
            dac_frame_buf[i][s] = level;

        stream_update(dac_channel[i], dac_frame_buf[i]);
        dac_write_pos[i] = 0;   /* start the next frame at the buffer head */
    }
}
