#include "Effects.h"
#include "DspUtils.h"

namespace
{
// ---------------------------------------------------------------------------
class SculptEq : public BuiltinEffect   // 6-band EQ
{
public:
    SculptEq()
    {
        const float freqs[] = { 80.0f, 250.0f, 800.0f, 2500.0f, 6000.0f, 12000.0f };
        const char* names[] = { "Low", "Low Mid", "Mid", "Hi Mid", "Presence", "High" };
        for (int b = 0; b < 6; ++b)
        {
            addParam (juce::String (names[b]) + " Freq", 20.0f, 20000.0f, freqs[b], " Hz");
            addParam (juce::String (names[b]) + " Gain", -18.0f, 18.0f, 0.0f, " dB");
        }
        addParam ("Q", 0.3f, 6.0f, 0.9f);
        addParam ("Output", -24.0f, 12.0f, 0.0f, " dB");
    }

    juce::String getType() const override { return "6-Band EQ"; }

    void prepare (double sr, int) override { sampleRate = sr; lastHash = -1.0f; reset(); }

    void reset() override
    {
        for (auto& ch : filters)
            for (auto& f : ch) f.reset();
    }

    void process (juce::AudioBuffer<float>& buffer, int n) override
    {
        updateCoefficients();
        const float out = dbToGain (get (13));
        for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
            {
                float x = d[i];
                for (int b = 0; b < 6; ++b)
                    if (std::abs (get (b * 2 + 1)) > 0.01f)
                        x = filters[(size_t) ch][(size_t) b].processSample (x);
                d[i] = x * out;
            }
        }
    }

private:
    void updateCoefficients()
    {
        float hash = 0.0f;
        for (auto& p : params) hash = hash * 1.0001f + p.value;
        if (hash == lastHash) return;
        lastHash = hash;

        const float q = get (12);
        for (int b = 0; b < 6; ++b)
        {
            const float f = juce::jlimit (20.0f, (float) sampleRate * 0.45f, get (b * 2));
            const float g = juce::Decibels::decibelsToGain (get (b * 2 + 1));
            juce::dsp::IIR::Coefficients<float>::Ptr c;
            if (b == 0)      c = juce::dsp::IIR::Coefficients<float>::makeLowShelf (sampleRate, f, q, g);
            else if (b == 5) c = juce::dsp::IIR::Coefficients<float>::makeHighShelf (sampleRate, f, q, g);
            else             c = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sampleRate, f, q, g);
            for (auto& ch : filters) *ch[(size_t) b].coefficients = *c;
        }
    }

    std::array<std::array<juce::dsp::IIR::Filter<float>, 6>, 2> filters;
    float lastHash = -1.0f;
};

// ---------------------------------------------------------------------------
class Dynamics : public BuiltinEffect   // compressor / limiter / expander
{
public:
    Dynamics()
    {
        addChoice ("Mode", { "Compressor", "Limiter", "Expander" });
        addParam ("Threshold", -60.0f, 0.0f, -18.0f, " dB");
        addParam ("Ratio", 1.0f, 20.0f, 4.0f, ":1");
        addParam ("Attack", 0.1f, 200.0f, 10.0f, " ms");
        addParam ("Release", 5.0f, 2000.0f, 120.0f, " ms");
        addParam ("Makeup", 0.0f, 24.0f, 0.0f, " dB");
    }

    juce::String getType() const override { return "Compressor"; }
    void prepare (double sr, int) override { sampleRate = sr; env = 0.0f; }
    void reset() override { env = 0.0f; }

    void process (juce::AudioBuffer<float>& buffer, int n) override
    {
        const int mode = getChoice (0);
        const float thr = get (1), ratio = get (2);
        const float attackMs = mode == 1 ? 0.1f : get (3);
        const float aCoef = std::exp (-1.0f / (0.001f * attackMs * (float) sampleRate));
        const float rCoef = std::exp (-1.0f / (0.001f * get (4) * (float) sampleRate));
        const float makeup = dbToGain (get (5));
        const int nch = juce::jmin (2, buffer.getNumChannels());

        for (int i = 0; i < n; ++i)
        {
            float peak = 0.0f;
            for (int ch = 0; ch < nch; ++ch) peak = juce::jmax (peak, std::abs (buffer.getSample (ch, i)));
            env = peak > env ? aCoef * env + (1.0f - aCoef) * peak : rCoef * env + (1.0f - rCoef) * peak;

            const float envDb = gainToDb (env);
            float gainDb = 0.0f;
            if (mode == 0 && envDb > thr)      gainDb = -(envDb - thr) * (1.0f - 1.0f / ratio);
            else if (mode == 1 && envDb > thr) gainDb = -(envDb - thr);
            else if (mode == 2 && envDb < thr) gainDb = juce::jmax (-60.0f, (envDb - thr) * (ratio - 1.0f));

            const float g = dbToGain (gainDb) * makeup;
            for (int ch = 0; ch < nch; ++ch) buffer.setSample (ch, i, buffer.getSample (ch, i) * g);
        }
    }

private:
    float env = 0.0f;
};

// ---------------------------------------------------------------------------
class Echo : public BuiltinEffect   // tempo-synced mono-time delay
{
public:
    Echo()
    {
        addChoice ("Time", syncNames(), 4);
        addParam ("Feedback", 0.0f, 0.95f, 0.35f);
        addParam ("Tone", 500.0f, 20000.0f, 8000.0f, " Hz");
        addParam ("Mix", 0.0f, 1.0f, 0.3f);
    }

    juce::String getType() const override { return "Delay"; }

    void prepare (double sr, int) override
    {
        sampleRate = sr;
        for (auto& d : lines) d.setMaxSamples ((int) (sr * 5.0));
        lp[0] = lp[1] = 0.0f;
    }

    void reset() override { for (auto& d : lines) d.clear(); }

    void process (juce::AudioBuffer<float>& buffer, int n) override
    {
        const double delay = syncBeats (getChoice (0)) * 60.0 / tempo * sampleRate;
        const float fb = get (1), mix = get (3);
        const float lpCoef = std::exp (-juce::MathConstants<float>::twoPi * get (2) / (float) sampleRate);
        for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
            {
                float wet = lines[(size_t) ch].read (delay);
                lp[ch] = (1.0f - lpCoef) * wet + lpCoef * lp[ch];
                lines[(size_t) ch].push (d[i] + lp[ch] * fb);
                d[i] = d[i] * (1.0f - mix) + wet * mix;
            }
        }
    }

private:
    std::array<DelayLine, 2> lines;
    float lp[2] {};
};

// ---------------------------------------------------------------------------
class DualEcho : public BuiltinEffect   // stereo delay with independent times
{
public:
    DualEcho()
    {
        addChoice ("Left Time", syncNames(), 3);
        addChoice ("Right Time", syncNames(), 5);
        addParam ("Feedback", 0.0f, 0.95f, 0.4f);
        addParam ("Cross", 0.0f, 1.0f, 0.3f);
        addParam ("Mix", 0.0f, 1.0f, 0.3f);
    }

    juce::String getType() const override { return "Stereo Delay"; }

    void prepare (double sr, int) override
    {
        sampleRate = sr;
        for (auto& d : lines) d.setMaxSamples ((int) (sr * 5.0));
    }

    void reset() override { for (auto& d : lines) d.clear(); }

    void process (juce::AudioBuffer<float>& buffer, int n) override
    {
        if (buffer.getNumChannels() < 2) return;
        const double beat = 60.0 / tempo * sampleRate;
        const double dl = syncBeats (getChoice (0)) * beat, dr = syncBeats (getChoice (1)) * beat;
        const float fb = get (2), cross = get (3), mix = get (4);
        auto* l = buffer.getWritePointer (0);
        auto* r = buffer.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            const float wl = lines[0].read (dl), wr = lines[1].read (dr);
            lines[0].push (l[i] + fb * ((1.0f - cross) * wl + cross * wr));
            lines[1].push (r[i] + fb * ((1.0f - cross) * wr + cross * wl));
            l[i] = l[i] * (1.0f - mix) + wl * mix;
            r[i] = r[i] * (1.0f - mix) + wr * mix;
        }
    }

private:
    std::array<DelayLine, 2> lines;
};

// ---------------------------------------------------------------------------
class Space : public BuiltinEffect   // reverb
{
public:
    Space()
    {
        addParam ("Size", 0.0f, 1.0f, 0.6f);
        addParam ("Damping", 0.0f, 1.0f, 0.5f);
        addParam ("Width", 0.0f, 1.0f, 1.0f);
        addParam ("Mix", 0.0f, 1.0f, 0.3f);
    }

    juce::String getType() const override { return "Reverb"; }
    void prepare (double sr, int) override { sampleRate = sr; reverb.setSampleRate (sr); reverb.reset(); }
    void reset() override { reverb.reset(); }

    void process (juce::AudioBuffer<float>& buffer, int n) override
    {
        juce::Reverb::Parameters p;
        p.roomSize = get (0);
        p.damping = get (1);
        p.width = get (2);
        p.wetLevel = get (3);
        p.dryLevel = 1.0f - get (3);
        reverb.setParameters (p);
        if (buffer.getNumChannels() >= 2)
            reverb.processStereo (buffer.getWritePointer (0), buffer.getWritePointer (1), n);
        else
            reverb.processMono (buffer.getWritePointer (0), n);
    }

private:
    juce::Reverb reverb;
};

// ---------------------------------------------------------------------------
class Swirl : public BuiltinEffect   // chorus / flanger / vibrato
{
public:
    Swirl()
    {
        addChoice ("Mode", { "Chorus", "Flanger", "Vibrato" });
        addParam ("Rate", 0.05f, 8.0f, 0.8f, " Hz");
        addParam ("Depth", 0.0f, 1.0f, 0.5f);
        addParam ("Feedback", 0.0f, 0.9f, 0.2f);
        addParam ("Mix", 0.0f, 1.0f, 0.5f);
    }

    juce::String getType() const override { return "Chorus / Flanger"; }

    void prepare (double sr, int) override
    {
        sampleRate = sr;
        for (auto& d : lines) d.setMaxSamples ((int) (sr * 0.05));
        phase = 0.0;
    }

    void reset() override { for (auto& d : lines) d.clear(); }

    void process (juce::AudioBuffer<float>& buffer, int n) override
    {
        const int mode = getChoice (0);
        const double centreMs = mode == 0 ? 15.0 : (mode == 1 ? 2.5 : 5.0);
        const double depthMs = (mode == 0 ? 7.0 : (mode == 1 ? 2.0 : 3.0)) * get (2);
        const float fb = mode == 2 ? 0.0f : get (3);
        const float mix = mode == 2 ? 1.0f : get (4);
        const double inc = get (1) / sampleRate;
        const int nch = juce::jmin (2, buffer.getNumChannels());

        for (int i = 0; i < n; ++i)
        {
            for (int ch = 0; ch < nch; ++ch)
            {
                const double lfo = std::sin (juce::MathConstants<double>::twoPi * (phase + ch * 0.25));
                const double delay = (centreMs + depthMs * lfo) * 0.001 * sampleRate;
                const float x = buffer.getSample (ch, i);
                const float wet = lines[(size_t) ch].read (delay);
                lines[(size_t) ch].push (x + wet * fb);
                buffer.setSample (ch, i, x * (1.0f - mix) + wet * mix);
            }
            phase += inc;
            if (phase >= 1.0) phase -= 1.0;
        }
    }

private:
    std::array<DelayLine, 2> lines;
    double phase = 0.0;
};

// ---------------------------------------------------------------------------
class Heat : public BuiltinEffect   // overdrive
{
public:
    Heat()
    {
        addParam ("Drive", 0.0f, 36.0f, 12.0f, " dB");
        addChoice ("Shape", { "Soft", "Hard", "Fold" });
        addParam ("Tone", 500.0f, 20000.0f, 9000.0f, " Hz");
        addParam ("Output", -24.0f, 6.0f, -6.0f, " dB");
        addParam ("Mix", 0.0f, 1.0f, 1.0f);
    }

    juce::String getType() const override { return "Overdrive"; }
    void prepare (double sr, int) override { sampleRate = sr; lp[0] = lp[1] = 0.0f; }

    void process (juce::AudioBuffer<float>& buffer, int n) override
    {
        const float drive = dbToGain (get (0)), out = dbToGain (get (3)), mix = get (4);
        const int shape = getChoice (1);
        const float c = std::exp (-juce::MathConstants<float>::twoPi * get (2) / (float) sampleRate);
        for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
            {
                float x = d[i] * drive, y;
                if (shape == 0)      y = std::tanh (x);
                else if (shape == 1) y = juce::jlimit (-1.0f, 1.0f, x);
                else                 y = std::sin (x);
                lp[ch] = (1.0f - c) * y + c * lp[ch];
                d[i] = d[i] * (1.0f - mix) + lp[ch] * out * mix;
            }
        }
    }

private:
    float lp[2] {};
};

// ---------------------------------------------------------------------------
class Grit : public BuiltinEffect   // bit crusher / sample-rate reducer
{
public:
    Grit()
    {
        addParam ("Bits", 1.0f, 24.0f, 12.0f);
        addParam ("Downsample", 1.0f, 32.0f, 2.0f, "x");
        addParam ("Mix", 0.0f, 1.0f, 1.0f);
    }

    juce::String getType() const override { return "Bit Crusher"; }
    void prepare (double sr, int) override { sampleRate = sr; counter = 0; }

    void process (juce::AudioBuffer<float>& buffer, int n) override
    {
        const float levels = std::pow (2.0f, get (0) - 1.0f);
        const int factor = juce::jmax (1, juce::roundToInt (get (1)));
        const float mix = get (2);
        const int nch = juce::jmin (2, buffer.getNumChannels());
        for (int i = 0; i < n; ++i)
        {
            if (counter == 0)
                for (int ch = 0; ch < nch; ++ch)
                    held[ch] = std::round (buffer.getSample (ch, i) * levels) / levels;
            if (++counter >= factor) counter = 0;
            for (int ch = 0; ch < nch; ++ch)
                buffer.setSample (ch, i, buffer.getSample (ch, i) * (1.0f - mix) + held[ch] * mix);
        }
    }

private:
    int counter = 0;
    float held[2] {};
};

// ---------------------------------------------------------------------------
class Contour : public BuiltinEffect   // multimode filter with LFO
{
public:
    Contour()
    {
        addChoice ("Type", { "Low Pass", "High Pass", "Band Pass", "Notch" });
        addParam ("Cutoff", 20.0f, 20000.0f, 2000.0f, " Hz");
        addParam ("Resonance", 0.5f, 10.0f, 1.0f);
        addParam ("LFO Rate", 0.05f, 10.0f, 1.0f, " Hz");
        addParam ("LFO Depth", 0.0f, 4.0f, 0.0f, " oct");
    }

    juce::String getType() const override { return "Filter"; }
    void prepare (double sr, int) override { sampleRate = sr; reset(); }
    void reset() override { for (auto& f : svf) f.reset(); }

    void process (juce::AudioBuffer<float>& buffer, int n) override
    {
        const int type = getChoice (0);
        const int step = 32;
        for (int start = 0; start < n; start += step)
        {
            const int len = juce::jmin (step, n - start);
            const float lfo = (float) std::sin (juce::MathConstants<double>::twoPi * phase) * get (4);
            const float fc = get (1) * std::pow (2.0f, lfo);
            for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
            {
                svf[(size_t) ch].setParams (sampleRate, fc, get (2));
                auto* d = buffer.getWritePointer (ch, start);
                for (int i = 0; i < len; ++i) d[i] = svf[(size_t) ch].process (d[i], type);
            }
            phase += get (3) * len / sampleRate;
            if (phase >= 1.0) phase -= 1.0;
        }
    }

private:
    std::array<Svf, 2> svf;
    double phase = 0.0;
};

// ---------------------------------------------------------------------------
class Orbit : public BuiltinEffect   // tempo-synced auto-pan
{
public:
    Orbit()
    {
        addChoice ("Rate", syncNames(), 5);
        addParam ("Depth", 0.0f, 1.0f, 0.7f);
        addChoice ("Shape", { "Sine", "Triangle", "Square" });
    }

    juce::String getType() const override { return "Auto-Pan"; }
    void prepare (double sr, int) override { sampleRate = sr; phase = 0.0; }

    void process (juce::AudioBuffer<float>& buffer, int n) override
    {
        if (buffer.getNumChannels() < 2) return;
        const double periodSamples = syncBeats (getChoice (0)) * 60.0 / tempo * sampleRate;
        const float depth = get (1);
        const int shape = getChoice (2);
        auto* l = buffer.getWritePointer (0);
        auto* r = buffer.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            float lfo;
            if (shape == 0)      lfo = (float) std::sin (juce::MathConstants<double>::twoPi * phase);
            else if (shape == 1) lfo = (float) (4.0 * std::abs (phase - 0.5) - 1.0);
            else                 lfo = phase < 0.5 ? 1.0f : -1.0f;
            const float pan = lfo * depth; // -1..1
            const float angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            const float mono = 0.5f * (l[i] + r[i]);
            const float side = 0.5f * (l[i] - r[i]);
            l[i] = (mono + side) * std::cos (angle) * juce::MathConstants<float>::sqrt2;
            r[i] = (mono - side) * std::sin (angle) * juce::MathConstants<float>::sqrt2;
            phase += 1.0 / periodSamples;
            if (phase >= 1.0) phase -= 1.0;
        }
    }

private:
    double phase = 0.0;
};

// ---------------------------------------------------------------------------
class Wide : public BuiltinEffect   // stereo widener
{
public:
    Wide()
    {
        addParam ("Width", 0.0f, 2.0f, 1.4f);
        addParam ("Bass Mono", 20.0f, 500.0f, 120.0f, " Hz");
    }

    juce::String getType() const override { return "Stereo Widener"; }
    void prepare (double sr, int) override { sampleRate = sr; lpSide = 0.0f; }

    void process (juce::AudioBuffer<float>& buffer, int n) override
    {
        if (buffer.getNumChannels() < 2) return;
        const float width = get (0);
        const float c = std::exp (-juce::MathConstants<float>::twoPi * get (1) / (float) sampleRate);
        auto* l = buffer.getWritePointer (0);
        auto* r = buffer.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            const float mid = 0.5f * (l[i] + r[i]);
            float side = 0.5f * (l[i] - r[i]);
            lpSide = (1.0f - c) * side + c * lpSide;
            side = (side - lpSide) * width; // keep low end mono
            l[i] = mid + side;
            r[i] = mid - side;
        }
    }

private:
    float lpSide = 0.0f;
};

// ---------------------------------------------------------------------------
class Chop : public BuiltinEffect   // rhythmic slicer / trance gate
{
public:
    Chop()
    {
        addChoice ("Rate", { "1/8", "1/16", "1/32" }, 1);
        addChoice ("Pattern", { "Every step", "Off-beats", "Dotted", "Broken" });
        addParam ("Depth", 0.0f, 1.0f, 1.0f);
        addParam ("Smooth", 0.5f, 30.0f, 4.0f, " ms");
    }

    juce::String getType() const override { return "Slicer"; }
    void prepare (double sr, int) override { sampleRate = sr; pos = 0.0; level = 1.0f; }

    void process (juce::AudioBuffer<float>& buffer, int n) override
    {
        static const int patterns[4][16] = {
            { 1,0,1,0, 1,0,1,0, 1,0,1,0, 1,0,1,0 },
            { 0,1,0,1, 0,1,0,1, 0,1,0,1, 0,1,0,1 },
            { 1,0,0,1, 0,0,1,0, 1,0,0,1, 0,0,1,0 },
            { 1,1,0,1, 0,1,1,0, 1,0,1,1, 0,1,0,1 } };
        const double stepBeats = getChoice (0) == 0 ? 0.5 : (getChoice (0) == 1 ? 0.25 : 0.125);
        const double stepSamples = stepBeats * 60.0 / tempo * sampleRate;
        const int pat = getChoice (1);
        const float depth = get (2);
        const float coef = std::exp (-1.0f / (0.001f * get (3) * (float) sampleRate));
        const int nch = juce::jmin (2, buffer.getNumChannels());
        for (int i = 0; i < n; ++i)
        {
            const int step = ((int) (pos / stepSamples)) % 16;
            const float target = patterns[pat][step] != 0 ? 1.0f : 1.0f - depth;
            level = coef * level + (1.0f - coef) * target;
            for (int ch = 0; ch < nch; ++ch) buffer.setSample (ch, i, buffer.getSample (ch, i) * level);
            pos += 1.0;
            if (pos >= stepSamples * 16.0) pos -= stepSamples * 16.0;
        }
    }

private:
    double pos = 0.0;
    float level = 1.0f;
};

// ---------------------------------------------------------------------------
class Gate : public BuiltinEffect   // noise gate
{
public:
    Gate()
    {
        addParam ("Threshold", -80.0f, 0.0f, -45.0f, " dB");
        addParam ("Attack", 0.1f, 50.0f, 1.0f, " ms");
        addParam ("Hold", 0.0f, 500.0f, 30.0f, " ms");
        addParam ("Release", 5.0f, 1000.0f, 100.0f, " ms");
        addParam ("Range", -80.0f, 0.0f, -80.0f, " dB");
    }

    juce::String getType() const override { return "Noise Gate"; }
    void prepare (double sr, int) override { sampleRate = sr; gain = 0.0f; holdCounter = 0; }

    void process (juce::AudioBuffer<float>& buffer, int n) override
    {
        const float thr = dbToGain (get (0));
        const float aC = std::exp (-1.0f / (0.001f * get (1) * (float) sampleRate));
        const float rC = std::exp (-1.0f / (0.001f * get (3) * (float) sampleRate));
        const int holdSamples = (int) (get (2) * 0.001f * sampleRate);
        const float floorGain = dbToGain (get (4));
        const int nch = juce::jmin (2, buffer.getNumChannels());
        for (int i = 0; i < n; ++i)
        {
            float peak = 0.0f;
            for (int ch = 0; ch < nch; ++ch) peak = juce::jmax (peak, std::abs (buffer.getSample (ch, i)));
            if (peak > thr) holdCounter = holdSamples;
            const bool open = holdCounter > 0;
            if (holdCounter > 0) --holdCounter;
            const float target = open ? 1.0f : floorGain;
            gain = target > gain ? aC * gain + (1.0f - aC) * target : rC * gain + (1.0f - rC) * target;
            for (int ch = 0; ch < nch; ++ch) buffer.setSample (ch, i, buffer.getSample (ch, i) * gain);
        }
    }

private:
    float gain = 0.0f;
    int holdCounter = 0;
};

} // namespace

juce::StringArray getBuiltinEffectTypes()
{
    return { "6-Band EQ", "Compressor", "Delay", "Stereo Delay", "Reverb", "Chorus / Flanger", "Overdrive",
             "Bit Crusher", "Filter", "Auto-Pan", "Stereo Widener", "Slicer", "Noise Gate" };
}

std::unique_ptr<BuiltinEffect> createBuiltinEffect (const juce::String& type)
{
    if (type == "6-Band EQ") return std::make_unique<SculptEq>();
    if (type == "Compressor")  return std::make_unique<Dynamics>();
    if (type == "Delay")      return std::make_unique<Echo>();
    if (type == "Stereo Delay") return std::make_unique<DualEcho>();
    if (type == "Reverb")     return std::make_unique<Space>();
    if (type == "Chorus / Flanger")     return std::make_unique<Swirl>();
    if (type == "Overdrive")      return std::make_unique<Heat>();
    if (type == "Bit Crusher")      return std::make_unique<Grit>();
    if (type == "Filter")   return std::make_unique<Contour>();
    if (type == "Auto-Pan")     return std::make_unique<Orbit>();
    if (type == "Stereo Widener")      return std::make_unique<Wide>();
    if (type == "Slicer")      return std::make_unique<Chop>();
    if (type == "Noise Gate")      return std::make_unique<Gate>();
    return nullptr;
}
