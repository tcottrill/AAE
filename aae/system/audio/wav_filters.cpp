#define _USE_MATH_DEFINES
#include <cmath>
#include <vector>
#include <algorithm>  

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


// Design a bilinear-transformed low-pass biquad (Butterworth-ish, Q = 0.707)
void design_biquad_lowpass(float fs, float fc, float Q,
    float& b0, float& b1, float& b2,
    float& a1, float& a2)
{
    const float w0 = 2.0f * float(M_PI) * fc / fs;
    const float cosw = std::cos(w0);
    const float sinw = std::sin(w0);
    const float alpha = sinw / (2.0f * Q);

    float b0u = (1.0f - cosw) * 0.5f;
    float b1u = 1.0f - cosw;
    float b2u = (1.0f - cosw) * 0.5f;
    float a0u = 1.0f + alpha;
    float a1u = -2.0f * cosw;
    float a2u = 1.0f - alpha;

    b0 = b0u / a0u;
    b1 = b1u / a0u;
    b2 = b2u / a0u;
    a1 = a1u / a0u;
    a2 = a2u / a0u;
}

// In-place biquad on int16_t (Direct Form 1, float state)
void biquad_lowpass_inplace_i16(int16_t* x, int n, float fs, float fc, float Q = 0.707f, int passes = 1)
{
    if (!x || n <= 2 || fc <= 0.0f || fs <= 0.0f) return;
    float b0, b1, b2, a1, a2;
    design_biquad_lowpass(fs, fc, Q, b0, b1, b2, a1, a2);

    for (int p = 0; p < passes; ++p)
    {
        float x1 = 0.0f, x2 = 0.0f;
        float y1 = 0.0f, y2 = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            const float xf = (float)x[i];
            const float y = b0 * xf + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;

            x2 = x1; x1 = xf;
            y2 = y1; y1 = y;

            int v = (int)std::lrintf(y);
            x[i] = (int16_t)std::clamp(v, -32768, 32767);
        }
    }
}