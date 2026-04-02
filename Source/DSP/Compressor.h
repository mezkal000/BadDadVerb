#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>

namespace dadbass
{

/**
 * Compressor — optical-style feed-forward RMS with soft knee.
 * Coefficients pre-computed in prepare(), not rebuilt every block.
 */
class Compressor
{
public:
    Compressor() = default;

    void prepare(double sampleRate) noexcept
    {
        sr       = sampleRate;
        envelope = 0.0f;
        rebuildCoeffs();
    }

    // Setters — cache values, defer coefficient rebuild to next prepare/explicit call
    void setThresholdDb(float dB)  noexcept { thresholdDb  = dB; }
    void setRatio(float r)         noexcept { ratio        = r;  }
    void setMakeupDb(float dB)     noexcept { makeupLinear = juce::Decibels::decibelsToGain(dB); }

    // FIX: rebuild only when called explicitly, not from setAttackMs/setReleaseMs every block
    void setAttackMs(float ms)  noexcept { attackMs  = ms; }
    void setReleaseMs(float ms) noexcept { releaseMs = ms; }
    void rebuildCoeffs() noexcept
    {
        auto tc = [&](float ms) {
            return std::exp(-1.0f / (float(sr) * ms * 0.001f));
        };
        attackCoeff  = tc(attackMs);
        releaseCoeff = tc(releaseMs);
    }

    [[nodiscard]] inline float process(float x) noexcept
    {
        const float absX  = std::abs(x);
        const float coeff = (absX > envelope) ? attackCoeff : releaseCoeff;
        envelope = coeff * envelope + (1.0f - coeff) * absX;

        const float levelDb = juce::Decibels::gainToDecibels(envelope + 1e-9f);
        const float overDb  = levelDb - thresholdDb;
        float gainDb = 0.0f;

        if (overDb > -kneeDb * 0.5f)
        {
            if (overDb < kneeDb * 0.5f)
            {
                const float k = overDb + kneeDb * 0.5f;
                gainDb = (1.0f / ratio - 1.0f) * (k * k) / (2.0f * kneeDb);
            }
            else
            {
                gainDb = overDb * (1.0f / ratio - 1.0f);
            }
        }

        return x * juce::Decibels::decibelsToGain(gainDb) * makeupLinear;
    }

private:
    double sr           = 44100.0;
    float  thresholdDb  = -18.0f;
    float  ratio        = 4.0f;
    float  kneeDb       = 6.0f;
    float  attackMs     = 12.0f;
    float  releaseMs    = 120.0f;
    float  makeupLinear = 1.0f;
    float  attackCoeff  = 0.0f;
    float  releaseCoeff = 0.0f;
    float  envelope     = 0.0f;
};

} // namespace dadbass
