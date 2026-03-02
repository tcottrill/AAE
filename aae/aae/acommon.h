#pragma once
#ifndef ACOMMON_H
#define ACOMMON_H


void video_loop(void);

void sanity_check_config(void);
void setup_ambient(int style);
int vector_timer(int deltax, int deltay);
#endif