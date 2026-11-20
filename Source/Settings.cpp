// Settings dialog (audio & MIDI, focus actions, behaviours, plugins, about), app settings
// persistence, and the bank / pad MIDI setup pop-ups.
#include "Views.h"

juce::StringArray& browserFavourites() { static juce::StringArray f; return f; }

namespace
{
juce::File settingsFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Crate").getChildFile ("settings.json");
}
} // namespace

void loadAppSettings()
{
    auto& e = Engine::get();
    juce::var v;
    if (juce::JSON::parse (settingsFile().loadFileAsString(), v).failed() || ! v.isObject()) return;
    auto& b = e.behaviours;
    const auto bv = v.getProperty ("behaviours", juce::var());
    if (bv.isObject())
    {
        b.omni = bv.getProperty ("omni", b.omni);
        b.stopBehaviour = bv.getProperty ("stopBehaviour", b.stopBehaviour);
        b.mixerMuteAlsoMidi = bv.getProperty ("mixerMuteAlsoMidi", b.mixerMuteAlsoMidi);
        b.padMuteAlsoMidi = bv.getProperty ("padMuteAlsoMidi", b.padMuteAlsoMidi);
        b.defaultTrigger = (cr::TriggerMode) (int) bv.getProperty ("defaultTrigger", (int) b.defaultTrigger);
        b.defaultPolyphony = bv.getProperty ("defaultPolyphony", b.defaultPolyphony);
        b.defaultGainDb = bv.getProperty ("defaultGainDb", b.defaultGainDb);
        b.sliceAutoSnap = bv.getProperty ("sliceAutoSnap", b.sliceAutoSnap);
        b.previewVolumeDb = bv.getProperty ("previewVolumeDb", b.previewVolumeDb);
        b.clockOutDevice = bv.getProperty ("clockOutDevice", b.clockOutDevice).toString();
        b.streamLongSamples = bv.getProperty ("streamLongSamples", b.streamLongSamples);
        b.recordBitDepth = bv.getProperty ("recordBitDepth", b.recordBitDepth);
        b.automationReenableOnLoop = bv.getProperty ("automationReenableOnLoop", b.automationReenableOnLoop);
        b.automationQuantize = bv.getProperty ("automationQuantize", b.automationQuantize);
    }
    e.focus.fromVar (v.getProperty ("focus", juce::var()));
    e.rebuildMidiTables();
    browserFavourites().clear();
    if (auto* a = v.getProperty ("favourites", juce::var()).getArray()) for (auto& f : *a) browserFavourites().add (f.toString());
    if (auto* fs = v.getProperty ("pluginFolders", juce::var()).getArray()) for (auto& f : *fs) e.plugins.addSearchFolder (juce::File (f.toString()));
    const auto audio = v.getProperty ("audioDevice", "").toString();
    if (audio.isNotEmpty())
        if (auto xml = juce::parseXML (audio))
            e.deviceManager.initialise (2, 2, xml.get(), true);
}

void saveAppSettings()
{
    auto& e = Engine::get();
    auto* o = new juce::DynamicObject();
    auto* b = new juce::DynamicObject();
    const auto& bh = e.behaviours;
    b->setProperty ("omni", bh.omni);
    b->setProperty ("stopBehaviour", bh.stopBehaviour);
    b->setProperty ("mixerMuteAlsoMidi", bh.mixerMuteAlsoMidi);
    b->setProperty ("padMuteAlsoMidi", bh.padMuteAlsoMidi);
    b->setProperty ("defaultTrigger", (int) bh.defaultTrigger);
    b->setProperty ("defaultPolyphony", bh.defaultPolyphony);
    b->setProperty ("defaultGainDb", bh.defaultGainDb);
    b->setProperty ("sliceAutoSnap", bh.sliceAutoSnap);
    b->setProperty ("previewVolumeDb", bh.previewVolumeDb);
    b->setProperty ("clockOutDevice", bh.clockOutDevice);
    b->setProperty ("streamLongSamples", bh.streamLongSamples);
    b->setProperty ("recordBitDepth", bh.recordBitDepth);
    b->setProperty ("automationReenableOnLoop", bh.automationReenableOnLoop);
    b->setProperty ("automationQuantize", bh.automationQuantize);
    o->setProperty ("behaviours", juce::var (b));
    o->setProperty ("focus", e.focus.toVar());
    juce::Array<juce::var> favs;
    for (auto& f : browserFavourites()) favs.add (f);
    o->setProperty ("favourites", favs);
    juce::Array<juce::var> folders;
    const auto path = e.plugins.getSearchPath();
    for (int i = 0; i < path.getNumPaths(); ++i) folders.add (path[i].getFullPathName());
    o->setProperty ("pluginFolders", folders);
    if (auto xml = e.deviceManager.createStateXml()) o->setProperty ("audioDevice", xml->toString (juce::XmlElement::TextFormat().singleLine()));
    settingsFile().getParentDirectory().createDirectory();
    settingsFile().replaceWithText (juce::JSON::toString (juce::var (o)));
}

namespace
{
// ============================================================================
// Focus actions editor
// ============================================================================
class FocusPage : public juce::Component, private juce::ListBoxModel
{
public:
    FocusPage()
    {
        list.setModel (this);
        list.setRowHeight (28);
        addAndMakeVisible (list);
        learnBtn.setClickingTogglesState (true);
        learnBtn.onClick = [this] { setLearning (learnBtn.getToggleState()); };
        clearBtn.onClick = [this] { const int r = list.getSelectedRow(); if (r >= 0) { Engine::get().focus.bindings[(size_t) r] = {}; changed(); } };
        clearAllBtn.onClick = [this] { for (auto& b : Engine::get().focus.bindings) b = {}; changed(); };
        autoLearn.setButtonText ("After learning, move to the next action");
        autoLearn.setToggleState (true, juce::dontSendNotification);
        omni.setButtonText ("OMNI: notes go to the selected bank / pad (turn off to use bank and pad MIDI setups)");
        omni.setToggleState (Engine::get().behaviours.omni, juce::dontSendNotification);
        omni.onClick = [this] { Engine::get().behaviours.omni = omni.getToggleState(); saveAppSettings(); };
        for (auto* c : std::initializer_list<juce::Component*> { &learnBtn, &clearBtn, &clearAllBtn, &autoLearn, &omni }) addAndMakeVisible (c);
    }
    ~FocusPage() override { setLearning (false); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Pal::panel);
        g.setColour (Pal::textDim);
        g.setFont (Fonts::ui (12.5f));
        g.drawFittedText ("Focus actions let one controller drive the app: its pads trigger the pads on screen (or notes in Keys mode), and its buttons and knobs "
                          "switch modes, banks, macros and the transport. Select an action, press LEARN and touch a key, pad or knob on your controller.",
                          getLocalBounds().reduced (16, 10).removeFromTop (48), juce::Justification::topLeft, 3);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16, 10);
        r.removeFromTop (54);
        omni.setBounds (r.removeFromTop (28));
        r.removeFromTop (6);
        auto b = r.removeFromBottom (36);
        learnBtn.setBounds (b.removeFromLeft (100).reduced (0, 2));
        b.removeFromLeft (6);
        clearBtn.setBounds (b.removeFromLeft (90).reduced (0, 2));
        b.removeFromLeft (6);
        clearAllBtn.setBounds (b.removeFromLeft (100).reduced (0, 2));
        b.removeFromLeft (12);
        autoLearn.setBounds (b);
        r.removeFromBottom (6);
        list.setBounds (r);
    }

private:
    int getNumRows() override { return FocusActions::numActions; }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel) override
    {
        if (sel) { g.setColour (Pal::surface3); g.fillRect (0, 0, w, h); }
        g.setColour (Pal::text);
        g.setFont (Fonts::ui (12.5f));
        g.drawText (FocusActions::actionName (row), 10, 0, w / 2, h, juce::Justification::centredLeft);
        const auto& b = Engine::get().focus.bindings[(size_t) row];
        juce::String t = b.number < 0 ? juce::String ("-") : (b.isCC ? "CC " + juce::String (b.number) : "Note " + juce::MidiMessage::getMidiNoteName (b.number, true, true, 3)) + "  ch " + juce::String (b.channel);
        g.setColour (b.number < 0 ? Pal::textFaint : Pal::accent);
        g.drawText (t, w / 2, 0, w / 2 - 10, h, juce::Justification::centredRight);
    }

    void setLearning (bool on)
    {
        auto& e = Engine::get();
        e.focusLearnArmed = on;
        if (on)
        {
            if (list.getSelectedRow() < 0) list.selectRow (0);
            e.onFocusLearnMessage = [this] (const juce::MidiMessage& m)
            {
                const int row = list.getSelectedRow();
                if (row < 0) return;
                auto& b = Engine::get().focus.bindings[(size_t) row];
                b.isCC = m.isController();
                b.channel = m.getChannel();
                b.number = b.isCC ? m.getControllerNumber() : m.getNoteNumber();
                changed();
                if (autoLearn.getToggleState() && row + 1 < FocusActions::numActions) list.selectRow (row + 1);
                else { learnBtn.setToggleState (false, juce::dontSendNotification); setLearning (false); }
            };
        }
        else e.onFocusLearnMessage = nullptr;
    }

    void changed() { Engine::get().rebuildMidiTables(); saveAppSettings(); list.repaint(); }

    juce::ListBox list;
    juce::TextButton learnBtn { "Learn" }, clearBtn { "Clear" }, clearAllBtn { "Clear all" };
    juce::ToggleButton autoLearn, omni;
};

// ============================================================================
// Behaviours
// ============================================================================
class BehavioursPage : public juce::Component
{
public:
    BehavioursPage()
    {
        auto& b = Engine::get().behaviours;
        auto choice = [this] (const juce::String& caption, juce::StringArray items, std::function<int()> get, std::function<void (int)> set)
        {
            auto* f = fields.add (new ChoiceField (caption));
            f->choices = items;
            f->getIndex = std::move (get);
            f->setIndex = [set] (int i) { set (i); saveAppSettings(); };
            addAndMakeVisible (f);
        };
        choice ("When stopping", { "Return to start / loop start", "Return to where play started" }, [&b] { return b.stopBehaviour; }, [&b] (int i) { b.stopBehaviour = i; });
        choice ("New sample trigger", { "One shot", "Hold", "On release" }, [&b] { return (int) b.defaultTrigger; }, [&b] (int i) { b.defaultTrigger = (cr::TriggerMode) i; });
        choice ("New sample polyphony", { "1", "2", "4", "8", "16" }, [&b] { const int p[] = { 1, 2, 4, 8, 16 }; for (int i = 0; i < 5; ++i) if (b.defaultPolyphony == p[i]) return i; return 0; },
                [&b] (int i) { const int p[] = { 1, 2, 4, 8, 16 }; b.defaultPolyphony = p[i]; });
        choice ("New sample gain", { "0 dB", "-3 dB", "-6 dB", "-12 dB" }, [&b] { const float v[] = { 0, -3, -6, -12 }; for (int i = 0; i < 4; ++i) if (std::abs (b.defaultGainDb - v[i]) < 0.1f) return i; return 2; },
                [&b] (int i) { const float v[] = { 0, -3, -6, -12 }; b.defaultGainDb = v[i]; });
        choice ("Long samples (> 30 s)", { "Load into memory", "Stream from disk" }, [&b] { return b.streamLongSamples ? 1 : 0; }, [&b] (int i) { b.streamLongSamples = i == 1; });
        choice ("Recording format", { "16-bit WAV", "24-bit WAV", "32-bit float WAV" }, [&b] { return b.recordBitDepth == 16 ? 0 : (b.recordBitDepth == 24 ? 1 : 2); },
                [&b] (int i) { b.recordBitDepth = i == 0 ? 16 : (i == 1 ? 24 : 32); });
        choice ("Slices / markers", { "Free", "Snap to zero crossings" }, [&b] { return b.sliceAutoSnap ? 1 : 0; }, [&b] (int i) { b.sliceAutoSnap = i == 1; });
        choice ("Preview volume", { "-18 dB", "-12 dB", "-6 dB", "0 dB" }, [&b] { const float v[] = { -18, -12, -6, 0 }; for (int i = 0; i < 4; ++i) if (std::abs (b.previewVolumeDb - v[i]) < 0.1f) return i; return 2; },
                [&b] (int i) { const float v[] = { -18, -12, -6, 0 }; b.previewVolumeDb = v[i]; });
        choice ("Muted pad", { "Silences audio only", "Also mutes its MIDI out" }, [&b] { return b.padMuteAlsoMidi ? 1 : 0; }, [&b] (int i) { b.padMuteAlsoMidi = i == 1; });
        choice ("Muted track", { "Silences audio only", "Also mutes its MIDI out" }, [&b] { return b.mixerMuteAlsoMidi ? 1 : 0; }, [&b] (int i) { b.mixerMuteAlsoMidi = i == 1; });
        choice ("Automation recording grid", cr::quantizeNames(), [&b] { return b.automationQuantize; }, [&b] (int i) { b.automationQuantize = i; });
        choice ("Overwritten automation", { "Stays off until stop", "Comes back on loop" }, [&b] { return b.automationReenableOnLoop ? 1 : 0; }, [&b] (int i) { b.automationReenableOnLoop = i == 1; });
        auto outs = Engine::get().getMidiOutputNames();
        outs.insert (0, "Off");
        choice ("MIDI clock out", outs, [&b, outs] { return juce::jmax (0, outs.indexOf (b.clockOutDevice)); }, [&b, outs] (int i) { b.clockOutDevice = i == 0 ? juce::String() : outs[i]; });
        auto* tune = new Knob ("Master tune");
        tune->setCustom (-100.0f, 100.0f, 0.0f, [] { return Engine::get().session().globalTuneCents; }, [] (float v) { Engine::get().session().globalTuneCents = v; },
                         [] (float v) { return juce::String (v, 0) + " ct"; });
        knobs.add (tune);
        addAndMakeVisible (tune);
    }

    void paint (juce::Graphics& g) override { g.fillAll (Pal::panel); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (16, 14);
        const int cols = 3, w = r.getWidth() / cols;
        int i = 0;
        for (auto* f : fields) { f->setBounds (r.getX() + (i % cols) * w, r.getY() + (i / cols) * 58, w - 12, 46); ++i; }
        for (auto* k : knobs) { k->setBounds (r.getX() + (i % cols) * w, r.getY() + (i / cols) * 58, 90, 70); ++i; }
    }

private:
    juce::OwnedArray<ChoiceField> fields;
    juce::OwnedArray<Knob> knobs;
};

// ============================================================================
// Plugins
// ============================================================================
class PluginsPage : public juce::Component, private juce::Timer
{
public:
    PluginsPage()
    {
        scanBtn.onClick = [this]
        {
            status = "Scanning...";
            Engine::get().plugins.startScan ([this] (juce::String s, bool done)
            {
                juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PluginsPage> (this), s, done]
                {
                    if (safe == nullptr) return;
                    safe->status = done ? "Scan finished: " + juce::String (Engine::get().plugins.knownPlugins.getNumTypes()) + " plugins" : s;
                    safe->repaint();
                });
            });
            repaint();
        };
        addBtn.onClick = [this]
        {
            chooser = std::make_unique<juce::FileChooser> ("Add a plugin folder", juce::File::getSpecialLocation (juce::File::userHomeDirectory));
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
            {
                if (fc.getResult().isDirectory()) { Engine::get().plugins.addSearchFolder (fc.getResult()); saveAppSettings(); repaint(); }
            });
        };
        addAndMakeVisible (scanBtn);
        addAndMakeVisible (addBtn);
        startTimerHz (4);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Pal::panel);
        auto& p = Engine::get().plugins;
        auto r = getLocalBounds().reduced (16, 14);
        r.removeFromTop (44);
        g.setColour (Pal::accent);
        g.setFont (Fonts::ui (13.0f, true));
        g.drawFittedText (status, r.removeFromTop (24), juce::Justification::centredLeft, 1);
        drawCaps (g, "Folders searched", r.removeFromTop (22), juce::Justification::centredLeft, Pal::textDim, 10.0f);
        g.setFont (Fonts::ui (12.0f));
        g.setColour (Pal::textDim);
        const auto path = p.getSearchPath();
        for (int i = 0; i < path.getNumPaths(); ++i) g.drawText (path[i].getFullPathName(), r.removeFromTop (20), juce::Justification::centredLeft);
        r.removeFromTop (10);
        drawCaps (g, "Found: " + juce::String (p.getInstruments().size()) + " instruments, " + juce::String (p.getEffects().size()) + " effects", r.removeFromTop (22), juce::Justification::centredLeft, Pal::textDim, 10.0f);
        g.setColour (Pal::text);
        for (auto& d : p.knownPlugins.getTypes())
        {
            if (r.getHeight() < 20) break;
            g.drawText (d.name + "   " + d.manufacturerName + "   (" + d.pluginFormatName + (d.isInstrument ? ", instrument)" : ", effect)"), r.removeFromTop (19), juce::Justification::centredLeft);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16, 14).removeFromTop (34);
        scanBtn.setBounds (r.removeFromLeft (160));
        r.removeFromLeft (8);
        addBtn.setBounds (r.removeFromLeft (160));
    }

private:
    void timerCallback() override { scanBtn.setEnabled (! Engine::get().plugins.isScanning()); }
    juce::TextButton scanBtn { "Scan for plugins" }, addBtn { "Add a folder..." };
    juce::String status { "Plugins are scanned in a separate step so a broken plugin can't take the app down." };
    std::unique_ptr<juce::FileChooser> chooser;
};

class AboutPage : public juce::Component
{
public:
    void paint (juce::Graphics& g) override
    {
        g.fillAll (Pal::panel);
        auto r = getLocalBounds().reduced (24, 20);
        g.setColour (Pal::text);
        g.setFont (Fonts::heavy (26.0f));
        g.drawText (JUCE_APPLICATION_NAME_STRING, r.removeFromTop (36), juce::Justification::centredLeft);
        g.setColour (Pal::textDim);
        g.setFont (Fonts::ui (13.0f));
        g.drawText ("Version " JUCE_APPLICATION_VERSION_STRING, r.removeFromTop (22), juce::Justification::centredLeft);
        r.removeFromTop (12);
        g.drawFittedText ("Pad sampler, sequencer and mixer for Windows and macOS.\n\n"
                          "Library folder: " + Engine::getLibraryFolder().getFullPathName() + "\n\n"
                          "Shortcuts: Space play/stop, R record, L loop, C metronome, T tap tempo, F1-F4 views, Tab next view, "
                          "B browser, X effects, M macros, K keyboard, Ctrl/Cmd+Z undo, Ctrl/Cmd+S save, Ctrl/Cmd+E export. "
                          "Computer keys Z X C V / A S D F / Q W E R / 1 2 3 4 play the 16 pads.\n\n"
                          "Built with JUCE. Interface font: Inter (SIL Open Font License).",
                          r.removeFromTop (240), juce::Justification::topLeft, 14);
    }
};

class SettingsComponent : public juce::Component
{
public:
    explicit SettingsComponent (int tab)
        : tabs (juce::TabbedButtonBar::TabsAtTop),
          devices (Engine::get().deviceManager, 0, 32, 0, 32, true, true, true, false)
    {
        tabs.setTabBarDepth (36);
        tabs.getTabbedButtonBar().setColour (juce::TabbedButtonBar::tabTextColourId, Pal::textDim);
        tabs.getTabbedButtonBar().setColour (juce::TabbedButtonBar::frontTextColourId, Pal::text);
        tabs.getTabbedButtonBar().setColour (juce::TabbedButtonBar::tabOutlineColourId, Pal::outline);
        tabs.getTabbedButtonBar().setColour (juce::TabbedButtonBar::frontOutlineColourId, Pal::accent);
        tabs.setColour (juce::TabbedComponent::backgroundColourId, Pal::panel);
        tabs.setColour (juce::TabbedComponent::outlineColourId, Pal::outline);
        tabs.addTab ("Audio & MIDI", Pal::panel, &devices, false);
        tabs.addTab ("MIDI focus", Pal::panel, new FocusPage(), true);
        tabs.addTab ("Behaviours", Pal::panel, new BehavioursPage(), true);
        tabs.addTab ("Plugins", Pal::panel, new PluginsPage(), true);
        tabs.addTab ("About", Pal::panel, new AboutPage(), true);
        tabs.setCurrentTabIndex (juce::jlimit (0, 4, tab));
        addAndMakeVisible (tabs);
        setSize (860, 640);
    }
    ~SettingsComponent() override { saveAppSettings(); }
    void resized() override { tabs.setBounds (getLocalBounds()); }

private:
    juce::TabbedComponent tabs;
    juce::AudioDeviceSelectorComponent devices;
};

// ============================================================================
// MIDI setup pop-ups
// ============================================================================
class MidiSetupComponent : public juce::Component
{
public:
    MidiSetupComponent (int bank, int padIndex) : b (bank), p (padIndex)
    {
        auto& e = Engine::get();
        auto ins = e.getMidiInputNames();
        auto outs = e.getMidiOutputNames();
        ins.insert (0, padIndex < 0 ? "All devices" : "Follow the bank");
        outs.insert (0, padIndex < 0 ? "No MIDI out" : "Bank output");
        juce::StringArray channels { padIndex < 0 ? "All channels" : "Follow the bank" };
        for (int c = 1; c <= 16; ++c) channels.add ("Channel " + juce::String (c));
        juce::StringArray outChannels;
        if (padIndex >= 0) outChannels.add ("Automatic");
        for (int c = 1; c <= 16; ++c) outChannels.add ("Channel " + juce::String (c));

        add ("Input device", ins,
             [this, ins] { const auto& d = isPad() ? padRef().midi.inDevice : bankRef().midi.inDevice; return juce::jmax (0, ins.indexOf (d)); },
             [this, ins] (int i) { auto v = i == 0 ? juce::String() : ins[i]; if (isPad()) padRef().midi.inDevice = v; else bankRef().midi.inDevice = v; });
        add ("Input channel", channels,
             [this] { return isPad() ? padRef().midi.inChannel : bankRef().midi.inChannel; },
             [this] (int i) { if (isPad()) padRef().midi.inChannel = i; else bankRef().midi.inChannel = i; });
        if (! isPad())
        {
            add ("Mode", { "Single channel: notes pick pads", "Multi channel: channel = pad, notes play keys" },
                 [this] { return bankRef().midi.multiChannel ? 1 : 0; }, [this] (int i) { bankRef().midi.multiChannel = i == 1; });
            juce::StringArray notes;
            for (int n = 0; n < 128; ++n) notes.add (juce::MidiMessage::getMidiNoteName (n, true, true, 3) + " (" + juce::String (n) + ")");
            add ("Note for pad 1", notes, [this] { return bankRef().midi.baseNote; }, [this] (int i) { bankRef().midi.baseNote = i; });
        }
        add ("Output device", outs,
             [this, outs] { const auto& d = isPad() ? padRef().midi.outDevice : bankRef().midi.outDevice; return juce::jmax (0, outs.indexOf (d)); },
             [this, outs] (int i) { auto v = i == 0 ? juce::String() : outs[i]; if (isPad()) padRef().midi.outDevice = v; else bankRef().midi.outDevice = v; });
        add ("Output channel", outChannels,
             [this] { return isPad() ? padRef().midi.outChannel : bankRef().midi.outChannel - 1; },
             [this] (int i) { if (isPad()) padRef().midi.outChannel = i; else bankRef().midi.outChannel = i + 1; });
        setSize (360, 40 + fields.size() * 52);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (Pal::text);
        g.setFont (Fonts::ui (14.0f, true));
        g.drawText ((isPad() ? "MIDI setup: pad " + juce::String (p + 1) : "MIDI setup: " + Engine::get().session().banks[(size_t) b].name), getLocalBounds().removeFromTop (28), juce::Justification::centredLeft);
        if (Engine::get().behaviours.omni)
            drawCaps (g, "OMNI is on: input setups apply when it's off (Settings > MIDI focus)", getLocalBounds().removeFromBottom (14), juce::Justification::centredLeft, Pal::textFaint, 8.5f);
    }

    void resized() override
    {
        auto r = getLocalBounds().withTrimmedTop (34);
        for (auto* f : fields) f->setBounds (r.removeFromTop (52).reduced (0, 4));
    }

private:
    bool isPad() const { return p >= 0; }
    cr::Pad& padRef() { return Engine::get().session().banks[(size_t) b].pads[(size_t) p]; }
    cr::Bank& bankRef() { return Engine::get().session().banks[(size_t) b]; }
    void add (const juce::String& caption, const juce::StringArray& items, std::function<int()> get, std::function<void (int)> set)
    {
        auto* f = fields.add (new ChoiceField (caption));
        f->choices = items;
        f->getIndex = std::move (get);
        f->setIndex = std::move (set);
        addAndMakeVisible (f);
    }
    int b, p;
    juce::OwnedArray<ChoiceField> fields;
};
} // namespace

void showSettingsDialog (int initialTab)
{
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new SettingsComponent (initialTab));
    o.dialogTitle = "Settings";
    o.dialogBackgroundColour = Pal::panel;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.launchAsync();
}

void showBankMidiSetup (int bank, juce::Component* target)
{
    Engine::get().createBank (bank);
    auto c = std::make_unique<MidiSetupComponent> (bank, -1);
    juce::CallOutBox::launchAsynchronously (std::move (c), target != nullptr ? target->getScreenBounds() : juce::Rectangle<int> (200, 200, 10, 10), nullptr);
}

void showPadMidiSetup (int bank, int pad, juce::Component* target)
{
    auto c = std::make_unique<MidiSetupComponent> (bank, pad);
    juce::CallOutBox::launchAsynchronously (std::move (c), target != nullptr ? target->getScreenBounds() : juce::Rectangle<int> (200, 200, 10, 10), nullptr);
}
