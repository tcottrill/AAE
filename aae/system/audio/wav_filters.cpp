#define _USE_MATH_DEFINES
#include <cmath>
#include <vector>

#pragma warning( disable : 4244)

// Function to apply a high pass filter
void highPassFilter(std::vector<int16_t>& audioSample, float cutoffFreq, float sampleRate) {
    float RC = 1.0 / (cutoffFreq * 2 * M_PI);
    float dt = 1.0 / sampleRate;
    float alpha = RC / (RC + dt);

    int16_t previousSample = audioSample[0];
    int16_t previousFilteredSample = audioSample[0];

    for (size_t i = 1; i < audioSample.size(); ++i) {
        int16_t currentSample = audioSample[i];
        audioSample[i] = alpha * (previousFilteredSample + currentSample - previousSample);
        previousFilteredSample = audioSample[i];
        previousSample = currentSample;
    }
}

// Function to apply a low pass filter
void lowPassFilter(std::vector<int16_t>&audioSample, float cutoffFreq, float sampleRate) {
    float RC = 1.0 / (cutoffFreq * 2 * M_PI);
    float dt = 1.0 / sampleRate;
    float alpha = dt / (RC + dt);

    int16_t previousSample = audioSample[0];

    for (size_t i = 1; i < audioSample.size(); ++i) {
        audioSample[i] = previousSample + alpha * (audioSample[i] - previousSample);
        previousSample = audioSample[i];
    }
}

/* Usage Example

int main() {
    std::vector<int16_t> audioSample = { ... }; // Your audio data goes here

    float cutoffFreq = 1000.0; // Cutoff frequency in Hz
    float sampleRate = 44100.0; // Sample rate in Hz

    highPassFilter(audioSample, cutoffFreq, sampleRate);
    lowPassFilter(audioSample, cutoffFreq, sampleRate);

    // Output the filtered audio sample
    for (int16_t sample : audioSample) {
        std::cout << sample << " ";
    }
    std::cout << std::endl;

    return 0;
}

*/