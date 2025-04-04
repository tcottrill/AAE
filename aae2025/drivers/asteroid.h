#ifndef ASTEROID_H
#define ASTEROID_H

//#include "aaemain.h"

int init_asteroid(void);
int init_astdelux(void);
void run_asteroid();
void run_astdelux();
void end_asteroid();
void end_astdelux();

int asteroid1_hiload();
void  asteroid1_hisave();

int asteroid_hiload();
void  asteroid_hisave();
extern void asteroid_interrupt();

// Should be in avg_dvg.h



#endif