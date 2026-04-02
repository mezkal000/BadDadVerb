#pragma once
#include <cmath>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

namespace dadbass
{

// Deterministic pseudo-random float [0,1] from integer seed — no heap, same every call
static inline float hashf(int seed) noexcept
{
    seed = (seed ^ (seed << 13)) * 1664525 + 1013904223;
    seed = (seed ^ (seed >> 17)) * 22695477 + 1;
    return float((unsigned)seed & 0xffffff) / float(0xffffff);
}

// Fast tanh approximation (Pade [5/4])
inline float fastTanh(float x) noexcept
{
    const float x2 = x * x;
    const float num = x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)));
    const float den = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f));
    return num / den;
}

// Soft clipper — keeps odd harmonics, gentle knee
inline float softClip(float x) noexcept
{
    x = juce::jlimit(-1.5f, 1.5f, x);
    return x - (x * x * x) / 3.375f; // x - x^3/3.375 normalised to ±1
}

// Linear interpolation helper
inline float lerp(float a, float b, float t) noexcept
{
    return a + t * (b - a);
}

} // namespace dadbass
