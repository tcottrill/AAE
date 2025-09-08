#pragma once

// -----------------------------------------------------------------------------
// Audio Mixer System - XAudio2 Based
// High-performance audio playback and mixing engine for game applications.
// Provides sample loading, playback, streaming, resampling, and volume/pan
// control using the Microsoft XAudio2 API. Supports both direct voice playback
// and software-mixed PCM channels with optional looping and streaming modes.
//
// Features:
//   - Load and manage multiple audio samples (WAV, PCM).
//   - Play, stop, pause, and resume audio with per-channel control.
//   - Real-time volume, pan, and frequency adjustment.
//   - Software mixer for low-level control and streaming support.
//   - Integrated resampling for 8-bit and 16-bit audio.
//   - Thread-safe operation using mutex locking.
//   - Save samples to disk as standard WAV files.
//
// Dependencies:
//   - Microsoft XAudio2
//   - WAV loading/resampling helpers
//   - Logging and file I/O utilities
//
// License:
//   This program is free software: you can redistribute it and/or modify
//   it under the terms of the GNU General Public License as published by
//   the Free Software Foundation, either version 3 of the License, or
//   (at your option) any later version.
//
//   This program is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
//   GNU General Public License for more details.
//
//   You should have received a copy of the GNU General Public License
//   along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// -----------------------------------------------------------------------------

// Note for ME: This is the FULL INTEGER version of this code, 8/15/25

#ifndef MIXER_H
#define MIXER_H

#include <string>
#include <cstdint>
#include <xaudio2redist.h>
#include <memory>
#include <vector>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <algorithm>
#include  "mixer_volume.h"

// Enum for sound state
enum class SoundState {
    Null,
    Loaded,
    Playing,
    Stopped,
    PCM,
    Stream
};

// Represents a playback channel
struct CHANNEL {
    IXAudio2SourceVoice* voice = nullptr;
    XAUDIO2_BUFFER buffer = {};
    SoundState state = SoundState::Stopped;
    unsigned int pos = 0;
    int loaded_sample_num = -1;
    int id = 0;
    int looping = 0;
    double vol = 1.0;
    int stream_type = 0;
    bool isAllocated = false;
    bool isPlaying = false;
    bool isReleased = false;
    int frequency = 0;
    int volume = 255;
    int pan = 128;
};

// Represents a loaded audio sample
struct SAMPLE {
    WAVEFORMATEX fx = {};

    uint32_t sampleCount = 0;
    uint32_t dataSize = 0;
    SoundState state = SoundState::Null;
    int num = -1;
    std::string name;

    std::unique_ptr<uint8_t[]> data8;    // 8-bit PCM data
    std::unique_ptr<int16_t[]> data16;   // 16-bit PCM data
    void* buffer = nullptr;              // Pointer to active buffer (either data8 or data16)
};
/*
Volume setting notes:
If you already have 0–255:
sample_set_volume(ch, vol255);
If a UI gives 0–100:
sample_set_volume(ch, VolPercentToByte(uiPercent));
*/
// Byte (0..255) -> linear gain, routed through the existing percent curve.
// This preserves the loudness taper while moving call sites to 0..255.
inline float VolumeByteToLinear(int vol255) noexcept
{
    const int v = std::clamp(vol255, 0, 255);
    // Round to nearest percent (0..100), then reuse the existing curve.
    const int percent = (v * 100 + 127) / 255;
    return VolumePercentToLinear(percent);
}

// helper to map 0..100 -> 0..255 and forward
inline int VolPercentToByte(int percent) noexcept {
    percent = std::clamp(percent, 0, 100);
    return (percent * 255 + 50) / 100; // round-to-nearest
}

inline int VolByteToPercent(int vol255) noexcept {
    return std::clamp((vol255 * 100 + 127) / 255, 0, 100);
}

//Main Audio Mixer volume controls
float mixer_get_master_volume();
void mixer_set_master_volume(int volume);
// Master volume: keep existing API if you’re hesitant to change it.
// Optional convenience wrapper if you want a 0..255 version without touching callers:
inline void mixer_set_master_volume_255(int vol255) {
    const int percent = std::clamp((vol255 * 100 + 127) / 255, 0, 100);
    mixer_set_master_volume(percent);  // existing function
}



// Mixer core functions
void mixer_init(int rate, int fps);
// Just signals the audio thread to process
void mixer_update();   
// Shutdown 
void mixer_end();

// -----------------------------------------------------------------------------
// mixer_upload_sample16
// Replace/initialize a sample's PCM with mono/stereo 16-bit data and format.
// Allocates/reallocates internal storage as needed.
// Returns 0 on success, -1 on failure.
//
// Parameters:
//   samplenum - index into the mixer sample registry (the same value used by sample_start)
//   pcm       - pointer to interleaved 16-bit PCM
//   frames    - number of frames (samples per channel)
//   freq      - sample rate (e.g., 44100)
//   stereo    - false=mono, true=stereo
// -----------------------------------------------------------------------------
int mixer_upload_sample16(int samplenum,
    const int16_t* pcm,
    uint32_t frames,
    int freq,
    bool stereo = false);

// Sample loading and control
int load_sample(const char* archname, const char* filename, bool force_resample = true);
void sample_stop(int chanid);
void sample_start(int chanid, int samplenum, int loop);
void sample_set_position(int chanid, int pos);
void sample_set_volume(int chanid, int volume);
int sample_get_volume(int chanid);
void sample_set_freq(int chanid, int freq);
int sample_get_freq(int chanid);
int sample_playing(int chanid);
void sample_end(int chanid);
void samples_stop_all();
void sample_remove(int samplenum);
void save_sample(int samplenum);
void sample_set_pan(int chanid, int pan);
int sample_get_pan(int chanid);

// Mixer-based sample control
void sample_start_mixer(int chanid, int samplenum, int loop);
void sample_end_mixer(int chanid);
void sample_stop_mixer(int chanid);
int sample_playing_mixer(int chanid);

// Utility
short Make16bit(uint8_t sample);
unsigned char Make8bit(int16_t sample);

// Global audio control
void pause_audio();
void restore_audio();

// Streaming functions
void stream_start(int chanid, int stream, int bits, int frame_rate);
// New (stereo-version):
void stream_start(int chanid, int stream, int bits, int frame_rate, bool stereo);
void stream_stop(int chanid, int stream);
void stream_update(int chanid, short* data);
void stream_update(int chanid, unsigned char* data);

// Resampling API
void resample_wav_8(SAMPLE* sample, int new_freq);
void resample_wav_16(SAMPLE* sample, int new_freq, bool use_cubic = true);

// Sample creation
int create_sample(int bits, bool is_stereo, int freq, int len, const std::string& name);

// Sample lookup
std::string numToName(int num);
int nameToNum(const std::string& name);
int snumlookup(int snum);

#ifdef USE_VUMETER
// -----------------------------------------------------------------------------
// VU meter API
// Returns smooth peak meters for left/right channels in the range 0..1.
// Thread-safe: values are written by the audio thread and read by the main thread.
//
// Parameters:
//   left  - optional pointer to receive left-channel level (0..1)
//   right - optional pointer to receive right-channel level (0..1)
// -----------------------------------------------------------------------------
void mixer_get_vu(float* left, float* right);

// -----------------------------------------------------------------------------
// mixer_reset_vu
// Immediately clears the internal VU meters to 0.
// Use if you want to blank meters upon pause/stop.
// -----------------------------------------------------------------------------
void mixer_reset_vu();
#endif

#endif // MIXER_H
