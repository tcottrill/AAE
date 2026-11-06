//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
//==============================================================================
// alsa_backend.h -- intentionally empty.
//
// The Linux IAudioBackend (class LinuxAudioBackend: native PipeWire first, raw
// ALSA as the no-server fallback) now lives entirely inside alsa_backend.cpp.
// Nothing else needs its definition - create_audio_backend() in audio_backend.h
// is the only way in. The file keeps its name so the project listings and the
// habit of looking for it still work.
//==============================================================================
#pragma once
