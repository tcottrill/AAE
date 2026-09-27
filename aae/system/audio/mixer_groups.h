//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
// mixer_groups.h -- user-facing group volumes layered over channel volumes.
//
// Drivers and chip emulators set a per-channel byte (sample_set_volume,
// mixing_level) that encodes their own balance. The user has no way to move
// a whole class of sound against another, so every channel also belongs to
// one group and the group's byte multiplies in:
//
//   applied gain = curve(channel byte) * curve(group byte) * curve(driver trim)
//
// The trim is a per-game constant the driver declares (AAE_DRIVER_SOUND_TRIM)
// so shipped balance lives in code; the user's knob sits on top of it.
//
// Both bytes go through the same perceptual curve as MAIN VOLUME
// (VolumeNormalizedToLinear), so a group at 128 reads "50%" in the menu and
// is about -6 dB, just like the master. Group byte 255 is bit-exact identity.
//
// Membership is decided by the function that starts the channel, not by the
// channel number: stream_start -> CHIP (POKEY, AY8910, SN76477, Namco, DAC,
// TMS5220...), sample_start / sample_start_mixer -> SAMPLE (recorded game
// sounds, plus ROM-decoded speech a driver plays through sample_start). The
// reserved ambient channels (MIXER_FIRST_RESERVED_CHANNEL and
// up) are NONE because setup_ambient already scales them by noisevol.
//
// Pure math, no state: mixer.cpp owns the group bytes and the channel tags.
//==========================================================================
#pragma once

#include "mixer.h"   // VolumeByteToLinear, MIXER_FIRST_RESERVED_CHANNEL

enum MixerGroup {
	MIXER_GROUP_NONE   = 0,   // gain 1.0, not user adjustable (ambient loops)
	MIXER_GROUP_SAMPLE = 1,   // recorded samples: sample_start / sample_start_mixer
	MIXER_GROUP_CHIP   = 2,   // emulated sound chips: stream_start
	MIXER_GROUP_COUNT  = 3
};

// Which group a channel joins when it is (re)started. is_stream is true for
// stream_start, false for sample_start / sample_start_mixer.
static inline int mixer_group_for_start(int chanid, bool is_stream) noexcept
{
	if (chanid >= MIXER_FIRST_RESERVED_CHANNEL) return MIXER_GROUP_NONE;
	return is_stream ? MIXER_GROUP_CHIP : MIXER_GROUP_SAMPLE;
}

// Linear gain for a channel byte under a group byte. Both clamped to 0..255.
static inline float mixer_group_channel_gain(int channel_vol255, int group_vol255) noexcept
{
	return VolumeByteToLinear(channel_vol255) * VolumeByteToLinear(group_vol255);
}

// Same, with a per-game driver trim (AAE_DRIVER_SOUND_TRIM) multiplied under
// the user's group byte. 255 = no trim.
static inline float mixer_group_channel_gain(int channel_vol255, int group_vol255, int trim255) noexcept
{
	return mixer_group_channel_gain(channel_vol255, group_vol255) * VolumeByteToLinear(trim255);
}

// AAEDriver::sample_trim / chip_trim -> trim byte. Drivers that omit
// AAE_DRIVER_SOUND_TRIM get value-initialised 0, which means "no trim", not
// silence. Out-of-range values clamp to 255.
static inline int mixer_trim_from_driver(int driver_value) noexcept
{
	if (driver_value <= 0) return 255;
	return driver_value > 255 ? 255 : driver_value;
}
