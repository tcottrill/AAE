//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
// =============================================================================
// wav_writer.h
// Streaming 16-bit PCM stereo WAV file writer for -wavwrite session capture.
//
// Platform-neutral: plain stdio, no audio API types. The RIFF/fmt/data
// header layout matches save_sample_to_buffer (mixer.cpp). flush() patches
// the two RIFF size fields in place so a crash mid-session still leaves a
// playable file up to the last flush.
//
// NOT thread-safe: after open(), exactly one thread (the capture writer
// thread) may call write()/flush()/close().
// =============================================================================
#pragma once

#include <cstdint>
#include <cstdio>

class WavWriter {
public:
	WavWriter() = default;
	~WavWriter() { close(); }

	WavWriter(const WavWriter&) = delete;
	WavWriter& operator=(const WavWriter&) = delete;

	// Creates/truncates the file and writes the 44-byte header with
	// placeholder sizes. Returns false (and logs) on failure.
	bool open(const char* path, int rateHz);

	// Appends interleaved L/R frames. frameCount = number of stereo frames
	// (2 int16 values each). No-op when the file is not open.
	void write(const int16_t* stereoFrames, size_t frameCount);

	// Patches the RIFF chunk size and data chunk size fields, then flushes
	// stdio buffers. Cheap enough to call every drain pass (~10 Hz).
	void flush();

	// flush() + fclose(). Idempotent.
	void close();

	bool is_open() const { return m_file != nullptr; }
	uint32_t data_bytes() const { return m_dataBytes; }

private:
	FILE*    m_file = nullptr;
	uint32_t m_dataBytes = 0;
};
