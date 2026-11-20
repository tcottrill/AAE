//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
// -----------------------------------------------------------------------------
// hdr_math.h - HDR10 output math: ST.2084 (PQ) encode, CRT highlight
// expansion curve, scene-to-nits conversion and user settings clamps.
//
// Header-only, no Vulkan or Windows includes, so it can be unit-tested
// standalone (aae_video_vk/tests/test_hdr_math.cpp).
// -----------------------------------------------------------------------------
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace hdr {

constexpr int kModeOff  = 0;
constexpr int kModeAuto = 1;
constexpr int kModeOn   = 2;

// Factory defaults: the single source for config.cpp and the menu reset.
constexpr int   kDefaultMode       = kModeOff;
constexpr float kDefaultPaperWhite = 150.0f;
constexpr float kDefaultBeamPeak   = 1000.0f;
constexpr float kDefaultBeamCurve  = 2.0f;
constexpr float kDefaultShotBoost  = 2.0f;

// Highlight curve exponent range. A high exponent keeps mid-intensity beams
// at their SDR level and gives the expansion only to beams near maximum
// intensity, which still land on the beam peak.
constexpr float kCurveMin = 0.5f;
constexpr float kCurveMax = 16.0f;

// ST.2084 inverse EOTF: absolute luminance in nits (clamped 0..10000) -> 0..1.
inline float PqEncode(float nits)
{
    const float m1 = 0.1593017578125f;
    const float m2 = 78.84375f;
    const float c1 = 0.8359375f;
    const float c2 = 18.8515625f;
    const float c3 = 18.6875f;

    const float y  = (std::min)((std::max)(nits, 0.0f), 10000.0f) / 10000.0f;
    const float ym = std::pow(y, m1);
    return std::pow((c1 + c2 * ym) / (1.0f + c3 * ym), m2);
}

// Highlight gain: 1 at v=0, k at v>=1, shaped by curve.
inline float HighlightGain(float v, float k, float curve)
{
    const float c = (std::min)((std::max)(v, 0.0f), 1.0f);
    return 1.0f + (k - 1.0f) * std::pow(c, curve);
}

// CRT-style expansion of one value: brighten highlights while leaving dim
// content near SDR. Scalar form; the renderer uses ExpandCrtRgb below.
inline float ExpandCrt(float v, float k, float curve)
{
    return v * std::pow(HighlightGain(v, k, curve), 1.0f / 2.2f);
}

// Renderer form: ONE gain from the brightest channel, applied to all three, so
// the ratio between channels (hue) is preserved. Identical to ExpandCrt for
// grey pixels. Keep in sync with expand_crt() in the vector_post_*_vk.frag
// shaders.
inline void ExpandCrtRgb(float& r, float& g, float& b, float k, float curve)
{
    const float m = (std::max)(r, (std::max)(g, b));
    const float s = std::pow(HighlightGain(m, k, curve), 1.0f / 2.2f);
    r *= s;
    g *= s;
    b *= s;
}

// Beam peak actually aimed at: the setting capped at the display's reported
// maximum (displayMaxNits <= 0 means unknown), never below paper white.
inline float EffectiveBeamPeak(float beamPeak, float paperWhite, float displayMaxNits)
{
    float p = beamPeak;
    if (displayMaxNits > 0.0f)
        p = (std::min)(p, displayMaxNits);
    return (std::max)(p, paperWhite);
}

// Display-referred scene value -> nits (gamma 2.2, sign preserved).
inline float SceneToNits(float v, float paperWhite)
{
    const float s = (v < 0.0f) ? -1.0f : 1.0f;
    return s * std::pow(std::fabs(v), 2.2f) * paperWhite;
}

// IEEE 754 half (binary16) -> float. Handles subnormals, infinities and NaN.
inline float HalfToFloat(uint16_t h)
{
    const int   sign = (h >> 15) & 1;
    const int   exp  = (h >> 10) & 0x1F;
    const int   man  = h & 0x3FF;
    float v;
    if (exp == 0)
        v = std::ldexp((float)man, -24);                 // zero / subnormal
    else if (exp == 31)
        return man ? std::nanf("") : (sign ? -INFINITY : INFINITY);
    else
        v = std::ldexp((float)(man | 0x400), exp - 25);  // normal
    return sign ? -v : v;
}

// Half scene value -> 8-bit SDR byte: clamp 0..1, round. NaN and negatives
// give 0; anything above 1.0 (beam highlights) clips to 255.
inline uint8_t HalfToSdrByte(uint16_t half)
{
    const float f = HalfToFloat(half);
    if (!(f > 0.0f)) return 0;          // NaN, zero, negative
    if (f >= 1.0f) return 255;
    return (uint8_t)(f * 255.0f + 0.5f);
}

struct Settings {
    int   mode;        // kModeOff / kModeAuto / kModeOn
    float paperWhite;  // nits
    float beamPeak;    // nits
    float curve;       // highlight curve exponent
};

// Clamp user settings to sane ranges. displayMaxNits <= 0 means unknown.
inline Settings ClampSettings(Settings s, float displayMaxNits)
{
    s.mode       = (std::min)((std::max)(s.mode, kModeOff), kModeOn);
    s.paperWhite = (std::min)((std::max)(s.paperWhite, 80.0f), 500.0f);
    s.beamPeak   = (std::min)((std::max)(s.beamPeak, 400.0f), 4000.0f);
    if (displayMaxNits > 0.0f) s.beamPeak = (std::min)(s.beamPeak, displayMaxNits);
    s.beamPeak   = (std::max)(s.beamPeak, s.paperWhite);
    s.curve      = (std::min)((std::max)(s.curve, kCurveMin), kCurveMax);
    return s;
}

// Shot boost: brightness multiplier for the hot core of a shot fragment.
// m = brightest channel of the fragment's blended contribution (colour * alpha).
// The square weights the boost to the core and leaves the soft halo nearly
// unchanged. Mirrors the shot fragment shaders (vector_shot_vk.frag,
// vector_shot_tex_vk.frag). boost 1.0 is the identity for any m.
inline float ShotBoostFactor(float m, float boost)
{
    const float c = (std::min)((std::max)(m, 0.0f), 1.0f);
    return 1.0f + (boost - 1.0f) * c * c;
}

// Clamp the user setting to 1.0 .. 4.0 (NaN -> 1.0).
inline float ClampShotBoost(float boost)
{
    if (!(boost > 1.0f)) return 1.0f;   // NaN, <= 1
    return (std::min)(boost, 4.0f);
}

} // namespace hdr
