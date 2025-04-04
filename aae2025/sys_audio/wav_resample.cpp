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

//#include <iostream>
//#include <cmath>
//#include <cstdint>

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

/*
void resample_simple_8(const unsigned char* input, int inputSize, int inputRate,unsigned char* output, int outputSize, int outputRate)
{
	double ratio = (double)inputRate / outputRate;
	for (int i = 0; i < outputSize; i++) {
		double srcIndex = i * ratio;
		int indexLow = (int)srcIndex;
		int indexHigh = indexLow + 1;

		if (indexHigh >= inputSize) {
			indexHigh = inputSize - 1; // Prevent out-of-bounds
		}

		double weightHigh = srcIndex - indexLow;
		double weightLow = 1.0 - weightHigh;

		output[i] = (unsigned char)(input[indexLow] * weightLow + input[indexHigh] * weightHigh);
	}
}

*/

/*

#define PI 3.141592653589793

// Sinc function
double sinc(double x) {
	if (x == 0.0) {
		return 1.0;
	}
	return sin(PI * x) / (PI * x);
}

// Windowed sinc function with a Hann window
double windowedSinc(double x, int windowSize) {
	if (fabs(x) > windowSize) {
		return 0.0;
	}
	double hann = 0.5 * (1.0 + cos(PI * x / windowSize));
	return hann * sinc(x);
}

// Resample with a sinc-based interpolation
void resample_sinc(const unsigned char* input, int inputSize, int inputRate,	unsigned char* output, int outputSize, int outputRate, int windowSize)
{
	double ratio = (double)inputRate / outputRate;

	for (int i = 0; i < outputSize; i++) {
		double srcIndex = i * ratio;
		int center = (int)srcIndex;

		double sample = 0.0;
		for (int j = -windowSize; j <= windowSize; j++) {
			int index = center + j;
			if (index >= 0 && index < inputSize) {
				sample += input[index] * windowedSinc(srcIndex - index, windowSize);
			}
		}
		// Clamp output to valid 8-bit range
		if (sample < 0.0) sample = 0.0;
		if (sample > 255.0) sample = 255.0;
		output[i] = (unsigned char)sample;
	}
}

*/


/*
double cubicInterpolate(double v0, double v1, double v2, double v3, double x) {
	double a = (-0.5 * v0) + (1.5 * v1) - (1.5 * v2) + (0.5 * v3);
	double b = v0 - (2.5 * v1) + (2.0 * v2) - (0.5 * v3);
	double c = (-0.5 * v0) + (0.5 * v2);
	double d = v1;

	return a * x * x * x + b * x * x + c * x + d;
}

void resample_cubic_8(unsigned char* input, int inputSize, int inputRate,	unsigned char* output, int outputSize, int outputRate)
{
	double ratio = (double)inputRate / outputRate;
	for (int i = 0; i < outputSize; i++) {
		double srcIndex = i * ratio;
		int index1 = (int)srcIndex - 1;
		int index2 = (int)srcIndex;
		int index3 = index2 + 1;
		int index4 = index3 + 1;

		// Ensure indices are within bounds
		if (index1 < 0) index1 = 0;
		if (index4 >= inputSize) index4 = inputSize - 1;
		if (index3 >= inputSize) index3 = inputSize - 1;
		if (index2 >= inputSize) index2 = inputSize - 1;

		double x = srcIndex - index2;

		output[i] = (unsigned char)cubicInterpolate(input[index1], input[index2], input[index3], input[index4], x);
	}
}
*/

/*
#include <iostream>
#include <cmath>
#include <cstdint>

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

int main() {
	const size_t dataSize = 1024;  // Example size of the audio sample
	uint8_t audioSample[dataSize] = { // Insert your audio sample data here  };
	uint8_t modifiedSample[dataSize]; // Output buffer for the processed data

	float originalFrequency = 440.0f; // Example: 440 Hz
	float targetFrequency = 880.0f;   // Example: 880 Hz (double the pitch)

	float ratio = targetFrequency / originalFrequency;

	changePitch(audioSample, modifiedSample, dataSize, ratio);

	std::cout << "Pitch adjustment complete. Data size remains unchanged: " << dataSize << "\n";
	return 0;
}
*/