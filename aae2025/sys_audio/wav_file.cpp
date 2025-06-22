// Mixercode 2 "The 2nd Version", 2025 Tim Cottrill
// Free for any use. See Unlicense license
// Modify this code however you see fit, but if you make it better please send me the updates. :)
// This is revision 11. 4/13/25
// Mixercode 2 - Updated for modern C++ SAMPLE structure
// Unlicense - Free for any use

//#define OGG_DECODE
//#define MP3_DECODE

#include "wav_file.h"
#include <xaudio2redist.h>
#include "log.h"
#include <cstring>
#include <memory>

#ifdef OGG_DECODE
#include "stb_vorbis.h"
#endif

#ifdef MP3_DECODE
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
#include "minimp3_ex.h"
#endif

#pragma warning(disable : 4018)

// WAV
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
    while (pos + 8 <= bufferSize) {
        char chunkID[5] = {};
        std::memcpy(chunkID, buffer + pos, 4);
        pos += 4;

        uint32_t chunkSize;
        std::memcpy(&chunkSize, buffer + pos, sizeof(uint32_t));
        pos += 4;

        if (std::strncmp(chunkID, "fmt ", 4) == 0) {
            std::memcpy(&audioFile->fx.wFormatTag, buffer + pos, sizeof(uint16_t));
            std::memcpy(&audioFile->fx.nChannels, buffer + pos + 2, sizeof(uint16_t));
            std::memcpy(&audioFile->fx.nSamplesPerSec, buffer + pos + 4, sizeof(uint32_t));
            std::memcpy(&audioFile->fx.nAvgBytesPerSec, buffer + pos + 8, sizeof(uint32_t));
            std::memcpy(&audioFile->fx.nBlockAlign, buffer + pos + 12, sizeof(uint16_t));
            std::memcpy(&audioFile->fx.wBitsPerSample, buffer + pos + 14, sizeof(uint16_t));
            pos += chunkSize;
        }
        else if (std::strncmp(chunkID, "data", 4) == 0) {
            audioFile->dataSize = chunkSize;

            if (audioFile->fx.wBitsPerSample == 8) {
                audioFile->data8 = std::make_unique<uint8_t[]>(chunkSize);
                std::memcpy(audioFile->data8.get(), buffer + pos, chunkSize);
                audioFile->buffer = audioFile->data8.get();
                audioFile->sampleCount = chunkSize;
            }
            else if (audioFile->fx.wBitsPerSample == 16) {
                size_t sampleCount = chunkSize / sizeof(int16_t);
                audioFile->data16 = std::make_unique<int16_t[]>(sampleCount);
                std::memcpy(audioFile->data16.get(), buffer + pos, chunkSize);
                audioFile->buffer = audioFile->data16.get();
                audioFile->sampleCount = static_cast<uint32_t>(sampleCount);
            }
            else {
                LOG_INFO("Unsupported bit depth: %d", audioFile->fx.wBitsPerSample);
                return -1;
            }

            pos += chunkSize;
        }
        else {
            pos += chunkSize;
        }
    }

    audioFile->fx.cbSize = 0;
    return 0;
}

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

    size_t sampleCount = info.samples;
    audioFile->dataSize = sampleCount * sizeof(int16_t);
    audioFile->data16 = std::make_unique<int16_t[]>(sampleCount);
    std::memcpy(audioFile->data16.get(), info.buffer, audioFile->dataSize);
    audioFile->buffer = audioFile->data16.get();
    audioFile->sampleCount = static_cast<uint32_t>(sampleCount);

    free(info.buffer);
    return 0;
}
#endif

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

    size_t sampleCount = stb_vorbis_stream_length_in_samples(vorbis) * info.channels;
    audioFile->dataSize = sampleCount * sizeof(int16_t);
    audioFile->data16 = std::make_unique<int16_t[]>(sampleCount);
    audioFile->buffer = audioFile->data16.get();
    audioFile->sampleCount = static_cast<uint32_t>(sampleCount);

    stb_vorbis_get_samples_short_interleaved(vorbis, info.channels,
        audioFile->data16.get(), static_cast<int>(sampleCount));
    stb_vorbis_close(vorbis);
    return 0;
}
#endif

int WavLoadFileInternal(unsigned char* buffer, int fileSize, SAMPLE* audioFile)
{
    if (fileSize >= 12 && std::memcmp(buffer, "RIFF", 4) == 0 && std::memcmp(buffer + 8, "WAVE", 4) == 0) {
        LOG_INFO("Processing as WAV...");
        if (processWaveDataBuffer(buffer, fileSize, audioFile) != 0) {
            LOG_ERROR("WAV decode failed.");
            return 0;
        }
    }

#ifdef MP3_DECODE
    else if (fileSize >= 3 && std::memcmp(buffer, "ID3", 3) == 0) {
        LOG_INFO("Processing as MP3...");
        if (processMp3DataBuffer(buffer, fileSize, audioFile) != 0) {
            LOG_ERROR("MP3 decode failed.");
            return 0;
        }
    }
#endif

#ifdef OGG_DECODE
    else {
        LOG_INFO("Processing as OGG...");
        if (processOggDataBuffer(buffer, fileSize, audioFile) != 0) {
            LOG_ERROR("OGG decode failed.");
            return 0;
        }
    }
#endif

    return 1;
}
