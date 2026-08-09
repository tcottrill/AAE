#pragma once

#ifndef DIGDUG_H
#define DIGDUG_H


void digdugint1();
void digdugint2();
void digdugint3();

int  init_digdug();
void run_digdug();
void end_digdug();

extern struct GfxDecodeInfo digdug_gfxdecodeinfo[];
void digdug_vh_convert_color_prom(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom);

#endif
