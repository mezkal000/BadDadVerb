#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>

namespace dadbass
{

class NoiseGate
{
public:
    NoiseGate() = default;

    void prepare(double sampleRate) noexcept
    {
        sr       = sampleRate;
        envelope = 0.0f;
        gateGain = 1.0f;
        const float tc = std::exp(-1.0f / (float(sr) * 5.0f * 0.001f)); // 5ms
        attackCoeff  = tc;
        const float tcR = std::exp(-1.0f / (float(sr) * 90.0f * 0.001f));
        releaseCoeff = tcR;
    }

    void setThresholdDb(float dB) noexcept
    {
        thresholdLinear = juce::Decibels::decibelsToGain(dB);
    }
    void setReleaseMs(float ms) noexcept
    {
        releaseCoeff = std::exp(-1.0f / (float(sr) * ms * 0.001f));
    }

    [[nodiscard]] inline float process(float x) noexcept
    {
        const float absX = std::abs(x);
        // Envelope follower
        if (absX > envelope)
            envelope = attackCoeff * envelope + (1.0f - attackCoeff) * absX;
        else
            envelope = releaseCoeff * envelope + (1.0f - releaseCoeff) * absX;

        // Smooth gate gain — open above threshold, closed below
        const float target = (envelope >= thresholdLinear) ? 1.0f
                           : (envelope / (thresholdLinear + 1e-9f)); // gentle expansion
        gateGain = 0.999f * gateGain + 0.001f * target; // very slow smooth
        return x * gateGain;
    }

private:
    double sr              = 44100.0;
    float  thresholdLinear = juce::Decibels::decibelsToGain(-55.0f); // below noise floor
    float  attackCoeff     = 0.0f;
    float  releaseCoeff    = 0.0f;
    float  envelope        = 0.0f;
    float  gateGain        = 1.0f;
};

} // namespace dadbass
