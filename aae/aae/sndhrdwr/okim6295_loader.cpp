
// 9/7/25 This an independent implementation of the MSM6295 ADPCM

#define NOMINMAX
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "okim6295_loader.h"
#include "wav_resample.h"   // cubic_interpolation_16(...)
#include "wav_filters.h"    // biquad_lowpass_inplace_i16(...)
#include "mixer.h"          // create_sample, mixer_upload_sample16
#include "sys_log.h"            // LOG_INFO/LOG_ERROR, etc.


// OKI MSM6295 4-bit ADPCM decoder
// - Processes HIGH nibble first, then LOW nibble
// - Produces 2 PCM samples per input byte.
// - Internal accumulator is 12-bit signed; we scale << 4 to int16.
void oki_msm6295_decode(const uint8_t* src, size_t src_bytes,
    std::vector<int16_t>& out_pcm)
{
    if (!src || src_bytes == 0) {
        out_pcm.clear();
        return;
    }

    // Index delta by code (bits 0..2)
    static const int index_shift[8] = { -1, -1, -1, -1,  2,  4,  6,  8 };

    // Step-size table (49 entries) used by MSM6295
    static const int step_table[49] = {
        16, 17, 19, 21, 23, 25, 28, 31, 34, 37,
        41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
        107, 118, 130, 143, 157, 173, 190, 209, 230, 253,
        279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
        724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552
    };

    out_pcm.clear();
    out_pcm.reserve(src_bytes * 2);

    // Per-voice ADPCM state (reset at start of each clip, per MAME)
    int signal = 0;  // 12-bit signed accumulator, clamp [-2048, 2047]
    int step_ix = 0;  // 0..48

    for (size_t i = 0; i < src_bytes; ++i)
    {
        const uint8_t b = src[i];

        // HIGH nibble first, then LOW nibble
        for (int k = 0; k < 2; ++k)
        {
            const uint8_t n = (k == 0) ? ((b >> 4) & 0x0F) : (b & 0x0F);

            const int step = step_table[step_ix];

            // Base diff = step >> 3; then add per bits 0..2
            int diff = (step >> 3);
            if (n & 0x01) diff += (step >> 2);
            if (n & 0x02) diff += (step >> 1);
            if (n & 0x04) diff += step;
            if (n & 0x08) diff = -diff;   // sign bit

            // Integrate and clamp to 12-bit
            signal += diff;
            if (signal > 2047) signal = 2047;
            if (signal < -2048) signal = -2048;

            // Update step index using code bits 0..2
            step_ix += index_shift[n & 0x07];
            if (step_ix < 0)  step_ix = 0;
            if (step_ix > 48) step_ix = 48;

            // Scale 12-bit to 16-bit and store
            out_pcm.push_back(static_cast<int16_t>(signal << 4));
        }
    }
}
// ---------- small utilities ----------
static void apply_end_fade(std::vector<int16_t>& pcm, int fade_samples)
{
    if (fade_samples <= 0 || (int)pcm.size() <= fade_samples) return;
    const int N = fade_samples;
    const int total = (int)pcm.size();
    for (int i = 0; i < N; ++i)
    {
        const float g = float(N - i) / float(N);
        int v = int(pcm[total - N + i] * g + (pcm[total - N + i] >= 0 ? 0.5f : -0.5f));
        v = std::clamp(v, -32768, 32767);
        pcm[total - N + i] = (int16_t)v;
    }
}

struct OkiDirEntry { uint32_t start; uint32_t stop; bool valid; };

static OkiDirEntry oki6295_read_entry(const uint8_t* rom, size_t rom_size, uint32_t sample_index)
{
    OkiDirEntry e{ 0,0,false };
    const size_t base = size_t(sample_index) * 8;
    if (base + 6 > rom_size) return e;

    uint32_t start = (uint32_t(rom[base + 0]) << 16) | (uint32_t(rom[base + 1]) << 8) | rom[base + 2];
    uint32_t stop = (uint32_t(rom[base + 3]) << 16) | (uint32_t(rom[base + 4]) << 8) | rom[base + 5];
    start &= 0x3ffff; stop &= 0x3ffff;
    if (start < stop) { e = { start, stop, true }; }
    return e;
}

// Scan the 8-byte directory until we reach data; entry_count = (first_data_offset)/8.
static uint32_t oki6295_detect_entry_count(const uint8_t* rom, size_t rom_size, uint32_t hard_cap = 4096)
{
    const uint32_t max_idx_scan = std::min<uint32_t>(hard_cap, (uint32_t)(rom_size / 8));
    uint32_t min_start = 0xffffffff;

    for (uint32_t idx = 0; idx < max_idx_scan; ++idx)
    {
        const size_t base = size_t(idx) * 8;
        if (base + 6 > rom_size) break;

        uint32_t start = (uint32_t(rom[base + 0]) << 16) | (uint32_t(rom[base + 1]) << 8) | rom[base + 2];
        start &= 0x3ffff;

        if (start != 0 && start < min_start) min_start = start;
        if (min_start != 0xffffffff && base >= min_start) break;
    }

    if (min_start == 0xffffffff || min_start == 0) return 0;
    const uint32_t entry_bytes = std::min<uint32_t>(min_start, (uint32_t)rom_size);
    return entry_bytes / 8;
}

// ---- public entry point ----
int load_okim6295_from_region(const unsigned char* rom_base,
    size_t rom_size,
    int out_rate,
    int oki_clock_hz,
    bool pin7_high,
    uint32_t max_entries_to_scan)
{
    if (!rom_base || !rom_size || out_rate <= 0 || oki_clock_hz <= 0) {
        LOG_ERROR("OKI loader: invalid params (rom=%p size=%zu out=%d clock=%d)",
            (const void*)rom_base, rom_size, out_rate, oki_clock_hz);
        return 0;
    }

    const uint8_t* rom = (const uint8_t*)rom_base;
    const int oki_rate = oki6295_output_rate(oki_clock_hz, pin7_high);

    // Detect number of directory entries
    // (You can pass rom_size/8 here instead of a small constant)
    const uint32_t entry_count = oki6295_detect_entry_count(rom, rom_size, max_entries_to_scan);
    if (entry_count == 0) {
        LOG_ERROR("OKI loader: no directory entries detected");
        return 0;
    }
    LOG_INFO("OKI: detected %u directory entries (data starts at 0x%X)", entry_count, entry_count * 8);

    int loaded = 0;
    for (uint32_t idx = 0; idx < entry_count; ++idx)
    {
        const OkiDirEntry ent = oki6295_read_entry(rom, rom_size, idx);
        if (!ent.valid) continue;

        const uint32_t start = ent.start;
        const uint32_t stop = ent.stop;
        const uint32_t byte_len = stop - start + 1;

        if ((size_t)start + (size_t)byte_len > rom_size) {
            LOG_ERROR("OKI dir %u out-of-bounds (start=%u len=%u size=%zu)", idx, start, byte_len, rom_size);
            continue;
        }

        // 1) Decode OKI ADPCM -> S16 mono @ oki_rate
        std::vector<int16_t> pcm16;
        oki_msm6295_decode(rom + start, byte_len, pcm16);
        if (pcm16.empty()) {
            LOG_ERROR("OKI dir %u decode produced 0 samples", idx);
            continue;
        }

        // 2) De-click before resample, but compute length at the OKI rate (tiny ~2 ms)
        const int fade_samples = std::max(1, out_rate / 500); // ~2 ms at oki_rate //out_rate
        apply_end_fade(pcm16, fade_samples);

        // 3) Resample -> out_rate
        const float ratio = float(out_rate) / float(oki_rate);
        int16_t* resampled = nullptr;
        int32_t  out_samples = 0;
        cubic_interpolation_16(pcm16.data(), (int32_t)pcm16.size(),
            &resampled, &out_samples, ratio);

        if (!resampled || out_samples <= 0) {
            LOG_ERROR("OKI dir %u resample failed", idx);
            if (resampled) delete[] resampled;
            continue;
        }

        // 4) Gentle post-LPF in output domain (optional)
        biquad_lowpass_inplace_i16(resampled, out_samples, (float)out_rate,
            /*fc=*/3200.0f, /*Q=*/0.707f, /*passes=*/1);

        // 5) Register with mixer
        const std::string sname = "oki_" + std::to_string(idx);
        const int samplenum = create_sample(16, false, out_rate, out_samples, sname);
        if (samplenum < 0) {
            LOG_ERROR("OKI dir %u create_sample failed", idx);
            delete[] resampled;
            continue;
        }
        if (mixer_upload_sample16(samplenum, resampled, out_samples, out_rate, false) != 0) {
            LOG_ERROR("OKI dir %u mixer_upload_sample16 failed", idx);
            delete[] resampled;
            continue;
        }

        delete[] resampled;
        ++loaded;
        LOG_INFO("OKI: loaded dir %3u start=%6u stop=%6u bytes=%5u frames=%d (oki=%dHz -> out=%dHz)",
            idx, start, stop, byte_len, out_samples, oki_rate, out_rate);
    }

    LOG_INFO("OKI: loaded %d/%u sample(s)", loaded, entry_count);
    return loaded;
}
