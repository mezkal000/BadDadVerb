#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>

namespace dadbass
{

/**
 * CabinetBlock — synchronous biquad cabinet voicing.
 * Replaces convolution (which has async IR loading, channel-count pitfalls).
 * Four biquad stages per channel:
 *   1. Low-shelf  +5 dB @ 100 Hz   — cabinet thump
 *   2. Peak       +4 dB @ 380 Hz   — speaker resonance
 *   3. Peak       -3 dB @ 2.5 kHz  — presence dip (cabinet air rolloff)
 *   4. High-shelf -10 dB @ 4 kHz   — natural HF rolloff
 */
class CabinetBlock
{
public:
    CabinetBlock() = default;

    void prepare(const juce::dsp::ProcessSpec& spec)
    {
        sr = spec.sampleRate;
        buildCoeffs();
        // Reset all state
        for (auto& ch : state) ch.fill({});
    }

    void process(juce::dsp::AudioBlock<float>& block)
    {
        for (size_t ch = 0; ch < block.getNumChannels(); ++ch)
        {
            // Use ch index clamped to 2 — both channels use same coeffs, separate state
            const size_t si = (ch < 2) ? ch : 1;
            float* s = block.getChannelPointer(ch);
            const int N = (int)block.getNumSamples();
            for (int i = 0; i < N; ++i)
            {
                float x = s[i];
                for (int b = 0; b < NumBands; ++b)
                    x = processBiquad(x, coeffs[b], state[si][b]);
                s[i] = x;
            }
        }
    }

private:
    struct BiquadCoeffs { float b0, b1, b2, a1, a2; };
    struct BiquadState  { float x1=0, x2=0, y1=0, y2=0; };

    static constexpr int NumBands = 4;
    double sr = 44100.0;
    std::array<BiquadCoeffs, NumBands> coeffs{};
    // 2 channels × NumBands stages
    std::array<std::array<BiquadState, NumBands>, 2> state{};

    inline float processBiquad(float x, const BiquadCoeffs& c, BiquadState& s) noexcept
    {
        const float y = c.b0*x + c.b1*s.x1 + c.b2*s.x2 - c.a1*s.y1 - c.a2*s.y2;
        s.x2 = s.x1; s.x1 = x;
        s.y2 = s.y1; s.y1 = y;
        return y;
    }

    // Low-shelf: H(s) = A * (s/w0 + A) / (A*s/w0 + 1)  [standard cookbook]
    BiquadCoeffs lowShelf(float freqHz, float gainDb) const
    {
        const float A  = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * juce::MathConstants<float>::pi * freqHz / float(sr);
        const float cosw = std::cos(w0);
        const float S    = 1.0f;
        const float alpha = std::sin(w0) * 0.5f * std::sqrt((A + 1.0f/A)*(1.0f/S - 1.0f) + 2.0f);
        const float a0inv = 1.0f / ((A+1) + (A-1)*cosw + 2*std::sqrt(A)*alpha);
        return {
              A*((A+1) - (A-1)*cosw + 2*std::sqrt(A)*alpha) * a0inv,
            2*A*((A-1) - (A+1)*cosw)                        * a0inv,
              A*((A+1) - (A-1)*cosw - 2*std::sqrt(A)*alpha) * a0inv,
           -2*((A-1) + (A+1)*cosw)                          * a0inv,
              ((A+1) + (A-1)*cosw - 2*std::sqrt(A)*alpha)   * a0inv
        };
    }

    // Peaking EQ: from Audio EQ Cookbook
    BiquadCoeffs peak(float freqHz, float gainDb, float Q) const
    {
        const float A    = std::pow(10.0f, gainDb / 40.0f);
        const float w0   = 2.0f * juce::MathConstants<float>::pi * freqHz / float(sr);
        const float alpha = std::sin(w0) / (2.0f * Q);
        const float a0inv = 1.0f / (1.0f + alpha/A);
        return {
            (1.0f + alpha*A)  * a0inv,
            -2.0f*std::cos(w0) * a0inv,
            (1.0f - alpha*A)  * a0inv,
            -2.0f*std::cos(w0) * a0inv,   // a1 (negated stored)
            (1.0f - alpha/A)  * a0inv      // a2
        };
    }

    // High-shelf
    BiquadCoeffs highShelf(float freqHz, float gainDb) const
    {
        const float A  = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * juce::MathConstants<float>::pi * freqHz / float(sr);
        const float cosw = std::cos(w0);
        const float S    = 1.0f;
        const float alpha = std::sin(w0) * 0.5f * std::sqrt((A + 1.0f/A)*(1.0f/S - 1.0f) + 2.0f);
        const float a0inv = 1.0f / ((A+1) - (A-1)*cosw + 2*std::sqrt(A)*alpha);
        return {
              A*((A+1) + (A-1)*cosw + 2*std::sqrt(A)*alpha) * a0inv,
           -2*A*((A-1) + (A+1)*cosw)                        * a0inv,
              A*((A+1) + (A-1)*cosw - 2*std::sqrt(A)*alpha) * a0inv,
            2*((A-1) - (A+1)*cosw)                          * a0inv,
              ((A+1) - (A-1)*cosw - 2*std::sqrt(A)*alpha)   * a0inv
        };
    }

    void buildCoeffs()
    {
        coeffs[0] = lowShelf (100.0f,  +5.0f);
        coeffs[1] = peak     (380.0f,  +4.0f, 2.5f);
        coeffs[2] = peak     (2500.0f, -3.0f, 1.8f);
        coeffs[3] = highShelf(4000.0f, -10.0f);
    }
};

} // namespace dadbass
