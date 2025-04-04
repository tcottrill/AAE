#ifndef RAND_H
#define RAND_H

//#include "aaemain.h"

void initrand();
int randint();
int randintm(int max);
int randintmm(int min, int max);
float randfloat();
float randfloatm(float max);
float randfloatmm(float min, float max);
double randdouble();
double randdoublem(double max);
double randdoublemm(double min, double max);
//char* randstr(char* str, unsigned long length);
//char* randstrb(char* str, unsigned long length, unsigned long flags);

#endif