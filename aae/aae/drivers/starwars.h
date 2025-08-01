#pragma once

//#include "aaemain.h"

void starwars_nvram_handler(void* file, int read_or_write);
int init_esb();
int init_starwars();
void run_starwars();
void end_starwars();
void starwars_interrupt();

