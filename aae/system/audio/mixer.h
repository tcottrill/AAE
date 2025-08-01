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

#ifndef MIXER_H
#define MIXER_H

#include <string>
#include <cstdint>
#include <xaudio2.h>
#include <memory>
#include <vector>
#include <thread>
#include <condition_variable>
#include <atomic>

// Strongly typed sound state
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
    XAUDIO2_BUFFER buffer = { 0 };
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

// Mixer core functions
void mixer_init(int rate, int fps);
void mixer_update();     // now signals the audio thread
void mixer_end();

// Sample loading and control
int load_sample(const char* archname, const char* filename, bool force_resample = true);
void sample_stop(int chanid);
void sample_start(int chanid, int samplenum, int loop);
void sample_set_position(int chanid, int pos);
void sample_set_volume(int chanid, int volume);
void sample_set_freq(int chanid, int freq);
int sample_get_freq(int chanid);
int sample_playing(int chanid);
void sample_end(int chanid);
void sample_remove(int samplenum);
void save_sample(int samplenum);

// Mixer-based sample control
void sample_start_mixer(int chanid, int samplenum, int loop);
void sample_end_mixer(int chanid);
void sample_stop_mixer(int chanid);
void sample_set_volume_mixer(int chanid, int volume);
int sample_get_volume_mixer(int chanid);
int sample_playing_mixer(int chanid);

// Utility
short Make16bit(uint8_t sample);

// Global audio control
void pause_audio();
void restore_audio();

// Streaming functions
void stream_start(int chanid, int stream, int bits, int frame_rate);
void stream_stop(int chanid, int stream);
void stream_update(int chanid, short* data);
void stream_update(int chanid, unsigned char* data);

// Resampling
void resample_wav_8(SAMPLE* sample, int new_freq);
void resample_wav_16(SAMPLE* sample, int new_freq);

// Sample creation
int create_sample(int bits, bool is_stereo, int freq, int len, const std::string& name);

// Sample lookup
std::string numToName(int num);
int nameToNum(const std::string& name);
int snumlookup(int snum);

#endif // MIXER_H
