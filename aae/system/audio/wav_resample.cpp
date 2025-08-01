#define _USE_MATH_DEFINES
#include <stdlib.h>
#include <math.h>
#include "wav_resample.h"

// Convert Sb to volume 0-100
double dBToAmplitude(double db)
{
	return (double)pow(10.0f, db / 20.0f);
}

// Function to convert 16-bit integer to fraction
double to_fraction(int16_t sample) 
{
	return static_cast<double>(sample) / 32768.0;
}

// USAGE: float dB = 6.0;  adjust_volume_dB(samples, num_samples, dB); 
// 16 Bit Samples ONLY!
void adjust_volume_dB(int16_t* samples, size_t num_samples, float dB) 
{
	// Convert dB to a linear scale factor
	float volume_factor = powf(10, dB / 20);

	for (size_t i = 0; i < num_samples; ++i) {
		// Adjust the volume
		int32_t adjusted_sample = (int32_t)(samples[i] * volume_factor);

		// Clamp the value to avoid overflow
		if (adjusted_sample > INT16_MAX) {
			adjusted_sample = INT16_MAX;
		}
		else if (adjusted_sample < INT16_MIN) {
			adjusted_sample = INT16_MIN;
		}
		samples[i] = (int16_t)adjusted_sample;
	}
}

int16_t linear_interpolate(int16_t a, int16_t b, float t) 
{
	return static_cast<int32_t> ( a + t * (b - a));
}

void linear_interpolation_16(int16_t* input_data, int32_t input_samples, int16_t** output_data, int32_t* output_samples, float ratio) 
{
	*output_samples = (int32_t)(input_samples * ratio);
	*output_data = (int16_t*)malloc(*output_samples * sizeof(int16_t));

	for (int i = 0; i < *output_samples; ++i) {
		float t = i / ratio;
		int index = (int)t;
		float frac = t - index;
		int16_t a = input_data[index];
		int16_t b = input_data[index + 1];
		(*output_data)[i] = linear_interpolate(a, b, frac);
	}
}

void linear_interpolation_8(uint8_t* input, uint8_t* output, int input_size, int output_size) {
	for (int i = 0; i < output_size; ++i) {
		float ratio = (float)i * (input_size - 1) / (output_size - 1);
		int index = (int)ratio;
		float fraction = ratio - index;

		if (index + 1 < input_size) {
			output[i] = (uint8_t)((1 - fraction) * input[index] + fraction * input[index + 1]);
		}
		else {
			output[i] = input[index];
		}
	}
}

// Function to scale pitch without changing data size
void changePitch(const uint8_t* input, uint8_t* output, size_t size, float ratio) {
	for (size_t i = 0; i < size; ++i) {
		float index = i / ratio;
		size_t floorIndex = static_cast<size_t>(index);

		if (floorIndex + 1 < size) {
			// Linear interpolation
			float frac = index - floorIndex;
			output[i] = static_cast<uint8_t>(
				input[floorIndex] * (1 - frac) + input[floorIndex + 1] * frac
				);
		}
		else {
			// Edge case: use the last sample for boundary conditions
			output[i] = input[size - 1];
		}
	}
}
