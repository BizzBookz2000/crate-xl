#pragma once
#include <JuceHeader.h>

// Small allocation-free DSP helpers shared by the sampler and the effects.

// Topology-preserving-transform state variable filter (one channel).
struct Svf
{
    enum Mode { lowPass = 0, highPass, bandPass, notch };

    void setParams (double sampleRate, float cutoffHz, float q)
    {
        cutoffHz = juce::jlimit (20.0f, (float) (sampleRate * 0.45), cutoffHz);
        q = juce::jmax (0.3f, q);
        const double g = std::tan (juce::MathConstants<double>::pi * cutoffHz / sampleRate);
        k = 1.0 / q;
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    float process (float x, int mode)
    {
        const double v3 = x - ic2;
        const double v1 = a1 * ic1 + a2 * v3;
        const double v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;

        switch (mode)
        {
            case highPass: return (float) (x - k * v1 - v2);
            case bandPass: return (float) v1;
            case notch:    return (float) (x - k * v1);
            default:       return (float) v2;
        }
    }

    void reset() { ic1 = ic2 = 0.0; }

    double k = 1.0, a1 = 0.0, a2 = 0.0, a3 = 0.0, ic1 = 0.0, ic2 = 0.0;
};

// Simple circular delay line with linear interpolation.
struct DelayLine
{
    void setMaxSamples (int n)
    {
        buffer.assign ((size_t) juce::jmax (4, n), 0.0f);
        writePos = 0;
    }

    void push (float x)
    {
        buffer[(size_t) writePos] = x;
        if (++writePos >= (int) buffer.size()) writePos = 0;
    }

    // delay in samples, >= 1
    float read (double delay) const
    {
        const int size = (int) buffer.size();
        delay = juce::jlimit (1.0, (double) size - 2.0, delay);
        double readPos = writePos - delay;
        while (readPos < 0) readPos += size;
        const int i0 = (int) readPos;
        const int i1 = (i0 + 1) % size;
        const float frac = (float) (readPos - i0);
        return buffer[(size_t) i0] + frac * (buffer[(size_t) i1] - buffer[(size_t) i0]);
    }

    void clear() { std::fill (buffer.begin(), buffer.end(), 0.0f); }

    std::vector<float> buffer;
    int writePos = 0;
};

inline float dbToGain (float db) { return db <= -96.0f ? 0.0f : std::pow (10.0f, db / 20.0f); }
inline float gainToDb (float g)  { return g <= 0.00001f ? -100.0f : 20.0f * std::log10 (g); }

// Note values in beats (quarter notes) used by tempo-synced effects.
inline const juce::StringArray& syncNames()
{
    static juce::StringArray names { "1/32", "1/16", "1/16 dot", "1/8", "1/8 dot", "1/4", "1/4 dot", "1/2", "1 bar" };
    return names;
}

inline double syncBeats (int index)
{
    static const double beats[] = { 0.125, 0.25, 0.375, 0.5, 0.75, 1.0, 1.5, 2.0, 4.0 };
    return beats[juce::jlimit (0, 8, index)];
}

// AHDSR envelope with exponential-ish segments (linear attack, exponential decay / release).
struct Envelope
{
    enum Stage { idle = 0, attack, hold, decay, sustain, release };

    void setParams (double sr, float a, float h, float d, float s, float r, bool ahdOnly)
    {
        sampleRate = sr;
        attackInc = 1.0 / juce::jmax (1.0, a * sr);
        holdSamples = (int) (h * sr);
        decayCoef = coefFor (d, sr);
        releaseCoef = coefFor (r, sr);
        sustainLevel = ahdOnly ? 0.0f : juce::jlimit (0.0f, 1.0f, s);
        ahd = ahdOnly;
    }

    static double coefFor (float seconds, double sr)
    {
        // reach -80 dB after 'seconds'
        return std::exp (std::log (0.0001) / juce::jmax (1.0, (double) seconds * sr));
    }

    void noteOn()  { stage = attack; if (level < 0.0) level = 0.0; holdLeft = holdSamples; }
    void noteOff() { if (stage != idle && ! ahd) stage = release; }
    void kill()    { stage = idle; level = 0.0; }
    bool isActive() const { return stage != idle; }

    float next()
    {
        switch (stage)
        {
            case attack:
                level += attackInc;
                if (level >= 1.0) { level = 1.0; stage = holdLeft > 0 ? hold : decay; }
                break;
            case hold:
                if (--holdLeft <= 0) stage = decay;
                break;
            case decay:
                level = sustainLevel + (level - sustainLevel) * decayCoef;
                if (level - sustainLevel < 0.0001)
                {
                    level = sustainLevel;
                    stage = (ahd || sustainLevel <= 0.0001f) ? idle : sustain;
                    if (stage == idle) level = 0.0;
                }
                break;
            case sustain: break;
            case release:
                level *= releaseCoef;
                if (level < 0.0001) { level = 0.0; stage = idle; }
                break;
            default: break;
        }
        return (float) level;
    }

    Stage stage = idle;
    double level = 0.0, attackInc = 1.0, decayCoef = 0.99, releaseCoef = 0.99, sampleRate = 44100.0;
    float sustainLevel = 1.0f;
    int holdSamples = 0, holdLeft = 0;
    bool ahd = false;
};

// LFO waveform for phase 0..1; 'held' is the sample & hold value.
inline float lfoShapeValue (int shape, double phase, float held)
{
    switch (shape)
    {
        case 0: return (float) std::sin (phase * juce::MathConstants<double>::twoPi);
        case 1: return (float) (phase < 0.5 ? phase * 4.0 - 1.0 : 3.0 - phase * 4.0);
        case 2: return (float) (phase * 2.0 - 1.0);
        case 3: return (float) (1.0 - phase * 2.0);
        case 4: return phase < 0.5 ? 1.0f : -1.0f;
        default: return held;
    }
}

inline float softClip (float x) { return std::tanh (x); }
