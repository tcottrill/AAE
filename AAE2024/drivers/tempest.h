#ifndef TEMPEST_H
#define TEMPEST_H

#include "aaemain.h"

extern char* gamename[];
extern int gamenum;

int init_tempest();
void run_tempest();
void end_tempest();
void tempest_interrupt();

#endif