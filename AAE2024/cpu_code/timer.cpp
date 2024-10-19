#include "aae_mame_driver.h"
#include <string.h>
#include "timer.h"
#include "log.h"

/*
This is a simplified version of the mame cpu timers. It's sufficent for my needs.
Timers are based off of cpu cycles, and can be tied to any cpu or audio cpu.
*/

//
// *Note to self, since this is called hundreds of times per frame, please add a fast linked list to this like in older MAME.
// This is taking up way to much valuable CPU time for no reason.
//
//
//

double cycles_to_sec[MAX_CPU + 1];
double sec_to_cycles[MAX_CPU + 1];

//We are starting all timers at 1!!!!
// All timers must be positive!

#define VERBOSE 1
#define MAX_TIMERS 8

typedef struct timer_entry
{
	void(*callback)(int);
	int callback_param;
	int enabled;
	double period;
	double count;
	double expire;
	int cpu;
} timer_entry;

timer_entry timer[MAX_TIMERS];

void timer_remove(int timer_num)
{
	if (VERBOSE) {
		wrlog("timer %d removed", timer_num);
	}
	timer[timer_num].cpu = 0;
	timer[timer_num].period = 0;
	timer[timer_num].count = 0;
	timer[timer_num].enabled = 0;
	timer[timer_num].expire = 0;
	timer[timer_num].callback = 0;
	timer[timer_num].callback_param = 0;
}

void timer_init(void)
{
				
	for (int x = 0; x < MAX_TIMERS; x++)
	{
		timer_remove(x);
	}
}

// Remember param here is actually just the cpu # to tie the timer too. It's overloaded with ONE_SHOT to denote
// a non-reoccuring timer. I did this to keep compatibility for troubleshooting against mame, but I may change this
// later. I don't have any drivers that need a value passed to the callback so it's ok for now.
// I don't know how mame determines which cpu to count a timer with and I don't really care.
// This is working fine for me for what I'm doing.

int timer_set(double duration, int param, int data, void (*callback)(int))
{
	int check = 0;
	int x;
	//Look for an unused timer, timers start at 1!
	for (x = 1; x < MAX_TIMERS; x++)
	{
		if (timer[x].enabled == 0)
		{
			timer[x].cpu = (int8_t)param & 0x0f;
			//timer[x].period = (Machine->drv->cpu[(int8_t)param & 0x0f].cpu_clock * duration); 
			timer[x].period = driver[gamenum].cpu_freq[(int8_t)param & 0x0f] * duration;

			if (VERBOSE) { wrlog("Timer %d duration %f cpuclock %d", x, timer[x].period, driver[gamenum].cpu_freq[(int8_t)param & 0x0f]); }
			timer[x].enabled = 1;
			// If it's a one-shot timer, make sure to set that.
			if (param > 0xff)
			{
				// Check that we're not setting a one shot timer (usually draw time) greater then the cycles
				// left in the current frame. This fixes tempest and other vector games.
				// This isn't how tempest works in the real world, but we need a frame to end somewhere.
				// This should be cycles reemaining??? Like, get_cycles_remaining();
				//int cycles = (Machine->drv->cpu[timer[x].cpu].cpu_clock / Machine->drv->frames_per_second);
				int cycles = (driver[gamenum].cpu_freq [timer[x].cpu] / driver[gamenum].fps);
				if (timer[x].period > cycles)
				{
					timer[x].period = cycles; // 20 picked randomly!
					if (VERBOSE) 
					{
						wrlog("New timer with Cycles at %f on cpu: %d out of total cycles: %d ", timer[x].period, timer[x].cpu,cycles); 
					}
				}
				timer[x].expire = 1;
				if (VERBOSE) { wrlog("Previous timer was a oneshot."); }
			}
			timer[x].count = 0; //Randomize the start a little bit so two timers don't fire at the same time?
			timer[x].callback = callback;
			timer[x].callback_param = data;
			check = 1;
			return x;
		}
	}
	if (check == 0) { wrlog("------ERROR!!! NO FREE TIMERS FOUND, SOMETHING IS SERIOUSLY WRONG!!--------"); }
	//Return timer number so you can keep it for deletion later.
	return 0;
}

void timer_update(int cycles, int cpunum)
{
	int x;

	
	for (x = 0; x < MAX_TIMERS; x++)
	{
		if (timer[x].enabled)
		{
			//Only do stuff if it's enabled and timed from this cpu, duh.
			if (timer[x].cpu == cpunum)
			{
				timer[x].count += cycles;
				 wrlog("Timer %d update to %f cycles", x,timer[x].count);

				if (timer[x].count >= timer[x].period)
				{
					if (VERBOSE) { wrlog("Timer %d fired at %f on cpu %d", x, timer[x].count, timer[x].cpu); }
					timer[x].count = 0;// timer[x].count - timer[x].period; //Count leftover cycles as passed
					if (timer[x].callback) { (timer[x].callback)(timer[x].callback_param); }

					//If this is a one shot timer, clear it. Else keep it around
					if (timer[x].expire == 1)
					{
						if (VERBOSE) { wrlog("timer %d removed", x); }
						timer_remove(x);
					}
				}
			}
		}
	}
}

void timer_cpu_reset(int cpunum)
{
	int x;
	for (x = 1; x < MAX_TIMERS; x++)
	{
		if (timer[x].cpu == cpunum)
		{
			timer[x].count = 0;
		}
	}
}

//This was written specifically for the watchdog, to reset it every frame.
void timer_reset(int timer_number, double duration)
{
	//wrlog("WATCHDOG TIMER RESET, NUMBER #%d--------------------", timer_number);
	if (timer[timer_number].enabled)
		timer[timer_number].count = 0;
}

//
int timer_pulse(double duration, int param, void(*callback)(int))
{
	int t = timer_set(duration, ONE_SHOT + param, 0, callback);
	return t;
}

int timer_set(double duration, int param, void(*callback)(int))
{
	int t = timer_set(duration, param, 0, callback);
	return t;
}

int timer_pulse(double duration, int param, int data, void(*callback)(int))
{
	int t = timer_set(duration, ONE_SHOT + param, data, callback);
	return t;
}

void timer_clear_all_eof()
{
	int x;
	for (x = 0; x < MAX_TIMERS; x++)
	{
		if (timer[x].enabled)
		{
			timer[x].count = 0;
		}
	}
}