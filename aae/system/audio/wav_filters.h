
#pragma once
#include <cstdint>
#include <vector>


void highPassFilter(std::vector<int16_t>& audioSample, float cutoffFreq, float sampleRate);
void lowPassFilter (std::vector<int16_t>& audioSample, float cutoffFreq, float sampleRate);
