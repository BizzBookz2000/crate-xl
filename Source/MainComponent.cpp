#include "MainComponent.h"

MainComponent* MainComponent::instance = nullptr;

// ============================================================================
// Nav (used by every view)
// ============================================================================
namespace Nav
{
void showView (int v) { if (auto* m = MainComponent::instance) m->showView (v); }
int currentView() { return MainComponent::instance ? MainComponent::instance->getView() : 0; }
void showSequencer (int mode) { if (auto* m = MainComponent::instance) { m->sequencerRequest = mode; m->showView (sequencer); } }
int takeSequencerRequest()
{
    auto* m = MainComponent::instance;
    if (m == nullptr) return -1;
    const int r = m->sequencerRequest;
    m->sequencerRequest = -1;
    return r;
}
void showAutomation (bool songLevel, int bank, const juce::String& param)
{
    if (auto* m = MainComponent::instance)
    {
        m->automationRequest = true;
        m->automationSong = songLevel;
        m->automationBank = bank;
        m->automationParam = param;
        m->sequencerRequest = songLevel ? song : pattern;
        m->showView (sequencer);
    }
}
bool takeAutomationRequest (bool& songLevel, int& bank, juce::String& param)
{
    auto* m = MainComponent::instance;
    if (m == nullptr || ! m->automationRequest) return false;
    m->automationRequest = false;
    songLevel = m->automationSong;
    bank = m->automationBank;
    param = m->automationParam;
    return true;
}
void setStatus (const juce::String& t) { if (auto* m = MainComponent::instance) m->setStatus (t); }
void togglePanel (int p) { if (auto* m = MainComponent::instance) m->togglePanel (p); }
bool isPanelOpen (int p) { return MainComponent::instance && MainComponent::instance->isPanelOpen (p); }
void showOverlay (std::unique_ptr<juce::Component> c) { if (auto* m = MainComponent::instance) m->showOverlay (std::move (c)); }
void closeOverlay() { if (auto* m = MainComponent::instance) m->closeOverlay(); }
void openSession() { if (auto* m = MainComponent::instance) m->openSession(); }
void saveSession (bool saveAs) { if (auto* m = MainComponent::instance) m->saveSession (saveAs); }
}

// ============================================================================
// Track helper: shows what the selected track is playing + a song overview
// ============================================================================
class TrackHelper : public juce::Component
{
public:
    void paint (juce::Graphics& g) override
    {
        auto& e = Engine::get();
        const int b = e.selectedBank;
        const auto& bank = e.session().banks[(size_t) b];
        auto r = getLocalBounds().toFloat();
        g.setColour (isMouseOver() ? Pal::surface2 : Pal::surface);
        g.fillRoundedRectangle (r, 5.0f);

        auto inner = r.reduced (6.0f, 5.0f);
        drawBankBadge (g, inner.removeFromLeft (inner.getHeight() * 0.62f).withHeight (inner.getHeight() * 0.62f), b, true);
        inner.removeFromLeft (8.0f);
        auto top = inner.removeFromTop (14.0f);
        juce::String state;
        if (bank.queued.pending) state = "QUEUED";
        else if (bank.source == cr::TrackSource::song) state = "PLAYING SONG";
        else if (bank.source == cr::TrackSource::pattern && juce::isPositiveAndBelow (bank.playingPattern, (int) bank.patterns.size()))
            state = juce::String (bank.playingPattern + 1) + "  " + bank.patterns[(size_t) bank.playingPattern].name;
        else state = "STOPPED";
        drawCaps (g, state, top.toNearestInt(), juce::Justification::centredLeft, bank.source == cr::TrackSource::pattern ? bank.colour : Pal::textDim, 10.0f);

        // overview of the song
        auto ov = inner.reduced (0.0f, 2.0f);
        g.setColour (Pal::bg);
        g.fillRoundedRectangle (ov, 3.0f);
        const int len = juce::jmax (cr::barTicks() * 8, e.getSongLengthTicks());
        std::vector<int> rows;
        for (auto& c : e.session().song) if (std::find (rows.begin(), rows.end(), c.bank) == rows.end()) rows.push_back (c.bank);
        std::sort (rows.begin(), rows.end());
        const float rowH = rows.empty() ? 0.0f : juce::jmin (4.0f, ov.getHeight() / (float) rows.size());
        for (auto& c : e.session().song)
        {
            const int row = (int) (std::find (rows.begin(), rows.end(), c.bank) - rows.begin());
            const float x0 = ov.getX() + ov.getWidth() * (float) c.start / (float) len;
            const float w = ov.getWidth() * (float) c.length / (float) len;
            g.setColour (e.session().banks[(size_t) c.bank].colour);
            g.fillRect (x0, ov.getY() + 1.0f + row * rowH, juce::jmax (1.0f, w - 1.0f), juce::jmax (1.0f, rowH - 1.0f));
        }
        const float px = ov.getX() + ov.getWidth() * (float) juce::jlimit (0.0, 1.0, e.getPositionTicks() / len);
        g.setColour (Pal::text);
        g.fillRect (px, ov.getY(), 1.0f, ov.getHeight());
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

    void mouseDown (const juce::MouseEvent&) override
    {
        auto& e = Engine::get();
        const int b = e.selectedBank;
        auto& bank = e.session().banks[(size_t) b];
        juce::PopupMenu m;
        m.addSectionHeader (bank.name);
        m.addItem (1, "Play song on this track", true, bank.source == cr::TrackSource::song);
        m.addItem (2, "Stop this track");
        m.addItem (3, "Play song on all tracks");
        m.addSeparator();
        juce::PopupMenu pats;
        for (int i = 0; i < (int) bank.patterns.size(); ++i)
            pats.addItem (100 + i, juce::String (i + 1) + "  " + bank.patterns[(size_t) i].name, true, bank.source == cr::TrackSource::pattern && bank.playingPattern == i);
        m.addSubMenu ("Play pattern", pats);
        m.addItem (4, "New pattern");
        m.addItem (5, "Duplicate working pattern");
        m.addItem (6, "Edit working pattern");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [b] (int r)
        {
            auto& en = Engine::get();
            if (r == 1) en.playSong (b);
            else if (r == 2) en.stopTrack (b);
            else if (r == 3) en.playSong (-1);
            else if (r == 4) { en.launchPattern (b, en.addPattern (b)); Nav::showSequencer (Nav::pattern); }
            else if (r == 5) { const int p = en.duplicatePattern (b, en.session().banks[(size_t) b].workingPattern); if (p >= 0) en.launchPattern (b, p); }
            else if (r == 6) Nav::showSequencer (Nav::pattern);
            else if (r >= 100) en.launchPattern (b, r - 100);
            ViewBase::broadcast();
        });
    }
};

// ============================================================================
// Position / tempo / metronome / quantize / automation display
// ============================================================================
class TransportDisplay : public juce::Component
{
public:
    int format = 0;   // 0 bars, 1 time

    void paint (juce::Graphics& g) override
    {
        auto& e = Engine::get();
        layout();
        auto bgr = getLocalBounds().toFloat();
        g.setColour (Pal::bg);
        g.fillRoundedRectangle (bgr, 5.0f);

        // position
        const double t = e.getPositionTicks();
        juce::String pos;
        if (format == 0)
        {
            const int bt = cr::barTicks();
            pos = juce::String ((int) (t / bt) + 1).paddedLeft ('0', 3) + " : " + juce::String ((int) (std::fmod (t, (double) bt) / cr::ppq) + 1);
        }
        else
        {
            const double secs = e.ticksToSeconds (t);
            pos = juce::String ((int) secs / 60).paddedLeft ('0', 2) + ":" + juce::String (std::fmod (secs, 60.0), 1).paddedLeft ('0', 4);
        }
        g.setColour (e.isInPreRoll() ? Pal::rec : Pal::text);
        g.setFont (Fonts::digits (24.0f));
        g.drawText (pos, posArea, juce::Justification::centred);

        divider (g, posArea.getRight());
        // tempo
        g.setColour (Pal::text);
        g.setFont (Fonts::digits (17.0f));
        g.drawText (juce::String (e.getBpm(), 2), tempoArea.withTrimmedBottom (14), juce::Justification::centredBottom);
        drawCaps (g, juce::String (e.session().beatsPerBar) + "/4", tempoArea.withTrimmedTop (tempoArea.getHeight() - 16), juce::Justification::centredTop, Pal::textDim, 10.0f);

        divider (g, tempoArea.getRight());
        // metronome
        const bool metro = e.session().metronome;
        drawIcon (g, Icon::metronome, metroArea.withTrimmedBottom (16).toFloat().reduced (4.0f), metro ? Pal::accent : Pal::textDim);
        drawCaps (g, metro ? "ON" : "OFF", metroArea.withTrimmedTop (metroArea.getHeight() - 16), juce::Justification::centredTop, metro ? Pal::accent : Pal::textFaint, 9.5f);
        // quantize
        const int q = e.session().quantize;
        g.setColour (q > 0 ? Pal::accent : Pal::textDim);
        g.setFont (Fonts::heavy (16.0f));
        g.drawText ("Q", qArea.withTrimmedBottom (16), juce::Justification::centredBottom);
        drawCaps (g, q > 0 ? cr::quantizeNames()[q] : juce::String ("OFF"), qArea.withTrimmedTop (qArea.getHeight() - 16), juce::Justification::centredTop, q > 0 ? Pal::accent : Pal::textFaint, 9.5f);
        // automation record
        const bool a = e.automationRecord.load();
        g.setColour (a ? Pal::rec : Pal::textDim);
        g.setFont (Fonts::heavy (16.0f));
        g.drawText ("A", aArea.withTrimmedBottom (16), juce::Justification::centredBottom);
        drawCaps (g, a ? "ON" : "OFF", aArea.withTrimmedTop (aArea.getHeight() - 16), juce::Justification::centredTop, a ? Pal::rec : Pal::textFaint, 9.5f);
    }

    void mouseDown (const juce::MouseEvent& ev) override
    {
        auto& e = Engine::get();
        layout();
        const auto p = ev.getPosition();
        dragStartBpm = e.getBpm();
        draggingTempo = false;
        if (posArea.contains (p)) { format = 1 - format; repaint(); return; }
        if (tempoArea.contains (p))
        {
            if (ev.mods.isPopupMenu()) { tempoMenu(); return; }
            draggingTempo = true;
            return;
        }
        if (metroArea.contains (p))
        {
            if (ev.mods.isPopupMenu()) { metroMenu(); return; }
            e.session().metronome = ! e.session().metronome;
        }
        else if (qArea.contains (p)) showQuantizeMenu (this);
        else if (aArea.contains (p)) e.automationRecord = ! e.automationRecord.load();
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& ev) override
    {
        if (! draggingTempo) return;
        const double fine = ev.mods.isShiftDown() ? 0.01 : 0.2;
        Engine::get().setBpm (std::round ((dragStartBpm - ev.getDistanceFromDragStartY() * fine) * 100.0) / 100.0);
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& ev) override
    {
        layout();
        if (tempoArea.contains (ev.getPosition()))
            askForNumber ("Tempo (BPM)", Engine::get().getBpm(), [this] (double v) { Engine::get().setBpm (v); repaint(); });
    }

    void tempoMenu()
    {
        juce::PopupMenu m;
        m.addItem (1, "Tap tempo");
        m.addItem (2, "Set tempo...");
        juce::PopupMenu sig;
        for (int n = 1; n <= 12; ++n) sig.addItem (100 + n, juce::String (n) + "/4", true, Engine::get().session().beatsPerBar == n);
        m.addSubMenu ("Time signature", sig);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int r)
        {
            auto& e = Engine::get();
            if (r == 1) e.tapTempo();
            else if (r == 2) askForNumber ("Tempo (BPM)", e.getBpm(), [] (double v) { Engine::get().setBpm (v); });
            else if (r > 100) e.setBeatsPerBar (r - 100);
            repaint();
            ViewBase::broadcast();
        });
    }

    void metroMenu()
    {
        auto& s = Engine::get().session();
        juce::PopupMenu m;
        m.addItem (1, "Metronome on", true, s.metronome);
        m.addItem (2, "Only when recording", true, s.metronomeOnlyWhenRecording);
        m.addItem (3, "Click during pre-roll", true, s.metronomeDuringPreRoll);
        juce::PopupMenu vol;
        for (int v : { 25, 50, 75, 100 }) vol.addItem (100 + v, juce::String (v) + "%", true, std::abs (s.metronomeVolume * 100.0f - (float) v) < 1.0f);
        m.addSubMenu ("Volume", vol);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int r)
        {
            auto& ss = Engine::get().session();
            if (r == 1) ss.metronome = ! ss.metronome;
            else if (r == 2) ss.metronomeOnlyWhenRecording = ! ss.metronomeOnlyWhenRecording;
            else if (r == 3) ss.metronomeDuringPreRoll = ! ss.metronomeDuringPreRoll;
            else if (r > 100) ss.metronomeVolume = (float) (r - 100) / 100.0f;
            repaint();
        });
    }

private:
    void layout()
    {
        auto r = getLocalBounds().reduced (4, 2);
        posArea = r.removeFromLeft (112);
        tempoArea = r.removeFromLeft (78);
        metroArea = r.removeFromLeft (46);
        qArea = r.removeFromLeft (46);
        aArea = r.removeFromLeft (46);
    }
    void divider (juce::Graphics& g, int x)
    {
        g.setColour (Pal::outline);
        g.fillRect (x, 8, 1, getHeight() - 16);
    }
    juce::Rectangle<int> posArea, tempoArea, metroArea, qArea, aArea;
    double dragStartBpm = 120.0;
    bool draggingTempo = false;
};

// ============================================================================
MainComponent::MainComponent()
{
    instance = this;
    auto& e = Engine::get();
    loadAppSettings();
    loadStarterKit();

    trackHelper = std::make_unique<TrackHelper>();
    display = std::make_unique<TransportDisplay>();
    addAndMakeVisible (menuBtn);
    addAndMakeVisible (*trackHelper);
    addAndMakeVisible (*display);
    for (auto* b : { &stopBtn, &playBtn, &recBtn, &rewBtn, &ffBtn, &loopBtn }) addAndMakeVisible (b);
    playBtn.setOnColour (Pal::play);
    recBtn.setOnColour (Pal::rec);
    loopBtn.setOnColour (Pal::accent);
    playBtn.setClickingTogglesState (false);
    recBtn.setClickingTogglesState (false);
    loopBtn.setClickingTogglesState (false);
    menuBtn.onClick = [this] { showFileMenu(); };
    stopBtn.onClick = [] { auto& en = Engine::get(); if (en.isPlaying()) en.stop(); else en.setPosition (en.session().loopOn ? en.session().loopStart : 0); };
    playBtn.onClick = [] { Engine::get().togglePlay(); };
    recBtn.onClick = [] { auto& en = Engine::get(); en.setRecording (! en.isRecording()); };
    rewBtn.onClick = [] { Engine::get().rewind(); };
    ffBtn.onClick = [] { Engine::get().fastForward(); };
    loopBtn.onClick = [] { auto& s = Engine::get().session(); s.loopOn = ! s.loopOn; ViewBase::broadcast(); };
    undoBtn.onClick = [] { if (Engine::get().undo()) ViewBase::broadcast(); };
    redoBtn.onClick = [] { if (Engine::get().redo()) ViewBase::broadcast(); };
    undoBtn.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    redoBtn.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (undoBtn);
    addAndMakeVisible (redoBtn);
    cpuLabel.setFont (Fonts::caps (9.5f));
    cpuLabel.setColour (juce::Label::textColourId, Pal::textFaint);
    cpuLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (cpuLabel);
    statusLabel.setFont (Fonts::ui (12.0f));
    statusLabel.setColour (juce::Label::textColourId, Pal::accent);
    statusLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (statusLabel);

    const std::pair<Icon, const char*> nav[] = { { Icon::pads, "Performance (F1)" }, { Icon::editor, "Editor (F2)" },
                                                  { Icon::sequencer, "Sequencer (F3)" }, { Icon::mixer, "Mixer (F4)" } };
    for (int i = 0; i < 4; ++i)
    {
        auto* b = navButtons.add (new IconButton (nav[i].first, nav[i].second));
        b->onClick = [this, i] { showView (i); };
        addAndMakeVisible (b);
    }
    const std::pair<Icon, const char*> pan[] = { { Icon::browser, "Browser (B)" }, { Icon::fx, "Effects panel (X)" },
                                                  { Icon::macro, "Macros (M)" }, { Icon::keyboard, "Keyboard (K)" } };
    for (int i = 0; i < 4; ++i)
    {
        auto* b = panelButtons.add (new IconButton (pan[i].first, pan[i].second));
        b->onClick = [this, i] { togglePanel (i); };
        addAndMakeVisible (b);
    }
    settingsBtn.onClick = [] { showSettingsDialog (0); };
    addAndMakeVisible (settingsBtn);

    views[0] = createPerformanceView();
    views[1] = createEditorView();
    views[2] = createSequencerView();
    views[3] = createMixerView();
    for (auto& v : views) addChildComponent (*v);
    panels[0] = createBrowserPanel();
    panels[1] = createFxPanel();
    panels[2] = createMacrosPanel();
    panels[3] = createKeyboardPanel();
    for (auto& p : panels) addChildComponent (*p);
    showView (0);

    e.addChangeListener (this);
    setWantsKeyboardFocus (true);
    setSize (1440, 900);
    startTimerHz (30);
    lastAutosave = juce::Time::getMillisecondCounter();

   #if ! JUCE_LINUX
    if (juce::SystemStats::getEnvironmentVariable ("CRATE_NO_GL", {}).isEmpty())
        gl.attachTo (*this);
   #endif
}

MainComponent::~MainComponent()
{
    gl.detach();
    Engine::get().removeChangeListener (this);
    instance = nullptr;
}

void MainComponent::prepareToQuit()
{
    saveAppSettings();
    autosave();
}

// ============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (Pal::bg);
    g.setColour (Pal::rail);
    g.fillRect (transportArea);
    g.fillRect (railArea);
    g.setColour (Pal::outline);
    g.fillRect (transportArea.getX(), transportArea.getBottom() - 1, transportArea.getWidth(), 1);
    g.fillRect (railArea.getRight() - 1, railArea.getY(), 1, railArea.getHeight());

    // selected nav item marker
    if (auto* b = navButtons[currentView])
    {
        g.setColour (Pal::accent);
        g.fillRoundedRectangle ((float) railArea.getX() + 1.0f, (float) b->getY() + 8.0f, 3.0f, (float) b->getHeight() - 16.0f, 1.5f);
    }
    // current bank / pad under the views
    auto& e = Engine::get();
    auto info = juce::Rectangle<int> (railArea.getX(), navButtons[3]->getBottom() + 14, railArea.getWidth(), 40);
    drawBankBadge (g, info.removeFromTop (22).withSizeKeepingCentre (22, 22).toFloat(), e.selectedBank, true);
    drawCaps (g, juce::String (e.selectedPad.load() + 1), info.removeFromTop (16), juce::Justification::centred, Pal::textDim, 10.0f);
}

void MainComponent::paintOverChildren (juce::Graphics& g)
{
    // panel separators
    g.setColour (Pal::outline);
    for (size_t i = 0; i < panels.size(); ++i)
        if (panelOpen[i] && panels[i]->isVisible())
        {
            auto r = panels[i]->getBounds();
            if (i == 0) g.fillRect (r.getRight(), r.getY(), 1, r.getHeight());
            else if (i == 3) g.fillRect (r.getX(), r.getY() - 1, r.getWidth(), 1);
            else g.fillRect (r.getX() - 1, r.getY(), 1, r.getHeight());
        }
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    transportArea = r.removeFromTop (58);
    railArea = r.removeFromLeft (58);

    auto t = transportArea.reduced (8, 7);
    menuBtn.setBounds (t.removeFromLeft (40).reduced (2));
    t.removeFromLeft (8);
    trackHelper->setBounds (t.removeFromLeft (juce::jmin (260, getWidth() / 6)));
    t.removeFromLeft (10);
    display->setBounds (t.removeFromLeft (336));
    t.removeFromLeft (12);
    auto right = t.removeFromRight (juce::jmin (300, t.getWidth() / 3));
    redoBtn.setBounds (right.removeFromRight (56).reduced (0, 6));
    undoBtn.setBounds (right.removeFromRight (56).reduced (0, 6));
    right.removeFromRight (8);
    cpuLabel.setBounds (right.removeFromBottom (14));
    statusLabel.setBounds (right);
    for (auto* b : { &stopBtn, &playBtn, &recBtn, &rewBtn, &ffBtn })
    {
        b->setBounds (t.removeFromLeft (44));
        t.removeFromLeft (2);
    }
    t.removeFromLeft (10);
    loopBtn.setBounds (t.removeFromLeft (44));

    auto rail = railArea.reduced (7, 10);
    for (auto* b : navButtons) { b->setBounds (rail.removeFromTop (44)); rail.removeFromTop (6); }
    settingsBtn.setBounds (rail.removeFromBottom (44));
    rail.removeFromBottom (14);
    for (int i = 3; i >= 0; --i) { panelButtons[i]->setBounds (rail.removeFromBottom (44)); rail.removeFromBottom (6); }

    if (panelOpen[3]) panels[3]->setBounds (r.removeFromBottom (120));
    if (panelOpen[0]) panels[0]->setBounds (r.removeFromLeft (320));
    if (panelOpen[1]) panels[1]->setBounds (r.removeFromRight (360));
    else if (panelOpen[2]) panels[2]->setBounds (r.removeFromRight (360));
    for (auto& v : views) v->setBounds (r);
    if (overlay) overlay->setBounds (getLocalBounds().withTrimmedTop (transportArea.getHeight()).withTrimmedLeft (railArea.getWidth()));
}

void MainComponent::showView (int index)
{
    currentView = juce::jlimit (0, 3, index);
    for (int i = 0; i < 4; ++i)
    {
        views[(size_t) i]->setVisible (i == currentView);
        navButtons[i]->setToggleState (i == currentView, juce::dontSendNotification);
    }
    views[(size_t) currentView]->refresh();
    repaint();
}

void MainComponent::togglePanel (int which)
{
    panelOpen[(size_t) which] = ! panelOpen[(size_t) which];
    if (which == 1 && panelOpen[1]) panelOpen[2] = false;   // FX and Macros share the right dock
    if (which == 2 && panelOpen[2]) panelOpen[1] = false;
    for (int i = 0; i < 4; ++i)
    {
        panels[(size_t) i]->setVisible (panelOpen[(size_t) i]);
        panelButtons[i]->setToggleState (panelOpen[(size_t) i], juce::dontSendNotification);
        if (panelOpen[(size_t) i]) panels[(size_t) i]->refresh();
    }
    resized();
    repaint();
}

void MainComponent::showOverlay (std::unique_ptr<juce::Component> c)
{
    overlay = std::move (c);
    addAndMakeVisible (*overlay);
    resized();
}

void MainComponent::closeOverlay()
{
    if (overlay) juce::MessageManager::callAsync ([this] { overlay.reset(); ViewBase::broadcast(); });
}

void MainComponent::setStatus (const juce::String& s)
{
    statusLabel.setText (s, juce::dontSendNotification);
    statusTicks = 120;
}

// ============================================================================
void MainComponent::updateTransport()
{
    auto& e = Engine::get();
    playBtn.setToggleState (e.isPlaying(), juce::dontSendNotification);
    playBtn.setIcon (e.isPlaying() ? Icon::stop : Icon::play);
    playBtn.setIcon (Icon::play);
    recBtn.setToggleState (e.isRecording(), juce::dontSendNotification);
    loopBtn.setToggleState (e.session().loopOn, juce::dontSendNotification);
    undoBtn.setEnabled (e.canUndo());
    redoBtn.setEnabled (e.canRedo());
}

void MainComponent::timerCallback()
{
    auto& e = Engine::get();
    ++tick;
    updateTransport();
    display->repaint();
    if (tick % 3 == 0)
    {
        trackHelper->repaint();
        Knob::refreshAll();
    }
    if (tick % 15 == 0)
    {
        cpuLabel.setText ("CPU " + juce::String (juce::roundToInt (e.getCpuLoad() * 100.0f)) + "%   VOICES " + juce::String (e.getActiveVoiceCount()),
                          juce::dontSendNotification);
        repaint (railArea);
    }
    if (statusTicks > 0 && --statusTicks == 0) statusLabel.setText ({}, juce::dontSendNotification);
    if (juce::Time::getMillisecondCounter() - lastAutosave > 120000) autosave();
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    ViewBase::broadcast();
    updateTransport();
    repaint (railArea);
}

void MainComponent::autosave()
{
    lastAutosave = juce::Time::getMillisecondCounter();
    juce::String err;
    Engine::get().saveSession (Engine::getSessionsFolder().getChildFile ("(Auto-saved).crsession"), err);
}

// ============================================================================
// Keyboard
// ============================================================================
bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    auto& e = Engine::get();
    const auto mods = key.getModifiers();
    const int code = key.getKeyCode();
    if (mods.isCommandDown())
    {
        if (code == 'Z') { if (mods.isShiftDown() ? e.redo() : e.undo()) ViewBase::broadcast(); return true; }
        if (code == 'Y') { if (e.redo()) ViewBase::broadcast(); return true; }
        if (code == 'S') { saveSession (mods.isShiftDown()); return true; }
        if (code == 'O') { openSession(); return true; }
        if (code == 'N') { newSession(); return true; }
        if (code == 'E') { showExportDialog(); return true; }
        if (code == ',') { showSettingsDialog (0); return true; }
        return false;
    }
    if (code == juce::KeyPress::spaceKey) { e.togglePlay(); return true; }
    if (handlePadKeyDown (key)) return true;
    if (code == juce::KeyPress::F1Key) { showView (0); return true; }
    if (code == juce::KeyPress::F2Key) { showView (1); return true; }
    if (code == juce::KeyPress::F3Key) { showView (2); return true; }
    if (code == juce::KeyPress::F4Key) { showView (3); return true; }
    if (code == juce::KeyPress::tabKey) { showView ((currentView + (mods.isShiftDown() ? 3 : 1)) % 4); return true; }
    if (code == juce::KeyPress::escapeKey && overlay) { closeOverlay(); return true; }
    const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
    if (c == 'r') { e.setRecording (! e.isRecording()); return true; }
    if (c == 'l') { e.session().loopOn = ! e.session().loopOn; ViewBase::broadcast(); return true; }
    if (c == 'b') { togglePanel (0); return true; }
    if (c == 'x') { togglePanel (1); return true; }
    if (c == 'm') { togglePanel (2); return true; }
    if (c == 'k') { togglePanel (3); return true; }
    if (c == 'c') { e.session().metronome = ! e.session().metronome; return true; }
    if (c == 't') { e.tapTempo(); return true; }
    return false;
}

// ============================================================================
// Files
// ============================================================================
void MainComponent::filesDropped (const juce::StringArray& files, int, int)
{
    auto& e = Engine::get();
    juce::String err;
    juce::StringArray audio;
    for (auto& path : files)
    {
        juce::File f (path);
        if (f.hasFileExtension ("crsession")) { openSessionFile (f); return; }
        if (f.hasFileExtension ("crbank")) { e.loadBankPreset (e.selectedBank, f, err); }
        else if (f.hasFileExtension ("mid;midi")) { e.importMidi (f, e.selectedBank, err); }
        else if (f.hasFileExtension ("crpack")) { if (e.installPack (f, err)) setStatus ("Pack installed"); }
        else if (isAudioFile (f)) audio.add (path);
    }
    if (! audio.isEmpty())
    {
        if (currentView == Nav::sequencer) importStems (audio, audio.size() > 1 ? 0 : (int) e.getPositionTicks());
        else
        {
            int pad = e.selectedPad;
            for (auto& a : audio) if (pad < cr::numPads && e.loadSampleToPad (juce::File (a), e.selectedBank, pad)) ++pad;
        }
    }
    if (err.isNotEmpty()) showError ("Import", err);
    ViewBase::broadcast();
}

bool MainComponent::openSessionFile (const juce::File& f)
{
    juce::String err;
    const bool ok = Engine::get().loadSession (f, err);
    if (! ok) showError ("Open session", err);
    else if (err.isNotEmpty()) showError ("Some items could not be loaded", err);
    if (ok) setStatus ("Opened " + f.getFileNameWithoutExtension());
    ViewBase::broadcast();
    return ok;
}

void MainComponent::openSession()
{
    chooser = std::make_unique<juce::FileChooser> ("Open session", Engine::getSessionsFolder(), "*.crsession");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
    {
        if (fc.getResult().existsAsFile()) openSessionFile (fc.getResult());
    });
}

void MainComponent::saveSession (bool saveAs)
{
    auto& e = Engine::get();
    auto doSave = [this] (const juce::File& f, bool copy)
    {
        juce::String err;
        if (Engine::get().saveSession (f, err, copy)) setStatus ("Saved " + f.getFileNameWithoutExtension());
        else showError ("Save session", err);
    };
    if (! saveAs && e.session().file.existsAsFile()) { doSave (e.session().file, false); return; }
    chooser = std::make_unique<juce::FileChooser> ("Save session", Engine::getSessionsFolder().getChildFile ("Untitled.crsession"), "*.crsession");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [doSave] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f == juce::File()) return;
        doSave (f.withFileExtension ("crsession"), false);
    });
}

void MainComponent::newSession()
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon)
                                      .withTitle ("New session").withMessage ("Start a new empty session? Unsaved changes are kept in the auto-save.")
                                      .withButton ("New session").withButton ("Cancel"),
                                  [this] (int r)
                                  {
                                      if (r != 1) return;
                                      autosave();
                                      Engine::get().newSession();
                                      ViewBase::broadcast();
                                  });
}

void MainComponent::loadBankPresetDialog (bool save)
{
    auto& e = Engine::get();
    const int b = e.selectedBank;
    if (save)
    {
        chooser = std::make_unique<juce::FileChooser> ("Save bank preset", Engine::getBanksFolder().getChildFile (juce::File::createLegalFileName (e.session().banks[(size_t) b].name) + ".crbank"), "*.crbank");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this, b] (const juce::FileChooser& fc)
        {
            if (fc.getResult() == juce::File()) return;
            juce::String err;
            if (Engine::get().saveBankPreset (b, fc.getResult().withFileExtension ("crbank"), err)) setStatus ("Bank saved");
            else showError ("Save bank", err);
        });
    }
    else
    {
        chooser = std::make_unique<juce::FileChooser> ("Load bank preset into " + e.session().banks[(size_t) b].name, Engine::getBanksFolder(), "*.crbank");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [b] (const juce::FileChooser& fc)
        {
            if (! fc.getResult().existsAsFile()) return;
            juce::String err;
            Engine::get().loadBankPreset (b, fc.getResult(), err);
            if (err.isNotEmpty()) showError ("Load bank", err);
            ViewBase::broadcast();
        });
    }
}

void MainComponent::showFileMenu()
{
    auto& e = Engine::get();
    juce::PopupMenu m;
    m.addSectionHeader ("Session");
    m.addItem (1, "New session");
    m.addItem (2, "Open session...");
    m.addItem (3, "Save session");
    m.addItem (4, "Save session as...");
    m.addItem (5, "Save session with all samples copied...");
    m.addItem (6, "Restore auto-saved session");
    m.addSectionHeader ("Banks");
    m.addItem (10, "Load bank preset into " + e.session().banks[(size_t) e.selectedBank.load()].name + "...");
    m.addItem (11, "Save " + e.session().banks[(size_t) e.selectedBank.load()].name + " as bank preset...");
    m.addSectionHeader ("Import / export");
    m.addItem (20, "Import audio files to audio tracks...");
    m.addItem (21, "Import MIDI file into the current bank...");
    m.addItem (22, "Export working pattern as MIDI...");
    m.addItem (23, "Export audio / stems...");
    m.addItem (24, "Export pack...");
    m.addItem (25, "Install pack...");
    m.addSeparator();
    m.addItem (30, "Settings...");
    m.addItem (31, "MIDI panic (all notes off)");
    m.addItem (32, "Show library folder");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menuBtn), [this] (int r)
    {
        auto& en = Engine::get();
        const int b = en.selectedBank;
        switch (r)
        {
            case 1: newSession(); break;
            case 2: openSession(); break;
            case 3: saveSession (false); break;
            case 4: saveSession (true); break;
            case 5:
                chooser = std::make_unique<juce::FileChooser> ("Save session with samples", Engine::getSessionsFolder().getChildFile ("Untitled.crsession"), "*.crsession");
                chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc)
                {
                    if (fc.getResult() == juce::File()) return;
                    juce::String err;
                    if (Engine::get().saveSession (fc.getResult().withFileExtension ("crsession"), err, true)) setStatus ("Saved with samples");
                    else showError ("Save session", err);
                });
                break;
            case 6:
            {
                auto f = Engine::getSessionsFolder().getChildFile ("(Auto-saved).crsession");
                if (f.existsAsFile()) openSessionFile (f); else showError ("Restore", "There is no auto-saved session yet.");
                break;
            }
            case 10: loadBankPresetDialog (false); break;
            case 11: loadBankPresetDialog (true); break;
            case 20:
                chooser = std::make_unique<juce::FileChooser> ("Import audio", Engine::getSamplesFolder(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                                      [] (const juce::FileChooser& fc)
                                      {
                                          juce::StringArray paths;
                                          for (auto& f : fc.getResults()) paths.add (f.getFullPathName());
                                          if (paths.isEmpty()) return;
                                          juce::String err;
                                          Engine::get().importAudioToTracks (paths, err);
                                          if (err.isNotEmpty()) showError ("Import audio", err);
                                          Nav::showSequencer (Nav::song);
                                      });
                break;
            case 21:
                chooser = std::make_unique<juce::FileChooser> ("Import MIDI", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.mid;*.midi");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [b] (const juce::FileChooser& fc)
                {
                    if (! fc.getResult().existsAsFile()) return;
                    juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("Import MIDI").withMessage ("How should notes map to pads?")
                                                      .withButton ("Note number = pad").withButton ("General MIDI drums").withButton ("Cancel"),
                                                  [b, f = fc.getResult()] (int r)
                                                  {
                                                      if (r == 0) return;
                                                      juce::String err;
                                                      if (! Engine::get().importMidi (f, b, err, r == 2)) showError ("Import MIDI", err);
                                                      ViewBase::broadcast();
                                                  });
                });
                break;
            case 22:
                chooser = std::make_unique<juce::FileChooser> ("Export pattern as MIDI", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Pattern.mid"), "*.mid");
                chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this, b] (const juce::FileChooser& fc)
                {
                    if (fc.getResult() == juce::File()) return;
                    juce::String err;
                    auto& en2 = Engine::get();
                    if (en2.exportPatternMidi (b, en2.session().banks[(size_t) b].workingPattern, fc.getResult().withFileExtension ("mid"), err)) setStatus ("MIDI exported");
                    else showError ("Export MIDI", err);
                });
                break;
            case 23: showExportDialog(); break;
            case 24:
                askForText ("Pack name", "My Pack", [this] (const juce::String& name)
                {
                    chooser = std::make_unique<juce::FileChooser> ("Export pack", Engine::getPacksFolder().getChildFile (juce::File::createLegalFileName (name) + ".crpack"), "*.crpack");
                    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this, name] (const juce::FileChooser& fc)
                    {
                        if (fc.getResult() == juce::File()) return;
                        juce::String err;
                        if (Engine::get().exportPack (fc.getResult().withFileExtension ("crpack"), name, juce::SystemStats::getFullUserName(), err)) setStatus ("Pack exported");
                        else showError ("Export pack", err);
                    });
                });
                break;
            case 25:
                chooser = std::make_unique<juce::FileChooser> ("Install pack", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.crpack;*.zip");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
                {
                    if (! fc.getResult().existsAsFile()) return;
                    juce::String err;
                    if (Engine::get().installPack (fc.getResult(), err)) { setStatus ("Pack installed: open the Browser > Packs"); ViewBase::broadcast(); }
                    else showError ("Install pack", err);
                });
                break;
            case 30: showSettingsDialog (0); break;
            case 31: en.midiPanic(); break;
            case 32: Engine::getLibraryFolder().revealToUser(); break;
            default: break;
        }
        ViewBase::broadcast();
    });
}
