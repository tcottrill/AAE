#ifndef __AVGDVG__
#define __AVGDVG__

#include "aae_mame_driver.h"

extern UINT8 *tempest_colorram;
extern UINT8 *mhavoc_colorram;
extern UINT16 *quantum_colorram;
extern UINT16 *quantum_vectorram;

extern unsigned char *vectorram;
extern unsigned int vectorram_size;

extern int vector_updates;

int avgdvg_done(void);

/* AAE memory-handler signatures for 8-bit bus games */
void avgdvg_go_w(UINT32 address, UINT8 data, struct MemoryWriteByte *psMemWrite);
void avgdvg_reset_w(UINT32 address, UINT8 data, struct MemoryWriteByte *psMemWrite);

/* Plain call versions (used internally and by 16-bit bus games) */
void avgdvg_go(int offset, int data);
void avgdvg_reset(int offset, int data);

/* 16-bit bus wrappers */
void avgdvg_go_word_w(unsigned int offset, unsigned int data);
void avgdvg_reset_word_w(unsigned int offset, unsigned int data);

/* Tempest and Quantum use this capability */
void avg_set_flip_x(int flip);
void avg_set_flip_y(int flip);

/* Video start functions — return 0 on success */
int dvg_start();
int avg_start();
int avg_start_tempest();
int avg_start_mhavoc();
int avg_start_alphaone();
int avg_start_starwars();
int avg_start_quantum();
int avg_start_bzone();

#endif
