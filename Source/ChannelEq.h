#pragma once
#include <JuceHeader.h>
#include <cmath>
#include <complex>

// ============================================================================
// Channel strip EQ: 4-band parametric (LF / LMF / HMF / HF) plus HPF / LPF with
// 12 / 24 / 48 dB per octave slopes. The same maths drives the audio and the
// curves drawn in the mixer, so what you see is what you hear.
// ============================================================================
namespace cr
{
struct EqBand
{
    bool on = false;
    float freq = 1000.0f;
    float gain = 0.0f;    // dB
    float q = 0.7f;
    bool bell = false;    // LF / HF only: bell instead of shelf
    bool operator== (const EqBand& o) const { return on == o.on && freq == o.freq && gain == o.gain && q == o.q && bell == o.bell; }
};

struct ChannelEqSettings
{
    // channel front end
    float trimDb = 0.0f;          // -24..+24, before the inserts
    bool invert = false;          // phase
    float width = 1.0f;           // 0..2 (stereo width)
    bool bypassInserts = false, bypassEq = false, bypassFilters = false;

    // EQ: 0 LF, 1 LMF, 2 HMF, 3 HF
    EqBand band[4];
    bool hpfOn = false; float hpfFreq = 80.0f;    int hpfSlope = 1;   // 0 12 dB, 1 24 dB, 2 48 dB / oct
    bool lpfOn = false; float lpfFreq = 16000.0f; int lpfSlope = 1;

    ChannelEqSettings()
    {
        band[0] = { false, 100.0f, 0.0f, 0.7f, false };
        band[1] = { false, 400.0f, 0.0f, 1.0f, true };
        band[2] = { false, 2500.0f, 0.0f, 1.0f, true };
        band[3] = { false, 8000.0f, 0.0f, 0.7f, false };
    }

    bool sameFilters (const ChannelEqSettings& o) const
    {
        for (int i = 0; i < 4; ++i) if (! (band[i] == o.band[i])) return false;
        return hpfOn == o.hpfOn && hpfFreq == o.hpfFreq && hpfSlope == o.hpfSlope
            && lpfOn == o.lpfOn && lpfFreq == o.lpfFreq && lpfSlope == o.lpfSlope;
    }
    bool anyBandOn() const { for (auto& b : band) if (b.on && std::abs (b.gain) > 0.01f) return true; return false; }
    bool anyFilterOn() const { return hpfOn || lpfOn; }
    bool isDefault() const { return ! anyBandOn() && ! anyFilterOn() && trimDb == 0.0f && ! invert && width == 1.0f; }
};

// Band ranges (from the channel strip spec)
struct EqRange { float minF, maxF; };
inline EqRange bandRange (int b)
{
    static const EqRange r[] = { { 20.0f, 500.0f }, { 80.0f, 5000.0f }, { 500.0f, 12000.0f }, { 1000.0f, 20000.0f } };
    return r[juce::jlimit (0, 3, b)];
}
inline EqRange hpfRange() { return { 20.0f, 2000.0f }; }
inline EqRange lpfRange() { return { 500.0f, 20000.0f }; }

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

    // RBJ audio EQ cookbook
    static Biquad peak (double sr, double f, double q, double db)
    {
        const double A = std::pow (10.0, db / 40.0), w = 2.0 * juce::MathConstants<double>::pi * f / sr;
        const double alpha = std::sin (w) / (2.0 * q), c = std::cos (w);
        return norm (1 + alpha * A, -2 * c, 1 - alpha * A, 1 + alpha / A, -2 * c, 1 - alpha / A);
    }
    static Biquad lowShelf (double sr, double f, double q, double db)
    {
        const double A = std::pow (10.0, db / 40.0), w = 2.0 * juce::MathConstants<double>::pi * f / sr;
        const double c = std::cos (w), alpha = std::sin (w) / (2.0 * q), s = 2.0 * std::sqrt (A) * alpha;
        return norm (A * ((A + 1) - (A - 1) * c + s), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - s),
                     (A + 1) + (A - 1) * c + s, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - s);
    }
    static Biquad highShelf (double sr, double f, double q, double db)
    {
        const double A = std::pow (10.0, db / 40.0), w = 2.0 * juce::MathConstants<double>::pi * f / sr;
        const double c = std::cos (w), alpha = std::sin (w) / (2.0 * q), s = 2.0 * std::sqrt (A) * alpha;
        return norm (A * ((A + 1) + (A - 1) * c + s), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - s),
                     (A + 1) - (A - 1) * c + s, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - s);
    }
    static Biquad highPass (double sr, double f, double q)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * f / sr, c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        return norm ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + alpha, -2 * c, 1 - alpha);
    }
    static Biquad lowPass (double sr, double f, double q)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * f / sr, c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        return norm ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + alpha, -2 * c, 1 - alpha);
    }

    double magnitude (double f, double sr) const
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * f / sr;
        const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
        return std::abs ((b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2));
    }

private:
    static Biquad norm (double nb0, double nb1, double nb2, double na0, double na1, double na2)
    {
        return { nb0 / na0, nb1 / na0, nb2 / na0, na1 / na0, na2 / na0 };
    }
};

struct BiquadState
{
    double z1 = 0, z2 = 0;
    inline float process (const Biquad& c, float x) noexcept
    {
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return (float) y;
    }
    void reset() { z1 = z2 = 0; }
};

// Butterworth sections for 12 / 24 / 48 dB per octave
inline int butterworthQs (int slope, double* qs)
{
    if (slope <= 0) { qs[0] = 0.70710678; return 1; }
    if (slope == 1) { qs[0] = 0.54119610; qs[1] = 1.30656296; return 2; }
    qs[0] = 0.50979558; qs[1] = 0.60134489; qs[2] = 0.89997622; qs[3] = 2.56291545; return 4;
}

inline Biquad makeBand (const EqBand& b, int index, double sr)
{
    const double f = juce::jlimit (10.0, sr * 0.45, (double) b.freq);
    const double q = juce::jlimit (0.1, 10.0, (double) b.q);
    if (index == 0 && ! b.bell) return Biquad::lowShelf (sr, f, q, b.gain);
    if (index == 3 && ! b.bell) return Biquad::highShelf (sr, f, q, b.gain);
    return Biquad::peak (sr, f, q, b.gain);
}

inline int makeFilter (bool highPass, float freq, int slope, double sr, Biquad* out)
{
    double qs[4];
    const int n = butterworthQs (slope, qs);
    const double f = juce::jlimit (10.0, sr * 0.45, (double) freq);
    for (int i = 0; i < n; ++i) out[i] = highPass ? Biquad::highPass (sr, f, qs[i]) : Biquad::lowPass (sr, f, qs[i]);
    return n;
}

// Response in dB. which: -1 everything, 0..3 one band, 4 HPF, 5 LPF.
inline double responseDb (const ChannelEqSettings& s, double f, double sr, int which = -1)
{
    double mag = 1.0;
    auto incl = [which] (int k) { return which < 0 || which == k; };
    if (! s.bypassEq)
        for (int b = 0; b < 4; ++b)
            if (incl (b) && s.band[b].on) mag *= makeBand (s.band[b], b, sr).magnitude (f, sr);
    if (! s.bypassFilters)
    {
        Biquad sec[4];
        if (incl (4) && s.hpfOn) { const int n = makeFilter (true, s.hpfFreq, s.hpfSlope, sr, sec); for (int i = 0; i < n; ++i) mag *= sec[i].magnitude (f, sr); }
        if (incl (5) && s.lpfOn) { const int n = makeFilter (false, s.lpfFreq, s.lpfSlope, sr, sec); for (int i = 0; i < n; ++i) mag *= sec[i].magnitude (f, sr); }
    }
    return 20.0 * std::log10 (juce::jmax (1.0e-9, mag));
}

// Audio-thread state for one channel (allocation-free).
struct ChannelEqDsp
{
    void process (const ChannelEqSettings& s, juce::AudioBuffer<float>& buf, int n, double sr)
    {
        if (! valid || sr != rate || ! s.sameFilters (cached)) rebuild (s, sr);
        const int chans = juce::jmin (2, buf.getNumChannels());
        const bool doFilters = ! s.bypassFilters, doEq = ! s.bypassEq;
        for (int c = 0; c < chans; ++c)
        {
            auto* d = buf.getWritePointer (c);
            auto& st = state[(size_t) c];
            if (doFilters)
            {
                for (int k = 0; k < numHp; ++k) for (int i = 0; i < n; ++i) d[i] = st[(size_t) (4 + k)].process (hp[k], d[i]);
                for (int k = 0; k < numLp; ++k) for (int i = 0; i < n; ++i) d[i] = st[(size_t) (8 + k)].process (lp[k], d[i]);
            }
            if (doEq)
                for (int b = 0; b < 4; ++b)
                    if (bandActive[b])
                        for (int i = 0; i < n; ++i) d[i] = st[(size_t) b].process (bands[b], d[i]);
        }
        // flush denormals / blow-ups
        for (auto& ch : state) for (auto& x : ch) if (! std::isfinite (x.z1) || ! std::isfinite (x.z2)) x.reset();
    }

    void reset() { for (auto& ch : state) for (auto& x : ch) x.reset(); }

private:
    void rebuild (const ChannelEqSettings& s, double sr)
    {
        const bool hpWas = cached.hpfOn, lpWas = cached.lpfOn;
        cached = s;
        rate = sr;
        for (int b = 0; b < 4; ++b)
        {
            const bool active = s.band[b].on && std::abs (s.band[b].gain) > 0.01f;
            if (active && ! bandActive[b]) for (auto& ch : state) ch[(size_t) b].reset();
            bandActive[b] = active;
            if (active) bands[b] = makeBand (s.band[b], b, sr);
        }
        numHp = s.hpfOn ? makeFilter (true, s.hpfFreq, s.hpfSlope, sr, hp) : 0;
        numLp = s.lpfOn ? makeFilter (false, s.lpfFreq, s.lpfSlope, sr, lp) : 0;
        if (! valid || (s.hpfOn && ! hpWas)) for (auto& ch : state) for (int k = 4; k < 8; ++k) ch[(size_t) k].reset();
        if (! valid || (s.lpfOn && ! lpWas)) for (auto& ch : state) for (int k = 8; k < 12; ++k) ch[(size_t) k].reset();
        valid = true;
    }

    ChannelEqSettings cached;
    bool valid = false;
    double rate = 0.0;
    Biquad bands[4], hp[4], lp[4];
    bool bandActive[4] { false, false, false, false };
    int numHp = 0, numLp = 0;
    std::array<std::array<BiquadState, 12>, 2> state;
};
} // namespace cr
