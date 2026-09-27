// Harris HC55516 CVSD speech decoder, from MAME 0.53 sound/hc55516.c,
// adapted to AAE's push-model mixer (see dac.cpp for the pattern).
#pragma once
#ifndef HC55516_H
#define HC55516_H

int  hc55516_sh_start(int volume /*0..255*/);
void hc55516_sh_stop(void);
void hc55516_sh_update(void);          // call once per video frame

// Peripheral-side pins (Williams wires these to the sound PIA CB2/CA2).
void hc55516_clock_w(int state);       // rising edge shifts in the digit
void hc55516_digit_w(int data);        // data bit

#endif
