#pragma once
#include <JuceHeader.h>

// ============================================================================
// Built-in effects. Every effect exposes a flat list of float parameters so the
// mixer can draw a generic knob editor and the session file can store them.
// ============================================================================

struct FxParam
{
    juce::String name;
    float minValue = 0.0f, maxValue = 1.0f, defaultValue = 0.0f;
    float value = 0.0f;
    juce::String suffix;
    juce::StringArray choices; // non-empty => integer choice parameter
};

class BuiltinEffect
{
public:
    virtual ~BuiltinEffect() = default;

    virtual juce::String getType() const = 0;
    virtual void prepare (double sampleRate, int maxBlockSize) = 0;
    virtual void process (juce::AudioBuffer<float>& buffer, int numSamples) = 0;
    virtual void reset() {}

    void setTempo (double bpm) { tempo = bpm; }

    // Effects that shape the frequency response report it here, so the mixer can
    // draw their curve on the channel EQ display (message thread only).
    virtual bool hasFrequencyResponse() const { return false; }
    virtual double getResponseDb (double /*freq*/, double /*sampleRate*/) const { return 0.0; }

    std::vector<FxParam> params;

    float get (int index) const { return params[(size_t) index].value; }
    int getChoice (int index) const { return juce::roundToInt (params[(size_t) index].value); }

    void setParam (int index, float v)
    {
        if (juce::isPositiveAndBelow (index, (int) params.size()))
        {
            auto& p = params[(size_t) index];
            p.value = juce::jlimit (p.minValue, p.maxValue, v);
        }
    }

protected:
    void addParam (const juce::String& name, float mn, float mx, float def, const juce::String& suffix = {})
    {
        params.push_back ({ name, mn, mx, def, def, suffix, {} });
    }

    void addChoice (const juce::String& name, const juce::StringArray& choices, int def = 0)
    {
        params.push_back ({ name, 0.0f, (float) (choices.size() - 1), (float) def, (float) def, {}, choices });
    }

    double sampleRate = 44100.0;
    double tempo = 120.0;
};

juce::StringArray getBuiltinEffectTypes();
std::unique_ptr<BuiltinEffect> createBuiltinEffect (const juce::String& type);
