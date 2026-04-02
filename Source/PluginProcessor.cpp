#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>   // std::reverse
#include <cmath>       // std::pow, std::abs, std::round, std::ceil, std::floor
#include <chrono>      // high_resolution_clock for psyOp seed

// ── IRConversionThread::run() ─────────────────────────────────────────────────
// Wakes on triggerConversion(), runs AudioToIR::convert(sourceAudio → rawIR),
// then sets needsIRRebuild so the 60Hz timer calls rebuildIR() safely.
void IRConversionThread::run()
{
    while (!threadShouldExit())
    {
        wait (1000);   // sleep until triggered or 1s keepalive
        if (threadShouldExit()) break;

        const bool mainConv = proc.needsConversion.load(std::memory_order_acquire);
        const bool psyConv  = proc.needsPsyConversion.load(std::memory_order_acquire);
        if (!mainConv && !psyConv) continue;

        // Claim both flags immediately — prevents double-processing and spin loops.
        // Main conversion takes priority: if both are set, we run main conversion
        // params. isPsyConv is true only when psyOp is the sole pending conversion.
        proc.needsConversion.store(false, std::memory_order_release);
        proc.needsPsyConversion.store(false, std::memory_order_release);
        const bool isPsyConv = psyConv && !mainConv;

        // Guard: need a valid sample rate before we can convert
        if (proc.currentSampleRate < 1.0) continue;

        // Copy source audio under lock — audio thread may write sourceAudio at any time
        juce::AudioBuffer<float> srcCopy;
        {
            const juce::ScopedLock sl(proc.irLock);
            if (proc.sourceAudio.getNumSamples() == 0) continue;
            srcCopy.makeCopyOf(proc.sourceAudio, true);
        }

        // Build conversion params
        dadbass::AudioToIR::Params p;
        p.sampleRate = proc.currentSampleRate;
        const float durS = (float)srcCopy.getNumSamples() / (float)proc.currentSampleRate;

        if (isPsyConv)
        {
            // PsyOp: preserve current IR length — only spectral character changes.
            float baseLength = 0.0f;
            {
                const juce::ScopedLock sl(proc.irLock);
                if (proc.rawIR.getNumSamples() > 0)
                    baseLength = (float)proc.rawIR.getNumSamples() / (float)proc.currentSampleRate;
            }
            if (baseLength > 0.1f)
            {
                p.t60      = juce::jlimit(0.2f, 3.5f, baseLength);
                p.irLength = juce::jmin(baseLength, 3.0f);
            }
            else
            {
                p.t60      = juce::jlimit(0.2f, 3.5f, durS * 0.7f);
                p.irLength = juce::jmin(p.t60 * 1.2f, 3.0f);
            }
            p.brightness = 0.9f;
            p.diffusion  = 0.95f;
            p.seed = (int)(proc.currentSampleRate * 0.1f) ^
                     (int)std::chrono::high_resolution_clock::now().time_since_epoch().count();
        }
        else
        {
            p.t60        = juce::jlimit(0.2f, 3.5f, durS * 0.7f);
            p.irLength   = juce::jmin(p.t60 * 1.2f, 3.0f);
            p.brightness = proc.apvts.getRawParameterValue("freq")->load();
            p.diffusion  = 0.5f + 0.5f * (1.f - proc.apvts.getRawParameterValue("res")->load());
            p.seed       = srcCopy.getNumSamples();
        }

        // Heavy FFT conversion — off message+audio threads
        juce::AudioBuffer<float> converted;
        dadbass::AudioToIR::convert(srcCopy, converted, p);
        if (threadShouldExit()) break;

        if (converted.getNumSamples() > 0)
        {
            {
                const juce::ScopedLock sl(proc.irLock);
                proc.rawIR.makeCopyOf(converted, true);
                if (proc.isReversed && proc.rawIR.getNumSamples() > 1)
                {
                    auto* d = proc.rawIR.getWritePointer(0);
                    std::reverse(d, d + proc.rawIR.getNumSamples());
                }
            }
            proc.needsIRRebuild.store(true, std::memory_order_release);
        }
    }
}

BadDadVerbAudioProcessor::BadDadVerbAudioProcessor()
    : AudioProcessor(BusesProperties()
                       .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
                       .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "BADDADVERB", createParameterLayout())
{
    conversionThread.startThread(juce::Thread::Priority::low);
}

juce::AudioProcessorValueTreeState::ParameterLayout BadDadVerbAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    auto dbRange = juce::NormalisableRange<float>(-30.0f,  0.0f, 0.01f);
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"input", 1},    "Input",    dbRange, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"mix", 1},      "Mix",      juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.30f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"predel", 1},   "PreDelay", juce::NormalisableRange<float>(0.0f, 150.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"output", 1},   "Output",   juce::NormalisableRange<float>(-24.0f,  6.0f, 0.01f), -2.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"irtime", 1},   "IR Time",  juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"irhpf", 1},    "IR HPF",   juce::NormalisableRange<float>(20.0f, 1500.0f, 1.0f, 0.35f), 140.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"freq", 1},     "Freq",     juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"res", 1},      "Res",      juce::NormalisableRange<float>(0.0f, 0.95f, 0.001f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"limit", 1},    "Limit",    juce::NormalisableRange<float>(-12.0f, -0.2f, 0.01f), -0.2f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"trimstart", 1},"Trim Start",juce::NormalisableRange<float>(0.03f, 0.95f, 0.001f), 0.03f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"trimend", 1},  "Trim End", juce::NormalisableRange<float>(0.05f, 0.97f, 0.001f), 0.97f));
    return { params.begin(), params.end() };
}

bool BadDadVerbAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    auto in  = layouts.getMainInputChannelSet();
    auto out = layouts.getMainOutputChannelSet();
    return (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo())
        && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
        && in == out;
}

void BadDadVerbAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    maxRecordSamples  = (int) std::round(sampleRate * 5.0);
    // Re-trigger IR conversion every 1 second of live audio
    psyOpRetrigger = (int) std::round(sampleRate * 0.3);  // retrigger every 0.3s
    psyOpWrite = 0; psyOpFilled = 0; psyOpCounter = 0;
    std::fill(std::begin(psyOpBuf), std::end(psyOpBuf), 0.f);

    recordBuffer.setSize(1, maxRecordSamples, false, true, true);
    recordBuffer.clear();
    dryBuffer.setSize(2, samplesPerBlock, false, false, true);

    juce::dsp::ProcessSpec spec{ sampleRate,
                                 (juce::uint32) samplesPerBlock,
                                 (juce::uint32) juce::jmax(1, getTotalNumOutputChannels()) };
    convolver.reset();
    convolver.prepare(spec);
    limiter.prepare(spec);

    // After prepare, re-trigger IR load — convolver.reset() cleared it.
    // Logic Pro calls prepareToPlay on every play press; this ensures
    // the IR survives those calls.
    if (rawIR.getNumSamples() > 0)
        needsIRRebuild.store(true, std::memory_order_release);
    else if (sourceAudio.getNumSamples() > 0)
    {
        needsConversion.store(true, std::memory_order_release);
        conversionThread.triggerConversion();
    }

    const float limitParam  = apvts.getRawParameterValue("limit")->load();
    const float limitThresh = (limitParam + 0.2f) * (12.0f / 11.8f);  // 0dB at off, -12dB at max
    limiter.setThreshold(limitThresh);
    limiter.setRelease(90.0f);
    lastLimitThreshold = limitParam;

    // Prepare smoothers — 20ms ramp
    const double ramp = 0.020;
    inputSmooth .reset(sampleRate, ramp);
    mixSmooth      .reset(sampleRate, ramp);
    predelaySmooth .reset(sampleRate, 0.05);   // 50ms ramp to avoid clicks on predelay change
    outputSmooth   .reset(sampleRate, ramp);
    freqSmooth     .reset(sampleRate, ramp);
    resSmooth      .reset(sampleRate, ramp);
    hpfSmooth      .reset(sampleRate, 0.02);   // 20ms ramp for HPF cutoff

    freqSmooth  .setCurrentAndTargetValue(apvts.getRawParameterValue("freq")->load());
    resSmooth   .setCurrentAndTargetValue(apvts.getRawParameterValue("res")->load());
    // Precompute overload compressor coefficients for this sample rate
    overloadAttack  = std::exp(-1.0f / (0.001f * (float)sampleRate));  // 1ms
    overloadRelease = std::exp(-1.0f / (0.080f * (float)sampleRate));  // 80ms
    overloadEnv[0] = overloadEnv[1] = 0.f;  // FIX BUG-14: reset envelope on SR change
    for (auto& m : moog)  { m.reset(); m.prime(0.f); }
    for (auto& h : hpWet) h.reset();
    for (auto& f : airFilter) { f.reset(); f.prepare(9000.0f, (float)sampleRate); }
    for (auto& s : shelfWet) { s.reset(); s.setLowShelf(400.f, -24.f, (float)sampleRate); }
    resetPredelay();
    resetMindPush();
    inputSmooth    .setCurrentAndTargetValue(juce::Decibels::decibelsToGain(apvts.getRawParameterValue("input")->load()));
    mixSmooth      .setCurrentAndTargetValue(apvts.getRawParameterValue("mix")->load());
    {
        const float pdMs = apvts.getRawParameterValue("predel")->load();
        predelaySmooth.setCurrentAndTargetValue(pdMs * (float)sampleRate / 1000.f);
    }
    outputSmooth   .setCurrentAndTargetValue(juce::Decibels::decibelsToGain(apvts.getRawParameterValue("output")->load()));
    hpfSmooth      .setCurrentAndTargetValue(apvts.getRawParameterValue("irhpf")->load());
}

void BadDadVerbAudioProcessor::releaseResources()
{
    convolver.reset();
}

void BadDadVerbAudioProcessor::captureScope(const juce::AudioBuffer<float>& buffer)
{
    const auto* src = buffer.getReadPointer(0);
    auto wp = scopeWritePos.load(std::memory_order_relaxed);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        scopeRing[(size_t) wp] = src[i];
        wp = (wp + 1) % ScopeSize;
    }
    scopeWritePos.store(wp, std::memory_order_release);
}

void BadDadVerbAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int N = buffer.getNumSamples();

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear(ch, 0, N);

    // Update smoother targets (input gain applied separately below)
    mixSmooth      .setTargetValue(apvts.getRawParameterValue("mix")->load());
    {
        const float pdMs = apvts.getRawParameterValue("predel")->load();
        predelaySmooth.setTargetValue(pdMs * (float)currentSampleRate / 1000.f);
    }
    outputSmooth   .setTargetValue(juce::Decibels::decibelsToGain(apvts.getRawParameterValue("output")->load()));
    freqSmooth     .setTargetValue(apvts.getRawParameterValue("freq")->load());
    resSmooth      .setTargetValue(apvts.getRawParameterValue("res")->load());
    hpfSmooth      .setTargetValue(apvts.getRawParameterValue("irhpf")->load());

    // IR rebuild when TIME semitones changes — post to message thread to avoid
    // stalling the audio thread with convolver.loadImpulseResponse()
    const float irSemi = apvts.getRawParameterValue("irtime")->load();
    if (std::abs(irSemi - lastIRSemitones) > 0.05f && rawIR.getNumSamples() > 0)
    {
        lastIRSemitones = irSemi;
        needsIRRebuild.store(true, std::memory_order_release);
    }

    // Only update limiter when threshold actually changed
    const float limitParam  = apvts.getRawParameterValue("limit")->load();
    if (limitParam != lastLimitThreshold)
    {
        const float limitThresh = (limitParam + 0.2f) * (12.0f / 11.8f);
        limiter.setThreshold(limitThresh);
        lastLimitThreshold = limitParam;
    }
    // limiterLamp decay is handled by the 60Hz editor timer — not reset here

    // Apply smoothed input gain per-sample.
    // Make one copy of the smoother per block, then apply the same ramp to each channel
    // so the gain curve is identical across channels (correct behaviour).
    {
        const float targetGain = juce::Decibels::decibelsToGain(apvts.getRawParameterValue("input")->load());
        inputSmooth.setTargetValue(targetGain);
        auto smoothCopy = inputSmooth;           // snapshot ramp state before any channel
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto* data = buffer.getWritePointer(ch);
            auto perChanSmooth = smoothCopy;     // each channel gets the same ramp
            for (int i = 0; i < N; ++i)
                data[i] *= perChanSmooth.getNextValue();
        }
        // Advance the master smoother by one block so its state stays current
        for (int i = 0; i < N; ++i) inputSmooth.getNextValue();
    }

    // Input level metering
    float peak = 0.0f;
    for (int ch = 0; ch < juce::jmin(2, buffer.getNumChannels()); ++ch)
        peak = juce::jmax(peak, buffer.getMagnitude(ch, 0, N));
    inputLevelDb.store(juce::jlimit(-60.0f, 3.0f, juce::Decibels::gainToDecibels(peak, -60.0f)),
                       std::memory_order_relaxed);

    // ── PsyOp: continuous live IR modulation ────────────────────────────────
    // Writes incoming audio into a rolling circular buffer.
    // Every psyOpRetrigger samples, takes a snapshot and re-converts to IR.
    // The predelay on the wet signal provides the perceptual buffer between
    // input and the reverb character updating.
    if (psyOpActive.load())
    {
        const auto* left  = buffer.getReadPointer(0);
        const auto* right = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : nullptr;
        for (int i = 0; i < N; ++i)
        {
            const float mono = right ? 0.5f * (left[i] + right[i]) : left[i];
            psyOpBuf[psyOpWrite] = mono;
            psyOpWrite = (psyOpWrite + 1) % psyOpBufSize;
            if (psyOpFilled < psyOpBufSize) ++psyOpFilled;
        }
        psyOpCounter += N;
        if (psyOpCounter >= psyOpRetrigger && psyOpFilled >= 512)
        {
            psyOpCounter = 0;
            // Snapshot the rolling buffer into sourceAudio (linearised)
            // and re-trigger conversion on the background thread.
            // We use min(filled, 2s) to keep conversion fast.
            // Snapshot 0.4s of audio — short enough to capture a specific moment,
            // long enough for AudioToIR spectral analysis.
            const int snapLen = juce::jmin(psyOpFilled,
                                           (int)(currentSampleRate * 0.4));
            {
                // PsyOp uses a TEMPORARY buffer — never overwrites sourceAudioFwd
                // which is the canonical loaded source used for all other operations.
                juce::AudioBuffer<float> psyTmp(1, snapLen);
                auto* dst = psyTmp.getWritePointer(0);
                for (int k = 0; k < snapLen; ++k)
                {
                    const int readIdx = (psyOpWrite - snapLen + k
                                         + psyOpBufSize) % psyOpBufSize;
                    dst[k] = psyOpBuf[readIdx];
                }
                // Heavy saturation to exaggerate spectral character
                for (int k = 0; k < snapLen; ++k)
                    dst[k] = std::tanh(dst[k] * 6.0f);

                const juce::ScopedLock sl(irLock);
                // Store as sourceAudio only (not Fwd) so psyOp doesn't corrupt
                // the canonical source that REV, trim, and preset save rely on
                sourceAudio.makeCopyOf(psyTmp, true);
            }
            needsPsyConversion.store(true, std::memory_order_release);
            conversionThread.triggerConversion();
        }
    }

    // Legacy one-shot recording (used by startRecordingIR/stopRecordingIR)
    if (recordingIR.load())
    {
        const int toCopy = juce::jmin(N, maxRecordSamples - recordWritePos);
        auto*       dst   = recordBuffer.getWritePointer(0);
        const auto* left  = buffer.getReadPointer(0);
        const auto* right = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : nullptr;
        for (int i = 0; i < toCopy; ++i)
            dst[recordWritePos + i] = right ? 0.5f * (left[i] + right[i]) : left[i];
        recordWritePos += toCopy;
        if (recordWritePos >= maxRecordSamples)
            stopRecordingIR();
    }

    captureScope(buffer);

    // Keep dry copy — setSize with keepExisting=false avoids reallocation when
    // channel/sample count is unchanged (which is the common case).
    dryBuffer.setSize(buffer.getNumChannels(), N, false, false, true);
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        dryBuffer.copyFrom(ch, 0, buffer, ch, 0, N);

    // Convolution
    if (activeIR.getNumSamples() > 0)
    {
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        convolver.process(ctx);
    }
    else
    {
        buffer.clear();
    }

    // Wet auto-gain removed — use output knob + limit knob to control level

    // ── Wet-signal processing + dry/wet blend ────────────────────────────────
    // Order (wet signal only):
    //   0. Low shelf  — fixed -12 dB @ 200 Hz, tames reverb low-end buildup
    //   1. Predelay   — circular buffer, 0-150 ms
    //   2. HPF        — 1st-order highpass, real-time, wet only
    //   3. LP filter  — 4-pole Moog ladder, wet only
    //   4. Soft saturation — tanh knee, prevents harsh peaks
    //   5. Blend      — equal-power crossfade dry/wet
    //
    // Advance all smoothers once into arrays, then replay per channel.
    {
        // Stack arrays — no heap allocation per block (max block size 4096)
        float pdArr[4096], mixArr[4096], outArr[4096], freqArr[4096], resArr[4096], hpfArr[4096];
        jassert(N <= 4096);
        for (int i = 0; i < N; ++i)
        {
            pdArr[i]  = predelaySmooth.getNextValue();
            mixArr[i] = mixSmooth.getNextValue();
            outArr[i] = outputSmooth.getNextValue();
            freqArr[i]= freqSmooth.getNextValue();
            resArr[i] = resSmooth.getNextValue();
            hpfArr[i] = hpfSmooth.getNextValue();
        }

        const float SR    = (float)currentSampleRate;
        const float invSR = 1.0f / SR;
        const float twoPi = juce::MathConstants<float>::pi * 2.0f;

        // Precompute HPF alpha and Moog cutoff per sample (smoothers change slowly).
        // pow() and divisions are expensive — compute once per sample, reuse across channels.
        float hpfAlpha[4096], moogCut[4096];
        for (int i = 0; i < N; ++i)
        {
            // HPF alpha
            const float fc = juce::jmax(20.f, hpfArr[i]);
            const float rc = 1.0f / (twoPi * fc);
            hpfAlpha[i] = rc / (rc + invSR);

            // Moog cutoff: log Hz mapping → one-pole coefficient
            const float freqHz   = 80.0f * std::pow(225.0f, freqArr[i]);
            const float wn       = twoPi * freqHz * invSR;
            moogCut[i] = juce::jmin(wn / (wn + 1.0f) / 0.82f, 1.0f);
        }

        // Pass 1: per-channel wet processing (shelf → predelay → HPF → LP → air LP)
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const int chi = ch < 2 ? ch : 1;  // clamped channel index, computed once
            auto* wetData = buffer.getWritePointer(ch);
            auto& mf  = moog[chi];
            auto& hp  = hpWet[chi];
            auto& sh  = shelfWet[chi];
            auto& air = airFilter[chi];
            int&  pw  = predelayWrite[chi];
            float* pd = predelayBuf[chi];

            for (int i = 0; i < N; ++i)
            {
                float w = sh.process(wetData[i]);

                // Predelay — bitwise AND replaces % for power-of-2 buffer size
                const int delaySamp = juce::jlimit(0, maxPredelaySamples - 1, (int)pdArr[i]);
                pd[pw] = w;
                const int readPos = (pw - delaySamp + maxPredelaySamples) & (maxPredelaySamples - 1);
                w = pd[readPos];
                pw = (pw + 1) & (maxPredelaySamples - 1);

                w = hp.process(w, hpfAlpha[i]);
                w = mf.process(w, moogCut[i], resArr[i]);
                w = air.process(w);

                if (!std::isfinite(w)) w = 0.0f;
                wetData[i] = juce::jlimit(-4.0f, 4.0f, w);
            }
        }

        // ── Overload: heavy compression + saturation on wet signal ─────────
        // Ratio ≈100:1, threshold -18dBFS, fast attack (1ms), medium release (80ms).
        // Drives the compressed wet into tanh saturation for a crushed, aggressive
        // reverb character. Runs after the LP filter so the compression is coloured.
        const bool doOverload = overloadActive.load();
        if (doOverload)
        {
            const float attackCoeff  = overloadAttack;   // precomputed in prepareToPlay
            const float releaseCoeff = overloadRelease;  // precomputed in prepareToPlay
            const float threshold    = 0.25f;   // ≈ -12 dBFS
            const float ratio        = 100.0f;  // extreme: near-limiting
            const float makeupGain   = 6.0f;    // compensate for gain reduction

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            {
                auto* w = buffer.getWritePointer(ch);
                float& env = overloadEnv[juce::jmin(ch, 1)];
                for (int i = 0; i < N; ++i)
                {
                    const float absSample = std::abs(w[i]);
                    // Peak envelope follower
                    if (absSample > env)
                        env = attackCoeff  * env + (1.f - attackCoeff)  * absSample;
                    else
                        env = releaseCoeff * env + (1.f - releaseCoeff) * absSample;

                    // Gain computer: compress everything above threshold
                    float gain = 1.0f;
                    if (env > threshold)
                        gain = threshold + (env - threshold) / ratio;
                    gain = (gain / std::max(env, 1e-6f)) * makeupGain;

                    // Clamp gain before apply to prevent explosion at near-silence
                    gain = juce::jmin(gain, 20.0f);
                    w[i] = std::tanh(w[i] * gain * 2.0f) * 0.5f;
                }
            }
        }

        // ── Mind Push ────────────────────────────────────────────────────────
        // Stereo effect: both channels are mixed with a 30ms-delayed version,
        // but the delayed copy has the LEFT channel phase-inverted.
        // LFO cycles randomly 0.5–3 s, crossfade 0:100 → 50:50 and back.
        // Equal-power crossfade keeps the level steady throughout.
        if (isMindPush && buffer.getNumChannels() >= 2)
        {
            const int delay30ms = juce::jlimit(0, mindPushDelaySamples - 1,
                                               (int)(0.030f * SR));
            const int delay17ms = juce::jlimit(0, mindPushDelaySamples - 1,
                                               (int)(0.017f * SR));
            auto* leftWet  = buffer.getWritePointer(0);
            auto* rightWet = buffer.getWritePointer(1);

            for (int i = 0; i < N; ++i)
            {
                // Advance LFO and pick new cycle length when done
                // Range 2–8 s — longer cycles give the fade-ins more time to breathe
                mpPhase += 1.0f / (mpCycleLen * SR);
                if (mpPhase >= 1.0f)
                {
                    mpPhase -= 1.0f;
                    std::uniform_real_distribution<float> ud(2.0f, 8.0f);
                    mpCycleLen = ud(mpRng);
                }

                // Smooth blend: sin² arc 0→0.5→0 over one cycle.
                // The 400ms lag smoother makes the blend follow the LFO target
                // slowly — so even though the LFO moves continuously, the actual
                // crossfade eases in and out over several hundred milliseconds,
                // giving long, gentle fade-in / fade-out transitions.
                const float sinP = std::sin(mpPhase * juce::MathConstants<float>::pi);
                const float blendTarget = 0.5f * sinP * sinP;
                mpBlend += (blendTarget - mpBlend)
                           * (1.0f - std::exp(-1.0f / (0.400f * SR)));

                // Write current wet samples into delay buffers
                mindPushBuf[0][mindPushWrite] = leftWet[i];
                mindPushBuf[1][mindPushWrite] = rightWet[i];

                // Asymmetric delays per channel — avoids mono cancellation.
                // Left gets 30ms, right gets 17ms.  The time difference (13ms)
                // creates stereo width via the Haas effect without phase inversion.
                const int readPL = (mindPushWrite - delay30ms + mindPushDelaySamples)
                                   & (mindPushDelaySamples - 1);
                const int readPR = (mindPushWrite - delay17ms + mindPushDelaySamples)
                                   & (mindPushDelaySamples - 1);

                // No phase inversion — pure time-offset stereo spread
                const float delayedL = mindPushBuf[0][readPL];
                const float delayedR = mindPushBuf[1][readPR];

                mindPushWrite = (mindPushWrite + 1) & (mindPushDelaySamples - 1);

                // Equal-power crossfade with the delayed signal
                const float mpBlendC = juce::jlimit(0.0f, 1.0f, mpBlend);
                const float gainOrig = std::sqrt(1.0f - mpBlendC);
                const float gainDel  = std::sqrt(mpBlendC);
                leftWet[i]  = leftWet[i]  * gainOrig + delayedL * gainDel;
                rightWet[i] = rightWet[i] * gainOrig + delayedR * gainDel;
            }
        }

        // Pass 2: apply soft limiter to wet signal only, then blend dry+wet.
        // Hard clip at threshold — clean, no saturation distortion.
        // Signals below threshold pass through with zero processing.
        {
            const float threshLinear = juce::Decibels::decibelsToGain((lastLimitThreshold + 0.2f) * (12.0f / 11.8f));

            float wetPeak = 0.0f;
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            {
                auto* w = buffer.getWritePointer(ch);
                for (int i = 0; i < N; ++i)
                {
                    wetPeak = juce::jmax(wetPeak, std::abs(w[i]));
                    if (std::abs(w[i]) > threshLinear)
                        w[i] = std::copysign(threshLinear, w[i]);
                }
            }
            limiterLamp.store(wetPeak > threshLinear, std::memory_order_relaxed);
        }

        // Precompute wet/dry gains for each sample (shared across channels)
        float wGArr[4096], dryGArr[4096];
        for (int i = 0; i < N; ++i)
        {
            const float wG = mixArr[i] * mixArr[i];
            wGArr[i]   = wG;
            dryGArr[i] = std::sqrt(juce::jmax(0.0f, 1.0f - wG * wG));
        }
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto*       wetData = buffer.getWritePointer(ch);
            const auto* dryData = dryBuffer.getReadPointer(ch);
            for (int i = 0; i < N; ++i)
            {
                float out = (dryData[i] * dryGArr[i] + wetData[i] * wGArr[i]) * outArr[i];
                if (!std::isfinite(out)) out = 0.0f;
                wetData[i] = juce::jlimit(-1.0f, 1.0f, out);
            }
        }
    }
}

void BadDadVerbAudioProcessor::sanitizeBuffer(juce::AudioBuffer<float>& buffer)
{
    // Soft saturation instead of hard clip — tanh naturally rounds peaks,
    // preserving transient shape while preventing harsh digital clipping.
    // Still catches and zeros NaN/Inf to protect the DAW.
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* d = buffer.getWritePointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            if (!std::isfinite(d[i])) { d[i] = 0.0f; continue; }
            // tanh(x) stays in ±1, knee around ±0.7 — transparent at low levels
            d[i] = std::tanh(d[i]);
        }
    }
}

void BadDadVerbAudioProcessor::startRecordingIR()
{
    recordWritePos = 0;
    recordBuffer.clear();
    recordingIR.store(true);
}

void BadDadVerbAudioProcessor::stopRecordingIR()
{
    if (!recordingIR.exchange(false)) return;
    if (recordWritePos <= 8) return;

    // Save the raw recorded audio into sourceAudio (NOT rawIR).
    // Conversion happens on the background thread — non-blocking.
    {
        const juce::ScopedLock sl (irLock);
        sourceAudio.setSize(1, recordWritePos, false, true, true);
        sourceAudio.copyFrom(0, 0, recordBuffer, 0, 0, recordWritePos);
    }
    needsConversion.store(true, std::memory_order_release);
    conversionThread.triggerConversion();
}

float BadDadVerbAudioProcessor::getTrimStartNorm() const noexcept
{
    return apvts.getRawParameterValue("trimstart")->load();
}

float BadDadVerbAudioProcessor::getTrimEndNorm() const noexcept
{
    return apvts.getRawParameterValue("trimend")->load();
}

void BadDadVerbAudioProcessor::applyTrimFromParameters() { rebuildIR(); }

bool BadDadVerbAudioProcessor::loadIRFromMemory(const void* data, int sizeBytes)
{
    // Decode WAV bytes → mono PCM (fast — just format parse + memcpy).
    // IMPORTANT: createReaderFor() always deletes the stream it receives,
    // regardless of deleteStreamIfOpeningFails. We heap-allocate the stream
    // and pass ownership (true) so the reader destructor can free it safely.
    // Passing a stack-allocated MemoryInputStream causes a double-free crash.
    juce::WavAudioFormat wavFmt;
    auto* mis = new juce::MemoryInputStream(data, (size_t) sizeBytes, false);
    std::unique_ptr<juce::AudioFormatReader> reader(
        wavFmt.createReaderFor(mis, true));   // reader now owns mis
    if (!reader || reader->lengthInSamples <= 0) return false;

    const int n  = (int) reader->lengthInSamples;
    const int ch = (int) reader->numChannels;

    juce::AudioBuffer<float> buf(ch, n);
    reader->read(&buf, 0, n, 0, true, ch >= 2);

    juce::AudioBuffer<float> mono(1, n);
    if (ch >= 2)
        for (int i = 0; i < n; ++i)
            mono.setSample(0, i, (buf.getSample(0, i) + buf.getSample(1, i)) * 0.5f);
    else
        mono.copyFrom(0, 0, buf, 0, 0, n);

    // Store source audio — heavy AudioToIR::convert() runs on background thread
    {
        const juce::ScopedLock sl (irLock);
        sourceAudioFwd.makeCopyOf(mono, true);  // canonical forward copy
        sourceAudio.makeCopyOf(mono, true);
        displayIR.makeCopyOf(mono, true);       // freeze display waveform here
        // Factory preset always loads forward — reset reversal state
        isReversed = false;
        reversalChangedExternal.store(true);    // tell editor to sync button
    }
    needsConversion.store(true, std::memory_order_release);
    conversionThread.triggerConversion();   // returns immediately
    return true;
}

void BadDadVerbAudioProcessor::rebuildIRIfNeeded()
{
    // Block on main conversions only — psyOp uses needsPsyConversion separately
    // and never blocks TIME/trim/REV rebuilds.
    if (needsConversion.load(std::memory_order_acquire)) return;

    if (!needsIRRebuild.exchange(false, std::memory_order_acq_rel)) return;

    rebuildIR();
}

void BadDadVerbAudioProcessor::reverseIR()
{
    setReversed(!isReversed);
}

void BadDadVerbAudioProcessor::setReversed(bool shouldBeReversed)
{
    // Reversal works on rawIR directly — FFT magnitude is identical for
    // forward and reversed audio, so re-converting doesn't change anything.
    // We flip rawIR and rebuild the convolver immediately.
    // isReversed is also checked by the conversion thread so every future
    // conversion automatically produces the correct direction.
    if (shouldBeReversed == isReversed) return;  // no change
    isReversed = shouldBeReversed;

    {
        const juce::ScopedLock sl(irLock);
        if (rawIR.getNumSamples() > 1)
        {
            // rawIR exists — flip it and rebuild immediately
            auto* d = rawIR.getWritePointer(0);
            std::reverse(d, d + rawIR.getNumSamples());
        }
        else if (sourceAudio.getNumSamples() > 0)
        {
            // rawIR not yet ready (conversion still running) — the conversion
            // thread will re-apply isReversed when it finishes, but retrigger
            // it now in case it missed the flag change.
            needsConversion.store(true, std::memory_order_release);
        }
    }

    // Update display: always rebuild from sourceAudioFwd (never modify in-place)
    {
        const juce::ScopedLock sl(irLock);
        // Use sourceAudioFwd if available, otherwise sourceAudio, otherwise
        // keep whatever displayIR already shows (never set it to empty)
        const juce::AudioBuffer<float>* fwd = nullptr;
        if (sourceAudioFwd.getNumSamples() > 0)
            fwd = &sourceAudioFwd;
        else if (sourceAudio.getNumSamples() > 0)
            fwd = &sourceAudio;

        if (fwd != nullptr)
        {
            const int n = fwd->getNumSamples();
            displayIR.setSize(1, n, false, true, true);
            displayIR.copyFrom(0, 0, *fwd, 0, 0, n);
            if (isReversed)
            {
                auto* d = displayIR.getWritePointer(0);
                std::reverse(d, d + n);
            }
        }
        // If no source at all, leave displayIR as-is — never show a flat line
    }

    if (rawIR.getNumSamples() > 1)
        rebuildIR();   // reload convolver with flipped IR
    else
        conversionThread.triggerConversion(); // wake thread to apply new flag
}

void BadDadVerbAudioProcessor::normalizeIR()
{
    if (rawIR.getNumSamples() <= 0) return;
    normalisePeak(rawIR, 0.35f);
    rebuildIR();
}

void BadDadVerbAudioProcessor::rebuildIR()
{
    // Snapshot rawIR under lock — conversion thread may write it at any time
    juce::AudioBuffer<float> tmp;
    {
        const juce::ScopedLock sl(irLock);
        if (rawIR.getNumSamples() <= 0) return;
        tmp.makeCopyOf(rawIR, true);
    }

    // Snapshot trim values once — prevents APVTS values changing mid-calculation
    const float trimStart = getTrimStartNorm();
    const float trimEnd   = getTrimEndNorm();

    // Apply trim — clamp robustly so start < end is always guaranteed
    const int total = tmp.getNumSamples();
    const int start = juce::jlimit(0, total - 1,
                                   (int) std::floor(trimStart * (float) total));
    const int end   = juce::jlimit(start + 1, total,
                                   (int) std::ceil (trimEnd   * (float) total));
    const int trimLen = end - start;

    // Hard minimum: at least 256 samples (~6ms). Bail if trim is too aggressive.
    if (trimLen < 256) return;

    if (trimLen < total)
    {
        juce::AudioBuffer<float> trimmed(1, trimLen);
        trimmed.copyFrom(0, 0, tmp, 0, start, trimLen);
        tmp = std::move(trimmed);
    }

    // Cap length at 3.5 s
    if (tmp.getNumSamples() > (int)(currentSampleRate * 3.5))
    {
        juce::AudioBuffer<float> capped(1, (int)(currentSampleRate * 3.5));
        capped.copyFrom(0, 0, tmp, 0, 0, capped.getNumSamples());
        tmp = std::move(capped);
    }

    // Apply TIME pitch shift (semitones) via linear resampling
    const float semi = apvts.getRawParameterValue("irtime")->load();
    if (std::abs(semi) > 0.05f)
        pitchShiftIR(tmp, semi);

    preprocessIR(tmp);

    {
        const juce::ScopedLock sl(irLock);
        activeIR.makeCopyOf(tmp, true);
        // displayIR is only set when source audio is loaded (see loadIRFromMemory / loadSourceAudioBuffer)
    }

    // Rate-limit convolver reloads — JUCE crossfade needs ~50ms to complete.
    // Rapid reloads stack up crossfades and produce duplication artifacts.
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (now - lastConvolverLoadTime > 500.0)   // max 2 reloads/second
    {
        lastConvolverLoadTime = now;
        convolver.loadImpulseResponse(juce::AudioBuffer<float>(tmp),
                                      currentSampleRate,
                                      juce::dsp::Convolution::Stereo::no,
                                      juce::dsp::Convolution::Trim::no,
                                      juce::dsp::Convolution::Normalise::no);
    }

}  // rebuildIR

void BadDadVerbAudioProcessor::trimSilence(juce::AudioBuffer<float>& buffer,
                                            float thresholdDb, float keepMs)
{
    const float th  = juce::Decibels::decibelsToGain(thresholdDb);
    const int   pad = (int) std::round(currentSampleRate * keepMs * 0.001);
    int first = 0, last = buffer.getNumSamples() - 1;
    auto* d = buffer.getWritePointer(0);
    while (first < buffer.getNumSamples() && std::abs(d[first]) < th) ++first;
    while (last  > first               && std::abs(d[last])  < th) --last;
    first = juce::jmax(0, first - pad);
    last  = juce::jmin(buffer.getNumSamples() - 1, last + pad);
    if (last > first)
    {
        juce::AudioBuffer<float> t(1, last - first + 1);
        t.copyFrom(0, 0, buffer, 0, first, t.getNumSamples());
        buffer = std::move(t);
    }
}

void BadDadVerbAudioProcessor::removeDC(juce::AudioBuffer<float>& buffer)
{
    auto* d = buffer.getWritePointer(0);
    double sum = 0.0;
    for (int i = 0; i < buffer.getNumSamples(); ++i) sum += d[i];
    const float mean = (float)(sum / juce::jmax(1, buffer.getNumSamples()));
    for (int i = 0; i < buffer.getNumSamples(); ++i) d[i] -= mean;
}

void BadDadVerbAudioProcessor::applyFadeInOut(juce::AudioBuffer<float>& buffer, int fadeSamples)
{
    fadeSamples = juce::jmin(fadeSamples, buffer.getNumSamples() / 2);
    auto* d = buffer.getWritePointer(0);
    for (int i = 0; i < fadeSamples; ++i)
    {
        const float g = (float) i / juce::jmax(1, fadeSamples);
        d[i] *= g;
        d[buffer.getNumSamples() - 1 - i] *= g;
    }
}

void BadDadVerbAudioProcessor::applyIRHighPass(juce::AudioBuffer<float>& buffer, float cutoffHz)
{
    // Simple first-order one-pole HP — safe for any buffer length
    const float rc    = 1.0f / (2.0f * juce::MathConstants<float>::pi * cutoffHz);
    const float dt    = 1.0f / (float) currentSampleRate;
    const float alpha = rc / (rc + dt);
    auto* d = buffer.getWritePointer(0);
    float prev_x = d[0], prev_y = d[0];
    for (int i = 1; i < buffer.getNumSamples(); ++i)
    {
        const float y = alpha * (prev_y + d[i] - prev_x);
        prev_x = d[i];
        prev_y = y;
        d[i]   = y;
    }
}

void BadDadVerbAudioProcessor::normalisePeak(juce::AudioBuffer<float>& buffer, float targetPeak)
{
    const float peak = buffer.getMagnitude(0, 0, buffer.getNumSamples());
    if (peak > 1.0e-6f)
        buffer.applyGain(targetPeak / peak);
}

void BadDadVerbAudioProcessor::preprocessIR(juce::AudioBuffer<float>& buffer)
{
    removeDC(buffer);

    // Apply a steep HPF to the IR — 3 passes at 150Hz gives ~18dB/oct rolloff,
    // removing the bass resonance that causes low-frequency bursts when convolving
    // kick-heavy material. Analysis showed the IR had 40-100Hz content causing
    // 120%+ bass amplification — three passes reduce this to inaudible levels.
    applyIRHighPass(buffer, 150.0f);
    applyIRHighPass(buffer, 150.0f);
    applyIRHighPass(buffer, 150.0f);
    removeDC(buffer);

    // Short fade-OUT at IR tail only — preserves HF onset
    const int fadeOut = juce::jmin(64, buffer.getNumSamples() / 8);
    auto* d = buffer.getWritePointer(0);
    const int n = buffer.getNumSamples();
    for (int i = 0; i < fadeOut; ++i)
        d[n - 1 - i] *= (float)i / (float)juce::jmax(1, fadeOut);

    // Normalise peak so the convolver output stays at a predictable level
    normalisePeak(buffer, 0.25f);
}

void BadDadVerbAudioProcessor::pitchShiftIR(juce::AudioBuffer<float>& buf, float semitones)
{
    // Resample the IR by ratio = 2^(semitones/12)
    // Higher pitch (positive semitones) = shorter IR (ratio < 1)
    // Lower pitch (negative semitones) = longer IR (ratio > 1)
    const double ratio  = std::pow(2.0, (double)semitones / 12.0);
    const int    srcLen = buf.getNumSamples();
    const int    dstLen = juce::jmax(1, (int)std::round((double)srcLen / ratio));
    const int    capLen = juce::jmin(dstLen, (int)(currentSampleRate * 4.0));

    juce::AudioBuffer<float> out(1, capLen);
    const float* src = buf.getReadPointer(0);
    float*       dst = out.getWritePointer(0);

    for (int i = 0; i < capLen; ++i)
    {
        const double pos = (double)i * ratio;
        const int    p0  = (int)pos;
        const float  frac = (float)(pos - p0);
        const int    p1  = juce::jmin(p0 + 1, srcLen - 1);
        if (p0 >= srcLen) { dst[i] = 0.0f; continue; }
        dst[i] = src[p0] * (1.0f - frac) + src[juce::jmin(p1, srcLen-1)] * frac;
    }
    buf = std::move(out);
}

void BadDadVerbAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();

    // Save the FORWARD source audio so reload always starts from a known state.
    // isReversed is saved separately so the button and direction are restored.
    const juce::AudioBuffer<float>& fwdBuf =
        (sourceAudioFwd.getNumSamples() > 0) ? sourceAudioFwd :
        (sourceAudio.getNumSamples()    > 0) ? sourceAudio    : rawIR;
    if (fwdBuf.getNumSamples() > 0)
    {
        juce::MemoryBlock irState;
        juce::MemoryOutputStream mos(irState, true);
        mos.writeInt(0x53524341);  // 'SRCA' magic = "source audio (forward)"
        mos.writeInt(fwdBuf.getNumSamples());
        mos.write(fwdBuf.getReadPointer(0),
                  (size_t) fwdBuf.getNumSamples() * sizeof(float));
        state.setProperty("irBinary",    irState.toBase64Encoding(), nullptr);
        state.setProperty("isReversed",  isReversed   ? 1 : 0, nullptr);
        state.setProperty("isMindPush",  isMindPush   ? 1 : 0, nullptr);
        state.setProperty("isOverload",  overloadActive.load() ? 1 : 0, nullptr);
    }
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void BadDadVerbAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        auto state = juce::ValueTree::fromXml(*xml);
        if (state.isValid()) apvts.replaceState(state);

        auto irBase64 = state.getProperty("irBinary").toString();
        if (irBase64.isNotEmpty())
        {
            juce::MemoryBlock mb;
            if (mb.fromBase64Encoding(irBase64))
            {
                juce::MemoryInputStream mis(mb, false);
                const int magic = mis.readInt();

                if (magic == 0x53524341)   // 'SRCA' — new format: source audio (forward)
                {
                    const int n = mis.readInt();
                    if (n > 0 && n < 5'000'000)
                    {
                        // Read the forward copy, then apply saved reversal direction
                        const bool wasReversed  = (int)state.getProperty("isReversed",  0) != 0;
                        const bool wasMindPush  = (int)state.getProperty("isMindPush",  0) != 0;
                        const bool wasOverload  = (int)state.getProperty("isOverload",  0) != 0;
                        isMindPush = wasMindPush;
                        overloadActive.store(wasOverload);
                        if (wasMindPush || wasOverload)
                            buttonStateChangedExternal.store(true);
                        {
                            const juce::ScopedLock sl (irLock);
                            sourceAudioFwd.setSize(1, n, false, true, true);
                            mis.read(sourceAudioFwd.getWritePointer(0), (size_t) n * sizeof(float));
                            sourceAudio.makeCopyOf(sourceAudioFwd, true);
                            displayIR.makeCopyOf(sourceAudioFwd, true);  // freeze display on state restore
                            isReversed = wasReversed;
                            if (wasReversed) reversalChangedExternal.store(true);
                            if (isReversed)
                            {
                                auto* d = sourceAudio.getWritePointer(0);
                                std::reverse(d, d + sourceAudio.getNumSamples());
                            }
                        }
                        needsConversion.store(true, std::memory_order_release);
                        conversionThread.triggerConversion();
                    }
                }
                else
                {
                    // Legacy format: first int was sample count (no magic)
                    // Treat as already-converted IR directly
                    const int n = magic;
                    if (n > 0 && n < 5'000'000)
                    {
                        rawIR.setSize(1, n, false, true, true);
                        mis.read(rawIR.getWritePointer(0), (size_t) n * sizeof(float));
                        rebuildIR();
                    }
                }
            }
        }
    }
}

juce::AudioProcessorEditor* BadDadVerbAudioProcessor::createEditor()
{
    return new BadDadVerbAudioProcessorEditor(*this);
}

// ── JUCE plugin entry point ───────────────────────────────────────────────────
// This function is called by the plugin host wrapper (VST3, AU, Standalone)
// to instantiate the processor. It must be defined exactly once.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BadDadVerbAudioProcessor();
}
