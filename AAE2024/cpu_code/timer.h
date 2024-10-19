/***************************************************************************

  timer.c

  Functions needed to generate timing and synchronization between several
  CPUs.

***************************************************************************/

#ifndef __TIMER_H__
#define __TIMER_H__

//#include "stuff.h"
#include "cpu_control.h"

//#ifdef __cplusplus
//extern "C" {
//#endif

extern double cycles_to_sec[];
extern double sec_to_cycles[];

#define ONE_SHOT               0x100
#define TIME_IN_HZ(hz)        (1.0 / (double)(hz))
#define TIME_IN_CYCLES(c,cpu) ((double)(c) * cycles_to_sec[cpu])
#define TIME_IN_SEC(s)        ((double)(s))
#define TIME_IN_MSEC(ms)      ((double)(ms) * (1.0 / 1000.0))
#define TIME_IN_USEC(us)      ((double)(us) * (1.0 / 1000000.0))
#define TIME_IN_NSEC(us)      ((double)(us) * (1.0 / 1000000000.0))

#define TIME_NOW              (0.0)
#define TIME_NEVER            (1.0e30)

#define TIME_TO_CYCLES(cpu,t) ((int)((t) * sec_to_cycles[cpu]))

#define MAX_CPU 4

void timer_init();
int timer_set(double duration, int param, void(*callback)(int));
//This is a repeating timer or one shot depending on param
int timer_set(double duration, int param, int data, void(*callback)(int));
//void timer_reset(void *which, double duration);
void timer_remove(int num);
//int  timer_enable(void *which, int enable);
void timer_update(int cycles, int cpunum);
//Resets the timer count to 0 for the specified timer.
//This is for the watchdog.
void timer_reset(int timer_number, double duration);
//Call a one shot timer explicitly
int timer_pulse(double duration, int param, void(*callback)(int));
int timer_pulse(double duration, int param, int data, void(*callback)(int));
void timer_cpu_reset(int cpunum);
void timer_clear_all_eof();

#endif
