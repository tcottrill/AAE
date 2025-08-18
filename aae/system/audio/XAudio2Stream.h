/* =============================================================================
 * File: XAudio2Stream.h
 * Overview:
 *   Public API for a simple XAudio2-based streaming output path. Provides
 *   initialization, per-frame submission, and teardown helpers for a stereo
 *   16-bit PCM ring-buffer workflow suitable for emulators and games.
 *
 * Responsibilities:
 *   - Expose xaudio2_init(rate, fpsExact) to configure the device and create
 *     a mastering voice and a streaming source voice.
 *   - Expose GetNextBuffer() to retrieve the current interleaved PCM write
 *     pointer (internal ring slot).
 *   - Expose xaudio2_update(ptr, byteCount) to submit mixed audio for the
 *     current frame (supports variable-length payloads in BYTES).
 *   - Expose xaudio2_stop() to cleanly destroy voices, free buffers, and
 *     uninitialize COM if this module initialized it.
 *
 * Data Format:
 *   - Interleaved S16 stereo (2 channels, 16-bit), i.e., 4 bytes per frame.
 *   - The mixer writes into the buffer returned by GetNextBuffer(), then calls
 *     xaudio2_update(..., frames * 4).
 *
 * Call Order (Typical):
 *   1) xaudio2_init(sampleRate, fpsExact);
 *   2) Per frame:
 *        BYTE* dst = GetNextBuffer();
 *        // mix into 'dst' ...
 *        xaudio2_update(dst, frames * 4);
 *   3) On shutdown: xaudio2_stop();
 *
 * Notes:
 *   - This header intentionally exposes only the minimal surface needed by the
 *     mixer/game loop. Implementation details (ring size, COM init/uninit, etc.)
 *     are in XAudio2Stream.cpp.
 *
 * Dependencies:
 *   - Requires XAudio2 (xaudio2redist) and Windows types (BYTE, DWORD, HRESULT).
 *   - Designed to be used in conjunction with mixer.h/cpp.
 *
* ---------------------------------------------------------------------------
  * License (GPLv3):
 *   This file is part of GameEngine Alpha.
 *
 *   <Project Name> is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   <Project Name> is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with GameEngine Alpha.  If not, see <https://www.gnu.org/licenses/>.
 *
 *   Copyright (C) 2022-2025  Tim Cottrill
 *   SPDX-License-Identifier: GPL-3.0-or-later
 * =============================================================================
 */

#ifndef XAUDIO2STREAM_H
#define XAUDIO2STREAM_H

#include <xaudio2redist.h>
#include <vector>
#include <thread>
#include <mutex>
#include <algorithm>

HRESULT xaudio2_init(int rate, int fps);
HRESULT xaudio2_update(BYTE* buffera, DWORD bufferLength);
void xaudio2_stop();
BYTE* GetNextBuffer();

#endif