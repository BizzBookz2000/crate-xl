// Performance view: bank tabs, 16 / 64 pad grids and the Station layout, keys mode,
// velocity / roll / wheels / XY panel, note repeat, pad modes, chords & scales, keyboard.
#include "Views.h"

namespace
{
// ---------------------------------------------------------------------------
// Keys mode: which note a pad plays (follows the selected scale)
int keysNoteForPad (int index)
{
    auto& e = Engine::get();
    const int base = juce::jlimit (0, 120, e.keysOctave.load() * 12 + e.scaleRoot.load());
    if (e.scaleIndex.load() == 0) return juce::jlimit (0, 127, e.keysOctave.load() * 12 + index);
    int note = base, count = 0;
    while (count < index && note < 127) { ++note; if (e.isKeyInScale (note)) ++count; }
    return juce::jlimit (0, 127, note);
}

const std::vector<float>& thumbnailFor (const cr::SampleData* s)
{
    static std::map<const cr::SampleData*, std::vector<float>> cache;
    static std::vector<float> empty;
    if (s == nullptr || s->isStreamed() || s->buffer.getNumSamples() == 0) return empty;
    auto it = cache.find (s);
    if (it != cache.end()) return it->second;
    if (cache.size() > 2048) cache.clear();
    std::vector<float> peaks (40, 0.0f);
    const int len = s->buffer.getNumSamples();
    for (int i = 0; i < 40; ++i)
    {
        const int a = len * i / 40, b = juce::jmax (a + 1, len * (i + 1) / 40);
        peaks[(size_t) i] = juce::jmax (s->buffer.getMagnitude (0, a, b - a), s->buffer.getMagnitude (1, a, b - a));
    }
    return cache.emplace (s, std::move (peaks)).first->second;
}

float padVelocity (const juce::MouseEvent& ev, int height)
{
    auto& e = Engine::get();
    if (e.yAxisVelocity.load()) return juce::jlimit (0.05f, 1.0f, 1.0f - (float) ev.y / (float) juce::jmax (1, height));
    if (e.fixedVelocityOn.load()) return e.fixedVelocity.load();
    return 0.85f;
}

// ---------------------------------------------------------------------------
class PadComponent : public juce::Component, public juce::FileDragAndDropTarget, public juce::DragAndDropTarget
{
public:
    std::function<void (int, float, int)> onDown;   // index, velocity, key (-1 = pad)
    std::function<void (int, int)> onUp;
    std::function<void (int)> onMenu;
    int index = 0;
    int keyNote = -1;      // keys mode: note played by this pad
    bool big = false;

    void paint (juce::Graphics& g) override
    {
        auto& e = Engine::get();
        const int b = e.selectedBank;
        auto& bank = e.session().banks[(size_t) b];
        auto r = getLocalBounds().toFloat().reduced (big ? 5.0f : 3.5f);
        const float corner = juce::jmin (8.0f, r.getWidth() * 0.08f);
        const int mode = e.padMode.load();

        if (keyNote >= 0)
        {
            // keys mode: chromatic note pads in the colour of the selected pad
            const auto col = padColour (b, e.selectedPad);
            const bool root = (keyNote - e.scaleRoot.load()) % 12 == 0;
            const bool inScale = e.isKeyInScale (keyNote);
            const float flash = keyFlash;
            auto fill = root ? col : (inScale ? col.darker (0.45f) : col.darker (0.85f).withSaturation (0.4f));
            fill = fill.interpolatedWith (juce::Colours::white, flash * 0.55f);
            g.setColour (fill);
            g.fillRoundedRectangle (r, corner);
            g.setColour (Pal::text.withAlpha (inScale ? 0.95f : 0.55f));
            g.setFont (Fonts::ui (big ? 15.0f : 12.5f, true));
            g.drawText (noteName (keyNote), r, juce::Justification::centred);
            return;
        }

        auto& pad = bank.pads[(size_t) index];
        const float flash = e.padFlash[b][index].load();
        const bool loaded = pad.hasSound();
        const bool selected = e.selectedPad == index;
        const auto col = padColour (b, index);

        if (mode == 4)
        {
            // scene mode: pads launch scenes
            const bool exists = index < (int) e.session().scenes.size();
            g.setColour (exists ? Pal::surface3 : Pal::emptyPad);
            g.fillRoundedRectangle (r, corner);
            g.setColour (exists ? Pal::text : Pal::textFaint);
            g.setFont (Fonts::ui (12.0f, true));
            g.drawFittedText (exists ? e.session().scenes[(size_t) index].name : "+ Capture", r.toNearestInt().reduced (6), juce::Justification::centred, 2);
            drawCaps (g, "SCENE " + juce::String (index + 1), r.toNearestInt().reduced (8, 6), juce::Justification::bottomLeft, Pal::textFaint, 9.0f);
            return;
        }

        juce::Colour fill = Pal::emptyPad;
        if (loaded)
        {
            fill = col.darker (0.15f);
            if (pad.channel.mute) fill = fill.withSaturation (0.15f).darker (0.6f);
        }
        if (flash > 0.01f) fill = fill.interpolatedWith (juce::Colours::white, flash * (loaded ? 0.45f : 0.2f));
        juce::ColourGradient grad (fill.brighter (0.12f), r.getX(), r.getY(), fill.darker (0.12f), r.getX(), r.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (r, corner);
        if (dragOver)
        {
            g.setColour (Pal::accent.withAlpha (0.35f));
            g.fillRoundedRectangle (r, corner);
        }
        if (! loaded)
        {
            g.setColour (Pal::outline);
            g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
        }

        // sample silhouette
        if (auto s = pad.firstSample(); s && loaded)
        {
            const auto& peaks = thumbnailFor (s.get());
            if (! peaks.empty() && r.getHeight() > 60.0f)
            {
                auto wr = r.reduced (r.getWidth() * 0.12f, 0.0f).withTrimmedTop (r.getHeight() * 0.38f).withTrimmedBottom (r.getHeight() * 0.28f);
                g.setColour (juce::Colours::black.withAlpha (0.16f));
                const float w = wr.getWidth() / (float) peaks.size();
                for (size_t i = 0; i < peaks.size(); ++i)
                {
                    const float h = juce::jmax (1.0f, peaks[i] * wr.getHeight());
                    g.fillRect (wr.getX() + (float) i * w, wr.getCentreY() - h * 0.5f, juce::jmax (1.0f, w - 1.0f), h);
                }
            }
        }

        const auto textCol = loaded ? (fill.getPerceivedBrightness() > 0.62f ? juce::Colour (0xff15161a) : juce::Colours::white) : Pal::textFaint;
        auto inner = r.reduced (juce::jmin (9.0f, r.getWidth() * 0.08f), juce::jmin (7.0f, r.getHeight() * 0.07f)).toNearestInt();
        g.setColour (textCol);
        g.setFont (Fonts::caps (big ? 12.0f : (getWidth() < 64 ? 8.5f : 10.0f)));
        g.drawFittedText (pad.name.toUpperCase(), inner.removeFromTop (big ? 30 : 26), juce::Justification::centredTop, 2, 0.8f);
        g.setColour (textCol.withAlpha (0.7f));
        g.setFont (Fonts::ui (big ? 11.0f : 9.5f, true));
        g.drawText (cr::bankLetter (b) + "." + juce::String (index + 1), inner, juce::Justification::bottomLeft);

        juce::StringArray badges;
        if (pad.instrument) badges.add ("PLUG");
        if (pad.keysMode) badges.add ("KEYS");
        if (pad.choke > 0) badges.add ("C" + juce::String (pad.choke));
        if (pad.link > 0) badges.add ("L" + juce::String (pad.link));
        if (pad.channel.mute) badges.add ("M");
        if (pad.channel.solo) badges.add ("S");
        if (getWidth() > 70)
        {
            g.setFont (Fonts::caps (8.5f));
            g.drawText (badges.joinIntoString (" "), inner, juce::Justification::bottomRight);
        }

        if (pad.channel.solo)
        {
            g.setColour (Pal::solo);
            g.fillRoundedRectangle (r.withHeight (3.0f).reduced (corner, 0.0f), 1.5f);
        }
        if (selected && mode != 2 && mode != 3)
        {
            g.setColour (juce::Colours::white);
            g.drawRoundedRectangle (r.reduced (1.0f), corner, 2.0f);
        }
    }

    void mouseDown (const juce::MouseEvent& ev) override
    {
        if (ev.mods.isPopupMenu()) { if (onMenu && keyNote < 0) onMenu (index); return; }
        pressedKey = keyNote;
        if (keyNote >= 0) { keyFlash = 1.0f; repaint(); }
        if (onDown) onDown (index, padVelocity (ev, getHeight()), keyNote);
    }
    void mouseUp (const juce::MouseEvent& ev) override
    {
        if (ev.mods.isPopupMenu()) return;
        if (pressedKey >= 0) { keyFlash = 0.0f; repaint(); }
        if (onUp) onUp (index, pressedKey);
    }

    float keyFlash = 0.0f;

    // desktop file drops
    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        for (auto& f : files) if (isAudioFile (juce::File (f))) return keyNote < 0;
        return false;
    }
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override
    {
        dragOver = false;
        auto& e = Engine::get();
        int pad = index;
        const auto method = juce::ModifierKeys::currentModifiers.isShiftDown() ? Engine::LoadMethod::newLayer : Engine::LoadMethod::replace;
        for (auto& f : files)
            if (isAudioFile (juce::File (f)) && pad < cr::numPads)
            {
                e.checkpointPad (e.selectedBank, pad);
                if (e.loadSampleToPad (juce::File (f), e.selectedBank, pad, method)) ++pad;
            }
        e.selectedPad = index;
        ViewBase::broadcast();
    }

    // drops from the Browser
    bool isInterestedInDragSource (const SourceDetails& d) override { return keyNote < 0 && isFileDrag (d) && isAudioFile (fileFromDrag (d)); }
    void itemDragEnter (const SourceDetails&) override { dragOver = true; repaint(); }
    void itemDragExit (const SourceDetails&) override { dragOver = false; repaint(); }
    void itemDropped (const SourceDetails& d) override
    {
        dragOver = false;
        auto& e = Engine::get();
        e.checkpointPad (e.selectedBank, index);
        e.loadSampleToPad (fileFromDrag (d), e.selectedBank, index,
                           juce::ModifierKeys::currentModifiers.isShiftDown() ? Engine::LoadMethod::newLayer : Engine::LoadMethod::replace);
        e.selectedPad = index;
        ViewBase::broadcast();
    }

private:
    bool dragOver = false;
    int pressedKey = -1;
};

// ---------------------------------------------------------------------------
// Roll: hold to roll the selected pad. X = rate (6 zones), Y = velocity.
class RollPad : public juce::Component
{
public:
    void paint (juce::Graphics& g) override
    {
        auto& e = Engine::get();
        auto r = getLocalBounds().toFloat();
        const auto col = padColour (e.selectedBank, e.selectedPad);
        g.setColour (Pal::bg);
        g.fillRoundedRectangle (r, 6.0f);
        const float zh = r.getHeight() / 6.0f;
        for (int i = 0; i < 6; ++i)
        {
            auto z = juce::Rectangle<float> (r.getX(), r.getBottom() - (float) (i + 1) * zh, r.getWidth(), zh);
            if (active && i == zone)
            {
                g.setColour (col.withAlpha (0.25f + 0.55f * vel));
                g.fillRect (z.reduced (1.0f));
            }
            g.setColour (Pal::outline);
            if (i > 0) g.fillRect (z.getX() + 6.0f, z.getBottom(), z.getWidth() - 12.0f, 1.0f);
            drawCaps (g, rateName (e.repeatTicks[(size_t) i]), z.toNearestInt().reduced (10, 0), juce::Justification::centredLeft, Pal::textFaint, 10.0f);
        }
        drawCaps (g, "VELOCITY >", getLocalBounds().reduced (10, 6), juce::Justification::topRight, Pal::textFaint, 9.0f);
        if (active)
        {
            g.setColour (juce::Colours::white);
            g.fillEllipse ((float) lastPos.x - 7.0f, (float) lastPos.y - 7.0f, 14.0f, 14.0f);
        }
    }

    static juce::String rateName (int ticks)
    {
        static const std::pair<int, const char*> rates[] = { { 192, "1/2" }, { 96, "1/4" }, { 64, "1/4T" }, { 48, "1/8" }, { 32, "1/8T" },
                                                             { 24, "1/16" }, { 16, "1/16T" }, { 12, "1/32" }, { 8, "1/32T" }, { 6, "1/64" } };
        for (auto& rt : rates) if (rt.first == ticks) return rt.second;
        return juce::String (ticks) + "t";
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto& eng = Engine::get();
        saved = eng.repeatSlot.load();
        active = true;
        update (e);
        bank = eng.selectedBank;
        pad = eng.selectedPad;
        eng.padDown (bank, pad, vel);
    }
    void mouseDrag (const juce::MouseEvent& e) override { update (e); }
    void mouseUp (const juce::MouseEvent&) override
    {
        auto& eng = Engine::get();
        eng.padUp (bank, pad);
        eng.repeatSlot = saved;
        active = false;
        repaint();
    }

private:
    void update (const juce::MouseEvent& e)
    {
        lastPos = e.getPosition();
        zone = juce::jlimit (0, 5, (getHeight() - e.y) * 6 / juce::jmax (1, getHeight()));
        vel = juce::jlimit (0.05f, 1.0f, (float) e.x / (float) juce::jmax (1, getWidth()));
        Engine::get().repeatSlot = zone;
        repaint();
    }
    bool active = false;
    int zone = 0, saved = -1, bank = 0, pad = 0;
    float vel = 0.8f;
    juce::Point<int> lastPos;
};

// ---------------------------------------------------------------------------
// Station step pads: a 16-step sequencer for the selected pad
class StepPads : public juce::Component
{
public:
    int page = 0;
    void paint (juce::Graphics& g) override
    {
        auto& e = Engine::get();
        auto& bank = e.session().banks[(size_t) e.selectedBank.load()];
        if (! bank.created) return;
        const auto& pat = bank.patterns[(size_t) juce::jlimit (0, (int) bank.patterns.size() - 1, bank.workingPattern)];
        const int stepT = cr::barTicks() / 16;
        const int pad = e.selectedPad;
        const double pos = e.getTrackPatternPosition (e.selectedBank);
        const int playing = (e.isPlaying() && pos >= 0.0 && bank.playingPattern == bank.workingPattern) ? (int) (pos / stepT) : -1;
        const auto col = padColour (e.selectedBank, pad);
        for (int i = 0; i < 16; ++i)
        {
            auto r = stepBounds (i).toFloat();
            const int tick = (page * 16 + i) * stepT;
            bool on = false;
            float vel = 0.0f;
            for (auto& n : pat.notes)
                if (n.pad == pad && n.start >= tick && n.start < tick + stepT) { on = true; vel = n.velocity; break; }
            const bool inPattern = tick < pat.lengthTicks();
            g.setColour (! inPattern ? Pal::bg : (i % 4 == 0 ? Pal::surface3 : Pal::surface2));
            g.fillRoundedRectangle (r, 5.0f);
            if (on)
            {
                g.setColour (col.withAlpha (0.45f + 0.55f * vel));
                g.fillRoundedRectangle (r.reduced (3.0f), 4.0f);
            }
            if (playing == page * 16 + i)
            {
                g.setColour (juce::Colours::white);
                g.drawRoundedRectangle (r.reduced (1.0f), 5.0f, 2.0f);
            }
            g.setColour (on ? Pal::bg : Pal::textFaint);
            g.setFont (Fonts::ui (10.0f, true));
            g.drawText (juce::String (i + 1), r.toNearestInt(), juce::Justification::centred);
        }
    }

    void mouseDown (const juce::MouseEvent& ev) override
    {
        for (int i = 0; i < 16; ++i)
            if (stepBounds (i).contains (ev.getPosition())) { toggle (i); return; }
    }

    juce::Rectangle<int> stepBounds (int i) const
    {
        const float w = (float) getWidth() / 16.0f;
        return juce::Rectangle<float> ((float) i * w + 2.0f, 2.0f, w - 4.0f, (float) getHeight() - 4.0f).toNearestInt();
    }

private:
    void toggle (int i)
    {
        auto& e = Engine::get();
        auto& bank = currentBank();
        const int stepT = cr::barTicks() / 16;
        const int tick = (page * 16 + i) * stepT;
        const int pad = e.selectedPad;
        e.checkpoint();
        {
            const juce::ScopedLock sl (e.getLock());
            auto& pat = bank.patterns[(size_t) juce::jlimit (0, (int) bank.patterns.size() - 1, bank.workingPattern)];
            if (tick >= pat.lengthTicks()) return;
            auto it = std::find_if (pat.notes.begin(), pat.notes.end(), [&] (const cr::Note& n) { return n.pad == pad && n.start >= tick && n.start < tick + stepT; });
            if (it != pat.notes.end()) pat.notes.erase (it);
            else pat.notes.push_back ({ pad, -1, tick, stepT, e.fixedVelocity.load() });
        }
        repaint();
    }
};

// ---------------------------------------------------------------------------
// Bank tabs: group selector + A..H
class BankTabs : public juce::Component
{
public:
    void paint (juce::Graphics& g) override
    {
        auto& e = Engine::get();
        const int sel = e.selectedBank;
        const int group = sel / cr::banksPerGroup;
        auto r = getLocalBounds();
        auto grp = r.removeFromLeft (46);
        g.setColour (Pal::surface);
        g.fillRoundedRectangle (grp.toFloat().reduced (1.0f), 4.0f);
        drawCaps (g, "GRP", grp.withTrimmedBottom (grp.getHeight() / 2), juce::Justification::centredBottom, Pal::textFaint, 8.5f);
        g.setColour (Pal::text);
        g.setFont (Fonts::ui (13.0f, true));
        g.drawText (juce::String (group + 1), grp.withTrimmedTop (grp.getHeight() / 2), juce::Justification::centredTop);
        r.removeFromLeft (6);
        const int w = juce::jmin (64, r.getWidth() / cr::banksPerGroup);
        for (int i = 0; i < cr::banksPerGroup; ++i)
        {
            const int b = group * cr::banksPerGroup + i;
            const auto& bank = e.session().banks[(size_t) b];
            auto t = juce::Rectangle<int> (r.getX() + i * w, r.getY(), w - 3, r.getHeight()).toFloat();
            const bool selected = b == sel;
            const auto col = bank.created ? bank.colour : Pal::textFaint;
            g.setColour (selected ? col : (bank.created ? Pal::surface2 : Pal::surface));
            g.fillRoundedRectangle (t, 4.0f);
            g.setColour (selected ? Pal::bg : col);
            g.setFont (Fonts::heavy (15.0f));
            g.drawText (cr::bankLetter (b), t.withTrimmedRight (t.getWidth() * 0.35f), juce::Justification::centred);
            // a little pad-grid glyph like a mini bank icon when the bank has content
            if (bank.created)
            {
                int filled = 0;
                for (auto& p : bank.pads) if (p.hasSound()) ++filled;
                auto glyph = t.withTrimmedLeft (t.getWidth() * 0.55f).withSizeKeepingCentre (12.0f, 12.0f);
                for (int k = 0; k < 4; ++k)
                {
                    g.setColour ((selected ? Pal::bg : col).withAlpha (k < (filled + 3) / 4 ? 0.9f : 0.3f));
                    g.fillRect (glyph.getX() + (k % 2) * 7.0f, glyph.getY() + (k / 2) * 7.0f, 5.0f, 5.0f);
                }
            }
        }
    }

    void mouseDown (const juce::MouseEvent& ev) override
    {
        auto& e = Engine::get();
        const int sel = e.selectedBank;
        const int group = sel / cr::banksPerGroup;
        if (ev.x < 46)
        {
            juce::PopupMenu m;
            for (int gr = 0; gr < cr::numGroups; ++gr)
            {
                int used = 0;
                for (int i = 0; i < cr::banksPerGroup; ++i) if (e.session().banks[(size_t) (gr * cr::banksPerGroup + i)].created) ++used;
                m.addItem (gr + 1, "Bank group " + juce::String (gr + 1) + (used > 0 ? "  (" + juce::String (used) + " banks)" : juce::String()), true, gr == group);
            }
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [] (int r)
            {
                if (r <= 0) return;
                auto& en = Engine::get();
                const int b = (r - 1) * cr::banksPerGroup;
                en.createBank (b);
                en.selectedBank = b;
                ViewBase::broadcast();
            });
            return;
        }
        const int w = juce::jmin (64, (getWidth() - 52) / cr::banksPerGroup);
        const int i = (ev.x - 52) / juce::jmax (1, w);
        if (! juce::isPositiveAndBelow (i, cr::banksPerGroup)) return;
        const int b = group * cr::banksPerGroup + i;
        if (ev.mods.isPopupMenu() && onBankMenu) { onBankMenu (b); return; }
        e.createBank (b);
        e.selectedBank = b;
        ViewBase::broadcast();
    }

    std::function<void (int)> onBankMenu;
};

// ---------------------------------------------------------------------------
class SideButton : public juce::Button
{
public:
    SideButton (const juce::String& n, juce::Colour on) : juce::Button (n), onCol (on) {}
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const bool on = getToggleState();
        g.setColour (on ? onCol : (down ? Pal::surface3 : (over ? Pal::surface2.brighter (0.04f) : Pal::surface2)));
        g.fillRoundedRectangle (r, 4.0f);
        drawCaps (g, getName(), getLocalBounds(), juce::Justification::centred, on ? Pal::bg : Pal::textDim, 11.0f);
    }
private:
    juce::Colour onCol;
};

// ===========================================================================
class PerformanceView : public ViewBase, private juce::Timer, private juce::MidiKeyboardState::Listener
{
public:
    enum Layout { grid16 = 0, grid64, station };

    PerformanceView()
    {
        // ---- left panel ----
        bankMenuBtn.onClick = [this] { showBankMenu (Engine::get().selectedBank, &bankMenuBtn); };
        addAndMakeVisible (bankMenuBtn);
        leftTabs.setTabs ({ "Velocity", "Roll", "Wheels", "X/Y" });
        leftTabs.onChange = [this] (int) { updateLeftPanel(); resized(); repaint(); };
        addAndMakeVisible (leftTabs);

        fixedVelToggle.setButtonText ("Fixed");
        fixedVelToggle.setToggleState (true, juce::dontSendNotification);
        fixedVelToggle.onClick = [this] { Engine::get().fixedVelocityOn = fixedVelToggle.getToggleState(); velSlider.setEnabled (fixedVelToggle.getToggleState()); };
        yVelToggle.setButtonText ("Pad Y-axis");
        yVelToggle.onClick = [this] { Engine::get().yAxisVelocity = yVelToggle.getToggleState(); };
        midiVelToggle.setButtonText ("MIDI fixed");
        midiVelToggle.onClick = [this] { Engine::get().fixedMidiVelocity = midiVelToggle.getToggleState(); };
        velSlider.setSliderStyle (juce::Slider::LinearBarVertical);
        velSlider.setRange (1, 127, 1);
        velSlider.setValue (100, juce::dontSendNotification);
        velSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        velSlider.onValueChange = [this] { Engine::get().fixedVelocity = (float) velSlider.getValue() / 127.0f; repaint(); };
        swingSlider.setSliderStyle (juce::Slider::LinearBarVertical);
        swingSlider.setRange (0, 50, 1);
        swingSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        swingSlider.onValueChange = [this]
        {
            auto& e = Engine::get();
            const int slot = e.repeatSlot.load() >= 0 ? e.repeatSlot.load() : lastRepeatSlot;
            e.repeatSwing[(size_t) slot] = (float) swingSlider.getValue() / 100.0f;
            refreshRepeat();
            repaint();
        };
        for (auto* c : std::initializer_list<juce::Component*> { &fixedVelToggle, &yVelToggle, &midiVelToggle, &velSlider, &swingSlider, &roll })
            addChildComponent (c);

        for (auto* w : { &pitchWheel, &modWheel, &touchSlider })
        {
            w->setSliderStyle (juce::Slider::LinearBarVertical);
            w->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            addChildComponent (w);
        }
        pitchWheel.setRange (-1.0, 1.0);
        pitchWheel.setValue (0.0, juce::dontSendNotification);
        pitchWheel.onDragEnd = [this] { pitchWheel.setValue (0.0); };
        pitchWheel.onValueChange = [this] { Engine::get().setParamValue (cr::ParamAddress::parse ("midi/" + juce::String (Engine::get().selectedBank.load()) + "/pb"), (float) pitchWheel.getValue(), true); };
        modWheel.setRange (0.0, 1.0);
        modWheel.onValueChange = [this] { Engine::get().setParamValue (cr::ParamAddress::parse ("midi/" + juce::String (Engine::get().selectedBank.load()) + "/mod"), (float) modWheel.getValue(), true); };
        touchSlider.setRange (0.0, 1.0);
        touchSlider.onValueChange = [this] { Engine::get().setParamValue (cr::ParamAddress::parse ("midi/" + juce::String (Engine::get().selectedBank.load()) + "/at"), (float) touchSlider.getValue(), true); };
        touchSlider.onDragEnd = [this] { touchSlider.setValue (0.0); };
        sustainBtn.setClickingTogglesState (true);
        sustainBtn.onClick = [this] { Engine::get().sustainPedal[(size_t) Engine::get().selectedBank.load()] = sustainBtn.getToggleState(); };
        addChildComponent (sustainBtn);

        xyTabs.setTabs ({ "X/Y 1-4", "X/Y 5-8" });
        xyTabs.onChange = [this] (int t)
        {
            xyTop.macroX = t * 4; xyTop.macroY = t * 4 + 1;
            xyBottom.macroX = t * 4 + 2; xyBottom.macroY = t * 4 + 3;
            repaint();
        };
        xyTop.colourA = juce::Colour (0xff2e5d36); xyTop.colourB = juce::Colour (0xff6b4a1f);
        xyBottom.colourA = juce::Colour (0xff2d4060); xyBottom.colourB = juce::Colour (0xff5b2d55);
        xyBottom.macroX = 2; xyBottom.macroY = 3;
        for (auto* c : std::initializer_list<juce::Component*> { &xyTabs, &xyTop, &xyBottom }) addChildComponent (c);

        for (int i = 0; i < cr::numRepeatSlots; ++i)
        {
            auto* b = repeatButtons.add (new juce::TextButton());
            b->setClickingTogglesState (false);
            b->onClick = [this, i] { auto& e = Engine::get(); e.repeatSlot = e.repeatSlot.load() == i ? -1 : i; lastRepeatSlot = i; refreshRepeat(); };
            b->setColour (juce::TextButton::buttonColourId, Pal::surface);
            setMidiAction (*b, FocusActions::repeat1 + i);
            addAndMakeVisible (b);
            auto* a = repeatArrows.add (new IconButton (Icon::down, "Change the repeat rate"));
            a->onClick = [this, i] { showRepeatMenu (i); };
            addAndMakeVisible (a);
        }

        // ---- centre ----
        bankTabs.onBankMenu = [this] (int b) { showBankMenu (b, &bankTabs); };
        addAndMakeVisible (bankTabs);
        for (int i = 0; i < 64; ++i)
        {
            auto* p = pads.add (new PadComponent());
            p->onDown = [this] (int idx, float vel, int key) { padPressed (idx, vel, key); };
            p->onUp = [this] (int idx, int key) { padReleased (idx, key); };
            p->onMenu = [this] (int idx) { showPadMenu (idx, pads[idx % 64]); };
            setMidiAction (*p, FocusActions::triggerPad1 + i);
            addAndMakeVisible (p);
        }

        // station
        for (int i = 0; i < 8; ++i)
        {
            auto* k = stationKnobs.add (new Knob());
            k->setCompact (true);
            addChildComponent (k);
            auto* s = stationFaders.add (new juce::Slider (juce::Slider::LinearVertical, juce::Slider::NoTextBox));
            s->setRange (0.0, 1.25);
            s->setSkewFactorFromMidPoint (0.5);
            s->setDoubleClickReturnValue (true, 0.8);
            s->onValueChange = [this, i, s]
            {
                auto& e = Engine::get();
                e.setParamValue (cr::ParamAddress::parse ("pad/" + juce::String (e.selectedBank.load()) + "/" + juce::String (page * 8 + i) + "/vol"), (float) s->getValue(), true);
            };
            addChildComponent (s);
        }
        addChildComponent (steps);
        stepPrev.onClick = [this] { steps.page = juce::jmax (0, steps.page - 1); repaint(); steps.repaint(); };
        stepNext.onClick = [this] { steps.page = juce::jmin (currentPatternBars() - 1, steps.page + 1); repaint(); steps.repaint(); };
        addChildComponent (stepPrev);
        addChildComponent (stepNext);
        stationLofi.choices = cr::lofiNames();
        stationLofi.getIndex = [] { auto* l = currentLayer(); return l ? (int) l->p.lofi : 0; };
        stationLofi.setIndex = [] (int i) { auto& e = Engine::get(); const juce::ScopedLock sl (e.getLock()); for (auto& l : currentPad().layers) l.p.lofi = (cr::LofiMode) i; };
        stationFilter.choices = cr::filterTypeNames();
        stationFilter.getIndex = [] { auto* l = currentLayer(); return l ? (int) l->p.filter : 0; };
        stationFilter.setIndex = [] (int i) { auto& e = Engine::get(); const juce::ScopedLock sl (e.getLock()); for (auto& l : currentPad().layers) l.p.filter = (cr::FilterType) i; };
        addChildComponent (stationLofi);
        addChildComponent (stationFilter);

        // ---- right column ----
        for (auto* b : { &keysBtn, &selectBtn, &sceneBtn, &muteBtn, &soloBtn }) addAndMakeVisible (b);
        setMidiAction (keysBtn, FocusActions::toggleKeys);
        setMidiAction (selectBtn, FocusActions::modeSelect);
        setMidiAction (sceneBtn, FocusActions::modeScene);
        setMidiAction (muteBtn, FocusActions::modeMute);
        setMidiAction (soloBtn, FocusActions::modeSolo);
        keysBtn.onClick = [this]
        {
            auto& e = Engine::get();
            {
                const juce::ScopedLock sl (e.getLock());
                auto& pad = currentPad();
                pad.keysMode = ! pad.keysMode;
                if (pad.keysMode) e.padMode = 0;
            }
            if (currentPad().keysMode && leftTabs.getSelected() == 0) leftTabs.setSelected (3, true);
            refresh();
        };
        selectBtn.onClick = [this] { toggleMode (1); };
        muteBtn.onClick = [this] { toggleMode (2); };
        soloBtn.onClick = [this] { toggleMode (3); };
        sceneBtn.onClick = [this] { toggleMode (4); };
        pageUp.onClick = [this] { changePage (1); };
        pageDown.onClick = [this] { changePage (-1); };
        addAndMakeVisible (pageUp);
        addAndMakeVisible (pageDown);

        // ---- bottom bar ----
        const char* layouts[] = { "16", "64", "Station" };
        for (int i = 0; i < 3; ++i)
        {
            auto* b = layoutButtons.add (new juce::TextButton (layouts[i]));
            b->onClick = [this, i] { layout = i; page = 0; refresh(); };
            b->setColour (juce::TextButton::buttonOnColourId, Pal::surface3);
            b->setColour (juce::TextButton::textColourOnId, Pal::text);
            addAndMakeVisible (b);
        }
        pianoBtn.onClick = [this] { showPiano = ! showPiano; pianoBtn.setToggleState (showPiano, juce::dontSendNotification); resized(); };
        addAndMakeVisible (pianoBtn);
        chordsBtn.onClick = [this] { showChordsAndScales (&chordsBtn); };
        addAndMakeVisible (chordsBtn);
        const char* ops[] = { "Copy", "Paste", "Cut", "Clear", "Colour", "Rename", "Edit" };
        for (int i = 0; i < 7; ++i)
        {
            auto* b = opButtons.add (new juce::TextButton (ops[i]));
            b->onClick = [this, i] { padOperation (i); };
            addChildComponent (b);
        }
        captureBtn.onClick = [] { auto& e = Engine::get(); e.captureScene ((int) e.session().scenes.size()); ViewBase::broadcast(); };
        stopScenesBtn.onClick = [] { Engine::get().stopAllScenes(); };
        scenesEditBtn.onClick = [] { Nav::showSequencer (Nav::scenes); };
        for (auto* b : { &captureBtn, &stopScenesBtn, &scenesEditBtn }) addChildComponent (b);

        keyboardState.addListener (this);
        piano.setAvailableRange (0, 127);
        piano.setLowestVisibleKey (36);
        piano.setKeyWidth (26.0f);
        piano.setScrollButtonsVisible (true);
        addChildComponent (piano);

        startTimerHz (30);
        refresh();
    }

    ~PerformanceView() override { keyboardState.removeListener (this); }

    // -----------------------------------------------------------------------
    void refresh() override
    {
        auto& e = Engine::get();
        auto& bank = currentBank();
        const int mode = e.padMode.load();
        const bool keys = bank.created && currentPad().keysMode;
        keysBtn.setToggleState (keys, juce::dontSendNotification);
        selectBtn.setToggleState (mode == 1, juce::dontSendNotification);
        muteBtn.setToggleState (mode == 2, juce::dontSendNotification);
        soloBtn.setToggleState (mode == 3, juce::dontSendNotification);
        sceneBtn.setToggleState (mode == 4, juce::dontSendNotification);
        for (int i = 0; i < 3; ++i) layoutButtons[i]->setToggleState (layout == i, juce::dontSendNotification);
        pianoBtn.setToggleState (showPiano, juce::dontSendNotification);
        for (auto* b : opButtons) b->setVisible (mode == 1);
        opButtons[1]->setEnabled (e.hasPadClipboard());
        for (auto* b : { &captureBtn, &stopScenesBtn, &scenesEditBtn }) b->setVisible (mode == 4);
        sustainBtn.setToggleState (e.sustainPedal[(size_t) e.selectedBank.load()].load(), juce::dontSendNotification);
        refreshRepeat();

        if (layout == station)
        {
            const int b = e.selectedBank, p = e.selectedPad;
            const int base = page * 8;
            for (int i = 0; i < 8; ++i)
                stationFaders[i]->setValue (bank.pads[(size_t) (base + i)].channel.volume, juce::dontSendNotification);
            const bool hasLayer = ! bank.pads[(size_t) p].layers.empty();
            const char* names[] = { "Tune", "Decay", "Cutoff", "Reso", "Attack", "Saturate", "Pan", "Gain" };
            const int fields[] = { cr::lfTune, cr::lfDecay, cr::lfCutoff, cr::lfResonance, cr::lfAttack, cr::lfSaturate, cr::lfPan, cr::lfGain };
            for (int i = 0; i < 8; ++i)
            {
                stationKnobs[i]->setAccent (padColour (b, p));
                if (hasLayer) stationKnobs[i]->setParam (layerPath (b, p, 0, fields[i]), names[i]);
                else { stationKnobs[i]->setLabel (names[i]); stationKnobs[i]->setEnabled (false); }
            }
        }
        updateLeftPanel();
        updateVisibility();
        resized();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto& e = Engine::get();
        g.fillAll (Pal::bg);
        // left panel
        g.setColour (Pal::panel);
        g.fillRect (leftArea);
        g.setColour (Pal::outline);
        g.fillRect (leftArea.getRight(), leftArea.getY(), 1, leftArea.getHeight());
        auto& bank = currentBank();
        auto hdr = leftHeader;
        drawBankBadge (g, hdr.removeFromLeft (30).withSizeKeepingCentre (28, 28).toFloat(), e.selectedBank, true);
        hdr.removeFromLeft (10);
        g.setColour (Pal::text);
        g.setFont (Fonts::ui (15.0f, true));
        g.drawFittedText (bank.created ? bank.name : "Empty bank", hdr.withTrimmedRight (30), juce::Justification::centredLeft, 1);

        const int tab = leftTabs.getSelected();
        if (tab == 0)
        {
            drawCaps (g, "Fixed vel", velCaption, juce::Justification::centred, Pal::textFaint, 9.5f);
            drawCaps (g, "Repeat swing", swingCaption, juce::Justification::centred, Pal::textFaint, 9.5f);
        }
        else if (tab == 2)
        {
            drawCaps (g, "Pitch", pitchWheel.getBounds().withY (pitchWheel.getBottom() + 2).withHeight (14), juce::Justification::centred, Pal::textFaint, 9.5f);
            drawCaps (g, "Mod", modWheel.getBounds().withY (modWheel.getBottom() + 2).withHeight (14), juce::Justification::centred, Pal::textFaint, 9.5f);
            drawCaps (g, "Touch", touchSlider.getBounds().withY (touchSlider.getBottom() + 2).withHeight (14), juce::Justification::centred, Pal::textFaint, 9.5f);
        }
        drawCaps (g, "Note repeat", repeatCaption, juce::Justification::centredLeft, Pal::textFaint, 9.5f);

        // right column
        g.setColour (Pal::panel);
        g.fillRect (rightArea);
        g.setColour (Pal::outline);
        g.fillRect (rightArea.getX() - 1, rightArea.getY(), 1, rightArea.getHeight());
        const bool keys = bank.created && currentPad().keysMode && layout != station;
        if (keys)
        {
            drawCaps (g, "Octave", pageCaption, juce::Justification::centred, Pal::textFaint, 9.5f);
            g.setColour (Pal::text);
            g.setFont (Fonts::digits (17.0f));
            g.drawText (noteName (keysNoteForPad (0)), pageValue, juce::Justification::centred);
            drawCaps (g, "Pad " + juce::String (e.selectedPad.load() + 1), pageValue.translated (0, 20), juce::Justification::centred, Pal::textFaint, 9.0f);
        }
        else
        {
            drawCaps (g, "Pads page", pageCaption, juce::Justification::centred, Pal::textFaint, 9.5f);
            g.setColour (Pal::text);
            g.setFont (Fonts::digits (19.0f));
            g.drawText (juce::String (page + 1), pageValue, juce::Justification::centred);
            const int per = padsPerPage();
            drawCaps (g, juce::String (page * per + 1) + "-" + juce::String (page * per + per), pageValue.translated (0, 20), juce::Justification::centred, Pal::textFaint, 9.0f);
        }

        // bottom bar
        g.setColour (Pal::panel);
        g.fillRect (bottomArea);
        g.setColour (Pal::outline);
        g.fillRect (bottomArea.getX(), bottomArea.getY(), bottomArea.getWidth(), 1);

        if (layout == station)
        {
            for (int i = 0; i < 8; ++i)
                drawCaps (g, cr::bankLetter (e.selectedBank) + "." + juce::String (page * 8 + i + 1),
                          stationFaders[i]->getBounds().withY (stationFaders[i]->getBottom()).withHeight (16), juce::Justification::centred, Pal::textFaint, 9.0f);
            drawCaps (g, "Volume", stationFaderCaption, juce::Justification::centredLeft, Pal::textDim, 10.0f);
            drawCaps (g, "Pad " + juce::String (e.selectedPad.load() + 1) + "  -  sound", stationKnobCaption, juce::Justification::centredLeft, Pal::textDim, 10.0f);
            drawCaps (g, "Step sequencer  -  pad " + juce::String (e.selectedPad.load() + 1) + ", bar " + juce::String (steps.page + 1) + " of " + juce::String (currentPatternBars()),
                      stationStepCaption, juce::Justification::centredLeft, Pal::textDim, 10.0f);
        }
        if (e.padMode.load() == 2 || e.padMode.load() == 3)
            drawCaps (g, e.padMode.load() == 2 ? "Tap pads to mute / unmute" : "Tap pads to solo / unsolo", hintArea, juce::Justification::centredLeft, Pal::textDim, 10.5f);
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        if (leftTabs.getSelected() != 0) return;
        g.setColour (Pal::text);
        g.setFont (Fonts::digits (15.0f));
        g.drawText (juce::String ((int) velSlider.getValue()), velSlider.getBounds().withHeight (30), juce::Justification::centred);
        g.drawText (juce::String ((int) swingSlider.getValue()) + "%", swingSlider.getBounds().withHeight (30), juce::Justification::centred);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        bottomArea = r.removeFromBottom (44);
        leftArea = r.removeFromLeft (juce::jlimit (230, 290, getWidth() / 5));
        rightArea = r.removeFromRight (92);

        // left panel
        auto l = leftArea.reduced (12, 10);
        leftHeader = l.removeFromTop (32);
        bankMenuBtn.setBounds (leftHeader.removeFromRight (28).withSizeKeepingCentre (28, 28));
        l.removeFromTop (8);
        leftTabs.setBounds (l.removeFromTop (30));
        l.removeFromTop (10);
        auto rep = l.removeFromBottom (64);
        repeatCaption = rep.removeFromTop (16);
        const int rw = rep.getWidth() / cr::numRepeatSlots;
        for (int i = 0; i < cr::numRepeatSlots; ++i)
        {
            auto cell = juce::Rectangle<int> (rep.getX() + i * rw, rep.getY(), rw, rep.getHeight());
            repeatArrows[i]->setBounds (cell.removeFromTop (18).reduced (2, 0));
            repeatButtons[i]->setBounds (cell.reduced (2, 2));
        }
        l.removeFromBottom (10);
        const int tab = leftTabs.getSelected();
        if (tab == 0)
        {
            auto toggles = l.removeFromBottom (78);
            fixedVelToggle.setBounds (toggles.removeFromTop (26));
            yVelToggle.setBounds (toggles.removeFromTop (26));
            midiVelToggle.setBounds (toggles.removeFromTop (26));
            l.removeFromBottom (6);
            auto half = l.getWidth() / 2;
            auto a = l.removeFromLeft (half).reduced (6, 0), b = l.reduced (6, 0);
            velCaption = a.removeFromTop (16);
            swingCaption = b.removeFromTop (16);
            velSlider.setBounds (a);
            swingSlider.setBounds (b);
        }
        else if (tab == 1) roll.setBounds (l);
        else if (tab == 2)
        {
            sustainBtn.setBounds (l.removeFromBottom (32));
            l.removeFromBottom (24);
            const int w = l.getWidth() / 3;
            pitchWheel.setBounds (l.removeFromLeft (w).reduced (6, 0));
            modWheel.setBounds (l.removeFromLeft (w).reduced (6, 0));
            touchSlider.setBounds (l.reduced (6, 0));
        }
        else
        {
            xyTabs.setBounds (l.removeFromTop (26));
            l.removeFromTop (8);
            const int h = (l.getHeight() - 8) / 2;
            xyTop.setBounds (l.removeFromTop (h));
            l.removeFromTop (8);
            xyBottom.setBounds (l);
        }

        // right column
        auto rc = rightArea.reduced (10, 12);
        for (auto* b : { &keysBtn, &selectBtn, &sceneBtn, &muteBtn, &soloBtn }) { b->setBounds (rc.removeFromTop (40)); rc.removeFromTop (8); }
        pageDown.setBounds (rc.removeFromBottom (40));
        rc.removeFromBottom (4);
        auto pv = rc.removeFromBottom (60);
        pageCaption = pv.removeFromTop (16);
        pageValue = pv.removeFromTop (24);
        rc.removeFromBottom (4);
        pageUp.setBounds (rc.removeFromBottom (40));

        // bottom bar
        auto bb = bottomArea.reduced (12, 7);
        for (auto* b : layoutButtons) { b->setBounds (bb.removeFromLeft (b == layoutButtons[2] ? 74 : 44)); bb.removeFromLeft (3); }
        bb.removeFromLeft (10);
        pianoBtn.setBounds (bb.removeFromLeft (40));
        bb.removeFromLeft (6);
        chordsBtn.setBounds (bb.removeFromLeft (150));
        bb.removeFromLeft (16);
        hintArea = bb;
        for (auto* b : opButtons) { b->setBounds (bb.removeFromLeft (70)); bb.removeFromLeft (4); }
        auto sb = hintArea;
        captureBtn.setBounds (sb.removeFromLeft (150)); sb.removeFromLeft (4);
        stopScenesBtn.setBounds (sb.removeFromLeft (130)); sb.removeFromLeft (4);
        scenesEditBtn.setBounds (sb.removeFromLeft (150));

        // centre
        auto c = r.reduced (16, 12);
        bankTabs.setBounds (c.removeFromTop (34));
        c.removeFromTop (12);
        if (showPiano && layout != station)
        {
            piano.setBounds (c.removeFromBottom (96));
            c.removeFromBottom (10);
        }
        const bool keys = currentBank().created && currentPad().keysMode;

        if (layout == station)
        {
            auto top = c.removeFromTop (juce::jmax (180, c.getHeight() * 32 / 100));
            stationFaderCaption = top.removeFromTop (16);
            auto faders = top.withTrimmedBottom (18);
            const int sw = faders.getWidth() / 8;
            for (int i = 0; i < 8; ++i) stationFaders[i]->setBounds (faders.getX() + i * sw + sw / 2 - 20, faders.getY() + 4, 40, faders.getHeight() - 4);
            c.removeFromTop (8);
            auto stepArea = c.removeFromBottom (86);
            stationStepCaption = stepArea.removeFromTop (18);
            stepPrev.setBounds (stepArea.removeFromLeft (30));
            stepNext.setBounds (stepArea.removeFromRight (30));
            steps.setBounds (stepArea.reduced (6, 0));
            c.removeFromBottom (8);
            auto knobArea = c.removeFromBottom (76);
            stationKnobCaption = knobArea.removeFromTop (16);
            const int kw = knobArea.getWidth() / 10;
            stationLofi.setBounds (knobArea.removeFromLeft (kw * 1).reduced (2, 6));
            stationFilter.setBounds (knobArea.removeFromLeft (kw * 1).reduced (2, 6));
            for (int i = 0; i < 8; ++i) stationKnobs[i]->setBounds (knobArea.removeFromLeft (kw).reduced (2, 0));
            c.removeFromBottom (8);
            const int pw = c.getWidth() / 8;
            const int ph = juce::jmin (c.getHeight(), (int) (pw * 1.15f));
            for (int i = 0; i < 64; ++i)
            {
                pads[i]->big = true;
                if (i < 8) pads[i]->setBounds (c.getX() + i * pw, c.getCentreY() - ph / 2, pw, ph);
            }
            return;
        }

        const int per = layout == grid64 ? 8 : 4;
        juce::ignoreUnused (keys);
        const int size = juce::jmin (c.getWidth(), c.getHeight());
        const int cell = size / per;
        auto grid = c.withSizeKeepingCentre (cell * per, cell * per);
        for (int i = 0; i < 64; ++i)
        {
            pads[i]->big = false;
            if (i >= per * per) continue;
            const int row = per - 1 - i / per, col = i % per;   // pad 1 at the bottom left
            pads[i]->setBounds (grid.getX() + col * cell, grid.getY() + row * cell, cell, cell);
        }
    }

private:
    int padsPerPage() const { return layout == grid64 ? 64 : (layout == station ? 8 : 16); }
    int numPages() const { return cr::numPads / padsPerPage(); }

    int currentPatternBars() const
    {
        auto& bank = currentBank();
        if (! bank.created) return 1;
        return bank.patterns[(size_t) juce::jlimit (0, (int) bank.patterns.size() - 1, bank.workingPattern)].bars();
    }

    void updateLeftPanel()
    {
        const int tab = leftTabs.getSelected();
        for (auto* c : std::initializer_list<juce::Component*> { &fixedVelToggle, &yVelToggle, &midiVelToggle, &velSlider, &swingSlider }) c->setVisible (tab == 0);
        roll.setVisible (tab == 1);
        for (auto* c : std::initializer_list<juce::Component*> { &pitchWheel, &modWheel, &touchSlider, &sustainBtn }) c->setVisible (tab == 2);
        for (auto* c : std::initializer_list<juce::Component*> { &xyTabs, &xyTop, &xyBottom }) c->setVisible (tab == 3);
        auto& e = Engine::get();
        fixedVelToggle.setToggleState (e.fixedVelocityOn.load(), juce::dontSendNotification);
        yVelToggle.setToggleState (e.yAxisVelocity.load(), juce::dontSendNotification);
        midiVelToggle.setToggleState (e.fixedMidiVelocity.load(), juce::dontSendNotification);
        velSlider.setEnabled (e.fixedVelocityOn.load());
        const auto col = padColour (e.selectedBank, e.selectedPad);
        for (auto* s : { &velSlider, &swingSlider, &pitchWheel, &modWheel, &touchSlider }) s->setColour (juce::Slider::trackColourId, col);
        leftTabs.setAccent (col);
    }

    void updateVisibility()
    {
        auto& e = Engine::get();
        const bool keys = currentBank().created && currentPad().keysMode && layout != station && e.padMode.load() != 4;
        const int per = padsPerPage();
        page = juce::jlimit (0, numPages() - 1, page);
        e.padPage = page;
        e.padsPerPage = per;
        for (int i = 0; i < 64; ++i)
        {
            pads[i]->index = juce::jmin (cr::numPads - 1, page * per + i);
            pads[i]->keyNote = keys ? keysNoteForPad (i) : -1;
            if (e.padMode.load() == 4 && layout != station) pads[i]->index = i;
            pads[i]->setVisible (i < per);
        }
        const bool st = layout == station;
        for (auto* s : stationFaders) s->setVisible (st);
        for (auto* k : stationKnobs) k->setVisible (st);
        for (auto* c : std::initializer_list<juce::Component*> { &steps, &stepPrev, &stepNext, &stationLofi, &stationFilter }) c->setVisible (st);
        piano.setVisible (showPiano && ! st);
        pageUp.setEnabled (keys || page < numPages() - 1);
        pageDown.setEnabled (keys || page > 0);
    }

    void changePage (int delta)
    {
        auto& e = Engine::get();
        if (currentBank().created && currentPad().keysMode && layout != station)
            e.keysOctave = juce::jlimit (0, 9, e.keysOctave.load() + delta);
        else page = juce::jlimit (0, numPages() - 1, page + delta);
        refresh();
    }

    void toggleMode (int mode)
    {
        auto& e = Engine::get();
        e.padMode = e.padMode.load() == mode ? 0 : mode;
        refresh();
    }

    void refreshRepeat()
    {
        auto& e = Engine::get();
        for (int i = 0; i < cr::numRepeatSlots; ++i)
        {
            juce::String name = RollPad::rateName (e.repeatTicks[(size_t) i]);
            if (e.repeatSwing[(size_t) i] > 0.0f) name << "s";
            repeatButtons[i]->setButtonText (name);
            const bool on = e.repeatSlot.load() == i;
            repeatButtons[i]->setToggleState (on, juce::dontSendNotification);
        }
        const int slot = e.repeatSlot.load() >= 0 ? e.repeatSlot.load() : lastRepeatSlot;
        swingSlider.setValue (e.repeatSwing[(size_t) slot] * 100.0f, juce::dontSendNotification);
    }

    void showRepeatMenu (int slot)
    {
        auto& e = Engine::get();
        static const int rateTicks[] = { 192, 96, 64, 48, 32, 24, 16, 12, 8, 6 };
        juce::PopupMenu m;
        m.addSectionHeader ("Repeat rate");
        for (int i = 0; i < 10; ++i) m.addItem (1 + i, RollPad::rateName (rateTicks[i]), true, e.repeatTicks[(size_t) slot] == rateTicks[i]);
        m.addSectionHeader ("Swing");
        for (int sw : { 0, 8, 16, 25, 33, 50 }) m.addItem (100 + sw, juce::String (sw) + "%", true, std::abs (e.repeatSwing[(size_t) slot] * 100.0f - (float) sw) < 0.5f);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (repeatArrows[slot]), [this, slot] (int r)
        {
            auto& en = Engine::get();
            if (r >= 1 && r <= 10) en.repeatTicks[(size_t) slot] = rateTicks[r - 1];
            else if (r >= 100) en.repeatSwing[(size_t) slot] = (float) (r - 100) / 100.0f;
            lastRepeatSlot = slot;
            refreshRepeat();
            roll.repaint();
        });
    }

    // -----------------------------------------------------------------------
    void padPressed (int idx, float vel, int key)
    {
        auto& e = Engine::get();
        const int b = e.selectedBank;
        if (! currentBank().created) e.createBank (b);
        if (key >= 0) { e.padDown (b, e.selectedPad, vel, key); return; }
        switch (e.padMode.load())
        {
            case 1: e.selectedPad = idx; ViewBase::broadcast(); return;
            case 2: case 3:
            {
                const juce::ScopedLock sl (e.getLock());
                auto& ch = e.session().banks[(size_t) b].pads[(size_t) idx].channel;
                if (e.padMode.load() == 2) ch.mute = ! ch.mute; else ch.solo = ! ch.solo;
                break;
            }
            case 4:
                if (idx < (int) e.session().scenes.size()) e.launchScene (idx);
                else e.captureScene (idx);
                break;
            default:
            {
                const bool changed = e.selectedPad != idx;
                e.selectedPad = idx;
                e.padDown (b, idx, vel);
                if (changed) ViewBase::broadcast();
                break;
            }
        }
        repaint();
        for (auto* p : pads) p->repaint();
    }

    void padReleased (int idx, int key)
    {
        auto& e = Engine::get();
        if (key >= 0) { e.padUp (e.selectedBank, e.selectedPad, key); return; }
        if (e.padMode.load() == 0) e.padUp (e.selectedBank, idx);
    }

    void padOperation (int op)
    {
        auto& e = Engine::get();
        const int b = e.selectedBank, p = e.selectedPad;
        switch (op)
        {
            case 0: e.copyPad (b, p); Nav::setStatus ("Pad " + juce::String (p + 1) + " copied"); break;
            case 1: e.pastePad (b, p); break;
            case 2: e.copyPad (b, p); e.clearPad (b, p); break;
            case 3: e.clearPad (b, p); break;
            case 4: showPadColourMenu (b, p, opButtons[4]); break;
            case 5:
                askForText ("Pad name", currentPad().name, [b, p] (const juce::String& n)
                {
                    Engine::get().session().banks[(size_t) b].pads[(size_t) p].name = n;
                    ViewBase::broadcast();
                });
                break;
            case 6: Nav::showView (Nav::editor); break;
            default: break;
        }
        ViewBase::broadcast();
    }

    void showPadMenu (int idx, juce::Component* target)
    {
        auto& e = Engine::get();
        const int b = e.selectedBank;
        e.selectedPad = idx;
        auto& pad = e.session().banks[(size_t) b].pads[(size_t) idx];
        juce::PopupMenu m;
        m.addSectionHeader ("Pad " + cr::bankLetter (b) + "." + juce::String (idx + 1) + (pad.name.isNotEmpty() ? "  " + pad.name : juce::String()));
        m.addItem (1, "Edit pad");
        m.addItem (2, "Record a sample onto this pad...");
        m.addItem (11, "Separate stems to a new bank (vocals, drums, bass, other)", e.padSample (b, idx) != nullptr);
        m.addSeparator();
        m.addItem (3, "Copy pad");
        m.addItem (4, "Paste pad", e.hasPadClipboard());
        m.addItem (5, "Cut pad");
        m.addItem (6, "Clear pad");
        m.addSeparator();
        m.addItem (7, "Rename...");
        m.addItem (8, "Colour...");
        m.addItem (9, "Keys mode (play chromatically)", true, pad.keysMode);
        m.addItem (10, "MIDI setup...");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [this, b, idx, target] (int r)
        {
            auto& en = Engine::get();
            switch (r)
            {
                case 1: Nav::showView (Nav::editor); break;
                case 2: showRecorderDialog (b, idx); break;
                case 3: en.copyPad (b, idx); break;
                case 4: en.pastePad (b, idx); break;
                case 5: en.copyPad (b, idx); en.clearPad (b, idx); break;
                case 6: en.clearPad (b, idx); break;
                case 7: padOperation (5); break;
                case 8: showPadColourMenu (b, idx, target); break;
                case 9: { const juce::ScopedLock sl (en.getLock()); auto& pd = en.session().banks[(size_t) b].pads[(size_t) idx]; pd.keysMode = ! pd.keysMode; break; }
                case 10: showPadMidiSetup (b, idx, target); break;
                case 11: separateStemsFromPad (b, idx); break;
                default: break;
            }
            ViewBase::broadcast();
        });
        ViewBase::broadcast();
    }

    void showBankMenu (int b, juce::Component* target)
    {
        auto& e = Engine::get();
        e.createBank (b);
        juce::PopupMenu m;
        m.addSectionHeader (e.session().banks[(size_t) b].name);
        m.addItem (1, "Rename bank...");
        m.addItem (2, "Bank colour...");
        m.addItem (3, "MIDI setup...");
        m.addSeparator();
        m.addItem (20, "New bank", e.firstFreeBank() >= 0);
        m.addItem (21, "Duplicate bank (pads, samples, settings, patterns)", e.firstFreeBank() >= 0);
        m.addItem (22, "Copy bank");
        m.addItem (23, "Paste bank over this one", e.hasBankClipboard());
        m.addSeparator();
        m.addItem (4, "Load bank preset...");
        m.addItem (5, "Save as bank preset...");
        m.addSeparator();
        m.addItem (6, "Clear all pads");
        m.addItem (7, "Delete bank (pads, patterns and song clips)");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [b, target] (int r)
        {
            auto& en = Engine::get();
            auto& bank = en.session().banks[(size_t) b];
            if (r == 1) askForText ("Bank name", bank.name, [b] (const juce::String& n) { auto& bk = Engine::get().session().banks[(size_t) b]; bk.name = n; bk.strip.name = n; ViewBase::broadcast(); });
            else if (r == 2)
            {
                juce::PopupMenu cm;
                const auto& cols = Pal::padColours();
                for (int i = 0; i < cols.size(); ++i)
                {
                    juce::Image img (juce::Image::ARGB, 14, 14, true);
                    { juce::Graphics g (img); g.setColour (cols[i]); g.fillRoundedRectangle (0, 0, 14, 14, 3); }
                    juce::PopupMenu::Item it ("Colour " + juce::String (i + 1));
                    it.itemID = 1 + i;
                    it.image = std::make_unique<juce::DrawableImage> (img);
                    cm.addItem (std::move (it));
                }
                cm.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [b] (int c)
                {
                    if (c > 0) { Engine::get().session().banks[(size_t) b].colour = Pal::padColours()[c - 1]; ViewBase::broadcast(); }
                });
            }
            else if (r == 3) showBankMidiSetup (b, target);
            else if (r >= 20 && r <= 23)
            {
                juce::String err;
                if (r == 20) { const int f = en.firstFreeBank(); if (f >= 0) { en.checkpoint(); en.createBank (f); en.selectedBank = f; en.selectedPad = 0; } }
                else if (r == 21) { const int f = en.duplicateBank (b, err); if (f >= 0) { en.selectedBank = f; Nav::setStatus ("Duplicated to " + cr::bankLetter (f)); } }
                else if (r == 22) { if (en.copyBank (b, err)) Nav::setStatus ("Copied " + bank.name); }
                else if (r == 23) en.pasteBank (b, err);
                if (err.isNotEmpty()) showError ("Banks", err);
            }
            else if (r == 4 || r == 5) { en.selectedBank = b; Nav::setStatus ("Use the menu at the top left: Banks"); }
            else if (r == 6) { for (int p = 0; p < cr::numPads; ++p) if (bank.pads[(size_t) p].hasSound() || bank.pads[(size_t) p].name.isNotEmpty()) en.clearPad (b, p); }
            else if (r == 7) en.deleteBank (b);
            ViewBase::broadcast();
        });
    }

    void timerCallback() override
    {
        if (! isShowing()) return;
        auto& e = Engine::get();
        const int b = e.selectedBank;
        for (auto* p : pads)
        {
            if (! p->isVisible() || p->keyNote >= 0) continue;
            const float f = e.padFlash[b][p->index].load();
            if (f > 0.004f) { e.padFlash[b][p->index] = f * 0.82f; p->repaint(); }
            else if (f > 0.0f) { e.padFlash[b][p->index] = 0.0f; p->repaint(); }
        }
        if (steps.isVisible() && e.isPlaying()) steps.repaint();
        if (++ticks % 6 == 0)
        {
            for (int i = 0; i < cr::numRepeatSlots; ++i) repeatButtons[i]->setToggleState (e.repeatSlot.load() == i, juce::dontSendNotification);
            if (leftTabs.getSelected() == 3) { xyTop.repaint(); xyBottom.repaint(); }
            if (e.padMode.load() != lastMode) { lastMode = e.padMode.load(); refresh(); }
            if (leftTabs.getSelected() == 2 && ! modWheel.isMouseButtonDown())
                modWheel.setValue (e.modWheel[(size_t) b].load(), juce::dontSendNotification);
        }
    }

    void handleNoteOn (juce::MidiKeyboardState*, int, int note, float velocity) override
    {
        auto& e = Engine::get();
        e.padDown (e.selectedBank, e.selectedPad, e.fixedVelocityOn.load() ? e.fixedVelocity.load() : velocity, note);
    }
    void handleNoteOff (juce::MidiKeyboardState*, int, int note, float) override
    {
        auto& e = Engine::get();
        e.padUp (e.selectedBank, e.selectedPad, note);
    }

    // ---- members --------------------------------------------------------------------------
    int layout = grid16, page = 0, lastMode = -1, lastRepeatSlot = 0, ticks = 0;
    bool showPiano = false;
    juce::Rectangle<int> leftArea, rightArea, bottomArea, leftHeader, velCaption, swingCaption, repeatCaption, pageCaption, pageValue, hintArea,
                         stationFaderCaption, stationKnobCaption, stationStepCaption;
    IconButton bankMenuBtn { Icon::dots, "Bank options" };
    TabStrip leftTabs;
    juce::ToggleButton fixedVelToggle, yVelToggle, midiVelToggle;
    juce::Slider velSlider, swingSlider, pitchWheel, modWheel, touchSlider;
    juce::TextButton sustainBtn { "Sustain" };
    RollPad roll;
    TabStrip xyTabs;
    XYPad xyTop, xyBottom;
    juce::OwnedArray<juce::TextButton> repeatButtons;
    juce::OwnedArray<IconButton> repeatArrows;
    BankTabs bankTabs;
    juce::OwnedArray<PadComponent> pads;
    juce::OwnedArray<Knob> stationKnobs;
    juce::OwnedArray<juce::Slider> stationFaders;
    StepPads steps;
    IconButton stepPrev { Icon::left, "Previous bar" }, stepNext { Icon::right, "Next bar" };
    ChoiceField stationLofi { "Lo-fi" }, stationFilter { "Filter" };
    SideButton keysBtn { "Keys", Pal::accent }, selectBtn { "Select", Pal::info }, sceneBtn { "Scene", Pal::play },
               muteBtn { "Mute", Pal::mute }, soloBtn { "Solo", Pal::solo };
    IconButton pageUp { Icon::up, "Next pad page / octave up" }, pageDown { Icon::down, "Previous pad page / octave down" };
    juce::OwnedArray<juce::TextButton> layoutButtons, opButtons;
    IconButton pianoBtn { Icon::keyboard, "Show a keyboard under the pads" };
    juce::TextButton chordsBtn { "Chords & Scales" };
    juce::TextButton captureBtn { "Capture new scene" }, stopScenesBtn { "Stop all patterns" }, scenesEditBtn { "Edit scenes" };
    juce::MidiKeyboardState keyboardState;
    juce::MidiKeyboardComponent piano { keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard };
};
} // namespace

std::unique_ptr<ViewBase> createPerformanceView() { return std::make_unique<PerformanceView>(); }

// ============================================================================
// Computer keyboard -> pads (4 x 4 grid, bottom row Z X C V = pads 1-4)
// ============================================================================
namespace
{
const char* padKeys = "zxcvasdfqwer1234";
std::array<bool, 16> padKeyDown {};
}

bool handlePadKeyDown (const juce::KeyPress& key)
{
    if (key.getModifiers().isAnyModifierKeyDown() || Nav::currentView() != Nav::performance) return false;
    const auto ch = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
    for (int i = 0; i < 16; ++i)
        if (padKeys[i] == (char) ch)
        {
            if (! padKeyDown[(size_t) i])
            {
                padKeyDown[(size_t) i] = true;
                auto& e = Engine::get();
                const int pad = e.padPage.load() * e.padsPerPage.load() + i;
                if (currentPad().keysMode) e.padDown (e.selectedBank, e.selectedPad, e.fixedVelocity.load(), keysNoteForPad (i));
                else if (pad < cr::numPads) { e.selectedPad = pad; e.padDown (e.selectedBank, pad, e.fixedVelocity.load()); }
            }
            return true;
        }
    return false;
}

void handlePadKeyStates()
{
    auto& e = Engine::get();
    for (int i = 0; i < 16; ++i)
    {
        if (! padKeyDown[(size_t) i]) continue;
        if (juce::KeyPress::isKeyCurrentlyDown ((int) padKeys[i]) || juce::KeyPress::isKeyCurrentlyDown ((int) juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) padKeys[i])))
            continue;
        padKeyDown[(size_t) i] = false;
        const int pad = e.padPage.load() * e.padsPerPage.load() + i;
        if (currentPad().keysMode) e.padUp (e.selectedBank, e.selectedPad, keysNoteForPad (i));
        else if (pad < cr::numPads) e.padUp (e.selectedBank, pad);
    }
}
