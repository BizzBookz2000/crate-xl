#pragma once
#include <JuceHeader.h>

// Offline time-stretching (WSOLA) and pitch-shifting, used by the sample editor and
// the "live stretch" layer option. Runs on a worker thread, never on the audio thread.
namespace stretch
{
// Changes speed without changing pitch. 'ratio' = output length / input length.
inline juce::AudioBuffer<float> timeStretch (const juce::AudioBuffer<float>& in, double ratio, double sampleRate)
{
    const int numCh = juce::jmax (1, in.getNumChannels());
    const int inLen = in.getNumSamples();
    ratio = juce::jlimit (0.1, 10.0, ratio);
    const int outLen = (int) std::round (inLen * ratio);
    juce::AudioBuffer<float> out (numCh, juce::jmax (1, outLen));
    out.clear();
    if (inLen < 64 || outLen < 64)
    {
        for (int ch = 0; ch < numCh; ++ch)
            for (int i = 0; i < outLen; ++i)
                out.setSample (ch, i, in.getSample (ch, juce::jmin (inLen - 1, (int) (i / ratio))));
        return out;
    }
    if (std::abs (ratio - 1.0) < 1.0e-4) { for (int ch = 0; ch < numCh; ++ch) out.copyFrom (ch, 0, in, ch, 0, juce::jmin (inLen, outLen)); return out; }

    const int N = juce::jlimit (512, 8192, juce::nextPowerOfTwo ((int) (sampleRate * 0.04)));
    const int Hs = N / 2;
    const double Ha = Hs / ratio;
    const int delta = N / 4;

    std::vector<float> window ((size_t) N), mono ((size_t) inLen), norm ((size_t) outLen + (size_t) N, 0.0f);
    for (int i = 0; i < N; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N);
    for (int i = 0; i < inLen; ++i)
    {
        float s = 0.0f;
        for (int ch = 0; ch < numCh; ++ch) s += in.getSample (ch, i);
        mono[(size_t) i] = s / (float) numCh;
    }

    auto inputAt = [&] (int ch, int idx) -> float { return (idx >= 0 && idx < inLen) ? in.getSample (ch, idx) : 0.0f; };
    auto monoAt = [&] (int idx) -> float { return (idx >= 0 && idx < inLen) ? mono[(size_t) idx] : 0.0f; };

    juce::AudioBuffer<float> acc (numCh, outLen + N);
    acc.clear();
    int prevPos = 0;
    const int numFrames = outLen / Hs + 1;
    for (int k = 0; k < numFrames; ++k)
    {
        const int outPos = k * Hs;
        const int ideal = (int) std::round (k * Ha);
        int best = ideal;
        if (k > 0)
        {
            // find the offset whose start best continues the previous frame
            const int natural = prevPos + Hs;
            double bestScore = -1.0e30;
            for (int d = -delta; d <= delta; d += 2)
            {
                const int cand = ideal + d;
                double score = 0.0;
                for (int i = 0; i < N / 2; i += 4) score += (double) monoAt (cand + i) * monoAt (natural + i);
                if (score > bestScore) { bestScore = score; best = cand; }
            }
        }
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* d = acc.getWritePointer (ch);
            for (int i = 0; i < N && outPos + i < outLen + N; ++i)
                d[outPos + i] += inputAt (ch, best + i) * window[(size_t) i];
        }
        for (int i = 0; i < N && outPos + i < (int) norm.size(); ++i) norm[(size_t) (outPos + i)] += window[(size_t) i];
        prevPos = best;
    }
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* o = out.getWritePointer (ch);
        const auto* a = acc.getReadPointer (ch);
        for (int i = 0; i < outLen; ++i) o[i] = a[i] / juce::jmax (0.15f, norm[(size_t) i]);
    }
    return out;
}

// Plays the buffer at 'speed' (2 = twice as fast, one octave up). Output length = input / speed.
inline juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>& in, double speed)
{
    speed = juce::jlimit (0.05, 20.0, speed);
    const int numCh = juce::jmax (1, in.getNumChannels());
    const int outLen = juce::jmax (1, (int) std::floor (in.getNumSamples() / speed));
    juce::AudioBuffer<float> out (numCh, outLen);
    out.clear();
    for (int ch = 0; ch < numCh; ++ch)
    {
        juce::LagrangeInterpolator interp;
        // pad the input so the interpolator never reads past the end
        juce::AudioBuffer<float> padded (1, in.getNumSamples() + 8);
        padded.clear();
        padded.copyFrom (0, 0, in, ch, 0, in.getNumSamples());
        interp.process (speed, padded.getReadPointer (0), out.getWritePointer (ch), outLen);
    }
    return out;
}

// Changes pitch by 'semitones' while keeping the length.
inline juce::AudioBuffer<float> pitchShift (const juce::AudioBuffer<float>& in, double semitones, double sampleRate)
{
    if (std::abs (semitones) < 0.001) return in;
    const double r = std::pow (2.0, semitones / 12.0);
    auto stretched = timeStretch (in, r, sampleRate);
    auto out = resample (stretched, r);
    // fix rounding so the result has exactly the original length
    juce::AudioBuffer<float> exact (in.getNumChannels(), in.getNumSamples());
    exact.clear();
    for (int ch = 0; ch < exact.getNumChannels(); ++ch)
        exact.copyFrom (ch, 0, out, juce::jmin (ch, out.getNumChannels() - 1), 0, juce::jmin (out.getNumSamples(), exact.getNumSamples()));
    return exact;
}
} // namespace stretch
