// -----------------------------------------------------------------------------
// Exidy 440 sound board  -  AAE port (interface)
//
// Adapted from M.A.M.E.(TM) sndhrdw/exidy440.c (the Exidy 440 CVSD sample
// player). In AAE this drives a second CPU (an MC6809 sound CPU) that receives
// commands from the main CPU over a command/ack handshake, programs an MC6844
// DMA controller, and streams CVSD-decoded samples to a stereo mixer.
//
// STAGE 1 (this revision): the 6809 + command/FIRQ/ack handshake + the MC6844
// DMA register bookkeeping and channel timing are live, so the sound CPU runs
// its ROM and the main CPU stops throwing the "unrecoverable system error".
// The actual audio output (CVSD decode + mixer stream) is NOT wired yet; the
// hooks are marked "STAGE 2" in the .cpp.
// -----------------------------------------------------------------------------

#ifndef EXIDY440_SOUND_H
#define EXIDY440_SOUND_H

#pragma once

#include "deftypes.h"   // UINT8

// ---- Lifecycle -------------------------------------------------------------
// clock_hz is the CVSD sample / FCLK rate (vertigo: 1 MHz / 16 = 62500).
void  exidy440_sound_init(int clock_hz);
void  exidy440_sound_reset(void);
void  exidy440_sound_stop(void);
// Advance the MC6844 DMA channels by one video frame's worth of samples.
// Call once per frame from the driver's run() hook.
void  exidy440_sound_update(void);

// ---- Main-CPU (68000) side: command + ack handshake ------------------------
void  exidy440_sound_command_w(int data);   // latch a command and FIRQ the 6809
UINT8 exidy440_sound_command_ack_r(void);   // 1 = read by the sound CPU, 0 = pending

// ---- Sound-CPU (6809) side: register handlers ------------------------------
// 'offset' is relative to each block's base, matching MAME's READ8/WRITE8
// offsets (the AAE 6809 already passes addr-base to its handlers).
UINT8 exidy440_m6844_r(int offset);
void  exidy440_m6844_w(int offset, UINT8 data);
UINT8 exidy440_sound_command_r(int offset);
UINT8 exidy440_sound_volume_r(int offset);
void  exidy440_sound_volume_w(int offset, UINT8 data);
UINT8 exidy440_sound_banks_r(int offset);
void  exidy440_sound_banks_w(int offset, UINT8 data);
void  exidy440_sound_interrupt_clear_w(int offset, UINT8 data);

#endif // EXIDY440_SOUND_H
