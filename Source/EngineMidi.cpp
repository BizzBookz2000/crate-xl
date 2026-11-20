// MIDI input routing (focus actions, MIDI learn, bank / pad setups, OMNI), MIDI outputs.
#include "Engine.h"

// ============================================================================
// Focus actions
// ============================================================================
juce::String FocusActions::actionName (int a)
{
    if (a >= triggerPad1 && a < triggerPad1 + 64) return "Trigger Pad " + juce::String (a - triggerPad1 + 1);
    if (a >= macro1 && a < macro1 + 16) return "Macro " + juce::String (a - macro1 + 1);
    if (a >= selectBankA && a < selectBankA + 8) return "Select Bank " + cr::bankLetter (a - selectBankA);
    if (a >= selectGroup1 && a < selectGroup1 + 16) return "Select Bank Group " + juce::String (a - selectGroup1 + 1);
    if (a >= repeat1 && a < repeat1 + 6) return "Note Repeat " + juce::String (a - repeat1 + 1);
    switch (a)
    {
        case modeSelect: return "Select Mode";
        case modeScene: return "Scene Mode";
        case modeMute: return "Mute Mode";
        case modeSolo: return "Solo Mode";
        case toggleKeys: return "Keys Mode";
        case prevBank: return "Previous Bank";
        case nextBank: return "Next Bank";
        case prevGroup: return "Previous Bank Group";
        case nextGroup: return "Next Bank Group";
        case octaveDown: return "Octave Down";
        case octaveUp: return "Octave Up";
        case transportPlay: return "Play / Stop";
        case transportStop: return "Stop";
        case transportRecord: return "Record";
        case transportTap: return "Tap Tempo";
        default: return "Action " + juce::String (a);
    }
}

juce::var FocusActions::toVar() const
{
    juce::Array<juce::var> arr;
    for (int i = 0; i < numActions; ++i)
    {
        const auto& b = bindings[(size_t) i];
        if (b.number < 0) continue;
        auto* o = new juce::DynamicObject();
        o->setProperty ("action", i);
        o->setProperty ("cc", b.isCC);
        o->setProperty ("channel", b.channel);
        o->setProperty ("number", b.number);
        arr.add (juce::var (o));
    }
    return arr;
}

void FocusActions::fromVar (const juce::var& v)
{
    for (auto& b : bindings) b = {};
    if (auto* arr = v.getArray())
        for (auto& e : *arr)
        {
            const int a = e.getProperty ("action", -1);
            if (! juce::isPositiveAndBelow (a, (int) numActions)) continue;
            auto& b = bindings[(size_t) a];
            b.isCC = e.getProperty ("cc", false);
            b.channel = e.getProperty ("channel", 1);
            b.number = e.getProperty ("number", -1);
        }
}

// ============================================================================
// MIDI thread
// ============================================================================
void Engine::handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& m)
{
    if (m.isActiveSense() || m.isMidiClock() || m.isSysEx()) return;

    auto toUi = [this] (const juce::MidiMessage& msg, int action)
    {
        {
            const juce::SpinLock::ScopedLockType sl (uiMidiLock);
            if (uiMidiQueue.size() < 512) uiMidiQueue.push_back ({ msg, action });
        }
        triggerAsyncUpdate();
    };

    if (focusLearnArmed.load() && (m.isNoteOn() || m.isController())) { toUi (m, -2); return; }
    if (learnArmed.load() && m.isController()) { toUi (m, -3); return; }
    if (mappingLearnArmed.load() && m.isNoteOn()) toUi (m, -4);

    // focus actions
    const int ch = juce::jlimit (1, 16, m.getChannel());
    if (m.isNoteOnOrOff() || m.isController())
    {
        const bool isCC = m.isController();
        const int number = isCC ? m.getControllerNumber() : m.getNoteNumber();
        const short action = focusTable[(size_t) ((isCC ? 1 : 0) * 2048 + (ch - 1) * 128 + number)].load();
        if (action >= 0)
        {
            if (action < 64)
            {
                const int b = selectedBank.load();
                const bool on = isCC ? m.getControllerValue() > 0 : m.isNoteOn();
                float vel = isCC ? 1.0f : m.getFloatVelocity();
                if (fixedMidiVelocity.load() && on) vel = fixedVelocity.load();
                const int sp = selectedPad.load();
                const bool keys = sess.banks[(size_t) b].pads[(size_t) sp].keysMode;
                if (padMode.load() == 4) { if (on) toUi (m, 10000 + action); return; }
                if (keys)
                {
                    const int key = juce::jlimit (0, 127, keysOctave.load() * 12 + action);
                    for (int k : expandKeys (key)) { if (on) padDown (b, sp, vel, k); else padUp (b, sp, k); }
                }
                else
                {
                    const int pad = juce::jlimit (0, cr::numPads - 1, padPage.load() * padsPerPage.load() + action);
                    if (on && padMode.load() != 0) { toUi (m, 20000 + pad); return; }
                    if (on) padDown (b, pad, vel); else padUp (b, pad);
                }
                return;
            }
            toUi (m, action);
            return;
        }
    }

    // MIDI learn mappings (CC -> parameter), applied on the message thread
    if (m.isController() && ccIsMapped[(size_t) ((ch - 1) * 128 + m.getControllerNumber())].load()) { toUi (m, -1); return; }

    const juce::SpinLock::ScopedLockType sl (midiQueueLock);
    if (midiQueue.size() < 1000) midiQueue.push_back ({ m, source != nullptr ? source->getName() : juce::String() });
}

// ============================================================================
// Audio thread: route queued MIDI to banks / pads
// ============================================================================
void Engine::routeMidiInput (int)
{
    {
        const juce::SpinLock::ScopedLockType sl (midiQueueLock);
        midiScratch.swap (midiQueue);
        midiQueue.clear();
    }
    for (auto& mi : midiScratch) routeMidiMessage (mi.msg, mi.device, 0);
    midiScratch.clear();
}

void Engine::routeMidiMessage (const juce::MidiMessage& m, const juce::String& device, int offset)
{
    const int ch = juce::jlimit (1, 16, m.getChannel());
    const double tick = position;

    auto playKeys = [&] (int b, int p, int note, float vel, bool on)
    {
        for (int k : expandKeys (note)) handleLiveEvent ({ b, p, k, vel, on }, offset, tick);
    };
    auto controllerTo = [&] (int b, int p)
    {
        if (m.isPitchWheel()) pitchBend[(size_t) b] = (float) (m.getPitchWheelValue() - 8192) / 8192.0f;
        else if (m.isChannelPressure()) aftertouch[(size_t) b] = (float) m.getChannelPressureValue() / 127.0f;
        else if (m.isAftertouch()) aftertouch[(size_t) b] = (float) m.getAfterTouchValue() / 127.0f;
        else if (m.isController())
        {
            if (m.getControllerNumber() == 1) modWheel[(size_t) b] = (float) m.getControllerValue() / 127.0f;
            if (m.getControllerNumber() == 64) sustainPedal[(size_t) b] = m.getControllerValue() >= 64;
        }
        // plugin instruments get controllers too
        for (size_t i = 0; i < instrumentPads.size(); ++i)
            if (instrumentPads[i] / cr::numPads == b && (p < 0 || instrumentPads[i] % cr::numPads == p))
            {
                auto msg = m;
                msg.setChannel (1);
                instrumentMidi[i].addEvent (msg, offset);
            }
        uiStateChanged = true;
    };
    auto velocityOf = [&] () { return fixedMidiVelocity.load() ? fixedVelocity.load() : m.getFloatVelocity(); };

    // 1. pads with their own MIDI input
    bool handled = false;
    if (! behaviours.omni)
    {
        for (int b = 0; b < cr::numBanks; ++b)
        {
            auto& bank = sess.banks[(size_t) b];
            if (! bank.created) continue;
            for (int p = 0; p < cr::numPads; ++p)
            {
                const auto& pm = bank.pads[(size_t) p].midi;
                if (pm.inDevice.isEmpty() && pm.inChannel == 0) continue;
                if (pm.inDevice.isNotEmpty() && pm.inDevice != device) continue;
                if (pm.inChannel > 0 && pm.inChannel != ch) continue;
                if (m.isNoteOn()) playKeys (b, p, m.getNoteNumber(), velocityOf(), true);
                else if (m.isNoteOff()) playKeys (b, p, m.getNoteNumber(), 0.0f, false);
                else controllerTo (b, p);
                handled = true;
            }
        }
        if (handled) return;

        // 2. banks with a MIDI input setup
        for (int b = 0; b < cr::numBanks; ++b)
        {
            auto& bank = sess.banks[(size_t) b];
            if (! bank.created) continue;
            const auto& bm = bank.midi;
            if (bm.inDevice.isNotEmpty() && bm.inDevice != device) continue;
            if (! bm.multiChannel && bm.inChannel > 0 && bm.inChannel != ch) continue;
            if (bm.inDevice.isEmpty() && bm.inChannel == 0 && ! bm.multiChannel) continue;   // not configured
            handled = true;
            if (m.isNoteOnOrOff())
            {
                const bool on = m.isNoteOn();
                const float vel = on ? velocityOf() : 0.0f;
                if (bm.multiChannel) playKeys (b, juce::jlimit (0, cr::numPads - 1, ch - 1), m.getNoteNumber(), vel, on);
                else
                {
                    const int sp = selectedBank.load() == b ? selectedPad.load() : 0;
                    if (bank.pads[(size_t) sp].keysMode) playKeys (b, sp, m.getNoteNumber(), vel, on);
                    else
                    {
                        const int p = m.getNoteNumber() - bm.baseNote;
                        if (juce::isPositiveAndBelow (p, cr::numPads)) handleLiveEvent ({ b, p, -1, vel, on }, offset, tick);
                    }
                }
            }
            else controllerTo (b, bm.multiChannel ? ch - 1 : -1);
        }
        if (handled) return;
    }

    // 3. OMNI: the selected bank / pad
    const int b = selectedBank.load();
    auto& bank = sess.banks[(size_t) b];
    if (! bank.created) return;
    const int sp = selectedPad.load();
    const auto& pad = bank.pads[(size_t) sp];
    if (m.isNoteOnOrOff())
    {
        const bool on = m.isNoteOn();
        const float vel = on ? velocityOf() : 0.0f;
        if (pad.keysMode || pad.instrument != nullptr) playKeys (b, sp, m.getNoteNumber(), vel, on);
        else
        {
            const int p = m.getNoteNumber() - 36;   // C1 = pad 1, the usual drum-controller layout
            if (juce::isPositiveAndBelow (p, cr::numPads)) handleLiveEvent ({ b, p, -1, vel, on }, offset, tick);
        }
    }
    else controllerTo (b, sp);
}

// ============================================================================
// Message thread: learn, mapped CCs and focus actions
// ============================================================================
void Engine::handleAsyncUpdate()
{
    std::vector<UiMidi> local;
    {
        const juce::SpinLock::ScopedLockType sl (uiMidiLock);
        local.swap (uiMidiQueue);
    }
    for (auto& u : local)
    {
        const auto& m = u.msg;
        if (u.action == -2) { if (onFocusLearnMessage) onFocusLearnMessage (m); continue; }
        if (u.action == -4) { if (onMappingLearnNote) onMappingLearnNote (m.getNoteNumber()); continue; }
        if (u.action == -3)
        {
            if (! learnArmed.load()) continue;
            {
                const juce::ScopedLock sl (lock);
                auto& mm = sess.midiMappings;
                mm.erase (std::remove_if (mm.begin(), mm.end(), [&] (const cr::MidiMapping& x)
                                          { return x.param == learnTarget || (x.channel == m.getChannel() && x.controller == m.getControllerNumber()); }), mm.end());
                cr::MidiMapping map;
                map.channel = m.getChannel();
                map.controller = m.getControllerNumber();
                map.param = learnTarget;
                map.address = cr::ParamAddress::parse (learnTarget);
                mm.push_back (map);
            }
            learnArmed = false;
            rebuildMidiTables();
            sendChangeMessage();
            continue;
        }
        if (u.action == -1)
        {
            for (auto& map : sess.midiMappings)
                if (map.channel == m.getChannel() && map.controller == m.getControllerNumber())
                    setParamNormalised (map.address, (float) m.getControllerValue() / 127.0f, true);
            continue;
        }
        if (u.action >= 20000) { selectedPad = u.action - 20000; uiStateChanged = true; continue; }
        if (u.action >= 10000) { launchScene (u.action - 10000); uiStateChanged = true; continue; }
        const bool isCC = m.isController();
        const float value = isCC ? (float) m.getControllerValue() / 127.0f : 1.0f;
        const bool on = isCC ? m.getControllerValue() > 0 : m.isNoteOn();
        handleFocusAction (u.action, value, on);
    }
}

void Engine::handleFocusAction (int a, float value, bool on)
{
    using F = FocusActions;
    if (a >= F::macro1 && a < F::macro1 + 16) { setMacro (a - F::macro1, value); uiStateChanged = true; return; }
    if (! on) return;
    auto toggleMode = [this] (int mode) { padMode = padMode.load() == mode ? 0 : mode; };
    if (a == F::modeSelect) toggleMode (1);
    else if (a == F::modeMute) toggleMode (2);
    else if (a == F::modeSolo) toggleMode (3);
    else if (a == F::modeScene) toggleMode (4);
    else if (a == F::toggleKeys)
    {
        const juce::ScopedLock sl (lock);
        auto& pad = sess.banks[(size_t) selectedBank.load()].pads[(size_t) selectedPad.load()];
        pad.keysMode = ! pad.keysMode;
    }
    else if (a >= F::selectBankA && a < F::selectBankA + 8)
    {
        const int b = (selectedBank.load() / cr::banksPerGroup) * cr::banksPerGroup + (a - F::selectBankA);
        createBank (b);
        selectedBank = b;
    }
    else if (a == F::prevBank || a == F::nextBank)
    {
        const int b = juce::jlimit (0, cr::numBanks - 1, selectedBank.load() + (a == F::nextBank ? 1 : -1));
        createBank (b);
        selectedBank = b;
    }
    else if (a >= F::selectGroup1 && a < F::selectGroup1 + 16)
    {
        const int b = (a - F::selectGroup1) * cr::banksPerGroup;
        createBank (b);
        selectedBank = b;
    }
    else if (a == F::prevGroup || a == F::nextGroup)
    {
        const int g = juce::jlimit (0, cr::numGroups - 1, selectedBank.load() / cr::banksPerGroup + (a == F::nextGroup ? 1 : -1));
        createBank (g * cr::banksPerGroup);
        selectedBank = g * cr::banksPerGroup;
    }
    else if (a == F::octaveDown) keysOctave = juce::jmax (0, keysOctave.load() - 1);
    else if (a == F::octaveUp) keysOctave = juce::jmin (9, keysOctave.load() + 1);
    else if (a >= F::repeat1 && a < F::repeat1 + 6) repeatSlot = repeatSlot.load() == a - F::repeat1 ? -1 : a - F::repeat1;
    else if (a == F::transportPlay) togglePlay();
    else if (a == F::transportStop) stop();
    else if (a == F::transportRecord) setRecording (! recording);
    else if (a == F::transportTap) tapTempo();
    uiStateChanged = true;
}

// ============================================================================
// MIDI outputs
// ============================================================================
juce::StringArray Engine::getMidiInputNames() const
{
    juce::StringArray names;
    for (auto& d : juce::MidiInput::getAvailableDevices()) names.add (d.name);
    return names;
}

void Engine::refreshMidiOutputs()
{
    const auto devices = juce::MidiOutput::getAvailableDevices();
    juce::StringArray names;
    for (auto& d : devices) names.add (d.name);

    std::vector<std::pair<juce::String, std::unique_ptr<juce::MidiOutput>>> opened;
    for (auto& d : devices)
        if (midiOutputs.find (d.name) == midiOutputs.end())
            if (auto o = juce::MidiOutput::openDevice (d.identifier)) opened.push_back ({ d.name, std::move (o) });

    std::vector<std::unique_ptr<juce::MidiOutput>> closed;
    {
        const juce::ScopedLock sl (lock);
        for (auto& o : opened)
        {
            midiOutputs[o.first] = std::move (o.second);
            midiOutBuffers[o.first].ensureSize (2048);
        }
        for (auto it = midiOutputs.begin(); it != midiOutputs.end();)
        {
            if (! names.contains (it->first)) { closed.push_back (std::move (it->second)); midiOutBuffers.erase (it->first); it = midiOutputs.erase (it); }
            else ++it;
        }
        midiOutputNames = names;
    }
    // also enable newly connected inputs
    static juce::StringArray knownInputs;
    for (auto& d : juce::MidiInput::getAvailableDevices())
        if (! knownInputs.contains (d.identifier))
        {
            knownInputs.add (d.identifier);
            if (! deviceManager.isMidiInputDeviceEnabled (d.identifier)) deviceManager.setMidiInputDeviceEnabled (d.identifier, true);
        }
}
