#pragma once
#include <JuceHeader.h>
#include <array>
#include <vector>
#include <algorithm>
#include <complex>

// ============================================================================
// Built-in stem separation (no AI model): vocals / drums / bass / other.
//
// Short-time Fourier transform, then per time-frequency bin:
//   * harmonic vs percussive: median filtering across time (harmonic) and
//     across frequency (percussive), Wiener-style soft masks
//   * bass: the harmonic part below ~120-250 Hz
//   * vocals: the harmonic part in the voice band that sits in the centre of
//     the stereo image
//   * drums: the percussive part (kick included)
//   * other: whatever is left
// The four masks always add up to 1, so the stems sum back to the original.
// Output is streamed in blocks, so long songs don't need four copies in memory.
// ============================================================================
namespace cr::stems
{
enum Stem { vocals = 0, drums, bass, other, numStems };
inline const char* stemName (int s) { static const char* n[] = { "Vocals", "Drums", "Bass", "Other" }; return n[juce::jlimit (0, 3, s)]; }

// Receives finished stereo audio for one stem: (stem, left, right, numSamples)
using StemSink = std::function<void (int, const float*, const float*, int)>;

class Separator
{
public:
    // in: 1 or 2 channels. progress returns false to cancel.
    bool run (const juce::AudioBuffer<float>& in, int numFrames, double sampleRate, const StemSink& sink,
              const std::function<bool (float)>& progress = {})
    {
        if (numFrames <= 0 || in.getNumChannels() <= 0) return false;
        const int order = sampleRate > 60000.0 ? 13 : 12;
        N = 1 << order;
        hop = N / 4;
        bins = N / 2 + 1;
        sr = sampleRate;
        fft = std::make_unique<juce::dsp::FFT> (order);

        window.resize ((size_t) N);
        for (int i = 0; i < N; ++i)
            window[(size_t) i] = std::sqrt (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N));

        // per-bin frequency weights
        lowW.resize ((size_t) bins); bandW.resize ((size_t) bins);
        for (int k = 0; k < bins; ++k)
        {
            const double f = (double) k * sr / N;
            lowW[(size_t) k] = (float) smoothStep (250.0, 120.0, f);                       // 1 below 120 Hz
            bandW[(size_t) k] = (float) (smoothStep (140.0, 260.0, f) * smoothStep (11000.0, 7000.0, f));
        }

        // ring of spectra for the time median (centre frame is `half` frames behind)
        ring.assign ((size_t) ringLen, Frame());
        for (auto& fr : ring) { fr.L.assign ((size_t) bins, {}); fr.R.assign ((size_t) bins, {}); fr.mag.assign ((size_t) bins, 0.0f); }

        // overlap-add output (one hop is final after each frame)
        for (auto& s : out) for (auto& c : s) c.assign ((size_t) N, 0.0f);
        fftBuf.assign ((size_t) (2 * N), 0.0f);
        med.resize ((size_t) ringLen);
        H.resize ((size_t) bins); P.resize ((size_t) bins);
        for (auto& mb : maskBuf) mb.assign ((size_t) bins, 0.0f);

        const float* src[2] = { in.getReadPointer (0), in.getReadPointer (juce::jmin (1, in.getNumChannels() - 1)) };
        // frame j starts at sample j*hop - (N - hop) of the input; the delay is removed on output
        const int lead = N - hop;
        const int totalFrames = (numFrames + lead) / hop + 1;
        int written = 0;          // input samples already sent to the sink
        long outPos = -lead;      // input sample index of out[..][0]
        std::vector<float> blockL[numStems], blockR[numStems];
        for (int s = 0; s < numStems; ++s) { blockL[s].resize ((size_t) hop); blockR[s].resize ((size_t) hop); }

        for (int j = 0; j < totalFrames + half; ++j)
        {
            // 1. analyse frame j into the ring (zeros past the end)
            auto& fr = ring[(size_t) (j % ringLen)];
            if (j < totalFrames)
            {
                const long start = (long) j * hop - lead;
                for (int c = 0; c < 2; ++c)
                {
                    std::fill (fftBuf.begin(), fftBuf.end(), 0.0f);
                    for (int i = 0; i < N; ++i)
                    {
                        const long n = start + i;
                        if (n >= 0 && n < numFrames) fftBuf[(size_t) i] = src[c][n] * window[(size_t) i];
                    }
                    fft->performRealOnlyForwardTransform (fftBuf.data(), true);
                    auto& dst = c == 0 ? fr.L : fr.R;
                    for (int k = 0; k < bins; ++k) dst[(size_t) k] = { fftBuf[(size_t) (2 * k)], fftBuf[(size_t) (2 * k + 1)] };
                }
                for (int k = 0; k < bins; ++k) fr.mag[(size_t) k] = 0.5f * (std::abs (fr.L[(size_t) k]) + std::abs (fr.R[(size_t) k]));
            }
            else
            {
                std::fill (fr.mag.begin(), fr.mag.end(), 0.0f);
                std::fill (fr.L.begin(), fr.L.end(), std::complex<float>());
                std::fill (fr.R.begin(), fr.R.end(), std::complex<float>());
            }

            // 2. separate the centre frame c = j - half
            const int c = j - half;
            if (c < 0) continue;
            auto& cf = ring[(size_t) (c % ringLen)];
            separateFrame (cf, j, c);

            // 3. after overlap-add, the first hop of the output buffer is final
            for (int s = 0; s < numStems; ++s)
            {
                for (int i = 0; i < hop; ++i) { blockL[s][(size_t) i] = out[s][0][(size_t) i]; blockR[s][(size_t) i] = out[s][1][(size_t) i]; }
                for (int ch = 0; ch < 2; ++ch)
                {
                    auto& o = out[s][ch];
                    std::move (o.begin() + hop, o.end(), o.begin());
                    std::fill (o.end() - hop, o.end(), 0.0f);
                }
            }
            // samples [outPos, outPos + hop) are done; send the part inside the input
            const long from = juce::jmax ((long) written, outPos), to = juce::jmin ((long) numFrames, outPos + hop);
            if (to > from)
            {
                const int off = (int) (from - outPos), len = (int) (to - from);
                for (int s = 0; s < numStems; ++s) sink (s, blockL[s].data() + off, blockR[s].data() + off, len);
                written = (int) to;
            }
            outPos += hop;

            if (progress && (c & 31) == 0 && ! progress ((float) c / (float) totalFrames)) return false;
        }
        // tail still inside the output buffer
        if (written < numFrames)
        {
            const int off = (int) (written - outPos);
            const int len = numFrames - written;
            if (off >= 0 && off + len <= N)
                for (int s = 0; s < numStems; ++s) sink (s, out[s][0].data() + off, out[s][1].data() + off, len);
        }
        if (progress) progress (1.0f);
        return true;
    }

private:
    struct Frame { std::vector<std::complex<float>> L, R; std::vector<float> mag; };

    static double smoothStep (double from, double to, double x)
    {
        double t = (x - from) / (to - from);
        t = juce::jlimit (0.0, 1.0, t);
        return t * t * (3.0 - 2.0 * t);
    }

    float median (float* v, int n)
    {
        std::nth_element (v, v + n / 2, v + n);
        return v[n / 2];
    }

    void separateFrame (Frame& cf, int newest, int centre)
    {
        // harmonic: median across time at each bin
        for (int k = 0; k < bins; ++k)
        {
            int n = 0;
            for (int t = centre - half; t <= centre + half; ++t)
                med[(size_t) n++] = t < 0 || t > newest ? 0.0f : ring[(size_t) (t % ringLen)].mag[(size_t) k];
            H[(size_t) k] = median (med.data(), n);
        }
        // percussive: median across frequency in this frame
        for (int k = 0; k < bins; ++k)
        {
            int n = 0;
            for (int q = k - half; q <= k + half; ++q)
                med[(size_t) n++] = q < 0 || q >= bins ? 0.0f : cf.mag[(size_t) q];
            P[(size_t) k] = median (med.data(), n);
        }

        for (int k = 0; k < bins; ++k)
        {
            float m[numStems];
            stemMasks (cf, k, m);
            for (int s = 0; s < numStems; ++s) maskBuf[(size_t) s][(size_t) k] = m[s];
        }
        for (int s = 0; s < numStems; ++s)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                for (int k = 0; k < bins; ++k)
                {
                    const auto X = (ch == 0 ? cf.L : cf.R)[(size_t) k] * maskBuf[(size_t) s][(size_t) k];
                    fftBuf[(size_t) (2 * k)] = X.real();
                    fftBuf[(size_t) (2 * k + 1)] = X.imag();
                }
                for (int i = 2 * bins; i < 2 * N; ++i) fftBuf[(size_t) i] = 0.0f;
                fft->performRealOnlyInverseTransform (fftBuf.data());
                auto& o = out[s][ch];
                // sqrt-Hann analysis * synthesis = Hann; Hann at 75% overlap sums to 2
                for (int i = 0; i < N; ++i) o[(size_t) i] += fftBuf[(size_t) i] * window[(size_t) i] * 0.5f;
            }
        }
    }

    void stemMasks (const Frame& cf, int k, float* m) const
    {
        const float h2 = H[(size_t) k] * H[(size_t) k], p2 = P[(size_t) k] * P[(size_t) k];
        const float perc = (h2 + p2) > 1.0e-18f ? p2 / (h2 + p2) : 0.0f;
        const float harm = 1.0f - perc;
        // centre-ness of this bin: 1 when left and right match
        const auto l = cf.L[(size_t) k], r = cf.R[(size_t) k];
        const float sum = std::abs (l) + std::abs (r);
        const float centre = sum > 1.0e-12f ? 1.0f - std::abs (l - r) / sum : 1.0f;
        const float centreW = (float) smoothStep (0.55, 0.92, centre);
        m[bass] = harm * lowW[(size_t) k];
        m[vocals] = harm * (1.0f - lowW[(size_t) k]) * bandW[(size_t) k] * centreW;
        m[drums] = perc;
        m[other] = juce::jmax (0.0f, 1.0f - m[bass] - m[vocals] - m[drums]);
    }

    static constexpr int half = 8, ringLen = 2 * half + 1;
    int N = 4096, hop = 1024, bins = 2049;
    double sr = 44100.0;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, lowW, bandW, fftBuf, med, H, P;
    std::vector<Frame> ring;
    std::array<std::array<std::vector<float>, 2>, numStems> out;
    std::array<std::vector<float>, numStems> maskBuf;
};
} // namespace cr::stems
