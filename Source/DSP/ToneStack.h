#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

namespace dadbass
{

/**
 * ToneStack — 4-band EQ voiced for Soviet bass amp.
 *   Bass: low shelf @120Hz   Mid: peak @440Hz
 *   Treble: high shelf @3.5kHz   Presence: peak @4.5kHz
 *
 * Coefficients are only rebuilt when values actually change,
 * preventing zipper noise from per-block coefficient updates.
 */
class ToneStack
{
public:
    ToneStack() = default;

    void prepare(const juce::dsp::ProcessSpec& spec)
    {
        currentSpec = spec;
        chain.prepare(spec);
        // Force initial coefficient build
        lastBass = lastMid = lastTreble = lastPresence = -999.0f;
        update(0.0f, 0.0f, 0.0f, 0.0f);
    }

    // Call from parameter update — only rebuilds coefficients when values change
    void update(float bassDb, float midDb, float trebleDb, float presenceDb)
    {
        // FIX: guard — don't rebuild IIR coefficients every block, only on change
        if (bassDb     == lastBass     && midDb    == lastMid &&
            trebleDb   == lastTreble   && presenceDb == lastPresence)
            return;

        using C = juce::dsp::IIR::Coefficients<float>;
        const float sr = float(currentSpec.sampleRate > 0 ? currentSpec.sampleRate : 44100.0);

        *chain.get<0>().coefficients = *C::makeLowShelf  (sr, 120.0f,  0.71f, juce::Decibels::decibelsToGain(bassDb));
        *chain.get<1>().coefficients = *C::makePeakFilter(sr, 440.0f,  0.9f,  juce::Decibels::decibelsToGain(midDb));
        *chain.get<2>().coefficients = *C::makeHighShelf (sr, 3500.0f, 0.71f, juce::Decibels::decibelsToGain(trebleDb));
        *chain.get<3>().coefficients = *C::makePeakFilter(sr, 4500.0f, 1.4f,  juce::Decibels::decibelsToGain(presenceDb));

        lastBass     = bassDb;
        lastMid      = midDb;
        lastTreble   = trebleDb;
        lastPresence = presenceDb;
    }

    void process(juce::dsp::AudioBlock<float>& block)
    {
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        chain.process(ctx);
    }

private:
    juce::dsp::ProcessSpec currentSpec { 44100.0, 512, 2 };
    using Filter = juce::dsp::IIR::Filter<float>;
    juce::dsp::ProcessorChain<Filter, Filter, Filter, Filter> chain;

    // Last-known values to detect real changes
    float lastBass     = -999.0f;
    float lastMid      = -999.0f;
    float lastTreble   = -999.0f;
    float lastPresence = -999.0f;
};

} // namespace dadbass
