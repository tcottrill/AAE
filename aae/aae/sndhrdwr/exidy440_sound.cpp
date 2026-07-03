// -----------------------------------------------------------------------------
// Exidy 440 sound board  -  AAE port
//
// Adapted from M.A.M.E.(TM) sndhrdw/exidy440.c (Aaron Giles; CVSD decoder from
// Zonn Moore / Neil Bradley's Retrocade). The MC6844 DMA register model, the
// command/ack handshake and the channel bookkeeping are preserved verbatim.
//
// STAGE 2 (this revision): the CVSD decode + 4-channel mix are now wired into
// AAE's mixer. The sound 6809 programs the MC6844 to DMA CVSD bytes from the
// sample ROMs (REGION_SOUND1); we decode each requested run to 16-bit PCM (with
// the same FIR post-filter and decode constants as MAME), cache it, and mix the
// four channels (per-channel L/R volume) into one stereo frame per video frame.
// That frame is pushed to a chip-stream voice at the board's native FCLK rate;
// the mixer resamples to the output rate inline (see stream_set_native_rate).
//
// AAE integration changes vs MAME:
//   - cpunum_set_input_line(1, line, state)  -> m_cpu_6809[SND_CPU]->firq/irq_line
//   - MAME's pull-based sound_stream callback -> a per-frame push: we mix one
//     video frame of samples in exidy440_sound_update() and stream_update() it.
//   - auto_malloc / state_save_register_*     -> malloc + plain statics.
// -----------------------------------------------------------------------------

#include "exidy440_sound.h"
#include "cpu_control.h"     // m_cpu_6809[], CPU index
#include "cpu_m6809.h"       // firq_line / irq_line
#include "aae_mame_driver.h" // Machine (fps)
#include "memory.h"          // memory_region / memory_region_length / REGION_SOUND1
#include "mixer.h"           // chip-stream API
#include "sys_log.h"
#include <cstring>
#include <cstdlib>
#include <cmath>

// The Exidy 440 sound 6809 is the second CPU in the vertigo machine (CPU1).
#define SND_CPU  1

// ---- CVSD decoding parameters (from MAME / Zonn Moore's Retrocade decoder) --
#define INTEGRATOR_LEAK_TC   (10e3 * 0.1e-6)
#define FILTER_DECAY_TC      ((18e3 + 3.3e3) * 0.33e-6)
#define FILTER_CHARGE_TC     (18e3 * 0.33e-6)
#define FILTER_MIN           0.0416
#define FILTER_MAX           1.0954
#define SAMPLE_GAIN          10000.0
#define FIR_HISTORY_LENGTH   57       // number of FIR coefficients
#define SAMPLE_BUFFER_LENGTH 1024     // stack decode chunk
#define MAX_CACHE_ENTRIES    1024     // max distinct decoded samples we expect
#define FADE_TO_ZERO         1        // ramp the last 512 samples down (kills clicks)

// ---- MC6844 DMA channel ----------------------------------------------------
typedef struct
{
    int   active;
    int   address;
    int   counter;
    UINT8 control;
    int   start_address;
    int   start_counter;
} m6844_channel_data;

// ---- active playback channel (points into the decoded-sample cache) ---------
typedef struct
{
    INT16 *base;      // decoded 16-bit PCM for the run currently playing
    int    offset;    // output samples consumed so far
    int    remaining; // output samples left to play (0 = idle)
} sound_channel_data;

// ---- one decoded run, kept in the cache so repeats don't re-decode ----------
typedef struct sound_cache_entry
{
    struct sound_cache_entry *next;
    int   address;
    int   length;
    int   bits;
    int   frequency;
    INT16 data[1];    // length*8 decoded samples (variable length allocation)
} sound_cache_entry;

// ---- command/ack handshake (read by the main CPU via sio) ------------------
static UINT8 exidy440_sound_command;
static UINT8 exidy440_sound_command_ack;

// ---- MC6844 description ----------------------------------------------------
static m6844_channel_data m6844_channel[4];
static UINT8 m6844_priority;
static UINT8 m6844_interrupt;
static UINT8 m6844_chain;

// ---- register banks the 6809 reads/writes ----------------------------------
static UINT8 sound_volume[8];   // 2 per channel (L/R), stored inverted (as MAME)
static UINT8 sound_banks[4];    // ROM bank select per channel

// ---- playback state --------------------------------------------------------
static sound_channel_data sound_channel[4];
static int channel_frequency[4];
static int sound_clock;         // FCLK / sample rate (for reset)
static int samples_per_frame;   // samples produced per video frame (FCLK / fps)

// channels 0,1 are MC3418s (4-bit CVSD); channels 2,3 are MC3417s (3-bit CVSD)
static const int channel_bits[4] = { 4, 4, 3, 3 };

// ---- decoded-sample cache --------------------------------------------------
static UINT8             *sound_rom = nullptr;   // REGION_SOUND1 base
static int                sound_rom_len = 0;
static sound_cache_entry *sound_cache = nullptr;
static sound_cache_entry *sound_cache_end = nullptr;
static sound_cache_entry *sound_cache_max = nullptr;

// ---- per-frame mix accumulators + AAE chip-stream voice ---------------------
static INT32 *mixer_buffer_left = nullptr;
static INT32 *mixer_buffer_right = nullptr;
static short *stream_out = nullptr;   // interleaved stereo handed to stream_update
static int    stream_buf_frames = 0;  // frames produced per update (== samples_per_frame)
static int    g_stream_ch = -1;       // mixer channel for the board's stereo stream
static int    mix_pos = 0;            // frames mixed so far this video frame

// ---- forward decls ---------------------------------------------------------
static void   exidy440_m6844_update(void);
static void   m6844_finished(int ch);
static void   play_cvsd(int ch);
static void   stop_cvsd(int ch);
static void   reset_sound_cache(void);
static INT16 *add_to_sound_cache(UINT8 *input, int address, int length, int bits, int frequency);
static INT16 *find_or_add_to_sound_cache(int address, int length, int bits, int frequency);
static void   decode_and_filter_cvsd(UINT8 *input, int bytes, int maskbits, int frequency, INT16 *output);
static void   fir_filter(INT32 *input, INT16 *output, int count);
static void   add_and_scale_samples(int ch, INT32 *dest, int samples, int volume);

// ---- interrupt helpers (raise/clear the sound 6809's lines) ----------------
// The main CPU's command pulses the 6809 FIRQ (line 1); PIT ch1 pulses the IRQ
// (line 0), cleared by the 6809 writing 0x9800.
static void snd_firq(bool asserted)
{
    if (m_cpu_6809[SND_CPU]) m_cpu_6809[SND_CPU]->firq_line(asserted);
}
static void snd_irq(bool asserted)
{
    if (m_cpu_6809[SND_CPU]) m_cpu_6809[SND_CPU]->irq_line(asserted);
}


// ===========================================================================
//  Lifecycle
// ===========================================================================
void exidy440_sound_init(int clock_hz)
{
    int i;

    // Idempotent: drop any prior allocations / stream so a re-init can't leak
    // or strand a mixer channel.
    exidy440_sound_stop();

    exidy440_sound_command = 0;
    exidy440_sound_command_ack = 1;

    for (i = 0; i < 4; i++)
    {
        m6844_channel[i].active  = 0;
        m6844_channel[i].control = 0x00;
    }
    m6844_priority  = 0x00;
    m6844_interrupt = 0x00;
    m6844_chain     = 0x00;

    memset(sound_volume,  0, sizeof(sound_volume));
    memset(sound_banks,   0, sizeof(sound_banks));
    memset(sound_channel, 0, sizeof(sound_channel));

    sound_clock = clock_hz;
    channel_frequency[0] = clock_hz;      // channels 0,1 run at FCLK
    channel_frequency[1] = clock_hz;
    channel_frequency[2] = clock_hz / 2;  // channels 2,3 run at SCLK (half)
    channel_frequency[3] = clock_hz / 2;

    int fps = (Machine && Machine->gamedrv && Machine->gamedrv->fps > 0)
              ? Machine->gamedrv->fps : 60;
    samples_per_frame = clock_hz / fps;
    if (samples_per_frame < 1) samples_per_frame = 1;
    stream_buf_frames = samples_per_frame;

    // Decoded-sample cache: worst case is the whole sample ROM decoded (each
    // input byte -> 8 samples -> 16 bytes), plus the per-entry headers.
    sound_rom     = memory_region(REGION_SOUND1);
    sound_rom_len = memory_region_length(REGION_SOUND1);
    if (sound_rom && sound_rom_len > 0)
    {
        size_t cache_size = (size_t)sound_rom_len * 16
                          + (size_t)MAX_CACHE_ENTRIES * sizeof(sound_cache_entry);
        sound_cache = (sound_cache_entry *)malloc(cache_size);
        sound_cache_max = (sound_cache_entry *)((UINT8 *)sound_cache + cache_size);
        reset_sound_cache();
    }

    // Per-frame mix accumulators + interleaved output (small margin for safety).
    // Zeroed here and after each frame push; mix_channels_to accumulates (+=).
    mixer_buffer_left  = (INT32 *)malloc(sizeof(INT32) * (stream_buf_frames + 16));
    mixer_buffer_right = (INT32 *)malloc(sizeof(INT32) * (stream_buf_frames + 16));
    stream_out         = (short *)malloc(sizeof(short) * (stream_buf_frames + 16) * 2);
    if (mixer_buffer_left)  memset(mixer_buffer_left,  0, sizeof(INT32) * (stream_buf_frames + 16));
    if (mixer_buffer_right) memset(mixer_buffer_right, 0, sizeof(INT32) * (stream_buf_frames + 16));
    mix_pos = 0;

    // Stereo chip-stream voice at the board's native rate; the mixer resamples
    // to the output rate inline (matches namco.cpp / pleiads_audio.cpp).
    g_stream_ch = mixer_alloc_channel(MIXER_CHIP_STREAM_RANGE_LOW, MIXER_FIRST_RESERVED_CHANNEL);
    if (g_stream_ch >= 0)
    {
        stream_start(g_stream_ch, 0, 16, fps, true);   // 16-bit, stereo
        // Declare the native rate as an EXACT multiple of fps (== the frames we
        // push per update). If we passed the raw FCLK (62500, not a multiple of
        // 60), the mixer would consume BUFFER_SIZE*step = 1041.67 frames/frame
        // against a 1041-frame buffer and wrap mid-frame -> a 60 Hz crackle.
        // Quantizing (matches namco.cpp's emulation_rate = buffer_len*fps) makes
        // consumption land exactly on the buffer boundary. The ~0.06% pitch shift
        // (62460 vs 62500) is inaudible.
        stream_set_native_rate(g_stream_ch, samples_per_frame * fps);
    }
    else
    {
        LOG_ERROR("exidy440 sound: no free mixer channel in chip-stream range");
    }

    LOG_INFO("exidy440 sound init: clock=%d Hz, %d samples/frame, cache=%d bytes, stream ch=%d",
        clock_hz, samples_per_frame, (sound_cache ? (int)((UINT8*)sound_cache_max - (UINT8*)sound_cache) : 0), g_stream_ch);
}

void exidy440_sound_reset(void)
{
    exidy440_sound_init(sound_clock ? sound_clock : 62500);
}

void exidy440_sound_stop(void)
{
    if (g_stream_ch >= 0)
    {
        stream_stop(g_stream_ch, 0);
        g_stream_ch = -1;
    }
    free(sound_cache);        sound_cache = sound_cache_end = sound_cache_max = nullptr;
    free(mixer_buffer_left);  mixer_buffer_left = nullptr;
    free(mixer_buffer_right); mixer_buffer_right = nullptr;
    free(stream_out);         stream_out = nullptr;
}


// ===========================================================================
//  Command register handshake
// ===========================================================================

// Main CPU (68000) writes a sound command: latch it, mark pending, FIRQ the 6809.
void exidy440_sound_command_w(int data)
{
    exidy440_sound_command = (UINT8)data;
    exidy440_sound_command_ack = 0;
    snd_firq(true);
}

// Main CPU polls the ack: 1 = the sound 6809 has read the command.
UINT8 exidy440_sound_command_ack_r(void)
{
    return exidy440_sound_command_ack;
}

// Sound 6809 reads the command (0x8800): clear the FIRQ, ack the main CPU.
UINT8 exidy440_sound_command_r(int offset)
{
    (void)offset;
    snd_firq(false);
    exidy440_sound_command_ack = 1;
    return exidy440_sound_command;
}


// ===========================================================================
//  Volume / bank registers + sound interrupt clear
// ===========================================================================
UINT8 exidy440_sound_volume_r(int offset) { return sound_volume[offset & 7]; }

void exidy440_sound_volume_w(int offset, UINT8 data)
{
    exidy440_m6844_update();            // mix up to now so the change lands on time
    sound_volume[offset & 7] = ~data;   // stored inverted, as on the hardware
}

UINT8 exidy440_sound_banks_r(int offset)            { return sound_banks[offset & 3]; }
void  exidy440_sound_banks_w(int offset, UINT8 data){ sound_banks[offset & 3] = data; }

void exidy440_sound_interrupt_clear_w(int offset, UINT8 data)
{
    (void)offset; (void)data;
    snd_irq(false);
}


// ===========================================================================
//  MC6844 DMA controller
// ===========================================================================
static void m6844_finished(int ch)
{
    m6844_channel_data *channel = &m6844_channel[ch];

    channel->active  = 0;
    channel->counter = 0;
    channel->address = channel->start_address + channel->start_counter;

    channel->control &= ~0x40;   // clear DMA-busy
    channel->control |=  0x80;   // set DMA-end
}

// Mix the active channels forward to `target` (a frame offset within the
// current video frame's accumulators), advancing each channel's offset and the
// MC6844 address/counter as we go. This is MAME's channel_update() stream
// callback split so it can run incrementally within a frame.
static void mix_channels_to(int target)
{
    int ch;

    if (!mixer_buffer_left || !mixer_buffer_right)
        return;
    if (target > stream_buf_frames) target = stream_buf_frames;
    const int count = target - mix_pos;
    if (count <= 0)
        return;

    for (ch = 0; ch < 4; ch++)
    {
        sound_channel_data *channel = &sound_channel[ch];
        int samples, volume, effective_offset;

        if (channel->remaining <= 0)
            continue;

        samples = (count > channel->remaining) ? channel->remaining : count;

        volume = sound_volume[2 * ch + 0];
        if (volume) add_and_scale_samples(ch, mixer_buffer_left + mix_pos, samples, volume);

        volume = sound_volume[2 * ch + 1];
        if (volume) add_and_scale_samples(ch, mixer_buffer_right + mix_pos, samples, volume);

        channel->offset    += samples;
        channel->remaining -= samples;

        // keep the MC6844 address/counter in step with playback
        effective_offset = (ch & 2) ? channel->offset / 2 : channel->offset;
        m6844_channel[ch].address = m6844_channel[ch].start_address + effective_offset / 8;
        m6844_channel[ch].counter = m6844_channel[ch].start_counter - effective_offset / 8;
        if (m6844_channel[ch].counter <= 0)
            m6844_finished(ch);
    }

    mix_pos = target;
}

// Mid-frame catch-up, the AAE equivalent of MAME's stream_update() pull: mix up
// to the current position within the video frame so the 6809 sees the MC6844
// address/counter/DMA-end advance in real time, and so channel starts/stops
// and volume writes land at their true offsets. Without this every DMA state
// change quantizes to frame boundaries -- short sounds started and replaced
// within one frame vanish entirely, and ROM code polling for DMA-end stalls.
static void exidy440_m6844_update(void)
{
    mix_channels_to(cpu_scale_by_cycles(stream_buf_frames, 0));
}

UINT8 exidy440_m6844_r(int offset)
{
    int result = 0;

    exidy440_m6844_update();

    switch (offset)
    {
        case 0x00: case 0x04: case 0x08: case 0x0c:  // address hi
            result = m6844_channel[offset / 4].address >> 8;
            break;
        case 0x01: case 0x05: case 0x09: case 0x0d:  // address lo
            result = m6844_channel[offset / 4].address & 0xff;
            break;
        case 0x02: case 0x06: case 0x0a: case 0x0e:  // counter hi
            result = m6844_channel[offset / 4].counter >> 8;
            break;
        case 0x03: case 0x07: case 0x0b: case 0x0f:  // counter lo
            result = m6844_channel[offset / 4].counter & 0xff;
            break;

        case 0x10: case 0x11: case 0x12: case 0x13:  // channel control
            result = m6844_channel[offset - 0x10].control;
            m6844_channel[offset - 0x10].control &= ~0x80;   // read clears DMA-end
            break;

        case 0x14:  // priority
            result = m6844_priority;
            break;

        case 0x15:  // interrupt control + global DMA-end
            m6844_interrupt &= ~0x80;
            m6844_interrupt |= (m6844_channel[0].control & 0x80) |
                               (m6844_channel[1].control & 0x80) |
                               (m6844_channel[2].control & 0x80) |
                               (m6844_channel[3].control & 0x80);
            result = m6844_interrupt;
            break;

        case 0x16:  // chaining
            result = m6844_chain;
            break;
    }

    return (UINT8)result;
}

void exidy440_m6844_w(int offset, UINT8 data)
{
    int i;

    exidy440_m6844_update();

    switch (offset)
    {
        case 0x00: case 0x04: case 0x08: case 0x0c:  // address hi
            m6844_channel[offset / 4].address = (m6844_channel[offset / 4].address & 0xff) | (data << 8);
            break;
        case 0x01: case 0x05: case 0x09: case 0x0d:  // address lo
            m6844_channel[offset / 4].address = (m6844_channel[offset / 4].address & 0xff00) | (data & 0xff);
            break;
        case 0x02: case 0x06: case 0x0a: case 0x0e:  // counter hi
            m6844_channel[offset / 4].counter = (m6844_channel[offset / 4].counter & 0xff) | (data << 8);
            break;
        case 0x03: case 0x07: case 0x0b: case 0x0f:  // counter lo
            m6844_channel[offset / 4].counter = (m6844_channel[offset / 4].counter & 0xff00) | (data & 0xff);
            break;

        case 0x10: case 0x11: case 0x12: case 0x13:  // channel control
            m6844_channel[offset - 0x10].control = (m6844_channel[offset - 0x10].control & 0xc0) | (data & 0x3f);
            break;

        case 0x14:  // priority: start/stop channels
            m6844_priority = data;
            for (i = 0; i < 4; i++)
            {
                if (!m6844_channel[i].active && (data & (1 << i)))
                {
                    m6844_channel[i].active = 1;
                    m6844_channel[i].control |=  0x40;   // DMA-busy
                    m6844_channel[i].control &= ~0x80;   // clear DMA-end
                    m6844_channel[i].start_address = m6844_channel[i].address;
                    m6844_channel[i].start_counter = m6844_channel[i].counter;
                    play_cvsd(i);
                }
                else if (m6844_channel[i].active && !(data & (1 << i)))
                {
                    m6844_channel[i].active = 0;
                    stop_cvsd(i);
                }
            }
            break;

        case 0x15:  // interrupt control
            m6844_interrupt = (m6844_interrupt & 0x80) | (data & 0x7f);
            break;

        case 0x16:  // chaining
            m6844_chain = data;
            break;
    }
}


// ===========================================================================
//  Decoded-sample cache
// ===========================================================================
static void reset_sound_cache(void)
{
    sound_cache_end = sound_cache;
}

static INT16 *add_to_sound_cache(UINT8 *input, int address, int length, int bits, int frequency)
{
    sound_cache_entry *current = sound_cache_end;

    // where the end will be once we add this entry (length*8 samples => length*16 bytes)
    sound_cache_end = (sound_cache_entry *)((UINT8 *)current + sizeof(sound_cache_entry) + (size_t)length * 16);

    // if this overflows the cache, reset and re-add (a single entry always fits)
    if (sound_cache_end > sound_cache_max)
    {
        reset_sound_cache();
        current = sound_cache_end = sound_cache;
        sound_cache_end = (sound_cache_entry *)((UINT8 *)current + sizeof(sound_cache_entry) + (size_t)length * 16);
        if (sound_cache_end > sound_cache_max)
            return nullptr;   // pathological: entry bigger than the whole cache
    }

    current->next      = sound_cache_end;
    current->address   = address;
    current->length    = length;
    current->bits      = bits;
    current->frequency = frequency;

    decode_and_filter_cvsd(input, length, bits, frequency, current->data);
    return current->data;
}

static INT16 *find_or_add_to_sound_cache(int address, int length, int bits, int frequency)
{
    sound_cache_entry *current;

    for (current = sound_cache; current < sound_cache_end; current = current->next)
        if (current->address == address && current->length == length &&
            current->bits == bits && current->frequency == frequency)
            return current->data;

    return add_to_sound_cache(&sound_rom[address], address, length, bits, frequency);
}


// ===========================================================================
//  Channel playback
// ===========================================================================
static void play_cvsd(int ch)
{
    sound_channel_data *channel = &sound_channel[ch];
    int address = m6844_channel[ch].address;
    int length  = m6844_channel[ch].counter;
    INT16 *base = nullptr;

    // add the bank offset to the address (lowest set bank bit wins, as MAME)
    if (sound_banks[ch] & 1)      address += 0x00000;
    else if (sound_banks[ch] & 2) address += 0x08000;
    else if (sound_banks[ch] & 4) address += 0x10000;
    else if (sound_banks[ch] & 8) address += 0x18000;

    // AAE safety: only decode when the run is fully inside the sample ROM.
    if (sound_cache && sound_rom && address >= 0 && address < sound_rom_len)
    {
        if (address + length > sound_rom_len)
            length = sound_rom_len - address;
        if (length > 0)
            base = find_or_add_to_sound_cache(address, length, channel_bits[ch], channel_frequency[ch]);
    }

    // Any non-playable run -- length 0..3 (MAME's early-out), an out-of-range
    // address, or a decode failure -- must END the channel via m6844_finished(),
    // NOT a bare return. The priority write already set active=1; returning
    // without finishing would wedge the channel 'active, remaining 0' and block
    // every later sound the 6809 starts on it.
    if (!base || length <= 3)
    {
        channel->base      = base;
        channel->offset    = (length > 0) ? length : 0;
        channel->remaining = 0;
        m6844_finished(ch);
        return;
    }

    channel->base      = base;
    channel->offset    = 0;
    channel->remaining = length * 8;   // CVSD = 1 bit per sample
    if (ch & 2)
        channel->remaining *= 2;        // channels 2,3 play half-speed
}

static void stop_cvsd(int ch)
{
    sound_channel[ch].remaining = 0;
}

// Mix `samples` of one channel's decoded PCM (scaled by volume) into dest.
static void add_and_scale_samples(int ch, INT32 *dest, int samples, int volume)
{
    sound_channel_data *channel = &sound_channel[ch];
    INT16 *srcdata;
    int i;

    if (!channel->base)
        return;

    // channels 2,3 are half-rate: each source sample is emitted twice
    if (ch & 2)
    {
        srcdata = &channel->base[channel->offset >> 1];

        // handle the odd starting phase
        if (channel->offset & 1)
        {
            *dest++ += *srcdata++ * volume / 256;
            samples--;
        }

        for (i = 0; i < samples; i += 2)
        {
            INT16 sample = (INT16)(*srcdata++ * volume / 256);
            *dest++ += sample;
            *dest++ += sample;
        }
    }
    // channels 0,1 are full-rate
    else
    {
        srcdata = &channel->base[channel->offset];
        for (i = 0; i < samples; i++)
            *dest++ += *srcdata++ * volume / 256;
    }
}

// Once per video frame: finish mixing the frame (channels have already been
// mixed up to the last mid-frame register access by mix_channels_to), push the
// completed stereo frame to the chip-stream, then reset the accumulators for
// the next frame.
void exidy440_sound_update(void)
{
    int i;
    int length = stream_buf_frames;

    if (!mixer_buffer_left || !mixer_buffer_right || !stream_out || length <= 0)
        return;

    // Mix whatever remains between the last mid-frame catch-up and frame end.
    mix_channels_to(length);

    // clip to 16-bit and interleave L/R for the stream
    for (i = 0; i < length; i++)
    {
        INT32 l = mixer_buffer_left[i];
        INT32 r = mixer_buffer_right[i];
        if (l < -32768) l = -32768; else if (l > 32767) l = 32767;
        if (r < -32768) r = -32768; else if (r > 32767) r = 32767;
        stream_out[2 * i + 0] = (short)l;
        stream_out[2 * i + 1] = (short)r;
    }

    if (g_stream_ch >= 0)
        stream_update(g_stream_ch, stream_out);

    // start the next frame's accumulation from zero
    mix_pos = 0;
    memset(mixer_buffer_left,  0, sizeof(INT32) * length);
    memset(mixer_buffer_right, 0, sizeof(INT32) * length);
}


// ===========================================================================
//  FIR digital filter (post-CVSD)
// ===========================================================================
static void fir_filter(INT32 *input, INT16 *output, int count)
{
    while (count--)
    {
        INT32 result = (input[-1] - input[-8] - input[-48] + input[-55]) << 2;
        result += (input[0] + input[-18] + input[-38] + input[-56]) << 3;
        result += (-input[-2] - input[-4] + input[-5] + input[-51] - input[-52] - input[-54]) << 4;
        result += (-input[-3] - input[-11] - input[-45] - input[-53]) << 5;
        result += (input[-6] + input[-7] - input[-9] - input[-15] - input[-41] - input[-47] + input[-49] + input[-50]) << 6;
        result += (-input[-10] + input[-12] + input[-13] + input[-14] + input[-21] + input[-35] + input[-42] + input[-43] + input[-44] - input[-46]) << 7;
        result += (-input[-16] - input[-17] + input[-19] + input[-37] - input[-39] - input[-40]) << 8;
        result += (input[-20] - input[-22] - input[-24] + input[-25] + input[-31] - input[-32] - input[-34] + input[-36]) << 9;
        result += (-input[-23] - input[-33]) << 10;
        result += (input[-26] + input[-30]) << 11;
        result += (input[-27] + input[-28] + input[-29]) << 12;
        result >>= 14;

        if (result < -32768)      result = -32768;
        else if (result > 32767)  result = 32767;

        *output++ = (INT16)result;
        input++;
    }
}


// ===========================================================================
//  CVSD decoder
// ===========================================================================
static void decode_and_filter_cvsd(UINT8 *input, int bytes, int maskbits, int frequency, INT16 *output)
{
    INT32 buffer[SAMPLE_BUFFER_LENGTH + FIR_HISTORY_LENGTH];
    int total_samples = bytes * 8;
    int mask = (1 << maskbits) - 1;
    double filter, integrator, leak;
    double charge, decay, gain;
    int steps;
    int chunk_start;

    /* compute the charge, decay, and leak constants */
    charge = pow(exp(-1.0), 1.0 / (FILTER_CHARGE_TC * (double)frequency));
    decay  = pow(exp(-1.0), 1.0 / (FILTER_DECAY_TC  * (double)frequency));
    leak   = pow(exp(-1.0), 1.0 / (INTEGRATOR_LEAK_TC * (double)frequency));

    gain = SAMPLE_GAIN;

    /* clear the history words for a start */
    memset(&buffer[0], 0, FIR_HISTORY_LENGTH * sizeof(INT32));

    /* initialize the CVSD decoder */
    steps = 0xaa;
    filter = FILTER_MIN;
    integrator = 0.0;

    /* loop over chunks */
    for (chunk_start = 0; chunk_start < total_samples; chunk_start += SAMPLE_BUFFER_LENGTH)
    {
        INT32 *bufptr = &buffer[FIR_HISTORY_LENGTH];
        int chunk_bytes;
        int ind;

        if (chunk_start + SAMPLE_BUFFER_LENGTH > total_samples)
            chunk_bytes = (total_samples - chunk_start) / 8;
        else
            chunk_bytes = SAMPLE_BUFFER_LENGTH / 8;

        for (ind = 0; ind < chunk_bytes; ind++)
        {
            double temp;
            int databyte = *input++;
            int bit;
            int sample;

            for (bit = 0; bit < 8; bit++)
            {
                /* move the estimator up or down a step based on the bit */
                if (databyte & (1 << bit))
                {
                    integrator += filter;
                    steps = (steps << 1) | 1;
                }
                else
                {
                    integrator -= filter;
                    steps <<= 1;
                }

                /* keep track of the last n bits */
                steps &= mask;

                /* simulate leakage */
                integrator *= leak;

                /* if we got all 0s or all 1s in the last n bits, bump the step up */
                if (steps == 0 || steps == mask)
                {
                    filter = FILTER_MAX - ((FILTER_MAX - filter) * charge);
                    if (filter > FILTER_MAX)
                        filter = FILTER_MAX;
                }
                /* simulate decay */
                else
                {
                    filter *= decay;
                    if (filter < FILTER_MIN)
                        filter = FILTER_MIN;
                }

                /* compute the sample as a 32-bit word */
                temp = integrator * gain;

                /* compress the sample range to fit better in a 16-bit word */
                if (temp < 0)
                    sample = (int)(temp / (-temp * (1.0 / 32768.0) + 1.0));
                else
                    sample = (int)(temp / (temp * (1.0 / 32768.0) + 1.0));

                *bufptr++ = sample;
            }
        }

        /* run the filter on this chunk */
        fir_filter(&buffer[FIR_HISTORY_LENGTH], &output[chunk_start], chunk_bytes * 8);

        /* copy the last few input samples down to the start for a new history */
        memcpy(&buffer[0], &buffer[SAMPLE_BUFFER_LENGTH], FIR_HISTORY_LENGTH * sizeof(INT32));
    }

    /* make sure the volume goes smoothly to 0 over the last 512 samples */
    if (FADE_TO_ZERO)
    {
        INT16 *data;

        chunk_start = (total_samples > 512) ? total_samples - 512 : 0;
        data = output + chunk_start;
        for ( ; chunk_start < total_samples; chunk_start++)
        {
            *data = (INT16)(*data * ((total_samples - chunk_start) >> 9));
            data++;
        }
    }
}
