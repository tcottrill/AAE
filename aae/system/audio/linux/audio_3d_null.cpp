//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
//==============================================================================
// audio_3d_null.cpp -- the Linux implementation of audio_3d.h.
//
// Positional audio is Phase 3d (see docs/superpowers/specs/
// 2026-07-28-phase3b-linux-backends-design.md, sections 3.6 and 5). It is
// SCHEDULED, not abandoned - parity with Windows is this programme's stated
// end point, and every deferral names a successor phase.
//
// This file exists so the gap is LOUD. A Linux build that silently played
// everything centre-panned would be indistinguishable from a working one until
// somebody noticed months later that the stereo field was dead, so
// audio_3d_init() logs a warning rather than just returning false.
//
// mixer.cpp gates its entire positional path on g_3d_inited, so returning false
// disables it cleanly with no other changes. AlsaBackend::VoiceSetOutputMatrix
// returns false for the same reason.
//
// Doing it properly means reimplementing X3DAudio's channel-matrix
// computation - self-contained DSP work, and not what a portability phase is
// for.
//==============================================================================
#include "audio_3d.h"
#include "sys_log.h"

bool audio_3d_init(uint32_t /*channel_mask*/, uint32_t /*dst_channels*/)
{
	LOG_WARN("Positional audio is not implemented on this platform (Phase 3d) - "
	         "samples will play without 3D panning");
	return false;
}

void audio_3d_shutdown() {}

bool audio_3d_ready() { return false; }

void audio_3d_set_listener_2d(float /*x*/, float /*y*/) {}

bool audio_3d_apply_2d(VoiceHandle* /*voice*/,
	float /*src_x*/, float /*src_y*/,
	uint32_t /*src_channels*/)
{
	return false;
}

void audio_3d_debug_print_next_matrix() {}

uint32_t audio_3d_get_channel_mask() { return 0; }
