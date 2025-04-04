// Still TODO: Add per sample volume control as well as main volume.
// Mixer code example in C/C++ Copyright 2022-2025 TC.
// This may be an example of how not to do things, so be warned!
// This is a work in progress..

// This code mixes one or more MONO 8bit or 16 bit WAV samples and outputs it at the desired rate using Xaudio2 WITHOUT using a callback.
// See example for usage. Optionally it will allow you to change the sample rate of a loaded wav file to match the output sample rate.
// This code is not optimal, but it should compile for Windows 7 through 11 with no issues using Xaudio 2.9.

// Error handling macro
#define HR(hr) if (FAILED(hr)) { wrlog("Error at line %d: HRESULT = 0x%08X\n", __LINE__, hr);  }

#define NOMINMAX
#include "framework.h"
#include "mixer.h"
#include "wavfile.h"
#include "aae_fileio.h"
#include "XAudio2Stream.h"
#include <cstdint>
#include <list>
#include "wav_resample.h"
#include "helper_functions.h"
#include "dbvolume.h"

#include <thread>
#include <mutex>
#include <assert.h>
#include <map>
#include <memory>

extern IXAudio2* pXAudio2;

using namespace std;
//#include <math.h>
//#include <limits>
//#include <algorithm>    // std::max
//#include <exception> //static_assert
static int SYS_FREQ = 44100;
static int BUFFER_SIZE = 0;

//#define SMP_START 0x2c
#define MAX_CHANNELS   16  //0-8 samples, 9-16 streaming
#define MAX_SOUNDS     255 // max number of sounds loaded in system at once 128 + 2 overloaded for streams

#define SOUND_NULL     0
#define SOUND_LOADED   1
#define SOUND_PLAYING  2
#define SOUND_STOPPED  3
#define SOUND_PCM      4
#define SOUND_STREAM   5

static int sound_paused = 0;
static int v_mute_audio = 0;

CHANNEL channel[MAX_CHANNELS];
SAMPLE sound[MAX_SOUNDS];
//List of actively playing samples
std::list<int> audio_list;
//List of loaded samples, so we can track, call by name, and delete when done;
std::vector<SAMPLE> lsamples;

static mutex audioMutex;

unsigned char Make8bit(int16_t sample)
{
	sample >>= 8;  // drop the low 8 bits
	sample ^= 0x80;  // toggle the sign bit
	return (sample & 0xFF);
}

short Make16bit(uint8_t sample)
{
	short sample16 = (int16_t)(sample - 0x80) << 8;
	return sample16;
}

bool ends_with(const std::string& s, const std::string& ending)
{
	return (s.size() >= ending.size()) && equal(ending.rbegin(), ending.rend(), s.rbegin());
}

void resample_wav_8(int sound_num, int new_freq)
{
	int input_size = sound[sound_num].sampleCount;

	int output_size = (int)((float)input_size * new_freq / sound[sound_num].sampleRate);
	uint8_t* output_data = (uint8_t*)malloc(output_size);

	linear_interpolation_8(sound[sound_num].data.u8, output_data, input_size, output_size);

	wrlog("Resampling 8 bit Sample #%d", sound_num);

	free(sound[sound_num].data.buffer);
	sound[sound_num].sampleRate = new_freq;
	sound[sound_num].dataLength = output_size;
	sound[sound_num].sampleCount = output_size;
	sound[sound_num].data.buffer = output_data;

	wrlog("Resample: Samplerate #: %d", sound[sound_num].sampleRate);
	wrlog("Resample: Length #: %d", sound[sound_num].dataLength);
	wrlog("Resample: BPS #: %d", sound[sound_num].sampleRate);
	wrlog("Resample: Samplecount #: %d", sound[sound_num].sampleCount);
}

void resample_wav_16(int sound_num, int new_freq)
{
	int16_t* output_data_16;
	int32_t output_samples;
	float resample_ratio = (float)new_freq / (float)sound[sound_num].sampleRate;

	wrlog("Resampling 16 bit Sample #%d", sound_num);

	linear_interpolation_16(sound[sound_num].data.u16, sound[sound_num].sampleCount, &output_data_16, &output_samples, resample_ratio);

	free(sound[sound_num].data.buffer);
	sound[sound_num].sampleRate = new_freq;
	sound[sound_num].dataLength = output_samples * 2;
	sound[sound_num].sampleCount = output_samples;
	sound[sound_num].data.buffer = output_data_16;

	wrlog("Resample: Samplerate #: %d", Wave.sampleRate);
	wrlog("Resample: Length #: %d", sound[sound_num].dataLength);
	wrlog("Resample: BPS #: %d", sound[sound_num].sampleRate);
	wrlog("Resample: Samplecount #: %d", sound[sound_num].sampleCount);
}

int load_sample(char* archname, char* filename, bool force_resample)
{
	int	sound_id = -1;      // id of sound to be loaded
	int  index;               // looping variable
	// step one: are there any open id's ?
	for (index = 0; index < MAX_SOUNDS; index++)
	{
		// make sure this sound is unused
		if (sound[index].state == SOUND_NULL)
		{
			sound_id = index;
			break;
		} // end if
	} // end for index
	  // did we get a free id? If not,fail.
	if (sound_id == -1) {
		wrlog("No free sound id's for sample %s", filename); return(-1);
	}
	//SOUND
	wrlog("Loading file %s with sound id %d", filename, sound_id);

	unsigned char* sample_temp;
	HRESULT result;
	// TODO: LOAD FILE - Please add some error handling here!!!!!!!!!
	if (archname)
	{
		sample_temp = load_zip_file(archname, filename);
		//Create Wav data
		result = WavFileLoadInternal(sample_temp, (int)get_last_zip_file_size());
	}
	else
	{
		sample_temp = load_file(filename);
		//Create Wav data
		result = WavFileLoadInternal(sample_temp, get_last_file_size());
		if (result == 0)
		{
			wrlog("Error, check loaded file format.");
			if (sample_temp) {
				free(sample_temp);
			}
			return -1;
		}
	}

	//If sample loaded successfully proceed!
	// 
	// Resample BEFORE setting all the parameters!
	// 
	// TODO: Do I still need to do all this? Can I edit the code to use the above? Think about it.
	sound[sound_id].channels = Wave.channels;
	sound[sound_id].sampleRate = Wave.sampleRate;
	sound[sound_id].bitPerSample = Wave.bitPerSample;
	sound[sound_id].dataLength = Wave.dataLength;
	sound[sound_id].sampleCount = Wave.sampleCount;
	sound[sound_id].state = SOUND_LOADED;
	sound[sound_id].name = filename;
	sound[sound_id].name = remove_extension(base_name(sound[sound_id].name));

	sound[sound_id].data.buffer = (unsigned char*)malloc(Wave.dataLength);
	// Here Wave.data is sample_temp
	memcpy(sound[sound_id].data.buffer, Wave.data, Wave.dataLength);

	// Either way, we don't need the original data any more.
	free(sample_temp);
	
	//If we're at the wrong sample rate, resample to match the current rate if requested
	if (sound[sound_id].sampleRate != SYS_FREQ && force_resample)
	{
		// The code currently doesn't handle resampling of stereo wav files. 
		if (sound[sound_id].channels == 1)
		{
			if (sound[sound_id].bitPerSample == 8)
			{ resample_wav_8(sound_id, SYS_FREQ); }
			else { resample_wav_16(sound_id, SYS_FREQ); }
		}
		else ("Warning, sample needs to be interpolated, but it's the wrong number of channels, fix.!");
	}
	
	// set rate and size in data structure for the non-mixed samples
	sound[sound_id].fx.wFormatTag = WAVE_FORMAT_PCM;
	sound[sound_id].fx.nChannels = sound[sound_id].channels;
	sound[sound_id].fx.nSamplesPerSec = sound[sound_id].sampleRate;
	sound[sound_id].fx.wBitsPerSample = sound[sound_id].bitPerSample;
	sound[sound_id].fx.nBlockAlign = sound[sound_id].channels * sound[sound_id].bitPerSample / 8;
	sound[sound_id].fx.nAvgBytesPerSec = sound[sound_id].fx.nBlockAlign * sound[sound_id].sampleRate;
	sound[sound_id].fx.cbSize = 0;
	
	// Debug Out:
	wrlog("File %s loaded with sound id: %d and state is: %d", filename, sound_id, sound[sound_id].state);
	wrlog("Loading WAV #: %d", sound_id);
	wrlog("Stored filename is %s", sound[sound_id].name.c_str());
	wrlog("Channels #: %d", Wave.channels);
	wrlog("Samplerate #: %d", Wave.sampleRate);
	wrlog("Length #: %d", Wave.dataLength);
	wrlog("BPS #: %d", Wave.bitPerSample);
	wrlog("Samplecount #: %d", Wave.sampleCount);

	//Add this sample to the loaded samples list
	lsamples.push_back(sound[sound_id]);
	//Return Sound ID
	wrlog("Loaded sound success");
	return(sound_id);
}

void mixer_init(int rate, int fps)
{
	int i = 0;
	BUFFER_SIZE = rate / fps;
	SYS_FREQ = rate;

	wrlog("Mixer init, BUFFER SIZE = %d, freq %d framerate %d", BUFFER_SIZE, rate, fps);

	// Initialize the xaudio2 backend at the correct rate.
	xaudio2_init(rate, fps);

	//Clear and init Sample Channels
	for (i = 0; i < MAX_CHANNELS; i++)
	{
		channel[i].loaded_sample_num = -1;
		channel[i].state = SOUND_STOPPED;
		channel[i].looping = 0;
		channel[i].pos = 0;
		channel[i].vol = 1.0;
	}
	//Set all samples to empty for start
	for (i = 0; i < MAX_SOUNDS; i++)
	{
		sound[i].state = SOUND_NULL;
	}

	sound_paused = 0;
	v_mute_audio = 0;
}

void mixer_update()
{
	int32_t smix = 0;    //Sample mix buffer
	int32_t fmix = 0;   // Final sample mix buffer

	BYTE* soundbuffer = GetNextBuffer();

	for (int i = 0; i < BUFFER_SIZE; i++)
	{
		fmix = 0; //Set mix buffer to zero (silence for 16 bit audio)

		if (!sound_paused) // Other option, keep playing but set fmix to zero at end. Maybe better option to prevent buffer overflow?
		{
			for (std::list<int>::iterator it = audio_list.begin(); it != audio_list.end(); ++it)
			{
				SAMPLE p = sound[channel[*it].loaded_sample_num]; //To shorten

				if (channel[*it].pos >= p.sampleCount) //Are we at the end?
				{
					if (channel[*it].looping == 0) {
						channel[*it].state = SOUND_STOPPED; audio_list.erase(it);
					} //If it's not looping, remove it.
					channel[*it].pos = 0;  //Otherwise, rewind to the beginning, or if it's a stream, ready to load more data;
				}
				// 16 bit mono
				if (p.bitPerSample == 16)
				{
					smix = (short)p.data.u16[channel[*it].pos];
					smix = lround(smix = static_cast<int32_t> (smix * channel[*it].vol));
					channel[*it].pos += p.channels;
				}
				// 8 bit mono
				else if (p.bitPerSample == 8)
				{
					smix = (short)(((p.data.u8[channel[*it].pos] - 128) << 8));
					smix = lround(smix = static_cast<int32_t> (smix * channel[*it].vol));
					channel[*it].pos += p.channels;
				}

				smix = static_cast<int32_t> (smix * .70); //Reduce volume to avoid clipping. This number can/should vary depending on the samples.
				fmix = fmix + smix;  //Mix here.
			}
		}

		if (v_mute_audio) fmix = 0; // Mute Volume

		if (fmix) //If the mix value is zero (nothing playing) , skip all this.
		{
			//Clip samples
			if (fmix > INT16_MAX) { fmix = INT16_MAX; }
			if (fmix < INT16_MIN) { fmix = INT16_MIN; }
		}
		soundbuffer[2 * i] = fmix & 0xff;
		soundbuffer[2 * i + 1] = (fmix >> 8) & 0xff;
	}

	xaudio2_update(soundbuffer, BUFFER_SIZE);
}

void mixer_end()
{
	xaudio2_stop();

	for (std::size_t i = 0; i < lsamples.size(); ++i)

	{
		if (sound[i].data.buffer)
		{
			free(sound[i].data.buffer);
			wrlog("Freeing sample #%d named %s", i, sound[i].name.c_str());
		}
	}
}

void sample_stop(int chanid)
{
	if (channel[chanid].isPlaying)
	{
		//channel[chanid].voice->SetVolume(0);
		channel[chanid].voice->Stop();
		channel[chanid].voice->FlushSourceBuffers();
		channel[chanid].isPlaying = false;

		channel[chanid].state = SOUND_STOPPED;
		channel[chanid].looping = 0;
		channel[chanid].pos = 0;
	}
}

static void SetPan(IXAudio2SourceVoice* voice, float pan)
{
	float left = 0.5f - pan / 2;
	float right = 0.5f + pan / 2;

	float outputMatrix[8] = { 0 };
	int nChannels = 0;

	outputMatrix[0] = left;
	outputMatrix[1] = right;
	nChannels = 2;

	voice->SetOutputMatrix(nullptr, 1, nChannels, outputMatrix);
}

void sample_start(int chanid, int samplenum, int loop)
{
	//First check that it's a valid sample!
	if (!sound[samplenum].state == SOUND_LOADED)
	{
		wrlog("error, attempting to play invalid sample on channel %d state: %d", chanid, channel[chanid].state);
		return;
	}


	/*
	if (channel[chanid].state == SOUND_PLAYING)
	{
		wrlog("error, sound already playing on this channel %d state: %d", chanid, channel[chanid].state);
		return;
	}
	
	lock_guard<mutex> lock(audioMutex);

	if (channel[chanid].isAllocated && channel[chanid].isReleased)
	{
		XAUDIO2_VOICE_STATE state;

		channel[chanid].voice->GetState(&state);//, XAUDIO2_VOICE_NOSAMPLESPLAYED);

		if (state.BuffersQueued == 0)
		{
			sample_stop(chanid);

			channel[chanid].isAllocated = false;
		}
	}
	*/
	// Start the sample playing
	
	if (channel[chanid].voice)
	{
		wrlog("Destroying Source Voice!, chan %d", chanid);
		channel[chanid].voice->Stop();
		channel[chanid].voice->FlushSourceBuffers();
		channel[chanid].voice->DestroyVoice();
		channel[chanid].voice = NULL;
	}

	if (!channel[chanid].voice)
	{
		if (FAILED(pXAudio2->CreateSourceVoice(&channel[chanid].voice, &sound[samplenum].fx, 0, 16.0f)))
		{
			CoUninitialize();
			wrlog("FAILED to create source voice %d");
		}
		else
			wrlog("Creating Source Voice!, chan %d", chanid);
	}

	channel[chanid].isAllocated = true;
	channel[chanid].isReleased = false;
	channel[chanid].isPlaying = true;
	channel[chanid].looping = loop;
	channel[chanid].volume = 255;
	channel[chanid].pan = 128;

	//samplenum
	CHANNEL& v = channel[chanid];
	
	channel[chanid].frequency = sound[samplenum].fx.nSamplesPerSec;
	channel[chanid].buffer.AudioBytes = sound[samplenum].dataLength;
	channel[chanid].buffer.pAudioData = (BYTE*)sound[samplenum].data.buffer;
	channel[chanid].buffer.LoopCount = v.looping ? XAUDIO2_LOOP_INFINITE : 0;
	channel[chanid].voice->SubmitSourceBuffer(&channel[chanid].buffer);
	float frequencyRatio = static_cast<float>((float) channel[chanid].frequency / (float) sound[samplenum].sampleRate);
	channel[chanid].voice->SetFrequencyRatio(frequencyRatio);
	channel[chanid].voice->SetVolume((float)channel[chanid].volume / 255.0f);
	SetPan(channel[chanid].voice, (float)(channel[chanid].pan - 128) / 128.0f);

	HR(channel[chanid].voice->Start());

	channel[chanid].isPlaying = true;

	wrlog("Playing Sample #%d :%s", samplenum, sound[samplenum].name.c_str());
}

int sample_get_position(int chanid)
{
	return channel[chanid].pos;
}

// This goes from 0 to 100, with 100 being the original level.
void sample_set_volume(int chanid, int volume)
{
	channel[chanid].vol = db_volume[volume];
	//wrlog("Setting channel %i to with volume %i setting bvolume %f", chanid, volume, channel[chanid].vol);
};

int sample_get_volume(int chanid)
{
	return (int)(channel[chanid].vol * 100);
};

void sample_set_position(int chanid, int pos)
{
	// Not needed with this code. ?
};

void sample_set_freq(int chanid, int freq)
{
	if (channel[chanid].isPlaying)
	{
		float frequencyRatio = static_cast<float>( (float) freq/(float)channel[chanid].frequency);
		channel[chanid].voice->SetFrequencyRatio(frequencyRatio);
	}
};

int sample_playing(int chanid)
{
	XAUDIO2_VOICE_STATE state;

	if (channel[chanid].voice)
	{

		channel[chanid].voice->GetState(&state);

		if (state.BuffersQueued == 0)
		{
			wrlog("Check sample #%d playing, returning false", chanid);
			return 0;
		}
		else
		{
			wrlog("Check sample #%d playing, returning true", chanid);
			return 1;
		}
	}
	wrlog("Check sample #%d playing, returning false, not allocated", chanid);
	return 0;
}

void sample_end(int chanid)
{
	channel[chanid].looping = 0;
}
/////////////////////////////////// ************** STREAMING / MIXER SAMPLE CODE BELOW  ***************** ////////////////////////////////////////////
void sample_start_mixer(int chanid, int samplenum, int loop)
{
	//First check that it's a valid sample!
	if (!sound[samplenum].state == SOUND_LOADED)
	{
		wrlog("error, attempting to play invalid sample on channel %d state: %d", chanid, channel[chanid].state);
		return;
	}

	if (channel[chanid].state == SOUND_PLAYING)
	{
		wrlog("error, sound already playing on this channel %d state: %d", chanid, channel[chanid].state);
		return;
	}

	channel[chanid].state = SOUND_PLAYING;
	channel[chanid].stream_type = SOUND_PCM;
	channel[chanid].loaded_sample_num = samplenum;
	channel[chanid].looping = loop;
	channel[chanid].pos = 0;
	//channel[chanid].vol = 1.0;
	audio_list.emplace_back(chanid);
	wrlog("Playing Sample #%d :%s", samplenum, sound[samplenum].name.c_str());
}

int sample_playing_mixer(int chanid)
{
	if (channel[chanid].state == SOUND_PLAYING)
		return 1;
	else return 0;
}

void sample_end_mixer(int chanid)
{
	channel[chanid].looping = 0;
}

void sample_stop_mixer(int chanid)
{
	channel[chanid].state = SOUND_STOPPED;
	channel[chanid].looping = 0;
	channel[chanid].pos = 0;
	audio_list.remove(chanid);
}

// This goes from 0 to 100, with 100 being the original level.
void sample_set_volume_mixer(int chanid, int volume)
{
	channel[chanid].vol = db_volume[volume];
	//wrlog("Setting channel %i to with volume %i setting bvolume %f", chanid, volume, channel[chanid].vol);
};

int sample_get_volume_mixer(int chanid)
{
	return (int)(channel[chanid].vol * 100);
};

void stream_start(int chanid, int stream, int bits, int frame_rate)
{
	int stream_sample = create_sample(bits, 0, SYS_FREQ, (int)SYS_FREQ / frame_rate);

	if (channel[chanid].state == SOUND_PLAYING)
	{
		wrlog("error, sound already playing on this channel %d state: %d", chanid, channel[chanid].state);
		return;
	}
	//wrlog("Playing Sample :%s", sound[samplenum].name.c_str());
	channel[chanid].state = SOUND_PLAYING;
	channel[chanid].loaded_sample_num = stream_sample;
	channel[chanid].looping = 1;
	channel[chanid].pos = 0;
	channel[chanid].stream_type = SOUND_STREAM;
	audio_list.emplace_back(chanid);
}

void stream_stop(int chanid, int stream)
{
	channel[stream].state = SOUND_STOPPED;
	channel[stream].loaded_sample_num = 0;
	channel[stream].looping = 0;
	channel[stream].pos = 0;
	audio_list.remove(chanid);
	//Warning, This doesn't delete the created sample/stream
}

void stream_update(int chanid, short* data)
{
	if (channel[chanid].state == SOUND_PLAYING)
	{
		SAMPLE p = sound[channel[chanid].loaded_sample_num];
		memcpy(p.data.buffer, data, p.dataLength);
	}
}

void stream_update(int chanid, unsigned char* data)
{
	if (channel[chanid].state == SOUND_PLAYING)
	{
		SAMPLE p = sound[channel[chanid].loaded_sample_num];
		memcpy(p.data.buffer, data, p.dataLength);
	}
}

void sample_remove(int samplenum)
{
}
// create_sample:
// *  Constructs a new sample structure of the specified type.
int create_sample(int bits, bool is_stereo, int freq, int len)
{
	wrlog("Creating sample, Buffer size here is %d", len);
	int	sound_id = -1;      // id of sound to be loaded
	int  index;               // looping variable
	// step one: are there any open id's ?
	for (index = 0; index < MAX_SOUNDS; index++)
	{
		// make sure this sound is unused
		if (sound[index].state == SOUND_NULL)
		{
			sound_id = index;
			break;
		} // end if
	} // end for index
	  // did we get a free id? If not,fail.
	if (sound_id == -1) {
		wrlog("No free sound id's for creation of new sample?"); return(-1);
	}
	//SOUND
	wrlog("Creating Stream Audio Sample with sound id %d", sound_id);
	// set rate and size in data structure
	// Sanity checks
	sound[sound_id].sampleRate = freq;
	sound[sound_id].bitPerSample = bits;
	sound[sound_id].channels = ((is_stereo) ? 2 : 1);
	sound[sound_id].dataLength = len * bits / 8;
	sound[sound_id].sampleCount = len;
	sound[sound_id].state = SOUND_LOADED;
	sound[sound_id].name = "STREAM";
	sound[sound_id].data.buffer = (unsigned char*)malloc(len * bits / 8);
	memset(sound[sound_id].data.buffer, 0, len * bits / 8);

	//wrlog("Real buffer size %d", BUFFER_SIZE * 2);
	//wrlog("Buffer size created here %d", (len * ((bits == 8) ? 1 : sizeof(short)) * ((is_stereo) ? 2 : 1)));
	lsamples.push_back(sound[sound_id]);
	return sound_id;
}

void mute_audio()
{
	v_mute_audio = 1;
}

void restore_audio()
{
	v_mute_audio = 0;
}

void pause_audio()
{
	sound_paused = 1;
}

void resume_audio()
{
	sound_paused = 0;
}

//Find a loaded sample number in a list.
int snumlookup(int snum)
{
	for (auto i = lsamples.begin(); i != lsamples.end(); ++i)
	{
		if (snum == (i->num)) { return i->num; }
	}

	wrlog("Attempted lookup of sample number, it was not found in loaded samples?");
	return 0;
}

//Be careful that you call this with a real sample->num, not just the loaded sample number
std::string numToName(int num)
{
	try {
		auto it = lsamples.at(num);      // vector::at throws an out-of-range
		return it.name;
	}

	catch (const std::out_of_range& err)
	{
		wrlog("Out of Range error: get loaded sample name: %s \n", err.what());
	}
	return ("notfound");
}

int nameToNum(std::string name)
{
	for (auto i = lsamples.begin(); i != lsamples.end(); ++i)
	{
		//if (name == i->name) { return i - lsamples.begin(); }

		if ((name.compare(i->name) == 0)) { return i->num; }
	}

	wrlog("Sample: %s not found, returning 0\n", name.c_str());

	return -1;
}