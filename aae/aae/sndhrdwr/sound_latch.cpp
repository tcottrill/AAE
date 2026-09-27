//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
// sound_latch.cpp - generic inter-CPU sound command latches.
// See sound_latch.h for semantics and the deliberate difference from MAME.
//
// License: GPL-3.0-or-later (as the rest of AAE).
// ASCII-only comments.
//==========================================================================

#include "sound_latch.h"
#include "sys_log.h"

#define NUM_LATCHES 4

static UINT8 s_latch[NUM_LATCHES];
static UINT8 s_consumed[NUM_LATCHES];   // 1 = read since last write

UINT8 soundlatch_get(int n)
{
	if (n < 0 || n >= NUM_LATCHES) return 0;
	s_consumed[n] = 1;
	return s_latch[n];
}

void soundlatch_set(int n, UINT8 v)
{
	if (n < 0 || n >= NUM_LATCHES) return;

	// A different value landing on an unread one means the reader missed a
	// command - the classic dropped-note bug. When this fires, the driver's
	// CPU interleave is too coarse for its sound handshake.
	if (!s_consumed[n] && s_latch[n] != v)
		LOG_DEBUG("soundlatch %d overwritten before read: %02X -> %02X", n + 1, s_latch[n], v);

	s_latch[n] = v;
	s_consumed[n] = 0;
}

void soundlatch_reset_all(void)
{
	for (int i = 0; i < NUM_LATCHES; i++)
	{
		s_latch[i] = 0;
		s_consumed[i] = 1;   // nothing pending, so no warning on first write
	}
}

// ---------------------------------------------------------------------------
// Memory-map trampolines
// ---------------------------------------------------------------------------
UINT8 soundlatch_r(UINT32 address, struct MemoryReadByte* psMemRead) { return soundlatch_get(0); }
void  soundlatch_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite) { soundlatch_set(0, data); }
UINT8 soundlatch2_r(UINT32 address, struct MemoryReadByte* psMemRead) { return soundlatch_get(1); }
void  soundlatch2_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite) { soundlatch_set(1, data); }
UINT8 soundlatch3_r(UINT32 address, struct MemoryReadByte* psMemRead) { return soundlatch_get(2); }
void  soundlatch3_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite) { soundlatch_set(2, data); }
UINT8 soundlatch4_r(UINT32 address, struct MemoryReadByte* psMemRead) { return soundlatch_get(3); }
void  soundlatch4_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite) { soundlatch_set(3, data); }
