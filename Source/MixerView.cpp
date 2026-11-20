// Mixer view: main, bank, pad, audio and AUX channel strips; unlimited inserts; sends; I/O.
#include "Views.h"

namespace
{
juce::String stripName (int key)
{
    auto& s = Engine::get().session();
    if (key == 3000) return "Main out";
    if (key < cr::numBanks) return s.banks[(size_t) key].name;
    if (key >= 1000 && key < 2000) return s.audioTracks[(size_t) (key - 1000)].name;
    if (key >= 2000 && key < 3000) return s.aux[(size_t) (key - 2000)].name;
    const int k = key - 10000;
    const auto& p = s.banks[(size_t) (k / cr::numPads)].pads[(size_t) (k % cr::numPads)];
    return p.name.isNotEmpty() ? p.name : "Pad " + juce::String (k % cr::numPads + 1);
}

juce::Colour stripColour (int key)
{
    auto& s = Engine::get().session();
    if (key == 3000) return Pal::rec;
    if (key < cr::numBanks) return s.banks[(size_t) key].colour;
    if (key >= 1000 && key < 2000) return s.audioTracks[(size_t) (key - 1000)].colour;
    if (key >= 2000 && key < 3000) return Pal::solo;
    const int k = key - 10000;
    return padColour (k / cr::numPads, k % cr::numPads);
}

juce::String stripBadge (int key)
{
    if (key == 3000) return "O";
    if (key < cr::numBanks) return cr::bankLetter (key);
    if (key >= 1000 && key < 2000) return "~";
    if (key >= 2000 && key < 3000) return "FX";
    return juce::String ((key - 10000) % cr::numPads + 1);
}

// dB <-> fader position (0..1); +2 dB at the top
float gainToPos (float g)
{
    const float db = gainToDb (g);
    if (db <= -70.0f) return 0.0f;
    return juce::jlimit (0.0f, 1.0f, std::pow ((db + 70.0f) / 72.0f, 2.2f));
}
float posToGain (float pos)
{
    if (pos <= 0.0005f) return 0.0f;
    return juce::jmin (1.25f, dbToGain (std::pow (pos, 1.0f / 2.2f) * 72.0f - 70.0f));
}

// ============================================================================
class FaderMeter : public juce::Component
{
public:
    int key = 0;
    juce::Colour colour = Pal::accent;

    void paint (juce::Graphics& g) override
    {
        auto* st = Engine::get().getStrip (key);
        if (st == nullptr) return;
        auto r = getLocalBounds().toFloat();
        auto scale = r.removeFromLeft (22.0f);
        auto meters = r.removeFromLeft (16.0f).reduced (1.0f, 6.0f);
        auto fader = r.reduced (2.0f, 6.0f);
        // scale
        g.setFont (Fonts::ui (8.5f));
        for (int db : { 6, 0, -6, -12, -24, -36, -48 })
        {
            const float y = fader.getBottom() - gainToPos (dbToGain ((float) db)) * fader.getHeight();
            if (y < fader.getY() - 2.0f) continue;
            g.setColour (Pal::textFaint);
            g.drawText (juce::String (db), scale.withY (y - 6.0f).withHeight (12.0f).toNearestInt(), juce::Justification::centredRight);
            g.setColour (Pal::outline);
            g.fillRect (meters.getX(), y, meters.getWidth() + fader.getWidth() + 4.0f, 1.0f);
        }
        // meters
        const float ml = st->meterL.load(), mr = st->meterR.load();
        const float w = (meters.getWidth() - 2.0f) / 2.0f;
        for (int c = 0; c < 2; ++c)
        {
            auto bar = juce::Rectangle<float> (meters.getX() + c * (w + 2.0f), meters.getY(), w, meters.getHeight());
            g.setColour (Pal::bg);
            g.fillRect (bar);
            const float lv = gainToPos (c == 0 ? ml : mr);
            auto lit = bar.withTop (bar.getBottom() - lv * bar.getHeight());
            juce::ColourGradient grad (Pal::play, bar.getX(), bar.getBottom(), Pal::rec, bar.getX(), bar.getY(), false);
            grad.addColour (0.82, Pal::solo);
            g.setGradientFill (grad);
            g.fillRect (lit);
        }
        // fader
        const float pos = gainToPos (st->volume);
        auto track = fader.withSizeKeepingCentre (4.0f, fader.getHeight());
        g.setColour (Pal::surface3);
        g.fillRoundedRectangle (track, 2.0f);
        const float y = fader.getBottom() - pos * fader.getHeight();
        juce::Path arrow;
        arrow.addTriangle (fader.getRight() - 2.0f, y - 8.0f, fader.getRight() - 2.0f, y + 8.0f, fader.getCentreX() - 6.0f, y);
        g.setColour (colour);
        g.fillPath (arrow);
        g.fillRect (fader.getCentreX() - 6.0f, y - 1.0f, fader.getWidth() * 0.5f, 2.0f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto* st = Engine::get().getStrip (key);
        if (st == nullptr) return;
        if (e.mods.isPopupMenu()) { showMenu(); return; }
        startPos = gainToPos (st->volume);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto* st = Engine::get().getStrip (key);
        if (st == nullptr) return;
        const float h = (float) getHeight() - 12.0f;
        const float scale = e.mods.isShiftDown() ? 0.2f : 1.0f;
        const float pos = juce::jlimit (0.0f, 1.0f, startPos - (float) e.getDistanceFromDragStartY() / h * scale);
        Engine::get().setParamValue (cr::ParamAddress::parse (stripPath (key) + "vol"), posToGain (pos), true);
        if (onChange) onChange();
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        Engine::get().setParamValue (cr::ParamAddress::parse (stripPath (key) + "vol"), key == 3000 ? 0.9f : 0.8f, true);
        if (onChange) onChange();
        repaint();
    }

    std::function<void()> onChange;

private:
    void showMenu()
    {
        const auto path = stripPath (key) + "vol";
        auto& e = Engine::get();
        juce::PopupMenu m;
        m.addItem (1, "MIDI learn");
        m.addItem (2, "Forget MIDI mapping", e.hasMidiMapping (path));
        juce::PopupMenu macros;
        for (int i = 0; i < cr::numMacros; ++i) macros.addItem (100 + i, e.session().macros[(size_t) i].name);
        m.addSubMenu ("Assign to macro", macros);
        m.addItem (3, "Add song automation lane");
        m.addItem (4, "Set to 0 dB");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, path] (int r)
        {
            auto& en = Engine::get();
            if (r == 1) { en.armMidiLearn (path); Nav::setStatus ("MIDI learn: move a controller"); }
            else if (r == 2) en.forgetMidiMapping (path);
            else if (r == 3) { auto* l = en.findOrCreateLane (path, true, cr::ParamAddress::parse (path).bankIndex()); if (l && l->points.empty()) l->points.push_back ({ 0, en.getParamNormalised (l->address) }); Nav::showAutomation (true, cr::ParamAddress::parse (path).bankIndex(), path); }
            else if (r == 4) en.setParamValue (cr::ParamAddress::parse (path), 1.0f, true);
            else if (r >= 100) en.bindMacro (r - 100, path);
            repaint();
        });
    }
    float startPos = 0.0f;
};

// ============================================================================
class StripComponent : public juce::Component
{
public:
    int key = 0;
    bool selected = false, showAllSends = false, ioMode = false;
    std::function<void (int)> onSelect;

    StripComponent()
    {
        addAndMakeVisible (fader);
        addAndMakeVisible (pan);
        pan.setCompact (true);
        for (int i = 0; i < cr::numAux; ++i) { auto* k = sends.add (new Knob()); k->setCompact (true); addChildComponent (k); }
        muteBtn.setClickingTogglesState (true);
        soloBtn.setClickingTogglesState (true);
        muteBtn.setColour (juce::TextButton::buttonOnColourId, Pal::mute);
        soloBtn.setColour (juce::TextButton::buttonOnColourId, Pal::solo);
        muteBtn.onClick = [this] { if (auto* st = Engine::get().getStrip (key)) st->mute = muteBtn.getToggleState(); };
        soloBtn.onClick = [this] { if (auto* st = Engine::get().getStrip (key)) st->solo = soloBtn.getToggleState(); };
        addAndMakeVisible (muteBtn);
        addAndMakeVisible (soloBtn);
        armBtn.setClickingTogglesState (true);
        armBtn.setOnColour (Pal::rec);
        armBtn.onClick = [this] { if (key >= 1000 && key < 2000) Engine::get().session().audioTracks[(size_t) (key - 1000)].armed = armBtn.getToggleState(); };
        monBtn.setClickingTogglesState (true);
        monBtn.onClick = [this] { if (key >= 1000 && key < 2000) Engine::get().session().audioTracks[(size_t) (key - 1000)].monitor = monBtn.getToggleState(); };
        addChildComponent (armBtn);
        addChildComponent (monBtn);
        inputField.onClick = [this] { inputMenu(); };
        inputField.getText = [this] { return inputText(); };
        outputField.onClick = [this] { outputMenu(); };
        outputField.getText = [this]
        {
            auto* st = Engine::get().getStrip (key);
            if (st == nullptr || key == 3000) return juce::String ("1 + 2");
            if (key >= 10000) return st->output == 0 ? juce::String ("Bank") : Engine::get().getOutputPairNames()[st->output];
            return st->output == 0 ? juce::String ("Main") : Engine::get().getOutputPairNames()[st->output];
        };
        addChildComponent (inputField);
        addChildComponent (outputField);
        addChildComponent (inputGain);
        addChildComponent (gateKnob);
        inputGain.setCompact (true);
        gateKnob.setCompact (true);
        fader.onChange = [this] { repaint(); };
    }

    void setKey (int k, bool sel, bool allSends, bool io)
    {
        key = k; selected = sel; showAllSends = allSends; ioMode = io;
        auto& e = Engine::get();
        auto* st = e.getStrip (key);
        const auto col = stripColour (key);
        fader.key = key;
        fader.colour = col;
        pan.setAccent (col);
        pan.setParam (stripPath (key) + "pan", "Pan");
        const bool hasSends = key != 3000 && ! (key >= 2000 && key < 3000);
        const int nSends = showAllSends ? e.session().auxCount : juce::jmin (2, e.session().auxCount);
        for (int i = 0; i < cr::numAux; ++i)
        {
            const bool show = hasSends && i < nSends && ! ioMode;
            sends[i]->setVisible (show);
            if (show) { sends[i]->setAccent (col); sends[i]->setParam (stripPath (key) + "send" + juce::String (i), e.session().aux[(size_t) i].name); }
        }
        if (st) { muteBtn.setToggleState (st->mute, juce::dontSendNotification); soloBtn.setToggleState (st->solo, juce::dontSendNotification); }
        soloBtn.setVisible (key != 3000);
        const bool audio = key >= 1000 && key < 2000;
        armBtn.setVisible (audio);
        monBtn.setVisible (audio);
        if (audio)
        {
            auto& tr = e.session().audioTracks[(size_t) (key - 1000)];
            armBtn.setToggleState (tr.armed, juce::dontSendNotification);
            monBtn.setToggleState (tr.monitor, juce::dontSendNotification);
            inputGain.setAccent (col);
            inputGain.setCustom (-24.0f, 24.0f, 0.0f, [&tr] { return tr.inputGainDb; }, [&tr] (float v) { tr.inputGainDb = v; }, [] (float v) { return juce::String (v, 1) + " dB"; });
            gateKnob.setAccent (col);
            gateKnob.setCustom (-80.0f, 0.0f, -50.0f, [&tr] { return tr.gate ? tr.gateThresholdDb : -80.0f; },
                                [&tr] (float v) { tr.gate = v > -79.5f; tr.gateThresholdDb = v; },
                                [] (float v) { return v <= -79.5f ? juce::String ("Off") : juce::String ((int) v) + " dB"; });
        }
        inputField.setVisible (ioMode && audio);
        inputGain.setVisible (ioMode && audio);
        gateKnob.setVisible (ioMode && audio);
        outputField.setVisible (ioMode);
        inputField.accent = outputField.accent = col;
        resized();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto& e = Engine::get();
        auto* st = e.getStrip (key);
        auto r = getLocalBounds().toFloat();
        const auto col = stripColour (key);
        g.setColour (selected ? Pal::surface2 : Pal::panel);
        g.fillRect (r.reduced (1.0f, 0.0f));
        // header
        auto hdr = juce::Rectangle<float> (r.getX() + 4.0f, 6.0f, r.getWidth() - 8.0f, 26.0f);
        g.setColour (selected ? col : Pal::surface);
        g.fillRoundedRectangle (hdr, 4.0f);
        auto badge = hdr.removeFromLeft (22.0f).reduced (3.0f);
        g.setColour (selected ? Pal::bg : col);
        g.fillRoundedRectangle (badge, 2.5f);
        g.setColour (selected ? col : Pal::bg);
        g.setFont (Fonts::heavy (10.0f));
        g.drawText (stripBadge (key), badge, juce::Justification::centred);
        g.setColour (selected ? Pal::bg : Pal::text);
        g.setFont (Fonts::ui (11.0f, true));
        g.drawFittedText (stripName (key), hdr.reduced (4.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
        if (st == nullptr) return;
        // peak / volume readout
        auto ro = juce::Rectangle<int> (4, 36, getWidth() - 8, 26);
        const float peak = st->peakHold.load();
        const float vdb = gainToDb (st->volume);
        g.setFont (Fonts::digits (11.0f));
        g.setColour (peak > 1.0f ? Pal::rec : Pal::textDim);
        g.drawText (peak > 0.00001f ? juce::String (gainToDb (peak), 1) : juce::String ("-inf"), ro.removeFromLeft (ro.getWidth() / 2), juce::Justification::centredTop);
        g.setColour (Pal::text);
        g.drawText (vdb <= -70.0f ? juce::String ("-inf") : juce::String (vdb, 1), ro, juce::Justification::centredTop);
        drawCaps (g, "peak", juce::Rectangle<int> (4, 50, (getWidth() - 8) / 2, 12), juce::Justification::centred, Pal::textFaint, 8.0f);
        drawCaps (g, "volume", juce::Rectangle<int> (4 + (getWidth() - 8) / 2, 50, (getWidth() - 8) / 2, 12), juce::Justification::centred, Pal::textFaint, 8.0f);
        if (ioMode)
        {
            const bool audio = key >= 1000 && key < 2000;
            drawCaps (g, audio ? "Input" : "", inputCaption, juce::Justification::centred, Pal::textFaint, 9.0f);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (4, 0);
        r.removeFromTop (64);
        auto bottom = r.removeFromBottom (28);
        muteBtn.setBounds (bottom.removeFromLeft (bottom.getWidth() / 2).reduced (1, 2));
        soloBtn.setBounds (bottom.reduced (1, 2));
        if (armBtn.isVisible())
        {
            auto ar = r.removeFromBottom (28);
            armBtn.setBounds (ar.removeFromLeft (ar.getWidth() / 2).reduced (6, 2));
            monBtn.setBounds (ar.reduced (6, 2));
        }
        if (ioMode)
        {
            outputField.setBounds (r.removeFromBottom (40));
            if (inputField.isVisible())
            {
                gateKnob.setBounds (r.removeFromBottom (60));
                inputGain.setBounds (r.removeFromBottom (60));
                inputField.setBounds (r.removeFromBottom (40));
            }
        }
        else
        {
            for (int i = cr::numAux - 1; i >= 0; --i)
                if (sends[i]->isVisible()) sends[i]->setBounds (r.removeFromBottom (58));
        }
        pan.setBounds (r.removeFromBottom (58));
        r.removeFromBottom (4);
        fader.setBounds (r);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.y < 36)
        {
            if (onSelect) onSelect (key);
            if (e.mods.isPopupMenu() || e.getNumberOfClicks() > 1) renameMenu();
        }
    }

    void refreshMeters() { fader.repaint(); repaint (0, 36, getWidth(), 28); }

private:
    juce::String inputText() const
    {
        if (key < 1000 || key >= 2000) return {};
        auto& e = Engine::get();
        auto& tr = e.session().audioTracks[(size_t) (key - 1000)];
        if (tr.inputSource == 0) { auto pairs = e.getInputPairNames(); return tr.inputMono ? "In " + juce::String (tr.inputPair + 1) : pairs[tr.inputPair]; }
        if (tr.inputSource == 1) return "Main out";
        if (tr.inputSource >= 2 && tr.inputSource < 2 + cr::numBanks) return e.session().banks[(size_t) (tr.inputSource - 2)].name;
        return e.session().aux[(size_t) (tr.inputSource - 200)].name;
    }

    void inputMenu()
    {
        auto& e = Engine::get();
        auto& tr = e.session().audioTracks[(size_t) (key - 1000)];
        juce::PopupMenu m;
        const auto pairs = e.getInputPairNames();
        juce::PopupMenu hw, mono, internal;
        for (int i = 0; i < pairs.size(); ++i) hw.addItem (1 + i, pairs[i], true, tr.inputSource == 0 && ! tr.inputMono && tr.inputPair == i);
        for (int i = 0; i < pairs.size() * 2; ++i) mono.addItem (100 + i, "Input " + juce::String (i + 1), true, tr.inputSource == 0 && tr.inputMono && tr.inputPair == i);
        internal.addItem (300, "Main out (resample)", true, tr.inputSource == 1);
        for (int b = 0; b < cr::numBanks; ++b) if (e.session().banks[(size_t) b].created) internal.addItem (400 + b, e.session().banks[(size_t) b].name, true, tr.inputSource == 2 + b);
        for (int a = 0; a < e.session().auxCount; ++a) internal.addItem (600 + a, e.session().aux[(size_t) a].name, true, tr.inputSource == 200 + a);
        m.addSubMenu ("Stereo inputs", hw);
        m.addSubMenu ("Mono inputs", mono);
        m.addSubMenu ("Internal", internal);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&inputField), [this] (int r)
        {
            auto& t = Engine::get().session().audioTracks[(size_t) (key - 1000)];
            if (r >= 600) t.inputSource = 200 + (r - 600);
            else if (r >= 400) t.inputSource = 2 + (r - 400);
            else if (r == 300) t.inputSource = 1;
            else if (r >= 100) { t.inputSource = 0; t.inputMono = true; t.inputPair = r - 100; }
            else if (r >= 1) { t.inputSource = 0; t.inputMono = false; t.inputPair = r - 1; }
            inputField.repaint();
        });
    }

    void outputMenu()
    {
        auto* st = Engine::get().getStrip (key);
        if (st == nullptr || key == 3000) return;
        const auto outs = Engine::get().getOutputPairNames();
        juce::PopupMenu m;
        m.addItem (1, key >= 10000 ? "Through the bank" : "Main out", true, st->output == 0);
        for (int i = 1; i < outs.size(); ++i) m.addItem (1 + i, outs[i], true, st->output == i);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&outputField), [this] (int r)
        {
            if (auto* s2 = Engine::get().getStrip (key)) if (r > 0) s2->output = r - 1;
            outputField.repaint();
        });
    }

    void renameMenu()
    {
        if (key == 3000) return;
        askForText ("Name", stripName (key), [k = key] (const juce::String& n)
        {
            auto& s = Engine::get().session();
            if (k < cr::numBanks) { s.banks[(size_t) k].name = n; s.banks[(size_t) k].strip.name = n; }
            else if (k >= 1000 && k < 2000) { s.audioTracks[(size_t) (k - 1000)].name = n; s.audioTracks[(size_t) (k - 1000)].strip.name = n; }
            else if (k >= 2000 && k < 3000) s.aux[(size_t) (k - 2000)].name = n;
            else { const int kk = k - 10000; s.banks[(size_t) (kk / cr::numPads)].pads[(size_t) (kk % cr::numPads)].name = n; }
            ViewBase::broadcast();
        });
    }

    FaderMeter fader;
    Knob pan { "Pan" };
    juce::OwnedArray<Knob> sends;
    juce::TextButton muteBtn { "M" }, soloBtn { "S" };
    IconButton armBtn { Icon::record, "Arm for recording", true }, monBtn { Icon::headphones, "Monitor the input", true };
    ChoiceField inputField { "Input" }, outputField { "Output" };
    Knob inputGain { "In gain" }, gateKnob { "Gate" };
    juce::Rectangle<int> inputCaption;
};

// ============================================================================
class MixerView : public ViewBase, private juce::Timer
{
public:
    MixerView()
    {
        addAndMakeVisible (chain);
        chain.onChanged = [this] { repaint(); };
        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (false, true);
        viewport.setScrollBarThickness (8);
        addAndMakeVisible (viewport);
        addTrackBtn.onClick = [this] { addTrackMenu(); };
        showPadsBtn.setClickingTogglesState (true);
        showPadsBtn.onClick = [this] { refresh(); };
        allSendsBtn.setClickingTogglesState (true);
        allSendsBtn.onClick = [this] { refresh(); };
        ioBtn.setClickingTogglesState (true);
        ioBtn.onClick = [this] { refresh(); };
        sendModeBtn.onClick = [this] { sendModeMenu(); };
        for (auto* b : { &addTrackBtn, &showPadsBtn, &allSendsBtn, &ioBtn, &sendModeBtn }) addAndMakeVisible (b);
        lockBtn.setClickingTogglesState (true);
        addAndMakeVisible (lockBtn);
        startTimerHz (30);
    }

    void refresh() override
    {
        auto& e = Engine::get();
        keys.clear();
        keys.push_back (3000);
        if (showPadsBtn.getToggleState())
        {
            const int b = e.selectedBank;
            keys.push_back (b);
            for (int p = 0; p < cr::numPads; ++p) if (e.session().banks[(size_t) b].pads[(size_t) p].hasSound()) keys.push_back (10000 + b * cr::numPads + p);
        }
        else
        {
            for (int b = 0; b < cr::numBanks; ++b) if (e.session().banks[(size_t) b].created) keys.push_back (b);
            for (int t = 0; t < cr::maxAudioTracks; ++t) if (e.session().audioTracks[(size_t) t].created) keys.push_back (1000 + t);
        }
        for (int a = 0; a < e.session().auxCount; ++a) keys.push_back (2000 + a);
        if (std::find (keys.begin(), keys.end(), selectedKey) == keys.end() && ! lockBtn.getToggleState()) selectedKey = keys.size() > 1 ? keys[1] : 3000;

        while (strips.size() < (int) keys.size()) { auto* s = strips.add (new StripComponent()); s->onSelect = [this] (int k) { select (k); }; content.addAndMakeVisible (s); }
        while (strips.size() > (int) keys.size()) strips.removeLast();
        for (int i = 0; i < (int) keys.size(); ++i) strips[i]->setKey (keys[(size_t) i], keys[(size_t) i] == selectedKey, allSendsBtn.getToggleState(), ioBtn.getToggleState());
        updateChain();
        resized();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Pal::bg);
        g.setColour (Pal::panel);
        g.fillRect (fxArea);
        g.fillRect (bottomBar);
        g.setColour (Pal::outline);
        g.fillRect (fxArea.getRight(), fxArea.getY(), 1, fxArea.getHeight());
        g.fillRect (bottomBar.getX(), bottomBar.getY(), bottomBar.getWidth(), 1);
        auto hdr = fxHeader;
        auto badge = hdr.removeFromLeft (28).withSizeKeepingCentre (26, 26).toFloat();
        const auto col = stripColour (selectedKey);
        g.setColour (col);
        g.fillRoundedRectangle (badge, 3.0f);
        g.setColour (Pal::bg);
        g.setFont (Fonts::heavy (11.0f));
        g.drawText (stripBadge (selectedKey), badge, juce::Justification::centred);
        hdr.removeFromLeft (10);
        g.setColour (Pal::text);
        g.setFont (Fonts::ui (14.0f, true));
        g.drawFittedText (stripName (selectedKey), hdr.withTrimmedRight (34), juce::Justification::centredLeft, 1);
        drawCaps (g, "Insert effects", fxHeader.translated (0, 30).withHeight (16), juce::Justification::centredLeft, Pal::textFaint, 9.5f);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        bottomBar = r.removeFromBottom (44);
        auto bb = bottomBar.reduced (12, 7);
        addTrackBtn.setBounds (bb.removeFromLeft (120)); bb.removeFromLeft (6);
        showPadsBtn.setBounds (bb.removeFromLeft (110)); bb.removeFromLeft (6);
        allSendsBtn.setBounds (bb.removeFromLeft (100)); bb.removeFromLeft (6);
        sendModeBtn.setBounds (bb.removeFromLeft (130)); bb.removeFromLeft (6);
        ioBtn.setBounds (bb.removeFromLeft (60));
        fxArea = r.removeFromLeft (juce::jlimit (280, 360, getWidth() / 4));
        auto f = fxArea.reduced (12, 10);
        fxHeader = f.removeFromTop (30);
        lockBtn.setBounds (fxHeader.removeFromRight (28).withSizeKeepingCentre (28, 28));
        f.removeFromTop (26);
        chain.setBounds (f);
        viewport.setBounds (r);
        const int w = 98;
        const int h = r.getHeight() - (viewport.isHorizontalScrollBarShown() ? 8 : 0);
        content.setSize (juce::jmax (r.getWidth(), w * strips.size()), h);
        for (int i = 0; i < strips.size(); ++i) strips[i]->setBounds (i * w, 0, w, h);
    }

private:
    void select (int k)
    {
        selectedKey = k;
        auto& e = Engine::get();
        if (k < cr::numBanks) e.selectedBank = k;
        else if (k >= 10000) e.selectedPad = (k - 10000) % cr::numPads;
        for (int i = 0; i < strips.size(); ++i) { strips[i]->selected = strips[i]->key == k; strips[i]->repaint(); }
        updateChain();
        repaint();
    }

    void updateChain()
    {
        auto* st = Engine::get().getStrip (selectedKey);
        if (st) chain.setChain (&st->inserts, stripPath (selectedKey), stripColour (selectedKey));
        else chain.clear();
    }

    void addTrackMenu()
    {
        auto& e = Engine::get();
        juce::PopupMenu m;
        m.addItem (1, "Audio track");
        m.addItem (2, "AUX return", e.session().auxCount < cr::numAux);
        int nextBank = -1;
        for (int b = 0; b < cr::numBanks; ++b) if (! e.session().banks[(size_t) b].created) { nextBank = b; break; }
        m.addItem (3, "Bank" + (nextBank >= 0 ? " (" + cr::bankName (nextBank) + ")" : juce::String()), nextBank >= 0);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addTrackBtn), [this, nextBank] (int r)
        {
            auto& en = Engine::get();
            if (r == 1) { const int t = en.createAudioTrack(); if (t >= 0) selectedKey = 1000 + t; }
            else if (r == 2) { en.session().auxCount = juce::jmin (cr::numAux, en.session().auxCount + 1); selectedKey = 2000 + en.session().auxCount - 1; }
            else if (r == 3 && nextBank >= 0) { en.createBank (nextBank); selectedKey = nextBank; }
            ViewBase::broadcast();
        });
    }

    void sendModeMenu()
    {
        auto* st = Engine::get().getStrip (selectedKey);
        if (st == nullptr) return;
        juce::PopupMenu m;
        m.addSectionHeader ("Sends of " + stripName (selectedKey));
        for (int a = 0; a < Engine::get().session().auxCount; ++a)
            m.addItem (1 + a, Engine::get().session().aux[(size_t) a].name + (st->sendPre[a] ? "  (pre-fader)" : "  (post-fader)"));
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&sendModeBtn), [this] (int r)
        {
            if (auto* s2 = Engine::get().getStrip (selectedKey)) if (r > 0) s2->sendPre[r - 1] = ! s2->sendPre[r - 1];
        });
    }

    void timerCallback() override
    {
        if (! isShowing()) return;
        for (auto* s : strips) s->refreshMeters();
        if (++ticks % 30 == 0)
            for (int i = 0; i < (int) keys.size(); ++i)
                if (auto* st = Engine::get().getStrip (keys[(size_t) i])) st->peakHold = st->peakHold.load() * 0.5f;
    }

    InsertChainView chain;
    juce::Viewport viewport;
    juce::Component content;
    juce::OwnedArray<StripComponent> strips;
    std::vector<int> keys;
    int selectedKey = 0, ticks = 0;
    juce::TextButton addTrackBtn { "+ Add track" }, showPadsBtn { "Show pads" }, allSendsBtn { "All sends" }, ioBtn { "I/O" }, sendModeBtn { "Pre / post sends" };
    IconButton lockBtn { Icon::lock, "Keep this channel's effects shown", true };
    juce::Rectangle<int> fxArea, fxHeader, bottomBar;
};
} // namespace

std::unique_ptr<ViewBase> createMixerView() { return std::make_unique<MixerView>(); }
