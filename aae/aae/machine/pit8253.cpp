/*****************************************************************************
 *
 *  Programmable Interval Timer 8253/8254  -  AAE port
 *
 *  Three Independent Timers (gate, clock, out pins). 8254 adds the read-back
 *  feature. Adapted from M.A.M.E.(TM) 0.109 src/machine/pit8253.c (Peter
 *  Trauner / Nathan Woods / Andrew Jenner). See pit8253.h for attribution.
 *
 *  Port notes (host-integration layer only - counter behaviour is verbatim):
 *    - MAME mame_timer (alloc/adjust/reset) -> a tiny self-contained shim over
 *      AAE timer_pulse(). AAE one-shot timers are auto-removed by timer_update()
 *      after they fire, and the removal is decided BEFORE the callback runs, so
 *      a timer that re-arms itself from inside its own callback (which the PIT's
 *      OUT timer does, via simulate2) cannot survive if re-armed by id. The shim
 *      therefore creates a FRESH one-shot on every arm instead of reusing an id.
 *    - mame_timer_get_time() -> a single free-running monotonic clock created
 *      with timer_set_elapsed() (frame-boundary safe, never auto-reset), giving
 *      the elapsed-time source the lazy update() needs for accurate reads.
 *    - mame_time arithmetic -> plain double seconds.
 *    - auto_malloc -> a fixed static pits[] array.
 *    - state_save_register_item -> dropped (AAE has no save-state layer).
 *    - READ8_HANDLER / READ16_HANDLER -> AAE MemoryReadByte/Word handlers.
 *    - logerror via LOG1/LOG2 -> AAE async logger (no-ops at VERBOSE 0).
 *
 *****************************************************************************/

#include "pit8253.h"
#include "timer.h"
#include "sys_log.h"
#include <cstdint>
#include <cstring>
#include <functional>

#ifdef _MSC_VER
#pragma warning( disable : 4244 )   // double->int / int64 truncation (as upstream)
#endif

/***************************************************************************

    Structures & macros

***************************************************************************/

#define MAX_TIMER       3
#define MAX_PIT         4          /* upper bound on PIT chips (was auto_malloc'd) */
#define VERBOSE         1   /* DEBUG: 1 = log control/count writes + ch0 OUT scheduling */

#if (VERBOSE == 2)
#define LOG1(msg)       LOG_INFO msg
#define LOG2(msg)       LOG_INFO msg
#elif (VERBOSE == 1)
#define LOG1(msg)       LOG_INFO msg
#define LOG2(msg)       (void)(0)
#else
#define LOG1(msg)       (void)(0)
#define LOG2(msg)       (void)(0)
#endif

#define CYCLES_NEVER    ((UINT32) -1)

/* The PIT lives in CPU 0's timing domain (the 68000 on Vertigo). All scheduled
   callbacks and the monotonic clock are driven by timer_update(.,PIT_CLOCK_CPU). */
#define PIT_CLOCK_CPU   0

typedef uint64_t u64;
typedef int64_t  s64;

struct pit8253_timer
{
    double clockin;                 /* input clock frequency in Hz */

    void (*output_callback)(int);   /* callback for when output changes */
    void (*freq_callback)(double);  /* callback for when output frequency changes */

    double last_updated;            /* emulated time (seconds) when last updated */

    int outputtimer;                /* shim handle for the output-change callback */
    int freqtimer;                  /* shim handle for the frequency-change callback */

    UINT16 value;                   /* current counter value ("CE" in Intel docs) */
    UINT16 latch;                   /* latched counter value ("OL" in Intel docs) */
    UINT16 count;                   /* new counter value ("CR" in Intel docs) */
    UINT8 control;                  /* 6-bit control byte */
    UINT8 status;                   /* status byte - 8254 only */
    UINT8 lowcount;                 /* LSB of new counter value for 16-bit writes */
    INT32 rmsb;                     /* 1 = Next read is MSB of 16-bit value */
    INT32 wmsb;                     /* 1 = Next write is MSB of 16-bit value */
    INT32 output;                   /* 0 = low, 1 = high */

    INT32 gate;                     /* gate input (0 = low, 1 = high) */
    INT32 latched_count;            /* number of bytes of count latched */
    INT32 latched_status;           /* 1 = status latched (8254 only) */
    INT32 null_count;               /* 1 = mode control or count written, 0 = count loaded */
    INT32 phase;                    /* see phase definition tables in simulate2(), below */

    UINT32 cycles_to_output;        /* cycles until output callback called */
    UINT32 cycles_to_freq;          /* cycles until frequency callback called */
    UINT32 freq_count;              /* counter period for periodic modes, 0 if non-periodic */
};

struct pit8253
{
    const struct pit8253_config *config;
    struct pit8253_timer timers[MAX_TIMER];
};

#define CTRL_ACCESS(control)        (((control) >> 4) & 0x03)
#define CTRL_MODE(control)          (((control) >> 1) & (((control) & 0x04) ? 0x03 : 0x07))
#define CTRL_BCD(control)           (((control) >> 0) & 0x01)


static int pit_count;
static struct pit8253 pits[MAX_PIT];



/***************************************************************************

    Functions

***************************************************************************/

static struct pit8253 *get_pit(int which)
{
    return &pits[which];
}


static struct pit8253_timer *get_timer(struct pit8253 *pit, int which)
{
    which &= 3;
    if (which < MAX_TIMER)
        return &pit->timers[which];
    return NULL;
}


static UINT32 decimal_from_bcd(UINT16 val)
{
    /* In BCD mode, a nybble loaded with value A-F counts down the same as in
       binary mode, but wraps around to 9 instead of F after 0, so loading the
       count register with 0xFFFF gives a period of
              0xF  - for the units to count down to 0
       +   10*0xF  - for the tens to count down to 0
       +  100*0xF  - for the hundreds to count down to 0
       + 1000*0xF  - for the thousands to count down to 0
       = 16665 cycles
    */
    return
        ((val >> 12) & 0xF) * 1000 +
        ((val >>  8) & 0xF) *  100 +
        ((val >>  4) & 0xF) *   10 +
        ( val        & 0xF);
}


static UINT32 adjusted_count(int bcd, UINT16 val)
{
    if (bcd == 0)
        return val == 0 ? 0x10000 : val;
    return val == 0 ? 10000 : decimal_from_bcd(val);
}


/* This function subtracts 1 from timer->value "cycles" times, taking into
   account binary or BCD operation, and wrapping around from 0 to 0xFFFF or
   0x9999 as necessary. */
static void decrease_counter_value(struct pit8253_timer *timer, u64 cycles)
{
    UINT16 value;
    int units, tens, hundreds, thousands;

    if (CTRL_BCD(timer->control) == 0)
    {
        timer->value -= (cycles & 0xFFFF);
        return;
    }

    value = timer->value;
    units     =  value        & 0xF;
    tens      = (value >>  4)  & 0xF;
    hundreds  = (value >>  8)  & 0xF;
    thousands = (value >> 12)  & 0xF;

    if (cycles <= (u64)units)
    {
        units -= cycles;
    }
    else
    {
        cycles -= units;
        units = (10 - cycles % 10) % 10;

        cycles = (cycles + 9) / 10; /* the +9 is so we get a carry if cycles%10 wasn't 0 */
        if (cycles <= (u64)tens)
        {
            tens -= cycles;
        }
        else
        {
            cycles -= tens;
            tens = (10 - cycles % 10) % 10;

            cycles = (cycles + 9) / 10;
            if (cycles <= (u64)hundreds)
            {
                hundreds -= cycles;
            }
            else
            {
                cycles -= hundreds;
                hundreds = (10 - cycles % 10) % 10;
                cycles = (cycles + 9) / 10;
                thousands = (10 + thousands - cycles % 10) % 10;
            }
        }
    }

    timer->value = (thousands << 12) | (hundreds << 8) | (tens << 4) | units;
}


static double get_frequency(struct pit8253_timer *timer)
{
    LOG2(("pit8253: get_frequency() : %lf\n", (double)(timer->freq_count == 0 ? 0 : timer->clockin / timer->freq_count)));
    return timer->freq_count == 0 ? 0 : timer->clockin / timer->freq_count;
}


/* =========================================================================
   mame_timer compatibility shim (self-contained; timer.cpp is untouched)

   See the file header for why we cannot re-arm an AAE one-shot by id. Each
   arm creates a fresh one-shot via timer_pulse(); the callback wrapper clears
   'cur' before invoking the user callback, so an in-callback re-arm starts
   clean and never tries to cancel the one that is currently firing. The firing
   one-shot's slot is still occupied while we allocate the replacement, so the
   new timer can never land on the slot timer_update() is mid-processing.
   ========================================================================= */
struct pit_sched
{
    std::function<void(int)> cb;    /* outputcallback / freqcallback */
    int  param;                     /* last param, reused by pit_timer_reset() */
    int  cur;                       /* current AAE one-shot id, or -1 */
    bool used;
};

#define PIT_SCHED_MAX  (MAX_PIT * MAX_TIMER * 2)   /* OUT + freq per counter */
static struct pit_sched pit_scheds[PIT_SCHED_MAX];

static int pit_clock_timer = -1;    /* free-running monotonic clock (seconds source) */

/* Optional sub-cycle hook. AAE's timer clock only advances at CPU time-slice
   boundaries (timer_update), so timer_timeelapsed() is frozen *within* a slice.
   A driver can install a callback returning the host CPU's consumed cycles since
   the last timer_update (PIT_CLOCK_CPU's domain); pit_get_time() folds that in so
   reads taken in a tight loop within one slice - e.g. a game polling a counter as
   an RNG - see the count advancing instead of a constant. Null => coarse (legacy)
   behaviour, identical to before. */
static int (*pit_subcycle_cb)(void) = NULL;

void pit8253_set_subcycle_source(int (*cb)(void))
{
    pit_subcycle_cb = cb;
}


static double pit_get_time(void)
{
    double t = (pit_clock_timer >= 0) ? timer_timeelapsed(pit_clock_timer) : 0.0;
    if (pit_subcycle_cb != NULL)
        t += TIME_IN_CYCLES(pit_subcycle_cb(), PIT_CLOCK_CPU);
    return t;
}


static int pit_timer_alloc(std::function<void(int)> cb)
{
    for (int i = 0; i < PIT_SCHED_MAX; ++i)
    {
        if (!pit_scheds[i].used)
        {
            pit_scheds[i].used  = true;
            pit_scheds[i].cb    = std::move(cb);
            pit_scheds[i].param = 0;
            pit_scheds[i].cur   = -1;
            return i;
        }
    }
    return -1;
}


/* Arm (seconds in (0,TIME_NEVER)) or cancel (TIME_NEVER) the scheduled callback. */
static void pit_timer_arm(int handle, double seconds, int param)
{
    if (handle < 0)
        return;

    struct pit_sched *s = &pit_scheds[handle];
    s->param = param;

    int fresh = -1;
    if (seconds > 0.0 && seconds < TIME_NEVER && s->cb)
    {
        int h = handle;
        fresh = timer_pulse(seconds, PIT_CLOCK_CPU, param,
            [h](int p)
            {
                pit_scheds[h].cur = -1;   /* this shot is firing; re-arm starts clean */
                if (pit_scheds[h].cb)
                    pit_scheds[h].cb(p);
            });
    }

    /* Cancel a still-pending previous shot. Inside the firing callback 'cur'
       was already cleared to -1 by the wrapper, so this only triggers when we
       re-arm from outside (e.g. a CPU write changing the count early). */
    if (s->cur >= 0 && s->cur != fresh && timer_is_timer_enabled(s->cur))
        timer_remove(s->cur);

    s->cur = fresh;

    // DEBUG: trace ch0's OUT scheduling chain (param 0 == pit0 / timer0 -> IRQ4).
    // sec >= TIME_NEVER means the PIT decided OUT won't transition again (cancel).
    if (param == 0)
        LOG1(("pit ch0 OUT (re)arm: sec=%.6f id=%d", seconds, fresh));
}


/* mame_timer_adjust(): the pit only ever passes period == 0 here. */
static void pit_timer_adjust(int handle, double seconds, int param, double /*period*/)
{
    pit_timer_arm(handle, seconds, param);
}


/* mame_timer_reset(): re-arm/cancel reusing the stored param. */
static void pit_timer_reset(int handle, double seconds)
{
    if (handle < 0)
        return;
    pit_timer_arm(handle, seconds, pit_scheds[handle].param);
}


/* Call the frequency callback in "cycles" cycles */
static void freq_callback_in(struct pit8253_timer *timer, UINT32 cycles)
{
    LOG2(("pit8253: freq_callback_in(): %d cycles\n", cycles));

    if (timer->freq_callback == NULL)
    {
        return;
    }

    if (timer->clockin == 0 || cycles == CYCLES_NEVER)
    {
        pit_timer_reset(timer->freqtimer, TIME_NEVER);
    }
    else
    {
        pit_timer_reset(timer->freqtimer, cycles / timer->clockin);
    }
    timer->cycles_to_freq = cycles;
}


static void set_freq_count(struct pit8253_timer *timer)
{
    int mode = CTRL_MODE(timer->control);
    UINT32 freq_count;

    if ((mode == 2 || mode == 3) && timer->gate != 0 && timer->phase != 0)
    {
        freq_count = adjusted_count(CTRL_BCD(timer->control), timer->count);
    }
    else
    {
        freq_count = 0;
    }

    if (freq_count != timer->freq_count)
    {
        timer->freq_count = freq_count;
        if (timer->freq_callback != NULL)
        {
            timer->freq_callback(get_frequency(timer));
            freq_callback_in(timer, CYCLES_NEVER);
        }
    }

    LOG2(("pit8253: set_freq_count() : %d\n", freq_count));
}


/* Call the output callback in "cycles" cycles */
static void trigger_countdown(struct pit8253_timer *timer)
{
    LOG2(("pit8253: trigger_countdown()\n"));

    timer->phase = 1;
    timer->value = timer->count;
    if (CTRL_MODE(timer->control) == 3 && timer->output == 0)
        timer->value &= 0xfffe;

    set_freq_count(timer);
}


static void set_output(struct pit8253_timer *timer, int output)
{
    if (output != timer->output)
    {
        timer->output = output;
        if (timer->output_callback != NULL)
        {
            timer->output_callback(output);
        }
    }
}


/* This emulates timer "timer" for "elapsed_cycles" cycles and assumes no
   callbacks occur during that time. */
static void simulate2(struct pit8253_timer *timer, u64 elapsed_cycles)
{
    UINT32 adjusted_value;
    int bcd = CTRL_BCD(timer->control);
    int mode = CTRL_MODE(timer->control);
    int cycles_to_output = 0;

    if (timer->cycles_to_freq != CYCLES_NEVER)
    {
        timer->cycles_to_freq -= elapsed_cycles;
    }

    LOG2(("pit8253: simulate2(): simulating %d cycles in mode %d, bcd = %d, phase = %d, gate = %d, value = 0x%04x\n",
          (int)elapsed_cycles, mode, bcd, timer->phase, timer->gate, timer->value));

    switch (mode) {
    case 0:
        /* Mode 0: (Interrupt on Terminal Count)

                  +------------------
                  |
        ----------+
          <- n+1 ->

          ^
          +- counter load

        phase|output|length  |value|next|comment
        -----+------+--------+-----+----+----------------------------------
            0|low   |infinity|     |1   |waiting for count
            1|low   |1       |     |2   |internal delay when counter loaded
            2|low   |n       |n..1 |3   |counting down
            3|high  |infinity|0..1 |3   |counting down

        Gate level sensitive only. Low disables counting, high enables it. */

        if (timer->gate == 0 || timer->phase == 0)
        {
            cycles_to_output = CYCLES_NEVER;
        }
        else
        {
            if (elapsed_cycles > 0 && timer->phase == 1)
            {
                --elapsed_cycles;
                timer->phase = 2;
            }

            if (timer->phase == 2)
            {
                adjusted_value = adjusted_count(bcd, timer->value);
                if (elapsed_cycles < adjusted_value)
                {
                    /* Counter didn't wrap */
                    decrease_counter_value(timer, elapsed_cycles);
                }
                else
                {
                    /* Counter wrapped, output goes high */
                    elapsed_cycles -= adjusted_value;
                    timer->phase = 3;
                    timer->value = 0;
                }
            }

            if (timer->phase == 3)
            {
                decrease_counter_value(timer, elapsed_cycles);
                cycles_to_output = CYCLES_NEVER;
            }
            else
            {
                cycles_to_output = adjusted_count(bcd, timer->value) + (timer->phase == 1 ? 1 : 0);
            }
        }

        set_output(timer, timer->phase == 3 ? 1 : 0);
        break;


    case 1:
        /* Mode 1: (Hardware Retriggerable One-Shot a.k.a. Programmable One-Shot)

        --+       +------------------
          |       |
          +-------+
          <-  n  ->

          ^
          +- trigger

        phase|output|length  |value|next|comment
        -----+------+--------+-----+----+----------------------------------
            0|high  |infinity|0..1 |1   |counting down
            1|low   |n       |n..1 |0   |counting down

        Gate rising-edge sensitive only.
        Rising edge initiates counting and resets output after next clock. */

        adjusted_value = adjusted_count(bcd, timer->value);
        if (elapsed_cycles < adjusted_value)
        {
            /* Counter didn't wrap */
            decrease_counter_value(timer, elapsed_cycles);
            cycles_to_output = (timer->phase == 0 ? CYCLES_NEVER : adjusted_count(bcd, timer->value));
        }
        else
        {
            /* Counter wrapped, output goes high */
            elapsed_cycles -= adjusted_value;
            timer->phase = 0;
            timer->value = 0;
            decrease_counter_value(timer, elapsed_cycles);
            cycles_to_output = CYCLES_NEVER;
        }
        set_output(timer, timer->phase == 0 ? 1 : 0);
        break;


    case 2:
        /* Mode 2: (Rate Generator)

        --------------+ +---------+ +----
                      | |         | |
                      +-+         +-+
            <-    n    -X-    n    ->
                      <1>
            ^
            +- counter load or trigger

        phase|output|length  |value|next|comment
        -----+------+--------+-----+----+----------------------------------
            0|high  |infinity|     |1   |waiting for count
            1|v!=1  |n       |n..1 |1   |counting down

        Counter rewrite has no effect until repeated

        Gate rising-edge and level sensitive.
        Gate low disables counting and sets output immediately high.
        Rising-edge reloads count and initiates counting
        Gate high enables counting. */

        if (timer->gate == 0 || timer->phase == 0)
        {
            /* Gate low or mode control write forces output high */
            set_output(timer, 1);
            cycles_to_output = CYCLES_NEVER;
        }
        else
        {
            adjusted_value = adjusted_count(bcd, timer->value);
            if (elapsed_cycles < adjusted_value)
            {
                /* Counter didn't wrap */
                decrease_counter_value(timer, elapsed_cycles);
            }
            else
            {
                /* Counter wrapped around one or more times */
                elapsed_cycles -= adjusted_value;
                trigger_countdown(timer);
                decrease_counter_value(timer, elapsed_cycles % adjusted_count(bcd, timer->count));
            }
            cycles_to_output = (timer->value == 1 ? 1 : (adjusted_count(bcd, timer->value) - 1));

            set_output(timer, timer->value != 1 ? 1 : 0);
        }
        break;


    case 3:
        /* Mode 3: (Square Wave Generator)

        ----------------+           +-----------+           +----
                        |           |           |           |
                        +-----------+           +-----------+
            <- (n+1)/2 -X-   n/2   ->
            ^
            +- counter load or trigger

        phase|output|length  |value|next|comment
        -----+------+--------+-----+----+----------------------------------
            0|high  |infinity|     |1   |waiting for count
            1|      |infinity|n..0 |1   |counting down double speed

        Counter rewrite has no effect until repeated (output falling or rising)

        Gate rising-edge and level sensitive.
        Gate low disables counting and sets output immediately high.
        Rising-edge reloads count and initiates counting
        Gate high enables counting. */

        if (timer->gate == 0 || timer->phase == 0)
        {
            /* Gate low or mode control write forces output high */
            set_output(timer, 1);
            cycles_to_output = CYCLES_NEVER;
        }
        else
        {
            adjusted_value = adjusted_count(bcd, timer->value);
            if ((elapsed_cycles << 1) < adjusted_value)
            {
                /* Counter didn't wrap around */
                decrease_counter_value(timer, elapsed_cycles << 1);
            }
            else
            {
                /* Counter wrapped around one or more times */
                elapsed_cycles -= ((adjusted_value + 1) >> 1);

                set_output(timer, 1 - timer->output);
                trigger_countdown(timer);

                elapsed_cycles %= adjusted_count(bcd, timer->count);
                adjusted_value = adjusted_count(bcd, timer->value);
                if ((elapsed_cycles << 1) >= adjusted_value)
                {
                    /* Counter wrapped around an even number of times */
                    elapsed_cycles -= ((adjusted_value + 1) >> 1);

                    set_output(timer, 1 - timer->output);
                    trigger_countdown(timer);
                }
                decrease_counter_value(timer, elapsed_cycles << 1);
            }
            cycles_to_output = (adjusted_count(bcd, timer->value) + 1) >> 1;
        }
        break;


    case 4:
    case 5:
        /* Mode 4: (Software Trigger Strobe)
           Mode 5: (Hardware Trigger Strobe)

        --------------+ +--------------------
                      | |
                      +-+
            <-  n+1  ->
            ^         <1>
            +- counter load (mode 4) or trigger (mode 5)

        phase|output|length  |value|next|comment
        -----+------+--------+-----+----+----------------------------------
            0|high  |infinity|0..1 |0   |waiting for count
            1|high  |1       |     |2   |internal delay when counter loaded
            2|high  |n       |n..1 |3   |counting down
            3|low   |1       |0    |0   |strobe

        Mode 4 only: counter rewrite loads new counter
        Mode 5 only: count not reloaded immediately.
        Mode control write doesn't stop count but sets output high

        Mode 4 only: Gate level sensitive only. Low disables counting, high enables it.
        Mode 5 only: Gate rising-edge sensitive only. Rising edge initiates counting */

        if (timer->gate == 0 && mode == 4)
        {
            cycles_to_output = CYCLES_NEVER;
        }
        else
        {
            if (elapsed_cycles > 0 && timer->phase == 1)
            {
                --elapsed_cycles;
                timer->phase = 2;
            }

            if (elapsed_cycles > 0 && timer->phase == 3)
            {
                --elapsed_cycles;
                timer->phase = 0;
                decrease_counter_value(timer, 1);
            }

            if (timer->value == 0 && timer->phase == 2)
                adjusted_value = 0;
            else
                adjusted_value = adjusted_count(bcd, timer->value);

            if (elapsed_cycles < adjusted_value)
            {
                /* Counter didn't wrap */
                decrease_counter_value(timer, elapsed_cycles);
            }
            else
            {
                elapsed_cycles -= adjusted_value;
                timer->value = 0;
                if (elapsed_cycles == 0)
                {
                    /* We hit the strobe cycle */
                    timer->phase = 3;
                }
                else
                {
                    decrease_counter_value(timer, elapsed_cycles);
                    timer->phase = 0;
                }
            }
            switch (timer->phase) {
            case 0:
                cycles_to_output = CYCLES_NEVER;
                break;
            case 1:
                cycles_to_output = adjusted_count(bcd, timer->value) + 1;
                break;
            case 2:
                cycles_to_output = adjusted_count(bcd, timer->value);
                break;
            case 3:
                cycles_to_output = 1;
                break;
            }
        }
        set_output(timer, timer->phase != 3 ? 1 : 0);
        break;
    }

    if (timer->output_callback != NULL)
    {
        timer->cycles_to_output = cycles_to_output;
        if ((UINT32)cycles_to_output == CYCLES_NEVER || timer->clockin == 0)
            pit_timer_reset(timer->outputtimer, TIME_NEVER);
        else
            pit_timer_reset(timer->outputtimer, cycles_to_output / timer->clockin);
    }

    if (timer->cycles_to_freq == 0)
        timer->cycles_to_freq = CYCLES_NEVER;
}


/* This emulates timer "timer" for "elapsed_cycles" cycles, broken down into
   sections punctuated by callbacks. */
static void simulate(struct pit8253_timer *timer, u64 elapsed_cycles)
{
    while ((timer->cycles_to_output != CYCLES_NEVER &&
            timer->cycles_to_output <= elapsed_cycles) ||
           (timer->cycles_to_freq != CYCLES_NEVER &&
            timer->cycles_to_freq <= elapsed_cycles))
    {
        UINT32 cycles_to_callback;

        if (timer->cycles_to_output < timer->cycles_to_freq &&
            timer->cycles_to_output != CYCLES_NEVER)
        {
            cycles_to_callback = timer->cycles_to_output;
        }
        else
        {
            cycles_to_callback = timer->cycles_to_freq;
        }

        simulate2(timer, cycles_to_callback);
        elapsed_cycles -= cycles_to_callback;
    }
    simulate2(timer, elapsed_cycles);
}


/* This brings timer "timer" up to date */
static void update(struct pit8253_timer *timer)
{
    /* Lazy catch-up: advance the counter by however many cycles have elapsed
       (in this timer's clock domain) since we last looked. */
    double now = pit_get_time();
    double elapsed_time = now - timer->last_updated;
    s64 elapsed_cycles = (s64)(elapsed_time * timer->clockin);

    timer->last_updated += (double)elapsed_cycles / timer->clockin;

    simulate(timer, elapsed_cycles);
}


void pit8253_reset(int which)
{
    struct pit8253 *pit = get_pit(which);
    struct pit8253_timer *timer;
    int i;

    LOG1(("pit8253_reset(): resetting pit %d\n", which));

    for (i = 0; i < MAX_TIMER; i++)
    {
        timer = get_timer(pit, i);
        /* According to Intel's 8254 docs, the state of a timer is undefined
           until the first mode control word is written. Here we define this
           undefined behaviour */
        timer->control = timer->status = 0x30;
        timer->rmsb = timer->wmsb = 0;
        timer->count = timer->value = timer->latch = 0;
        timer->lowcount = 0;
        timer->gate = 1;
        timer->output = 0;
        timer->latched_count = 0;
        timer->latched_status = 0;
        timer->null_count = 1;
        timer->cycles_to_output = timer->cycles_to_freq = CYCLES_NEVER;

        timer->last_updated = pit_get_time();

        update(timer);
    }
}


static void freqcallback(int param)
{
    struct pit8253_timer *timer = get_timer(get_pit(param & 0x0F), (param >> 4) & 0x0F);
    s64 cycles = timer->cycles_to_freq;
    double t;

    LOG2(("pit8253: freqcallback(): pit %d, timer %d, %d cycles\n", param & 0xf, (param >> 4) & 0xf, (UINT32)cycles));

    simulate(timer, cycles);

    t = cycles / timer->clockin;

    timer->last_updated += t;
}


static void outputcallback(int param)
{
    struct pit8253_timer *timer = get_timer(get_pit(param & 0x0F), (param >> 4) & 0x0F);
    s64 cycles = timer->cycles_to_output;
    double t;

    LOG2(("pit8253: outputcallback(): pit %d, timer %d, %d cycles\n", param & 0xf, (param >> 4) & 0xf, (UINT32)cycles));

    simulate(timer, cycles);

    t = cycles / timer->clockin;

    timer->last_updated += t;
}


int pit8253_init(int count, const struct pit8253_config *config)
{
    int i, timerno;
    struct pit8253 *pit;
    struct pit8253_timer *timer;

    LOG2(("pit8253_init(): initializing %d pit(s)\n", count));

    if (count > MAX_PIT)
        count = MAX_PIT;
    pit_count = count;

    memset(pits, 0, sizeof(pits));

    /* Reset the scheduled-callback pool and (re)create the monotonic clock that
       feeds pit_get_time(). */
    for (i = 0; i < PIT_SCHED_MAX; ++i)
    {
        pit_scheds[i].cb    = nullptr;
        pit_scheds[i].param = 0;
        pit_scheds[i].cur   = -1;
        pit_scheds[i].used  = false;
    }
    pit_clock_timer = timer_set_elapsed(PIT_CLOCK_CPU);
    pit_subcycle_cb = NULL;   /* default to coarse; a driver may install one after init */

    for (i = 0; i < count; i++)
    {
        pit = get_pit(i);
        pit->config = &config[i];

        for (timerno = 0; timerno < MAX_TIMER; timerno++)
        {
            timer = get_timer(pit, timerno);

            timer->clockin = pit->config->timer[timerno].clockin;
            timer->output_callback = pit->config->timer[timerno].output_callback;
            timer->freq_callback = pit->config->timer[timerno].clock_callback;

            if (timer->output_callback == NULL)
                timer->outputtimer = -1;
            else
            {
                timer->outputtimer = pit_timer_alloc(outputcallback);
                pit_timer_adjust(timer->outputtimer, TIME_NEVER, i | (timerno << 4), 0);
            }
            if (timer->freq_callback == NULL)
                timer->freqtimer = -1;
            else
            {
                timer->freqtimer = pit_timer_alloc(freqcallback);
                pit_timer_adjust(timer->freqtimer, TIME_NEVER, i | (timerno << 4), 0);
            }

            /* (state_save_register_item removed - AAE has no save-state layer.) */
        }
        pit8253_reset(i);
    }

    LOG1(("pit8253_init(): initialized successfully\n"));

    return 0;
}


/* We recycle bit 0 of timer->value to hold the phase in mode 3 when count is
   odd. Since read commands in mode 3 always return even numbers, we need to
   mask this bit off. */
static UINT16 masked_value(struct pit8253_timer *timer)
{
    LOG2(("pit8253: masked_value\n"));

    if (CTRL_MODE(timer->control) == 3)
        return timer->value & 0xfffe;
    return timer->value;
}

/* Reads only affect the following bits of the counter state:
     latched_status
     latched_count
     rmsb
  so they don't affect any timer operations except other reads. */
UINT8 pit8253_read(int which, UINT32 offset)
{
    struct pit8253 *pit = get_pit(which);
    struct pit8253_timer *timer = get_timer(pit, offset);
    UINT8 data;
    UINT16 value;

    LOG2(("pit8253_read(): pit %d, offset %d\n", which, offset));

    if (timer == NULL)
    {
        /* Reading mode control register is illegal according to docs */
        /* Experimentally determined: reading it returns 0 */
        data = 0;
    }
    else
    {
        update(timer);

        if (timer->latched_status)
        {
            /* Read status register (8254 only) */
            data = timer->status;
            timer->latched_status = 0;
        }
        else
        {
            if (timer->latched_count != 0)
            {
                /* Read back latched count */
                data = (timer->latch >> (timer->rmsb != 0 ? 8 : 0)) & 0xff;
                timer->rmsb = 1 - timer->rmsb;
                --timer->latched_count;
            }
            else {
                value = masked_value(timer);

                /* Read back current count */
                switch (CTRL_ACCESS(timer->control)) {
                case 0:
                default:
                    /* This should never happen */
                    data = 0; /* Appease compiler */
                    break;

                case 1:
                    /* read counter bits 0-7 only */
                    data = (value >> 0) & 0xff;
                    break;

                case 2:
                    /* read counter bits 8-15 only */
                    data = (value >> 8) & 0xff;
                    break;

                case 3:
                    /* read bits 0-7 first, then 8-15 */
                    data = (value >> (timer->rmsb != 0 ? 8 : 0)) & 0xff;
                    timer->rmsb = 1 - timer->rmsb;
                    break;
                }
            }
        }
    }

    LOG2(("pit8253_read(): PIT #%d offset=%d data=0x%02x\n", which, (int)offset, (unsigned)data));
    return data;
}


/* Loads a new value from the bus to the count register (CR) */
static void load_count(struct pit8253_timer *timer, UINT16 newcount)
{
    int mode = CTRL_MODE(timer->control);

    LOG1(("pit8253: load_count(): %04x\n", newcount));

    if (newcount == 1)
    {
        /* Count of 1 is illegal in modes 2 and 3. What happens here was
           determined experimentally. */
        if (mode == 2)
            newcount = 2;
        if (mode == 3)
            newcount = 0;
    }
    timer->count = newcount;
    timer->null_count = 1;
    if (mode == 2 || mode == 3)
    {
        if (timer->phase == 0)
        {
            trigger_countdown(timer);
        }
        else
        {
            int bcd = CTRL_BCD(timer->control);
            if (mode == 2)
            {
                freq_callback_in(timer, adjusted_count(bcd, timer->value));
            }
            else
            {
                freq_callback_in(timer, (adjusted_count(bcd, timer->value) + 1) >> 1);
            }
        }
    }
    else
    {
        if (mode == 0 || mode == 4)
        {
            trigger_countdown(timer);
        }
    }
}


static void readback(struct pit8253_timer *timer, int command)
{
    UINT16 value;
    update(timer);

    if ((command & 1) == 0)
    {
        /* readback status command */
        if (timer->latched_status == 0)
        {
            timer->status = timer->control | (timer->output != 0 ? 0x80 : 0) | (timer->null_count != 0 ? 0x40 : 0);
        }

        timer->latched_status = 1;
    }
    /* Experimentally determined: the read latch command seems to have no
       effect if we're halfway through a 16-bit read */
    if ((command & 2) == 0 && timer->rmsb == 0)
    {
        /* readback count command */

        if (timer->latched_count == 0)
        {
            value = masked_value(timer);
            switch (CTRL_ACCESS(timer->control)) {
            case 0:
                /* This should never happen */
                break;

            case 1:
                /* latch bits 0-7 only */
                timer->latch = ((value << 8) & 0xff00) | (value & 0xff);
                timer->latched_count = 1;
                break;

            case 2:
                /* read bits 8-15 only */
                timer->latch = (value & 0xff00) | ((value >> 8) & 0xff);
                timer->latched_count = 1;
                break;

            case 3:
                /* latch all 16 bits */
                timer->latch = value;
                timer->latched_count = 2;
                break;
            }
        }
    }
}


void pit8253_write(int which, UINT32 offset, int data)
{
    struct pit8253 *pit = get_pit(which);
    struct pit8253_timer *timer = get_timer(pit, offset);
    int read_command;

    LOG2(("pit8253_write(): PIT #%d offset=%d data=0x%02x\n", which, (int)offset, (unsigned)data));

    if (timer == NULL) {
        /* Write to mode control register */
        timer = get_timer(pit, (data >> 6) & 3);
        if (timer == NULL)
        {
            /* Readback command. Illegal on 8253 */
            /* Todo: find out what (if anything) the 8253 hardware actually does here. */
            if (pit->config->type == TYPE8254)
            {
                LOG1(("pit8253_write(): PIT #%d readback %02x\n", which, data & 0x3f));

                /* Bit 0 of data must be 0. Todo: find out what the hardware does if it isn't. */
                read_command = (data >> 4) & 3;
                if ((data & 2) != 0)
                    readback(get_timer(pit, 0), read_command);
                if ((data & 4) != 0)
                    readback(get_timer(pit, 1), read_command);
                if ((data & 8) != 0)
                    readback(get_timer(pit, 2), read_command);
            }
            return;
        }

        update(timer);

        if (CTRL_ACCESS(data) == 0)
        {
            LOG1(("pit8253_write(): PIT #%d timer=%d readback\n", which, (data >> 6) & 3));

            /* Latch current timer value */
            /* Experimentally verified: this command does not affect the mode control register */
            readback(timer, 1);
        }
        else {
            LOG1(("pit8253_write(): PIT #%d timer=%d bytes=%d mode=%d bcd=%d\n", which, (data >> 6) & 3, (data >> 4) & 3, (data >> 1) & 7, data & 1));

            timer->control = (data & 0x3f);
            timer->null_count = 1;
            timer->wmsb = timer->rmsb = 0;
            /* Phase 0 is always the phase after a mode control write */
            timer->phase = 0;
            set_output(timer, 1);
            set_freq_count(timer);
        }
    }
    else
    {
        update(timer);

        switch (CTRL_ACCESS(timer->control)) {
        case 0:
            /* This should never happen */
            break;

        case 1:
            /* read/write counter bits 0-7 only */
            load_count(timer, data);
            break;

        case 2:
            /* read/write counter bits 8-15 only */
            load_count(timer, data << 8);
            break;

        case 3:
            /* read/write bits 0-7 first, then 8-15 */
            if (timer->wmsb != 0)
            {
                load_count(timer, timer->lowcount | (data << 8));
            }
            else
            {
                timer->lowcount = data;
                if (CTRL_MODE(timer->control) == 0)
                {
                    /* The Intel docs say that writing the MSB in mode 0, phase
                       2 won't stop the count, but this was experimentally
                       determined to be false. */
                    timer->phase = 0;
                }
            }
            timer->wmsb = 1 - timer->wmsb;
            break;
        }
    }
    update(timer);
}


void pit8253_gate_write(int which, int offset, int data)
{
    struct pit8253_timer *timer = get_timer(get_pit(which), offset);
    int mode;
    int gate = (data != 0 ? 1 : 0);

    LOG2(("pit8253_gate_write(): PIT #%d offset=%d gate=%d\n", which, (int)offset, (unsigned)data));

    if (timer == NULL)
        return;

    mode = CTRL_MODE(timer->control);

    if (gate != timer->gate)
    {
        update(timer);
        timer->gate = gate;
        set_freq_count(timer);
        if (gate != 0 &&
            (mode == 1 || mode == 5 ||
             (timer->phase == 1 && (mode == 2 || mode == 3))))
        {
            trigger_countdown(timer);
        }
        update(timer);
    }
}



/* ----------------------------------------------------------------------- */

int pit8253_get_frequency(int which, int timerno)
{
    struct pit8253_timer *timer = get_timer(get_pit(which), timerno);

    update(timer);
    return get_frequency(timer);
}



int pit8253_get_output(int which, int timerno)
{
    struct pit8253_timer *timer = get_timer(get_pit(which), timerno);
    int result;

    update(timer);
    result = timer->output;
    LOG2(("pit8253_get_output(): PIT #%d timer=%d result=%d\n", which, timerno, result));
    return result;
}



void pit8253_set_clockin(int which, int timerno, double new_clockin)
{
    struct pit8253_timer *timer = get_timer(get_pit(which), timerno);

    LOG2(("pit8253_set_clockin(): PIT #%d timer=%d, clockin = %lf\n", which, (int)timerno, new_clockin));

    update(timer);
    timer->clockin = new_clockin;
    update(timer);

    if (timer->freq_callback != NULL)
    {
        timer->freq_callback(get_frequency(timer));
        if (timer->cycles_to_freq != CYCLES_NEVER)
        {
            freq_callback_in(timer, timer->cycles_to_freq);
        }
    }
}



/* ----------------------------------------------------------------------- */
/* AAE memory handlers. Byte handlers map one register per byte address; the
   16-bit LSB handlers target a 68000-style word bus (registers word-spaced,
   value in the low byte) and are the equivalent of MAME's *_lsb_r/_lsb_w. */

UINT8 pit8253_0_r(UINT32 offset, struct MemoryReadByte *mem)  { (void)mem; return pit8253_read(0, offset); }
UINT8 pit8253_1_r(UINT32 offset, struct MemoryReadByte *mem)  { (void)mem; return pit8253_read(1, offset); }
void  pit8253_0_w(UINT32 offset, UINT8 data, struct MemoryWriteByte *mem) { (void)mem; pit8253_write(0, offset, data); }
void  pit8253_1_w(UINT32 offset, UINT8 data, struct MemoryWriteByte *mem) { (void)mem; pit8253_write(1, offset, data); }

UINT16 pit8253_0_lsb_r(UINT32 offset, struct MemoryReadWord *mem) { (void)mem; return pit8253_read(0, offset >> 1); }
UINT16 pit8253_1_lsb_r(UINT32 offset, struct MemoryReadWord *mem) { (void)mem; return pit8253_read(1, offset >> 1); }
void   pit8253_0_lsb_w(UINT32 offset, UINT16 data, struct MemoryWriteWord *mem) { (void)mem; pit8253_write(0, offset >> 1, data & 0xff); }
void   pit8253_1_lsb_w(UINT32 offset, UINT16 data, struct MemoryWriteWord *mem) { (void)mem; pit8253_write(1, offset >> 1, data & 0xff); }
