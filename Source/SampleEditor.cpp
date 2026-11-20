// Waveform view, the full-screen Audio Editor, auto-slice and the slice "Save" dialog.
#include "SampleEditor.h"

// ============================================================================
// WaveformView
// ============================================================================
WaveformView::WaveformView() { setMouseCursor (juce::MouseCursor::NormalCursor); }

void WaveformView::setTarget (int b, int p, int l, int z)
{
    if (b != bank || p != pad || l != layer || z != zone) { viewStart = 0.0; viewEnd = 1.0; selStart = selEnd = 0.0f; }
    bank = b; pad = p; layer = l; zone = z;
    repaint();
}

cr::Zone* WaveformView::getZone() const
{
    auto& padRef = Engine::get().session().banks[(size_t) bank].pads[(size_t) pad];
    if (! juce::isPositiveAndBelow (layer, (int) padRef.layers.size())) return nullptr;
    auto& l = padRef.layers[(size_t) layer];
    if (! juce::isPositiveAndBelow (zone, (int) l.zones.size())) return nullptr;
    return &l.zones[(size_t) zone];
}

cr::SamplePtr WaveformView::getSample() const
{
    if (auto* z = getZone()) return z->sample;
    return nullptr;
}

float WaveformView::xToPos (float x) const
{
    auto r = waveArea();
    return (float) juce::jlimit (0.0, 1.0, viewStart + (viewEnd - viewStart) * (x - r.getX()) / juce::jmax (1, r.getWidth()));
}

float WaveformView::posToX (float pos) const
{
    auto r = waveArea();
    return (float) r.getX() + (float) ((pos - viewStart) / juce::jmax (1.0e-9, viewEnd - viewStart)) * (float) r.getWidth();
}

float WaveformView::snap (float pos) const
{
    if (! zeroSnap) return pos;
    if (auto s = getSample()) return Engine::get().snapToZeroCrossing (*s, pos);
    return pos;
}

void WaveformView::zoom (double factor, double centre)
{
    if (centre < 0.0) centre = (viewStart + viewEnd) * 0.5;
    double span = juce::jlimit (0.0005, 1.0, (viewEnd - viewStart) / factor);
    viewStart = juce::jlimit (0.0, 1.0 - span, centre - span * 0.5);
    viewEnd = viewStart + span;
    repaint();
}

void WaveformView::scroll (double fraction)
{
    const double span = viewEnd - viewStart;
    viewStart = juce::jlimit (0.0, 1.0 - span, viewStart + span * fraction);
    viewEnd = viewStart + span;
    repaint();
}

void WaveformView::zoomToSelection()
{
    if (selEnd - selStart > 0.0001f) { viewStart = selStart; viewEnd = selEnd; }
    else if (auto* z = getZone()) { viewStart = z->start; viewEnd = juce::jmax (z->start + 0.001f, z->end); }
    repaint();
}

juce::String WaveformView::getZoomText() const
{
    const double z = 1.0 / juce::jmax (1.0e-6, viewEnd - viewStart);
    return z < 1.05 ? juce::String ("FIT") : "x" + juce::String (z, z < 10 ? 1 : 0);
}

void WaveformView::buildPeaks()
{
    auto s = getSample();
    auto r = waveArea();
    if (! s) { peakMin.clear(); peakMax.clear(); peakSample = nullptr; return; }
    if (s.get() == peakSample && peakStart == viewStart && peakEnd == viewEnd && peakWidth == r.getWidth()) return;
    peakSample = s.get(); peakStart = viewStart; peakEnd = viewEnd; peakWidth = r.getWidth();
    const int w = juce::jmax (1, r.getWidth());
    peakMin.assign ((size_t) w, 0.0f);
    peakMax.assign ((size_t) w, 0.0f);

    if (s->isStreamed())
    {
        if (overviewSample != s.get())
        {
            overviewSample = s.get();
            const int n = 8192;
            overviewMin.assign (n, 0.0f);
            overviewMax.assign (n, 0.0f);
            juce::AudioBuffer<float> tmp (2, 4096);
            for (int i = 0; i < n; ++i)
            {
                const juce::int64 a = s->numFrames * i / n;
                const int len = (int) juce::jmin ((juce::int64) 4096, juce::jmax ((juce::int64) 1, s->numFrames * (i + 1) / n - a));
                s->mapped->read (&tmp, 0, len, a, true, true);
                auto range = tmp.findMinMax (0, 0, len);
                overviewMin[(size_t) i] = range.getStart();
                overviewMax[(size_t) i] = range.getEnd();
            }
        }
        for (int x = 0; x < w; ++x)
        {
            const int a = (int) ((viewStart + (viewEnd - viewStart) * x / w) * 8191);
            const int b = juce::jmax (a + 1, (int) ((viewStart + (viewEnd - viewStart) * (x + 1) / w) * 8191));
            float mn = 0.0f, mx = 0.0f;
            for (int i = a; i < b && i < 8192; ++i) { mn = juce::jmin (mn, overviewMin[(size_t) i]); mx = juce::jmax (mx, overviewMax[(size_t) i]); }
            peakMin[(size_t) x] = mn; peakMax[(size_t) x] = mx;
        }
        return;
    }
    const auto& buf = s->buffer;
    const int len = buf.getNumSamples();
    for (int x = 0; x < w; ++x)
    {
        const int a = (int) ((viewStart + (viewEnd - viewStart) * x / w) * len);
        const int b = juce::jmin (len, juce::jmax (a + 1, (int) ((viewStart + (viewEnd - viewStart) * (x + 1) / w) * len)));
        if (a >= len) break;
        const int step = juce::jmax (1, (b - a) / 256);
        float mn = 1.0f, mx = -1.0f;
        const float* l = buf.getReadPointer (0);
        const float* rr = buf.getReadPointer (1);
        for (int i = a; i < b; i += step)
        {
            const float v = (l[i] + rr[i]) * 0.5f;
            mn = juce::jmin (mn, v); mx = juce::jmax (mx, v);
        }
        peakMin[(size_t) x] = mn; peakMax[(size_t) x] = mx;
    }
}

void WaveformView::paint (juce::Graphics& g)
{
    auto r = waveArea();
    auto ruler = rulerArea();
    g.setColour (Pal::bg);
    g.fillRect (getLocalBounds());
    g.setColour (Pal::panel);
    g.fillRect (ruler);
    auto s = getSample();
    auto* z = getZone();
    if (! s || z == nullptr)
    {
        g.setColour (Pal::textFaint);
        g.setFont (Fonts::ui (14.0f));
        g.drawText ("Drop a sample here, load one from the Browser, or press REC to sample", r, juce::Justification::centred);
        return;
    }

    // time grid on the ruler (seconds)
    const double total = s->lengthSeconds();
    const double visible = total * (viewEnd - viewStart);
    double step = 0.001;
    for (double c : { 0.001, 0.005, 0.01, 0.05, 0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 30.0, 60.0 }) if (visible / c < 12.0) { step = c; break; }
    g.setFont (Fonts::ui (9.5f));
    for (double t = std::ceil (viewStart * total / step) * step; t < viewEnd * total; t += step)
    {
        const float x = posToX ((float) (t / total));
        g.setColour (Pal::outline);
        g.fillRect (x, (float) r.getY(), 1.0f, (float) r.getHeight());
        g.setColour (Pal::textFaint);
        g.drawText (step < 1.0 ? juce::String (t, step < 0.01 ? 3 : 2) : juce::String ((int) t) + "s", (int) x + 3, ruler.getY(), 60, ruler.getHeight(), juce::Justification::centredLeft);
    }

    // played region
    const float xs = posToX (z->start), xe = posToX (z->end);
    g.setColour (accent.withAlpha (0.06f));
    g.fillRect (juce::Rectangle<float> (xs, (float) r.getY(), xe - xs, (float) r.getHeight()));

    // loop region
    auto& padRef = Engine::get().session().banks[(size_t) bank].pads[(size_t) pad];
    const bool looping = juce::isPositiveAndBelow (layer, (int) padRef.layers.size()) && padRef.layers[(size_t) layer].p.loop != cr::LoopMode::off;
    if (looping && mode == Mode::markers)
    {
        const float a = posToX (z->loopStart), b = posToX (z->loopEnd);
        g.setColour (Pal::info.withAlpha (0.12f));
        g.fillRect (juce::Rectangle<float> (a, (float) r.getY(), b - a, (float) r.getHeight()));
    }

    // selection
    if (mode == Mode::select && selEnd - selStart > 0.00001f)
    {
        const float a = posToX (selStart), b = posToX (selEnd);
        g.setColour (Pal::text.withAlpha (0.14f));
        g.fillRect (juce::Rectangle<float> (a, (float) r.getY(), b - a, (float) r.getHeight()));
    }

    // waveform
    buildPeaks();
    const float mid = (float) r.getCentreY();
    const float amp = (float) r.getHeight() * 0.46f;
    juce::Path wave;
    for (int x = 0; x < (int) peakMax.size(); ++x)
    {
        const float pos = (float) (viewStart + (viewEnd - viewStart) * x / juce::jmax (1, (int) peakMax.size()));
        const bool inside = pos >= z->start && pos <= z->end;
        g.setColour (inside ? accent : accent.withAlpha (0.35f).withSaturation (0.3f));
        const float top = mid - peakMax[(size_t) x] * amp, bot = mid - peakMin[(size_t) x] * amp;
        g.fillRect ((float) (r.getX() + x), top, 1.0f, juce::jmax (1.0f, bot - top));
    }
    g.setColour (Pal::line.withAlpha (0.5f));
    g.fillRect ((float) r.getX(), mid, (float) r.getWidth(), 1.0f);

    auto flag = [&] (float pos, juce::Colour c, const juce::String& label, bool atTop)
    {
        const float x = posToX (pos);
        if (x < (float) r.getX() - 1.0f || x > (float) r.getRight() + 1.0f) return;
        g.setColour (c);
        g.fillRect (x - 0.5f, (float) r.getY(), 1.5f, (float) r.getHeight());
        auto f = juce::Rectangle<float> (x, atTop ? (float) r.getY() : (float) r.getBottom() - 14.0f, 14.0f, 14.0f);
        g.fillRect (f);
        g.setColour (Pal::bg);
        g.setFont (Fonts::ui (9.5f, true));
        g.drawText (label, f, juce::Justification::centred);
    };

    if (mode == Mode::slices || ! z->slices.empty())
    {
        for (size_t i = 0; i < z->slices.size(); ++i)
        {
            const float x = posToX (z->slices[i]);
            if (x < (float) r.getX() || x > (float) r.getRight()) continue;
            g.setColour (mode == Mode::slices ? Pal::text : Pal::textFaint);
            g.fillRect (x, (float) ruler.getY(), 1.0f, (float) getHeight());
            if (mode == Mode::slices)
            {
                g.setColour (Pal::text);
                g.fillRect (x, (float) ruler.getY(), 16.0f, (float) ruler.getHeight());
                g.setColour (Pal::bg);
                g.setFont (Fonts::ui (9.5f, true));
                g.drawText (juce::String ((int) i + 1), juce::Rectangle<float> (x, (float) ruler.getY(), 16.0f, (float) ruler.getHeight()), juce::Justification::centred);
            }
        }
        if (mode == Mode::slices)
            drawCaps (g, "Click the ruler to add a slice, drag to move, right-click to remove", ruler.withTrimmedLeft (8).withTrimmedRight (8), juce::Justification::centredRight, Pal::textFaint, 9.0f);
    }
    if (mode == Mode::markers)
    {
        if (looping) { flag (z->loopStart, Pal::info, "L", false); flag (z->loopEnd, Pal::info, "L", false); }
        flag (z->start, Pal::play, "S", true);
        flag (z->end, Pal::rec, "E", true);
    }

    // preview playhead
    auto& e = Engine::get();
    if (e.isPreviewing())
    {
        const float x = posToX (e.getPreviewPosition());
        g.setColour (Pal::text);
        g.fillRect (x, (float) r.getY(), 1.0f, (float) r.getHeight());
    }
}

int WaveformView::hitMarker (int x, int y) const
{
    auto* z = getZone();
    if (z == nullptr) return -1;
    auto near = [&] (float pos) { return std::abs (posToX (pos) - (float) x) < 6.0f; };
    if (mode == Mode::slices)
    {
        for (size_t i = 0; i < z->slices.size(); ++i) if (near (z->slices[i])) return 100 + (int) i;
        return -1;
    }
    if (mode != Mode::markers) return -1;
    auto& padRef = Engine::get().session().banks[(size_t) bank].pads[(size_t) pad];
    const bool looping = juce::isPositiveAndBelow (layer, (int) padRef.layers.size()) && padRef.layers[(size_t) layer].p.loop != cr::LoopMode::off;
    const bool bottomHalf = y > getHeight() / 2;
    if (looping && bottomHalf)
    {
        if (near (z->loopStart)) return 2;
        if (near (z->loopEnd)) return 3;
    }
    if (near (z->start)) return 0;
    if (near (z->end)) return 1;
    if (looping)
    {
        if (near (z->loopStart)) return 2;
        if (near (z->loopEnd)) return 3;
    }
    return -1;
}

void WaveformView::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (hitMarker (e.x, e.y) >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    auto* z = getZone();
    auto& eng = Engine::get();
    if (z == nullptr) return;
    dragging = hitMarker (e.x, e.y);
    const float pos = xToPos ((float) e.x);
    if (mode == Mode::slices)
    {
        if (e.mods.isPopupMenu())
        {
            if (dragging >= 100)
            {
                const juce::ScopedLock sl (eng.getLock());
                z->slices.erase (z->slices.begin() + (dragging - 100));
                dragging = -1;
                if (onChange) onChange();
                repaint();
            }
            return;
        }
        if (dragging < 0 && rulerArea().contains (e.getPosition()))
        {
            const juce::ScopedLock sl (eng.getLock());
            z->slices.push_back (snap (pos));
            std::sort (z->slices.begin(), z->slices.end());
            for (size_t i = 0; i < z->slices.size(); ++i) if (std::abs (z->slices[i] - snap (pos)) < 1.0e-6f) dragging = 100 + (int) i;
            if (onChange) onChange();
            repaint();
            return;
        }
        if (dragging < 0)
        {
            // audition the slice under the mouse
            float a = z->start, b = z->end;
            for (size_t i = 0; i < z->slices.size(); ++i)
                if (z->slices[i] <= pos) { a = z->slices[i]; b = i + 1 < z->slices.size() ? z->slices[i + 1] : z->end; }
            eng.previewBuffer (z->sample, a, b);
        }
        return;
    }
    if (mode == Mode::select && dragging < 0)
    {
        dragAnchor = snap (pos);
        selStart = selEnd = dragAnchor;
        repaint();
        return;
    }
    if (dragging < 0 && ! e.mods.isPopupMenu())
        eng.previewBuffer (z->sample, pos, z->end);
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    auto* z = getZone();
    if (z == nullptr) return;
    auto& eng = Engine::get();
    const float pos = snap (xToPos ((float) e.x));
    if (mode == Mode::select && dragging < 0)
    {
        selStart = juce::jmin (dragAnchor, pos);
        selEnd = juce::jmax (dragAnchor, pos);
        repaint();
        return;
    }
    if (dragging < 0) return;
    {
        const juce::ScopedLock sl (eng.getLock());
        if (dragging == 0) z->start = juce::jlimit (0.0f, z->end - 0.0005f, pos);
        else if (dragging == 1) z->end = juce::jlimit (z->start + 0.0005f, 1.0f, pos);
        else if (dragging == 2) z->loopStart = juce::jlimit (z->start, z->loopEnd - 0.0005f, pos);
        else if (dragging == 3) z->loopEnd = juce::jlimit (z->loopStart + 0.0005f, z->end, pos);
        else if (dragging >= 100 && dragging - 100 < (int) z->slices.size())
        {
            const int i = dragging - 100;
            const float lo = i > 0 ? z->slices[(size_t) i - 1] + 0.0005f : 0.0f;
            const float hi = i + 1 < (int) z->slices.size() ? z->slices[(size_t) i + 1] - 0.0005f : 1.0f;
            z->slices[(size_t) i] = juce::jlimit (lo, hi, pos);
        }
        if (z->loopStart < z->start) z->loopStart = z->start;
        if (z->loopEnd > z->end) z->loopEnd = z->end;
    }
    if (onChange) onChange();
    repaint();
}

void WaveformView::mouseUp (const juce::MouseEvent&)
{
    dragging = -1;
    if (mode == Mode::markers) Engine::get().stopPreview();
}

void WaveformView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (mode == Mode::select)
    {
        if (auto* z = getZone()) { selStart = z->start; selEnd = z->end; }
        repaint();
    }
    else zoom (2.0, xToPos ((float) e.x));
}

void WaveformView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown()) zoom (w.deltaY > 0 ? 1.25 : 0.8, xToPos ((float) e.x));
    else scroll ((w.deltaX != 0.0f ? -w.deltaX : -w.deltaY) * 0.5);
}

// ============================================================================
// Audio Editor (full window): selection tools and processing
// ============================================================================
namespace
{
class AudioEditor : public juce::Component, private juce::Timer
{
public:
    AudioEditor (int b, int p, int l, int z) : bank (b), pad (p), layer (l), zone (z)
    {
        wave.setTarget (b, p, l, z);
        wave.setMode (WaveformView::Mode::select);
        wave.setAccent (padColour (b, p));
        wave.zeroSnap = Engine::get().behaviours.sliceAutoSnap;
        addAndMakeVisible (wave);
        modeTabs.setTabs ({ "Select", "Start / End / Loop" });
        modeTabs.onChange = [this] (int t) { wave.setMode (t == 0 ? WaveformView::Mode::select : WaveformView::Mode::markers); updateButtons(); };
        addAndMakeVisible (modeTabs);
        snapBtn.setClickingTogglesState (true);
        snapBtn.setToggleState (wave.zeroSnap, juce::dontSendNotification);
        snapBtn.onClick = [this] { wave.zeroSnap = snapBtn.getToggleState(); };

        auto add = [this] (juce::TextButton& b, std::function<void()> f) { b.onClick = std::move (f); addAndMakeVisible (b); };
        add (snapBtn, [this] { wave.zeroSnap = snapBtn.getToggleState(); });
        add (allBtn, [this] { if (auto* z = wave.getZone()) { wave.selStart = 0.0f; wave.selEnd = 1.0f; juce::ignoreUnused (z); } wave.repaint(); updateButtons(); });
        add (playBtn, [this]
        {
            auto& e = Engine::get();
            if (e.isPreviewing()) { e.stopPreview(); return; }
            auto* z = wave.getZone();
            if (z == nullptr) return;
            const bool sel = wave.selEnd - wave.selStart > 0.0001f;
            e.previewBuffer (z->sample, sel ? wave.selStart : z->start, sel ? wave.selEnd : z->end);
        });
        add (cutBtn, [this] { apply (Engine::SampleEdit::cut); });
        add (copyBtn, [this] { apply (Engine::SampleEdit::copy); Nav::setStatus ("Copied"); updateButtons(); });
        add (pasteBtn, [this] { apply (Engine::SampleEdit::paste); });
        add (deleteBtn, [this] { apply (Engine::SampleEdit::deleteRange); });
        add (trimBtn, [this] { apply (Engine::SampleEdit::trim); });
        add (processBtn, [this] { showProcessMenu(); });
        add (undoBtn, [this] { if (Engine::get().undo()) { wave.setTarget (bank, pad, layer, zone); ViewBase::broadcast(); } });
        add (doneBtn, [] { Engine::get().stopPreview(); Nav::closeOverlay(); });
        doneBtn.setColour (juce::TextButton::buttonColourId, Pal::accent);
        doneBtn.setColour (juce::TextButton::textColourOffId, Pal::bg);
        add (setStartEndBtn, [this]
        {
            auto* z = wave.getZone();
            if (z == nullptr || wave.selEnd - wave.selStart < 0.0001f) return;
            const juce::ScopedLock sl (Engine::get().getLock());
            z->start = wave.selStart; z->end = wave.selEnd;
            z->loopStart = juce::jmax (z->loopStart, z->start);
            z->loopEnd = juce::jmin (z->loopEnd, z->end);
            wave.repaint();
        });
        add (setLoopBtn, [this]
        {
            auto* z = wave.getZone();
            if (z == nullptr || wave.selEnd - wave.selStart < 0.0001f) return;
            auto& e = Engine::get();
            const juce::ScopedLock sl (e.getLock());
            z->loopStart = juce::jmax (z->start, wave.selStart); z->loopEnd = juce::jmin (z->end, wave.selEnd);
            auto& l = e.session().banks[(size_t) bank].pads[(size_t) pad].layers[(size_t) layer];
            if (l.p.loop == cr::LoopMode::off) l.p.loop = cr::LoopMode::forward;
            wave.repaint();
        });
        zoomIn.onClick = [this] { wave.zoom (2.0, wave.selEnd > wave.selStart ? (wave.selStart + wave.selEnd) * 0.5 : -1.0); };
        zoomOut.onClick = [this] { wave.zoom (0.5); };
        zoomSel.onClick = [this] { wave.zoomToSelection(); };
        zoomFit.onClick = [this] { wave.zoomToFit(); };
        for (auto* b : { &zoomIn, &zoomOut, &zoomSel, &zoomFit }) addAndMakeVisible (b);
        updateButtons();
        startTimerHz (20);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Pal::bg);
        g.setColour (Pal::panel);
        g.fillRect (getLocalBounds().removeFromTop (54));
        g.fillRect (getLocalBounds().removeFromBottom (60));
        auto& e = Engine::get();
        auto& p = e.session().banks[(size_t) bank].pads[(size_t) pad];
        auto title = getLocalBounds().removeFromTop (54).reduced (16, 0);
        g.setColour (Pal::text);
        g.setFont (Fonts::ui (16.0f, true));
        juce::String name = "Audio Editor";
        if (auto s = wave.getSample()) name << "  -  " << s->name;
        g.drawText (name, title.removeFromLeft (520), juce::Justification::centredLeft);
        drawCaps (g, cr::bankLetter (bank) + "." + juce::String (pad + 1) + "  " + p.name, title.removeFromLeft (220), juce::Justification::centredLeft, Pal::textDim, 10.5f);

        // selection info
        if (auto s = wave.getSample())
        {
            const double len = s->lengthSeconds();
            juce::String info;
            if (wave.selEnd - wave.selStart > 0.00001f)
                info << "SELECTION  " << juce::String (wave.selStart * len, 3) << " s  -  " << juce::String (wave.selEnd * len, 3)
                     << " s   (" << juce::String ((wave.selEnd - wave.selStart) * len, 3) << " s)";
            else info << "LENGTH  " << juce::String (len, 3) << " s   " << juce::String ((int) s->sampleRate) << " Hz";
            drawCaps (g, info, getLocalBounds().removeFromBottom (60).reduced (16, 0).removeFromRight (420), juce::Justification::centredRight, Pal::textDim, 10.0f);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (54).reduced (16, 11);
        doneBtn.setBounds (top.removeFromRight (90));
        top.removeFromRight (12);
        undoBtn.setBounds (top.removeFromRight (70));
        top.removeFromRight (12);
        for (auto* b : { &zoomFit, &zoomSel, &zoomOut, &zoomIn }) { b->setBounds (top.removeFromRight (32)); top.removeFromRight (2); }
        auto bottom = r.removeFromBottom (60).reduced (16, 13);
        modeTabs.setBounds (bottom.removeFromLeft (300));
        bottom.removeFromLeft (16);
        for (auto* b : { &snapBtn, &allBtn, &playBtn }) { b->setBounds (bottom.removeFromLeft (b == &allBtn ? 80 : 92)); bottom.removeFromLeft (6); }
        bottom.removeFromLeft (14);
        for (auto* b : { &cutBtn, &copyBtn, &pasteBtn, &deleteBtn, &trimBtn, &processBtn, &setStartEndBtn, &setLoopBtn })
        {
            b->setBounds (bottom.removeFromLeft (b == &processBtn ? 96 : (b == &setStartEndBtn || b == &setLoopBtn ? 116 : 70)));
            bottom.removeFromLeft (4);
        }
        wave.setBounds (r.reduced (16, 12));
    }

private:
    void timerCallback() override { updateButtons(); if (Engine::get().isPreviewing()) wave.repaint(); }

    void updateButtons()
    {
        const bool sel = wave.selEnd - wave.selStart > 0.0001f;
        cutBtn.setEnabled (sel);
        deleteBtn.setEnabled (sel);
        trimBtn.setEnabled (sel);
        setStartEndBtn.setEnabled (sel);
        setLoopBtn.setEnabled (sel);
        pasteBtn.setEnabled (Engine::get().hasAudioClipboard());
        undoBtn.setEnabled (Engine::get().canUndo());
        playBtn.setButtonText (Engine::get().isPreviewing() ? "Stop" : "Play");
    }

    void apply (Engine::SampleEdit edit, double amount = 0.0)
    {
        auto& e = Engine::get();
        const float a = wave.selStart, b = wave.selEnd;
        if (! e.editSample (bank, pad, layer, zone, edit, a, b, amount))
        {
            if (edit != Engine::SampleEdit::copy) Nav::setStatus ("Select a region first");
            return;
        }
        if (edit == Engine::SampleEdit::trim || edit == Engine::SampleEdit::cut || edit == Engine::SampleEdit::deleteRange)
        {
            wave.selStart = wave.selEnd = edit == Engine::SampleEdit::trim ? 0.0f : a;
            wave.zoomToFit();
        }
        wave.setTarget (bank, pad, layer, zone);
        wave.repaint();
        updateButtons();
        ViewBase::broadcast();
    }

    void showProcessMenu()
    {
        juce::PopupMenu m;
        const bool sel = wave.selEnd - wave.selStart > 0.0001f;
        m.addSectionHeader (sel ? "Process the selection" : "Process the whole sample");
        m.addItem (1, "Normalize");
        m.addItem (2, "Gain...");
        m.addItem (3, "Fade in");
        m.addItem (4, "Fade out");
        m.addItem (5, "Reverse");
        m.addItem (6, "Silence");
        m.addItem (7, "Remove DC offset");
        m.addSeparator();
        m.addItem (8, "Time-stretch (keep pitch)...");
        m.addItem (9, "Pitch-shift (keep length)...");
        m.addItem (10, "Stretch to tempo...");
        m.addSeparator();
        m.addItem (11, "Save a copy of this sample in the library");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&processBtn), [this] (int r)
        {
            using E = Engine::SampleEdit;
            switch (r)
            {
                case 1: apply (E::normalize); break;
                case 2: askForNumber ("Gain (dB)", 3.0, [this] (double v) { apply (E::gain, v); }); break;
                case 3: apply (E::fadeIn); break;
                case 4: apply (E::fadeOut); break;
                case 5: apply (E::reverse); break;
                case 6: apply (E::silence); break;
                case 7: apply (E::dcRemove); break;
                case 8: askForNumber ("New length (% of the original)", 100.0, [this] (double v) { if (v > 5.0) apply (E::stretch, v / 100.0); }); break;
                case 9: askForNumber ("Pitch shift (semitones)", 0.0, [this] (double v) { apply (E::pitch, v); }); break;
                case 10:
                {
                    auto& e = Engine::get();
                    auto& l = e.session().banks[(size_t) bank].pads[(size_t) pad].layers[(size_t) layer];
                    const double from = l.p.sampleBpm;
                    askForNumber ("Stretch from " + juce::String (from, 2) + " BPM to (BPM)", e.getBpm(), [this, from] (double to)
                    {
                        if (to < 20.0) return;
                        apply (E::stretch, from / to);
                        auto& en = Engine::get();
                        en.session().banks[(size_t) bank].pads[(size_t) pad].layers[(size_t) layer].p.sampleBpm = (float) to;
                    });
                    break;
                }
                case 11:
                    if (Engine::get().duplicateSampleFile (bank, pad, layer, zone)) Nav::setStatus ("Saved to Library > Samples");
                    break;
                default: break;
            }
        });
    }

    int bank, pad, layer, zone;
    WaveformView wave;
    TabStrip modeTabs;
    juce::TextButton snapBtn { "Zero snap" }, allBtn { "All" }, playBtn { "Play" }, cutBtn { "Cut" }, copyBtn { "Copy" }, pasteBtn { "Paste" },
                     deleteBtn { "Delete" }, trimBtn { "Trim" }, processBtn { "Process" }, undoBtn { "Undo" }, doneBtn { "Done" },
                     setStartEndBtn { "Set start/end" }, setLoopBtn { "Set loop" };
    IconButton zoomIn { Icon::zoomIn, "Zoom in" }, zoomOut { Icon::zoomOut, "Zoom out" }, zoomSel { Icon::select, "Zoom to selection" }, zoomFit { Icon::grid, "Show all" };
};
} // namespace

std::unique_ptr<juce::Component> createAudioEditor (int bank, int pad, int layer, int zone)
{
    return std::make_unique<AudioEditor> (bank, pad, layer, zone);
}

// ============================================================================
// Auto-slice
// ============================================================================
void showAutoSliceMenu (juce::Component* target, int bank, int pad, int layer, int zone, std::function<void()> done)
{
    juce::PopupMenu m;
    juce::PopupMenu split, grid, trans;
    for (int n : { 2, 4, 8, 12, 16, 24, 32, 48, 64 }) split.addItem (100 + n, juce::String (n) + " equal slices");
    const char* divs[] = { "1 bar", "1/2", "1/4", "1/8", "1/16", "1/32" };
    for (int i = 0; i < 6; ++i) grid.addItem (200 + i, divs[i]);
    for (int s : { 20, 40, 60, 80, 95 }) trans.addItem (300 + s, "Sensitivity " + juce::String (s) + "%");
    m.addSectionHeader ("Auto-slice");
    m.addSubMenu ("Split", split);
    m.addSubMenu ("Grid (uses the layer BPM)", grid);
    m.addSubMenu ("Transients", trans);
    m.addSeparator();
    m.addItem (1, "Clear all slices");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [=] (int r)
    {
        auto& e = Engine::get();
        if (r == 1)
        {
            const juce::ScopedLock sl (e.getLock());
            auto& l = e.session().banks[(size_t) bank].pads[(size_t) pad].layers;
            if (juce::isPositiveAndBelow (layer, (int) l.size()) && juce::isPositiveAndBelow (zone, (int) l[(size_t) layer].zones.size()))
                l[(size_t) layer].zones[(size_t) zone].slices.clear();
        }
        else if (r >= 300) e.autoSlice (bank, pad, layer, zone, 2, 0, (float) (r - 300) / 100.0f);
        else if (r >= 200) e.autoSlice (bank, pad, layer, zone, 1, r - 200, 0.5f);
        else if (r >= 100) e.autoSlice (bank, pad, layer, zone, 0, r - 100, 0.5f);
        if (r > 0 && done) done();
    });
}

// ============================================================================
// Slice "Save" dialog: where the slices go
// ============================================================================
namespace
{
class SliceSaveDialog : public juce::Component
{
public:
    SliceSaveDialog (int b, int p, int l, int z) : bank (b), pad (p), layer (l), zone (z)
    {
        auto& e = Engine::get();
        targets.setTabs ({ "Pads", "Single layer", "Multiple layers", "Files" });
        targets.onChange = [this] (int) { update(); };
        addAndMakeVisible (targets);

        // destination bank: the next empty bank after this one
        int dest = b;
        for (int i = 1; i < cr::numBanks; ++i)
        {
            const int c = (b + i) % cr::numBanks;
            bool empty = true;
            for (auto& pd : e.session().banks[(size_t) c].pads) if (pd.hasSound()) { empty = false; break; }
            if (empty) { dest = c; break; }
        }
        destBank = dest;
        bankStep.getText = [this] { return cr::bankName (destBank); };
        bankStep.onStep = [this] (int d) { destBank = juce::jlimit (0, cr::numBanks - 1, destBank + d); };
        startStep.getText = [this] { return targets.getSelected() == 0 ? "Pad " + juce::String (start + 1) : noteName (startKey); };
        startStep.onStep = [this] (int d) { if (targets.getSelected() == 0) start = juce::jlimit (0, cr::numPads - 1, start + d); else startKey = juce::jlimit (0, 127, startKey + d); };
        chokeStep.getText = [this] { return choke == 0 ? juce::String ("None") : juce::String (choke); };
        chokeStep.onStep = [this] (int d) { choke = juce::jlimit (0, 16, choke + d); };
        fadeIn.setCustom (0.0f, 50.0f, 1.0f, [this] { return fadeInMs; }, [this] (float v) { fadeInMs = v; }, [] (float v) { return juce::String (v, 1) + " ms"; });
        fadeOut.setCustom (0.0f, 200.0f, 5.0f, [this] { return fadeOutMs; }, [this] (float v) { fadeOutMs = v; }, [] (float v) { return juce::String (v, 1) + " ms"; });
        pattern.setButtonText ("Create a pattern that plays the slices in order");
        pattern.setToggleState (true, juce::dontSendNotification);
        for (auto* c : std::initializer_list<juce::Component*> { &bankStep, &startStep, &chokeStep, &fadeIn, &fadeOut, &pattern, &ok, &cancel }) addAndMakeVisible (c);
        ok.setColour (juce::TextButton::buttonColourId, Pal::accent);
        ok.setColour (juce::TextButton::textColourOffId, Pal::bg);
        ok.onClick = [this] { save(); };
        cancel.onClick = [this] { close(); };
        setSize (560, 330);
        update();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Pal::panel);
        auto r = getLocalBounds().reduced (20, 16);
        g.setColour (Pal::text);
        g.setFont (Fonts::ui (16.0f, true));
        g.drawText ("Save slices", r.removeFromTop (24), juce::Justification::centredLeft);
        auto& z = Engine::get().session().banks[(size_t) bank].pads[(size_t) pad].layers[(size_t) layer].zones[(size_t) zone];
        drawCaps (g, juce::String ((int) z.slices.size()) + " slices", r.removeFromTop (18), juce::Justification::centredLeft, Pal::textDim, 10.5f);
        g.setColour (Pal::textDim);
        g.setFont (Fonts::ui (12.5f));
        g.drawFittedText (explanation, explanationArea, juce::Justification::topLeft, 3);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (20, 16);
        r.removeFromTop (46);
        targets.setBounds (r.removeFromTop (30));
        r.removeFromTop (10);
        explanationArea = r.removeFromTop (36);
        r.removeFromTop (6);
        auto row = r.removeFromTop (64);
        const int w = row.getWidth() / 5;
        for (auto* c : std::initializer_list<juce::Component*> { &bankStep, &startStep, &chokeStep, &fadeIn, &fadeOut }) c->setBounds (row.removeFromLeft (w).reduced (4, 0));
        r.removeFromTop (10);
        pattern.setBounds (r.removeFromTop (26));
        auto buttons = r.removeFromBottom (34);
        ok.setBounds (buttons.removeFromRight (120));
        buttons.removeFromRight (8);
        cancel.setBounds (buttons.removeFromRight (100));
    }

private:
    void update()
    {
        const int t = targets.getSelected();
        const char* text[] = { "Each slice goes onto its own pad in the destination bank. Pads in the same choke group cut each other off.",
                               "Slices are spread over consecutive keys of this layer; the pad switches to Keys mode.",
                               "Each slice gets its own layer, each mapped to one key; the pad switches to Keys mode.",
                               "Each slice is written to a WAV file in Library > Samples." };
        explanation = text[t];
        bankStep.setVisible (t == 0);
        chokeStep.setVisible (t == 0);
        startStep.setVisible (t != 3);
        fadeIn.setVisible (t != 3);
        fadeOut.setVisible (t != 3);
        pattern.setVisible (t != 3);
        repaint();
    }

    void save()
    {
        auto& e = Engine::get();
        auto& z = e.session().banks[(size_t) bank].pads[(size_t) pad].layers[(size_t) layer].zones[(size_t) zone];
        const auto slices = z.slices;
        const int t = targets.getSelected();
        const auto target = t == 0 ? Engine::SliceTarget::pads : t == 1 ? Engine::SliceTarget::singleLayer : t == 2 ? Engine::SliceTarget::multipleLayers : Engine::SliceTarget::files;
        juce::String err;
        if (! e.applySlices (bank, pad, layer, zone, slices, target, destBank, t == 0 ? start : startKey, choke, pattern.getToggleState() && t != 3, fadeInMs, fadeOutMs, err))
        {
            showError ("Slices", err);
            return;
        }
        if (t == 0) { e.selectedBank = destBank; e.selectedPad = start; }
        Nav::setStatus (t == 3 ? "Slices written to Library > Samples" : "Slices saved");
        ViewBase::broadcast();
        close();
    }

    void close() { if (auto* w = findParentComponentOfClass<juce::DialogWindow>()) w->exitModalState (0); }

    int bank, pad, layer, zone;
    int destBank = 1, start = 0, startKey = 48, choke = 1;
    float fadeInMs = 1.0f, fadeOutMs = 5.0f;
    TabStrip targets;
    juce::String explanation;
    juce::Rectangle<int> explanationArea;
    Stepper bankStep { "Destination bank" }, startStep { "Start at" }, chokeStep { "Choke group" };
    Knob fadeIn { "Fade in" }, fadeOut { "Fade out" };
    juce::ToggleButton pattern;
    juce::TextButton ok { "Save slices" }, cancel { "Cancel" };
};
} // namespace

void showSliceSaveDialog (int bank, int pad, int layer, int zone)
{
    auto& e = Engine::get();
    auto& p = e.session().banks[(size_t) bank].pads[(size_t) pad];
    if (! juce::isPositiveAndBelow (layer, (int) p.layers.size()) || ! juce::isPositiveAndBelow (zone, (int) p.layers[(size_t) layer].zones.size())
        || p.layers[(size_t) layer].zones[(size_t) zone].slices.empty())
    {
        showError ("Save slices", "There are no slices yet. Turn on SLICE MODE and click the ruler above the waveform, or use AUTO-SLICE.");
        return;
    }
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new SliceSaveDialog (bank, pad, layer, zone));
    o.dialogTitle = "Save slices";
    o.dialogBackgroundColour = Pal::panel;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}
