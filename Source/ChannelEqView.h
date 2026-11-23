#pragma once
#include "UiCommon.h"

// Channel EQ display and editor for the mixer (ChannelEqView.cpp).

namespace eqview
{
juce::Colour bandColour (int handle);   // 0 LF, 1 LMF, 2 HMF, 3 HF, 4 HPF, 5 LPF
}

// Small read-only curve drawn in each mixer strip.
class EqThumbnail : public juce::Component
{
public:
    int key = 0;
    std::function<void()> onClick;
    void paint (juce::Graphics& g) override;
    void mouseUp (const juce::MouseEvent& e) override { if (onClick && ! e.mouseWasDraggedSinceMouseDown()) onClick(); }
};

// Interactive graph: log frequency axis, +-24 dB, band nodes, analyzer, insert curves.
class EqGraph : public juce::Component, private juce::Timer
{
public:
    EqGraph();
    ~EqGraph() override;

    void setKey (int stripKey);
    int getKey() const { return key; }
    enum AnalyzerMode { analyzerOff = 0, analyzerPost, analyzerPre, analyzerBoth };
    void setAnalyzerMode (int m);
    int getAnalyzerMode() const { return analyzerMode; }
    int selected = 2;
    std::function<void (int)> onSelect;
    std::function<void()> onEdit;

    void paint (juce::Graphics& g) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;
    void visibilityChanged() override;

    // axis helpers (public for the thumbnail)
    static float freqToX (double f, juce::Rectangle<float> r);
    static double xToFreq (float x, juce::Rectangle<float> r);
    static float dbToY (double db, juce::Rectangle<float> r, double range = 24.0);

private:
    void timerCallback() override;
    juce::Rectangle<float> plotArea() const;
    juce::Point<float> handlePos (int h) const;
    int hitHandle (juce::Point<float> p) const;
    void updateTap();
    void computeSpectrum (const float* samples, std::vector<float>& smooth, std::vector<float>& peak);
    void drawSpectrum (juce::Graphics& g, juce::Rectangle<float> r, const std::vector<float>& s, juce::Colour fill, juce::Colour line);

    int key = -1;
    int analyzerMode = analyzerPost;
    int dragging = -1, hover = -1;

    static constexpr int fftOrder = 12, fftSize = 1 << fftOrder;
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::vector<float> fftData, preSmooth, prePeak, postSmooth, postPeak, scratch;
};

// The whole channel EQ panel: graph plus a control column for every band.
class ChannelEqPanel : public juce::Component, private juce::Timer
{
public:
    ChannelEqPanel();
    void setKey (int stripKey, const juce::String& name, juce::Colour colour);
    std::function<void()> onClose;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshButtons();

    int key = -1;
    juce::String name;
    juce::Colour colour = Pal::accent;
    EqGraph graph;
    juce::TextButton bypassInserts { "Inserts" }, bypassEq { "EQ" }, bypassFilters { "Filters" }, resetBtn { "Reset" }, closeBtn { "Close" };
    ChoiceField analyzer { "Analyzer" };
    struct BandColumn
    {
        PowerButton power;
        Knob freq { "Freq" }, gain { "Gain" }, q { "Q" };
        juce::TextButton shape { "Shelf" };
        ChoiceField slope { "Slope" };
        juce::Rectangle<int> area;
    };
    std::array<BandColumn, 6> cols;   // HPF, LF, LMF, HMF, HF, LPF (display order)
    static int handleForColumn (int c) { static const int h[] = { 4, 0, 1, 2, 3, 5 }; return h[c]; }
    juce::Rectangle<int> header;
};
