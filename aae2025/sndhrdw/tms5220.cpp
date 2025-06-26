/**********************************************************************************************

     TMS5220 Simulator & Interface

     Written for MAME by Frank Palazzolo
     With help from Neill Corlett
     Additional tweaking by Aaron Giles
     Modernization and integration by ChatGPT

***********************************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "log.h"
#include "tms5220.h"
#include "aae_mame_driver.h"
#include "wav_resample.h"

#pragma warning(disable : 4838 4309)

// --------------------------------------------------------
// ROM Tables, Lookup Constants, and Filter Coefficients
// --------------------------------------------------------

const unsigned short energytable[0x10] = {
    0x0000, 0x00C0, 0x0140, 0x01C0, 0x0280, 0x0380, 0x0500, 0x0740,
    0x0A00, 0x0E40, 0x1440, 0x1C80, 0x2840, 0x38C0, 0x5040, 0x7FC0 };

const unsigned short pitchtable[0x40] = {
    0x0000, 0x1000, 0x1100, 0x1200, 0x1300, 0x1400, 0x1500, 0x1600,
    0x1700, 0x1800, 0x1900, 0x1A00, 0x1B00, 0x1C00, 0x1D00, 0x1E00,
    0x1F00, 0x2000, 0x2100, 0x2200, 0x2300, 0x2400, 0x2500, 0x2600,
    0x2700, 0x2800, 0x2900, 0x2A00, 0x2B00, 0x2D00, 0x2F00, 0x3100,
    0x3300, 0x3500, 0x3600, 0x3900, 0x3B00, 0x3D00, 0x3F00, 0x4200,
    0x4500, 0x4700, 0x4900, 0x4D00, 0x4F00, 0x5100, 0x5500, 0x5700,
    0x5C00, 0x5F00, 0x6300, 0x6600, 0x6A00, 0x6E00, 0x7300, 0x7700,
    0x7B00, 0x8000, 0x8500, 0x8A00, 0x8F00, 0x9500, 0x9A00, 0xA000 };

const short k1table[0x20] = {
0x82C0,0x8380,0x83C0,0x8440,0x84C0,0x8540,0x8600,0x8780,
0x8880,0x8980,0x8AC0,0x8C00,0x8D40,0x8F00,0x90C0,0x92C0,
0x9900,0xA140,0xAB80,0xB840,0xC740,0xD8C0,0xEBC0,0x0000,
0x1440,0x2740,0x38C0,0x47C0,0x5480,0x5EC0,0x6700,0x6D40 };

const short k2table[0x20] = {
0xAE00,0xB480,0xBB80,0xC340,0xCB80,0xD440,0xDDC0,0xE780,
0xF180,0xFBC0,0x0600,0x1040,0x1A40,0x2400,0x2D40,0x3600,
0x3E40,0x45C0,0x4CC0,0x5300,0x5880,0x5DC0,0x6240,0x6640,
0x69C0,0x6CC0,0x6F80,0x71C0,0x73C0,0x7580,0x7700,0x7E80 };

const short k3table[0x10] = {
0x9200,0x9F00,0xAD00,0xBA00,0xC800,0xD500,0xE300,0xF000,
0xFE00,0x0B00,0x1900,0x2600,0x3400,0x4100,0x4F00,0x5C00 };

const short k4table[0x10] = {
0xAE00,0xBC00,0xCA00,0xD800,0xE600,0xF400,0x0100,0x0F00,
0x1D00,0x2B00,0x3900,0x4700,0x5500,0x6300,0x7100,0x7E00 };

const short k5table[0x10] = {
0xAE00,0xBA00,0xC500,0xD100,0xDD00,0xE800,0xF400,0xFF00,
0x0B00,0x1700,0x2200,0x2E00,0x3900,0x4500,0x5100,0x5C00 };

const short k6table[0x10] = {
0xC000,0xCB00,0xD600,0xE100,0xEC00,0xF700,0x0300,0x0E00,
0x1900,0x2400,0x2F00,0x3A00,0x4500,0x5000,0x5B00,0x6600 };

const short k7table[0x10] = {
0xB300,0xBF00,0xCB00,0xD700,0xE300,0xEF00,0xFB00,0x0700,
0x1300,0x1F00,0x2B00,0x3700,0x4300,0x4F00,0x5A00,0x6600 };

const short k8table[0x08] = {
0xC000,0xD800,0xF000,0x0700,0x1F00,0x3700,0x4F00,0x6600 };

const short k9table[0x08] = {
0xC000,0xD400,0xE800,0xFC00,0x1000,0x2500,0x3900,0x4D00 };

const short k10table[0x08] = {
0xCD00,0xDF00,0xF100,0x0400,0x1600,0x2000,0x3B00,0x4D00 };

static unsigned char chirptable[41] = {
    0x00, 0x2a, 0xd4, 0x32, 0xb2, 0x12, 0x25, 0x14,
    0x02, 0xe1, 0xc5, 0x02, 0x5f, 0x5a, 0x05, 0x0f,
    0x26, 0xfc, 0xa5, 0xa5, 0xd6, 0xdd, 0xdc, 0xfc,
    0x25, 0x2b, 0x22, 0x21, 0x0f, 0xff, 0xf8, 0xee,
    0xed, 0xef, 0xf7, 0xf6, 0xfa, 0x00, 0x03, 0x02,
    0x01 };

static char interp_coeff[8] = { 8, 8, 8, 4, 4, 2, 2, 1 };

#define FIFO_SIZE 16

static unsigned char fifo[FIFO_SIZE];
static int fifo_head;
static int fifo_tail;
static int fifo_count;
static int bits_taken;

static int speak_external;
static int talk_status;
static int buffer_low;
static int buffer_empty;
static int irq_pin;

static void (*irq_func)(void);

// Voice frame states
static unsigned short old_energy = 0;
static unsigned short old_pitch = 0;
static int old_k[10] = { 0 };

static unsigned short new_energy = 0;
static unsigned short new_pitch = 0;
static int new_k[10] = { 0 };

static unsigned short current_energy = 0;
static unsigned short current_pitch = 0;
static int current_k[10] = { 0 };

static unsigned short target_energy = 0;
static unsigned short target_pitch = 0;
static int target_k[10] = { 0 };

static int interp_count = 0;       // number of interp periods (0-7)
static int sample_count = 0;       // sample number within interp (0-24)
static int pitch_count = 0;

static int u[11] = { 0 };
static int x[10] = { 0 };

static int randbit = 0;

// Static function prototypes
static void process_command(void);
static int extract_bits(int count);
static int parse_frame(int remove);
static void check_buffer_low(void);
static void cause_interrupt(void);

#ifdef DEBUG_5220
static FILE* f;
#endif

// Interface state
#define MIN_SLICE 100

static int sample_pos;
static int emulation_rate;
static int buffer_len;
static short* buffer;

static int stream_buffer_len;
static short* stream_buffer;

static const TMS5220interface* intfa;
static int channel;

// Forward declarations
static void tms5220_update(int force);

// Reset the TMS5220
void tms5220_reset(void)
{
    memset(fifo, 0, sizeof(fifo));
    fifo_head = fifo_tail = fifo_count = bits_taken = 0;

    speak_external = talk_status = buffer_empty = irq_pin = 0;
    buffer_low = 1;

    old_energy = new_energy = current_energy = target_energy = 0;
    old_pitch = new_pitch = current_pitch = target_pitch = 0;
    memset(old_k, 0, sizeof(old_k));
    memset(new_k, 0, sizeof(new_k));
    memset(current_k, 0, sizeof(current_k));
    memset(target_k, 0, sizeof(target_k));

    interp_count = sample_count = pitch_count = randbit = 0;
    memset(u, 0, sizeof(u));
    memset(x, 0, sizeof(x));

#ifdef DEBUG_5220
    f = fopen("tms.log", "w");
#endif
}

void tms5220_set_irq(void (*func)(void))
{
    irq_func = func;
}

void tms5220_data_write(int data)
{
    if (fifo_count < FIFO_SIZE)
    {
        fifo[fifo_tail] = data;
        fifo_tail = (fifo_tail + 1) % FIFO_SIZE;
        fifo_count++;

#ifdef DEBUG_5220
        if (f) fprintf(f, "Added byte to FIFO (size=%2d)\n", fifo_count);
#endif
    }
    else
    {
#ifdef DEBUG_5220
        if (f) fprintf(f, "Ran out of room in the FIFO!\n");
#endif
    }

    check_buffer_low();
}

int tms5220_status_read(void)
{
    irq_pin = 0;

#ifdef DEBUG_5220
    if (f) fprintf(f, "Status read: TS=%d BL=%d BE=%d\n", talk_status, buffer_low, buffer_empty);
#endif

    return (talk_status << 7) | (buffer_low << 6) | (buffer_empty << 5);
}

int tms5220_ready_read(void)
{
    return (fifo_count < FIFO_SIZE - 1);
}

int tms5220_int_read(void)
{
    return irq_pin;
}

void tms5220_process(short* out_buffer, unsigned int size)
{
    int buf_count = 0;
    int i, interp_period;

tryagain:
    while (!speak_external && fifo_count > 0)
        process_command();

    if (!size)
        return;

    if (!speak_external)
        goto empty;

    if (!talk_status)
    {
        if (fifo_count < 9)
            goto empty;

        parse_frame(0);
        talk_status = 1;
        buffer_empty = 0;
    }

    while ((size > 0) && speak_external)
    {
        int current_val;

        if ((interp_count == 0) && (sample_count == 0))
        {
            if (!parse_frame(1))
                break;

            current_energy = old_energy;
            current_pitch = old_pitch;
            for (i = 0; i < 10; i++)
                current_k[i] = old_k[i];

            if (current_energy == 0)
            {
                target_energy = 0;
                target_pitch = current_pitch;
                for (i = 0; i < 10; i++)
                    target_k[i] = current_k[i];
            }
            else if (current_energy == (energytable[15] >> 6))
            {
                current_energy = energytable[0] >> 6;
                target_energy = current_energy;
                speak_external = talk_status = 0;
                interp_count = sample_count = pitch_count = 0;
                cause_interrupt();
                goto tryagain;
            }
            else
            {
                if (new_energy == (energytable[15] >> 6))
                {
                    target_energy = 0;
                    target_pitch = current_pitch;
                    for (i = 0; i < 10; i++)
                        target_k[i] = current_k[i];
                }
                else
                {
                    target_energy = new_energy;
                    target_pitch = new_pitch;

                    for (i = 0; i < 4; i++)
                        target_k[i] = new_k[i];

                    if (current_pitch == 0)
                    {
                        for (i = 4; i < 10; i++)
                            target_k[i] = current_k[i] = 0;
                    }
                    else
                    {
                        for (i = 4; i < 10; i++)
                            target_k[i] = new_k[i];
                    }
                }
            }
        }
        else if (interp_count == 0)
        {
            interp_period = sample_count / 25;
            current_energy += (target_energy - current_energy) / interp_coeff[interp_period];
            if (old_pitch != 0)
                current_pitch += (target_pitch - current_pitch) / interp_coeff[interp_period];
            for (i = 0; i < 10; i++)
                current_k[i] += (target_k[i] - current_k[i]) / interp_coeff[interp_period];
        }

        if (old_energy == 0)
        {
            current_val = 0x00;
        }
        else if (old_pitch == 0)
        {
            randbit = (rand() % 2) * 2 - 1;
            current_val = (randbit * current_energy) / 4;
        }
        else
        {
            if (pitch_count < sizeof(chirptable))
                current_val = (chirptable[pitch_count] * current_energy) / 256;
            else
                current_val = 0x00;
        }

        u[10] = current_val;

        for (i = 9; i >= 0; i--)
            u[i] = u[i + 1] - ((current_k[i] * x[i]) / 32768);

        for (i = 9; i >= 1; i--)
            x[i] = x[i - 1] + ((current_k[i - 1] * u[i - 1]) / 32768);

        x[0] = u[0];

        if (u[0] > 511)
            out_buffer[buf_count] = 127 << 8;
        else if (u[0] < -512)
            out_buffer[buf_count] = -128 << 8;
        else
            out_buffer[buf_count] = u[0] << 6;

        size--;
        sample_count = (sample_count + 1) % 200;

        if (current_pitch != 0)
            pitch_count = (pitch_count + 1) % current_pitch;
        else
            pitch_count = 0;

        interp_count = (interp_count + 1) % 25;
        buf_count++;
    }

empty:
    while (size > 0)
    {
        out_buffer[buf_count++] = 0x00;
        size--;
    }
}

static void process_command(void)
{
    unsigned char cmd;

    if (bits_taken)
    {
        bits_taken = 0;
        fifo_count--;
        fifo_head = (fifo_head + 1) % FIFO_SIZE;
    }

    if (fifo_count > 0)
    {
        cmd = fifo[fifo_head] & 0x70;
        fifo_count--;
        fifo_head = (fifo_head + 1) % FIFO_SIZE;

        if (cmd == 0x60)
        {
            speak_external = 1;

            if (!buffer_empty)
            {
                buffer_empty = 1;
                cause_interrupt();
            }
        }
    }

    check_buffer_low();
}

static int extract_bits(int count)
{
    int val = 0;

    while (count--)
    {
        val = (val << 1) | ((fifo[fifo_head] >> bits_taken) & 1);
        bits_taken++;
        if (bits_taken >= 8)
        {
            fifo_count--;
            fifo_head = (fifo_head + 1) % FIFO_SIZE;
            bits_taken = 0;
        }
    }
    return val;
}

static int parse_frame(int remove)
{
    int old_head, old_taken, old_count;
    int bits, index, i, rep_flag;

    old_energy = new_energy;
    old_pitch = new_pitch;
    for (i = 0; i < 10; i++) old_k[i] = new_k[i];

    new_energy = 0;
    new_pitch = 0;
    for (i = 0; i < 10; i++) new_k[i] = 0;

    if (old_energy == (energytable[15] >> 6))
        return 1;

    old_count = fifo_count;
    old_head = fifo_head;
    old_taken = bits_taken;

    bits = fifo_count * 8 - bits_taken;

    bits -= 4;
    if (bits < 0) goto ranout;
    index = extract_bits(4);
    new_energy = energytable[index] >> 6;

    if (index == 0 || index == 15)
    {
#ifdef DEBUG_5220
        if (f) fprintf(f, "  (4-bit energy=%d frame)\n", new_energy);
#endif
        if (index == 15)
        {
            fifo_head = fifo_tail = fifo_count = bits_taken = 0;
            remove = 1;
        }
        goto done;
    }

    bits -= 1;
    if (bits < 0) goto ranout;
    rep_flag = extract_bits(1);

    bits -= 6;
    if (bits < 0) goto ranout;
    index = extract_bits(6);
    new_pitch = pitchtable[index] / 256;

    if (rep_flag)
    {
        for (i = 0; i < 10; i++)
            new_k[i] = old_k[i];
#ifdef DEBUG_5220
        if (f) fprintf(f, "  (11-bit energy=%d pitch=%d rep=%d frame)\n", new_energy, new_pitch, rep_flag);
#endif
        goto done;
    }

    if (index == 0)
    {
        bits -= 18;
        if (bits < 0) goto ranout;
        new_k[0] = k1table[extract_bits(5)];
        new_k[1] = k2table[extract_bits(5)];
        new_k[2] = k3table[extract_bits(4)];
        new_k[3] = k4table[extract_bits(4)];
#ifdef DEBUG_5220
        if (f) fprintf(f, "  (29-bit energy=%d pitch=%d rep=%d 4K frame)\n", new_energy, new_pitch, rep_flag);
#endif
        goto done;
    }

    bits -= 39;
    if (bits < 0) goto ranout;
    new_k[0] = k1table[extract_bits(5)];
    new_k[1] = k2table[extract_bits(5)];
    new_k[2] = k3table[extract_bits(4)];
    new_k[3] = k4table[extract_bits(4)];
    new_k[4] = k5table[extract_bits(4)];
    new_k[5] = k6table[extract_bits(4)];
    new_k[6] = k7table[extract_bits(4)];
    new_k[7] = k8table[extract_bits(3)];
    new_k[8] = k9table[extract_bits(3)];
    new_k[9] = k10table[extract_bits(3)];
#ifdef DEBUG_5220
    if (f) fprintf(f, "  (50-bit energy=%d pitch=%d rep=%d 10K frame)\n", new_energy, new_pitch, rep_flag);
#endif

done:
#ifdef DEBUG_5220
    if (f) fprintf(f, "Parsed a frame successfully - %d bits remaining\n", bits);
#endif
    if (!remove)
    {
        fifo_count = old_count;
        fifo_head = old_head;
        bits_taken = old_taken;
    }
    check_buffer_low();
    return 1;

ranout:
#ifdef DEBUG_5220
    if (f) fprintf(f, "Ran out of bits on a parse!\n");
#endif
    buffer_empty = 1;
    talk_status = speak_external = 0;
    fifo_count = fifo_head = fifo_tail = 0;
    cause_interrupt();
    return 0;
}

static void check_buffer_low(void)
{
    if (fifo_count < 8)
    {
        if (!buffer_low)
            cause_interrupt();
        buffer_low = 1;
#ifdef DEBUG_5220
        if (f) fprintf(f, "Buffer low set\n");
#endif
    }
    else
    {
        buffer_low = 0;
#ifdef DEBUG_5220
        if (f) fprintf(f, "Buffer low cleared\n");
#endif
    }
}

static void cause_interrupt(void)
{
    irq_pin = 1;
    if (irq_func) irq_func();
}

// Internal update function for syncing with CPU
static void tms5220_update(int force)
{
    int newpos = cpu_scale_by_cycles(buffer_len, intfa->clock);

    if (newpos - sample_pos < MIN_SLICE && !force)
        return;

    tms5220_process(buffer + sample_pos, newpos - sample_pos);
    sample_pos = newpos;
}

// Start the TMS5220 interface (MAME-style)
int tms5220_sh_start(struct TMS5220interface* iinterface)
{
    intfa = iinterface;

    buffer_len = intfa->clock / 80 / Machine->gamedrv->fps;
    emulation_rate = buffer_len * Machine->gamedrv->fps;
    sample_pos = 0;

    LOG_INFO("Emulation Rate here is %d, buffer len is %d", emulation_rate, buffer_len);

    buffer = (short*)malloc(buffer_len * sizeof(short));
    if (!buffer)
        return 1;

    stream_buffer_len = config.samplerate / Machine->gamedrv->fps;
    stream_buffer = (short*)malloc(stream_buffer_len * sizeof(short));
    if (!stream_buffer)
    {
        LOG_INFO("TMS5220 stream buffer allocation failed");
        free(buffer);
        buffer = nullptr;
        return 1;
    }

    stream_start(2, 2, 16, Machine->gamedrv->fps);

    tms5220_reset();
    tms5220_set_irq(iinterface->irq);

    LOG_INFO("TMS 5220 Init completed");

    channel = 1;
    return 0;
}

// Stop and free buffers
void tms5220_sh_stop(void)
{
    LOG_INFO("Stopping and cleaning up TMS5220 Audio");

    stream_stop(2, 2);
    if (buffer) free(buffer);
    buffer = nullptr;
    if (stream_buffer) free(stream_buffer);
    stream_buffer = nullptr;
}

// Update the sound stream
void tms5220_sh_update(void)
{
    if (sample_pos < buffer_len)
        tms5220_process(buffer + sample_pos, buffer_len - sample_pos);
    sample_pos = 0;

    float resample_ratio = (float)((float)config.samplerate / (float)emulation_rate);
    linear_interpolation_16(buffer, buffer_len, &stream_buffer, &stream_buffer_len, resample_ratio);

    stream_update(2, stream_buffer);
}

// Legacy I/O entrypoints
void tms5220_data_w(int offset, int data)
{
    tms5220_update(0);
    tms5220_data_write(data);
}

int tms5220_status_r(int offset)
{
    tms5220_update(1);
    return tms5220_status_read();
}

int tms5220_ready_r(void)
{
    tms5220_update(0);
    return tms5220_ready_read();
}

int tms5220_int_r(void)
{
    tms5220_update(0);
    return tms5220_int_read();
}
