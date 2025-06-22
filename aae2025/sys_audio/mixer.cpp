#include "mixer.h"
#include "framework.h"
#include "wav_file.h"
#include "aae_fileio.h"
#include "XAudio2Stream.h"
#include "wav_resample.h"
#include "helper_functions.h"
#include "dbvolume.h"
#include "error_wav.h"

#include <mutex>
#include <vector>
#include <list>
#include <atomic>
#include <cmath>
#include <memory>
#include <cstring>
#include <algorithm>

#define HR(hr) if (FAILED(hr)) { LOG_ERROR("Error at line %d: HRESULT = 0x%08X\n", __LINE__, hr); }

extern IXAudio2* pXAudio2;
static int SYS_FREQ = 44100;
static int BUFFER_SIZE = 0;

constexpr int MAX_CHANNELS = 20;
constexpr int MAX_SOUNDS = 255;

static std::atomic<bool> sound_paused{ false };
static std::atomic<bool> v_mute_audio{ false };
static int sound_id = -1;
static float last_master_vol = 1.0f;

static std::mutex audioMutex;
static std::list<int> audio_list;
static std::vector<std::shared_ptr<SAMPLE>> lsamples;

static CHANNEL channel[MAX_CHANNELS];

unsigned char Make8bit(int16_t sample)
{
	sample >>= 8;
	sample ^= 0x80;
	return static_cast<uint8_t>(sample & 0xFF);
}

short Make16bit(uint8_t sample)
{
	return static_cast<int16_t>(sample - 0x80) << 8;
}

void resample_wav_8(SAMPLE* sample, int new_freq)
{
	int input_size = sample->sampleCount;
	int output_size = static_cast<int>((float)input_size * new_freq / sample->fx.nSamplesPerSec);

	auto output_data = std::make_unique<uint8_t[]>(output_size);
	linear_interpolation_8(sample->data8.get(), output_data.get(), input_size, output_size);

	LOG_INFO("Resampling 8-bit Sample #%d, %s", sample->num, sample->name.c_str());

	sample->data8 = std::move(output_data);
	sample->data16.reset();
	sample->fx.nSamplesPerSec = new_freq;
	sample->dataSize = output_size;
	sample->sampleCount = output_size;
	sample->fx.nAvgBytesPerSec = sample->fx.nSamplesPerSec * sample->fx.nBlockAlign;
	sample->buffer = sample->data8.get();
}

void resample_wav_16(SAMPLE* sample, int new_freq)
{
	int32_t output_samples = 0;
	int16_t* output_data_16 = nullptr;
	float resample_ratio = static_cast<float>(new_freq) / sample->fx.nSamplesPerSec;

	LOG_INFO("Resampling 16-bit Sample #%d, %s", sample->num, sample->name.c_str());

	linear_interpolation_16(sample->data16.get(), sample->sampleCount, &output_data_16, &output_samples, resample_ratio);

	sample->data16.reset(output_data_16);
	sample->data8.reset();
	sample->fx.nSamplesPerSec = new_freq;
	sample->dataSize = output_samples * 2;
	sample->sampleCount = output_samples;
	sample->fx.nAvgBytesPerSec = sample->fx.nSamplesPerSec * sample->fx.nBlockAlign;
	sample->buffer = sample->data16.get();
}

int load_sample(const char* archname, const char* filename, bool force_resample)
{
	auto sample = std::make_shared<SAMPLE>();
	std::unique_ptr<uint8_t[]> sample_data;
	int sample_size = 0;

	if (archname) {
		sample_data.reset(load_zip_file(archname, filename));
		sample_size = get_last_zip_file_size();
	}
	else {
		sample_data.reset(load_file(filename));
		sample_size = get_last_file_size();
	}

	HRESULT result = S_OK;
	if (!sample_data) {
		LOG_ERROR("Error: File not found %s, loading fallback error.wav", filename);
		result = WavLoadFileInternal(error_wav, 10008, sample.get());
	}
	else {
		result = WavLoadFileInternal(sample_data.get(), sample_size, sample.get());
	}

	if (FAILED(result)) {
		LOG_ERROR("Error loading WAV file: %s", filename);
		return -1;
	}

	sample->name = base_name(remove_extension(filename));
	sample->state = SoundState::Loaded;
	sample->num = ++sound_id;

	if (force_resample && sample->fx.nSamplesPerSec != SYS_FREQ && sample->fx.nChannels == 1) {
		if (sample->fx.wBitsPerSample == 8)
			resample_wav_8(sample.get(), SYS_FREQ);
		else
			resample_wav_16(sample.get(), SYS_FREQ);
	}

	LOG_INFO("Loaded file %s with ID %d", filename, sample->num);
	{
		std::scoped_lock lock(audioMutex);
		lsamples.push_back(sample);
	}

	return sample->num;
}

void mixer_init(int rate, int fps)
{
	BUFFER_SIZE = rate / fps;
	SYS_FREQ = rate;

	LOG_INFO("Mixer init, BUFFER_SIZE = %d, freq = %d, framerate = %d", BUFFER_SIZE, rate, fps);
	xaudio2_init(rate, fps);

	for (int i = 0; i < MAX_CHANNELS; ++i) {
		channel[i] = CHANNEL(); // resets all fields to default
	}

	sound_paused = false;
	v_mute_audio = false;
}

void mixer_update()
{
	int32_t fmix = 0;
	BYTE* soundbuffer = GetNextBuffer();

	std::scoped_lock lock(audioMutex);

	for (int i = 0; i < BUFFER_SIZE; ++i) {
		fmix = 0;

		if (!sound_paused) {
			for (auto it = audio_list.begin(); it != audio_list.end(); ) {
				int chan = *it;
				auto& ch = channel[chan];
				auto& sample = lsamples[ch.loaded_sample_num];

				if (ch.pos >= sample->sampleCount) {
					if (!ch.looping) {
						ch.state = SoundState::Stopped;
						it = audio_list.erase(it);
						continue;
					}
					ch.pos = 0;
				}

				int32_t smix = 0;
				if (sample->fx.wBitsPerSample == 16 && sample->data16) {
					smix = static_cast<int32_t>(sample->data16[ch.pos]);
				}
				else if (sample->fx.wBitsPerSample == 8 && sample->data8) {
					smix = static_cast<int32_t>((sample->data8[ch.pos] - 128) << 8);
				}

				smix = static_cast<int32_t>(smix * ch.vol * 0.70);
				fmix += smix;
				ch.pos += sample->fx.nChannels;
				++it;
			}
		}

		if (v_mute_audio)
			fmix = 0;

		fmix = std::clamp(fmix, static_cast<int32_t>(INT16_MIN), static_cast<int32_t>(INT16_MAX));

		soundbuffer[2 * i] = fmix & 0xFF;
		soundbuffer[2 * i + 1] = (fmix >> 8) & 0xFF;
	}

	xaudio2_update(soundbuffer, BUFFER_SIZE);
}

void mixer_end()
{
	xaudio2_stop();
	std::scoped_lock lock(audioMutex);
	for (auto& sample : lsamples) {
		if (sample->buffer)
			LOG_INFO("Freeing sample #%d named %s", sample->num, sample->name.c_str());
	}
	lsamples.clear();
	audio_list.clear();
}

void sample_stop(int chanid)
{
	auto& ch = channel[chanid];
	if (ch.isPlaying && ch.voice) {
		ch.voice->Stop();
		ch.voice->FlushSourceBuffers();
		ch.isPlaying = false;
		ch.state = SoundState::Stopped;
		ch.looping = 0;
		ch.pos = 0;
	}
}

static void SetPan(IXAudio2SourceVoice* voice, float pan)
{
	float left = 0.5f - pan / 2.0f;
	float right = 0.5f + pan / 2.0f;
	float outputMatrix[2] = { left, right };

	voice->SetOutputMatrix(nullptr, 1, 2, outputMatrix);
}

void sample_remove(int samplenum)
{
}

void sample_start(int chanid, int samplenum, int loop)
{
	std::scoped_lock lock(audioMutex);

	if (samplenum < 0 || samplenum >= static_cast<int>(lsamples.size()) ||
		lsamples[samplenum]->state != SoundState::Loaded) {
		LOG_ERROR("Error: Attempting to play invalid sample %d on channel %d", samplenum, chanid);
		return;
	}

	auto& ch = channel[chanid];

	if (ch.voice) {
		ch.voice->Stop();
		ch.voice->FlushSourceBuffers();
		ch.voice->DestroyVoice();
		ch.voice = nullptr;
	}

	auto& sample = lsamples[samplenum];
	
	// the 4.0f is really important here and required for the StarCastle drone.
	if (FAILED(pXAudio2->CreateSourceVoice(&ch.voice, &sample->fx, 0, 4.0f)))   
	{
		LOG_ERROR("Failed to create voice for sample %d", sample->num);
		return;
	}

	ch.isAllocated = true;
	ch.isReleased = false;
	ch.isPlaying = true;
	ch.looping = loop;
	ch.volume = 255;
	ch.pan = 128;
	ch.frequency = sample->fx.nSamplesPerSec;
	ch.loaded_sample_num = samplenum;

	ch.buffer.AudioBytes = sample->dataSize;
	ch.buffer.pAudioData = static_cast<BYTE*>(sample->buffer);
	ch.buffer.LoopCount = loop ? XAUDIO2_LOOP_INFINITE : 0;

	ch.voice->SubmitSourceBuffer(&ch.buffer);
	ch.voice->SetVolume(static_cast<float>(ch.volume) / 255.0f);
	SetPan(ch.voice, (ch.pan - 128) / 128.0f);

	HR(ch.voice->Start());
}

int sample_get_position(int chanid)
{
	return channel[chanid].pos;
}

void sample_set_volume(int chanid, int volume)
{
	auto& ch = channel[chanid];
	ch.vol = db_volume[std::clamp(volume, 0, 255)];

	if (ch.voice) {
		float vol = static_cast<float>(volume) / 255.0f;
		ch.voice->SetVolume(vol);
	}
}

int sample_get_volume(int chanid)
{
	return static_cast<int>(channel[chanid].vol * 100.0);
}

void sample_set_position(int chanid, int pos)
{
	// Placeholder: currently unused
}

int sample_get_freq(int chanid)
{
	if (channel[chanid].isPlaying)
	{
		return channel[chanid].frequency;
	}
	return 0;
}


void sample_set_freq(int chanid, int freq)
{
	auto& ch = channel[chanid];
	if (ch.isPlaying && ch.voice) {
		float ratio = static_cast<float>(freq) / static_cast<float>(ch.frequency);
		ch.voice->SetFrequencyRatio(ratio);
	}
}

int sample_playing(int chanid)
{
	auto& ch = channel[chanid];
	if (ch.voice) {
		XAUDIO2_VOICE_STATE state;
		ch.voice->GetState(&state);
		return (state.BuffersQueued > 0) ? 1 : 0;
	}
	return 0;
}

void sample_end(int chanid)
{
	channel[chanid].looping = 0;
}

void sample_start_mixer(int chanid, int samplenum, int loop)
{
	std::scoped_lock lock(audioMutex);

	if (samplenum < 0 || samplenum >= static_cast<int>(lsamples.size()) ||
		lsamples[samplenum]->state != SoundState::Loaded) {
		LOG_ERROR("Error: Attempting to play invalid sample %d on channel %d", samplenum, chanid);
		return;
	}

	if (channel[chanid].state == SoundState::Playing) {
		LOG_ERROR("Error: Sound already playing on channel %d", chanid);
		return;
	}

	auto& ch = channel[chanid];
	ch.state = SoundState::Playing;
	ch.stream_type = static_cast<int>(SoundState::PCM);
	ch.loaded_sample_num = samplenum;
	ch.looping = loop;
	ch.pos = 0;

	audio_list.push_back(chanid);
	LOG_INFO("Playing Sample #%d :%s", samplenum, lsamples[samplenum]->name.c_str());
}

int sample_playing_mixer(int chanid)
{
	return (channel[chanid].state == SoundState::Playing) ? 1 : 0;
}

void sample_end_mixer(int chanid)
{
	channel[chanid].looping = 0;
}

void sample_stop_mixer(int chanid)
{
	std::scoped_lock lock(audioMutex);
	channel[chanid].state = SoundState::Stopped;
	channel[chanid].looping = 0;
	channel[chanid].pos = 0;
	audio_list.remove(chanid);
}

void sample_set_volume_mixer(int chanid, int volume)
{
	channel[chanid].vol = db_volume[std::clamp(volume, 0, 255)];
}

int sample_get_volume_mixer(int chanid)
{
	return static_cast<int>(channel[chanid].vol * 100.0);
}

void stream_start(int chanid, int /*stream*/, int bits, int frame_rate)
{
	int stream_sample = create_sample(bits, false, SYS_FREQ, SYS_FREQ / frame_rate, "STREAM");

	auto& ch = channel[chanid];

	if (ch.state == SoundState::Playing) {
		LOG_ERROR("Error: Stream already playing on channel %d", chanid);
		return;
	}

	ch.state = SoundState::Playing;
	ch.loaded_sample_num = stream_sample;
	ch.looping = 1;
	ch.pos = 0;
	ch.stream_type = static_cast<int>(SoundState::Stream);

	std::scoped_lock lock(audioMutex);
	audio_list.push_back(chanid);
}

void stream_stop(int chanid, int /*stream*/)
{
	auto& ch = channel[chanid];
	ch.state = SoundState::Stopped;
	ch.loaded_sample_num = -1;
	ch.looping = 0;
	ch.pos = 0;

	std::scoped_lock lock(audioMutex);
	audio_list.remove(chanid);
}

void stream_update(int chanid, short* data)
{
	auto& ch = channel[chanid];
	if (ch.state == SoundState::Playing) {
		auto& sample = lsamples[ch.loaded_sample_num];
		std::memcpy(sample->data16.get(), data, sample->dataSize);
	}
}

void stream_update(int chanid, unsigned char* data)
{
	auto& ch = channel[chanid];
	if (ch.state == SoundState::Playing) {
		auto& sample = lsamples[ch.loaded_sample_num];
		std::memcpy(sample->data8.get(), data, sample->dataSize);
	}
}

void mute_audio()
{
	last_master_vol = mixer_get_master_volume();
	v_mute_audio = true;
	mixer_set_master_volume(0);
}

void restore_audio()
{
	mixer_set_master_volume(static_cast<int>(last_master_vol * 100));
	v_mute_audio = false;
}

void pause_audio()
{
	last_master_vol = mixer_get_master_volume();
	mixer_set_master_volume(0);
	sound_paused = true;
}

void resume_audio()
{
	mixer_set_master_volume(static_cast<int>(last_master_vol * 100));
	sound_paused = false;
}

int create_sample(int bits, bool is_stereo, int freq, int len, const std::string& name)
{
	auto sample = std::make_shared<SAMPLE>();
	sample->num = ++sound_id;
	sample->name = (name == "STREAM") ? name + std::to_string(sample->num) : name;

	LOG_INFO("Creating Audio Sample with name %s and sound id %d", sample->name.c_str(), sample->num);

	sample->fx.wFormatTag = WAVE_FORMAT_PCM;
	sample->fx.nChannels = is_stereo ? 2 : 1;
	sample->fx.nSamplesPerSec = freq;
	sample->fx.wBitsPerSample = bits;
	sample->fx.nBlockAlign = sample->fx.nChannels * bits / 8;
	sample->fx.nAvgBytesPerSec = sample->fx.nSamplesPerSec * sample->fx.nBlockAlign;
	sample->state = SoundState::Loaded;
	sample->sampleCount = len;
	sample->dataSize = len * bits / 8;

	if (bits == 8) {
		sample->data8 = std::make_unique<uint8_t[]>(sample->dataSize);
		std::memset(sample->data8.get(), 0, sample->dataSize);
		sample->buffer = sample->data8.get();
	}
	else {
		sample->data16 = std::make_unique<int16_t[]>(len);
		std::memset(sample->data16.get(), 0, sample->dataSize);
		sample->buffer = sample->data16.get();
	}

	std::scoped_lock lock(audioMutex);
	lsamples.push_back(sample);
	return sample->num;
}

std::string numToName(int num)
{
	std::scoped_lock lock(audioMutex);
	for (const auto& sample : lsamples) {
		if (sample->num == num) {
			return sample->name;
		}
	}
	LOG_ERROR("Name not found for Sample #%d!", num);
	return "";
}

int nameToNum(const std::string& name)
{
	std::scoped_lock lock(audioMutex);
	for (const auto& sample : lsamples) {
		if (sample->name == name) {
			return sample->num;
		}
	}
	return -1;
}

int snumlookup(int snum)
{
	std::scoped_lock lock(audioMutex);
	for (size_t i = 0; i < lsamples.size(); ++i) {
		if (lsamples[i]->num == snum) {
			return static_cast<int>(i);
		}
	}
	LOG_ERROR("Sample number not found in lookup: %d", snum);
	return -1;
}

void save_sample(int samplenum)
{
	std::scoped_lock lock(audioMutex);
	if (samplenum < 0 || samplenum >= static_cast<int>(lsamples.size())) return;

	const auto& sample = lsamples[samplenum];
	if (!sample) return;

	std::string filename = sample->name + ".wav";
	FILE* file = nullptr;
	if (fopen_s(&file, filename.c_str(), "wb") != 0 || !file) {
		LOG_ERROR("Failed to open file for writing: %s", filename.c_str());
		return;
	}

	DWORD subchunk1Size = 16;
	DWORD subchunk2Size = static_cast<DWORD>(sample->dataSize);
	DWORD chunkSize = 4 + (8 + subchunk1Size) + (8 + subchunk2Size);

	fwrite("RIFF", 1, 4, file);
	fwrite(&chunkSize, 4, 1, file);
	fwrite("WAVE", 1, 4, file);
	fwrite("fmt ", 1, 4, file);
	fwrite(&subchunk1Size, 4, 1, file);
	fwrite(&sample->fx.wFormatTag, 2, 1, file);
	fwrite(&sample->fx.nChannels, 2, 1, file);
	fwrite(&sample->fx.nSamplesPerSec, 4, 1, file);
	fwrite(&sample->fx.nAvgBytesPerSec, 4, 1, file);
	fwrite(&sample->fx.nBlockAlign, 2, 1, file);
	fwrite(&sample->fx.wBitsPerSample, 2, 1, file);
	fwrite("data", 1, 4, file);
	fwrite(&subchunk2Size, 4, 1, file);

	if (sample->fx.wBitsPerSample == 8 && sample->data8) {
		fwrite(sample->data8.get(), 1, sample->dataSize, file);
	}
	else if (sample->fx.wBitsPerSample == 16 && sample->data16) {
		fwrite(sample->data16.get(), 1, sample->dataSize, file);
	}

	fclose(file);
	LOG_INFO("WAV file saved to %s", filename.c_str());
}