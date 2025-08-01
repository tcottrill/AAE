#pragma once

#ifndef INVADERS_H
#define INVADERS_H

extern unsigned char* invaders_videoram;
int init_invaddlx();
int init_invaders();
void run_invaders();
void end_invaders();
extern void invaders_interrupt();
extern void init_palette(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom);



#endif