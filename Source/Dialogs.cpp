// Recorder (sampling onto pads), Export, Chords & Scales, the quantize / pre-roll menu.
#include "Views.h"

namespace
{
// ============================================================================
// Recorder
// ============================================================================
class RecorderComponent : public juce::Component, private juce::Timer
{
public:
    RecorderComponent (int bank, int pad)
    {
        auto& e = Engine::get();
        e.recorder.destBank = bank;
        e.recorder.destPad = pad;
        source.getText = [] { return sourceName(); };
        source.onClick = [this] { sourceMenu(); };
        addAndMakeVisible (source);
        bankStep.getText = [] { return cr::bankName (Engine::get().recorder.destBank); };
        bankStep.onStep = [] (int d) { auto& r = Engine::get().recorder; r.destBank = juce::jlimit (0, cr::numBanks - 1, r.destBank + d); };
        padStep.getText = [] { return "Pad " + juce::String (Engine::get().recorder.destPad + 1); };
        padStep.onStep = [] (int d) { auto& r = Engine::get().recorder; r.destPad = juce::jlimit (0, cr::numPads - 1, r.destPad + d); };
        addAndMakeVisible (bankStep);
        addAndMakeVisible (padStep);
        nextPad.setButtonText ("Move to the next pad after each take");
        nextPad.setToggleState (e.recorder.moveToNextPad, juce::dontSendNotification);
        nextPad.onClick = [this] { Engine::get().recorder.moveToNextPad = nextPad.getToggleState(); };
        threshold.setButtonText ("Start when the input passes the threshold");
        threshold.setToggleState (e.recorder.useThreshold, juce::dontSendNotification);
        threshold.onClick = [this] { Engine::get().recorder.useThreshold = threshold.getToggleState(); };
        addAndMakeVisible (nextPad);
        addAndMakeVisible (threshold);
        thrKnob.setCustom (-60.0f, 0.0f, -30.0f, [] { return Engine::get().recorder.thresholdDb; }, [] (float v) { Engine::get().recorder.thresholdDb = v; },
                           [] (float v) { return juce::String ((int) v) + " dB"; });
        thrKnob.setAccent (Pal::rec);
        addAndMakeVisible (thrKnob);
        recBtn.setColour (juce::TextButton::buttonColourId, Pal::rec);
        recBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        recBtn.onClick = [this]
        {
            auto& en = Engine::get();
            if (en.isSampling()) en.stopSampling();
            else en.startSampling();
            update();
        };
        addAndMakeVisible (recBtn);
        closeBtn.onClick = [this]
        {
            if (Engine::get().isSampling()) Engine::get().stopSampling();
            if (auto* w = findParentComponentOfClass<juce::DialogWindow>()) w->exitModalState (0);
        };
        addAndMakeVisible (closeBtn);
        setSize (520, 380);
        startTimerHz (30);
    }

    ~RecorderComponent() override { if (Engine::get().isSampling()) Engine::get().stopSampling(); }

    static juce::String sourceName()
    {
        auto& e = Engine::get();
        const auto& r = e.recorder;
        if (r.source == 0) return r.mono ? "Input " + juce::String (r.inputPair + 1) : e.getInputPairNames()[r.inputPair];
        if (r.source == 1) return "Main out (resample)";
        if (r.source >= 2 && r.source < 2 + cr::numBanks) return e.session().banks[(size_t) (r.source - 2)].name;
        return e.session().aux[(size_t) juce::jlimit (0, cr::numAux - 1, r.source - 200)].name;
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Pal::panel);
        auto& e = Engine::get();
        auto r = getLocalBounds().reduced (20, 16);
        g.setColour (Pal::text);
        g.setFont (Fonts::ui (16.0f, true));
        g.drawText ("Sample recorder", r.removeFromTop (26), juce::Justification::centredLeft);
        // level meter
        auto meter = meterArea.toFloat();
        g.setColour (Pal::bg);
        g.fillRoundedRectangle (meter, 4.0f);
        const float lvl = juce::jlimit (0.0f, 1.0f, (gainToDb (e.getInputLevel()) + 60.0f) / 60.0f);
        g.setColour (lvl > 0.95f ? Pal::rec : Pal::play);
        g.fillRoundedRectangle (meter.withWidth (meter.getWidth() * lvl), 4.0f);
        if (e.recorder.useThreshold)
        {
            const float tx = meter.getX() + meter.getWidth() * (e.recorder.thresholdDb + 60.0f) / 60.0f;
            g.setColour (Pal::text);
            g.fillRect (tx, meter.getY() - 3.0f, 2.0f, meter.getHeight() + 6.0f);
        }
        // status
        juce::String st;
        if (e.isSampling()) st = e.isWaitingForThreshold() ? "Waiting for the input to pass the threshold..." : "Recording  " + juce::String (e.getSamplingSeconds(), 1) + " s";
        else st = "Ready. The take goes onto " + cr::bankName (e.recorder.destBank) + " pad " + juce::String (e.recorder.destPad + 1) + ".";
        g.setColour (e.isSampling() ? Pal::rec : Pal::textDim);
        g.setFont (Fonts::ui (13.0f, true));
        g.drawText (st, statusArea, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (20, 16);
        r.removeFromTop (34);
        auto row = r.removeFromTop (48);
        source.setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (0, 2));
        bankStep.setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (4, 2));
        padStep.setBounds (row.reduced (4, 2));
        r.removeFromTop (10);
        meterArea = r.removeFromTop (18);
        r.removeFromTop (10);
        auto t = r.removeFromTop (70);
        thrKnob.setBounds (t.removeFromRight (90));
        threshold.setBounds (t.removeFromTop (30));
        nextPad.setBounds (t.removeFromTop (30));
        r.removeFromTop (8);
        statusArea = r.removeFromTop (24);
        auto b = r.removeFromBottom (44);
        closeBtn.setBounds (b.removeFromRight (100).reduced (0, 4));
        b.removeFromRight (10);
        recBtn.setBounds (b.removeFromRight (160));
    }

private:
    void sourceMenu()
    {
        auto& e = Engine::get();
        auto& rs = e.recorder;
        juce::PopupMenu m, hw, mono, internal;
        const auto pairs = e.getInputPairNames();
        for (int i = 0; i < pairs.size(); ++i) hw.addItem (1 + i, pairs[i], true, rs.source == 0 && ! rs.mono && rs.inputPair == i);
        for (int i = 0; i < pairs.size() * 2; ++i) mono.addItem (100 + i, "Input " + juce::String (i + 1), true, rs.source == 0 && rs.mono && rs.inputPair == i);
        internal.addItem (300, "Main out (resample)", true, rs.source == 1);
        for (int b = 0; b < cr::numBanks; ++b) if (e.session().banks[(size_t) b].created) internal.addItem (400 + b, e.session().banks[(size_t) b].name, true, rs.source == 2 + b);
        for (int a = 0; a < e.session().auxCount; ++a) internal.addItem (600 + a, e.session().aux[(size_t) a].name, true, rs.source == 200 + a);
        m.addSubMenu ("Stereo inputs", hw);
        m.addSubMenu ("Mono inputs", mono);
        m.addSubMenu ("Inside the app", internal);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&source), [this] (int r)
        {
            auto& rr = Engine::get().recorder;
            if (r >= 600) rr.source = 200 + r - 600;
            else if (r >= 400) rr.source = 2 + r - 400;
            else if (r == 300) rr.source = 1;
            else if (r >= 100) { rr.source = 0; rr.mono = true; rr.inputPair = r - 100; }
            else if (r >= 1) { rr.source = 0; rr.mono = false; rr.inputPair = r - 1; }
            source.repaint();
        });
    }

    void update()
    {
        const bool rec = Engine::get().isSampling();
        recBtn.setButtonText (rec ? "Stop" : "Record");
        padStep.repaint();
        bankStep.repaint();
        repaint();
    }

    void timerCallback() override
    {
        if (wasSampling && ! Engine::get().isSampling()) ViewBase::broadcast();
        wasSampling = Engine::get().isSampling();
        update();
    }

    ChoiceField source { "Source" };
    Stepper bankStep { "Bank" }, padStep { "Pad" };
    juce::ToggleButton nextPad, threshold;
    Knob thrKnob { "Threshold" };
    juce::TextButton recBtn { "Record" }, closeBtn { "Close" };
    juce::Rectangle<int> meterArea, statusArea;
    bool wasSampling = false;
};

// ============================================================================
// Export
// ============================================================================
class ExportComponent : public juce::Component
{
public:
    ExportComponent()
    {
        auto& e = Engine::get();
        what.setTabs ({ "Main mix", "Stems (one file per track)" });
        addAndMakeVisible (what);
        range.choices = { "Whole song", "Loop region", "Custom bars" };
        range.getIndex = [this] { return rangeIndex; };
        range.setIndex = [this] (int i) { rangeIndex = i; resized(); repaint(); };
        addAndMakeVisible (range);
        fromBar.getText = [this] { return "Bar " + juce::String (from + 1); };
        fromBar.onStep = [this] (int d) { from = juce::jlimit (0, 999, from + d); to = juce::jmax (to, from + 1); };
        toBar.getText = [this] { return "to bar " + juce::String (to + 1); };
        toBar.onStep = [this] (int d) { to = juce::jlimit (from + 1, 1000, to + d); };
        addAndMakeVisible (fromBar);
        addAndMakeVisible (toBar);
        format.choices = { "WAV 16-bit", "WAV 24-bit", "WAV 32-bit float", "WAV 12-bit (vintage)" };
        format.getIndex = [this] { return formatIndex; };
        format.setIndex = [this] (int i) { formatIndex = i; };
        addAndMakeVisible (format);
        rate.choices = { "Device rate", "44.1 kHz", "48 kHz", "96 kHz" };
        rate.getIndex = [this] { return rateIndex; };
        rate.setIndex = [this] (int i) { rateIndex = i; };
        addAndMakeVisible (rate);
        normalize.setButtonText ("Normalize");
        tail.setButtonText ("Render 2 s tail (reverb / delay)");
        tail.setToggleState (true, juce::dontSendNotification);
        addAndMakeVisible (normalize);
        addAndMakeVisible (tail);
        exportBtn.setColour (juce::TextButton::buttonColourId, Pal::accent);
        exportBtn.setColour (juce::TextButton::textColourOffId, Pal::bg);
        exportBtn.onClick = [this] { chooseFile(); };
        cancelBtn.onClick = [this] { close(); };
        addAndMakeVisible (exportBtn);
        addAndMakeVisible (cancelBtn);
        from = e.session().loopStart / cr::barTicks();
        to = juce::jmax (from + 1, e.session().loopEnd / cr::barTicks());
        setSize (540, 370);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Pal::panel);
        auto& e = Engine::get();
        g.setColour (Pal::text);
        g.setFont (Fonts::ui (16.0f, true));
        g.drawText ("Export audio", 20, 14, 300, 26, juce::Justification::centredLeft);
        const int len = e.getSongLengthTicks();
        drawCaps (g, "Song length: " + juce::String ((len + cr::barTicks() - 1) / cr::barTicks()) + " bars", juce::Rectangle<int> (20, 40, 400, 16), juce::Justification::centredLeft, Pal::textFaint, 9.5f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (20, 16);
        r.removeFromTop (48);
        what.setBounds (r.removeFromTop (30));
        r.removeFromTop (12);
        auto row = r.removeFromTop (48);
        range.setBounds (row.removeFromLeft (170));
        fromBar.setVisible (rangeIndex == 2);
        toBar.setVisible (rangeIndex == 2);
        fromBar.setBounds (row.removeFromLeft (150).reduced (6, 0));
        toBar.setBounds (row.removeFromLeft (150).reduced (6, 0));
        r.removeFromTop (8);
        auto row2 = r.removeFromTop (48);
        format.setBounds (row2.removeFromLeft (220));
        rate.setBounds (row2.removeFromLeft (170));
        r.removeFromTop (8);
        normalize.setBounds (r.removeFromTop (28).withWidth (300));
        tail.setBounds (r.removeFromTop (28).withWidth (300));
        auto b = r.removeFromBottom (38);
        exportBtn.setBounds (b.removeFromRight (140));
        b.removeFromRight (8);
        cancelBtn.setBounds (b.removeFromRight (100));
    }

private:
    void chooseFile()
    {
        chooser = std::make_unique<juce::FileChooser> ("Export audio", Engine::getExportsFolder().getChildFile ("Mixdown.wav"), "*.wav");
        Engine::getExportsFolder().createDirectory();
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File()) return;
            run (f.withFileExtension ("wav"));
        });
    }

    void run (const juce::File& file)
    {
        auto& e = Engine::get();
        ExportOptions o;
        o.stems = what.getSelected() == 1;
        const int len = juce::jmax (cr::barTicks(), e.getSongLengthTicks());
        if (rangeIndex == 0) { o.startTick = 0; o.endTick = (len + cr::barTicks() - 1) / cr::barTicks() * cr::barTicks(); }
        else if (rangeIndex == 1) { o.startTick = e.session().loopStart; o.endTick = e.session().loopEnd; }
        else { o.startTick = from * cr::barTicks(); o.endTick = to * cr::barTicks(); }
        const int bits[] = { 16, 24, 32, 12 };
        o.bitDepth = bits[formatIndex];
        const double rates[] = { 0.0, 44100.0, 48000.0, 96000.0 };
        o.sampleRate = rates[rateIndex];
        o.normalize = normalize.getToggleState();
        o.includeTail = tail.getToggleState();

        struct Job : juce::ThreadWithProgressWindow
        {
            Job (juce::File f, ExportOptions op) : juce::ThreadWithProgressWindow ("Exporting...", true, true), file (std::move (f)), options (op) {}
            void run() override
            {
                ok = Engine::get().exportAudio (file, options, error, [this] (float p) { setProgress (p); return ! threadShouldExit(); });
            }
            void threadComplete (bool cancelled) override
            {
                if (cancelled || ! ok) showError ("Export", error.isNotEmpty() ? error : juce::String ("Export cancelled."));
                else { Nav::setStatus ("Exported " + file.getFileName()); file.revealToUser(); }
                delete this;
            }
            juce::File file;
            ExportOptions options;
            juce::String error;
            bool ok = false;
        };
        (new Job (file, o))->launchThread();
        close();
    }

    void close() { if (auto* w = findParentComponentOfClass<juce::DialogWindow>()) w->exitModalState (0); }

    TabStrip what;
    ChoiceField range { "Range" }, format { "Format" }, rate { "Sample rate" };
    Stepper fromBar { "From" }, toBar { "To" };
    juce::ToggleButton normalize, tail;
    juce::TextButton exportBtn { "Export..." }, cancelBtn { "Cancel" };
    int rangeIndex = 0, formatIndex = 1, rateIndex = 0, from = 0, to = 4;
    std::unique_ptr<juce::FileChooser> chooser;
};

// ============================================================================
// Chords & scales
// ============================================================================
class ChordsComponent : public juce::Component
{
public:
    ChordsComponent()
    {
        auto& e = Engine::get();
        scale.choices = Engine::getScaleNames();
        scale.getIndex = [&e] { return e.scaleIndex.load(); };
        scale.setIndex = [&e] (int i) { e.scaleIndex = i; ViewBase::broadcast(); };
        root.choices = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        root.getIndex = [&e] { return e.scaleRoot.load(); };
        root.setIndex = [&e] (int i) { e.scaleRoot = i; ViewBase::broadcast(); };
        chord.choices = Engine::getChordNames();
        chord.getIndex = [&e] { return e.chordIndex.load(); };
        chord.setIndex = [&e] (int i) { e.chordIndex = i; };
        octave.getText = [&e] { return "C" + juce::String (e.keysOctave.load() - 2); };
        octave.onStep = [&e] (int d) { e.keysOctave = juce::jlimit (0, 9, e.keysOctave.load() + d); ViewBase::broadcast(); };
        for (auto* c : std::initializer_list<juce::Component*> { &scale, &root, &chord, &octave }) addAndMakeVisible (c);
        setSize (420, 170);
    }
    void paint (juce::Graphics& g) override
    {
        g.setColour (Pal::textDim);
        g.setFont (Fonts::ui (12.0f));
        g.drawFittedText ("Scales fold the keys-mode pads onto the scale; chords play several notes from one pad or key.", getLocalBounds().removeFromBottom (40), juce::Justification::centredLeft, 2);
    }
    void resized() override
    {
        auto r = getLocalBounds();
        auto row1 = r.removeFromTop (54);
        scale.setBounds (row1.removeFromLeft (row1.getWidth() / 2).reduced (4));
        root.setBounds (row1.reduced (4));
        auto row2 = r.removeFromTop (54);
        chord.setBounds (row2.removeFromLeft (row2.getWidth() / 2).reduced (4));
        octave.setBounds (row2.reduced (4));
    }
private:
    ChoiceField scale { "Scale" }, root { "Root" }, chord { "Chord" };
    Stepper octave { "Keys start at" };
};
} // namespace

void showRecorderDialog (int bank, int pad)
{
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new RecorderComponent (bank, pad));
    o.dialogTitle = "Record a sample";
    o.dialogBackgroundColour = Pal::panel;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}

void showExportDialog()
{
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new ExportComponent());
    o.dialogTitle = "Export audio";
    o.dialogBackgroundColour = Pal::panel;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}

void showChordsAndScales (juce::Component* target)
{
    juce::CallOutBox::launchAsynchronously (std::make_unique<ChordsComponent>(), target->getScreenBounds(), nullptr);
}

void showQuantizeMenu (juce::Component* target)
{
    auto& s = Engine::get().session();
    juce::PopupMenu m;
    m.addSectionHeader ("Record quantize");
    const auto names = cr::quantizeNames();
    for (int i = 0; i < names.size(); ++i) m.addItem (1 + i, names[i], true, s.quantize == i);
    juce::PopupMenu swing, pre;
    for (int sw : { 0, 8, 16, 25, 33, 50 }) swing.addItem (100 + sw, juce::String (sw) + "%", true, std::abs (s.swing * 100.0f - (float) sw) < 0.5f);
    for (int b = 0; b <= 4; ++b) pre.addItem (200 + b, b == 0 ? juce::String ("No pre-roll") : juce::String (b) + (b == 1 ? " bar" : " bars"), true, s.preRollBars == b);
    m.addSubMenu ("Swing", swing);
    m.addSubMenu ("Pre-roll (count-in)", pre);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [target] (int r)
    {
        auto& ss = Engine::get().session();
        if (r >= 200) ss.preRollBars = r - 200;
        else if (r >= 100) ss.swing = (float) (r - 100) / 100.0f;
        else if (r >= 1) ss.quantize = r - 1;
        if (target) target->repaint();
    });
}
