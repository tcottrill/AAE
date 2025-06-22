#pragma once

#ifndef WAVFILE_H
#define WAVFILE_H

#include <cstdint>
#include "mixer.h"

int WavLoadFileInternal(unsigned char* buffer, int fileSize, SAMPLE *audioFile);

#endif