#pragma once

#ifndef WAVFILE_H
#define WAVFILE_H

#include <cstdint>

typedef struct
{
	int loadtype;
	int16_t channels;		    //  Number of channels 
	uint16_t sampleRate;	//  Sample rate 
	unsigned long sampleCount;	//  Sample count
	int16_t blockAlign;       //  # of bytes per sample
    unsigned long dataLength;	//  Data length
	uint8_t *data;		//  Pointer to the actual WAV data
	int16_t bitPerSample;	    //  The bit rate of the WAV 
}wavedef;



extern wavedef Wave;
int WavFileLoadInternal(unsigned char *wavfile, int size);

#endif