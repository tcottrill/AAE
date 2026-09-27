//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
// fuzz_state.cpp - storage for the composite defocus/whiteout level.
// See fuzz_state.h for why this lives in aae_core rather than aae_video.
//
// License: GPL-3.0-or-later (as the rest of AAE).
// ASCII-only comments.
//==========================================================================

#include "fuzz_state.h"

static float s_fuzzLevel = 0.0f;

void Fuzz_Set(float level)
{
	if (level < 0.0f) level = 0.0f;
	else if (level > 1.0f) level = 1.0f;

	s_fuzzLevel = level;
}

float Fuzz_Get(void)
{
	return s_fuzzLevel;
}
