#pragma once

#ifndef BOSCO_H
#define BOSCO_H

#pragma warning(disable:4996 4102)

void boscoint1();
void boscoint2();
void boscoint3();

int  init_bosco();
void run_bosco();
void end_bosco();
extern void bosco_interrupt();
extern struct GfxDecodeInfo bosco_gfxdecodeinfo[];
void bosco_vh_convert_color_prom(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom);



#endif