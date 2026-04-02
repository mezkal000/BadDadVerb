#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>
#include "DSPHelpers.h"

namespace dadbass
{

/**
 * TriodeStage — simple, stable triode waveshaper.
 * Uses std::tanh (always bounded), DC-blocked output, sag.
 */
class TriodeStage
{
public:
    TriodeStage() = default;

    void prepare(double sampleRate) noexcept
    {
        sr          = sampleRate;
        // HPF at 20 Hz
        dcCoeff     = 1.0f - (2.0f * juce::MathConstants<float>::pi * 20.0f / float(sr));
        dcCoeff     = juce::jlimit(0.0f, 0.9999f, dcCoeff);
        humPhase    = 0.0f;
        wobblePhase = 0.0f;
        flutterPhase= 0.0f;
        sagEnvelope = 0.0f;
        dcPrev      = 0.0f;
        tickCounter = 0;
    }

    void setDrive(float d)           noexcept { drive         = juce::jlimit(1.0f, 8.0f, d); }
    void setBias(float b)            noexcept { bias          = b; }
    void setSagAmount(float s)       noexcept { sagAmount     = s; }
    void setColdWarAmount(float c)   noexcept { coldWarAmount = c; }

    [[nodiscard]] inline float process(float x, bool coldWar, bool realShit) noexcept
    {
        // Clamp input to prevent any possibility of explosion
        x = juce::jlimit(-2.0f, 2.0f, x);

        // Sag envelope (soft dynamic compression of input gain)
        sagEnvelope = 0.999f * sagEnvelope + 0.001f * std::abs(x);
        const float sagGain = 1.0f / (1.0f + sagAmount * sagEnvelope * 2.0f);

        // Drive + waveshape — std::tanh is always in (-1, 1)
        float s = std::tanh(x * drive * sagGain + bias);

        // Cold War: 50Hz hum + wobble
        if (coldWar)
        {
            humPhase += (2.0f * juce::MathConstants<float>::pi * 50.0f) / float(sr);
            if (humPhase > juce::MathConstants<float>::twoPi)
                humPhase -= juce::MathConstants<float>::twoPi;
            s += coldWarAmount * 0.15f * std::sin(humPhase);
            s += coldWarAmount * 0.05f * std::sin(humPhase * 2.0f);

            wobblePhase += (2.0f * juce::MathConstants<float>::pi * 0.8f) / float(sr);
            if (wobblePhase > juce::MathConstants<float>::twoPi)
                wobblePhase -= juce::MathConstants<float>::twoPi;
            s *= (1.0f + coldWarAmount * 0.18f * std::sin(wobblePhase));
        }

        // Real Shit: bit-crush + flutter + crackle
        if (realShit)
        {
            s = std::round(s * 32.0f) / 32.0f;

            flutterPhase += (2.0f * juce::MathConstants<float>::pi * 8.3f) / float(sr);
            if (flutterPhase > juce::MathConstants<float>::twoPi)
                flutterPhase -= juce::MathConstants<float>::twoPi;
            s *= (1.0f + 0.10f * std::sin(flutterPhase));

            tickCounter = (tickCounter + 1) % 607;
            if (tickCounter == 0)
                s += (juce::Random::getSystemRandom().nextFloat() * 2.0f - 1.0f) * 0.25f;

            s += (juce::Random::getSystemRandom().nextFloat() * 2.0f - 1.0f) * 0.015f;
        }

        // DC block: simple one-pole HPF  y[n] = x[n] - x[n-1] + c*y[n-1]
        // Rewritten as: y[n] = (x[n] - x[n-1]) * 1 + dcCoeff * y[n-1]
        // where dcCoeff ~ 0.9997 for 20Hz @ 44.1kHz
        const float out = s - dcPrev_x + dcCoeff * dcPrev_y;
        dcPrev_x = s;
        dcPrev_y = juce::jlimit(-2.0f, 2.0f, out); // clamp state too

        return juce::jlimit(-1.5f, 1.5f, out);
    }

private:
    double sr            = 44100.0;
    float  drive         = 2.6f;
    float  bias          = 0.018f;
    float  sagAmount     = 0.3f;
    float  coldWarAmount = 0.35f;

    float  sagEnvelope   = 0.0f;
    float  humPhase      = 0.0f;
    float  wobblePhase   = 0.0f;
    float  flutterPhase  = 0.0f;
    float  dcCoeff       = 0.9997f;
    float  dcPrev_x      = 0.0f;
    float  dcPrev_y      = 0.0f;
    float  dcPrev        = 0.0f; // unused, kept for compat
    int    tickCounter   = 0;
};

} // namespace dadbass
