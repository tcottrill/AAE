#pragma once

#include <string>
#include <cstdint>
#include <xaudio2.h>
#include "wavfile.h"

struct CHANNEL
{
	IXAudio2SourceVoice* voice = nullptr;
	XAUDIO2_BUFFER buffer = { 0 };
	int state;                      // state of the sound ?? Playing/Stopped
	unsigned int pos;
	int loaded_sample_num;
	int id;
	int looping;
	double vol;
	int stream_type;
	bool isAllocated;
	bool isPlaying;
	bool isReleased;
	int frequency;
	int volume;
	int pan;

};


typedef struct
{
	WAVEFORMATEX fx;
	int channels;		     //<  Number of channels
	unsigned short sampleRate;	/**<  Sample rate */
	unsigned long sampleCount;	/**<  Sample count */
	unsigned long dataLength;	/**<  Data length */
	int16_t bitPerSample;	/**<  The bit rate of the WAV */
	int state;                  //Sound loaded or sound empty
	int num;
	std::string name;
	union {
		uint8_t *u8;      /* data for 8 bit samples */
		int16_t *u16;    /* data for 16 bit samples */
		void *buffer;           /* generic data pointer to the actual wave data*/
	} data;

}SAMPLE;

short Make16bit(uint8_t sample);
void mute_audio();
void restore_audio();
void pause_audio();
void resume_audio();
void mixer_init(int rate, int fps);
void mixer_update();
void mixer_end();
int load_sample(char *archname, char *filename, bool force_resample=true);
void sample_stop(int chanid);
void sample_start(int chanid, int samplenum, int loop);
void sample_set_position(int chanid, int pos);
void sample_set_volume(int chanid, int volume);
void sample_set_freq(int chanid, int freq);
int sample_playing(int chanid);
void sample_end(int chanid);
void sample_remove(int samplenum);
//
void sample_start_mixer(int chanid, int samplenum, int loop);
void sample_end_mixer(int chanid);
void sample_stop_mixer(int chanid);
void sample_set_volume_mixer(int chanid, int volume);
int sample_get_volume_mixer(int chanid);
int sample_playing_mixer(int chanid);

//Streaming audio functions added on.
void stream_start(int chanid, int stream, int bits, int frame_rate);
void stream_stop(int chanid, int stream);
void stream_update(int chanid, short *data);
void stream_update(int chanid, unsigned char* data);
void resample_wav_8(int sound_num, int new_freq);
void resample_wav_16(int sound_num, int new_freq);
int create_sample(int bits, bool is_stereo, int freq, int len);






