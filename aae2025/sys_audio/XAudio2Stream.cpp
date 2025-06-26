#include "XAudio2Stream.h"
#include "mixer.h"
#include "log.h"

extern double dBToAmplitude(double db);

// Error handling macro
#define HR(hr) if (FAILED(hr)) { LOG_INFO("Error at line %d: HRESULT = 0x%08X\n", __LINE__, hr); return hr; }

#pragma comment(lib, "xaudio2.lib")

// Global variables
const int NUM_BUFFERS = 5;
IXAudio2* pXAudio2 = NULL;
IXAudio2MasteringVoice* pMasterVoice = NULL;
IXAudio2SourceVoice* pSourceVoice = NULL;
BYTE* audioBuffers[NUM_BUFFERS];
DWORD bufferSize;
WAVEFORMATEXTENSIBLE wfx = {};

// Number of audio updates per second
int UpdatesPerSecond = 60;
int SamplesPerSecond = 22050;
int BufferDurationMs = 1000 / UpdatesPerSecond;
int SamplesPerBuffer = SamplesPerSecond / UpdatesPerSecond;

int currentBufferIndex = 0;

void mixer_set_master_volume(int volume)
{
	// Ensure volume is within the range 0-100
	if (volume < 0) volume = 0;
	if (volume > 100) volume = 100;

	// Convert volume from 0-100 to decibels
	float decibels = -96.0f + (volume / 100.0f) * 96.0f; // Assuming -96dB to 0dB range
	float amplitude = (float)dBToAmplitude(decibels);

	pMasterVoice->SetVolume(amplitude);
}

float mixer_get_master_volume()
{
	float vol;
	pMasterVoice->GetVolume(&vol);
	return vol;;
}

BYTE* GetNextBuffer()
{
	// LOG_INFO("Now using buffer %d", currentBufferIndex);
	return audioBuffers[currentBufferIndex];
}

HRESULT xaudio2_init(int rate, int fps)
{
	HRESULT hr;

	UpdatesPerSecond = fps;
	SamplesPerSecond = rate;
	BufferDurationMs = 1000 / UpdatesPerSecond;
	SamplesPerBuffer = SamplesPerSecond / UpdatesPerSecond;

	HR(CoInitializeEx(nullptr, COINIT_MULTITHREADED));

	// Initialize XAudio2
	HR(XAudio2Create(&pXAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR));

	// Create a mastering voice
 	HR(pXAudio2->CreateMasteringVoice(&pMasterVoice, XAUDIO2_DEFAULT_CHANNELS, SamplesPerSecond, 0, 0));

	pMasterVoice->SetVolume(.9f);

	wfx.Format.wFormatTag = WAVE_FORMAT_PCM;
	wfx.Format.nSamplesPerSec = SamplesPerSecond;
	wfx.Format.nChannels = 1;
	wfx.Format.wBitsPerSample = 16;
	wfx.Format.nBlockAlign = wfx.Format.nChannels * wfx.Format.wBitsPerSample / 8;
	wfx.Format.nAvgBytesPerSec = wfx.Format.nSamplesPerSec * wfx.Format.nBlockAlign;
	wfx.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
	wfx.Samples.wValidBitsPerSample = 16;
	wfx.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
	wfx.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;

	// Create the source voice
	hr = pXAudio2->CreateSourceVoice(&pSourceVoice, &wfx.Format, XAUDIO2_VOICE_NOPITCH, XAUDIO2_DEFAULT_FREQ_RATIO, NULL, NULL, NULL);
	if (FAILED(hr))
	{
		LOG_INFO("Failed to create source voice: %#X\n", hr);
		return hr;
	}

	LOG_INFO("Source Voice Created Successfully. SamplesPerBuffer here %d", SamplesPerBuffer);
	// Allocate the audio buffers
	bufferSize = SamplesPerBuffer * 2;
	for (int i = 0; i < NUM_BUFFERS; ++i)
	{
		audioBuffers[i] = new BYTE[bufferSize];
	}

	// Start the source voice
	HR(pSourceVoice->Start());

	currentBufferIndex = 0;

	return S_OK;
}

HRESULT xaudio2_update(BYTE* buffera, DWORD bufferLength)
{
	HRESULT hr;
	//Debugging
	XAUDIO2_VOICE_STATE VoiceState;
	//  LOG_INFO("Bufferlength %d, Buffersize %d buffernum %d",bufferLength,bufferSize, currentBufferIndex);
	// Submit the buffer to the source voice
	XAUDIO2_BUFFER buffer = { 0 };
	buffer.AudioBytes = bufferSize;
	buffer.pContext = audioBuffers[currentBufferIndex];
	buffer.pAudioData = audioBuffers[currentBufferIndex];
	// buffer.Flags = XAUDIO2_END_OF_STREAM;

	hr = pSourceVoice->SubmitSourceBuffer(&buffer);
	if (FAILED(hr))
	{
		LOG_INFO("Failed to submit source buffer: %#X\n", hr);
	}

	if (hr == XAUDIO2_E_DEVICE_INVALIDATED) {
		/* !!! FIXME: possibly disconnected or temporary lost. Recover? */
		LOG_INFO("Lost the XAudio2 source buffer: %#X\n", hr);
	}

	if (hr != S_OK) {  /* uhoh, panic! */

		pSourceVoice->FlushSourceBuffers();
		LOG_INFO("Panic, some odd error submitting the XAudio2 source buffer: %#X\n", hr);
		exit(1);
	}

	pSourceVoice->GetState(&VoiceState);
	//LOG_INFO("Buffers in Queue: %d Samples Played: %d", VoiceState.BuffersQueued, VoiceState.SamplesPlayed);
	// Move to the next buffer
	currentBufferIndex = (currentBufferIndex + 1) % NUM_BUFFERS;

	return hr;
}

void xaudio2_stop()
{
	// Clean up XAudio2
	if (pSourceVoice) pSourceVoice->DestroyVoice();
	if (pMasterVoice) pMasterVoice->DestroyVoice();
	if (pXAudio2) pXAudio2->Release();

	for (int i = 0; i < 3; ++i)
	{
		delete[] audioBuffers[i];
	}
}