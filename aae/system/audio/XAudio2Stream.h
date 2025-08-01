#ifndef XAUDIO2STREAM_H
#define XAUDIO2STREAM_H

#include <xaudio2.h>
#include <vector>
#include <thread>
#include <mutex>

float mixer_get_master_volume();
void mixer_set_master_volume(int volume);
HRESULT xaudio2_init(int rate, int fps);
HRESULT xaudio2_update(BYTE* buffera, DWORD bufferLength);
void xaudio2_stop();
BYTE* GetNextBuffer();


#endif