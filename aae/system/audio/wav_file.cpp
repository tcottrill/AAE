/* =============================================================================
* -------------------------------------------------------------------------- -
*License(GPLv3) :
    *This file is part of GameEngine Alpha.
    *
    *<Project Name> is free software : you can redistribute it and /or modify
    * it under the terms of the GNU General Public License as published by
    * the Free Software Foundation, either version 3 of the License, or
    *(at your option) any later version.
    *
    *<Project Name> is distributed in the hope that it will be useful,
    * but WITHOUT ANY WARRANTY; without even the implied warranty of
    * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
    * GNU General Public License for more details.
    *
    * You should have received a copy of the GNU General Public License
    * along with GameEngine Alpha.If not, see < https://www.gnu.org/licenses/>.
*
*Copyright(C) 2022 - 2025  Tim Cottrill
* SPDX - License - Identifier : GPL - 3.0 - or -later
* ============================================================================ =
*/
// Mixercode 2 "The 4th Version", 2025 Tim Cottrill
// This is revision 14. 8/13/25
// Mixercode 2 - Updated for modern C++ SAMPLE structure


//#define OGG_DECODE
//#define MP3_DECODE

#include "wav_file.h"
#include <xaudio2redist.h>
#include "sys_log.h"
#include <cstring>
#include <memory>
#include <algorithm>

#ifdef OGG_DECODE
#include "stb_vorbis.h"
#endif

#ifdef MP3_DECODE
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
#include "minimp3_ex.h"
#endif

#pragma warning(disable : 4018)

// ------------------ small helpers for MP3 detection -------------------------------
// These are only used onthe off chance an MP3 doesn't strt with "MP3" in the ID tag.
// ----------------------------------------------------------------------------------
#ifdef MP3_DECODE
static inline bool has_id3v2(const unsigned char* p, size_t n) {
    return (n >= 10 && p[0] == 'I' && p[1] == 'D' && p[2] == '3');
}

static inline size_t skip_id3v2(const unsigned char* p, size_t n) {
    if (!has_id3v2(p, n)) return 0;
    // ID3v2 header is 10 bytes; size is syncsafe at bytes 6..9
    if (n < 10) return 0;
    size_t sz = ((p[6] & 0x7F) << 21) | ((p[7] & 0x7F) << 14) | ((p[8] & 0x7F) << 7) | (p[9] & 0x7F);
    return (sz > n - 10) ? 10 : (10 + sz); // guard overflow
}

static inline bool looks_like_mpeg_frame(const unsigned char* p, size_t n) {
    // Minimal sync: 0xFFF sync bits plus version/layer not all invalid
    if (n < 2) return false;
    return (p[0] == 0xFF) && ((p[1] & 0xE0) == 0xE0);
}
#endif

// =====================================================================
// WAV
// =====================================================================
int processWaveDataBuffer(const unsigned char* buffer, size_t bufferSize, SAMPLE* audioFile) {
    if (bufferSize < 12) {
        LOG_ERROR("Invalid WAV buffer size.");
        return -1;
    }

    if (std::memcmp(buffer, "RIFF", 4) != 0 || std::memcmp(buffer + 8, "WAVE", 4) != 0) {
        LOG_ERROR("Invalid WAV header.");
        return -1;
    }

    size_t pos = 12;
    bool haveFmt = false;
    bool haveData = false;

    while (pos + 8 <= bufferSize) {
        char chunkID[5] = {};
        std::memcpy(chunkID, buffer + pos, 4);
        pos += 4;

        uint32_t chunkSize = 0;
        std::memcpy(&chunkSize, buffer + pos, sizeof(uint32_t));
        pos += 4;

        // Bounds check for the declared chunk size
        if (pos + chunkSize > bufferSize) {
            LOG_ERROR("WAV chunk overruns buffer (chunk '%c%c%c%c', size=%u)",
                chunkID[0], chunkID[1], chunkID[2], chunkID[3], chunkSize);
            return -1;
        }

        const size_t chunkStart = pos;

        if (std::strncmp(chunkID, "fmt ", 4) == 0) {
            if (chunkSize < 16) {
                LOG_ERROR("WAV fmt chunk too small.");
                return -1;
            }

            std::memcpy(&audioFile->fx.wFormatTag, buffer + pos, sizeof(uint16_t));
            std::memcpy(&audioFile->fx.nChannels, buffer + pos + 2, sizeof(uint16_t));
            std::memcpy(&audioFile->fx.nSamplesPerSec, buffer + pos + 4, sizeof(uint32_t));
            std::memcpy(&audioFile->fx.nAvgBytesPerSec, buffer + pos + 8, sizeof(uint32_t));
            std::memcpy(&audioFile->fx.nBlockAlign, buffer + pos + 12, sizeof(uint16_t));
            std::memcpy(&audioFile->fx.wBitsPerSample, buffer + pos + 14, sizeof(uint16_t));
            haveFmt = true;

            pos += chunkSize;
        }
        else if (std::strncmp(chunkID, "data", 4) == 0) {
            if (!haveFmt) {
                LOG_ERROR("WAV data before fmt.");
                return -1;
            }

            audioFile->dataSize = chunkSize;

            if (audioFile->fx.wBitsPerSample == 8) {
                audioFile->data8 = std::make_unique<uint8_t[]>(chunkSize);
                std::memcpy(audioFile->data8.get(), buffer + pos, chunkSize);
                audioFile->buffer = audioFile->data8.get();
                audioFile->sampleCount = static_cast<uint32_t>(chunkSize); // 8-bit = 1 byte/sample
                haveData = true;
            }
            else if (audioFile->fx.wBitsPerSample == 16) {
                size_t sampleCount = chunkSize / sizeof(int16_t);
                audioFile->data16 = std::make_unique<int16_t[]>(sampleCount);
                std::memcpy(audioFile->data16.get(), buffer + pos, chunkSize);
                audioFile->buffer = audioFile->data16.get();
                audioFile->sampleCount = static_cast<uint32_t>(sampleCount);
                haveData = true;
            }
            else {
                LOG_INFO("Unsupported bit depth: %d", audioFile->fx.wBitsPerSample);
                return -1;
            }

            pos += chunkSize;
        }
        else {
            // Skip unknown chunk payload
            pos += chunkSize;
        }

        // Pad to even boundary per RIFF spec
        if ((pos & 1u) != 0) {
            ++pos;
        }

        // If we've read both fmt and data, we can stop
        if (haveFmt && haveData) break;

        // Guard against stalled loop
        if (pos <= chunkStart) {
            LOG_ERROR("WAV parser stalled.");
            return -1;
        }
    }

    audioFile->fx.cbSize = 0; // PCM baseline
    return haveData ? 0 : -1;
}

// =====================================================================
// MP3
// =====================================================================
#ifdef MP3_DECODE
int processMp3DataBuffer(const unsigned char* buffer, size_t bufferSize, SAMPLE* audioFile) {
    mp3dec_t mp3d;
    mp3dec_file_info_t info{};
    mp3dec_init(&mp3d);

    if (mp3dec_load_buf(&mp3d, buffer, bufferSize, &info, nullptr, nullptr) != 0) {
        LOG_ERROR("MP3 decode failed.");
        return -1;
    }

    audioFile->fx.wFormatTag = WAVE_FORMAT_PCM;
    audioFile->fx.nChannels = info.channels;
    audioFile->fx.nSamplesPerSec = info.hz;
    audioFile->fx.wBitsPerSample = 16;
    audioFile->fx.nBlockAlign = info.channels * 2;
    audioFile->fx.nAvgBytesPerSec = info.hz * audioFile->fx.nBlockAlign;
    audioFile->fx.cbSize = 0; // important for PCM

    size_t sampleCount = info.samples; // total interleaved samples
    audioFile->dataSize = sampleCount * sizeof(int16_t);
    audioFile->data16 = std::make_unique<int16_t[]>(sampleCount);
    std::memcpy(audioFile->data16.get(), info.buffer, audioFile->dataSize);
    audioFile->buffer = audioFile->data16.get();
    audioFile->sampleCount = static_cast<uint32_t>(sampleCount);

    free(info.buffer);
    return 0;
}
#endif

// =====================================================================
// OGG
// =====================================================================
#ifdef OGG_DECODE
int processOggDataBuffer(const unsigned char* buffer, size_t bufferSize, SAMPLE* audioFile) {
    int error = 0;
    stb_vorbis* vorbis = stb_vorbis_open_memory(buffer, static_cast<int>(bufferSize), &error, nullptr);
    if (!vorbis || error) {
        LOG_ERROR("OGG decode failed.");
        return -1;
    }

    stb_vorbis_info info = stb_vorbis_get_info(vorbis);

    audioFile->fx.wFormatTag = WAVE_FORMAT_PCM;
    audioFile->fx.nChannels = info.channels;
    audioFile->fx.nSamplesPerSec = info.sample_rate;
    audioFile->fx.wBitsPerSample = 16;
    audioFile->fx.nBlockAlign = info.channels * 2;
    audioFile->fx.nAvgBytesPerSec = info.sample_rate * audioFile->fx.nBlockAlign;
    audioFile->fx.cbSize = 0;

    // Total frames (per-channel samples), then convert to interleaved sample count
    const int totalFrames = stb_vorbis_stream_length_in_samples(vorbis);
    const size_t totalSamples = static_cast<size_t>(totalFrames) * info.channels;

    audioFile->dataSize = totalSamples * sizeof(int16_t);
    audioFile->data16 = std::make_unique<int16_t[]>(totalSamples);
    audioFile->buffer = audioFile->data16.get();
    audioFile->sampleCount = static_cast<uint32_t>(totalSamples);

    // IMPORTANT: pass "frames" (per-channel count), not interleaved total samples
    stb_vorbis_get_samples_short_interleaved(vorbis, info.channels,
        audioFile->data16.get(), totalFrames);

    stb_vorbis_close(vorbis);
    return 0;
}
#endif

// =====================================================================
// Top-level loader: returns 1 on success, 0 on failure
// =====================================================================
int WavLoadFileInternal(unsigned char* buffer, int fileSize, SAMPLE* audioFile)
{
    const unsigned char* p = buffer;
    const size_t n = static_cast<size_t>(fileSize);

    // 1) WAV?
    if (n >= 12 && std::memcmp(p, "RIFF", 4) == 0 && std::memcmp(p + 8, "WAVE", 4) == 0) {
        LOG_INFO("Processing as WAV.");
        return (processWaveDataBuffer(p, n, audioFile) == 0) ? 1 : 0;
    }

    // 2) OGG?
#ifdef OGG_DECODE
    if (n >= 4 && std::memcmp(p, "OggS", 4) == 0) {
        LOG_INFO("Processing as OGG...");
        return (processOggDataBuffer(p, n, audioFile) == 0) ? 1 : 0;
    }
#endif

    // 3) MP3? (ID3 or raw MPEG frames)
#ifdef MP3_DECODE
    {
        size_t off = skip_id3v2(p, n);
        const unsigned char* q = (off < n) ? (p + off) : p;
        const size_t m = (off < n) ? (n - off) : n;

        if ((n >= 3 && std::memcmp(p, "ID3", 3) == 0) || looks_like_mpeg_frame(q, m)) {
            LOG_INFO("Processing as MP3...");
            return (processMp3DataBuffer(p, n, audioFile) == 0) ? 1 : 0;
        }
    }
#endif

    LOG_ERROR("Unknown audio format (no RIFF/WAVE, no OggS, no ID3/MPEG sync)");
    return 0;
}
