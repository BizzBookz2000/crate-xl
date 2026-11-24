// Engine core: construction, session defaults, transport, the audio callback and the mixer.
#include "Engine.h"
#include <map>
#include "FileUtils.h"

Engine* Engine::instance = nullptr;

namespace
{
constexpr int padKey (int bank, int pad) { return bank * cr::numPads + pad; }
constexpr int maxVoices = 256;

void applyPanVolume (juce::AudioBuffer<float>& b, int n, float volume, float pan)
{
    const float l = volume * juce::jmin (1.0f, 1.0f - pan);
    const float r = volume * juce::jmin (1.0f, 1.0f + pan);
    b.applyGain (0, 0, n, l);
    if (b.getNumChannels() > 1) b.applyGain (1, 0, n, r);
}
} // namespace

// ============================================================================
// Construction
// ============================================================================
Engine::Engine (bool openDevices)
{
    instance = this;
    for (auto& b : padFlash) for (auto& p : b) p = 0.0f;
    for (auto& v : pitchBend) v = 0.0f;
    for (auto& v : modWheel) v = 0.0f;
    for (auto& v : aftertouch) v = 0.0f;
    for (auto& v : sustainPedal) v = false;
    for (auto& v : ccIsMapped) v = false;
    for (auto& v : focusTable) v = -1;
    for (auto& row : heldKey) for (auto& k : row) k = -1;

    formatManager.registerBasicFormats();
    voices.resize (maxVoices);
    for (auto& v : voices) { v.winL.resize (8192); v.winR.resize (8192); }   // disk-streaming windows
    events.reserve (8192);
    for (int i = 0; i < (int) activeInputToHardware.size(); ++i) activeInputToHardware[(size_t) i] = i;
    clipStreamL.assign (65536, 0.0f);
    clipStreamR.assign (65536, 0.0f);
    liveQueue.reserve (1024);
    liveScratch.reserve (1024);
    midiQueue.reserve (1024);
    midiScratch.reserve (1024);
    pendingRecords.reserve (128);
    uiMidiQueue.reserve (256);

    resetSessionToDefaults();
    prepare (44100.0, 512);
    diskWriterThread.startThread();

    if (openDevices)
    {
        deviceManager.initialiseWithDefaultDevices (2, 2);
        for (auto& d : juce::MidiInput::getAvailableDevices())
            deviceManager.setMidiInputDeviceEnabled (d.identifier, true);
        deviceManager.addMidiInputDeviceCallback ({}, this);
        deviceManager.addAudioCallback (this);
        refreshMidiOutputs();
    }
    startTimerHz (20);
}

Engine::~Engine()
{
    stopTimer();
    deviceManager.removeAudioCallback (this);
    deviceManager.removeMidiInputDeviceCallback ({}, this);
    workers.removeAllJobs (true, 4000);
    PluginWindow::closeAll();
    for (auto& w : trackWriters) w.reset();
    diskWriterThread.stopThread (2000);

    const juce::ScopedLock sl (lock);
    for (auto& b : sess.banks)
    {
        for (auto& p : b.pads)
        {
            p.instrument.reset();
            for (auto& s : p.channel.inserts) s.plugin.reset();
            for (auto& l : p.layers) for (auto& f : l.fx) f.plugin.reset();
        }
        for (auto& s : b.strip.inserts) s.plugin.reset();
    }
    for (auto& t : sess.audioTracks) for (auto& s : t.strip.inserts) s.plugin.reset();
    for (auto& a : sess.aux) for (auto& s : a.inserts) s.plugin.reset();
    for (auto& s : sess.master.inserts) s.plugin.reset();
    pluginGraveyard.clear();
    instance = nullptr;
}

void Engine::resetAudioTrack (cr::AudioTrack& tr, int t)
{
    tr.created = false;
    tr.name = "Audio " + juce::String (t + 1);
    tr.colour = juce::Colour (0xff9aa7b5).withRotatedHue ((float) t * 0.07f);
    tr.strip.inserts.clear();
    tr.strip.name = tr.name;
    tr.strip.volume = 0.8f;
    tr.strip.pan = 0.0f;
    tr.strip.mute = tr.strip.solo = false;
    for (int a = 0; a < cr::numAux; ++a) { tr.strip.send[a] = 0.0f; tr.strip.sendPre[a] = false; }
    tr.strip.output = 0;
    tr.strip.eq = {};
    tr.strip.frozen = false;
    tr.frozenFrom = -1;
    tr.inputSource = 0; tr.inputPair = 0; tr.inputMono = false; tr.inputGainDb = 0.0f;
    tr.gate = false; tr.gateThresholdDb = -50.0f;
    tr.armed = tr.monitor = false;
    tr.source = cr::TrackSource::song;
    tr.clips.clear();
}

cr::Layer Engine::makeDefaultLayer() const
{
    cr::Layer l;
    l.p.trigger = behaviours.defaultTrigger;
    l.p.polyphony = behaviours.defaultPolyphony;
    l.p.gainDb = behaviours.defaultGainDb;
    l.p.envMode = cr::EnvMode::ahdsr;
    for (int i = 0; i < 16; ++i) l.step.steps[(size_t) i] = (i % 4 == 0) ? 1.0f : (i % 2 == 0 ? 0.5f : 0.0f);
    return l;
}

void Engine::initialiseBank (int b)
{
    auto& bank = sess.banks[(size_t) b];
    bank.created = false;
    bank.name = "Bank " + cr::bankName (b);
    bank.colour = cr::bankColour (b);
    for (int p = 0; p < cr::numPads; ++p)
    {
        auto& pad = bank.pads[(size_t) p];
        for (auto& l : pad.layers) for (auto& z : l.zones) retire (z.sample);
        if (pad.instrument) retirePlugin (std::move (pad.instrument));
        for (auto& s : pad.channel.inserts) if (s.plugin) retirePlugin (std::move (s.plugin));
        for (auto& l : pad.layers) for (auto& f : l.fx) if (f.plugin) retirePlugin (std::move (f.plugin));
        pad.layers.clear();
        pad.name.clear();
        pad.colour = {};
        pad.padNote = 60;
        pad.keysMode = false;
        pad.choke = pad.link = 0;
        pad.midi = {};
        pad.channel.name = "Pad " + juce::String (p + 1);
        pad.channel.volume = 0.8f;
        pad.channel.pan = 0.0f;
        pad.channel.mute = pad.channel.solo = false;
        for (int a = 0; a < cr::numAux; ++a) { pad.channel.send[a] = 0.0f; pad.channel.sendPre[a] = false; }
        pad.channel.inserts.clear();
        pad.channel.output = 0;
        pad.channel.eq = {};
    }
    bank.patterns.assign (1, cr::Pattern());
    bank.patterns[0].name = "Pattern 1";
    bank.patterns[0].length = cr::barTicks();
    bank.workingPattern = 0;
    bank.playingPattern = 0;
    bank.source = cr::TrackSource::song;
    bank.queued = {};
    bank.midi = {};
    for (auto& s : bank.strip.inserts) if (s.plugin) retirePlugin (std::move (s.plugin));
    bank.strip.inserts.clear();
    bank.strip.name = bank.name;
    bank.strip.volume = 0.8f;
    bank.strip.pan = 0.0f;
    bank.strip.mute = bank.strip.solo = false;
    for (int a = 0; a < cr::numAux; ++a) { bank.strip.send[a] = 0.0f; bank.strip.sendPre[a] = false; }
    bank.strip.output = 0;
    bank.strip.eq = {};
    bank.strip.frozen = false;
    bank.recordArm = true;
}

void Engine::resetSessionToDefaults()
{
    PluginWindow::closeAll();
    const juce::ScopedLock sl (lock);
    for (int b = 0; b < cr::numBanks; ++b) initialiseBank (b);

    for (int t = 0; t < cr::maxAudioTracks; ++t)
    {
        auto& tr = sess.audioTracks[(size_t) t];
        for (auto& s : tr.strip.inserts) if (s.plugin) retirePlugin (std::move (s.plugin));
        for (auto& c : tr.clips) retire (c.sample);
        resetAudioTrack (tr, t);
    }

    for (int a = 0; a < cr::numAux; ++a)
    {
        auto& aux = sess.aux[(size_t) a];
        for (auto& s : aux.inserts) if (s.plugin) retirePlugin (std::move (s.plugin));
        aux.inserts.clear();
        aux.name = "AUX " + juce::String (a + 1);
        aux.volume = 0.8f;
        aux.pan = 0.0f;
        aux.mute = aux.solo = false;
        aux.output = 0;
    }
    sess.auxCount = 2;
    {
        cr::InsertSlot rev;
        rev.builtin = createBuiltinEffect ("Reverb");
        rev.builtin->setParam (3, 1.0f);   // fully wet on a return
        sess.aux[0].inserts.push_back (std::move (rev));
        cr::InsertSlot del;
        del.builtin = createBuiltinEffect ("Delay");
        del.builtin->setParam (3, 1.0f);
        sess.aux[1].inserts.push_back (std::move (del));
    }

    for (auto& s : sess.master.inserts) if (s.plugin) retirePlugin (std::move (s.plugin));
    sess.master.inserts.clear();
    sess.master.name = "Main";
    sess.master.volume = 0.9f;
    sess.master.pan = 0.0f;
    sess.master.mute = false;

    sess.song.clear();
    sess.songAutomation.clear();
    sess.scenes.clear();
    sess.bpm = 120.0;
    sess.beatsPerBar = 4;
    cr::beatsPerBarRef() = 4;
    sess.sceneSyncTicks = cr::barTicks();
    sess.loopOn = false;
    sess.loopStart = 0;
    sess.loopEnd = cr::barTicks();
    sess.quantize = 2;
    sess.swing = 0.0f;
    sess.preRollBars = 1;
    sess.metronome = false;
    sess.metronomeVolume = 0.5f;
    sess.metronomeOnlyWhenRecording = false;
    sess.globalTuneCents = 0.0f;
    for (int m = 0; m < cr::numMacros; ++m)
    {
        sess.macros[(size_t) m].name = "Macro " + juce::String (m + 1);
        sess.macros[(size_t) m].value = 0.0f;
        sess.macros[(size_t) m].bindings.clear();
    }
    sess.midiMappings.clear();
    sess.file = juce::File();

    playing = recording = false;
    inPreRoll = false;
    position = playStartPosition = 0.0;
    uiPosition = 0.0;
    patternOrigin.fill (0.0);
    undoStack.clear();
    redoStack.clear();
    pendingRecords.clear();
    for (auto& v : voices) { v.active = false; v.sample.reset(); v.stretched.reset(); }
    selectedBank = 0;
    selectedPad = 0;
    selectedLayer = 0;
    selectedZone = 0;

    createBank (0);
    prepareAllProcessors();
    rebuildMidiTables();
}

void Engine::newSession()
{
    resetSessionToDefaults();
    notifyStructureChanged();
    sendChangeMessage();
}

void Engine::createBank (int b)
{
    if (! juce::isPositiveAndBelow (b, cr::numBanks)) return;
    const juce::ScopedLock sl (lock);
    auto& bank = sess.banks[(size_t) b];
    if (bank.created) return;
    bank.created = true;
    if (bank.patterns.empty()) { bank.patterns.assign (1, cr::Pattern()); bank.patterns[0].name = "Pattern 1"; }
    for (auto& p : bank.patterns) if (p.length <= 0) p.length = cr::barTicks();
}

void Engine::deleteBank (int b)
{
    checkpoint();
    const juce::ScopedLock sl (lock);
    for (auto& v : voices) if (v.active && v.bank == b) { v.active = false; v.sample.reset(); v.stretched.reset(); }
    initialiseBank (b);
    sess.song.erase (std::remove_if (sess.song.begin(), sess.song.end(), [b] (const cr::SongClip& c) { return c.bank == b; }), sess.song.end());
    for (auto& s : sess.scenes) s.slots[(size_t) b] = -1;
    if (b == 0) createBank (0);
    notifyStructureChanged();
}

int Engine::createAudioTrack()
{
    const juce::ScopedLock sl (lock);
    for (int t = 0; t < cr::maxAudioTracks; ++t)
    {
        auto& tr = sess.audioTracks[(size_t) t];
        if (! tr.created)
        {
            tr.created = true;
            tr.source = cr::TrackSource::song;
            return t;
        }
    }
    return -1;
}

void Engine::deleteAudioTrack (int t)
{
    if (! juce::isPositiveAndBelow (t, cr::maxAudioTracks)) return;
    const juce::ScopedLock sl (lock);
    auto& tr = sess.audioTracks[(size_t) t];
    for (auto& c : tr.clips) retire (c.sample);
    for (auto& s : tr.strip.inserts) if (s.plugin) retirePlugin (std::move (s.plugin));
    resetAudioTrack (tr, t);
}

// ============================================================================
// Scales & chords
// ============================================================================
namespace
{
const std::vector<std::vector<int>>& scaleTable()
{
    static const std::vector<std::vector<int>> scales {
        {}, { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 8, 11 }, { 0, 2, 3, 5, 7, 9, 11 },
        { 0, 2, 4, 7, 9 }, { 0, 3, 5, 7, 10 }, { 0, 2, 3, 5, 7, 9, 10 }, { 0, 1, 3, 5, 7, 8, 10 }, { 0, 2, 4, 6, 7, 9, 11 },
        { 0, 2, 4, 5, 7, 9, 10 }, { 0, 1, 3, 5, 6, 8, 10 }, { 0, 3, 5, 6, 7, 10 }, { 0, 2, 4, 6, 8, 10 }, { 0, 1, 4, 5, 7, 8, 10 } };
    return scales;
}
const std::vector<std::vector<int>>& chordTable()
{
    static const std::vector<std::vector<int>> chords {
        { 0 }, { 0, 4, 7 }, { 0, 3, 7 }, { 0, 4, 7, 10 }, { 0, 4, 7, 11 }, { 0, 3, 7, 10 }, { 0, 3, 6 }, { 0, 4, 8 },
        { 0, 5, 7 }, { 0, 2, 7 }, { 0, 7 }, { 0, 7, 12 }, { 0, 4, 7, 9 }, { 0, 4, 7, 10, 14 }, { 0, 3, 7, 10, 14 }, { 0, 4, 7, 11, 14 } };
    return chords;
}
} // namespace

juce::StringArray Engine::getScaleNames()
{
    return { "Chromatic", "Major", "Natural Minor", "Harmonic Minor", "Melodic Minor", "Major Pentatonic", "Minor Pentatonic",
             "Dorian", "Phrygian", "Lydian", "Mixolydian", "Locrian", "Blues", "Whole Tone", "Phrygian Dominant" };
}

juce::StringArray Engine::getChordNames()
{
    return { "No chord", "Major", "Minor", "Dominant 7", "Major 7", "Minor 7", "Diminished", "Augmented",
             "Sus 4", "Sus 2", "Power (5th)", "Power + octave", "Major 6", "Dominant 9", "Minor 9", "Major 9" };
}

bool Engine::isKeyInScale (int key) const
{
    const auto& sc = scaleTable()[(size_t) juce::jlimit (0, (int) scaleTable().size() - 1, scaleIndex.load())];
    if (sc.empty()) return true;
    const int degree = ((key - scaleRoot.load()) % 12 + 12) % 12;
    return std::find (sc.begin(), sc.end(), degree) != sc.end();
}

std::vector<int> Engine::expandKeys (int key) const
{
    const int sIdx = juce::jlimit (0, (int) scaleTable().size() - 1, scaleIndex.load());
    if (sIdx > 0)
    {
        const auto& sc = scaleTable()[(size_t) sIdx];
        const int degree = ((key - scaleRoot.load()) % 12 + 12) % 12;
        int snapped = 0;
        for (int iv : sc) if (iv <= degree) snapped = iv;
        key -= (degree - snapped);
    }
    std::vector<int> out;
    const int cIdx = juce::jlimit (0, (int) chordTable().size() - 1, chordIndex.load());
    for (int iv : chordTable()[(size_t) cIdx])
        if (key + iv <= 127 && key + iv >= 0) out.push_back (key + iv);
    return out;
}

// ============================================================================
// Transport
// ============================================================================
void Engine::play()
{
    {
        const juce::ScopedLock sl (lock);
        if (playing) return;
        if (sess.loopOn && (position < sess.loopStart || position >= sess.loopEnd)) position = sess.loopStart;
        playStartPosition = position;
        for (int b = 0; b < cr::numBanks; ++b)
        {
            auto& bank = sess.banks[(size_t) b];
            if (bank.queued.pending) applyLaunch (b, position);
            patternOrigin[(size_t) b] = recording ? juce::jmax (position, recordStartTick) : position;
            for (auto& p : bank.patterns) for (auto& n : p.notes) n.skipThisPass = false;
        }
        pendingRecords.clear();
        repeatNextTick = 0.0;
        lastClockTick = -1.0;
        playing = true;
        clockRunning = false;
    }
    sendChangeMessage();
}

void Engine::stop()
{
    bool wasRecordingAudio = false;
    {
        const juce::ScopedLock sl (lock);
        const bool wasPlaying = playing;
        if (recording) commitRecordedPatterns (position, true);
        playing = false;
        recording = false;
        inPreRoll = false;
        preRollLeft = 0.0;
        pendingRecords.clear();
        if (! wasPlaying || behaviours.stopBehaviour == 0)
            position = sess.loopOn ? (double) sess.loopStart : 0.0;
        else
            position = playStartPosition;
        uiPosition = position;
        for (auto& b : sess.banks)
        {
            b.queued = {};
            for (auto& p : b.patterns)
            {
                for (auto& n : p.notes) n.skipThisPass = false;
                for (auto& l : p.automation) l.suspended = false;
            }
        }
        for (auto& l : sess.songAutomation) l.suspended = false;
        wasRecordingAudio = audioRecordingActive;
        if (clockRunning)
            for (auto& o : midiOutputs) if (o.first == behaviours.clockOutDevice && o.second) o.second->sendMessageNow (juce::MidiMessage::midiStop());
        clockRunning = false;
    }
    if (wasRecordingAudio) finishAudioRecording();
    allNotesOff();
    sendChangeMessage();
}

// When a recording ends, what was recorded appears on its own: the pattern is placed in the
// song at the spot it was recorded (as long as it played, in whole loops) and in the scene grid.
// The pattern chip at the bottom still works for dragging copies anywhere.
void Engine::commitRecordedPatterns (double endTick, bool backToSong, bool final)
{
    for (int b = 0; b < cr::numBanks; ++b)
    {
        if (! recordedInto[(size_t) b]) continue;
        if (final) recordedInto[(size_t) b] = false;
        auto& bank = sess.banks[(size_t) b];
        const int pi = bank.workingPattern;
        if (! juce::isPositiveAndBelow (pi, (int) bank.patterns.size()) || bank.patterns[(size_t) pi].notes.empty()) continue;
        const int L = juce::jmax (1, (int) bank.patterns[(size_t) pi].lengthTicks());
        const int start = juce::jmax (0, (int) std::round (patternOrigin[(size_t) b]));
        const double played = juce::jmax (0.0, endTick - start);
        const int loops = juce::jmax (1, (int) std::ceil (played / L - 0.02));
        const int end = start + loops * L;

        // song: make room on this bank's track, then add the clip
        auto& song = sess.song;
        bool covered = false;
        for (auto& c : song)
            if (c.bank == b && c.pattern == pi && c.start <= start && c.start + c.length >= end) covered = true;
        if (! covered)
        {
            std::vector<cr::SongClip> keep;
            for (auto c : song)
            {
                if (c.bank != b || c.start + c.length <= start || c.start >= end) { keep.push_back (c); continue; }
                if (c.start < start) { auto head = c; head.length = start - c.start; keep.push_back (head); }
                if (c.start + c.length > end) { auto tail = c; tail.offset += end - c.start; tail.length = c.start + c.length - end; tail.start = end; keep.push_back (tail); }
            }
            cr::SongClip clip;
            clip.bank = b; clip.pattern = pi; clip.start = start; clip.length = end - start;
            keep.push_back (clip);
            song = std::move (keep);
        }

        // scenes: the scene last launched gets it, unless that slot already holds another pattern
        if (sess.scenes.empty()) { cr::Scene sc; sc.name = "Scene 1"; sess.scenes.push_back (sc); }
        int target = juce::jlimit (0, (int) sess.scenes.size() - 1, lastLaunchedScene);
        auto& slot = sess.scenes[(size_t) target].slots[(size_t) b];
        if (slot >= 0 && slot != pi)
        {
            target = -1;
            for (int i = 0; i < (int) sess.scenes.size(); ++i)
                if (sess.scenes[(size_t) i].slots[(size_t) b] == pi || sess.scenes[(size_t) i].slots[(size_t) b] < 0) { target = i; break; }
            if (target < 0 && sess.scenes.size() < 64)
            {
                cr::Scene sc;
                sc.name = "Scene " + juce::String ((int) sess.scenes.size() + 1);
                sess.scenes.push_back (sc);
                target = (int) sess.scenes.size() - 1;
            }
        }
        if (target >= 0) sess.scenes[(size_t) target].slots[(size_t) b] = pi;

        // after Stop the bank follows the song again, which now has the recording in it
        // (turning Record off while playing keeps the loop going)
        if (final && backToSong) bank.source = cr::TrackSource::song;
    }
}

void Engine::setRecording (bool shouldRecord)
{
    if (shouldRecord)
    {
        checkpoint();
        bool startTransport = false;
        {
            const juce::ScopedLock sl (lock);
            const int b = selectedBank;
            createBank (b);
            auto& bank = sess.banks[(size_t) b];
            recordStartTick = position;
            recordedInto.fill (false);
            // the working track takes over: it loops its working pattern while recording,
            // unless audio tracks are armed (they record along the song)
            bool anyAudioArmed = false;
            for (auto& t : sess.audioTracks) if (t.created && t.armed) anyAudioArmed = true;
            if (! anyAudioArmed && bank.source != cr::TrackSource::pattern)
            {
                bank.source = cr::TrackSource::pattern;
                bank.playingPattern = bank.workingPattern;
                patternOrigin[(size_t) b] = position;
            }
            else if (! anyAudioArmed && bank.playingPattern != bank.workingPattern)
            {
                bank.playingPattern = bank.workingPattern;
                patternOrigin[(size_t) b] = position;
            }
            recording = true;
            if (! playing)
            {
                startTransport = true;
                if (sess.preRollBars > 0)
                {
                    preRollLeft = (double) sess.preRollBars * cr::barTicks();
                    inPreRoll = true;
                }
            }
            if (anyAudioArmed)
            {
                audioRecordStartTick = position;
                audioRecordingActive = true;
                for (int t = 0; t < cr::maxAudioTracks; ++t)
                {
                    auto& tr = sess.audioTracks[(size_t) t];
                    trackWriters[(size_t) t].reset();
                    if (! tr.created || ! tr.armed) continue;
                    auto f = getRecordingsFolder().getNonexistentChildFile (tr.name.replaceCharacters (" /\\:", "____")
                                                                             + "_" + juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S"), ".wav");
                    getRecordingsFolder().createDirectory();
                    if (auto w = createWavWriter (f, sampleRate, 2, behaviours.recordBitDepth))
                    {
                        trackWriters[(size_t) t] = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (w.release(), diskWriterThread, 65536);
                        trackRecordFiles[(size_t) t] = f;
                        trackRecordedFrames[(size_t) t] = 0;
                    }
                }
            }
        }
        if (startTransport) play();
    }
    else
    {
        bool finishAudio = false;
        {
            const juce::ScopedLock sl (lock);
            if (recording) commitRecordedPatterns (position, false);
            recording = false;
            inPreRoll = false;
            preRollLeft = 0.0;
            pendingRecords.clear();
            finishAudio = audioRecordingActive;
        }
        if (finishAudio) finishAudioRecording();
    }
    sendChangeMessage();
}

double Engine::getTrackPatternPosition (int b) const
{
    const auto& bank = sess.banks[(size_t) b];
    const double tick = uiPosition.load();
    if (bank.source == cr::TrackSource::pattern)
    {
        if (! juce::isPositiveAndBelow (bank.playingPattern, (int) bank.patterns.size())) return -1.0;
        const double len = bank.patterns[(size_t) bank.playingPattern].lengthTicks();
        const double rel = tick - patternOrigin[(size_t) b];
        if (! sess.loopOn && rel >= len) return -1.0;   // loop off: the pattern has played through
        return len > 0 ? std::fmod (juce::jmax (0.0, rel), len) : 0.0;
    }
    if (bank.source == cr::TrackSource::song)
    {
        for (auto& c : sess.song)
            if (c.bank == b && tick >= c.start && tick < c.start + c.length && juce::isPositiveAndBelow (c.pattern, (int) bank.patterns.size()))
            {
                const double len = bank.patterns[(size_t) c.pattern].lengthTicks();
                return len > 0 ? std::fmod (tick - c.start + c.offset, len) : 0.0;
            }
    }
    return -1.0;
}

void Engine::setPosition (double ticks)
{
    {
        const juce::ScopedLock sl (lock);
        position = juce::jmax (0.0, ticks);
        playStartPosition = position;
        uiPosition = position;
        for (int b = 0; b < cr::numBanks; ++b) patternOrigin[(size_t) b] = position;
    }
    if (playing) allNotesOff();
}

void Engine::rewind()
{
    const double bt = cr::barTicks();
    const double p = std::floor ((uiPosition.load() - 1.0) / bt) * bt;
    setPosition (juce::jmax (0.0, p));
}

void Engine::fastForward()
{
    const double bt = cr::barTicks();
    setPosition ((std::floor (uiPosition.load() / bt) + 1.0) * bt);
}

void Engine::setBpm (double bpm)
{
    {
        const juce::ScopedLock sl (lock);
        sess.bpm = juce::jlimit (20.0, 400.0, bpm);
        for (auto& b : sess.banks) for (auto& p : b.pads) for (auto& l : p.channel.inserts) if (l.builtin) l.builtin->setTempo (sess.bpm);
    }
    requestStretches();
}

void Engine::setBeatsPerBar (int beats)
{
    const juce::ScopedLock sl (lock);
    sess.beatsPerBar = juce::jlimit (1, 16, beats);
    cr::beatsPerBarRef() = sess.beatsPerBar;
}

void Engine::tapTempo()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (! tapTimes.empty() && now - tapTimes.back() > 2000.0) tapTimes.clear();
    tapTimes.push_back (now);
    if (tapTimes.size() > 6) tapTimes.erase (tapTimes.begin());
    if (tapTimes.size() >= 2)
    {
        const double avg = (tapTimes.back() - tapTimes.front()) / (double) (tapTimes.size() - 1);
        setBpm (std::round (60000.0 / avg * 100.0) / 100.0);
    }
}

// ============================================================================
// Performance input (message thread -> audio thread)
// ============================================================================
void Engine::padDown (int bank, int pad, float velocity, int key)
{
    if (! juce::isPositiveAndBelow (bank, cr::numBanks) || ! juce::isPositiveAndBelow (pad, cr::numPads)) return;
    const juce::SpinLock::ScopedLockType sl (liveLock);
    if (liveQueue.size() < 1000) liveQueue.push_back ({ bank, pad, key, juce::jlimit (0.01f, 1.0f, velocity), true });
}

void Engine::padUp (int bank, int pad, int key)
{
    if (! juce::isPositiveAndBelow (bank, cr::numBanks) || ! juce::isPositiveAndBelow (pad, cr::numPads)) return;
    const juce::SpinLock::ScopedLockType sl (liveLock);
    if (liveQueue.size() < 1000) liveQueue.push_back ({ bank, pad, key, 0.0f, false });
}

void Engine::allNotesOff()
{
    const juce::ScopedLock sl (lock);
    for (auto& v : voices)
        if (v.active && v.fadeOut < 0) v.fadeOut = 256;
    for (auto& row : heldVelocity) for (auto& h : row) h = 0.0f;
    for (auto& row : heldKey) for (auto& k : row) k = -1;
    onReleaseNotes.clear();
    sustainedNotes.clear();
    for (auto& m : instrumentMidi)
        for (int ch = 1; ch <= 16; ++ch) m.addEvent (juce::MidiMessage::allNotesOff (ch), 0);
}

void Engine::midiPanic()
{
    allNotesOff();
    for (auto& o : midiOutputs)
        if (o.second)
            for (int ch = 1; ch <= 16; ++ch)
            {
                o.second->sendMessageNow (juce::MidiMessage::allNotesOff (ch));
                o.second->sendMessageNow (juce::MidiMessage::allControllersOff (ch));
            }
}

// ============================================================================
// Tracks, patterns, scenes
// ============================================================================
void Engine::playSong (int bank)
{
    const juce::ScopedLock sl (lock);
    for (int b = 0; b < cr::numBanks; ++b)
    {
        if (bank >= 0 && b != bank) continue;
        auto& bk = sess.banks[(size_t) b];
        if (playing) { bk.queued.pending = true; bk.queued.source = cr::TrackSource::song; }
        else { bk.source = cr::TrackSource::song; bk.queued = {}; }
    }
    if (bank < 0)
        for (auto& t : sess.audioTracks) t.source = cr::TrackSource::song;
}

void Engine::launchPattern (int bank, int pattern)
{
    if (! juce::isPositiveAndBelow (bank, cr::numBanks)) return;
    bool start = false;
    {
        const juce::ScopedLock sl (lock);
        auto& b = sess.banks[(size_t) bank];
        if (! juce::isPositiveAndBelow (pattern, (int) b.patterns.size())) return;
        b.workingPattern = pattern;
        if (playing)
        {
            b.queued.pending = true;
            b.queued.source = cr::TrackSource::pattern;
            b.queued.pattern = pattern;
        }
        else
        {
            b.source = cr::TrackSource::pattern;
            b.playingPattern = pattern;
            b.queued = {};
            patternOrigin[(size_t) bank] = position;
        }
    }
    juce::ignoreUnused (start);
}

void Engine::stopTrack (int bank)
{
    const juce::ScopedLock sl (lock);
    auto& b = sess.banks[(size_t) bank];
    if (playing) { b.queued.pending = true; b.queued.source = cr::TrackSource::stopped; }
    else { b.source = cr::TrackSource::stopped; b.queued = {}; }
}

void Engine::stopAllScenes()
{
    const juce::ScopedLock sl (lock);
    for (int b = 0; b < cr::numBanks; ++b)
    {
        auto& bk = sess.banks[(size_t) b];
        if (bk.source == cr::TrackSource::pattern || (bk.queued.pending && bk.queued.source == cr::TrackSource::pattern))
        {
            if (playing) { bk.queued.pending = true; bk.queued.source = cr::TrackSource::stopped; }
            else { bk.source = cr::TrackSource::stopped; bk.queued = {}; }
        }
    }
}

void Engine::launchScene (int scene)
{
    bool startTransport = false;
    {
        const juce::ScopedLock sl (lock);
        if (! juce::isPositiveAndBelow (scene, (int) sess.scenes.size())) return;
        lastLaunchedScene = scene;
        const auto& sc = sess.scenes[(size_t) scene];
        for (int b = 0; b < cr::numBanks; ++b)
        {
            auto& bk = sess.banks[(size_t) b];
            if (! bk.created) continue;
            const int slot = sc.slots[(size_t) b];
            cr::LaunchRequest req;
            req.pending = true;
            if (slot >= 0 && slot < (int) bk.patterns.size()) { req.source = cr::TrackSource::pattern; req.pattern = slot; bk.workingPattern = slot; }
            else if (slot == -2) req.source = cr::TrackSource::song;
            else req.source = cr::TrackSource::stopped;
            bk.queued = req;
            if (! playing) applyLaunch (b, position);
        }
        startTransport = ! playing;
    }
    if (startTransport) play();
}

void Engine::captureScene (int scene)
{
    checkpoint();
    const juce::ScopedLock sl (lock);
    while ((int) sess.scenes.size() <= scene)
    {
        cr::Scene s;
        s.name = "Scene " + juce::String (sess.scenes.size() + 1);
        sess.scenes.push_back (s);
    }
    auto& sc = sess.scenes[(size_t) scene];
    for (int b = 0; b < cr::numBanks; ++b)
    {
        const auto& bk = sess.banks[(size_t) b];
        if (! bk.created) { sc.slots[(size_t) b] = -1; continue; }
        if (bk.source == cr::TrackSource::pattern) sc.slots[(size_t) b] = bk.playingPattern;
        else if (bk.source == cr::TrackSource::song) sc.slots[(size_t) b] = -2;
        else sc.slots[(size_t) b] = -1;
    }
}

void Engine::applyLaunch (int b, double atTick)
{
    auto& bk = sess.banks[(size_t) b];
    if (! bk.queued.pending) return;
    const auto req = bk.queued;
    bk.queued = {};
    if (bk.source != req.source || bk.playingPattern != req.pattern || req.source != cr::TrackSource::pattern)
        for (auto& v : voices)
            if (v.active && v.bank == b && v.fadeOut < 0 && v.loop != cr::LoopMode::off) v.fadeOut = 128;
    bk.source = req.source;
    if (req.source == cr::TrackSource::pattern)
    {
        bk.playingPattern = juce::jlimit (0, (int) bk.patterns.size() - 1, req.pattern);
        patternOrigin[(size_t) b] = atTick;
        for (auto& n : bk.patterns[(size_t) bk.playingPattern].notes) n.skipThisPass = false;
    }
}

int Engine::addPattern (int bank, const juce::String& name, int lengthTicks)
{
    checkpoint();
    const juce::ScopedLock sl (lock);
    createBank (bank);
    auto& b = sess.banks[(size_t) bank];
    cr::Pattern p;
    p.name = name.isNotEmpty() ? name : "Pattern " + juce::String (b.patterns.size() + 1);
    p.length = lengthTicks > 0 ? lengthTicks : cr::barTicks();
    b.patterns.push_back (std::move (p));
    b.workingPattern = (int) b.patterns.size() - 1;
    return b.workingPattern;
}

int Engine::duplicatePattern (int bank, int pattern)
{
    checkpoint();
    const juce::ScopedLock sl (lock);
    auto& b = sess.banks[(size_t) bank];
    if (! juce::isPositiveAndBelow (pattern, (int) b.patterns.size())) return -1;
    auto copy = b.patterns[(size_t) pattern];
    copy.name = copy.name + " copy";
    b.patterns.push_back (std::move (copy));
    return (int) b.patterns.size() - 1;
}

void Engine::copyPattern (int bank, int pattern)
{
    const juce::ScopedLock sl (lock);
    auto& b = sess.banks[(size_t) bank];
    if (! juce::isPositiveAndBelow (pattern, (int) b.patterns.size())) return;
    patternClipboard = { b.patterns[(size_t) pattern] };
}

int Engine::pastePattern (int bank)
{
    if (patternClipboard.empty() || ! juce::isPositiveAndBelow (bank, cr::numBanks)) return -1;
    checkpoint();
    createBank (bank);
    const juce::ScopedLock sl (lock);
    auto& b = sess.banks[(size_t) bank];
    auto copy = patternClipboard.front();
    copy.name = copy.name + " copy";
    b.patterns.push_back (std::move (copy));
    b.workingPattern = (int) b.patterns.size() - 1;
    sendChangeMessage();
    return b.workingPattern;
}

int Engine::copySongClips()
{
    const juce::ScopedLock sl (lock);
    songClipboard.clear();
    songAudioClipboard.clear();
    for (auto& c : sess.song)
        if (c.selected && juce::isPositiveAndBelow (c.pattern, (int) sess.banks[(size_t) c.bank].patterns.size()))
            songClipboard.push_back ({ c, sess.banks[(size_t) c.bank].patterns[(size_t) c.pattern] });
    for (int t = 0; t < cr::maxAudioTracks; ++t)
        for (auto& c : sess.audioTracks[(size_t) t].clips)
            if (c.selected && c.sample) songAudioClipboard.push_back ({ t, c });
    return (int) (songClipboard.size() + songAudioClipboard.size());
}

int Engine::pasteSongClips (int atTick, int targetBank)
{
    if (songClipboard.empty() && songAudioClipboard.empty()) return 0;
    checkpoint();
    int first = std::numeric_limits<int>::max();
    for (auto& cc : songClipboard) first = juce::jmin (first, cc.clip.start);
    for (auto& ac : songAudioClipboard) first = juce::jmin (first, ac.clip.start);
    if (targetBank >= 0) createBank (targetBank);
    const juce::ScopedLock sl (lock);
    for (auto& c : sess.song) c.selected = false;
    // each distinct source pattern is copied once, so clips that shared a pattern still share the copy
    std::map<std::pair<int, int>, int> made;
    int n = 0;
    for (auto& cc : songClipboard)
    {
        const int bank = targetBank >= 0 ? targetBank : cc.clip.bank;
        if (! sess.banks[(size_t) bank].created) continue;
        auto& b = sess.banks[(size_t) bank];
        const auto keyP = std::make_pair (cc.clip.bank, cc.clip.pattern);
        int pi;
        if (auto it = made.find (keyP); it != made.end()) pi = it->second;
        else
        {
            auto p = cc.pattern;
            p.name = p.name + " copy";
            b.patterns.push_back (std::move (p));
            pi = (int) b.patterns.size() - 1;
            made[keyP] = pi;
        }
        auto clip = cc.clip;
        clip.bank = bank;
        clip.pattern = pi;
        clip.start = atTick + (cc.clip.start - first);
        clip.selected = true;
        sess.song.push_back (clip);
        ++n;
    }
    for (auto& t : sess.audioTracks) for (auto& c : t.clips) c.selected = false;
    for (auto& ac : songAudioClipboard)
    {
        auto& tr = sess.audioTracks[(size_t) ac.track];
        if (! tr.created) continue;
        auto clip = ac.clip;
        clip.start = atTick + (ac.clip.start - first);
        clip.selected = true;
        tr.clips.push_back (clip);
        ++n;
    }
    sendChangeMessage();
    return n;
}

void Engine::deletePattern (int bank, int pattern)
{
    checkpoint();
    const juce::ScopedLock sl (lock);
    auto& b = sess.banks[(size_t) bank];
    if (! juce::isPositiveAndBelow (pattern, (int) b.patterns.size())) return;
    b.patterns.erase (b.patterns.begin() + pattern);
    if (b.patterns.empty()) { b.patterns.push_back ({}); b.patterns[0].name = "Pattern 1"; b.patterns[0].length = cr::barTicks(); }
    sess.song.erase (std::remove_if (sess.song.begin(), sess.song.end(), [&] (const cr::SongClip& c) { return c.bank == bank && c.pattern == pattern; }), sess.song.end());
    for (auto& c : sess.song) if (c.bank == bank && c.pattern > pattern) --c.pattern;
    for (auto& s : sess.scenes)
    {
        auto& slot = s.slots[(size_t) bank];
        if (slot == pattern) slot = -1;
        else if (slot > pattern) --slot;
    }
    auto fix = [&] (int& idx) { if (idx == pattern) idx = 0; else if (idx > pattern) --idx; idx = juce::jlimit (0, (int) b.patterns.size() - 1, idx); };
    fix (b.workingPattern);
    fix (b.playingPattern);
}

int Engine::findTrackPatternOnSong (int bank) const
{
    for (auto& c : sess.song) if (c.bank == bank) return c.pattern;
    return -1;
}

// ============================================================================
// Audio device
// ============================================================================
void Engine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    // The callback only gets the input channels that are switched on, packed together.
    // Remember which hardware channel each one is, so input choices mean the real channel.
    for (int i = 0; i < (int) activeInputToHardware.size(); ++i) activeInputToHardware[(size_t) i] = 99;
    const auto active = device->getActiveInputChannels();
    int k = 0;
    for (int ch = 0; ch < 32 && k < (int) activeInputToHardware.size(); ++ch)
        if (active[ch]) activeInputToHardware[(size_t) k++] = ch;
    prepare (device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
}

void Engine::audioDeviceStopped() {}

void Engine::prepare (double sr, int blockSize)
{
    const juce::ScopedLock sl (lock);
    sampleRate = sr > 0 ? sr : 44100.0;
    maxBlock = juce::jlimit (32, 4096, blockSize);
    const int bs = maxBlock;
    for (auto& b : bankBuffers) b.setSize (2, bs, false, false, true);
    for (auto& a : auxBuffers) a.setSize (2, bs, false, false, true);
    for (auto& t : trackBuffers) t.setSize (2, bs, false, false, true);
    masterBuffer.setSize (2, bs, false, false, true);
    scratchBuffer.setSize (2, bs, false, false, true);
    layerScratch.setSize (2, bs, false, false, true);
    pluginBuffer.setSize (32, bs, false, false, true);
    for (auto& b : padBuses) b.buffer.setSize (2, bs, false, false, true);
    for (auto& b : layerBuses) b.buffer.setSize (2, bs, false, false, true);
    if (padBuses.size() < 64) padBuses.resize (64);
    if (layerBuses.size() < 32) layerBuses.resize (32);
    for (auto& b : padBuses) b.buffer.setSize (2, bs, false, false, true);
    for (auto& b : layerBuses) b.buffer.setSize (2, bs, false, false, true);
    hwOutBuffers.resize (8);
    for (auto& b : hwOutBuffers) b.setSize (2, bs, false, false, true);
    for (auto& d : delayComps) d.second.buffer.setSize (2, juce::jmax (1, d.second.delay + bs), false, true, true);
    samplingBuffer.setSize (2, (int) (sampleRate * 2.0), false, true, true);
    prepareAllProcessors();
}

void Engine::prepareChain (std::vector<cr::InsertSlot>& chain)
{
    for (auto& s : chain)
    {
        if (s.builtin) { s.builtin->prepare (sampleRate, maxBlock); s.builtin->setTempo (sess.bpm); }
        if (s.plugin)
        {
            s.plugin->setRateAndBufferSizeDetails (sampleRate, maxBlock);
            s.plugin->prepareToPlay (sampleRate, maxBlock);
        }
    }
}

void Engine::prepareAllProcessors()
{
    for (auto& b : sess.banks)
    {
        if (! b.created) continue;
        prepareChain (b.strip.inserts);
        for (auto& p : b.pads)
        {
            prepareChain (p.channel.inserts);
            for (auto& l : p.layers)
                for (auto& f : l.fx)
                {
                    if (f.builtin) { f.builtin->prepare (sampleRate, maxBlock); f.builtin->setTempo (sess.bpm); }
                    if (f.plugin) { f.plugin->setRateAndBufferSizeDetails (sampleRate, maxBlock); f.plugin->prepareToPlay (sampleRate, maxBlock); }
                }
            if (p.instrument) { p.instrument->setRateAndBufferSizeDetails (sampleRate, maxBlock); p.instrument->prepareToPlay (sampleRate, maxBlock); }
        }
    }
    for (auto& t : sess.audioTracks) prepareChain (t.strip.inserts);
    for (auto& a : sess.aux) prepareChain (a.inserts);
    prepareChain (sess.master.inserts);
    notifyStructureChanged();
    updateLatencyCompensation();
}

void Engine::notifyStructureChanged()
{
    const juce::ScopedLock sl (lock);
    instrumentPads.clear();
    for (int b = 0; b < cr::numBanks; ++b)
    {
        if (! sess.banks[(size_t) b].created) continue;
        for (int p = 0; p < cr::numPads; ++p)
            if (sess.banks[(size_t) b].pads[(size_t) p].instrument) instrumentPads.push_back (padKey (b, p));
    }
    if (instrumentMidi.size() < instrumentPads.size()) instrumentMidi.resize (instrumentPads.size());
    for (auto& m : instrumentMidi) m.ensureSize (2048);
}

void Engine::updateLatencyCompensation()
{
    const juce::ScopedLock sl (lock);
    auto chainLatency = [] (std::vector<cr::InsertSlot>& c)
    {
        int l = 0;
        for (auto& s : c) if (s.plugin && ! s.bypass) l += s.plugin->getLatencySamples();
        return l;
    };
    maxStripLatency = 0;
    for (int b = 0; b < cr::numBanks; ++b)
    {
        auto& bank = sess.banks[(size_t) b];
        if (! bank.created) continue;
        int padMax = 0;
        for (auto& p : bank.pads)
        {
            int l = chainLatency (p.channel.inserts);
            if (p.instrument) l += p.instrument->getLatencySamples();
            padMax = juce::jmax (padMax, l);
        }
        bank.strip.latency = chainLatency (bank.strip.inserts) + padMax;
        maxStripLatency = juce::jmax (maxStripLatency, bank.strip.latency);
    }
    for (auto& t : sess.audioTracks)
        if (t.created) { t.strip.latency = chainLatency (t.strip.inserts); maxStripLatency = juce::jmax (maxStripLatency, t.strip.latency); }

    delayComps.clear();
    auto setup = [this] (int key, int latency)
    {
        const int d = maxStripLatency - latency;
        if (d <= 0) return;
        auto& dc = delayComps[key];
        dc.delay = d;
        dc.writePos = 0;
        dc.buffer.setSize (2, d + maxBlock);
        dc.buffer.clear();
    };
    if (maxStripLatency > 0)
    {
        for (int b = 0; b < cr::numBanks; ++b) if (sess.banks[(size_t) b].created) setup (b, sess.banks[(size_t) b].strip.latency);
        for (int t = 0; t < cr::maxAudioTracks; ++t) if (sess.audioTracks[(size_t) t].created) setup (1000 + t, sess.audioTracks[(size_t) t].strip.latency);
    }
}

void Engine::audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                               int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    renderBlock (in, numIn, out, numOut, numSamples);
}

void Engine::renderOffline (juce::AudioBuffer<float>& out, int numSamples)
{
    out.setSize (2, numSamples, false, false, true);
    out.clear();
    renderBlock (nullptr, 0, out.getArrayOfWritePointers(), 2, numSamples);
}

void Engine::renderBlock (const float* const* in, int numIn, float* const* out, int numOut, int numSamples)
{
    const juce::ScopedTryLock sl (lock);
    if (! sl.isLocked())
    {
        for (int c = 0; c < numOut; ++c) if (out[c] != nullptr) juce::FloatVectorOperations::clear (out[c], numSamples);
        return;
    }
    int done = 0;
    while (done < numSamples)
    {
        const int n = juce::jmin (maxBlock, numSamples - done);
        const float* inPtrs[32] {};
        float* outPtrs[32] {};
        const int no = juce::jmin (32, numOut);
        int ni = 0;   // inputs are re-spread to their hardware channel numbers
        for (int c = 0; c < juce::jmin (32, numIn); ++c)
        {
            const int hw = activeInputToHardware[(size_t) c];
            if (hw < 32 && in[c] != nullptr) { inPtrs[hw] = in[c] + done; ni = juce::jmax (ni, hw + 1); }
        }
        for (int c = 0; c < no; ++c) outPtrs[c] = out[c] != nullptr ? out[c] + done : nullptr;
        renderChunk (inPtrs, ni, outPtrs, no, n);
        done += n;
    }
}

juce::AudioBuffer<float>* Engine::getBus (std::vector<Bus>& pool, int& used, int key, int n)
{
    for (int i = 0; i < used; ++i)
        if (pool[(size_t) i].key == key) return &pool[(size_t) i].buffer;
    if (used >= (int) pool.size()) return nullptr;   // out of buses (pre-allocated): drop
    auto& b = pool[(size_t) used++];
    b.key = key;
    b.buffer.clear (0, n);
    return &b.buffer;
}

void Engine::addEvent (const Event& e)
{
    if (events.size() < events.capacity()) events.push_back (e);
}

// ============================================================================
// One chunk (<= maxBlock samples)
// ============================================================================
void Engine::renderChunk (const float* const* in, int numIn, float* const* out, int numOut, int n)
{
    curBlock = n;
    curTps = sess.bpm * cr::ppq / (60.0 * sampleRate);
    events.clear();
    padBusesUsed = layerBusesUsed = 0;
    for (auto& m : midiOutBuffers) m.second.clear();

    // --- timeline segments (loop wraps) ---------------------------------------------------------
    numSegs = 0;
    const bool counting = inPreRoll.load();
    if (playing && ! counting)
    {
        double tick = position;
        int offset = 0;
        while (offset < n && numSegs < (int) segs.size())
        {
            int count = n - offset;
            if (sess.loopOn && sess.loopEnd > sess.loopStart && tick < sess.loopEnd)
            {
                const double until = (sess.loopEnd - tick) / curTps;
                if (until < count) count = juce::jmax (1, (int) std::ceil (until));
            }
            segs[(size_t) numSegs++] = { offset, count, tick };
            tick += count * curTps;
            offset += count;
            if (sess.loopOn && sess.loopEnd > sess.loopStart && tick >= sess.loopEnd - 1.0e-6)
            {
                const double over = tick - sess.loopEnd;
                tick = sess.loopStart + over;
                if (behaviours.automationReenableOnLoop)
                    for (auto& l : sess.songAutomation) l.suspended = false;
            }
        }
    }

    // --- live input -----------------------------------------------------------------------------------
    {
        const juce::SpinLock::ScopedLockType sl (liveLock);
        liveScratch.swap (liveQueue);
        liveQueue.clear();
    }
    for (auto& le : liveScratch) handleLiveEvent (le, 0, numSegs > 0 ? segs[0].tick : position);
    liveScratch.clear();

    routeMidiInput (n);
    if (playing && ! counting) collectSequencerEvents (n);

    // note repeat (works with the transport stopped too, on a free-running grid)
    if (repeatSlot.load() >= 0)
    {
        const int slot = juce::jlimit (0, cr::numRepeatSlots - 1, repeatSlot.load());
        const double step = juce::jmax (1, repeatTicks[(size_t) slot]);
        const float swing = repeatSwing[(size_t) slot];
        const double startTick = (playing && ! counting) ? position : repeatNextTick;
        const double endTick = startTick + n * curTps;
        bool anyHeld = false;
        for (int b = 0; b < cr::numBanks && ! anyHeld; ++b)
            for (int p = 0; p < cr::numPads; ++p) if (heldVelocity[b][p] > 0.0f) { anyHeld = true; break; }
        if (anyHeld)
        {
            double k = std::ceil (startTick / step);
            for (;; k += 1.0)
            {
                double t = k * step;
                if (((long long) k) % 2 == 1) t += swing * step;
                if (t >= endTick) break;
                if (t < startTick) continue;
                const int off = juce::jlimit (0, n - 1, (int) ((t - startTick) / curTps));
                for (int b = 0; b < cr::numBanks; ++b)
                {
                    if (! sess.banks[(size_t) b].created) continue;
                    for (int p = 0; p < cr::numPads; ++p)
                    {
                        const float v = heldVelocity[b][p];
                        if (v <= 0.0f) continue;
                        Event on; on.offset = off; on.bank = b; on.pad = p; on.key = heldKey[b][p]; on.velocity = v; on.on = true;
                        addEvent (on);
                        Event offE = on; offE.on = false; offE.offset = juce::jmin (n - 1, off + juce::jmax (1, (int) (step * 0.5 / curTps)));
                        if (offE.offset > off) addEvent (offE);
                        if (recording && playing && ! counting) handleLiveEvent ({ b, p, heldKey[b][p], v, true }, off, t), handleLiveEvent ({ b, p, heldKey[b][p], 0.0f, false }, off, t + step * 0.5);
                    }
                }
            }
        }
        repeatNextTick = (playing && ! counting) ? position : endTick;
    }
    else repeatNextTick += n * curTps;

    // --- render voices and events, sample accurate --------------------------------------------
    std::stable_sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.offset < b.offset; });
    int cursor = 0;
    for (auto& e : events)
    {
        const int at = juce::jlimit (0, n, e.offset);
        if (at > cursor) { renderVoices (cursor, at); cursor = at; }
        applyEvent (e);
    }
    if (cursor < n) renderVoices (cursor, n);

    // --- layer FX into pad buses ----------------------------------------------------------------
    for (int i = 0; i < layerBusesUsed; ++i)
    {
        auto& lb = layerBuses[(size_t) i];
        const int b = lb.key / (cr::numPads * 8), p = (lb.key / 8) % cr::numPads, l = lb.key % 8;
        auto& pad = sess.banks[(size_t) b].pads[(size_t) p];
        if (juce::isPositiveAndBelow (l, (int) pad.layers.size()))
            for (auto& f : pad.layers[(size_t) l].fx) processSlot (f, lb.buffer, n);
        if (auto* pb = getBus (padBuses, padBusesUsed, padKey (b, p), n))
            for (int c = 0; c < 2; ++c) pb->addFrom (c, 0, lb.buffer, c, 0, n);
    }

    // --- plugin instruments ----------------------------------------------------------------------
    for (size_t i = 0; i < instrumentPads.size(); ++i)
    {
        const int key = instrumentPads[i];
        auto& pad = sess.banks[(size_t) (key / cr::numPads)].pads[(size_t) (key % cr::numPads)];
        if (! pad.instrument) continue;
        if (pad.channel.frozen || sess.banks[(size_t) (key / cr::numPads)].strip.frozen) continue;
        auto& inst = *pad.instrument;
        const int chans = juce::jmax (2, juce::jmax (inst.getTotalNumInputChannels(), inst.getTotalNumOutputChannels()));
        if (chans > pluginBuffer.getNumChannels()) continue;
        juce::AudioBuffer<float> pb (pluginBuffer.getArrayOfWritePointers(), chans, n);
        pb.clear();
        inst.processBlock (pb, instrumentMidi[i]);
        instrumentMidi[i].clear();
        const int outs = juce::jmax (1, inst.getTotalNumOutputChannels());
        if (auto* bus = getBus (padBuses, padBusesUsed, key, n))
            for (int c = 0; c < 2; ++c) bus->addFrom (c, 0, pb, juce::jmin (c, outs - 1), 0, n);
    }

    // --- mixer -----------------------------------------------------------------------------------------
    masterBuffer.clear (0, n);
    for (int a = 0; a < cr::numAux; ++a) auxBuffers[(size_t) a].clear (0, n);
    for (auto& h : hwOutBuffers) h.clear (0, n);

    bool anySolo = false;
    for (auto& b : sess.banks) if (b.created && b.strip.solo) anySolo = true;
    for (auto& t : sess.audioTracks) if (t.created && t.strip.solo) anySolo = true;
    for (int a = 0; a < sess.auxCount; ++a) if (sess.aux[(size_t) a].solo) anySolo = true;

    std::array<bool, cr::numBanks> bankHasPadSolo {};
    for (int i = 0; i < padBusesUsed; ++i)
    {
        const int b = padBuses[(size_t) i].key / cr::numPads;
        juce::ignoreUnused (b);
    }
    for (int b = 0; b < cr::numBanks; ++b)
    {
        auto& bank = sess.banks[(size_t) b];
        if (! bank.created) continue;
        for (auto& p : bank.pads) if (p.channel.solo) { bankHasPadSolo[(size_t) b] = true; break; }
    }

    for (int b = 0; b < cr::numBanks; ++b) { if (bankActive[(size_t) b]) bankBuffers[(size_t) b].clear (0, n); bankActive[(size_t) b] = false; }

    // pads -> bank buffers
    for (int i = 0; i < padBusesUsed; ++i)
    {
        auto& bus = padBuses[(size_t) i];
        const int b = bus.key / cr::numPads, p = bus.key % cr::numPads;
        auto& pad = sess.banks[(size_t) b].pads[(size_t) p];
        auto& ch = pad.channel;
        if (ch.frozen) { ch.meterL = ch.meterL * 0.9f; ch.meterR = ch.meterR * 0.9f; continue; }
        const bool audible = ! ch.mute && (! bankHasPadSolo[(size_t) b] || ch.solo)
                             && (exportSoloKey < 0 || exportSoloKey == b || exportSoloKey == 10000 + bus.key);
        processChannel (ch, bus.buffer, n);
        if (! bankActive[(size_t) b]) { bankBuffers[(size_t) b].clear (0, n); bankActive[(size_t) b] = true; }
        mixStripTo (ch, bus.buffer, n, audible, -1, bankBuffers[(size_t) b]);
    }
    // pads with inserts keep their tails alive
    for (int b = 0; b < cr::numBanks; ++b)
    {
        auto& bank = sess.banks[(size_t) b];
        if (! bank.created) continue;
        if (bank.strip.frozen) { bank.strip.meterL = bank.strip.meterL * 0.9f; bank.strip.meterR = bank.strip.meterR * 0.9f; bankActive[(size_t) b] = false; continue; }
        if (bankActive[(size_t) b]) bankTail[(size_t) b] = (int) (sampleRate * 4.0);
        const bool hasFx = ! bank.strip.inserts.empty();
        if (! bankActive[(size_t) b])
        {
            if (! hasFx || bankTail[(size_t) b] <= 0) { bank.strip.meterL = bank.strip.meterL * 0.9f; bank.strip.meterR = bank.strip.meterR * 0.9f; continue; }
            bankBuffers[(size_t) b].clear (0, n);
            bankActive[(size_t) b] = true;
        }
        bankTail[(size_t) b] -= n;
        auto& buf = bankBuffers[(size_t) b];
        processChannel (bank.strip, buf, n);
        const bool audible = ! bank.strip.mute && (! anySolo || bank.strip.solo)
                             && (exportSoloKey < 0 || exportSoloKey == b || (exportSoloKey >= 10000 && (exportSoloKey - 10000) / cr::numPads == b));
        mixStripTo (bank.strip, buf, n, audible, b, masterBuffer);
    }

    // audio tracks
    renderAudioTracks (in, numIn, n);

    // aux returns
    for (int a = 0; a < cr::numAux; ++a)
    {
        auto& aux = sess.aux[(size_t) a];
        auto& buf = auxBuffers[(size_t) a];
        if (aux.frozen) { aux.meterL = aux.meterL * 0.9f; aux.meterR = aux.meterR * 0.9f; continue; }
        processChannel (aux, buf, n);
        const bool audible = ! aux.mute && (! anySolo || aux.solo) && (exportSoloKey < 0 || exportSoloKey == 2000 + a || exportIncludeReturns);
        applyPanVolume (buf, n, aux.volume, aux.pan);
        meter (aux, buf, n);
        if (! audible) continue;
        auto& dest = (aux.output > 0 && aux.output <= (int) hwOutBuffers.size()) ? hwOutBuffers[(size_t) aux.output - 1] : masterBuffer;
        for (int c = 0; c < 2; ++c) dest.addFrom (c, 0, buf, c, 0, n);
    }

    // master
    if (! (offlineRender && exportBypassMaster))
    {
        processChannel (sess.master, masterBuffer, n);
        applyPanVolume (masterBuffer, n, sess.master.mute ? 0.0f : sess.master.volume, sess.master.pan);
    }
    meter (sess.master, masterBuffer, n);

    // sampling onto pads (after the mix so internal sources can be resampled)
    captureSampling (in, numIn, n);

    // metronome
    const bool clickOn = (sess.metronome && (! sess.metronomeOnlyWhenRecording || recording)) || (counting && sess.metronomeDuringPreRoll);
    if (playing && clickOn && ! offlineRender)
    {
        auto clickAt = [this] (int offset, double tick)
        {
            const bool accent = std::fmod (tick, (double) cr::barTicks()) < 1.0;
            clickFreq = accent ? 1760.0 : 1100.0;
            clickPhase = 0.0;
            clickSamplesLeft = (int) (sampleRate * 0.03);
            juce::ignoreUnused (offset);
        };
        if (counting)
        {
            const double total = (double) sess.preRollBars * cr::barTicks();
            const double done0 = total - preRollLeft;
            const double done1 = done0 + n * curTps;
            const double nextBeat = std::ceil (done0 / cr::ppq) * cr::ppq;
            if (nextBeat < done1 && nextBeat < total)
            {
                const int off = juce::jlimit (0, n - 1, (int) ((nextBeat - done0) / curTps));
                clickAt (off, nextBeat);
                for (int i = off; i < n && clickSamplesLeft > 0; ++i, --clickSamplesLeft)
                {
                    const float s = (float) std::sin (clickPhase) * sess.metronomeVolume * 0.6f * ((float) clickSamplesLeft / (float) (sampleRate * 0.03));
                    clickPhase += juce::MathConstants<double>::twoPi * clickFreq / sampleRate;
                    masterBuffer.addSample (0, i, s);
                    masterBuffer.addSample (1, i, s);
                }
            }
        }
        else
        {
            int startOffset = 0;
            for (int s = 0; s < numSegs; ++s)
            {
                const auto& sg = segs[(size_t) s];
                const double t0 = sg.tick, t1 = sg.tick + sg.count * curTps;
                const double nextBeat = std::ceil (t0 / cr::ppq) * cr::ppq;
                if (nextBeat < t1)
                {
                    const int off = sg.offset + juce::jlimit (0, sg.count - 1, (int) ((nextBeat - t0) / curTps));
                    clickAt (off, nextBeat);
                    startOffset = off;
                }
            }
            for (int i = startOffset; i < n && clickSamplesLeft > 0; ++i, --clickSamplesLeft)
            {
                const float s = (float) std::sin (clickPhase) * sess.metronomeVolume * 0.6f * ((float) clickSamplesLeft / (float) (sampleRate * 0.03));
                clickPhase += juce::MathConstants<double>::twoPi * clickFreq / sampleRate;
                masterBuffer.addSample (0, i, s);
                masterBuffer.addSample (1, i, s);
            }
        }
    }

    // --- output ------------------------------------------------------------------------------------------
    for (int c = 0; c < numOut; ++c)
    {
        if (out[c] == nullptr) continue;
        if (c < 2) juce::FloatVectorOperations::copy (out[c], masterBuffer.getReadPointer (c), n);
        else
        {
            const int pair = c / 2 - 1;
            if (pair < (int) hwOutBuffers.size()) juce::FloatVectorOperations::copy (out[c], hwOutBuffers[(size_t) pair].getReadPointer (c % 2), n);
            else juce::FloatVectorOperations::clear (out[c], n);
        }
    }
    if (! offlineRender) renderPreview (out, numOut, n);

    // --- MIDI out -------------------------------------------------------------------------------------------
    sendClock (n);
    for (auto& mb : midiOutBuffers)
        if (! mb.second.isEmpty())
        {
            auto it = midiOutputs.find (mb.first);
            if (it != midiOutputs.end() && it->second) it->second->sendBlockOfMessagesNow (mb.second);
        }

    // --- advance time ----------------------------------------------------------------------------------
    if (playing)
    {
        if (counting)
        {
            preRollLeft -= n * curTps;
            if (preRollLeft <= 0.0)
            {
                preRollLeft = 0.0;
                inPreRoll = false;
                for (int b = 0; b < cr::numBanks; ++b)
                    if (sess.banks[(size_t) b].source == cr::TrackSource::pattern) patternOrigin[(size_t) b] = position;
            }
        }
        else if (numSegs > 0)
        {
            const auto& last = segs[(size_t) numSegs - 1];
            position = last.tick + last.count * curTps;
            if (sess.loopOn && sess.loopEnd > sess.loopStart && position >= sess.loopEnd) position = sess.loopStart + (position - sess.loopEnd);
        }
    }
    uiPosition = position;
    freeClockSeconds += n / sampleRate;

    // sustain pedal released: send the held note-offs
    for (int b = 0; b < cr::numBanks; ++b)
    {
        const bool down = sustainPedal[(size_t) b].load();
        if (sustainWasDown[(size_t) b] && ! down)
            for (size_t i = 0; i < sustainedNotes.size();)
            {
                if (sustainedNotes[i].first / cr::numPads == b)
                {
                    const auto sn = sustainedNotes[i];
                    sustainedNotes[i] = sustainedNotes.back();
                    sustainedNotes.pop_back();
                    deliverToPad (b, sn.first % cr::numPads, sn.second, 0.0f, false, 0);
                }
                else ++i;
            }
        sustainWasDown[(size_t) b] = down;
    }

    int active = 0;
    for (auto& v : voices) if (v.active) ++active;
    activeVoices = active;
}

void Engine::meter (cr::Strip& st, const juce::AudioBuffer<float>& b, int n)
{
    const int rc = juce::jmin (1, b.getNumChannels() - 1);
    const float l = b.getMagnitude (0, 0, n), r = b.getMagnitude (rc, 0, n);
    st.meterL = juce::jmax (l, st.meterL.load() * 0.94f);
    st.meterR = juce::jmax (r, st.meterR.load() * 0.94f);
    st.peakHold = juce::jmax (st.peakHold.load(), juce::jmax (l, r));
    // RMS with ~300 ms integration
    const float k = 1.0f - std::exp (-(float) n / (float) (0.3 * sampleRate));
    const float ml = b.getRMSLevel (0, 0, n), mr = b.getRMSLevel (rc, 0, n);
    st.rmsL = st.rmsL.load() + (ml - st.rmsL.load()) * k;
    st.rmsR = st.rmsR.load() + (mr - st.rmsR.load()) * k;
    if (l >= 0.999f || r >= 0.999f) st.clipped = true;
}

// Channel strip front end: trim + phase -> inserts -> HPF / LPF + EQ -> stereo width.
// Volume, pan and sends follow in mixStripTo.
void Engine::processChannel (cr::Strip& st, juce::AudioBuffer<float>& buf, int n)
{
    const auto& s = st.eq;
    const float trim = (s.invert ? -1.0f : 1.0f) * dbToGain (s.trimDb);
    if (trim != 1.0f) for (int c = 0; c < 2; ++c) buf.applyGain (c, 0, n, trim);
    if (! s.bypassInserts) processChain (st.inserts, buf, n);
    const bool tap = analyzerStrip.load() == &st;
    if (tap) analyzerPre.push (buf, n);
    if ((! s.bypassEq && s.anyBandOn()) || (! s.bypassFilters && s.anyFilterOn()))
        st.eqDsp.process (s, buf, n, sampleRate);
    if (s.width != 1.0f && buf.getNumChannels() > 1)
    {
        auto* l = buf.getWritePointer (0);
        auto* r = buf.getWritePointer (1);
        const float w = juce::jlimit (0.0f, 2.0f, s.width);
        for (int i = 0; i < n; ++i)
        {
            const float m = 0.5f * (l[i] + r[i]), sd = 0.5f * (l[i] - r[i]) * w;
            l[i] = m + sd;
            r[i] = m - sd;
        }
    }
    if (tap) analyzerPost.push (buf, n);
}

// Volume / pan / sends of a strip; adds the result to 'dest' (or a hardware output).
void Engine::mixStripTo (cr::Strip& st, juce::AudioBuffer<float>& src, int n, bool audible, int delayKey, juce::AudioBuffer<float>& dest)
{
    // pre-fader sends
    for (int a = 0; a < cr::numAux; ++a)
        if (st.send[a] > 0.0001f && st.sendPre[a] && audible)
            for (int c = 0; c < 2; ++c) auxBuffers[(size_t) a].addFrom (c, 0, src, c, 0, n, st.send[a]);

    applyPanVolume (src, n, st.volume, st.pan);

    // latency compensation
    if (delayKey >= 0)
    {
        auto it = delayComps.find (delayKey);
        if (it != delayComps.end() && it->second.delay > 0)
        {
            auto& dc = it->second;
            const int size = dc.buffer.getNumSamples();
            for (int c = 0; c < 2; ++c)
            {
                auto* d = src.getWritePointer (c);
                auto* line = dc.buffer.getWritePointer (c);
                int w = dc.writePos;
                for (int i = 0; i < n; ++i)
                {
                    int r = w - dc.delay;
                    if (r < 0) r += size;
                    line[w] = d[i];
                    d[i] = line[r];
                    if (++w >= size) w = 0;
                }
            }
            dc.writePos = (dc.writePos + n) % size;
        }
    }

    meter (st, src, n);
    if (! audible) return;
    for (int a = 0; a < cr::numAux; ++a)
        if (st.send[a] > 0.0001f && ! st.sendPre[a])
            for (int c = 0; c < 2; ++c) auxBuffers[(size_t) a].addFrom (c, 0, src, c, 0, n, st.send[a]);

    auto& target = (st.output > 0 && st.output <= (int) hwOutBuffers.size()) ? hwOutBuffers[(size_t) st.output - 1] : dest;
    for (int c = 0; c < 2; ++c) target.addFrom (c, 0, src, c, 0, n);
}

void Engine::processChain (std::vector<cr::InsertSlot>& chain, juce::AudioBuffer<float>& buffer, int n)
{
    for (auto& s : chain) processSlot (s, buffer, n);
}

void Engine::processSlot (cr::InsertSlot& slot, juce::AudioBuffer<float>& buffer, int n)
{
    if (slot.bypass) return;
    if (slot.builtin)
    {
        juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), 2, n);
        slot.builtin->setTempo (sess.bpm);
        slot.builtin->process (view, n);
    }
    else if (slot.plugin)
        processPlugin (*slot.plugin, buffer, emptyMidi, n);
}

void Engine::processPlugin (juce::AudioPluginInstance& plugin, juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, int n)
{
    const int chans = juce::jmax (2, juce::jmax (plugin.getTotalNumInputChannels(), plugin.getTotalNumOutputChannels()));
    if (chans > pluginBuffer.getNumChannels()) return;
    juce::AudioBuffer<float> pb (pluginBuffer.getArrayOfWritePointers(), chans, n);
    pb.clear();
    for (int c = 0; c < 2; ++c) pb.copyFrom (c, 0, buffer, c, 0, n);
    plugin.processBlock (pb, midi);
    midi.clear();
    const int outs = plugin.getTotalNumOutputChannels();
    if (outs <= 0) return;
    for (int c = 0; c < 2; ++c) buffer.copyFrom (c, 0, pb, juce::jmin (c, outs - 1), 0, n);
}

// ============================================================================
// Audio tracks: playback of clips, monitoring and recording
// ============================================================================
void Engine::renderAudioTracks (const float* const* in, int numIn, int n)
{
    bool anySolo = false;
    for (auto& b : sess.banks) if (b.created && b.strip.solo) anySolo = true;
    for (auto& t : sess.audioTracks) if (t.created && t.strip.solo) anySolo = true;
    const bool rolling = playing && ! inPreRoll.load();

    for (int t = 0; t < cr::maxAudioTracks; ++t)
    {
        auto& tr = sess.audioTracks[(size_t) t];
        if (! tr.created) continue;
        auto& buf = trackBuffers[(size_t) t];
        buf.clear (0, n);

        // input
        scratchBuffer.clear (0, n);
        if (tr.inputSource == 0)
        {
            const int c0 = tr.inputMono ? tr.inputPair : tr.inputPair * 2;
            for (int c = 0; c < 2; ++c)
            {
                const int src = tr.inputMono ? c0 : c0 + c;
                if (src < numIn && in != nullptr && in[src] != nullptr) scratchBuffer.copyFrom (c, 0, in[src], n);
            }
        }
        else if (tr.inputSource == 1)
            for (int c = 0; c < 2; ++c) scratchBuffer.copyFrom (c, 0, masterBuffer, c, 0, n);
        else if (tr.inputSource >= 2 && tr.inputSource < 2 + cr::numBanks && bankActive[(size_t) (tr.inputSource - 2)])
            for (int c = 0; c < 2; ++c) scratchBuffer.copyFrom (c, 0, bankBuffers[(size_t) (tr.inputSource - 2)], c, 0, n);
        else if (tr.inputSource >= 200 && tr.inputSource < 200 + cr::numAux)
            for (int c = 0; c < 2; ++c) scratchBuffer.copyFrom (c, 0, auxBuffers[(size_t) (tr.inputSource - 200)], c, 0, n);
        else if (! copyInternalSource (tr.inputSource, n, t)) {}
        scratchBuffer.applyGain (0, n, dbToGain (tr.inputGainDb));

        if (tr.gate)
        {
            const float thr = dbToGain (tr.gateThresholdDb);
            const float att = 1.0f - std::exp (-1.0f / (float) (0.001 * sampleRate));
            const float rel = 1.0f - std::exp (-1.0f / (float) (0.08 * sampleRate));
            auto* l = scratchBuffer.getWritePointer (0);
            auto* r = scratchBuffer.getWritePointer (1);
            float env = gateEnv[(size_t) t];
            for (int i = 0; i < n; ++i)
            {
                const float level = juce::jmax (std::abs (l[i]), std::abs (r[i]));
                const float target = level > thr ? 1.0f : 0.0f;
                env += (target - env) * (target > env ? att : rel);
                l[i] *= env;
                r[i] *= env;
            }
            gateEnv[(size_t) t] = env;
        }

        if (tr.armed && recording && rolling && audioRecordingActive && trackWriters[(size_t) t])
        {
            const float* ptrs[2] = { scratchBuffer.getReadPointer (0), scratchBuffer.getReadPointer (1) };
            trackWriters[(size_t) t]->write (ptrs, n);
            trackRecordedFrames[(size_t) t] += n;
        }
        if (tr.monitor || (tr.armed && ! playing))
            for (int c = 0; c < 2; ++c) buf.addFrom (c, 0, scratchBuffer, c, 0, n);

        // clips
        if (rolling && tr.source == cr::TrackSource::song)
        {
            for (int s = 0; s < numSegs; ++s)
            {
                const auto& sg = segs[(size_t) s];
                const double framesPerTick = 1.0 / curTps;
                for (auto& clip : tr.clips)
                {
                    if (! clip.sample) continue;
                    const auto& smp = *clip.sample;
                    const double ratio = smp.sampleRate / sampleRate;
                    const double clipStartFrame = clip.start * framesPerTick;
                    const double segStartFrame = sg.tick * framesPerTick;
                    const double clipEndFrame = clipStartFrame + clip.length / ratio;
                    if (segStartFrame + sg.count <= clipStartFrame || segStartFrame >= clipEndFrame) continue;
                    const float g = dbToGain (clip.gainDb);

                    // source frames needed for this segment
                    const int iFirst = juce::jmax (0, (int) std::ceil (clipStartFrame - segStartFrame));
                    const int iLast = juce::jmin (sg.count, (int) std::ceil (clipEndFrame - segStartFrame));
                    if (iLast <= iFirst) continue;
                    const float* srcL = nullptr;
                    const float* srcR = nullptr;
                    juce::int64 base = 0, avail = 0;
                    if (smp.isStreamed())
                    {
                        // disk-streamed (memory-mapped) stem: pull just the frames this block needs
                        const double p0 = clip.offset + (segStartFrame + iFirst - clipStartFrame) * ratio;
                        const double p1 = clip.offset + (segStartFrame + iLast - 1 - clipStartFrame) * ratio;
                        base = juce::jmax ((juce::int64) 0, (juce::int64) p0);
                        const int count = (int) juce::jmin ((juce::int64) clipStreamL.size(), juce::jmin (smp.numFrames, (juce::int64) p1 + 2) - base);
                        if (count <= 0) continue;
                        float* chans[2] = { clipStreamL.data(), clipStreamR.data() };
                        smp.mapped->read (chans, 2, base, count);
                        if (smp.mapped->numChannels == 1) std::copy (clipStreamL.begin(), clipStreamL.begin() + count, clipStreamR.begin());
                        srcL = clipStreamL.data(); srcR = clipStreamR.data(); avail = count;
                    }
                    else
                    {
                        const auto& sb = smp.buffer;
                        srcL = sb.getReadPointer (0);
                        srcR = sb.getReadPointer (juce::jmin (1, sb.getNumChannels() - 1));
                        avail = sb.getNumSamples();
                    }
                    auto* outL = buf.getWritePointer (0, sg.offset);
                    auto* outR = buf.getWritePointer (1, sg.offset);
                    for (int i = iFirst; i < iLast; ++i)
                    {
                        const double srcPos = clip.offset + (segStartFrame + i - clipStartFrame) * ratio;
                        const juce::int64 i0 = (juce::int64) srcPos - base;
                        if (i0 < 0 || i0 + 1 >= avail) continue;
                        const float frac = (float) (srcPos - std::floor (srcPos));
                        outL[i] += (srcL[i0] + frac * (srcL[i0 + 1] - srcL[i0])) * g;
                        outR[i] += (srcR[i0] + frac * (srcR[i0 + 1] - srcR[i0])) * g;
                    }
                }
            }
        }

        if (tr.strip.frozen) { tr.strip.meterL = tr.strip.meterL * 0.9f; tr.strip.meterR = tr.strip.meterR * 0.9f; buf.clear (0, n); continue; }
        processChannel (tr.strip, buf, n);
        const bool audible = ! tr.strip.mute && (! anySolo || tr.strip.solo) && (exportSoloKey < 0 || exportSoloKey == 1000 + t);
        mixStripTo (tr.strip, buf, n, audible, 1000 + t, masterBuffer);
    }
}

void Engine::finishAudioRecording()
{
    std::array<std::pair<juce::File, juce::int64>, cr::maxAudioTracks> done {};
    double startTick = 0.0;
    {
        const juce::ScopedLock sl (lock);
        audioRecordingActive = false;
        startTick = audioRecordStartTick;
        for (int t = 0; t < cr::maxAudioTracks; ++t)
        {
            if (trackWriters[(size_t) t])
            {
                done[(size_t) t] = { trackRecordFiles[(size_t) t], trackRecordedFrames[(size_t) t] };
                trackWriters[(size_t) t].reset();   // flushes
            }
        }
    }
    for (int t = 0; t < cr::maxAudioTracks; ++t)
    {
        if (done[(size_t) t].second <= 0) continue;
        auto s = loadSample (done[(size_t) t].first);
        if (! s) continue;
        const juce::ScopedLock sl (lock);
        cr::AudioClip clip;
        clip.sample = s;
        clip.start = (int) startTick;
        clip.offset = 0;
        clip.length = s->numFrames;
        sess.audioTracks[(size_t) t].clips.push_back (clip);
    }
    sendChangeMessage();
}

// ============================================================================
// Preview (browser / editor) and sampling
// ============================================================================
void Engine::preview (const juce::File& file)
{
    auto s = loadSample (file);
    previewBuffer (s);
}

void Engine::previewBuffer (cr::SamplePtr s, float start, float end)
{
    const juce::ScopedLock sl (lock);
    retire (previewSample);
    previewSample = s;
    if (s)
    {
        previewPos = start * (double) s->numFrames;
        previewEnd = end * (double) s->numFrames;
    }
}

void Engine::stopPreview()
{
    const juce::ScopedLock sl (lock);
    retire (previewSample);
    previewSample.reset();
    previewPosNorm = 0.0f;
}

void Engine::renderPreview (float* const* out, int numOut, int n)
{
    if (! previewSample || previewSample->isStreamed()) return;
    auto& b = previewSample->buffer;
    const double inc = previewSample->sampleRate / sampleRate;
    const float g = dbToGain (behaviours.previewVolumeDb);
    const int len = b.getNumSamples();
    for (int i = 0; i < n; ++i)
    {
        const int i0 = (int) previewPos;
        if (i0 + 1 >= len || previewPos >= previewEnd) { retire (previewSample); previewSample.reset(); previewPosNorm = 0.0f; return; }
        const float frac = (float) (previewPos - i0);
        for (int c = 0; c < juce::jmin (2, numOut); ++c)
        {
            if (out[c] == nullptr) continue;
            const float* d = b.getReadPointer (juce::jmin (c, b.getNumChannels() - 1));
            out[c][i] += (d[i0] + frac * (d[i0 + 1] - d[i0])) * g;
        }
        previewPos += inc;
    }
    previewPosNorm = (float) (previewPos / juce::jmax (1, len));
}

void Engine::captureSampling (const float* const* in, int numIn, int n)
{
    // input level for meters (selected hardware pair)
    float level = 0.0f;
    const auto& rs = recorder;
    scratchBuffer.clear (0, n);
    if (rs.source == 0)
    {
        for (int c = 0; c < 2; ++c)
        {
            const int src = rs.mono ? rs.inputPair : rs.inputPair * 2 + c;
            if (in != nullptr && src < numIn && in[src] != nullptr) scratchBuffer.copyFrom (c, 0, in[src], n);
        }
    }
    else if (rs.source == 1) for (int c = 0; c < 2; ++c) scratchBuffer.copyFrom (c, 0, masterBuffer, c, 0, n);
    else if (rs.source >= 2 && rs.source < 2 + cr::numBanks)
    {
        if (bankActive[(size_t) (rs.source - 2)]) for (int c = 0; c < 2; ++c) scratchBuffer.copyFrom (c, 0, bankBuffers[(size_t) (rs.source - 2)], c, 0, n);
    }
    else if (rs.source >= 200 && rs.source < 200 + cr::numAux)
        for (int c = 0; c < 2; ++c) scratchBuffer.copyFrom (c, 0, auxBuffers[(size_t) (rs.source - 200)], c, 0, n);
    else copyInternalSource (rs.source, n, -1);
    level = juce::jmax (scratchBuffer.getMagnitude (0, 0, n), scratchBuffer.getMagnitude (1, 0, n));
    inputLevel = juce::jmax (level, inputLevel.load() * 0.9f);

    if (! sampling.load()) return;
    int startAt = 0;
    if (samplingWaiting.load())
    {
        const float thr = dbToGain (rs.thresholdDb);
        startAt = -1;
        for (int i = 0; i < n && startAt < 0; ++i)
            if (std::abs (scratchBuffer.getSample (0, i)) > thr || std::abs (scratchBuffer.getSample (1, i)) > thr) startAt = i;
        if (startAt < 0) return;
        samplingWaiting = false;
    }
    const int count = n - startAt;
    const int cap = samplingBuffer.getNumSamples();
    if (samplingLength + count > cap)
    {
        // grow is not real-time safe; the buffer is pre-sized for 'maxSeconds' when recording starts
        samplingDone = true;
        sampling = false;
        return;
    }
    for (int c = 0; c < 2; ++c) samplingBuffer.copyFrom (c, samplingLength, scratchBuffer, c, startAt, count);
    samplingLength += count;
}

void Engine::startSampling()
{
    const juce::ScopedLock sl (lock);
    const int cap = (int) (juce::jlimit (1.0f, 900.0f, recorder.maxSeconds) * sampleRate);
    if (samplingBuffer.getNumSamples() < cap) samplingBuffer.setSize (2, cap, false, true, false);
    samplingLength = 0;
    samplingDone = false;
    samplingWaiting = recorder.useThreshold;
    sampling = true;
}

void Engine::stopSampling()
{
    {
        const juce::ScopedLock sl (lock);
        sampling = false;
        samplingWaiting = false;
    }
    finishSampling();
}

void Engine::finishSampling()
{
    juce::AudioBuffer<float> take;
    {
        const juce::ScopedLock sl (lock);
        samplingDone = false;
        if (samplingLength <= 16) { samplingLength = 0; return; }
        take.setSize (2, samplingLength);
        for (int c = 0; c < 2; ++c) take.copyFrom (c, 0, samplingBuffer, c, 0, samplingLength);
        samplingLength = 0;
    }
    const int b = recorder.destBank, p = recorder.destPad;
    auto f = writeSampleToRecordings (take, sampleRate, cr::bankName (b) + "_Pad" + juce::String (p + 1));
    if (f.existsAsFile())
    {
        checkpointPad (b, p);
        loadSampleToPad (f, b, p);
        if (recorder.moveToNextPad) recorder.destPad = juce::jmin (cr::numPads - 1, p + 1);
    }
    sendChangeMessage();
}

juce::File Engine::writeSampleToRecordings (const juce::AudioBuffer<float>& buffer, double sr, const juce::String& name)
{
    auto folder = getRecordingsFolder();
    folder.createDirectory();
    auto f = folder.getNonexistentChildFile (name + "_" + juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S"), ".wav");
    if (auto w = createWavWriter (f, sr, buffer.getNumChannels(), 24))
    {
        w->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
        return f;
    }
    return {};
}

juce::StringArray Engine::getInputPairNames() const
{
    juce::StringArray names;
    if (auto* d = const_cast<juce::AudioDeviceManager&> (deviceManager).getCurrentAudioDevice())
    {
        auto ins = d->getInputChannelNames();
        auto active = d->getActiveInputChannels();
        for (int i = 0; i + 1 < ins.size(); i += 2) names.add (ins[i] + " + " + ins[i + 1]);
        if (ins.size() % 2 == 1) names.add (ins[ins.size() - 1]);
        juce::ignoreUnused (active);
    }
    if (names.isEmpty()) names.add ("Input 1 + 2");
    return names;
}

juce::StringArray Engine::getInputChannelNames() const
{
    juce::StringArray names;
    if (auto* d = const_cast<juce::AudioDeviceManager&> (deviceManager).getCurrentAudioDevice())
        names = d->getInputChannelNames();
    names.removeRange (32, names.size());
    if (names.isEmpty()) names.addArray ({ "Input 1", "Input 2" });
    return names;
}

juce::StringArray Engine::getInputDeviceNames() const
{
    if (auto* type = const_cast<juce::AudioDeviceManager&> (deviceManager).getCurrentDeviceTypeObject())
        return type->getDeviceNames (true);
    return {};
}

juce::String Engine::getInputDeviceName() const
{
    auto setup = deviceManager.getAudioDeviceSetup();
    return canChooseInputDevice() ? setup.inputDeviceName : setup.outputDeviceName;
}

bool Engine::canChooseInputDevice() const
{
    if (auto* type = const_cast<juce::AudioDeviceManager&> (deviceManager).getCurrentDeviceTypeObject())
        return type->hasSeparateInputsAndOutputs();
    return false;
}

juce::String Engine::setInputDevice (const juce::String& name)
{
    if (! canChooseInputDevice()) return "This driver uses one device for inputs and outputs. Change it in Settings > Audio.";
    auto setup = deviceManager.getAudioDeviceSetup();
    if (setup.inputDeviceName == name) return {};
    setup.inputDeviceName = name;
    setup.useDefaultInputChannels = false;
    setup.inputChannels.clear();
    setup.inputChannels.setRange (0, 2, true);
    auto err = deviceManager.setAudioDeviceSetup (setup, true);
    recorder.inputPair = 0;
    return err;
}

juce::String Engine::requireInputChannels (int first, int num)
{
    auto setup = deviceManager.getAudioDeviceSetup();
    auto* d = deviceManager.getCurrentAudioDevice();
    if (d == nullptr) return "No audio device is open. Choose one in Settings > Audio.";
    const int available = d->getInputChannelNames().size();
    auto have = d->getActiveInputChannels();
    bool missing = false;
    for (int c = first; c < first + num; ++c)
        if (c >= 0 && c < juce::jmin (32, available) && ! have[c]) missing = true;
    if (! missing) return {};
    setup.useDefaultInputChannels = false;
    setup.inputChannels = have;
    for (int c = first; c < first + num; ++c)
        if (c >= 0 && c < juce::jmin (32, available)) setup.inputChannels.setBit (c);
    return deviceManager.setAudioDeviceSetup (setup, true);
}

juce::StringArray Engine::getOutputPairNames() const
{
    juce::StringArray names { "Main (1 + 2)" };
    if (auto* d = const_cast<juce::AudioDeviceManager&> (deviceManager).getCurrentAudioDevice())
    {
        auto outs = d->getOutputChannelNames();
        for (int i = 2; i + 1 < outs.size() && names.size() <= 8; i += 2) names.add (outs[i] + " + " + outs[i + 1]);
    }
    return names;
}

// ============================================================================
// Timer (message thread): housekeeping
// ============================================================================
void Engine::timerCallback()
{
    {
        const juce::ScopedLock sl (lock);
        graveyard.erase (std::remove_if (graveyard.begin(), graveyard.end(), [] (const cr::SamplePtr& s) { return s.use_count() <= 1; }), graveyard.end());
        pluginGraveyard.clear();
    }
    if (samplingDone.load()) finishSampling();
    requestStretches();
    // While recording, what's been recorded so far shows in the song and the scene grid
    // straight away (and grows as the recording goes on), not only after Stop.
    static int liveTick = 0;
    if (++liveTick % 5 == 0 && recording && playing && ! inPreRoll)
    {
        bool any = false;
        {
            const juce::ScopedLock sl (lock);
            for (auto r : recordedInto) any = any || r;
            if (any) commitRecordedPatterns (position, false, false);
        }
        if (any) uiStateChanged = true;
    }
    if (uiStateChanged.exchange (false)) sendChangeMessage();

    static int counter = 0;
    if (++counter % 60 == 0) refreshMidiOutputs();
}

void Engine::retirePlugin (std::unique_ptr<juce::AudioPluginInstance> p)
{
    if (p == nullptr) return;
    PluginWindow::closeFor (p.get());
    pluginGraveyard.push_back (std::move (p));
}

void Engine::sendClock (int n)
{
    if (behaviours.clockOutDevice.isEmpty() || offlineRender) return;
    auto it = midiOutputs.find (behaviours.clockOutDevice);
    if (it == midiOutputs.end() || ! it->second) return;
    auto bit = midiOutBuffers.find (behaviours.clockOutDevice);
    if (bit == midiOutBuffers.end()) return;
    auto& buf = bit->second;
    if (playing && ! inPreRoll.load())
    {
        if (! clockRunning)
        {
            buf.addEvent (position <= 0.0 ? juce::MidiMessage::midiStart() : juce::MidiMessage::midiContinue(), 0);
            clockRunning = true;
            clockAccumulator = 0.0;
        }
        const double ticksPerClock = cr::ppq / 24.0;
        double t = clockAccumulator;
        const double end = t + n * curTps;
        double next = std::ceil (t / ticksPerClock) * ticksPerClock;
        while (next < end)
        {
            buf.addEvent (juce::MidiMessage::midiClock(), juce::jlimit (0, n - 1, (int) ((next - t) / curTps)));
            next += ticksPerClock;
        }
        clockAccumulator = end;
    }
}
