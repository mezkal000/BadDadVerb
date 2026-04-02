#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "DSP/AudioToIR.h"
#include <array>
#include <atomic>

// ── Background thread for AudioToIR conversion ───────────────────────────────
// Running AudioToIR::convert() on the message thread or audio thread blocks the
// DAW. This thread wakes on demand, converts sourceAudio → rawIR, then triggers
// rebuildIR() which is safe to call from any non-audio thread.
class BadDadVerbAudioProcessor;

class IRConversionThread : public juce::Thread
{
public:
    explicit IRConversionThread (BadDadVerbAudioProcessor& p)
        : juce::Thread ("IRConversion"), proc (p) {}

    void triggerConversion()
    {
        notify();   // wake the thread
    }

    void run() override;   // defined in PluginProcessor.cpp

private:
    BadDadVerbAudioProcessor& proc;
};

// ─────────────────────────────────────────────────────────────────────────────
class BadDadVerbAudioProcessor : public juce::AudioProcessor
{
public:
    BadDadVerbAudioProcessor();
    ~BadDadVerbAudioProcessor() override
    {
        conversionThread.stopThread (3000);
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "BadDadVerb"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;

    static constexpr int ScopeSize = 512;
    std::array<float, ScopeSize> scopeRing{};
    std::atomic<int>   scopeWritePos { 0 };
    std::atomic<float> inputLevelDb  { -60.0f };
    std::atomic<bool>  limiterLamp   { false };

    void startRecordingIR();
    void stopRecordingIR();
    void reverseIR();
    void setReversed(bool shouldBeReversed);
    bool getIsReversed() const noexcept { return isReversed; }
    bool popReversalChangedExternal() noexcept { return reversalChangedExternal.exchange(false); }
    bool popButtonStateChangedExternal() noexcept { return buttonStateChangedExternal.exchange(false); }
    bool getMindPush()   const noexcept { return isMindPush; }
    void setMindPush(bool on) noexcept  { isMindPush = on; if (!on) resetMindPush(); }
    bool isOverload()   const noexcept { return overloadActive.load(); }
    void setOverload(bool on) noexcept { overloadActive.store(on); if (!on) { overloadEnv[0] = overloadEnv[1] = 0.f; } }
    bool isPsyOp()        const noexcept { return psyOpActive.load(); }
    void setPsyOp(bool on) noexcept
    {
        psyOpActive.store(on);
        if (!on)
        {
            recordingIR.store(false);
            // Restore sourceAudio from the canonical forward copy and
            // trigger a fresh conversion so the full-length IR is rebuilt.
            {
                const juce::ScopedLock sl(irLock);
                if (sourceAudioFwd.getNumSamples() > 0)
                {
                    sourceAudio.makeCopyOf(sourceAudioFwd, true);
                    if (isReversed)
                    {
                        auto* d = sourceAudio.getWritePointer(0);
                        std::reverse(d, d + sourceAudio.getNumSamples());
                    }
                }
            }
            // Reconvert with the restored full source — replaces the short psyOp IR
            needsConversion.store(true, std::memory_order_release);
            conversionThread.triggerConversion();
        }
    }
    void normalizeIR();
    void applyTrimFromParameters();
    void rebuildIRIfNeeded();   // call from message thread — handles needsIRRebuild flag

    // Load an IR from WAV data in memory (used by preset manager)
    bool loadIRFromMemory(const void* data, int sizeBytes);

    bool isRecordingIR()  const noexcept { return recordingIR.load(); }
    bool hasIRLoaded()    const noexcept { return activeIR.getNumSamples() > 0; }
    bool hasSourceAudio() const noexcept { return sourceAudio.getNumSamples() > 0; }

    // Load a pre-decoded mono audio buffer as source material.
    // Used by the editor's file drag-and-drop. Non-blocking — conversion
    // happens on the background thread.
    void loadSourceAudioBuffer(const juce::AudioBuffer<float>& mono)
    {
        {
            const juce::ScopedLock sl(irLock);
            sourceAudioFwd.makeCopyOf(mono, true);
            sourceAudio.makeCopyOf(mono, true);
            displayIR.makeCopyOf(mono, true);  // freeze display waveform here
            if (isReversed)
            {
                auto* d = sourceAudio.getWritePointer(0);
                std::reverse(d, d + sourceAudio.getNumSamples());
            }
        }
        needsConversion.store(true, std::memory_order_release);
        conversionThread.triggerConversion();
    }

    // Serialise source audio to base64 for preset XML embedding
    juce::String getSourceAudioAsBase64() const
    {
        // Always save the FORWARD copy so presets restore cleanly regardless
        // of whether REV was on when the preset was saved.
        const auto& buf = (sourceAudioFwd.getNumSamples() > 0) ? sourceAudioFwd :
                          (sourceAudio.getNumSamples()    > 0) ? sourceAudio    : rawIR;
        if (buf.getNumSamples() == 0) return {};
        juce::MemoryBlock mb;
        juce::MemoryOutputStream mos(mb, true);
        mos.writeInt(0x53524341);   // 'SRCA' magic
        mos.writeInt(buf.getNumSamples());
        mos.write(buf.getReadPointer(0),
                  (size_t) buf.getNumSamples() * sizeof(float));
        return mb.toBase64Encoding();
    }

    // Load source audio from base64 preset string, trigger background conversion
    void loadSourceAudioFromBase64(const juce::String& b64)
    {
        if (b64.isEmpty()) return;
        juce::MemoryBlock mb;
        if (!mb.fromBase64Encoding(b64)) return;
        juce::MemoryInputStream mis(mb, false);
        const int magic = mis.readInt();
        const int n     = (magic == 0x53524341) ? mis.readInt() : magic;
        if (n <= 0 || n > 5'000'000) return;
        if (magic != 0x53524341) mis.setPosition(0);
        {
            const juce::ScopedLock sl (irLock);
            // Load as forward source — apply current reversal state on top
            sourceAudioFwd.setSize(1, n, false, true, true);
            mis.read(sourceAudioFwd.getWritePointer(0), (size_t) n * sizeof(float));
            sourceAudio.makeCopyOf(sourceAudioFwd, true);
            displayIR.makeCopyOf(sourceAudioFwd, true);   // update display
            if (isReversed)
            {
                auto* d = sourceAudio.getWritePointer(0);
                std::reverse(d, d + sourceAudio.getNumSamples());
            }
        }
        needsConversion.store(true, std::memory_order_release);
        conversionThread.triggerConversion();   // non-blocking: returns immediately
    }
    const juce::AudioBuffer<float>& getIRForDisplay()     const noexcept { return displayIR; }
    const juce::AudioBuffer<float>& getSourceForDisplay()  const noexcept { return sourceAudio; }
    float getTrimStartNorm() const noexcept;
    float getTrimEndNorm()   const noexcept;

private:
    void rebuildIR();
    void preprocessIR(juce::AudioBuffer<float>&);
    void trimSilence(juce::AudioBuffer<float>& buffer,
                     float thresholdDb = -48.0f, float keepMs = 20.0f);
    void removeDC(juce::AudioBuffer<float>&);
    void applyFadeInOut(juce::AudioBuffer<float>&, int fadeSamples);
    void applyIRHighPass(juce::AudioBuffer<float>&, float cutoffHz);
    void normalisePeak(juce::AudioBuffer<float>&, float targetPeak);
    void captureScope(const juce::AudioBuffer<float>&);
    void sanitizeBuffer(juce::AudioBuffer<float>&);

    // Pitch-shift IR by semitones using linear resampling
    void pitchShiftIR(juce::AudioBuffer<float>& buf, float semitones);

    std::atomic<bool>  recordingIR    { false };
    std::atomic<bool>  needsIRRebuild { false };  // set on audio thread, consumed on message thread
    double currentSampleRate = 44100.0;
    int    maxRecordSamples  = 0;
    int    recordWritePos    = 0;

    // ── PsyOp: live continuous IR modulation ────────────────────────────────
    // When ON: incoming audio is written into a rolling circular buffer.
    // Every psyOpRetriggerSamples the latest slice is snapshotted and
    // re-converted into a new IR, creating a reverb that continuously
    // morphs with the live input signal.
    std::atomic<bool> psyOpActive  { false };
    std::atomic<bool> overloadActive { false };

    // Overload compressor state — per-channel envelope followers

    // isOverload / setOverload are public (see above)
    // Rolling capture: 3 seconds at 48kHz (generous headroom)
    static constexpr int psyOpBufSize = 3 * 48001;
    float   psyOpBuf[3 * 48001] = {};
    int     psyOpWrite    = 0;   // write head in rolling buffer
    int     psyOpFilled   = 0;   // how many samples have been written (saturates at psyOpBufSize)
    int     psyOpCounter  = 0;   // counts samples since last re-conversion trigger
    int     psyOpRetrigger = 0;  // retrigger interval in samples (set in prepareToPlay)

    // isPsyOp / setPsyOp declared public above

    IRConversionThread       conversionThread { *this };

    juce::AudioBuffer<float> recordBuffer;
    juce::AudioBuffer<float> sourceAudio;    // current source (may be reversed)
    juce::AudioBuffer<float> sourceAudioFwd; // canonical forward copy, never reversed
    bool isReversed = false;                 // single source of truth for direction
    std::atomic<bool> reversalChangedExternal { false }; // set by state load, polled by editor
    std::atomic<bool> buttonStateChangedExternal { false }; // mindPush/overload restored from DAW state
    juce::AudioBuffer<float> rawIR;        // converted IR ready for convolution
    juce::AudioBuffer<float> activeIR;
    juce::AudioBuffer<float> displayIR;
    juce::AudioBuffer<float> dryBuffer;

    // Flag: true when needsIRRebuild was set by a fresh recording/load
    // (requires AudioToIR conversion). False = just re-run rebuildIR() with
    // existing rawIR (e.g. parameter change).
    std::atomic<bool> needsConversion    { false };
    std::atomic<bool> needsPsyConversion { false }; // separate from main conversion
    double lastConvolverLoadTime = 0.0;             // rate-limit convolver reloads

    juce::CriticalSection irLock;
    juce::dsp::Convolution    convolver;
    juce::dsp::Limiter<float> limiter;

    // Moog-style 12dB/oct ladder lowpass (2-pole) on wet signal only
    // State per channel
    struct MoogFilter {
        // 4-pole Moog ladder (24 dB/oct) with tanh saturation.
        // Gives the characteristic warm, slightly nonlinear Moog sound.
        float s[4] = {};
        void reset() { s[0] = s[1] = s[2] = s[3] = 0.f; }
        void prime(float x) noexcept { s[0]=s[1]=s[2]=s[3]=x; }

        float process(float x, float cutoff01, float res) noexcept {
            const float f   = std::sqrt(juce::jlimit(0.0f, 1.0f, cutoff01)) * 0.82f;
            // Reduced resonance headroom — prevents feedback explosion at high res
            const float fb  = res * 3.5f * (1.0f - 0.15f * f * f);
            // Clamp inp to ±1 so tanh is always in its linear/gentle region
            const float inp = juce::jlimit(-1.0f, 1.0f,
                              juce::jlimit(-2.0f, 2.0f, x) - fb * s[3]);
            using T = juce::dsp::FastMathApproximations;
            s[0] += f * (T::tanh(inp)  - T::tanh(s[0]));
            s[1] += f * (T::tanh(s[0]) - T::tanh(s[1]));
            s[2] += f * (T::tanh(s[1]) - T::tanh(s[2]));
            s[3] += f * (T::tanh(s[2]) - T::tanh(s[3]));
            s[0] *= 0.9999f; s[1] *= 0.9999f;
            s[2] *= 0.9999f; s[3] *= 0.9999f;
            // Denormal flush — cheaper than std::abs check every sample
            s[0] += 1e-30f; s[1] += 1e-30f; s[2] += 1e-30f; s[3] += 1e-30f;
            s[0] -= 1e-30f; s[1] -= 1e-30f; s[2] -= 1e-30f; s[3] -= 1e-30f;
            return s[3];
        }
    };
    MoogFilter moog[2];  // stereo

    // First-order one-pole highpass per channel (wet signal only, real-time)
    struct HP1 {
        float x1 = 0.f, y1 = 0.f;
        void reset() { x1 = y1 = 0.f; }
        float process(float x, float alpha) noexcept {
            float y = alpha * (y1 + x - x1);
            x1 = x; y1 = y; return y;
        }
    };
    HP1 hpWet[2];   // stereo wet-signal HPF state

    // Fixed 48 dB/oct (8-pole) lowpass at 9 kHz — always on, not user-editable.
    // Eight cascaded one-pole lowpass stages. No resonance.
    struct LP8 {
        float s[8] = {};
        void reset() { std::fill(std::begin(s), std::end(s), 0.f); }
        void prepare(float cutoffHz, float sampleRate) noexcept
        {
            // One-pole coefficient
            const float w = 2.0f * juce::MathConstants<float>::pi * cutoffHz / sampleRate;
            coeff = w / (w + 1.0f);  // bilinear-derived: stable for any cutoff < Nyquist
        }
        float process(float x) noexcept
        {
            // Eight cascaded one-pole lowpass stages
            s[0] += coeff * (x    - s[0]);
            s[1] += coeff * (s[0] - s[1]);
            s[2] += coeff * (s[1] - s[2]);
            s[3] += coeff * (s[2] - s[3]);
            s[4] += coeff * (s[3] - s[4]);
            s[5] += coeff * (s[4] - s[5]);
            s[6] += coeff * (s[5] - s[6]);
            s[7] += coeff * (s[6] - s[7]);
            return s[7];
        }
        float coeff = 0.9f;
    };
    LP8 airFilter[2];  // stereo, fixed 9kHz 48dB/oct LP on wet signal

    // Fixed low-shelf: -12 dB at 200 Hz, biquad, wet chain only.
    // Tames the natural low-end buildup from convolution reverb without
    // fully removing warmth. Coefficients set once in prepareToPlay.
    struct Biquad {
        float b0=1,b1=0,b2=0,a1=0,a2=0;
        float x1=0,x2=0,y1=0,y2=0;
        void reset() { x1=x2=y1=y2=0.f; }
        void setLowShelf(float fc, float gainDb, float SR) noexcept {
            // Matched-z bilinear low shelf (Audio EQ Cookbook)
            const float A  = std::pow(10.f, gainDb / 40.f);  // sqrt of linear gain
            const float w0 = juce::MathConstants<float>::twoPi * fc / SR;
            const float cw = std::cos(w0);
            const float sw = std::sin(w0);
            const float S  = 1.f;   // shelf slope = 1
            const float al = sw / 2.f * std::sqrt((A + 1.f/A) * (1.f/S - 1.f) + 2.f);
            b0 =  A*((A+1) - (A-1)*cw + 2*std::sqrt(A)*al);
            b1 =  2*A*((A-1) - (A+1)*cw);
            b2 =  A*((A+1) - (A-1)*cw - 2*std::sqrt(A)*al);
            const float ia0 = 1.f / ((A+1) + (A-1)*cw + 2*std::sqrt(A)*al);
            a1 = -2*((A-1) + (A+1)*cw) * ia0;
            a2 =    ((A+1) + (A-1)*cw - 2*std::sqrt(A)*al) * ia0;
            b0 *= ia0; b1 *= ia0; b2 *= ia0;
        }
        float process(float x) noexcept {
            float y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
            x2=x1; x1=x; y2=y1; y1=y;
            return y;
        }
    };
    Biquad shelfWet[2];  // stereo fixed low-shelf, wet only

    // Predelay: circular buffer per channel, max 150 ms
    static constexpr int maxPredelaySamples = 32768; // covers 150ms @192kHz
    float predelayBuf[2][32768] = {};
    int   predelayWrite[2]     = {};
    void  resetPredelay() noexcept {
        std::memset(predelayBuf, 0, sizeof(predelayBuf));
        predelayWrite[0] = predelayWrite[1] = 0;
    }

    juce::LinearSmoothedValue<float> inputSmooth    { 1.0f };
    juce::LinearSmoothedValue<float> mixSmooth      { 0.3f };
    juce::LinearSmoothedValue<float> predelaySmooth { 0.0f }; // predelay in samples
    juce::LinearSmoothedValue<float> outputSmooth   { 1.0f };
    juce::LinearSmoothedValue<float> freqSmooth     { 1.0f };
    juce::LinearSmoothedValue<float> resSmooth      { 0.0f };
    juce::LinearSmoothedValue<float> hpfSmooth      { 20.0f }; // cutoff Hz, wet HPF

    // Wet auto-gain: slow-attack / slow-release envelope follower that keeps
    // the wet signal at a sensible level before the rest of the chain.

    // ── Mind Push effect state ────────────────────────────────────────────────
    // When active: stereo wet signal is mixed with a 30ms-delayed copy where
    // the LEFT channel has inverted phase. The mix cycles smoothly 0:100→50:50
    // with random period 0.5–3 s (equal-power crossfade for constant loudness).
    bool  isMindPush = false;

    // Overload compressor: coefficients precomputed in prepareToPlay
    float overloadAttack  = 0.99f;   // precomputed 1ms attack at current SR
    float overloadRelease = 0.9994f; // precomputed 80ms release at current SR
    float overloadEnv[2]  = {};      // peak follower state per channel

    // 30ms delay buffers — stereo (one per channel)
    // Buffer sized for 30ms@96kHz with generous headroom
    static constexpr int mindPushDelaySamples = 8192;
    float mindPushBuf[2][8192] = {};
    int   mindPushWrite = 0;   // shared write pointer (same position both channels)

    // LFO state — drives the blend crossfade
    float mpPhase    = 0.0f;   // 0..1 within cycle
    float mpCycleLen = 4.0f;   // seconds, randomised each cycle (range 2-8 s)
    float mpBlend    = 0.0f;   // 0=pure original, 0.5=equal mix
    std::mt19937 mpRng { 0xBADDAD42u };

    void resetMindPush() noexcept {
        std::memset(mindPushBuf, 0, sizeof(mindPushBuf));
        mindPushWrite = 0;
        mpPhase = 0.f; mpBlend = 0.f; mpCycleLen = 1.5f;
    }
    // getMindPush / setMindPush are public (see above)

    float lastLimitThreshold = -1000.0f;
    float lastIRSemitones    = -999.0f;  // cache to avoid redundant rebuilds

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BadDadVerbAudioProcessor)

    friend class IRConversionThread;
};
