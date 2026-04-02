#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>

namespace dadbass
{

/**
 * Limiter — brickwall peak limiter, instant attack, smoothed release toward 1.0.
 */
class Limiter
{
public:
    Limiter() = default;

    void prepare(double sampleRate) noexcept
    {
        sr = sampleRate;
        gainReduction = 1.0f;
        rebuildCoeff();
    }

    void setCeiling(float dB)   noexcept { ceiling = juce::Decibels::decibelsToGain(dB); }
    void setReleaseMs(float ms) noexcept { releaseMs = ms; rebuildCoeff(); }

    [[nodiscard]] inline float process(float x) noexcept
    {
        const float absX = std::abs(x);

        if (absX * gainReduction > ceiling)
        {
            // Instant attack — clamp gain immediately
            gainReduction = ceiling / (absX + 1e-9f);
        }
        else
        {
            // Release: one-pole toward 1.0  (gainReduction climbs back up)
            gainReduction += (1.0f - gainReduction) * oneMinusRelease;
            if (gainReduction > 1.0f) gainReduction = 1.0f;
        }

        return x * gainReduction;
    }

private:
    void rebuildCoeff() noexcept
    {
        // oneMinusRelease = 1 - exp(-1/(sr*releaseMs*0.001))
        // When gainReduction is small this adds (1-gainReduction)*k per sample → climbs to 1
        const float coeff = std::exp(-1.0f / (float(sr) * releaseMs * 0.001f));
        oneMinusRelease = 1.0f - coeff;
    }

    double sr              = 44100.0;
    float  ceiling         = juce::Decibels::decibelsToGain(-0.3f);
    float  releaseMs       = 80.0f;
    float  oneMinusRelease = 0.0f;
    float  gainReduction   = 1.0f;
};

} // namespace dadbass
