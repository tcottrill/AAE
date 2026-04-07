/*****************************************************************************
  DAC (Digital-to-Analog Converter) sound emulation
  Ported from MAME 0.36 to AAE (Another Arcade Emulator)

  Supports up to MAX_DAC independent DAC channels.
  Each channel holds a single DC output level that is written by the CPU
  and replicated across the entire audio frame each update.

  Volume tables are linear:
    Unsigned 8-bit:  0..255  maps to  0..32767
    Signed   8-bit:  0..255  maps to -32768..32767
    Unsigned 16-bit: 0..65535 maps to  0..32767   (top bit dropped)
    Signed   16-bit: 0..65535 maps to -32768..32767

 *****************************************************************************/
#ifndef DAC_H
#define DAC_H


#include "aae_mame_driver.h"   /* WRITE_HANDLER, UINT8 etc. */

#define MAX_DAC 4

/* Interface structure passed to DAC_sh_start() */
struct DACinterface {
    int num;                      /* number of DAC channels (max MAX_DAC) */
    int mixing_level[MAX_DAC];    /* per-channel volume, 0..100 percent */
};

/* Lifecycle */
int  DAC_sh_start(const struct DACinterface *intf_in);
void DAC_sh_stop(void);
void DAC_sh_update(void);

/* Data write functions - call from memory write handlers */
void DAC_data_w(int num, int data);          /* unsigned 8-bit:  0..255 */
void DAC_signed_data_w(int num, int data);   /* signed   8-bit:  0..255 (treated as signed) */
void DAC_data_16_w(int num, int data);       /* unsigned 16-bit: 0..65535 */
void DAC_signed_data_16_w(int num, int data);/* signed   16-bit: 0..65535 (treated as signed) */

/* Convenience write handlers wired directly into memory maps */
WRITE_HANDLER(DAC_0_data_w);
WRITE_HANDLER(DAC_1_data_w);
WRITE_HANDLER(DAC_0_signed_data_w);
WRITE_HANDLER(DAC_1_signed_data_w);


#endif /* DAC_H */
