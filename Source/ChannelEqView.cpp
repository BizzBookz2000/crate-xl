// Channel EQ: the interactive graph (log frequency, +-24 dB, draggable band nodes,
// spectrum analyzer, insert-effect curves), the per-band controls, and the small
// curve shown in every mixer strip.
#include "ChannelEqView.h"

namespace eqview
{
juce::Colour bandColour (int h)
{
    switch (h)
    {
        case 0: return juce::Colour (0xffa77bf3);   // LF  purple
        case 1: return juce::Colour (0xff45d483);   // LMF green
        case 2: return juce::Colour (0xffff9a3c);   // HMF orange
        case 3: return juce::Colour (0xff6cc8ff);   // HF  light blue
        case 4: return juce::Colour (0xffff4a5c);   // HPF red
        default: return juce::Colour (0xfff2d13d);  // LPF yellow
    }
}

const char* handleName (int h)
{
    static const char* n[] = { "LF", "LMF", "HMF", "HF", "HPF", "LPF" };
    return n[juce::jlimit (0, 5, h)];
}

double sampleRateNow() { return juce::jmax (22050.0, Engine::get().getSampleRate()); }

juce::String fmtFreq (double f)
{
    return f >= 1000.0 ? juce::String (f / 1000.0, f >= 10000.0 ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz";
}
} // namespace eqview

using namespace eqview;

// ============================================================================
// Axes
// ============================================================================
static constexpr double minHz = 20.0, maxHz = 20000.0;

float EqGraph::freqToX (double f, juce::Rectangle<float> r)
{
    return r.getX() + r.getWidth() * (float) (std::log (juce::jlimit (minHz, maxHz, f) / minHz) / std::log (maxHz / minHz));
}

double EqGraph::xToFreq (float x, juce::Rectangle<float> r)
{
    const double n = juce::jlimit (0.0, 1.0, (double) ((x - r.getX()) / r.getWidth()));
    return minHz * std::pow (maxHz / minHz, n);
}

float EqGraph::dbToY (double db, juce::Rectangle<float> r, double range)
{
    return r.getCentreY() - (float) (juce::jlimit (-range, range, db) / range) * r.getHeight() * 0.5f;
}

// ============================================================================
// Thumbnail
// ============================================================================
void EqThumbnail::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (Pal::bg);
    g.fillRoundedRectangle (r, 3.0f);
    auto* st = Engine::get().getStrip (key);
    if (st == nullptr) return;
    const auto& s = st->eq;
    g.setColour (Pal::outline);
    g.fillRect (r.getX() + 2.0f, r.getCentreY(), r.getWidth() - 4.0f, 1.0f);
    const bool active = (s.anyBandOn() && ! s.bypassEq) || (s.anyFilterOn() && ! s.bypassFilters);
    if (! active)
    {
        drawCaps (g, "EQ", getLocalBounds(), juce::Justification::centred, Pal::textFaint, 8.5f);
        return;
    }
    const double sr = sampleRateNow();
    auto plot = r.reduced (2.0f, 3.0f);
    juce::Path p;
    for (int x = 0; x <= (int) plot.getWidth(); x += 2)
    {
        const float px = plot.getX() + (float) x;
        const float y = EqGraph::dbToY (cr::responseDb (s, EqGraph::xToFreq (px, plot), sr), plot, 18.0);
        if (x == 0) p.startNewSubPath (px, y); else p.lineTo (px, y);
    }
    g.setColour (Pal::accent);
    g.strokePath (p, juce::PathStrokeType (1.3f));
}

// ============================================================================
// Graph
// ============================================================================
EqGraph::EqGraph()
{
    fftData.assign ((size_t) fftSize * 2, 0.0f);
    scratch.assign ((size_t) fftSize, 0.0f);
    const size_t bins = (size_t) fftSize / 2;
    for (auto* v : { &preSmooth, &prePeak, &postSmooth, &postPeak }) v->assign (bins, -120.0f);
    setRepaintsOnMouseActivity (false);
}

EqGraph::~EqGraph()
{
    auto& e = Engine::get();
    if (auto* st = e.getStrip (key)) if (e.analyzerStrip.load() == st) e.analyzerStrip = nullptr;
}

void EqGraph::setKey (int k)
{
    key = k;
    for (auto* v : { &preSmooth, &prePeak, &postSmooth, &postPeak }) std::fill (v->begin(), v->end(), -120.0f);
    updateTap();
    repaint();
}

void EqGraph::setAnalyzerMode (int m) { analyzerMode = m; updateTap(); repaint(); }

void EqGraph::visibilityChanged() { updateTap(); }

void EqGraph::updateTap()
{
    auto& e = Engine::get();
    const bool on = isShowing() && analyzerMode != analyzerOff;
    if (on) e.analyzerStrip = e.getStrip (key);
    else if (auto* st = e.getStrip (key); st != nullptr && e.analyzerStrip.load() == st) e.analyzerStrip = nullptr;
    startTimerHz (on ? 60 : 20);
}

juce::Rectangle<float> EqGraph::plotArea() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (34.0f).withTrimmedBottom (16.0f).withTrimmedTop (6.0f).withTrimmedRight (8.0f);
}

void EqGraph::timerCallback()
{
    if (! isShowing()) return;
    auto& e = Engine::get();
    if (analyzerMode != analyzerOff && e.analyzerStrip.load() == e.getStrip (key))
    {
        if (analyzerMode != analyzerPre)
        {
            e.analyzerPost.read (scratch.data(), fftSize);
            computeSpectrum (scratch.data(), postSmooth, postPeak);
        }
        if (analyzerMode == analyzerPre || analyzerMode == analyzerBoth)
        {
            e.analyzerPre.read (scratch.data(), fftSize);
            computeSpectrum (scratch.data(), preSmooth, prePeak);
        }
    }
    else if (analyzerMode != analyzerOff) updateTap();
    repaint();
}

void EqGraph::computeSpectrum (const float* samples, std::vector<float>& smooth, std::vector<float>& peak)
{
    std::fill (fftData.begin(), fftData.end(), 0.0f);
    std::copy (samples, samples + fftSize, fftData.begin());
    window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data(), true);
    const float norm = 4.0f / (float) fftSize;   // hann window: sine at 0 dBFS reads ~0 dB
    for (size_t i = 1; i < smooth.size(); ++i)
    {
        const float db = juce::Decibels::gainToDecibels (fftData[i] * norm, -120.0f);
        // average: fast up, slower down
        smooth[i] = db > smooth[i] ? smooth[i] + (db - smooth[i]) * 0.6f : smooth[i] + (db - smooth[i]) * 0.12f;
        peak[i] = juce::jmax (db, peak[i] - 0.25f);
    }
}

void EqGraph::drawSpectrum (juce::Graphics& g, juce::Rectangle<float> r, const std::vector<float>& s, juce::Colour fill, juce::Colour line)
{
    const double sr = sampleRateNow();
    const double binHz = sr / fftSize;
    auto levelY = [&r] (float db, double f)
    {
        const float tilted = db + 3.0f * (float) std::log2 (f / 1000.0);   // +3 dB / octave, so music reads flat-ish
        return r.getBottom() - juce::jlimit (0.0f, 1.0f, (tilted + 90.0f) / 96.0f) * r.getHeight();
    };
    juce::Path p;
    bool started = false;
    for (float x = r.getX(); x <= r.getRight(); x += 1.5f)
    {
        const double f0 = xToFreq (x, r), f1 = xToFreq (x + 1.5f, r);
        const double b0 = f0 / binHz, b1 = f1 / binHz;
        float db;
        if (b1 - b0 < 1.0)
        {
            const int i = juce::jlimit (1, (int) s.size() - 2, (int) b0);
            const float t = (float) (b0 - i);
            db = s[(size_t) i] + (s[(size_t) i + 1] - s[(size_t) i]) * t;
        }
        else
        {
            db = -120.0f;
            for (int i = juce::jmax (1, (int) b0); i <= juce::jmin ((int) s.size() - 1, (int) b1); ++i) db = juce::jmax (db, s[(size_t) i]);
        }
        const float y = levelY (db, f0);
        if (! started) { p.startNewSubPath (x, y); started = true; } else p.lineTo (x, y);
    }
    if (! started) return;
    juce::Path filled (p);
    filled.lineTo (r.getRight(), r.getBottom());
    filled.lineTo (r.getX(), r.getBottom());
    filled.closeSubPath();
    if (! fill.isTransparent()) { g.setColour (fill); g.fillPath (filled); }
    g.setColour (line);
    g.strokePath (p, juce::PathStrokeType (1.0f));
}

juce::Point<float> EqGraph::handlePos (int h) const
{
    auto* st = Engine::get().getStrip (key);
    auto r = plotArea();
    if (st == nullptr) return {};
    const auto& s = st->eq;
    const double sr = sampleRateNow();
    if (h < 4) return { freqToX (s.band[h].freq, r), dbToY (s.band[h].on ? s.band[h].gain : 0.0, r) };
    const float f = h == 4 ? s.hpfFreq : s.lpfFreq;
    const bool on = h == 4 ? s.hpfOn : s.lpfOn;
    return { freqToX (f, r), dbToY (on ? cr::responseDb (s, f, sr, h) : 0.0, r) };
}

int EqGraph::hitHandle (juce::Point<float> p) const
{
    int best = -1;
    float bestD = 12.0f;
    for (int h = 0; h < 6; ++h)
    {
        const float d = handlePos (h).getDistanceFrom (p);
        if (d < bestD) { bestD = d; best = h; }
    }
    return best;
}

void EqGraph::paint (juce::Graphics& g)
{
    auto r = plotArea();
    g.setColour (Pal::bg);
    g.fillRect (getLocalBounds());
    auto* st = Engine::get().getStrip (key);

    // grid
    g.setFont (Fonts::ui (9.0f));
    for (double f : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
    {
        const float x = freqToX (f, r);
        g.setColour (Pal::outline.withAlpha (0.8f));
        g.fillRect (x, r.getY(), 1.0f, r.getHeight());
        g.setColour (Pal::textFaint);
        const auto label = f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f);
        g.drawText (label, juce::Rectangle<float> (x - 20.0f, r.getBottom() + 2.0f, 40.0f, 12.0f), juce::Justification::centred);
    }
    for (int db = -24; db <= 24; db += 6)
    {
        const float y = dbToY (db, r);
        g.setColour (db == 0 ? Pal::line : Pal::outline.withAlpha (0.6f));
        g.fillRect (r.getX(), y, r.getWidth(), 1.0f);
        g.setColour (Pal::textFaint);
        g.drawText ((db > 0 ? "+" : "") + juce::String (db), juce::Rectangle<float> (0.0f, y - 6.0f, 30.0f, 12.0f), juce::Justification::centredRight);
    }
    if (st == nullptr) return;
    const auto& s = st->eq;
    const double sr = sampleRateNow();

    // analyzer behind everything
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (r.toNearestInt());
        if (analyzerMode == analyzerPre || analyzerMode == analyzerBoth)
            drawSpectrum (g, r, preSmooth, analyzerMode == analyzerPre ? Pal::textFaint.withAlpha (0.18f) : juce::Colours::transparentBlack, Pal::textFaint.withAlpha (0.6f));
        if (analyzerMode == analyzerPost || analyzerMode == analyzerBoth)
        {
            drawSpectrum (g, r, postSmooth, Pal::info.withAlpha (0.14f), Pal::info.withAlpha (0.55f));
            drawSpectrum (g, r, postPeak, juce::Colours::transparentBlack, Pal::text.withAlpha (0.22f));   // peak hold
        }
    }

    auto curvePath = [&] (auto&& dbAt)
    {
        juce::Path p;
        for (float x = r.getX(); x <= r.getRight(); x += 1.5f)
        {
            const float y = dbToY (dbAt (xToFreq (x, r)), r);
            if (x == r.getX()) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        return p;
    };
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (r.toNearestInt().expanded (0, 1));

    // insert effects that shape the response (dashed)
    bool anyInsertCurve = false;
    if (! s.bypassInserts)
        for (auto& slot : st->inserts)
            if (slot.builtin && ! slot.bypass && slot.builtin->hasFrequencyResponse())
            {
                auto* fx = slot.builtin.get();
                auto p = curvePath ([fx, sr] (double f) { return fx->getResponseDb (f, sr); });
                juce::Path dashed;
                const float dashes[] = { 4.0f, 3.0f };
                juce::PathStrokeType (1.2f).createDashedStroke (dashed, p, dashes, 2);
                g.setColour (Pal::textDim.withAlpha (0.8f));
                g.fillPath (dashed);
                anyInsertCurve = true;
            }

    // each band, filled towards 0 dB
    const float zeroY = dbToY (0.0, r);
    for (int h = 0; h < 6; ++h)
    {
        const bool on = h < 4 ? s.band[h].on : (h == 4 ? s.hpfOn : s.lpfOn);
        const bool bypassed = h < 4 ? s.bypassEq : s.bypassFilters;
        if (! on || bypassed) continue;
        auto p = curvePath ([&s, sr, h] (double f) { return cr::responseDb (s, f, sr, h); });
        juce::Path area (p);
        area.lineTo (r.getRight(), zeroY);
        area.lineTo (r.getX(), zeroY);
        area.closeSubPath();
        const auto c = bandColour (h);
        g.setColour (c.withAlpha (h == selected ? 0.22f : 0.10f));
        g.fillPath (area);
        g.setColour (c.withAlpha (0.7f));
        g.strokePath (p, juce::PathStrokeType (1.0f));
    }

    // combined channel curve (plus inserts, if any)
    {
        auto p = curvePath ([&s, sr] (double f) { return cr::responseDb (s, f, sr); });
        g.setColour (Pal::text);
        g.strokePath (p, juce::PathStrokeType (2.0f));
        if (anyInsertCurve)
        {
            auto all = curvePath ([&s, st, sr] (double f)
            {
                double db = cr::responseDb (s, f, sr);
                for (auto& slot : st->inserts)
                    if (slot.builtin && ! slot.bypass && slot.builtin->hasFrequencyResponse()) db += slot.builtin->getResponseDb (f, sr);
                return db;
            });
            g.setColour (Pal::accent.withAlpha (0.8f));
            g.strokePath (all, juce::PathStrokeType (1.2f));
        }
    }

    // nodes
    for (int h = 0; h < 6; ++h)
    {
        const auto pt = handlePos (h);
        const bool on = h < 4 ? s.band[h].on : (h == 4 ? s.hpfOn : s.lpfOn);
        const auto c = bandColour (h);
        const float rad = (h == selected || h == hover || h == dragging) ? 8.0f : 6.5f;
        auto circle = juce::Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (pt);
        if (on) { g.setColour (c); g.fillEllipse (circle); }
        else { g.setColour (Pal::bg); g.fillEllipse (circle); }
        g.setColour (on ? Pal::bg : c);
        g.drawEllipse (circle, h == selected ? 2.0f : 1.4f);
        g.setFont (Fonts::heavy (8.0f));
        g.setColour (on ? Pal::bg : c);
        const juce::String label = h < 4 ? juce::String (h + 1) : (h == 4 ? "H" : "L");
        g.drawText (label, circle, juce::Justification::centred);
    }

    // readout for the node under the mouse
    const int show = dragging >= 0 ? dragging : hover;
    if (show >= 0)
    {
        juce::String t = juce::String (handleName (show)) + "  ";
        if (show < 4) t << fmtFreq (s.band[show].freq) << "  " << (s.band[show].gain > 0 ? "+" : "") << juce::String (s.band[show].gain, 1) << " dB  Q " << juce::String (s.band[show].q, 2);
        else t << fmtFreq (show == 4 ? s.hpfFreq : s.lpfFreq) << "  " << juce::String (12 << (show == 4 ? s.hpfSlope : s.lpfSlope)) << " dB/oct";
        g.setFont (Fonts::ui (11.0f, true));
        g.setColour (bandColour (show));
        g.drawText (t, r.reduced (8.0f, 4.0f), juce::Justification::topRight);
    }
}

void EqGraph::mouseMove (const juce::MouseEvent& e)
{
    const int h = hitHandle (e.position);
    if (h != hover) { hover = h; repaint(); }
    setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
}

void EqGraph::mouseDown (const juce::MouseEvent& e)
{
    auto* st = Engine::get().getStrip (key);
    if (st == nullptr) return;
    dragging = hitHandle (e.position);
    if (dragging < 0) return;
    selected = dragging;
    if (onSelect) onSelect (selected);
    if (e.mods.isPopupMenu())
    {
        const int h = dragging;
        dragging = -1;
        juce::PopupMenu m;
        auto& s = st->eq;
        const bool on = h < 4 ? s.band[h].on : (h == 4 ? s.hpfOn : s.lpfOn);
        m.addItem (1, on ? "Turn off" : "Turn on");
        if (h == 0 || h == 3) m.addItem (2, s.band[h].bell ? "Make it a shelf" : "Make it a bell");
        if (h >= 4) for (int k = 0; k < 3; ++k) m.addItem (10 + k, juce::String (12 << k) + " dB / octave", true, (h == 4 ? s.hpfSlope : s.lpfSlope) == k);
        m.addItem (3, "Reset");
        juce::Component::SafePointer<EqGraph> safe (this);
        m.showMenuAsync ({}, [safe, h] (int r)
        {
            if (safe == nullptr) return;
            auto* s2 = Engine::get().getStrip (safe->key);
            if (s2 == nullptr) return;
            auto& q = s2->eq;
            if (r == 1) { if (h < 4) q.band[h].on = ! q.band[h].on; else if (h == 4) q.hpfOn = ! q.hpfOn; else q.lpfOn = ! q.lpfOn; }
            else if (r == 2) q.band[h].bell = ! q.band[h].bell;
            else if (r >= 10) (h == 4 ? q.hpfSlope : q.lpfSlope) = r - 10;
            else if (r == 3) { cr::ChannelEqSettings d; if (h < 4) q.band[h] = d.band[h]; else if (h == 4) { q.hpfOn = false; q.hpfFreq = d.hpfFreq; } else { q.lpfOn = false; q.lpfFreq = d.lpfFreq; } }
            if (safe->onEdit) safe->onEdit();
            safe->repaint();
        });
        return;
    }
    // grabbing a node that's off switches it on
    auto& s = st->eq;
    if (dragging < 4) s.band[dragging].on = true;
    else if (dragging == 4) s.hpfOn = true;
    else s.lpfOn = true;
    if (onEdit) onEdit();
    repaint();
}

void EqGraph::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0) return;
    auto* st = Engine::get().getStrip (key);
    if (st == nullptr) return;
    auto& en = Engine::get();
    auto r = plotArea();
    const auto path = stripPath (key);
    double f = xToFreq (e.position.x, r);
    if (dragging < 4)
    {
        const auto rg = cr::bandRange (dragging);
        f = juce::jlimit ((double) rg.minF, (double) rg.maxF, f);
        const double db = juce::jlimit (-18.0, 18.0, (r.getCentreY() - e.position.y) / (r.getHeight() * 0.5) * 24.0);
        en.setParamValue (cr::ParamAddress::parse (path + "eq" + juce::String (dragging) + "f"), (float) f, true);
        en.setParamValue (cr::ParamAddress::parse (path + "eq" + juce::String (dragging) + "g"), (float) (std::round (db * 10.0) / 10.0), true);
    }
    else
    {
        const auto rg = dragging == 4 ? cr::hpfRange() : cr::lpfRange();
        f = juce::jlimit ((double) rg.minF, (double) rg.maxF, f);
        en.setParamValue (cr::ParamAddress::parse (path + (dragging == 4 ? "hpf" : "lpf")), (float) f, true);
    }
    if (onEdit) onEdit();
    repaint();
}

void EqGraph::mouseUp (const juce::MouseEvent&) { dragging = -1; repaint(); }

void EqGraph::mouseDoubleClick (const juce::MouseEvent& e)
{
    auto* st = Engine::get().getStrip (key);
    const int h = hitHandle (e.position);
    if (st == nullptr || h < 0) return;
    cr::ChannelEqSettings d;
    auto& q = st->eq;
    if (h < 4) { q.band[h].gain = 0.0f; q.band[h].freq = d.band[h].freq; q.band[h].q = d.band[h].q; }
    else if (h == 4) { q.hpfFreq = d.hpfFreq; q.hpfSlope = d.hpfSlope; }
    else { q.lpfFreq = d.lpfFreq; q.lpfSlope = d.lpfSlope; }
    if (onEdit) onEdit();
    repaint();
}

void EqGraph::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    auto* st = Engine::get().getStrip (key);
    if (st == nullptr) return;
    int h = hitHandle (e.position);
    if (h < 0) h = selected;
    auto& q = st->eq;
    const float delta = w.deltaY != 0.0f ? w.deltaY : w.deltaX;
    if (h < 4)
    {
        if (e.mods.isShiftDown())
            Engine::get().setParamValue (cr::ParamAddress::parse (stripPath (key) + "eq" + juce::String (h) + "g"), juce::jlimit (-18.0f, 18.0f, q.band[h].gain + delta * 3.0f), true);
        else
            Engine::get().setParamValue (cr::ParamAddress::parse (stripPath (key) + "eq" + juce::String (h) + "q"),
                                         juce::jlimit (0.1f, 10.0f, q.band[h].q * std::pow (2.0f, delta * 1.5f)), true);
    }
    else
    {
        auto& slope = h == 4 ? q.hpfSlope : q.lpfSlope;
        slope = juce::jlimit (0, 2, slope + (delta > 0 ? 1 : -1));
    }
    selected = h;
    if (onSelect) onSelect (h);
    if (onEdit) onEdit();
    repaint();
}

// ============================================================================
// Panel
// ============================================================================
ChannelEqPanel::ChannelEqPanel()
{
    addAndMakeVisible (graph);
    graph.onSelect = [this] (int) { repaint(); };
    graph.onEdit = [this] { refreshButtons(); Knob::refreshAll(); };
    for (auto* b : { &bypassInserts, &bypassEq, &bypassFilters })
    {
        b->setClickingTogglesState (true);
        b->setColour (juce::TextButton::buttonOnColourId, Pal::mute);
        b->setTooltip ("Bypass without muting");
        addAndMakeVisible (b);
    }
    bypassInserts.onClick = [this] { if (auto* st = Engine::get().getStrip (key)) st->eq.bypassInserts = bypassInserts.getToggleState(); graph.repaint(); };
    bypassEq.onClick      = [this] { if (auto* st = Engine::get().getStrip (key)) st->eq.bypassEq = bypassEq.getToggleState(); graph.repaint(); };
    bypassFilters.onClick = [this] { if (auto* st = Engine::get().getStrip (key)) st->eq.bypassFilters = bypassFilters.getToggleState(); graph.repaint(); };
    resetBtn.onClick = [this]
    {
        if (auto* st = Engine::get().getStrip (key))
        {
            const auto keepFront = st->eq;
            st->eq = {};
            st->eq.trimDb = keepFront.trimDb; st->eq.invert = keepFront.invert; st->eq.width = keepFront.width;
        }
        refreshButtons();
        Knob::refreshAll();
        graph.repaint();
    };
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    for (auto* b : { &resetBtn, &closeBtn }) addAndMakeVisible (b);
    analyzer.choices = { "Off", "Post-EQ", "Pre-EQ", "Pre + post" };
    analyzer.getIndex = [this] { return graph.getAnalyzerMode(); };
    analyzer.setIndex = [this] (int i) { graph.setAnalyzerMode (i); analyzer.repaint(); };
    addAndMakeVisible (analyzer);

    for (int c = 0; c < 6; ++c)
    {
        auto& col = cols[(size_t) c];
        const int h = handleForColumn (c);
        const auto bc = bandColour (h);
        col.power.accent = bc;
        col.power.onClick = [this, h, c]
        {
            auto* st = Engine::get().getStrip (key);
            if (st == nullptr) return;
            const bool on = cols[(size_t) c].power.getToggleState();
            if (h < 4) st->eq.band[h].on = on; else if (h == 4) st->eq.hpfOn = on; else st->eq.lpfOn = on;
            graph.repaint();
        };
        addAndMakeVisible (col.power);
        for (auto* k : { &col.freq, &col.gain, &col.q }) { k->setCompact (true); k->setAccent (bc); k->onUserChange = [this] { graph.repaint(); }; }
        addAndMakeVisible (col.freq);
        if (h < 4)
        {
            addAndMakeVisible (col.gain);
            addAndMakeVisible (col.q);
        }
        if (h == 0 || h == 3)
        {
            col.shape.setClickingTogglesState (true);
            col.shape.onClick = [this, h, c]
            {
                if (auto* st = Engine::get().getStrip (key)) st->eq.band[h].bell = cols[(size_t) c].shape.getToggleState();
                refreshButtons();
                graph.repaint();
            };
            addAndMakeVisible (col.shape);
        }
        if (h >= 4)
        {
            col.slope.choices = { "12 dB", "24 dB", "48 dB" };
            col.slope.accent = bc;
            col.slope.getIndex = [this, h] { auto* st = Engine::get().getStrip (key); return st ? (h == 4 ? st->eq.hpfSlope : st->eq.lpfSlope) : 1; };
            col.slope.setIndex = [this, h, c] (int i) { if (auto* st = Engine::get().getStrip (key)) (h == 4 ? st->eq.hpfSlope : st->eq.lpfSlope) = i; cols[(size_t) c].slope.repaint(); graph.repaint(); };
            addAndMakeVisible (col.slope);
        }
    }
    startTimerHz (10);
}

void ChannelEqPanel::setKey (int k, const juce::String& n, juce::Colour c)
{
    const bool changed = k != key;
    key = k; name = n; colour = c;
    if (changed) graph.setKey (k);
    const auto path = stripPath (k);
    for (int ci = 0; ci < 6; ++ci)
    {
        auto& col = cols[(size_t) ci];
        const int h = handleForColumn (ci);
        if (h < 4)
        {
            const auto b = juce::String (h);
            col.freq.setParam (path + "eq" + b + "f", "Freq");
            col.gain.setParam (path + "eq" + b + "g", "Gain");
            col.q.setParam (path + "eq" + b + "q", "Q");
        }
        else col.freq.setParam (path + (h == 4 ? "hpf" : "lpf"), "Freq");
    }
    refreshButtons();
    repaint();
}

void ChannelEqPanel::refreshButtons()
{
    auto* st = Engine::get().getStrip (key);
    if (st == nullptr) return;
    const auto& s = st->eq;
    bypassInserts.setToggleState (s.bypassInserts, juce::dontSendNotification);
    bypassEq.setToggleState (s.bypassEq, juce::dontSendNotification);
    bypassFilters.setToggleState (s.bypassFilters, juce::dontSendNotification);
    for (int c = 0; c < 6; ++c)
    {
        auto& col = cols[(size_t) c];
        const int h = handleForColumn (c);
        col.power.setToggleState (h < 4 ? s.band[h].on : (h == 4 ? s.hpfOn : s.lpfOn), juce::dontSendNotification);
        if (h == 0 || h == 3)
        {
            col.shape.setToggleState (s.band[h].bell, juce::dontSendNotification);
            col.shape.setButtonText (s.band[h].bell ? "Bell" : "Shelf");
        }
        col.slope.repaint();
    }
}

void ChannelEqPanel::timerCallback() { refreshButtons(); }

void ChannelEqPanel::paint (juce::Graphics& g)
{
    g.fillAll (Pal::panel);
    g.setColour (Pal::outline);
    g.fillRect (0, 0, getWidth(), 1);
    auto h = header;
    auto badge = h.removeFromLeft (6).toFloat();
    g.setColour (colour);
    g.fillRect (badge.withTrimmedTop (6.0f).withTrimmedBottom (6.0f));
    h.removeFromLeft (8);
    g.setColour (Pal::text);
    g.setFont (Fonts::ui (13.0f, true));
    g.drawText ("Channel EQ  -  " + name, h.removeFromLeft (360), juce::Justification::centredLeft);
    for (int c = 0; c < 6; ++c)
    {
        auto& col = cols[(size_t) c];
        const int hd = handleForColumn (c);
        auto a = col.area;
        g.setColour (graph.selected == hd ? Pal::surface2 : Pal::surface);
        g.fillRoundedRectangle (a.toFloat(), 4.0f);
        g.setColour (bandColour (hd));
        g.fillRect (a.getX() + 4, a.getY(), a.getWidth() - 8, 2);
        drawCaps (g, handleName (hd), a.withHeight (24).withTrimmedLeft (28), juce::Justification::centredLeft, bandColour (hd), 10.0f);
    }
}

void ChannelEqPanel::resized()
{
    auto r = getLocalBounds().reduced (10, 6);
    header = r.removeFromTop (30);
    auto hb = header;
    closeBtn.setBounds (hb.removeFromRight (64).reduced (0, 2));
    hb.removeFromRight (6);
    resetBtn.setBounds (hb.removeFromRight (64).reduced (0, 2));
    hb.removeFromRight (12);
    analyzer.setBounds (hb.removeFromRight (150));
    hb.removeFromRight (12);
    bypassFilters.setBounds (hb.removeFromRight (70).reduced (0, 2));
    hb.removeFromRight (4);
    bypassEq.setBounds (hb.removeFromRight (50).reduced (0, 2));
    hb.removeFromRight (4);
    bypassInserts.setBounds (hb.removeFromRight (70).reduced (0, 2));
    r.removeFromTop (4);

    auto controls = r.removeFromBottom (92);
    r.removeFromBottom (6);
    graph.setBounds (r);
    // column widths: filters narrower than bands
    const int total = controls.getWidth();
    const int unit = total / 16;   // filters 2 units, bands 3 units
    for (int c = 0; c < 6; ++c)
    {
        auto& col = cols[(size_t) c];
        const int h = handleForColumn (c);
        const int w = h >= 4 ? unit * 2 : unit * 3;
        col.area = controls.removeFromLeft (c == 5 ? controls.getWidth() : w).reduced (3, 0);
        auto a = col.area.reduced (4, 2);
        auto top = a.removeFromTop (22);
        col.power.setBounds (top.removeFromLeft (20).withSizeKeepingCentre (18, 18));
        if (h == 0 || h == 3) col.shape.setBounds (top.removeFromRight (52).reduced (0, 1));
        if (h < 4)
        {
            const int kw = a.getWidth() / 3;
            col.freq.setBounds (a.removeFromLeft (kw));
            col.gain.setBounds (a.removeFromLeft (kw));
            col.q.setBounds (a);
        }
        else
        {
            const int kw = a.getWidth() / 2;
            col.freq.setBounds (a.removeFromLeft (kw));
            col.slope.setBounds (a.withSizeKeepingCentre (a.getWidth(), juce::jmin (a.getHeight(), 44)));
        }
    }
}
