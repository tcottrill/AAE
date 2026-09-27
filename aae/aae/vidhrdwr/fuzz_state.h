//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
// -----------------------------------------------------------------------------
// fuzz_state.h - screen defocus/whiteout level published by a driver and
// consumed by the vector composite in BOTH renderers: GL final_render's
// fragMulti pass, and VK VectorPostVK::RecordComposite / RecordFrameBuild.
//
//   0.0 = off. The composite takes its normal path and its output is unchanged.
//   1.0 = full defocus plus blowout to white.
//
// Star Wars drives this from the Death Star explosion phase (see
// drivers/starwars.cpp). Nothing else sets it, so every other game reads 0.
//
// This lives in vidhrdwr/ rather than aae_video/ on purpose: the setter is a
// driver, drivers are aae_core translation units, and aae_core's include path
// reaches vidhrdwr but not aae_video. Keeping the definition inside aae_core
// also keeps aae_headless - which links aae_core with no OSD at all - linking.
//
// License: GPL-3.0-or-later (as the rest of AAE).
// ASCII-only comments.
// -----------------------------------------------------------------------------
#pragma once
#ifndef FUZZ_STATE_H
#define FUZZ_STATE_H

// Publish this frame's level; clamped to 0..1. Called once per frame from the
// driver's run_game(). init_machine() calls Fuzz_Set(0) before every driver's
// init_game(), so a level left over from a previous game cannot leak into the
// next one.
void Fuzz_Set(float level);

// Read the current level. Safe to call more than once per frame - the VK chain
// composites at two different call sites.
float Fuzz_Get(void);

#endif // FUZZ_STATE_H
