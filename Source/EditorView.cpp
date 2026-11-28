// Editor view: pad selector, channel settings, layers & zones, and the Sampler,
// Layer FX, Modulations, Mapping and Plugin pages.
#include "SampleEditor.h"

namespace
{
int selBank() { return Engine::get().selectedBank.load(); }
int selPad() { return Engine::get().selectedPad.load(); }
int selLayer()
{
    auto& p = currentPad();
    return p.layers.empty() ? -1 : juce::jlimit (0, (int) p.layers.size() - 1, Engine::get().selectedLayer.load());
}
int selZone()
{
    auto* l = currentLayer();
    if (l == nullptr || l->zones.empty()) return -1;
    return juce::jlimit (0, (int) l->zones.size() - 1, Engine::get().selectedZone.load());
}
juce::Colour accentColour() { return padColour (selBank(), selPad()); }

template <typename Fn>
void withLayer (Fn&& fn)
{
    auto& e = Engine::get();
    const juce::ScopedLock sl (e.getLock());
    if (auto* l = currentLayer()) fn (*l);
}

// ============================================================================
// Mini pad grid (left column)
// ============================================================================
class MiniPads : public juce::Component
{
public:
    int page = 0;
    void paint (juce::Graphics& g) override
    {
        auto& e = Engine::get();
        const int b = selBank();
        const auto& bank = e.session().banks[(size_t) b];
        const float cw = (float) getWidth() / 4.0f, ch = (float) getHeight() / 4.0f;
        for (int i = 0; i < 16; ++i)
        {
            const int p = page * 16 + i;
            const int row = 3 - i / 4, col = i % 4;
            auto r = juce::Rectangle<float> (col * cw, row * ch, cw, ch).reduced (2.5f);
            const auto& pad = bank.pads[(size_t) p];
            const float flash = e.padFlash[b][p].load();
            auto c = pad.hasSound() ? padColour (b, p).darker (0.15f) : Pal::emptyPad;
            c = c.interpolatedWith (juce::Colours::white, flash * 0.4f);
            g.setColour (c);
            g.fillRoundedRectangle (r, 4.0f);
            if (! pad.hasSound()) { g.setColour (Pal::outline); g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f); }
            if (p == selPad()) { g.setColour (juce::Colours::white); g.drawRoundedRectangle (r.reduced (1.0f), 4.0f, 2.0f); }
            g.setColour (pad.hasSound() ? juce::Colours::white.withAlpha (0.8f) : Pal::textFaint);
            g.setFont (Fonts::ui (9.5f, true));
            g.drawText (juce::String (p + 1), r.reduced (4.0f, 2.0f), juce::Justification::bottomLeft);
        }
    }
    void mouseDown (const juce::MouseEvent& ev) override
    {
        const int col = juce::jlimit (0, 3, ev.x * 4 / juce::jmax (1, getWidth()));
        const int row = juce::jlimit (0, 3, ev.y * 4 / juce::jmax (1, getHeight()));
        const int p = page * 16 + (3 - row) * 4 + col;
        auto& e = Engine::get();
        if (! currentBank().created) e.createBank (selBank());
        e.selectedPad = p;
        e.selectedLayer = 0;
        e.selectedZone = 0;
        if (ev.mods.isPopupMenu()) { if (onMenu) onMenu(); }
        else { e.padDown (selBank(), p, e.fixedVelocity.load()); pressed = p; }
        ViewBase::broadcast();
    }
    void mouseUp (const juce::MouseEvent&) override { if (pressed >= 0) Engine::get().padUp (selBank(), pressed); pressed = -1; }
    std::function<void()> onMenu;
private:
    int pressed = -1;
};

// ============================================================================
// Layers & zones list (Samples tab)
// ============================================================================
class LayerList : public juce::Component
{
public:
    std::function<void()> onChanged;

    int getContentHeight() const
    {
        int h = 0;
        for (auto& l : currentPad().layers) h += 34 + 26 * (int) l.zones.size() + 6;
        return h + 4;
    }

    void paint (juce::Graphics& g) override
    {
        auto& pad = currentPad();
        int y = 0;
        const auto acc = accentColour();
        for (int l = 0; l < (int) pad.layers.size(); ++l)
        {
            const auto& layer = pad.layers[(size_t) l];
            auto row = juce::Rectangle<int> (0, y, getWidth(), 32);
            const bool sel = l == selLayer();
            g.setColour (sel ? Pal::surface2 : Pal::surface);
            g.fillRoundedRectangle (row.toFloat(), 4.0f);
            if (sel) { g.setColour (acc); g.fillRect (row.getX(), row.getY() + 6, 3, row.getHeight() - 12); }
            g.setColour (layer.p.mute ? Pal::textFaint : Pal::text);
            g.setFont (Fonts::ui (12.5f, true));
            g.drawFittedText (juce::String (l + 1) + "   " + layer.name, row.reduced (12, 0).withTrimmedRight (70), juce::Justification::centredLeft, 1);
            // M and menu
            auto m = row.removeFromRight (34).reduced (5);
            juce::ignoreUnused (m);
            auto mute = row.removeFromRight (30).reduced (4, 6);
            g.setColour (layer.p.mute ? Pal::mute : Pal::surface3);
            g.fillRoundedRectangle (mute.toFloat(), 3.0f);
            g.setColour (layer.p.mute ? Pal::bg : Pal::textDim);
            g.setFont (Fonts::ui (10.5f, true));
            g.drawText ("M", mute, juce::Justification::centred);
            drawIcon (g, Icon::dots, juce::Rectangle<float> ((float) getWidth() - 30.0f, (float) y + 4.0f, 24.0f, 24.0f), Pal::textDim);
            y += 34;
            for (int z = 0; z < (int) layer.zones.size(); ++z)
            {
                const auto& zone = layer.zones[(size_t) z];
                auto zr = juce::Rectangle<int> (14, y, getWidth() - 14, 24);
                const bool zs = sel && z == selZone();
                if (zs) { g.setColour (acc.withAlpha (0.18f)); g.fillRoundedRectangle (zr.toFloat(), 3.0f); }
                drawIcon (g, Icon::wave, zr.removeFromLeft (22).toFloat().reduced (4.0f), zs ? acc : Pal::textFaint);
                g.setColour (zone.sample ? (zone.mute ? Pal::textFaint : Pal::textDim) : Pal::textFaint);
                g.setFont (Fonts::ui (11.5f));
                juce::String range = noteName (zone.lowKey) + "-" + noteName (zone.highKey);
                if (zone.lowVel > 1 || zone.highVel < 127) range << "  v" << zone.lowVel << "-" << zone.highVel;
                g.drawFittedText (zone.sample ? zone.sample->name : juce::String ("(empty)"), zr.withTrimmedRight (90), juce::Justification::centredLeft, 1);
                drawCaps (g, range, zr.withTrimmedRight (6), juce::Justification::centredRight, Pal::textFaint, 9.0f);
                y += 26;
            }
            y += 6;
        }
        if (pad.layers.empty())
        {
            g.setColour (Pal::textFaint);
            g.setFont (Fonts::ui (12.5f));
            g.drawFittedText ("No samples on this pad. Load one from the Browser, drag a file here, or record one.", getLocalBounds().reduced (6), juce::Justification::centredTop, 3);
        }
    }

    void mouseDown (const juce::MouseEvent& ev) override
    {
        auto& e = Engine::get();
        auto& pad = currentPad();
        int y = 0;
        for (int l = 0; l < (int) pad.layers.size(); ++l)
        {
            auto& layer = pad.layers[(size_t) l];
            if (ev.y >= y && ev.y < y + 32)
            {
                e.selectedLayer = l;
                e.selectedZone = 0;
                if (ev.x > getWidth() - 34 || ev.mods.isPopupMenu()) layerMenu (l);
                else if (ev.x > getWidth() - 64) { const juce::ScopedLock sl (e.getLock()); layer.p.mute = ! layer.p.mute; }
                changed();
                return;
            }
            y += 34;
            for (int z = 0; z < (int) layer.zones.size(); ++z)
            {
                if (ev.y >= y && ev.y < y + 24)
                {
                    e.selectedLayer = l;
                    e.selectedZone = z;
                    if (ev.mods.isPopupMenu()) zoneMenu (l, z);
                    changed();
                    return;
                }
                y += 26;
            }
            y += 6;
        }
    }

private:
    void changed() { repaint(); if (onChanged) onChanged(); ViewBase::broadcast(); }

    void layerMenu (int l)
    {
        auto& e = Engine::get();
        auto& layer = currentPad().layers[(size_t) l];
        juce::PopupMenu m;
        m.addSectionHeader (layer.name);
        m.addItem (1, "Rename layer...");
        m.addItem (2, "Mute", true, layer.p.mute);
        m.addItem (3, "Disk streaming", true, layer.p.diskStream);
        m.addItem (4, "Delete layer");
        const int b = selBank(), p = selPad();
        m.showMenuAsync (juce::PopupMenu::Options(), [this, b, p, l] (int r)
        {
            auto& en = Engine::get();
            auto& ly = en.session().banks[(size_t) b].pads[(size_t) p].layers;
            if (! juce::isPositiveAndBelow (l, (int) ly.size())) return;
            if (r == 1) askForText ("Layer name", ly[(size_t) l].name, [b, p, l] (const juce::String& n)
            {
                auto& lys = Engine::get().session().banks[(size_t) b].pads[(size_t) p].layers;
                if (juce::isPositiveAndBelow (l, (int) lys.size())) lys[(size_t) l].name = n;
                ViewBase::broadcast();
            });
            else if (r == 2) { const juce::ScopedLock sl (en.getLock()); ly[(size_t) l].p.mute = ! ly[(size_t) l].p.mute; }
            else if (r == 3) en.setLayerStreaming (b, p, l, ! ly[(size_t) l].p.diskStream);
            else if (r == 4) { en.checkpointPad (b, p); en.removeLayer (b, p, l); }
            changed();
        });
        juce::ignoreUnused (e);
    }

    void zoneMenu (int l, int z)
    {
        const int b = selBank(), p = selPad();
        auto& pad = currentPad();
        juce::PopupMenu m;
        juce::PopupMenu move;
        for (int i = 0; i < (int) pad.layers.size(); ++i) if (i != l) move.addItem (100 + i, "Layer " + juce::String (i + 1) + "  " + pad.layers[(size_t) i].name);
        m.addSubMenu ("Move to layer", move, pad.layers.size() > 1);
        m.addItem (1, "Mute this sample", true, pad.layers[(size_t) l].zones[(size_t) z].mute);
        m.addItem (2, "Remove this sample");
        m.addItem (3, "Open in the Audio Editor");
        m.showMenuAsync (juce::PopupMenu::Options(), [this, b, p, l, z] (int r)
        {
            auto& e = Engine::get();
            if (r >= 100) { e.checkpointPad (b, p); e.moveZone (b, p, l, z, r - 100); }
            else if (r == 1) { const juce::ScopedLock sl (e.getLock()); auto& zn = e.session().banks[(size_t) b].pads[(size_t) p].layers[(size_t) l].zones[(size_t) z]; zn.mute = ! zn.mute; }
            else if (r == 2) { e.checkpointPad (b, p); e.removeZone (b, p, l, z); }
            else if (r == 3) Nav::showOverlay (createAudioEditor (b, p, l, z));
            changed();
        });
    }
};

// ============================================================================
// Envelope & filter drawings
// ============================================================================
class EnvelopeGraph : public juce::Component
{
public:
    std::function<std::array<float, 5>()> getADSR;   // attack, hold, decay, sustain, release
    bool ahd = false;
    juce::Colour accent = Pal::accent;
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (Pal::bg);
        g.fillRoundedRectangle (r, 4.0f);
        if (! getADSR) return;
        auto v = getADSR();
        auto seg = [] (float s) { return std::sqrt (juce::jlimit (0.0f, 20.0f, s) + 0.001f); };
        const float a = seg (v[0]), h = seg (v[1]) * 0.6f, d = seg (v[2]), s = ahd ? 0.0f : v[3], rl = ahd ? 0.0f : seg (v[4]);
        const float sustainW = ahd ? 0.0f : 0.8f;
        const float total = a + h + d + sustainW + rl;
        const float w = r.getWidth() / juce::jmax (0.01f, total);
        juce::Path p;
        float x = r.getX();
        const float bottom = r.getBottom() - 4.0f, top = r.getY() + 6.0f;
        p.startNewSubPath (x, bottom);
        x += a * w; p.lineTo (x, top);
        x += h * w; p.lineTo (x, top);
        x += d * w; p.lineTo (x, bottom - s * (bottom - top));
        if (! ahd) { x += sustainW * w; p.lineTo (x, bottom - s * (bottom - top)); x += rl * w; p.lineTo (x, bottom); }
        juce::Path fill (p);
        fill.lineTo (x, bottom);
        fill.closeSubPath();
        g.setColour (accent.withAlpha (0.18f));
        g.fillPath (fill);
        g.setColour (accent);
        g.strokePath (p, juce::PathStrokeType (2.0f));
    }
};

class FilterGraph : public juce::Component
{
public:
    juce::Colour accent = Pal::accent;
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (Pal::bg);
        g.fillRoundedRectangle (r, 4.0f);
        auto* l = currentLayer();
        if (l == nullptr) return;
        const int type = (int) l->p.filter;
        const float cutoff = l->p.cutoff, q = l->p.resonance;
        juce::Path p;
        const float bottom = r.getBottom() - 3.0f, mid = r.getY() + r.getHeight() * 0.45f;
        for (int i = 0; i <= 120; ++i)
        {
            const float f = 20.0f * std::pow (1000.0f, (float) i / 120.0f);
            const float x = r.getX() + r.getWidth() * (float) i / 120.0f;
            const float ratio = f / cutoff;
            float db = 0.0f;
            const float res = 20.0f * std::log10 (juce::jmax (0.3f, q)) * std::exp (-std::pow (std::log2 (ratio) * 3.0f, 2.0f));
            switch (type)
            {
                case 1: db = ratio > 1.0f ? -12.0f * std::log2 (ratio) : 0.0f; db += res; break;
                case 2: db = ratio > 1.0f ? -24.0f * std::log2 (ratio) : 0.0f; db += res; break;
                case 3: db = ratio < 1.0f ? 12.0f * std::log2 (ratio) : 0.0f; db += res; break;
                case 4: db = ratio < 1.0f ? 24.0f * std::log2 (ratio) : 0.0f; db += res; break;
                case 5: db = -12.0f * std::abs (std::log2 (ratio)) + res; break;
                case 6: db = -30.0f * std::exp (-std::pow (std::log2 (ratio) * 4.0f, 2.0f)); break;
                default: db = 0.0f; break;
            }
            const float y = juce::jlimit (r.getY() + 2.0f, bottom, mid - db * (r.getHeight() / 60.0f));
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (type == 0 ? Pal::textFaint : accent);
        g.strokePath (p, juce::PathStrokeType (2.0f));
    }
};

// ============================================================================
// Sampler page
// ============================================================================
class SamplerPage : public juce::Component, private juce::Timer
{
public:
    SamplerPage()
    {
        addAndMakeVisible (wave);
        wave.onChange = [this] { repaint(); };
        auto addBtn = [this] (juce::Button& b) { addAndMakeVisible (b); };
        recBtn.setOnColour (Pal::rec);
        recBtn.onClick = [] { showRecorderDialog (selBank(), selPad()); };
        sampleMenuBtn.onClick = [this] { sampleMenu(); };
        autoSliceBtn.onClick = [this] { if (selZone() >= 0) { setSliceMode (true); showAutoSliceMenu (&autoSliceBtn, selBank(), selPad(), selLayer(), selZone(), [this] { wave.repaint(); }); } };
        saveBtn.onClick = [] { if (selZone() >= 0) showSliceSaveDialog (selBank(), selPad(), selLayer(), selZone()); };
        sliceModeBtn.setClickingTogglesState (false);
        sliceModeBtn.onClick = [this] { setSliceMode (wave.getMode() != WaveformView::Mode::slices); };
        editBtn.onClick = [] { if (selZone() >= 0) Nav::showOverlay (createAudioEditor (selBank(), selPad(), selLayer(), selZone())); };
        trimBtn.setTooltip ("Cut the sample down to what's between the S and E markers");
        trimBtn.onClick = [this]
        {
            auto* z = wave.getZone();
            if (z == nullptr) return;
            if (z->start <= 0.0001f && z->end >= 0.9999f) { Nav::setStatus ("Move the S and E markers first, then Trim"); return; }
            if (Engine::get().editSample (selBank(), selPad(), selLayer(), selZone(), Engine::SampleEdit::trim, z->start, z->end))
            {
                wave.setTarget (selBank(), selPad(), selLayer(), selZone());
                wave.zoomToFit();
                Nav::setStatus ("Trimmed to the markers (Ctrl/Cmd+Z to undo)");
                ViewBase::broadcast();
            }
        };
        snapBtn.setClickingTogglesState (true);
        snapBtn.setToggleState (true, juce::dontSendNotification);
        snapBtn.onClick = [this] { wave.zeroSnap = snapBtn.getToggleState(); };
        zoomIn.onClick = [this] { wave.zoom (2.0); updateZoom(); };
        zoomOut.onClick = [this] { wave.zoom (0.5); updateZoom(); };
        scrollLeft.onClick = [this] { wave.scroll (-0.5); };
        scrollRight.onClick = [this] { wave.scroll (0.5); };
        toStart.onClick = [this] { wave.scroll (-1000.0); };
        toEnd.onClick = [this] { wave.scroll (1000.0); };
        zoomLabel.setJustificationType (juce::Justification::centred);
        zoomLabel.setFont (Fonts::caps (10.5f));
        zoomLabel.setColour (juce::Label::backgroundColourId, Pal::surface);
        for (auto* b : std::initializer_list<juce::Button*> { &recBtn, &sampleMenuBtn, &autoSliceBtn, &saveBtn, &sliceModeBtn, &editBtn, &trimBtn, &snapBtn,
                                                           &zoomIn, &zoomOut, &scrollLeft, &scrollRight, &toStart, &toEnd })
            addBtn (*b);
        addAndMakeVisible (zoomLabel);

        // ---- parameter rows ----
        loopField.choices = { "Not looped", "Forward", "Alternate" };
        loopField.getIndex = [] { auto* l = currentLayer(); return l ? (int) l->p.loop : 0; };
        loopField.setIndex = [this] (int i) { withLayer ([i] (cr::Layer& l) { l.p.loop = (cr::LoopMode) i; }); wave.repaint(); };
        triggerField.choices = { "One shot", "Hold", "On release" };
        triggerField.getIndex = [] { auto* l = currentLayer(); return l ? (int) l->p.trigger : 0; };
        triggerField.setIndex = [] (int i) { withLayer ([i] (cr::Layer& l) { l.p.trigger = (cr::TriggerMode) i; }); };
        streamField.choices = { "Off", "On" };
        streamField.getIndex = [] { auto* l = currentLayer(); return l && l->p.diskStream ? 1 : 0; };
        streamField.setIndex = [] (int i) { if (selLayer() >= 0) Engine::get().setLayerStreaming (selBank(), selPad(), selLayer(), i == 1); };
        stretchField.choices = { "Off", "On" };
        stretchField.getIndex = [] { auto* l = currentLayer(); return l && l->p.liveStretch ? 1 : 0; };
        stretchField.setIndex = [] (int i) { withLayer ([i] (cr::Layer& l) { l.p.liveStretch = i == 1; }); };
        reverseField.choices = { "Off", "On" };
        reverseField.getIndex = [] { auto* l = currentLayer(); return l && l->p.reverse ? 1 : 0; };
        reverseField.setIndex = [] (int i) { withLayer ([i] (cr::Layer& l) { l.p.reverse = i == 1; }); };
        legatoField.choices = { "Off", "On" };
        legatoField.getIndex = [] { auto* l = currentLayer(); return l && l->p.legato ? 1 : 0; };
        legatoField.setIndex = [] (int i) { withLayer ([i] (cr::Layer& l) { l.p.legato = i == 1; }); };
        lofiField.choices = cr::lofiNames();
        lofiField.getIndex = [] { auto* l = currentLayer(); return l ? (int) l->p.lofi : 0; };
        lofiField.setIndex = [] (int i) { withLayer ([i] (cr::Layer& l) { l.p.lofi = (cr::LofiMode) i; }); };
        polyStep.getText = [] { auto* l = currentLayer(); return l ? juce::String (l->p.polyphony) : juce::String ("-"); };
        polyStep.onStep = [] (int d) { withLayer ([d] (cr::Layer& l) { l.p.polyphony = juce::jlimit (1, 64, l.p.polyphony + d); }); };
        bpmStep.getText = [] { auto* l = currentLayer(); return l ? juce::String (l->p.sampleBpm, 2) : juce::String ("-"); };
        bpmStep.onStep = [] (int d) { withLayer ([d] (cr::Layer& l) { l.p.sampleBpm = juce::jlimit (20.0f, 400.0f, l.p.sampleBpm + (float) d); }); };
        bpmStep.onClickValue = [] { if (auto* l = currentLayer()) askForNumber ("Sample BPM", l->p.sampleBpm, [] (double v) { withLayer ([v] (cr::Layer& l2) { l2.p.sampleBpm = (float) juce::jlimit (20.0, 400.0, v); }); ViewBase::broadcast(); }); };
        beatsStep.getText = [] { auto* l = currentLayer(); return l ? juce::String (l->p.sampleBeats) + " beats" : juce::String ("-"); };
        beatsStep.onStep = [] (int d)
        {
            withLayer ([d] (cr::Layer& l)
            {
                l.p.sampleBeats = juce::jlimit (1, 256, l.p.sampleBeats + d);
                if (! l.zones.empty() && l.zones[0].sample)
                {
                    const double secs = l.zones[0].sample->lengthSeconds() * (l.zones[0].end - l.zones[0].start);
                    if (secs > 0.01) l.p.sampleBpm = (float) (l.p.sampleBeats * 60.0 / secs);
                }
            });
        };
        for (auto* c : std::initializer_list<juce::Component*> { &loopField, &triggerField, &streamField, &stretchField, &reverseField, &legatoField,
                                                              &lofiField, &polyStep, &bpmStep, &beatsStep, &xfade, &tune, &fine, &gain, &pan, &saturate, &glide })
            addAndMakeVisible (c);

        // ---- envelope / filter ----
        envPower.onClick = [this] { withLayer ([this] (cr::Layer& l) { l.p.envOn = envPower.getToggleState(); }); repaint(); };
        envModeField.choices = { "AHD", "AHDSR" };
        envModeField.getIndex = [] { auto* l = currentLayer(); return l ? (int) l->p.envMode : 1; };
        envModeField.setIndex = [this] (int i) { withLayer ([i] (cr::Layer& l) { l.p.envMode = (cr::EnvMode) i; }); refresh(); };
        envGraph.getADSR = [] () -> std::array<float, 5>
        {
            auto* l = currentLayer();
            if (l == nullptr) return { 0.01f, 0.0f, 0.5f, 1.0f, 0.1f };
            return { l->p.attack, l->p.hold, l->p.decay, l->p.sustain, l->p.release };
        };
        filterPower.onClick = [this]
        {
            withLayer ([this] (cr::Layer& l)
            {
                if (filterPower.getToggleState()) { if (l.p.filter == cr::FilterType::off) l.p.filter = lastFilter; }
                else { lastFilter = l.p.filter == cr::FilterType::off ? lastFilter : l.p.filter; l.p.filter = cr::FilterType::off; }
            });
            refresh();
        };
        filterField.choices = cr::filterTypeNames();
        filterField.getIndex = [] { auto* l = currentLayer(); return l ? (int) l->p.filter : 0; };
        filterField.setIndex = [this] (int i) { withLayer ([i] (cr::Layer& l) { l.p.filter = (cr::FilterType) i; }); refresh(); };
        for (auto* c : std::initializer_list<juce::Component*> { &envPower, &envModeField, &envGraph, &attack, &hold, &decay, &sustain, &release,
                                                              &filterPower, &filterField, &filterGraph, &cutoff, &reso })
            addAndMakeVisible (c);
        for (auto* k : { &attack, &hold, &decay, &sustain, &release }) k->onUserChange = [this] { envGraph.repaint(); };
        for (auto* k : { &cutoff, &reso }) k->onUserChange = [this] { filterGraph.repaint(); };
        startTimerHz (15);
    }

    void refresh()
    {
        const int b = selBank(), p = selPad(), l = selLayer(), z = selZone();
        wave.setTarget (b, p, l, z);
        const auto acc = accentColour();
        wave.setAccent (acc);
        const bool hasLayer = l >= 0;
        auto bind = [&] (Knob& k, int field, const juce::String& label)
        {
            k.setAccent (acc);
            if (hasLayer) k.setParam (layerPath (b, p, l, field), label);
            else { k.setLabel (label); k.setEnabled (false); }
        };
        bind (tune, cr::lfTune, "Tune"); bind (fine, cr::lfFine, "Fine tune"); bind (gain, cr::lfGain, "Gain"); bind (pan, cr::lfPan, "Pan");
        bind (saturate, cr::lfSaturate, "Saturate"); bind (glide, cr::lfGlide, "Glide");
        bind (attack, cr::lfAttack, "Attack"); bind (hold, cr::lfHold, "Hold"); bind (decay, cr::lfDecay, "Decay");
        bind (sustain, cr::lfSustain, "Sustain"); bind (release, cr::lfRelease, "Release");
        bind (cutoff, cr::lfCutoff, "Cutoff"); bind (reso, cr::lfResonance, "Reso");
        xfade.setAccent (acc);
        xfade.setCustom (0.0f, 0.5f, 0.0f, [] { auto* ly = currentLayer(); return ly ? ly->p.crossfade : 0.0f; },
                         [] (float v) { withLayer ([v] (cr::Layer& ly) { ly.p.crossfade = v; }); },
                         [] (float v) { return juce::String (juce::roundToInt (v * 200.0f)) + "%"; });
        xfade.setEnabled (hasLayer);
        auto* layer = currentLayer();
        const bool ahd = layer != nullptr && layer->p.envMode == cr::EnvMode::ahd;
        sustain.setVisible (! ahd);
        release.setVisible (! ahd);
        envGraph.ahd = ahd;
        envGraph.accent = acc;
        filterGraph.accent = acc;
        envPower.accent = acc;
        filterPower.accent = acc;
        envPower.setToggleState (layer && layer->p.envOn, juce::dontSendNotification);
        filterPower.setToggleState (layer && layer->p.filter != cr::FilterType::off, juce::dontSendNotification);
        for (auto* f : { &loopField, &triggerField, &streamField, &stretchField, &reverseField, &legatoField, &lofiField, &envModeField, &filterField })
        {
            f->accent = acc;
            f->setEnabled (hasLayer);
        }
        cutoff.setEnabled (hasLayer && layer->p.filter != cr::FilterType::off);
        reso.setEnabled (hasLayer && layer->p.filter != cr::FilterType::off);
        sliceModeBtn.setToggleState (wave.getMode() == WaveformView::Mode::slices, juce::dontSendNotification);
        for (auto* b2 : std::initializer_list<juce::Button*> { &autoSliceBtn, &saveBtn, &sliceModeBtn, &editBtn, &trimBtn, &sampleMenuBtn }) b2->setEnabled (z >= 0);
        updateZoom();
        resized();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Pal::bg);
        // sample name
        auto s = wave.getSample();
        g.setColour (s ? Pal::text : Pal::textFaint);
        g.setFont (Fonts::ui (14.0f, true));
        g.drawFittedText (s ? s->name : juce::String ("No sample"), nameArea, juce::Justification::centredLeft, 1);
        if (s)
            drawCaps (g, cr::bankLetter (selBank()) + "." + juce::String (selPad() + 1) + "  layer " + juce::String (selLayer() + 1)
                            + "   " + juce::String (s->lengthSeconds(), 2) + " s" + (s->isStreamed() ? "  disk" : ""),
                      nameArea.translated (0, 15), juce::Justification::centredLeft, Pal::textFaint, 9.0f);
        // duration readout
        if (auto* l = currentLayer())
            if (s)
            {
                auto* z = wave.getZone();
                const double d = s->lengthSeconds() * (z ? (z->end - z->start) : 1.0f);
                auto da = durationArea;
                drawCaps (g, "Duration", da.removeFromTop (14), juce::Justification::centred, Pal::textFaint, 9.5f);
                g.setColour (accentColour());
                g.setFont (Fonts::ui (12.5f, true));
                g.drawText (juce::String (d, 2) + " s", da, juce::Justification::centred);
                juce::ignoreUnused (l);
            }
        auto envBox = drawSection (g, envArea, "Amplitude envelope");
        auto filterBox = drawSection (g, filterArea, "Filter");
        juce::ignoreUnused (envBox, filterBox);
        g.setColour (Pal::outline);
        g.fillRect (rowsArea.getX(), rowsArea.getY() - 6, rowsArea.getWidth(), 1);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14, 10);
        auto top = r.removeFromTop (36);
        recBtn.setBounds (top.removeFromLeft (52));
        top.removeFromLeft (10);
        editBtn.setBounds (top.removeFromRight (70).reduced (0, 3));
        top.removeFromRight (6);
        trimBtn.setBounds (top.removeFromRight (64).reduced (0, 3));
        top.removeFromRight (6);
        sliceModeBtn.setBounds (top.removeFromRight (104).reduced (0, 3));
        top.removeFromRight (6);
        saveBtn.setBounds (top.removeFromRight (74).reduced (0, 3));
        top.removeFromRight (6);
        autoSliceBtn.setBounds (top.removeFromRight (98).reduced (0, 3));
        top.removeFromRight (10);
        sampleMenuBtn.setBounds (top.removeFromRight (30));
        nameArea = top.withHeight (18).withY (top.getY() + 1);
        r.removeFromTop (6);

        const int waveH = juce::jmax (120, r.getHeight() - 36 - 140 - 170);
        wave.setBounds (r.removeFromTop (waveH));
        r.removeFromTop (6);
        auto nav = r.removeFromTop (30);
        snapBtn.setBounds (nav.removeFromLeft (100));
        auto navc = nav.withSizeKeepingCentre (300, 30);
        toStart.setBounds (navc.removeFromLeft (30));
        scrollLeft.setBounds (navc.removeFromLeft (30));
        toEnd.setBounds (navc.removeFromRight (30));
        scrollRight.setBounds (navc.removeFromRight (30));
        zoomLabel.setBounds (navc.reduced (8, 2));
        zoomIn.setBounds (nav.removeFromRight (30));
        zoomOut.setBounds (nav.removeFromRight (30));
        r.removeFromTop (14);

        rowsArea = r.removeFromTop (130);
        auto rows = rowsArea;
        auto row1 = rows.removeFromTop (44);
        const int w1 = row1.getWidth() / 9;
        for (auto* c : std::initializer_list<juce::Component*> { &loopField, &bpmStep, &beatsStep })
            c->setBounds (row1.removeFromLeft (w1).reduced (4, 2));
        durationArea = row1.removeFromLeft (w1).reduced (4, 2);
        for (auto* c : std::initializer_list<juce::Component*> { &triggerField, &streamField, &stretchField, &reverseField, &polyStep })
            c->setBounds (row1.removeFromLeft (w1).reduced (4, 2));
        rows.removeFromTop (8);
        auto row2 = rows;
        const int w2 = row2.getWidth() / 9;
        for (auto* c : std::initializer_list<juce::Component*> { &tune, &fine, &gain, &pan, &saturate, &xfade, &glide })
            c->setBounds (row2.removeFromLeft (w2).reduced (6, 0));
        legatoField.setBounds (row2.removeFromLeft (w2).reduced (4, 14));
        lofiField.setBounds (row2.removeFromLeft (w2).reduced (4, 14));
        r.removeFromTop (10);

        auto bottom = r;
        envArea = bottom.removeFromLeft (bottom.getWidth() * 58 / 100);
        bottom.removeFromLeft (10);
        filterArea = bottom;
        auto ec = envArea.reduced (10, 8);
        auto eh = ec.removeFromTop (22);
        envPower.setBounds (eh.removeFromRight (22));
        eh.removeFromRight (8);
        envModeField.setBounds (eh.removeFromRight (80).withTrimmedTop (-10));
        ec.removeFromTop (4);
        auto knobs = ec.removeFromBottom (62);
        envGraph.setBounds (ec);
        const int kw = knobs.getWidth() / 5;
        for (auto* k : { &attack, &hold, &decay, &sustain, &release }) k->setBounds (knobs.removeFromLeft (kw));
        auto fc = filterArea.reduced (10, 8);
        auto fh = fc.removeFromTop (22);
        filterPower.setBounds (fh.removeFromRight (22));
        fh.removeFromRight (8);
        filterField.setBounds (fh.removeFromRight (120).withTrimmedTop (-10));
        fc.removeFromTop (4);
        auto fknobs = fc.removeFromBottom (62);
        filterGraph.setBounds (fc);
        cutoff.setBounds (fknobs.removeFromLeft (fknobs.getWidth() / 2));
        reso.setBounds (fknobs);
    }

private:
    void timerCallback() override
    {
        if (Engine::get().isPreviewing()) wave.repaint();
    }

    void updateZoom() { zoomLabel.setText (wave.getZoomText(), juce::dontSendNotification); }

    void setSliceMode (bool on)
    {
        wave.setMode (on ? WaveformView::Mode::slices : WaveformView::Mode::markers);
        sliceModeBtn.setToggleState (on, juce::dontSendNotification);
        wave.repaint();
    }

    void sampleMenu()
    {
        const int b = selBank(), p = selPad(), l = selLayer(), z = selZone();
        auto s = wave.getSample();
        juce::PopupMenu m;
        m.addItem (1, "Replace sample...");
        m.addItem (2, "Add a sample to this layer...");
        m.addItem (3, "Add a sample as a new layer...");
        m.addSeparator();
        m.addItem (4, "Save a copy in the library (makes edits permanent)");
        m.addItem (5, "Reveal the file", s && s->file.existsAsFile());
        m.addItem (6, "Detect tempo from the length");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&sampleMenuBtn), [this, b, p, l, z, s] (int r)
        {
            auto& e = Engine::get();
            if (r >= 1 && r <= 3)
            {
                chooser = makeFileChooser ("Choose a sample", Engine::getSamplesFolder(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [b, p, l, z, r] (const juce::FileChooser& fc)
                {
                    auto f = fc.getResult();
                    if (! f.existsAsFile()) return;
                    auto& en = Engine::get();
                    en.checkpointPad (b, p);
                    if (r == 1 && l >= 0 && z >= 0) { if (auto smp = en.loadSample (f)) en.setZoneSample (b, p, l, z, smp); }
                    else en.loadSampleToPad (f, b, p, r == 2 ? Engine::LoadMethod::addToLayer : (r == 3 ? Engine::LoadMethod::newLayer : Engine::LoadMethod::replace));
                    ViewBase::broadcast();
                });
            }
            else if (r == 4) { if (e.duplicateSampleFile (b, p, l, z)) Nav::setStatus ("Saved to Library > Samples"); }
            else if (r == 5 && s) s->file.revealToUser();
            else if (r == 6 && s)
            {
                withLayer ([s] (cr::Layer& ly)
                {
                    const double secs = s->lengthSeconds() * (ly.zones.empty() ? 1.0f : ly.zones[0].end - ly.zones[0].start);
                    ly.p.sampleBpm = (float) (ly.p.sampleBeats * 60.0 / juce::jmax (0.01, secs));
                });
            }
            ViewBase::broadcast();
        });
    }

    WaveformView wave;
    std::unique_ptr<juce::FileChooser> chooser;
    IconButton recBtn { Icon::record, "Record a sample onto this pad" }, sampleMenuBtn { Icon::dots, "Sample options" };
    juce::TextButton autoSliceBtn { "Auto-slice" }, saveBtn { "Save..." }, sliceModeBtn { "Slice mode" }, editBtn { "Edit" }, trimBtn { "Trim" }, snapBtn { "Zero snap" };
    IconButton zoomIn { Icon::zoomIn, "Zoom in" }, zoomOut { Icon::zoomOut, "Zoom out" }, scrollLeft { Icon::left, "Scroll left" },
               scrollRight { Icon::right, "Scroll right" }, toStart { Icon::rewind, "Go to start" }, toEnd { Icon::forward, "Go to end" };
    juce::Label zoomLabel;
    ChoiceField loopField { "Sample loop" }, triggerField { "Layer trigger" }, streamField { "Disk streaming" }, stretchField { "Live stretch" },
                reverseField { "Reverse" }, legatoField { "Legato" }, lofiField { "Lo-fi" };
    Stepper polyStep { "Polyphony" }, bpmStep { "BPM" }, beatsStep { "Length" };
    Knob xfade { "Loop x-fade" }, tune, fine, gain, pan, saturate, glide;
    PowerButton envPower, filterPower;
    ChoiceField envModeField { "" }, filterField { "" };
    EnvelopeGraph envGraph;
    FilterGraph filterGraph;
    Knob attack, hold, decay, sustain, release, cutoff, reso;
    cr::FilterType lastFilter = cr::FilterType::lp12;
    juce::Rectangle<int> nameArea, durationArea, rowsArea, envArea, filterArea;
};

// ============================================================================
// Modulations page
// ============================================================================
class LfoShapeView : public juce::Component
{
public:
    int lfo = 0;
    juce::Colour accent = Pal::accent;
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (Pal::bg);
        g.fillRoundedRectangle (r, 4.0f);
        auto* l = currentLayer();
        if (l == nullptr) return;
        const int shape = (int) l->lfo[(size_t) lfo].shape;
        juce::Path p;
        juce::Random rnd (7);
        float held = 0.0f;
        for (int i = 0; i <= 200; ++i)
        {
            const double ph = std::fmod (i / 100.0, 1.0);
            if (i % 25 == 0) held = rnd.nextFloat() * 2.0f - 1.0f;
            const float v = lfoShapeValue (shape, ph, held);
            const float x = r.getX() + r.getWidth() * (float) i / 200.0f;
            const float y = r.getCentreY() - v * r.getHeight() * 0.4f;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (accent);
        g.strokePath (p, juce::PathStrokeType (2.0f));
    }
};

class StepEditor : public juce::Component
{
public:
    juce::Colour accent = Pal::accent;
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Pal::bg);
        g.fillRoundedRectangle (r, 4.0f);
        auto* l = currentLayer();
        if (l == nullptr) return;
        const int n = juce::jlimit (1, 32, l->step.numSteps);
        const float w = r.getWidth() / (float) n;
        for (int i = 0; i < n; ++i)
        {
            const float v = l->step.steps[(size_t) i];
            auto bar = juce::Rectangle<float> (r.getX() + i * w + 1.0f, r.getBottom() - v * r.getHeight(), w - 2.0f, v * r.getHeight());
            g.setColour (accent.withAlpha (i % 4 == 0 ? 0.95f : 0.7f));
            g.fillRect (bar);
        }
        g.setColour (Pal::line.withAlpha (0.4f));
        g.fillRect (r.getX(), r.getCentreY(), r.getWidth(), 1.0f);
    }
    void mouseDown (const juce::MouseEvent& e) override { set (e); }
    void mouseDrag (const juce::MouseEvent& e) override { set (e); }
private:
    void set (const juce::MouseEvent& e)
    {
        withLayer ([&] (cr::Layer& l)
        {
            const int n = juce::jlimit (1, 32, l.step.numSteps);
            const int i = juce::jlimit (0, n - 1, e.x * n / juce::jmax (1, getWidth()));
            l.step.steps[(size_t) i] = juce::jlimit (0.0f, 1.0f, 1.0f - (float) e.y / (float) juce::jmax (1, getHeight()));
        });
        repaint();
    }
};

class ModulationsPage : public juce::Component
{
public:
    ModulationsPage()
    {
        addBtn.onClick = [this]
        {
            withLayer ([] (cr::Layer& l) { if (l.mods.size() < 24) l.mods.push_back ({}); });
            refresh();
        };
        addAndMakeVisible (addBtn);
        addAndMakeVisible (routesView);
        routesView.setViewedComponent (&routesContent, false);
        routesView.setScrollBarsShown (true, false);
        modTabs.setTabs ({ "LFO 1", "LFO 2", "Mod envelope", "Step modulator" });
        modTabs.onChange = [this] (int) { refresh(); };
        addAndMakeVisible (modTabs);

        shapeField.choices = cr::lfoShapeNames();
        shapeField.getIndex = [this] { auto* l = currentLayer(); return l ? (int) l->lfo[(size_t) lfoIndex()].shape : 0; };
        shapeField.setIndex = [this] (int i) { const int k = lfoIndex(); withLayer ([i, k] (cr::Layer& l) { l.lfo[(size_t) k].shape = (cr::LfoShape) i; }); shape.repaint(); };
        syncField.choices = { "Free (Hz)", "Tempo sync" };
        syncField.getIndex = [this] { auto* l = currentLayer(); return l && l->lfo[(size_t) lfoIndex()].sync ? 1 : 0; };
        syncField.setIndex = [this] (int i) { const int k = lfoIndex(); withLayer ([i, k] (cr::Layer& l) { l.lfo[(size_t) k].sync = i == 1; }); refresh(); };
        divField.choices = syncNames();
        divField.getIndex = [this] { auto* l = currentLayer(); return l ? l->lfo[(size_t) lfoIndex()].syncIndex : 0; };
        divField.setIndex = [this] (int i) { const int k = lfoIndex(); withLayer ([i, k] (cr::Layer& l) { l.lfo[(size_t) k].syncIndex = i; }); };
        retrigField.choices = { "Poly (free)", "Mono (retrigger)" };
        retrigField.getIndex = [this] { auto* l = currentLayer(); return l && l->lfo[(size_t) lfoIndex()].retrigger ? 1 : 0; };
        retrigField.setIndex = [this] (int i) { const int k = lfoIndex(); withLayer ([i, k] (cr::Layer& l) { l.lfo[(size_t) k].retrigger = i == 1; }); };
        stepsStep.getText = [] { auto* l = currentLayer(); return l ? juce::String (l->step.numSteps) : juce::String ("-"); };
        stepsStep.onStep = [this] (int d) { withLayer ([d] (cr::Layer& l) { l.step.numSteps = juce::jlimit (1, 32, l.step.numSteps + d); }); steps.repaint(); };
        stepRate.choices = syncNames();
        stepRate.getIndex = [] { auto* l = currentLayer(); return l ? l->step.syncIndex : 0; };
        stepRate.setIndex = [] (int i) { withLayer ([i] (cr::Layer& l) { l.step.syncIndex = i; }); };
        stepRetrig.choices = { "Free", "Retrigger" };
        stepRetrig.getIndex = [] { auto* l = currentLayer(); return l && l->step.retrigger ? 1 : 0; };
        stepRetrig.setIndex = [] (int i) { withLayer ([i] (cr::Layer& l) { l.step.retrigger = i == 1; }); };
        for (auto* c : std::initializer_list<juce::Component*> { &shapeField, &syncField, &divField, &retrigField, &rate, &shape,
                                                              &mA, &mH, &mD, &mS, &mR, &modEnvGraph, &steps, &stepsStep, &stepRate, &stepRetrig, &smooth })
            addChildComponent (c);
        modEnvGraph.getADSR = [] () -> std::array<float, 5>
        {
            auto* l = currentLayer();
            if (l == nullptr) return { 0.01f, 0.0f, 0.3f, 0.0f, 0.2f };
            return { l->modEnv.attack, l->modEnv.hold, l->modEnv.decay, l->modEnv.sustain, l->modEnv.release };
        };
    }

    int lfoIndex() const { return juce::jlimit (0, 1, modTabs.getSelected()); }

    void refresh()
    {
        const auto acc = accentColour();
        modTabs.setAccent (acc);
        // routes
        rows.clear();
        auto* l = currentLayer();
        if (l != nullptr)
        {
            for (int i = 0; i < (int) l->mods.size(); ++i)
            {
                auto* row = rows.add (new RouteRow (i, acc));
                row->onRemove = [this] { juce::MessageManager::callAsync ([this] { refresh(); }); };
                routesContent.addAndMakeVisible (row);
            }
        }
        addBtn.setEnabled (l != nullptr);
        // modulator editors
        const int tab = modTabs.getSelected();
        for (auto* c : std::initializer_list<juce::Component*> { &shapeField, &syncField, &divField, &retrigField, &rate, &shape }) c->setVisible (tab <= 1 && l);
        for (auto* c : std::initializer_list<juce::Component*> { &mA, &mH, &mD, &mS, &mR, &modEnvGraph }) c->setVisible (tab == 2 && l);
        for (auto* c : std::initializer_list<juce::Component*> { &steps, &stepsStep, &stepRate, &stepRetrig, &smooth }) c->setVisible (tab == 3 && l);
        if (l != nullptr)
        {
            const int k = lfoIndex();
            const bool sync = l->lfo[(size_t) k].sync;
            rate.setVisible (tab <= 1 && ! sync);
            divField.setVisible (tab <= 1 && sync);
            rate.setAccent (acc);
            rate.setParam (layerPath (selBank(), selPad(), selLayer(), k == 0 ? cr::lfLfo1Rate : cr::lfLfo2Rate), "Rate");
            shape.lfo = k;
            shape.accent = acc;
            auto envKnob = [acc] (Knob& kn, float* field, float mn, float mx, float def, bool isLevel)
            {
                kn.setAccent (acc);
                kn.setCustom (mn, mx, def, [field] { return *field; }, [field] (float v) { *field = v; },
                              [isLevel] (float v) { return isLevel ? juce::String (juce::roundToInt (v * 100.0f)) + "%" : (v < 1.0f ? juce::String (juce::roundToInt (v * 1000.0f)) + " ms" : juce::String (v, 2) + " s"); },
                              ! isLevel && mn > 0.0f);
            };
            envKnob (mA, &l->modEnv.attack, 0.001f, 10.0f, 0.005f, false);
            envKnob (mH, &l->modEnv.hold, 0.0f, 10.0f, 0.0f, false);
            envKnob (mD, &l->modEnv.decay, 0.005f, 20.0f, 0.3f, false);
            envKnob (mS, &l->modEnv.sustain, 0.0f, 1.0f, 0.0f, true);
            envKnob (mR, &l->modEnv.release, 0.001f, 20.0f, 0.2f, false);
            for (auto* kn : { &mA, &mH, &mD, &mS, &mR }) kn->onUserChange = [this] { modEnvGraph.repaint(); };
            modEnvGraph.accent = acc;
            steps.accent = acc;
            smooth.setAccent (acc);
            smooth.setCustom (0.0f, 0.95f, 0.0f, [l] { return l->step.smooth; }, [l] (float v) { l->step.smooth = v; },
                              [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; });
        }
        for (auto* f : { &shapeField, &syncField, &divField, &retrigField, &stepRate, &stepRetrig }) f->accent = acc;
        resized();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Pal::bg);
        drawSection (g, routesArea, "Modulation matrix");
        drawSection (g, editorArea, "");
        if (rows.isEmpty())
        {
            g.setColour (Pal::textFaint);
            g.setFont (Fonts::ui (13.0f));
            g.drawFittedText (currentLayer() == nullptr ? "Load a sample to modulate it." : "No modulations yet. Add one to route a source (LFO, envelope, velocity, a macro...) to a destination.",
                              routesView.getBounds().reduced (10), juce::Justification::centredTop, 3);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14, 12);
        routesArea = r.removeFromLeft (r.getWidth() * 52 / 100);
        r.removeFromLeft (12);
        editorArea = r;
        auto ra = routesArea.reduced (10, 8);
        ra.removeFromTop (22);
        addBtn.setBounds (ra.removeFromBottom (32));
        ra.removeFromBottom (8);
        routesView.setBounds (ra);
        int y = 0;
        for (auto* row : rows) { row->setBounds (0, y, ra.getWidth() - 8, 56); y += 60; }
        routesContent.setSize (ra.getWidth() - 8, juce::jmax (y, 10));

        auto ea = editorArea.reduced (10, 8);
        modTabs.setBounds (ea.removeFromTop (30));
        ea.removeFromTop (12);
        auto fields = ea.removeFromTop (44);
        const int fw = fields.getWidth() / 4;
        shapeField.setBounds (fields.removeFromLeft (fw).reduced (4, 0));
        syncField.setBounds (fields.removeFromLeft (fw).reduced (4, 0));
        auto rateSlot = fields.removeFromLeft (fw).reduced (4, 0);
        divField.setBounds (rateSlot);
        retrigField.setBounds (fields.reduced (4, 0));
        rate.setBounds (rateSlot.withHeight (70));
        ea.removeFromTop (30);
        shape.setBounds (ea.removeFromTop (juce::jmin (180, ea.getHeight())));

        auto me = editorArea.reduced (10, 8).withTrimmedTop (42);
        auto knobs = me.removeFromBottom (70);
        modEnvGraph.setBounds (me.withTrimmedBottom (10));
        const int kw = knobs.getWidth() / 5;
        for (auto* k : { &mA, &mH, &mD, &mS, &mR }) k->setBounds (knobs.removeFromLeft (kw));

        auto st = editorArea.reduced (10, 8).withTrimmedTop (42);
        auto sf = st.removeFromTop (70);
        const int sw = sf.getWidth() / 4;
        stepsStep.setBounds (sf.removeFromLeft (sw).reduced (4, 10));
        stepRate.setBounds (sf.removeFromLeft (sw).reduced (4, 10));
        stepRetrig.setBounds (sf.removeFromLeft (sw).reduced (4, 10));
        smooth.setBounds (sf.reduced (4, 0));
        st.removeFromTop (8);
        steps.setBounds (st);
    }

private:
    class RouteRow : public juce::Component
    {
    public:
        RouteRow (int idx, juce::Colour acc) : index (idx)
        {
            source.choices = cr::modSourceNames();
            source.accent = acc;
            source.getIndex = [this] { auto* l = currentLayer(); return l && index < (int) l->mods.size() ? (int) l->mods[(size_t) index].source : 0; };
            source.setIndex = [this] (int i) { withLayer ([this, i] (cr::Layer& l) { if (index < (int) l.mods.size()) l.mods[(size_t) index].source = (cr::ModSource) i; }); };
            target.choices = cr::modTargetNames();
            target.accent = acc;
            target.getIndex = [this] { auto* l = currentLayer(); return l && index < (int) l->mods.size() ? (int) l->mods[(size_t) index].target : 0; };
            target.setIndex = [this] (int i) { withLayer ([this, i] (cr::Layer& l) { if (index < (int) l.mods.size()) l.mods[(size_t) index].target = (cr::ModTarget) i; }); };
            amount.setCompact (true);
            amount.setAccent (acc);
            amount.setCustom (-1.0f, 1.0f, 0.0f,
                              [this] { auto* l = currentLayer(); return l && index < (int) l->mods.size() ? l->mods[(size_t) index].amount : 0.0f; },
                              [this] (float v) { withLayer ([this, v] (cr::Layer& l) { if (index < (int) l.mods.size()) l.mods[(size_t) index].amount = v; }); },
                              [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; });
            bypass.accent = acc;
            bypass.setToggleState (currentLayer() && index < (int) currentLayer()->mods.size() && ! currentLayer()->mods[(size_t) index].bypass, juce::dontSendNotification);
            bypass.onClick = [this] { withLayer ([this] (cr::Layer& l) { if (index < (int) l.mods.size()) l.mods[(size_t) index].bypass = ! bypass.getToggleState(); }); };
            remove.onClick = [this]
            {
                withLayer ([this] (cr::Layer& l) { if (index < (int) l.mods.size()) l.mods.erase (l.mods.begin() + index); });
                if (onRemove) onRemove();
            };
            for (auto* c : std::initializer_list<juce::Component*> { &source, &target, &amount, &bypass, &remove }) addAndMakeVisible (c);
        }
        std::function<void()> onRemove;
        void paint (juce::Graphics& g) override
        {
            g.setColour (Pal::surface);
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);
            drawIcon (g, Icon::right, arrow.toFloat().reduced (4.0f), Pal::textFaint);
        }
        void resized() override
        {
            auto r = getLocalBounds().reduced (8, 4);
            bypass.setBounds (r.removeFromLeft (24).withSizeKeepingCentre (24, 24));
            remove.setBounds (r.removeFromRight (28).withSizeKeepingCentre (28, 28));
            amount.setBounds (r.removeFromRight (70));
            const int w = r.getWidth() / 2;
            source.setBounds (r.removeFromLeft (w - 12).reduced (4, 4));
            arrow = r.removeFromLeft (24);
            target.setBounds (r.reduced (4, 4));
        }
    private:
        int index;
        ChoiceField source { "Source" }, target { "Destination" };
        Knob amount { "Amount" };
        PowerButton bypass;
        IconButton remove { Icon::trash, "Remove this modulation" };
        juce::Rectangle<int> arrow;
    };

    juce::TextButton addBtn { "+ Add modulation" };
    juce::Viewport routesView;
    juce::Component routesContent;
    juce::OwnedArray<RouteRow> rows;
    TabStrip modTabs;
    ChoiceField shapeField { "Shape" }, syncField { "Rate mode" }, divField { "Rate" }, retrigField { "Trigger" }, stepRate { "Rate" }, stepRetrig { "Trigger" };
    Knob rate { "Rate" }, mA { "Attack" }, mH { "Hold" }, mD { "Decay" }, mS { "Sustain" }, mR { "Release" }, smooth { "Smooth" };
    LfoShapeView shape;
    EnvelopeGraph modEnvGraph;
    StepEditor steps;
    Stepper stepsStep { "Steps" };
    juce::Rectangle<int> routesArea, editorArea;
};

// ============================================================================
// Mapping page: zones on a key x velocity map
// ============================================================================
class ZoneMap : public juce::Component
{
public:
    std::function<void()> onChange;
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Pal::bg);
        g.fillRoundedRectangle (r, 4.0f);
        const float kw = r.getWidth() / 128.0f;
        for (int k = 0; k < 128; ++k)
        {
            const bool black = juce::MidiMessage::isMidiNoteBlack (k);
            if (black) { g.setColour (Pal::panel); g.fillRect (r.getX() + k * kw, r.getY(), kw, r.getHeight()); }
            if (k % 12 == 0)
            {
                g.setColour (Pal::outline);
                g.fillRect (r.getX() + k * kw, r.getY(), 1.0f, r.getHeight());
                drawCaps (g, noteName (k), juce::Rectangle<float> (r.getX() + k * kw + 3.0f, r.getBottom() - 16.0f, 40.0f, 14.0f).toNearestInt(), juce::Justification::centredLeft, Pal::textFaint, 9.0f);
            }
        }
        auto& pad = currentPad();
        const auto acc = accentColour();
        for (int l = 0; l < (int) pad.layers.size(); ++l)
            for (int z = 0; z < (int) pad.layers[(size_t) l].zones.size(); ++z)
            {
                const auto& zone = pad.layers[(size_t) l].zones[(size_t) z];
                auto zr = zoneRect (zone);
                const bool sel = l == selLayer() && z == selZone();
                const auto c = acc.withRotatedHue ((float) l * 0.11f);
                g.setColour (c.withAlpha (sel ? 0.55f : 0.25f));
                g.fillRect (zr);
                g.setColour (sel ? juce::Colours::white : c);
                g.drawRect (zr, sel ? 2.0f : 1.0f);
                g.setColour (Pal::text);
                g.setFont (Fonts::ui (10.5f, true));
                if (zr.getWidth() > 40) g.drawFittedText (zone.sample ? zone.sample->name : juce::String ("-"), zr.reduced (4.0f).toNearestInt(), juce::Justification::topLeft, 2);
                // root key marker
                const float rx = r.getX() + (zone.rootKey + 0.5f) * kw;
                g.setColour (Pal::accent);
                g.fillEllipse (rx - 3.0f, zr.getBottom() - 8.0f, 6.0f, 6.0f);
            }
        const int last = Engine::get().lastPlayedKey.load();
        g.setColour (Pal::text.withAlpha (0.5f));
        g.fillRect (r.getX() + last * kw, r.getY(), kw, 3.0f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto& pad = currentPad();
        dragMode = -1;
        for (int l = (int) pad.layers.size() - 1; l >= 0 && dragMode < 0; --l)
            for (int z = (int) pad.layers[(size_t) l].zones.size() - 1; z >= 0; --z)
            {
                auto zr = zoneRect (pad.layers[(size_t) l].zones[(size_t) z]);
                if (! zr.expanded (4.0f).contains (e.position)) continue;
                Engine::get().selectedLayer = l;
                Engine::get().selectedZone = z;
                const float edge = 6.0f;
                if (std::abs (e.position.x - zr.getX()) < edge) dragMode = 1;
                else if (std::abs (e.position.x - zr.getRight()) < edge) dragMode = 2;
                else if (std::abs (e.position.y - zr.getY()) < edge) dragMode = 3;
                else if (std::abs (e.position.y - zr.getBottom()) < edge) dragMode = 4;
                else dragMode = 0;
                Engine::get().checkpointPad (selBank(), selPad());
                if (auto* zn = zone()) start = *zn;
                break;
            }
        if (onChange) onChange();
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto* zn = zone();
        if (zn == nullptr || dragMode < 0) return;
        const float kw = (float) getWidth() / 128.0f;
        const int dk = juce::roundToInt ((float) e.getDistanceFromDragStartX() / kw);
        const int dv = -juce::roundToInt ((float) e.getDistanceFromDragStartY() / (float) getHeight() * 127.0f);
        const juce::ScopedLock sl (Engine::get().getLock());
        switch (dragMode)
        {
            case 0:
            {
                const int span = start.highKey - start.lowKey;
                zn->lowKey = juce::jlimit (0, 127 - span, start.lowKey + dk);
                zn->highKey = zn->lowKey + span;
                zn->rootKey = juce::jlimit (0, 127, start.rootKey + (zn->lowKey - start.lowKey));
                break;
            }
            case 1: zn->lowKey = juce::jlimit (0, zn->highKey, start.lowKey + dk); break;
            case 2: zn->highKey = juce::jlimit (zn->lowKey, 127, start.highKey + dk); break;
            case 3: zn->highVel = juce::jlimit (zn->lowVel, 127, start.highVel + dv); break;
            case 4: zn->lowVel = juce::jlimit (1, zn->highVel, start.lowVel + dv); break;
            default: break;
        }
        repaint();
        if (onChange) onChange();
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        auto* zn = zone();
        juce::MouseCursor c = juce::MouseCursor::NormalCursor;
        if (zn != nullptr)
        {
            auto zr = zoneRect (*zn);
            if (std::abs (e.position.x - zr.getX()) < 6.0f || std::abs (e.position.x - zr.getRight()) < 6.0f) c = juce::MouseCursor::LeftRightResizeCursor;
            else if (std::abs (e.position.y - zr.getY()) < 6.0f || std::abs (e.position.y - zr.getBottom()) < 6.0f) c = juce::MouseCursor::UpDownResizeCursor;
        }
        setMouseCursor (c);
    }

private:
    cr::Zone* zone()
    {
        auto* l = currentLayer();
        const int z = selZone();
        return (l != nullptr && z >= 0) ? &l->zones[(size_t) z] : nullptr;
    }
    juce::Rectangle<float> zoneRect (const cr::Zone& z) const
    {
        auto r = getLocalBounds().toFloat().withTrimmedBottom (18.0f);
        const float kw = r.getWidth() / 128.0f;
        const float y0 = r.getBottom() - (float) z.highVel / 127.0f * r.getHeight();
        const float y1 = r.getBottom() - (float) (z.lowVel - 1) / 127.0f * r.getHeight();
        return { r.getX() + z.lowKey * kw, y0, (z.highKey - z.lowKey + 1) * kw, y1 - y0 };
    }
    int dragMode = -1;
    cr::Zone start;
};

class MappingPage : public juce::Component
{
public:
    MappingPage()
    {
        auto zoneInt = [] (int cr::Zone::* member, int lo, int hi, bool asNote)
        {
            return std::make_pair ([member, asNote] () -> juce::String
            {
                auto* l = currentLayer();
                const int z = selZone();
                if (l == nullptr || z < 0) return "-";
                const int v = l->zones[(size_t) z].*member;
                return asNote ? noteName (v) : juce::String (v);
            },
            [member, lo, hi] (int d)
            {
                withLayer ([&] (cr::Layer& l)
                {
                    const int z = selZone();
                    if (z < 0) return;
                    auto& zn = l.zones[(size_t) z];
                    zn.*member = juce::jlimit (lo, hi, zn.*member + d);
                    zn.highKey = juce::jmax (zn.highKey, zn.lowKey);
                    zn.highVel = juce::jmax (zn.highVel, zn.lowVel);
                });
            });
        };
        auto bindStep = [&] (Stepper& s, int cr::Zone::* member, int lo, int hi, bool note)
        {
            auto p = zoneInt (member, lo, hi, note);
            s.getText = p.first;
            auto fn = p.second;
            s.onStep = [this, fn] (int d) { fn (d); map.repaint(); };
            addAndMakeVisible (s);
        };
        bindStep (rootStep, &cr::Zone::rootKey, 0, 127, true);
        bindStep (lowKeyStep, &cr::Zone::lowKey, 0, 127, true);
        bindStep (highKeyStep, &cr::Zone::highKey, 0, 127, true);
        bindStep (lowVelStep, &cr::Zone::lowVel, 1, 127, false);
        bindStep (highVelStep, &cr::Zone::highVel, 1, 127, false);
        pitchField.choices = { "Fixed", "Follows keys" };
        pitchField.getIndex = [] { auto* l = currentLayer(); const int z = selZone(); return l && z >= 0 && l->zones[(size_t) z].pitchTrack ? 1 : 0; };
        pitchField.setIndex = [] (int i) { withLayer ([i] (cr::Layer& l) { const int z = selZone(); if (z >= 0) l.zones[(size_t) z].pitchTrack = i == 1; }); };
        addAndMakeVisible (pitchField);
        learnBtn.setClickingTogglesState (true);
        learnBtn.onClick = [this]
        {
            auto& e = Engine::get();
            e.mappingLearnArmed = learnBtn.getToggleState();
            e.onMappingLearnNote = [this] (int note)
            {
                withLayer ([note] (cr::Layer& l) { const int z = selZone(); if (z >= 0) l.zones[(size_t) z].rootKey = note; });
                Engine::get().mappingLearnArmed = false;
                learnBtn.setToggleState (false, juce::dontSendNotification);
                map.repaint();
                repaint();
            };
        };
        addAndMakeVisible (learnBtn);
        spreadBtn.onClick = []
        {
            // spread the layer's zones over consecutive keys starting at their first root
            withLayer ([] (cr::Layer& l)
            {
                if (l.zones.empty()) return;
                const int base = l.zones[0].lowKey == 0 ? 48 : l.zones[0].lowKey;
                for (int i = 0; i < (int) l.zones.size(); ++i)
                {
                    auto& z = l.zones[(size_t) i];
                    z.lowKey = z.highKey = z.rootKey = juce::jlimit (0, 127, base + i);
                }
            });
            ViewBase::broadcast();
        };
        fullBtn.onClick = []
        {
            withLayer ([] (cr::Layer& l) { const int z = selZone(); if (z >= 0) { auto& zn = l.zones[(size_t) z]; zn.lowKey = 0; zn.highKey = 127; zn.lowVel = 1; zn.highVel = 127; } });
            ViewBase::broadcast();
        };
        addAndMakeVisible (spreadBtn);
        addAndMakeVisible (fullBtn);
        map.onChange = [this] { repaint(); for (auto* s : { &rootStep, &lowKeyStep, &highKeyStep, &lowVelStep, &highVelStep }) s->repaint(); };
        addAndMakeVisible (map);
        addAndMakeVisible (zoneTune);
        addAndMakeVisible (zoneGain);
    }

    void refresh()
    {
        const auto acc = accentColour();
        pitchField.accent = acc;
        auto* l = currentLayer();
        const int z = selZone();
        auto zoneFloat = [&] (Knob& k, float cr::Zone::* member, float lo, float hi, const juce::String& unit)
        {
            k.setAccent (acc);
            k.setCustom (lo, hi, 0.0f,
                         [member] { auto* ly = currentLayer(); const int zi = selZone(); return ly && zi >= 0 ? ly->zones[(size_t) zi].*member : 0.0f; },
                         [member] (float v) { withLayer ([member, v] (cr::Layer& ly) { const int zi = selZone(); if (zi >= 0) ly.zones[(size_t) zi].*member = v; }); },
                         [unit] (float v) { return juce::String (v, 1) + unit; });
            k.setEnabled (l != nullptr && z >= 0);
        };
        zoneFloat (zoneTune, &cr::Zone::tune, -24.0f, 24.0f, " st");
        zoneFloat (zoneGain, &cr::Zone::gainDb, -24.0f, 12.0f, " dB");
        map.repaint();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Pal::bg);
        drawCaps (g, "Drag a zone to move it, drag its edges to set the key and velocity range. Velocity goes up the side.", hint, juce::Justification::centredLeft, Pal::textFaint, 9.5f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14, 12);
        auto fields = r.removeFromTop (54);
        const int w = fields.getWidth() / 9;
        for (auto* c : std::initializer_list<juce::Component*> { &rootStep, &lowKeyStep, &highKeyStep, &lowVelStep, &highVelStep })
            c->setBounds (fields.removeFromLeft (w).reduced (4, 6));
        learnBtn.setBounds (fields.removeFromLeft (w).reduced (4, 14));
        pitchField.setBounds (fields.removeFromLeft (w).reduced (4, 6));
        zoneTune.setBounds (fields.removeFromLeft (w).withHeight (66));
        zoneGain.setBounds (fields.removeFromLeft (w).withHeight (66));
        r.removeFromTop (16);
        auto btnRow = r.removeFromTop (30);
        fullBtn.setBounds (btnRow.removeFromRight (110));
        btnRow.removeFromRight (6);
        spreadBtn.setBounds (btnRow.removeFromRight (110));
        r.removeFromTop (6);
        hint = btnRow;
        r.removeFromTop (4);
        map.setBounds (r);
    }

private:
    Stepper rootStep { "Root key" }, lowKeyStep { "Low key" }, highKeyStep { "High key" }, lowVelStep { "Low vel" }, highVelStep { "High vel" };
    ChoiceField pitchField { "Pitch" };
    juce::TextButton learnBtn { "Learn root" }, spreadBtn { "Spread keys" }, fullBtn { "Full range" };
    Knob zoneTune { "Zone tune" }, zoneGain { "Zone gain" };
    ZoneMap map;
    juce::Rectangle<int> hint;
};

// ============================================================================
// Plugin page (instrument on the pad)
// ============================================================================
class PluginPage : public juce::Component
{
public:
    PluginPage()
    {
        chooseBtn.onClick = [this]
        {
            showEffectMenu (&chooseBtn, [] (const juce::String&, int i)
            {
                auto& e = Engine::get();
                juce::String err;
                const auto list = e.plugins.getInstruments();
                if (i >= 0 && i < list.size() && ! e.setPadInstrument (selBank(), selPad(), list[i], err)) showError ("Plugin", err);
                ViewBase::broadcast();
            }, true);
        };
        openBtn.onClick = [] { if (auto& inst = currentPad().instrument) PluginWindow::show (*inst); };
        removeBtn.onClick = [] { Engine::get().clearPadInstrument (selBank(), selPad()); ViewBase::broadcast(); };
        recordBtn.onClick = []
        {
            auto& e = Engine::get();
            e.recorder.source = 2 + selBank();
            showRecorderDialog (selBank(), juce::jmin (cr::numPads - 1, selPad() + 1));
        };
        scanBtn.onClick = [] { showSettingsDialog (3); };
        prevPage.onClick = [this] { paramPage = juce::jmax (0, paramPage - 1); refresh(); };
        nextPage.onClick = [this] { paramPage = paramPage + 1; refresh(); };
        for (auto* c : std::initializer_list<juce::Component*> { &chooseBtn, &openBtn, &removeBtn, &recordBtn, &scanBtn, &prevPage, &nextPage }) addAndMakeVisible (c);
        for (int i = 0; i < 16; ++i) { auto* k = knobs.add (new Knob()); addChildComponent (k); }
    }

    void refresh()
    {
        auto& pad = currentPad();
        const bool has = pad.instrument != nullptr;
        openBtn.setVisible (has);
        removeBtn.setVisible (has);
        recordBtn.setVisible (has);
        prevPage.setVisible (has);
        nextPage.setVisible (has);
        chooseBtn.setButtonText (has ? "Replace instrument..." : "Choose an instrument plugin...");
        const int n = has ? pad.instrument->getParameters().size() : 0;
        const int pages = juce::jmax (1, (n + 15) / 16);
        paramPage = juce::jlimit (0, pages - 1, paramPage);
        for (int i = 0; i < 16; ++i)
        {
            const int p = paramPage * 16 + i;
            knobs[i]->setVisible (has && p < n);
            if (has && p < n)
            {
                knobs[i]->setAccent (accentColour());
                knobs[i]->setParam ("inst/" + juce::String (selBank()) + "/" + juce::String (selPad()) + "/" + juce::String (p));
            }
        }
        resized();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Pal::bg);
        auto& pad = currentPad();
        auto r = getLocalBounds().reduced (14, 12);
        if (pad.instrument)
        {
            g.setColour (Pal::text);
            g.setFont (Fonts::ui (18.0f, true));
            g.drawText (pad.instrument->getName(), r.removeFromTop (30), juce::Justification::centredLeft);
            const int n = pad.instrument->getParameters().size();
            drawCaps (g, juce::String (n) + " parameters  -  page " + juce::String (paramPage + 1) + " of " + juce::String (juce::jmax (1, (n + 15) / 16))
                         + "  -  right-click a knob to MIDI-learn, automate or assign it to a macro", r.removeFromTop (18), juce::Justification::centredLeft, Pal::textFaint, 9.5f);
        }
        else
        {
            g.setColour (Pal::textDim);
            g.setFont (Fonts::ui (14.0f));
            g.drawFittedText ("Host a VST3" + juce::String ((juce::SystemStats::getOperatingSystemType() & juce::SystemStats::MacOSX) != 0 ? " or Audio Unit" : "") + " instrument on this pad. The pad switches to Keys mode so it plays chromatically, "
                              "and the pad's channel, sends and effects process the instrument's output.\n\nPlugins are found by the scan in Settings > Plugins.",
                              r.removeFromTop (120).withTrimmedTop (60), juce::Justification::topLeft, 6);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14, 12);
        auto top = r.removeFromTop (34);
        if (currentPad().instrument)
        {
            removeBtn.setBounds (top.removeFromRight (90));
            top.removeFromRight (6);
            recordBtn.setBounds (top.removeFromRight (150));
            top.removeFromRight (6);
            openBtn.setBounds (top.removeFromRight (120));
            top.removeFromRight (6);
            chooseBtn.setBounds (top.removeFromRight (170));
            r.removeFromTop (34);
            auto pageRow = r.removeFromBottom (34);
            prevPage.setBounds (pageRow.removeFromLeft (34));
            nextPage.setBounds (pageRow.removeFromLeft (34));
            const int w = r.getWidth() / 8;
            for (int i = 0; i < 16; ++i) knobs[i]->setBounds (r.getX() + (i % 8) * w, r.getY() + (i / 8) * 92, w, 84);
        }
        else
        {
            chooseBtn.setBounds (top.removeFromLeft (260));
            top.removeFromLeft (8);
            scanBtn.setBounds (top.removeFromLeft (160));
        }
        scanBtn.setVisible (currentPad().instrument == nullptr);
    }

private:
    juce::TextButton chooseBtn { "Choose an instrument plugin..." }, openBtn { "Open plugin" }, removeBtn { "Remove" },
                     recordBtn { "Record output to a pad..." }, scanBtn { "Scan for plugins..." };
    IconButton prevPage { Icon::left, "Previous parameters" }, nextPage { Icon::right, "Next parameters" };
    juce::OwnedArray<Knob> knobs;
    int paramPage = 0;
};

// ============================================================================
// Editor view
// ============================================================================
class EditorView : public ViewBase, public juce::FileDragAndDropTarget, public juce::DragAndDropTarget,
                   private juce::MidiKeyboardState::Listener, private juce::Timer
{
public:
    EditorView()
    {
        bankMenuBtn.onClick = [this] { showBanksMenu (&bankMenuBtn); };
        addAndMakeVisible (bankMenuBtn);
        miniPads.onMenu = [this] { padMenu(); };
        addAndMakeVisible (miniPads);
        padPrev.onClick = [this] { miniPads.page = juce::jmax (0, miniPads.page - 1); refresh(); };
        padNext.onClick = [this] { miniPads.page = juce::jmin (7, miniPads.page + 1); refresh(); };
        addAndMakeVisible (padPrev);
        addAndMakeVisible (padNext);
        padMenuBtn.onClick = [this] { padMenu(); };
        addAndMakeVisible (padMenuBtn);
        leftTabs.setTabs ({ "Channel", "Samples" });
        leftTabs.onChange = [this] (int) { updateLeft(); resized(); repaint(); };
        addAndMakeVisible (leftTabs);

        // channel
        for (int i = 0; i < 2 + cr::numAux; ++i) { auto* k = channelKnobs.add (new Knob()); addChildComponent (k); }
        chokeStep.getText = [] { const int c = currentPad().choke; return c == 0 ? juce::String ("None") : juce::String (c); };
        chokeStep.onStep = [] (int d) { auto& p = currentPad(); p.choke = juce::jlimit (0, 16, p.choke + d); };
        linkStep.getText = [] { const int c = currentPad().link; return c == 0 ? juce::String ("None") : juce::String (c); };
        linkStep.onStep = [] (int d) { auto& p = currentPad(); p.link = juce::jlimit (0, 16, p.link + d); };
        noteStep.getText = [] { return noteName (currentPad().padNote); };
        noteStep.onStep = [] (int d) { auto& p = currentPad(); p.padNote = juce::jlimit (0, 127, p.padNote + d); };
        keysField.choices = { "Off", "On" };
        keysField.getIndex = [] { return currentPad().keysMode ? 1 : 0; };
        keysField.setIndex = [] (int i) { currentPad().keysMode = i == 1; ViewBase::broadcast(); };
        outputField.getText = [] { const int o = currentPad().channel.output; return o == 0 ? juce::String ("Bank") : Engine::get().getOutputPairNames()[o]; };
        outputField.onClick = [this]
        {
            juce::PopupMenu m;
            const auto outs = Engine::get().getOutputPairNames();
            m.addItem (1, "Through the bank", true, currentPad().channel.output == 0);
            for (int i = 1; i < outs.size(); ++i) m.addItem (1 + i, outs[i], true, currentPad().channel.output == i);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&outputField), [this] (int r) { if (r > 0) { currentPad().channel.output = r - 1; outputField.repaint(); } });
        };
        midiBtn.onClick = [this] { showPadMidiSetup (selBank(), selPad(), &midiBtn); };
        for (auto* c : std::initializer_list<juce::Component*> { &chokeStep, &linkStep, &noteStep, &keysField, &outputField, &midiBtn }) addChildComponent (c);

        // samples
        layersView.setViewedComponent (&layerList, false);
        layersView.setScrollBarsShown (true, false);
        layerList.onChanged = [this] { refreshPages(); };
        addChildComponent (layersView);
        addLayerBtn.onClick = [] { auto& e = Engine::get(); e.checkpointPad (selBank(), selPad()); e.selectedLayer = e.addLayer (selBank(), selPad()); ViewBase::broadcast(); };
        loadBtn.onClick = [this]
        {
            chooser = makeFileChooser ("Load samples", Engine::getSamplesFolder(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                                  [] (const juce::FileChooser& fc)
                                  {
                                      auto& e = Engine::get();
                                      if (fc.getResults().isEmpty()) return;
                                      e.checkpointPad (selBank(), selPad());
                                      bool first = true;
                                      for (auto& f : fc.getResults())
                                      {
                                          e.loadSampleToPad (f, selBank(), selPad(), first && currentPad().layers.empty() ? Engine::LoadMethod::replace : Engine::LoadMethod::addToLayer);
                                          first = false;
                                      }
                                      ViewBase::broadcast();
                                  });
        };
        recordBtn.onClick = [] { showRecorderDialog (selBank(), selPad()); };
        for (auto* c : std::initializer_list<juce::Component*> { &addLayerBtn, &loadBtn, &recordBtn }) addChildComponent (c);

        // main pages
        pageTabs.setTabs ({ "Sampler", "Layer FX", "Modulations", "Mapping", "Plugin" });
        pageTabs.onChange = [this] (int) { showPage(); };
        addAndMakeVisible (pageTabs);
        addChildComponent (sampler);
        addChildComponent (layerFx);
        addChildComponent (modulations);
        addChildComponent (mapping);
        addChildComponent (plugin);

        keyboardState.addListener (this);
        keyboard.setAvailableRange (0, 127);
        keyboard.setLowestVisibleKey (36);
        keyboard.setKeyWidth (24.0f);
        keyboard.setScrollButtonsVisible (true);
        addAndMakeVisible (keyboard);

        showPage();
        startTimerHz (20);
        refresh();
    }

    ~EditorView() override { keyboardState.removeListener (this); }

    void refresh() override
    {
        auto& e = Engine::get();
        miniPads.page = juce::jlimit (0, 7, juce::jmax (miniPads.page, 0));
        if (selPad() / 16 != miniPads.page && ! miniPads.isMouseButtonDown()) miniPads.page = selPad() / 16;
        const int b = selBank(), p = selPad();
        const auto acc = accentColour();
        const char* names[] = { "Volume", "Pan" };
        for (int i = 0; i < channelKnobs.size(); ++i)
        {
            auto* k = channelKnobs[i];
            k->setAccent (acc);
            const juce::String base = "pad/" + juce::String (b) + "/" + juce::String (p) + "/";
            if (i < 2) k->setParam (base + (i == 0 ? "vol" : "pan"), names[i]);
            else k->setParam (base + "send" + juce::String (i - 2), e.session().aux[(size_t) (i - 2)].name);
        }
        keysField.accent = acc;
        leftTabs.setAccent (acc);
        pageTabs.setAccent (acc);
        keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, acc.withAlpha (0.8f));
        layerList.setSize (juce::jmax (100, layersView.getWidth() - 8), layerList.getContentHeight());
        updateLeft();
        refreshPages();
        resized();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto& e = Engine::get();
        g.fillAll (Pal::bg);
        g.setColour (Pal::panel);
        g.fillRect (leftArea);
        g.setColour (Pal::outline);
        g.fillRect (leftArea.getRight(), leftArea.getY(), 1, leftArea.getHeight());
        auto hdr = bankHeader;
        drawBankBadge (g, hdr.removeFromLeft (28).withSizeKeepingCentre (26, 26).toFloat(), e.selectedBank, true);
        hdr.removeFromLeft (10);
        g.setColour (Pal::text);
        g.setFont (Fonts::ui (14.0f, true));
        g.drawFittedText (currentBank().name, hdr.withTrimmedRight (30), juce::Justification::centredLeft, 1);
        drawCaps (g, juce::String (miniPads.page + 1) + "/8", padPageLabel, juce::Justification::centred, Pal::textFaint, 9.0f);

        auto ph = padHeader;
        auto badge = ph.removeFromLeft (30).withSizeKeepingCentre (30, 30).toFloat();
        const auto acc = accentColour();
        g.setColour (acc);
        g.fillRoundedRectangle (badge, 4.0f);
        g.setColour (acc.getPerceivedBrightness() > 0.6f ? Pal::bg : juce::Colours::white);
        g.setFont (Fonts::heavy (14.0f));
        g.drawText (juce::String (selPad() + 1), badge, juce::Justification::centred);
        ph.removeFromLeft (10);
        g.setColour (Pal::text);
        g.setFont (Fonts::ui (14.0f, true));
        const auto& pad = currentPad();
        g.drawFittedText (pad.name.isNotEmpty() ? pad.name : "Pad " + juce::String (selPad() + 1), ph.withTrimmedRight (30), juce::Justification::centredLeft, 1);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto kb = r.removeFromBottom (78);
        keyboard.setBounds (kb.withTrimmedLeft (0));
        leftArea = r.removeFromLeft (juce::jlimit (250, 300, getWidth() / 5));
        auto l = leftArea.reduced (12, 10);
        bankHeader = l.removeFromTop (30);
        bankMenuBtn.setBounds (bankHeader.removeFromRight (26).withSizeKeepingCentre (26, 26));
        l.removeFromTop (8);
        auto padsArea = l.removeFromTop (juce::jmin (l.getWidth() - 34, 200));
        auto arrows = padsArea.removeFromRight (30);
        padPrev.setBounds (arrows.removeFromBottom (30));
        padNext.setBounds (arrows.removeFromTop (30));
        padPageLabel = arrows;
        miniPads.setBounds (padsArea.withTrimmedRight (4));
        l.removeFromTop (10);
        padHeader = l.removeFromTop (32);
        padMenuBtn.setBounds (padHeader.removeFromRight (26).withSizeKeepingCentre (26, 26));
        l.removeFromTop (6);
        leftTabs.setBounds (l.removeFromTop (30));
        l.removeFromTop (10);
        if (leftTabs.getSelected() == 0)
        {
            const int kw = l.getWidth() / 2;
            const int sends = Engine::get().session().auxCount;
            for (int i = 0; i < channelKnobs.size(); ++i)
            {
                const bool show = i < 2 + sends;
                channelKnobs[i]->setVisible (show);
                if (! show) continue;
                const int row = i / 2, col = i % 2;
                channelKnobs[i]->setBounds (l.getX() + col * kw, l.getY() + row * 76, kw, 72);
            }
            l.removeFromTop (((2 + sends) + 1) / 2 * 76 + 6);
            auto row1 = l.removeFromTop (44);
            chokeStep.setBounds (row1.removeFromLeft (row1.getWidth() / 2).reduced (3, 0));
            linkStep.setBounds (row1.reduced (3, 0));
            l.removeFromTop (8);
            auto row2 = l.removeFromTop (44);
            noteStep.setBounds (row2.removeFromLeft (row2.getWidth() / 2).reduced (3, 0));
            keysField.setBounds (row2.reduced (3, 0));
            l.removeFromTop (8);
            auto row3 = l.removeFromTop (40);
            outputField.setBounds (row3.removeFromLeft (row3.getWidth() / 2).reduced (3, 0));
            midiBtn.setBounds (row3.reduced (3, 6));
        }
        else
        {
            auto buttons = l.removeFromBottom (32);
            const int bw = buttons.getWidth() / 3;
            addLayerBtn.setBounds (buttons.removeFromLeft (bw).reduced (2, 0));
            loadBtn.setBounds (buttons.removeFromLeft (bw).reduced (2, 0));
            recordBtn.setBounds (buttons.reduced (2, 0));
            l.removeFromBottom (8);
            layersView.setBounds (l);
            layerList.setSize (juce::jmax (100, l.getWidth() - 8), juce::jmax (l.getHeight(), layerList.getContentHeight()));
        }

        auto main = r;
        pageTabs.setBounds (main.removeFromTop (40).reduced (14, 4).withWidth (juce::jmin (640, main.getWidth() - 28)));
        for (auto* c : std::initializer_list<juce::Component*> { &sampler, &layerFx, &modulations, &mapping, &plugin }) c->setBounds (main);
        layerFx.setBounds (main.reduced (14, 12).withWidth (juce::jmin (main.getWidth() - 28, 900)));
    }

    // drag & drop onto the editor loads onto the selected pad
    bool isInterestedInFileDrag (const juce::StringArray& files) override { for (auto& f : files) if (isAudioFile (juce::File (f))) return true; return false; }
    void filesDropped (const juce::StringArray& files, int, int) override
    {
        auto& e = Engine::get();
        e.checkpointPad (selBank(), selPad());
        const bool empty = currentPad().layers.empty();
        for (int i = 0; i < files.size(); ++i)
            if (isAudioFile (juce::File (files[i])))
                e.loadSampleToPad (juce::File (files[i]), selBank(), selPad(), (empty && i == 0) ? Engine::LoadMethod::replace : Engine::LoadMethod::newLayer);
        ViewBase::broadcast();
    }
    bool isInterestedInDragSource (const SourceDetails& d) override { return isFileDrag (d) && isAudioFile (fileFromDrag (d)); }
    void itemDropped (const SourceDetails& d) override
    {
        auto& e = Engine::get();
        e.checkpointPad (selBank(), selPad());
        e.loadSampleToPad (fileFromDrag (d), selBank(), selPad(), currentPad().layers.empty() ? Engine::LoadMethod::replace : Engine::LoadMethod::newLayer);
        ViewBase::broadcast();
    }

private:
    void updateLeft()
    {
        const bool ch = leftTabs.getSelected() == 0;
        for (auto* k : channelKnobs) k->setVisible (ch);
        for (auto* c : std::initializer_list<juce::Component*> { &chokeStep, &linkStep, &noteStep, &keysField, &outputField, &midiBtn }) c->setVisible (ch);
        for (auto* c : std::initializer_list<juce::Component*> { &layersView, &addLayerBtn, &loadBtn, &recordBtn }) c->setVisible (! ch);
        layerList.repaint();
    }

    void refreshPages()
    {
        sampler.refresh();
        layerFx.setLayerFx (selBank(), selPad(), juce::jmax (0, selLayer()), accentColour());
        modulations.refresh();
        mapping.refresh();
        plugin.refresh();
        layerList.setSize (juce::jmax (100, layersView.getWidth() - 8), juce::jmax (layersView.getHeight(), layerList.getContentHeight()));
        layerList.repaint();
    }

    void showPage()
    {
        const int t = pageTabs.getSelected();
        sampler.setVisible (t == 0);
        layerFx.setVisible (t == 1);
        modulations.setVisible (t == 2);
        mapping.setVisible (t == 3);
        plugin.setVisible (t == 4);
        refreshPages();
    }

    void padMenu()
    {
        auto& e = Engine::get();
        const int b = selBank(), p = selPad();
        juce::PopupMenu m;
        m.addItem (1, "Rename pad...");
        m.addItem (2, "Pad colour...");
        m.addSeparator();
        m.addItem (3, "Copy pad");
        m.addItem (4, "Paste pad", e.hasPadClipboard());
        m.addItem (5, "Clear pad");
        m.addSeparator();
        m.addItem (6, "Record a sample...");
        m.addItem (8, "Separate stems to a new bank (vocals, drums, bass, other)", e.padSample (b, p) != nullptr);
        m.addItem (7, "MIDI setup...");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&padMenuBtn), [this, b, p] (int r)
        {
            auto& en = Engine::get();
            if (r == 1) askForText ("Pad name", currentPad().name, [b, p] (const juce::String& n) { Engine::get().session().banks[(size_t) b].pads[(size_t) p].name = n; ViewBase::broadcast(); });
            else if (r == 2) showPadColourMenu (b, p, &padMenuBtn);
            else if (r == 3) en.copyPad (b, p);
            else if (r == 4) en.pastePad (b, p);
            else if (r == 5) en.clearPad (b, p);
            else if (r == 6) showRecorderDialog (b, p);
            else if (r == 7) showPadMidiSetup (b, p, &padMenuBtn);
            else if (r == 8) { separateStemsFromPad (b, p); return; }
            ViewBase::broadcast();
        });
    }

    void timerCallback() override
    {
        if (! isShowing()) return;
        auto& e = Engine::get();
        bool any = false;
        const int b = selBank();
        for (int i = 0; i < 16; ++i)
        {
            const int p = miniPads.page * 16 + i;
            const float f = e.padFlash[b][p].load();
            if (f > 0.004f) { e.padFlash[b][p] = f * 0.82f; any = true; }
            else if (f > 0.0f) { e.padFlash[b][p] = 0.0f; any = true; }
        }
        if (any) miniPads.repaint();
    }

    void handleNoteOn (juce::MidiKeyboardState*, int, int note, float velocity) override
    {
        auto& e = Engine::get();
        e.padDown (selBank(), selPad(), e.fixedVelocityOn.load() ? e.fixedVelocity.load() : velocity, note);
    }
    void handleNoteOff (juce::MidiKeyboardState*, int, int note, float) override { Engine::get().padUp (selBank(), selPad(), note); }

    IconButton bankMenuBtn { Icon::pads, "Banks: choose, new, copy, paste" }, padMenuBtn { Icon::dots, "Pad options" };
    MiniPads miniPads;
    IconButton padPrev { Icon::down, "Previous 16 pads" }, padNext { Icon::up, "Next 16 pads" };
    TabStrip leftTabs, pageTabs;
    juce::OwnedArray<Knob> channelKnobs;
    Stepper chokeStep { "Choke group" }, linkStep { "Link group" }, noteStep { "Pad note" };
    ChoiceField keysField { "Keys mode" }, outputField { "Output" };
    juce::TextButton midiBtn { "MIDI setup" };
    juce::Viewport layersView;
    LayerList layerList;
    juce::TextButton addLayerBtn { "+ Layer" }, loadBtn { "Load..." }, recordBtn { "Record..." };
    SamplerPage sampler;
    InsertChainView layerFx;
    ModulationsPage modulations;
    MappingPage mapping;
    PluginPage plugin;
    juce::MidiKeyboardState keyboardState;
    juce::MidiKeyboardComponent keyboard { keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> leftArea, bankHeader, padHeader, padPageLabel;
};
} // namespace

std::unique_ptr<ViewBase> createEditorView() { return std::make_unique<EditorView>(); }
