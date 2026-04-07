/***************************************************************************

  circus.h

  Header for the Exidy Circus hardware family:
    - Circus (1977)
    - Robot Bowl (1977)
    - Crash (1979)
    - Ripcord (1977)

  Single 6502 at 705,562 Hz (11.289 MHz / 16), 57 fps.
  256x256 screen, 1-bit monochrome graphics.
  DAC sound on all four games.

  Ported from MAME 0.36 driver by Mike Coates.

***************************************************************************/

#pragma once

#ifndef CIRCUS_H
#define CIRCUS_H

/* init / run / end for each game variant */
int  init_circus(void);
void run_circus(void);
void end_circus(void);

int  init_robotbwl(void);
void run_robotbwl(void);
void end_robotbwl(void);

int  init_crash(void);
void run_crash(void);
void end_crash(void);

int  init_ripcord(void);
void run_ripcord(void);
void end_ripcord(void);

/* interrupt callbacks */
void circus_interrupt(void);
void crash_interrupt(void);
void ripcord_interrupt(void);

/* palette init callback (shared across all four games) */
void circus_init_palette(unsigned char* palette, unsigned char* colortable,
                         const unsigned char* color_prom);

#endif /* CIRCUS_H */
