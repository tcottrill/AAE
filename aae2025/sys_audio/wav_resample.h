#pragma once
#include <cstdint>
#include <vector>

double dBToAmplitude(double db);
void adjust_volume_dB(int16_t* samples, size_t num_samples, float dB);
void linear_interpolation_16(int16_t* input_data, int32_t input_samples, int16_t** output_data, int32_t* output_samples, float ratio);
void linear_interpolation_8(uint8_t* input, uint8_t* output, int input_size, int output_size);

