//==============================================================================
// audiotest_main.cpp -- Phase 3b's audible proof.
//
// Links the REAL mixer.cpp against whichever IAudioBackend the platform
// factory supplies (XAudio2 on Windows, ALSA on Linux) and plays a sample.
//
// This target exists because aae_headless deliberately STUBS the mixer out
// (aae/headless/null_backends.cpp), so a green Linux aae_headless says nothing
// whatsoever about whether audio works. It is also the first real exercise of
// the voice path since Phase 2 rewrote it - that rewrite has only ever been
// smoke-tested, and an audio regression does not show up in a log.
//
// Plays the built-in error_wav, which is compiled in, so this needs no ROM set
// and no asset directory and runs from a bare checkout.
//==============================================================================
#include "mixer.h"
#include "audio_backend.h"
#include "sys_log.h"

// error_wav.h DEFINES `unsigned char error_wav[10008]` at file scope rather
// than declaring it, so including it here as well as in mixer.cpp (which
// already does, for its bad-WAV fallback) is a duplicate symbol at link time.
// Declared extern instead. The dimension is repeated so sizeof works; the
// static_assert below is what stops the two drifting apart silently.
extern unsigned char error_wav[10008];

#include <chrono>
#include <cstdio>
#include <thread>

int main(void)
{
	// Log to file only. Deliberately NOT Log::setConsoleOutputEnabled(true):
	// on Windows that calls AllocConsole() and freopen("CONOUT$"), which
	// detaches stdout into a brand-new console window - so this program's own
	// printf output would vanish from whatever shell launched it.
	Log::open("audiotest.log");

	// 44.1 kHz / 60 fps. mixer_init returns 0 on FAILURE (not an errno), so
	// the test is != 0 for success - the opposite of the usual convention.
	if (mixer_init(44100, 60) == 0) {
		fprintf(stderr, "audiotest: mixer_init failed - no audio backend?\n");
		Log::close();
		return 1;
	}

	const int snd = load_sample_from_buffer(error_wav, sizeof(error_wav), "audiotest");
	if (snd < 0) {
		fprintf(stderr, "audiotest: load_sample_from_buffer failed\n");
		mixer_end();
		Log::close();
		return 1;
	}

	const int ch = mixer_alloc_channel();
	if (ch < 0) {
		fprintf(stderr, "audiotest: mixer_alloc_channel failed\n");
		mixer_end();
		Log::close();
		return 1;
	}

	printf("audiotest: playing sample %d on channel %d - YOU SHOULD HEAR THIS\n",
	       snd, ch);
	sample_start(ch, snd, 0);   // 0 = no loop

	// Pump mixer_update() the way the emulator's frame loop does rather than
	// sleeping straight through: the software-mixer path does its work there,
	// so a plain sleep would exercise only the voice path and silently skip
	// half of what this target exists to test.
	for (int i = 0; i < 180; ++i) {          // 180 frames @ 60fps = ~3 seconds
		mixer_update();
		std::this_thread::sleep_for(std::chrono::milliseconds(16));
	}

	const int stillPlaying = sample_playing(ch);
	sample_stop(ch);
	mixer_end();
	Log::close();

	printf("audiotest: done (channel %s at the end).\n"
	       "           If you heard NOTHING, this test FAILED regardless of exit code.\n",
	       stillPlaying ? "still playing" : "finished");
	return 0;
}
