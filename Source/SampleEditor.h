#pragma once
#include "Views.h"

// Waveform display / editor for one zone of a layer: start / end / loop markers,
// slice markers, selections, zoom and scroll. Used by the Sampler tab and the Audio Editor.
class WaveformView : public juce::Component
{
public:
    enum class Mode { markers, slices, select };

    WaveformView();
    void setTarget (int bank, int pad, int layer, int zone);
    void setMode (Mode m) { mode = m; repaint(); }
    Mode getMode() const { return mode; }
    void setAccent (juce::Colour c) { accent = c; repaint(); }
    void zoom (double factor, double centre = -1.0);
    void scroll (double fraction);
    void zoomToFit() { viewStart = 0.0; viewEnd = 1.0; repaint(); }
    void zoomToSelection();
    juce::String getZoomText() const;

    bool zeroSnap = true;
    float selStart = 0.0f, selEnd = 0.0f;   // selection (select mode), normalised
    std::function<void()> onChange;          // markers / slices moved

    cr::Zone* getZone() const;
    cr::SamplePtr getSample() const;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;

private:
    float xToPos (float x) const;
    float posToX (float pos) const;
    float snap (float pos) const;
    juce::Rectangle<int> rulerArea() const { return getLocalBounds().removeFromTop (18); }
    juce::Rectangle<int> waveArea() const { return getLocalBounds().withTrimmedTop (18); }
    void buildPeaks();
    int hitMarker (int x, int y) const;   // 0 start, 1 end, 2 loop start, 3 loop end, 100 + i slice

    int bank = 0, pad = 0, layer = 0, zone = 0;
    Mode mode = Mode::markers;
    juce::Colour accent = Pal::accent;
    double viewStart = 0.0, viewEnd = 1.0;
    int dragging = -1;
    float dragAnchor = 0.0f;
    // peaks cache
    const cr::SampleData* peakSample = nullptr;
    double peakStart = -1.0, peakEnd = -1.0;
    int peakWidth = 0;
    std::vector<float> peakMin, peakMax;
    std::vector<float> overviewMin, overviewMax;   // whole-file overview for streamed samples
    const cr::SampleData* overviewSample = nullptr;
};
