#include "samples.h"
#include "aae_mame_driver.h"


#define MAX_VOICE 16
#define SAMPLE_RAMP_MIN 75
int testvoc = 0;
int testvol;
int game_voice[MAX_VOICE]; //Global Voice For Samples
int sample_vol[MAX_VOICE];

SAMPLE* game_sounds[60];

AUDIOSTREAM* stream[12]; //Global Streaming Sound

void voice_init(int num)
{
	int i;
	for (i = 0; i < MAX_VOICE; i++)
	{
		game_voice[i] = allocate_voice(game_sounds[0]);
		// wrlog("Voice INIT - Position %x",i);
	}
}

void sample_start(int channel, int samplenum, int loop)
{
	voice_stop(game_voice[channel]);
	reallocate_voice(game_voice[channel], game_sounds[samplenum]);

	voice_set_playmode(game_voice[channel], loop);
	voice_ramp_volume(game_voice[channel], SAMPLE_RAMP_MIN, config.mainvol);
	voice_set_position(game_voice[channel], 0);
	voice_start(game_voice[channel]);
	//wrlog("Voice allocated Voice: %d Channel %d",samplenum,channel);
}

void sample_set_freq(int channel, int freq)
{//voice_set_frequency(game_voice[channel], freq);
	voice_sweep_frequency(game_voice[channel], 300, freq);
}

void sample_set_volume(int channel, int volume)
{
	voice_set_volume(game_voice[channel], volume);
}

void sample_adjust(int channel, int mode)
{
	voice_set_playmode(game_voice[channel], mode);
}

void sample_stop(int channel)
{
	voice_set_volume(game_voice[channel], 1);
	voice_stop(game_voice[channel]);
	voice_set_position(game_voice[channel], 0);
}
void sample_end(int channel) //For looped samples to avoid crackle.
{
	voice_ramp_volume(game_voice[channel], SAMPLE_RAMP_MIN, 0);
	//voice_set_playmode(game_voice[channel], PLAYMODE_PLAY);
	//voice_set_volume(game_voice[channel],5);
}
int sample_playing(int channel)
{
	int i = 0;
	i = voice_get_position(game_voice[channel]);
	//wrlog("Position %d",i);
	if (i > -1) { return 1; } //Sample still being played
	else { return 0; }//Sample Stopped
}

void free_samples(void)
{
	int i;

	for (i = 0; i < MAX_VOICE; i++)
	{
		if (sample_playing(i)) { sample_stop(i); }

		if (voice_check(game_voice[i]))

		{
			deallocate_voice(game_voice[i]);//wrlog("Deallocated Voice %d",i);
		}
	}

	for (i = 0; i < num_samples; i++)
	{
		destroy_sample(game_sounds[i]);
		wrlog("Freed sample %d num_samples is %d", i, num_samples);
	}
}

void mute_sound()
{
	set_volume(5, -1);
}

void restore_sound()
{
	set_volume(config.mainvol, -1);
}


void aae_stop_stream(int channel)
{
	stop_audio_stream(stream[channel]);
}

int aae_stream_init(int channel, int rate, int len, int vol)
{
	wrlog("Stream init:channel: %d", channel);
	stream[channel] = play_audio_stream(len, 8, 0, rate, vol, 128);
	return 1;
}

void aae_play_streamed_sample(int channel, unsigned char* data, int len, int freq, int volume)
{
	//	wrlog("Playing stream channel: %d", channel);

	unsigned char* p;

	p = (unsigned char*)get_audio_stream_buffer(stream[channel]);

	if (p)
	{
		for (int i = 0; i < len; i++)
		{
			p[i] = data[i];
			p[i] ^= 0x80;
		}

		free_audio_stream_buffer(stream[channel]);
	}
}