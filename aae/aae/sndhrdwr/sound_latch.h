//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
// sound_latch.h - generic inter-CPU sound command latches, the AAE analog of
// MAME sndintrf.c's soundlatch_r/w family.
//
// The typical master-slave arrangement: the main CPU writes a command byte
// into a hardware latch (74LS374-class) and usually pulses the sound CPU's
// interrupt; the sound CPU reads the byte out - from its memory map, an I/O
// port, or an AY-8910 parallel port the latch is wired to.
//
// Four independent 8-bit latches, MAME name parity: soundlatch = index 0,
// soundlatch2 = 1, soundlatch3 = 2, soundlatch4 = 3.
//
// SEMANTICS - deliberate difference from MAME: writes land DIRECTLY. MAME
// 0.36 routes every write through timer_set(TIME_NOW, ...) to force a CPU
// timeslice sync first; that trick belongs to MAME's scheduler, and AAE
// drivers handle the same race (two commands written within one slice, the
// first never seen by the reader) with per-driver interleave instead - see
// cchasm.cpp's interleave notes. soundlatch_set() logs a LOG_DEBUG when it
// overwrites a value no one has read: that warning firing is the sign a
// driver's interleave is too coarse.
//
// Side effects (IRQ triggers, data munges like dkong's ^0x0f) are NOT the
// latch's business - they are board wiring, and stay in the drivers as thin
// wrappers around soundlatch_set()/soundlatch_get().
//
// All four latches are cleared by init_machine before every driver's
// init_game() (soundlatch_reset_all), so a stale command from the previous
// session can never replay into a relaunch.
//
// License: GPL-3.0-or-later (as the rest of AAE).
// ASCII-only comments.
//==========================================================================
#pragma once
#ifndef SOUND_LATCH_H
#define SOUND_LATCH_H

#include "deftypes.h"

struct MemoryReadByte;
struct MemoryWriteByte;

// Raw accessors - for AY port callbacks, z80 port handlers, and custom
// handlers. n is 0..3; out-of-range n reads 0 / writes nowhere.
UINT8 soundlatch_get(int n);
void  soundlatch_set(int n, UINT8 v);

// Clear all four latches (engine calls this from init_machine; a driver may
// also call it from a reset handler).
void soundlatch_reset_all(void);

// Memory-map-ready handlers (AAE signatures, range-relative address unused).
// soundlatch_r/w = latch 0 ... soundlatch4_r/w = latch 3, as in MAME.
UINT8 soundlatch_r(UINT32 address, struct MemoryReadByte* psMemRead);
void  soundlatch_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite);
UINT8 soundlatch2_r(UINT32 address, struct MemoryReadByte* psMemRead);
void  soundlatch2_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite);
UINT8 soundlatch3_r(UINT32 address, struct MemoryReadByte* psMemRead);
void  soundlatch3_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite);
UINT8 soundlatch4_r(UINT32 address, struct MemoryReadByte* psMemRead);
void  soundlatch4_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite);

#endif // SOUND_LATCH_H
