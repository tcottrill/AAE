#pragma once

// -----------------------------------------------------------------------------
// Audio File Loader - WAV / MP3 / OGG
// Unified audio decoding backend for the mixer system. Supports parsing and
// decoding of multiple audio formats into a standard SAMPLE structure used by
// the mixer. Provides in-memory decoding for WAV, MP3, and OGG files with
// optional integration of third-party libraries (stb_vorbis, minimp3).
//
// Features:
//   - Parse and decode standard PCM WAV files (8/16-bit).
//   - Decode MP3 files using the minimp3 library.
//   - Decode OGG Vorbis files using stb_vorbis.
//   - Convert raw file buffers into ready-to-play SAMPLE objects.
//   - Automatic detection of file type by header inspection.
//   - Safe memory handling using modern C++ smart pointers.
//   - Seamless integration with the XAudio2-based mixer system.
//
// Dependencies:
//   - stb_vorbis (for OGG decoding)
//   - minimp3 (for MP3 decoding)
//   - Microsoft XAudio2 (for audio playback)
//   - Logging and mixer headers
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


#ifndef WAVFILE_H
#define WAVFILE_H

#include <cstdint>
#include "mixer.h"

int WavLoadFileInternal(unsigned char* buffer, int fileSize, SAMPLE *audioFile);

#endif