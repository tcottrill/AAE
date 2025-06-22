
#ifndef CPU_TIMER_H
#define CPU_TIMER_H

#include <functional>

#define ONE_SHOT               0x100
#define TIME_IN_HZ(hz)         (1.0 / static_cast<double>(hz))
#define TIME_IN_CYCLES(c,cpu)  ((double)(c) * cycles_to_sec[cpu])
#define TIME_IN_SEC(s)         ((double)(s))
#define TIME_IN_MSEC(ms)       ((double)(ms) * 0.001)
#define TIME_IN_USEC(us)       ((double)(us) * 0.000001)
#define TIME_IN_NSEC(ns)       ((double)(ns) * 0.000000001)

#define TIME_NOW               (0.0)
#define TIME_NEVER             (1.0e30)

#define TIME_TO_CYCLES(cpu,t)  (static_cast<int>((t) * sec_to_cycles[cpu]))

#define MAX_CPU 4

extern double cycles_to_sec[MAX_CPU + 1];
extern double sec_to_cycles[MAX_CPU + 1];

void timer_init();
int  timer_set(double duration, int param, std::function<void(int)> callback);
int  timer_set(double duration, int param, int data, std::function<void(int)> callback);
int  timer_pulse(double duration, int param, std::function<void(int)> callback);
int  timer_pulse(double duration, int param, int data, std::function<void(int)> callback);
void timer_remove(int timer_id);
void timer_reset(int timer_id, double duration);
void timer_update(int cycles, int cpunum);
void timer_cpu_reset(int cpunum);
int  timer_is_timer_enabled(int timer_id);
void timer_clear_all_eof();
void timer_clear_end_of_game();

#endif