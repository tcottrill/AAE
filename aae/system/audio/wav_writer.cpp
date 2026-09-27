//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
// =============================================================================
// wav_writer.cpp
// See wav_writer.h. Header layout mirrors save_sample_to_buffer (mixer.cpp):
// RIFF <size> WAVE / fmt (16-byte PCM block) / data <size>.
// =============================================================================
#include "wav_writer.h"
#include "sys_log.h"

#include <cstring>

namespace {
// File offsets of the two size fields flush() patches.
constexpr long kRiffSizeOffset = 4;   // RIFF chunk size = 36 + dataBytes
constexpr long kDataSizeOffset = 40;  // data chunk size = dataBytes
constexpr uint32_t kHeaderBytes = 44;
}

bool WavWriter::open(const char* path, int rateHz)
{
	close();

	if (!path || !path[0] || rateHz <= 0) {
		LOG_ERROR("WavWriter::open: invalid args (path=%s rate=%d)",
		          path ? path : "(null)", rateHz);
		return false;
	}

#ifdef _WIN32
	if (fopen_s(&m_file, path, "wb") != 0) m_file = nullptr;
#else
	m_file = std::fopen(path, "wb");
#endif
	if (!m_file) {
		LOG_ERROR("WavWriter::open: cannot create '%s'", path);
		return false;
	}

	// 16-bit PCM stereo header with placeholder sizes (patched by flush()).
	const uint16_t formatTag  = 1;  // WAVE_FORMAT_PCM
	const uint16_t channels   = 2;
	const uint32_t rate       = static_cast<uint32_t>(rateHz);
	const uint16_t bits       = 16;
	const uint16_t blockAlign = channels * bits / 8;             // 4
	const uint32_t avgBytes   = rate * blockAlign;
	const uint32_t fmtSize    = 16;
	const uint32_t zero       = 0;

	uint8_t header[kHeaderBytes];
	uint8_t* p = header;
	auto put = [&p](const void* src, size_t n) {
		std::memcpy(p, src, n); p += n;
	};
	put("RIFF", 4); put(&zero, 4); put("WAVE", 4);
	put("fmt ", 4); put(&fmtSize, 4);
	put(&formatTag, 2); put(&channels, 2); put(&rate, 4);
	put(&avgBytes, 4); put(&blockAlign, 2); put(&bits, 2);
	put("data", 4); put(&zero, 4);

	if (std::fwrite(header, 1, kHeaderBytes, m_file) != kHeaderBytes) {
		LOG_ERROR("WavWriter::open: header write failed for '%s'", path);
		std::fclose(m_file);
		m_file = nullptr;
		return false;
	}

	m_dataBytes = 0;
	LOG_INFO("WavWriter: recording 16-bit stereo %u Hz to '%s'", rate, path);
	return true;
}

void WavWriter::write(const int16_t* stereoFrames, size_t frameCount)
{
	if (!m_file || !stereoFrames || frameCount == 0) return;

	const size_t bytes = frameCount * 2 * sizeof(int16_t);
	if (std::fwrite(stereoFrames, 1, bytes, m_file) != bytes) {
		// Disk full / device gone: stop recording rather than spam.
		LOG_ERROR("WavWriter::write: write failed after %u data bytes - closing",
		          m_dataBytes);
		std::fclose(m_file);
		m_file = nullptr;
		return;
	}
	m_dataBytes += static_cast<uint32_t>(bytes);
}

void WavWriter::flush()
{
	if (!m_file) return;

	const uint32_t riffSize = 36 + m_dataBytes;
	// Patch sizes in place, then return to the append position.
	if (std::fseek(m_file, kRiffSizeOffset, SEEK_SET) == 0)
		std::fwrite(&riffSize, 4, 1, m_file);
	if (std::fseek(m_file, kDataSizeOffset, SEEK_SET) == 0)
		std::fwrite(&m_dataBytes, 4, 1, m_file);
	std::fseek(m_file, 0, SEEK_END);
	std::fflush(m_file);
}

void WavWriter::close()
{
	if (!m_file) return;
	flush();
	std::fclose(m_file);
	m_file = nullptr;
	LOG_INFO("WavWriter: closed file (%u data bytes)", m_dataBytes);
}
