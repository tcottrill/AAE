#pragma once

#ifndef VICDUAL_H
#define VICDUAL_H

extern unsigned char* vicdual_videoram;

int init_vicdual();
void run_vicdual();
void end_vicdual();
extern void vicdual_interrupt();
extern void init_palette(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom);



#endif