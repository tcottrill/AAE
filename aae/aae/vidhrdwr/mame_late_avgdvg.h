#ifndef __AVGDVG__
#define __AVGDVG__

#include "aae_mame_driver.h"

extern UINT8 *tempest_colorram;
extern UINT8 *mhavoc_colorram;
/* Real storage (not a pointer): Quantum's 68K memory map installs this as a
 * direct write base via MEM_ADDR16, which needs a static array address. */
extern UINT16 quantum_colorram[0x20];
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

/* Drops everything the generator has produced so far: the pass buffered
 * since the last frame cut and the renderer's display list. For a host-side
 * discontinuity such as a multigame bank switch, where the outgoing game's
 * picture must not be shown under the incoming game's geometry. */
void avgdvg_discard(void);

/* Run the DVG/AVG state machine synchronously until the halt strobe, then
 * disarm the run/halt timers and mark the VG halted. For drivers that
 * frame-lock the "done" bit (omegrace60): the whole list is drawn at kick
 * time instead of trickling over emulated time, so render() never presents
 * a partially executed list. Call directly after avgdvg_go(). */
void avgdvg_run_to_halt(void);

/* 16-bit bus wrappers (AAE MemoryWriteWord handler signature) */
void avgdvg_go_word_w(UINT32 address, UINT16 data, struct MemoryWriteWord *psMemWrite);
void avgdvg_reset_word_w(UINT32 address, UINT16 data, struct MemoryWriteWord *psMemWrite);

/* Tempest and Quantum use this capability */
void avg_set_flip_x(int flip);
void avg_set_flip_y(int flip);

/* Per-game vertical shift of the DVG picture in game units (positive moves
 * the picture up). Reset to 0 by every *_start; call after dvg_start().
 * Used by asteroids / asteroids deluxe / lunar lander for overlay/texture
 * alignment without touching video.ini. */
void dvg_set_yshift(int shift);

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
