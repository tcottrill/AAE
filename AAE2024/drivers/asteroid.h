#ifndef ASTEROID_H
#define ASTEROID_H

#include "../aaemain.h"

extern char* gamename[];

int init_asteroid(void);
int init_astdelux(void);
void run_asteroids();
void end_asteroids();
void end_astdelux();
int asteroid_load_hi();
void  asteroid_save_hi();
void asteroid_interrupt();

#endif