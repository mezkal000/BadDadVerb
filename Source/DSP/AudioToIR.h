#pragma once
/*  AudioToIR.h — Musical Audio-to-Impulse-Response Converter
    ============================================================
    Takes any audio buffer and transforms it into a perceptually valid,
    musically interesting impulse response.

    Algorithm: "Spectral Morphing Decay"
    ─────────────────────────────────────
    1. Analyse the source audio with overlapping FFT frames to extract
       a time-varying spectral envelope (magnitude only, discard phase).

    2. Build a white-noise skeleton with an exponential t60 decay —
       this guarantees the fundamental reverb physics are correct.

    3. Imprint the source spectral shape onto each frame of the skeleton:
         • Each skeleton frame's magnitude spectrum is multiplied by the
           source spectral envelope sampled at a time-mapped position
           (early frames → early audio, late frames → late audio).
         • Phase is kept from the white noise → always diffuse.
       This stamps the timbral character of the source onto the reverb
       without the audio ever being directly audible.

    4. Apply temporal smoothing between frames to create evolving,
       living spectral movement rather than static colouration.

    5. Sculpt the onset: the first few ms receives a shaped impulse
       derived from the source's spectral centroid (bright → sharper
       onset, dark → softer onset).

    6. Post-process: DC removal, fade in/out, normalise.

    The result: reverb that has the impossible spectral character of
    arbitrary audio — music, speech, machines, anything — while still
    decaying correctly and sounding like a real (if alien) space.

    All processing is done in-place with no heap allocation in realtime.
    The convert() function should be called on a non-realtime thread.
*/

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <complex>
#include <cmath>
#include <algorithm>
#include <random>

namespace dadbass
{

class AudioToIR
{
public:
    struct Params
    {
        double sampleRate   = 44100.0;
        float  t60          = 2.0f;    // decay time in seconds
        float  irLength     = 3.0f;    // total IR length in seconds (clamped ≤ t60*1.5)
        float  brightness   = 0.5f;    // 0=dark (strong LP shaping), 1=bright (flat shaping)
        float  diffusion    = 0.8f;    // 0=sparse dry, 1=fully diffuse
        float  predelayMs   = 0.0f;    // pre-delay before onset
        int    seed         = 42;      // random seed for reproducibility
    };

    // Convert source audio buffer → IR buffer.
    // srcBuf must be mono (or will use ch0). Result written into outBuf (mono).
    static void convert(const juce::AudioBuffer<float>& srcBuf,
                        juce::AudioBuffer<float>&        outBuf,
                        const Params&                    p)
    {
        const int   SR     = (int) p.sampleRate;
        const int   outLen = std::min((int)(p.irLength * SR),
                                      (int)(p.t60 * 1.5f * SR));
        const int   srcLen = srcBuf.getNumSamples();

        if (srcLen < 64 || outLen < 64) return;

        // ── FFT setup ─────────────────────────────────────────────────────────
        // Frame size: 2048 samples — good frequency resolution for musical content
        constexpr int FFT_ORDER = 11;        // 2^11 = 2048
        constexpr int FFT_SIZE  = 1 << FFT_ORDER;
        constexpr int HOP       = FFT_SIZE / 2;   // 50% overlap — faster, still smooth

        juce::dsp::FFT fft(FFT_ORDER);

        // ── Step 1: Analyse source → spectral envelope table ──────────────────
        // Each row = magnitude spectrum of one analysis frame.
        // We store enough frames to span the full IR length.

        const int numFrames = std::max(4, (int)std::ceil((double)outLen / HOP));
        const int specSize  = FFT_SIZE / 2 + 1;

        // specTable[frame][bin] = smoothed magnitude
        std::vector<std::vector<float>> specTable(numFrames,
                                                   std::vector<float>(specSize, 0.f));

        // Analysis window (Hann)
        std::vector<float> window(FFT_SIZE);
        for (int i = 0; i < FFT_SIZE; ++i)
            window[i] = 0.5f * (1.f - std::cos(2.f * juce::MathConstants<float>::pi
                                                 * i / (FFT_SIZE - 1)));

        // Compute per-frame magnitudes
        std::vector<float> fftBuf(FFT_SIZE * 2, 0.f);
        const float* src = srcBuf.getReadPointer(0);

        for (int frame = 0; frame < numFrames; ++frame)
        {
            // Map frame → source position (spread across full source length)
            const double srcPos = (double)frame / numFrames * srcLen;
            const int    srcOff = (int)srcPos;

            // Fill windowed FFT buffer
            std::fill(fftBuf.begin(), fftBuf.end(), 0.f);
            for (int k = 0; k < FFT_SIZE; ++k)
            {
                const int si = srcOff + k;
                fftBuf[k * 2]     = (si < srcLen) ? src[si] * window[k] : 0.f;
                fftBuf[k * 2 + 1] = 0.f;
            }
            fft.performFrequencyOnlyForwardTransform(fftBuf.data());

            // Store magnitude (first specSize real values after freq-only transform)
            for (int b = 0; b < specSize; ++b)
                specTable[frame][b] = std::abs(fftBuf[b]);
        }

        // Smooth spectral table across frames (temporal smoothing)
        // Creates gradual spectral evolution rather than hard jumps
        {
            const float alpha = 0.35f;  // lower = smoother
            for (int b = 0; b < specSize; ++b)
            {
                float prev = specTable[0][b];
                for (int frame = 1; frame < numFrames; ++frame)
                {
                    specTable[frame][b] = alpha * specTable[frame][b]
                                       + (1.f - alpha) * prev;
                    prev = specTable[frame][b];
                }
            }
        }

        // Per-frame normalisation to unit energy (keeps spectral SHAPE, not level)
        for (int frame = 0; frame < numFrames; ++frame)
        {
            float energy = 0.f;
            for (int b = 0; b < specSize; ++b)
                energy += specTable[frame][b] * specTable[frame][b];
            if (energy > 1e-12f)
            {
                const float scale = 1.f / std::sqrt(energy);
                for (int b = 0; b < specSize; ++b)
                    specTable[frame][b] *= scale;
            }
        }

        // ── Step 2: Spectral centroid → onset sharpness ───────────────────────
        // Brighter source → sharper initial onset spike.
        float centroid = 0.f, centroidDenom = 0.f;
        for (int b = 0; b < specSize; ++b)
        {
            centroid      += (float)b * specTable[0][b];
            centroidDenom += specTable[0][b];
        }
        if (centroidDenom > 1e-9f) centroid /= centroidDenom;
        const float normCentroid = centroid / (float)specSize;   // 0..1
        // Blend with brightness param
        const float onsetSharpness = 0.3f + 0.7f * (normCentroid * 0.5f + p.brightness * 0.5f);

        // ── Step 3: Build IR by synthesis ─────────────────────────────────────
        // Allocate output
        outBuf.setSize(1, outLen, false, true, false);
        outBuf.clear();
        float* out = outBuf.getWritePointer(0);

        // t60 decay coefficient: amplitude at sample i = exp(k * i)
        // k = ln(0.001) / (t60 * SR)  → -60dB at t60
        const float decayK = (float)(std::log(0.001) / (p.t60 * SR));

        // Random number generator for noise skeleton
        std::mt19937 rng((unsigned)p.seed);
        std::uniform_real_distribution<float> udist(-1.f, 1.f);

        // Synthesis in overlapping frames (same size as analysis)
        // Use std::complex<float> so fft.perform() accepts it directly.
        std::vector<std::complex<float>> synthBuf(FFT_SIZE);

        // Accumulate window energy for OLA normalisation
        std::vector<float> wsum(outLen, 0.f);

        for (int frame = 0; frame < numFrames; ++frame)
        {
            const int outOff   = frame * HOP;
            const int midSamp  = outOff + FFT_SIZE / 2;
            if (outOff >= outLen) break;

            // Time-centre of this frame → decay amplitude
            const float envGain = std::exp(decayK * std::max(0, midSamp));

            // Skip frames that are essentially silent
            if (envGain < 1e-6f) break;

            // ── Generate real white noise frame in TIME domain ──────────────
            // Fill with windowed white noise, zero imaginary part
            for (int k = 0; k < FFT_SIZE; ++k)
                synthBuf[k] = std::complex<float>(udist(rng) * window[k], 0.f);

            // ── Forward FFT → frequency domain ───────────────────────────────
            fft.perform(synthBuf.data(), synthBuf.data(), false);

            // ── Spectral imprinting ───────────────────────────────────────────
            // Replace magnitudes with source-shaped magnitudes,
            // keep the noise phases from the FFT of real noise (→ diffuse)
            const int specFrame = std::min(frame, numFrames - 1);
            const float flatMag = 1.f / std::sqrt((float)specSize);

            for (int b = 0; b < specSize; ++b)
            {
                const float re  = synthBuf[b].real();
                const float im  = synthBuf[b].imag();
                const float mag = std::sqrt(re * re + im * im);
                if (mag < 1e-12f) { synthBuf[b] = std::complex<float>(flatMag, 0.f); continue; }

                // Source spectral magnitude (from analysis table)
                float srcMag = specTable[specFrame][b];

                // Brightness shelf: rolls off highs for dark, opens for bright
                const float binHz = (float)b * SR / FFT_SIZE;
                const float shelf = 1.f + (p.brightness - 0.5f) * 2.f
                                    * std::tanh((binHz - 2000.f) / 2000.f);
                srcMag *= std::max(0.01f, shelf);

                // Diffusion: blend imprinted vs flat-white spectrum
                srcMag = p.diffusion * srcMag + (1.f - p.diffusion) * flatMag;

                // Impose new magnitude onto noise phase
                synthBuf[b] *= (srcMag / mag);
            }

            // Enforce conjugate symmetry so IFFT output is real
            synthBuf[0]            = std::complex<float>(synthBuf[0].real(), 0.f);
            synthBuf[FFT_SIZE / 2] = std::complex<float>(synthBuf[FFT_SIZE / 2].real(), 0.f);
            for (int b = 1; b < FFT_SIZE / 2; ++b)
                synthBuf[FFT_SIZE - b] = std::conj(synthBuf[b]);

            // ── IFFT → time domain ────────────────────────────────────────────
            fft.perform(synthBuf.data(), synthBuf.data(), true);

            // ── OLA: accumulate output ───────────────────────────────────────
            // Noise was pre-windowed before FFT; apply synthesis window here for
            // clean OLA reconstruction (standard WOLA: analysis * synthesis window)
            for (int k = 0; k < FFT_SIZE; ++k)
            {
                const int oi = outOff + k;
                if (oi >= outLen) break;
                const float w = window[k];
                // Scale by 1/FFT_SIZE to undo JUCE's unnormalised IFFT
                out[oi]  += (synthBuf[k].real() / FFT_SIZE) * w * envGain;
                wsum[oi] += w * w;
            }
        }

        // Normalise OLA accumulation
        for (int i = 0; i < outLen; ++i)
            if (wsum[i] > 1e-9f)
                out[i] /= wsum[i];

        // ── Step 4: Shape the onset ───────────────────────────────────────────
        // Add a shaped impulse at t=0 (or pre-delay).
        // The onset sharpness (derived from spectral centroid) controls the
        // width: sharp → narrow spike, dark → spread over 2-8ms.
        const int pdSamples  = (int)(p.predelayMs * SR / 1000.f);
        const int onsetWidth = (int)((1.f - onsetSharpness) * 0.008f * SR + 0.001f * SR);
        const float impulseAmp = 0.8f;

        for (int i = 0; i < onsetWidth && (pdSamples + i) < outLen; ++i)
        {
            const float env = std::exp(-5.f * (float)i / onsetWidth)
                            * (0.5f - 0.5f * std::cos(juce::MathConstants<float>::pi
                                                       * i / onsetWidth));
            out[pdSamples + i] += impulseAmp * env;
        }

        // ── Step 5: Post-process ──────────────────────────────────────────────
        // Remove DC
        {
            double sum = 0.0;
            for (int i = 0; i < outLen; ++i) sum += out[i];
            const float mean = (float)(sum / outLen);
            for (int i = 0; i < outLen; ++i) out[i] -= mean;
        }

        // Fade in (2ms) — prevents click at sample 0
        const int fadeInSamp = std::min((int)(0.002 * SR), outLen / 16);
        for (int i = 0; i < fadeInSamp; ++i)
            out[i] *= (float)i / fadeInSamp;

        // Fade out (last 5% of length)
        const int fadeOutSamp = std::max(256, outLen / 20);
        for (int i = 0; i < fadeOutSamp; ++i)
            out[outLen - fadeOutSamp + i] *= 1.f - (float)i / fadeOutSamp;

        // Normalise peak
        float peak = 0.f;
        for (int i = 0; i < outLen; ++i) peak = std::max(peak, std::abs(out[i]));
        if (peak > 1e-9f)
        {
            const float scale = 0.75f / peak;
            for (int i = 0; i < outLen; ++i) out[i] *= scale;
        }
    }

private:
    AudioToIR() = delete;
};

} // namespace dadbass
