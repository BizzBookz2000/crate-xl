// Sequencer: song / pattern playback, scene launch sync, live recording, automation, undo.
#include "Engine.h"

namespace
{
constexpr int padKey (int bank, int pad) { return bank * cr::numPads + pad; }
}

// ============================================================================
// Collecting events for one chunk
// ============================================================================
void Engine::collectSequencerEvents (int n)
{
    if (scheduledOffs.capacity() < 4096) scheduledOffs.reserve (4096);

    // pending note-offs from earlier chunks
    for (size_t i = 0; i < scheduledOffs.size();)
    {
        auto& so = scheduledOffs[i];
        if (so.samplesLeft < n)
        {
            Event e; e.offset = juce::jlimit (0, n - 1, (int) so.samplesLeft); e.bank = so.bank; e.pad = so.pad; e.key = so.key; e.on = false; e.fromSequencer = true;
            addEvent (e);
            scheduledOffs[i] = scheduledOffs.back();
            scheduledOffs.pop_back();
        }
        else { so.samplesLeft -= n; ++i; }
    }

    const double sync = juce::jmax (1, sess.sceneSyncTicks);
    for (int s = 0; s < numSegs; ++s)
    {
        const auto& sg = segs[(size_t) s];
        double t0 = sg.tick;
        const double t1 = sg.tick + sg.count * curTps;
        applyAutomation (t0);

        // launches take effect on the next sync boundary
        bool anyQueued = false;
        for (auto& b : sess.banks) if (b.queued.pending) { anyQueued = true; break; }
        if (anyQueued)
        {
            const double boundary = std::ceil (t0 / sync - 1.0e-9) * sync;
            if (boundary < t1)
            {
                collectRange (t0, boundary, sg.offset - (t0 - sg.tick) / curTps);
                for (int b = 0; b < cr::numBanks; ++b)
                    if (sess.banks[(size_t) b].queued.pending)
                    {
                        // stop notes of the old content
                        for (size_t i = 0; i < scheduledOffs.size();)
                        {
                            if (scheduledOffs[i].bank == b)
                            {
                                Event e; e.offset = juce::jlimit (0, n - 1, sg.offset + (int) ((boundary - sg.tick) / curTps)); e.bank = b; e.pad = scheduledOffs[i].pad; e.key = scheduledOffs[i].key; e.on = false;
                                addEvent (e);
                                scheduledOffs[i] = scheduledOffs.back();
                                scheduledOffs.pop_back();
                            }
                            else ++i;
                        }
                        applyLaunch (b, boundary);
                    }
                uiStateChanged = true;
                t0 = boundary;
            }
        }
        collectRange (t0, t1, (double) sg.offset - (sg.tick) / curTps * 0.0);
    }
}

// offsetBase is unused: offsets are derived from the segment table
void Engine::collectRange (double from, double to, double)
{
    if (to <= from) return;
    for (int b = 0; b < cr::numBanks; ++b)
    {
        auto& bank = sess.banks[(size_t) b];
        if (! bank.created) continue;
        if (bank.source == cr::TrackSource::pattern)
        {
            if (! juce::isPositiveAndBelow (bank.playingPattern, (int) bank.patterns.size())) continue;
            collectPatternNotes (b, bank.patterns[(size_t) bank.playingPattern], patternOrigin[(size_t) b], from, to, -1.0e18, 1.0e18, 0);
        }
        else if (bank.source == cr::TrackSource::song)
        {
            for (auto& c : sess.song)
            {
                if (c.bank != b || ! juce::isPositiveAndBelow (c.pattern, (int) bank.patterns.size())) continue;
                const double cs = c.start, ce = (double) c.start + c.length;
                if (ce <= from || cs >= to) continue;
                collectPatternNotes (b, bank.patterns[(size_t) c.pattern], cs - c.offset, juce::jmax (from, cs), juce::jmin (to, ce), cs, ce, c.offset);
            }
        }
    }
}

void Engine::collectPatternNotes (int b, cr::Pattern& pattern, double origin, double from, double to, double clipStart, double clipEnd, int)
{
    const double L = pattern.lengthTicks();
    if (L <= 0 || pattern.notes.empty()) return;
    const double r0 = from - origin, r1 = to - origin;
    const long long k0 = (long long) std::floor (r0 / L), k1 = (long long) std::floor (r1 / L);
    for (long long k = k0; k <= k1; ++k)
    {
        const double base = origin + (double) k * L;
        for (auto& note : pattern.notes)
        {
            const double at = base + note.start;
            if (at < from || at >= to || at < clipStart || at >= clipEnd) continue;
            if (note.start >= L) continue;
            if (note.skipThisPass) { note.skipThisPass = false; continue; }
            // offset inside the chunk from the segment that contains 'at'
            int offset = 0;
            for (int s = 0; s < numSegs; ++s)
            {
                const auto& sg = segs[(size_t) s];
                const double segEnd = sg.tick + sg.count * curTps;
                if (at >= sg.tick - 1.0e-6 && at < segEnd + 1.0e-6) { offset = sg.offset + (int) ((at - sg.tick) / curTps); break; }
            }
            offset = juce::jlimit (0, curBlock - 1, offset);
            Event e;
            e.offset = offset; e.bank = b; e.pad = note.pad; e.key = note.key; e.velocity = note.velocity; e.on = true; e.fromSequencer = true;
            addEvent (e);
            double lenTicks = juce::jmin ((double) note.length, clipEnd - at);
            const double offSamples = offset + juce::jmax (1.0, lenTicks / curTps);
            if (offSamples < curBlock)
            {
                Event off = e; off.on = false; off.offset = juce::jlimit (offset, curBlock - 1, (int) offSamples);
                addEvent (off);
            }
            else if (scheduledOffs.size() < scheduledOffs.capacity())
                scheduledOffs.push_back ({ offSamples - curBlock, b, note.pad, note.key });
        }
    }
}

// ============================================================================
// Live events: pads, MIDI -> voices, recording into patterns
// ============================================================================
void Engine::handleLiveEvent (const LiveEvent& le, int offset, double tick)
{
    const int b = le.bank, p = le.pad;
    auto& bank = sess.banks[(size_t) b];
    if (! bank.created) return;

    const bool repeating = repeatSlot.load() >= 0;
    if (le.on)
    {
        heldVelocity[b][p] = repeating ? le.velocity : 0.0f;
        heldKey[b][p] = le.key;
    }
    else if (heldKey[b][p] == le.key || le.key < 0)
    {
        heldVelocity[b][p] = 0.0f;
        heldKey[b][p] = -1;
    }

    if (! (repeating && ! le.on && false))
    {
        Event e; e.offset = offset; e.bank = b; e.pad = p; e.key = le.key; e.velocity = le.velocity; e.on = le.on;
        addEvent (e);
    }

    // record
    if (! recording || ! playing || inPreRoll.load() || ! bank.recordArm) return;
    if (bank.source != cr::TrackSource::pattern || bank.playingPattern != bank.workingPattern) return;
    if (! juce::isPositiveAndBelow (bank.workingPattern, (int) bank.patterns.size())) return;
    auto& pat = bank.patterns[(size_t) bank.workingPattern];
    const double L = pat.lengthTicks();
    if (L <= 0) return;
    const double pos = std::fmod (juce::jmax (0.0, tick - patternOrigin[(size_t) b]), L);
    const int q = cr::quantizeTicks (sess.quantize);

    if (le.on)
    {
        int start = (int) std::round (pos);
        if (sess.quantize > 0)
        {
            const int step = (int) std::round (pos / q);
            start = step * q;
            if (step % 2 == 1) start += (int) std::round (sess.swing * q);
        }
        bool wrapped = false;
        if (start >= (int) L) { start -= (int) L; wrapped = true; }
        cr::Note note;
        note.pad = p; note.key = le.key; note.start = juce::jmax (0, start); note.length = juce::jmax (q, cr::ppq / 8); note.velocity = le.velocity;
        // a note quantised forward would be played again by the sequencer in this pass
        note.skipThisPass = wrapped || start > pos + 0.5;
        if (pat.notes.size() < 100000)
        {
            pat.notes.push_back (note);
            if (pendingRecords.size() < pendingRecords.capacity())
                pendingRecords.push_back ({ b, p, le.key, (int) pat.notes.size() - 1, bank.workingPattern, tick });
        }
    }
    else
    {
        for (size_t i = 0; i < pendingRecords.size(); ++i)
        {
            auto& pr = pendingRecords[i];
            if (pr.bank != b || pr.pad != p || pr.key != le.key) continue;
            if (pr.pattern == bank.workingPattern && juce::isPositiveAndBelow (pr.noteIndex, (int) pat.notes.size()))
            {
                auto& note = pat.notes[(size_t) pr.noteIndex];
                if (note.pad == p)
                {
                    int len = (int) std::round (tick - pr.startTick);
                    if (sess.quantize > 0) len = juce::jmax (q, (int) std::round ((double) len / q) * q);
                    note.length = juce::jlimit (1, (int) L, len);
                }
            }
            pendingRecords.erase (pendingRecords.begin() + (long) i);
            break;
        }
    }
}

void Engine::applyEvent (const Event& e)
{
    deliverToPad (e.bank, e.pad, e.key, e.velocity, e.on, e.offset);
    // link groups: pads in the same link group play together
    const auto& bank = sess.banks[(size_t) e.bank];
    const int link = bank.pads[(size_t) e.pad].link;
    if (link > 0)
        for (int p = 0; p < cr::numPads; ++p)
            if (p != e.pad && bank.pads[(size_t) p].link == link)
                deliverToPad (e.bank, p, e.key, e.velocity, e.on, e.offset);
}

void Engine::deliverToPad (int b, int p, int key, float vel, bool on, int offset)
{
    auto& bank = sess.banks[(size_t) b];
    auto& pad = bank.pads[(size_t) p];

    if (on)
    {
        padFlash[b][p] = juce::jmax (padFlash[b][p].load(), juce::jmax (0.35f, vel));
        if (key >= 0) lastPlayedKey = key;
    }

    // sustain pedal holds note-offs
    if (! on && sustainPedal[(size_t) b].load())
    {
        if (sustainedNotes.size() < 512) sustainedNotes.push_back ({ padKey (b, p), key });
        return;
    }

    if (pad.instrument)
    {
        for (size_t i = 0; i < instrumentPads.size(); ++i)
            if (instrumentPads[i] == padKey (b, p))
            {
                const int note = juce::jlimit (0, 127, key >= 0 ? key : pad.padNote);
                if (on) instrumentMidi[i].addEvent (juce::MidiMessage::noteOn (1, note, juce::jlimit (0.0f, 1.0f, vel)), offset);
                else instrumentMidi[i].addEvent (juce::MidiMessage::noteOff (1, note), offset);
            }
    }

    if (on) startVoices (b, p, key, vel, false);
    else
    {
        stopVoices (b, p, key);
        bool hasRelease = false;
        for (auto& l : pad.layers) if (l.p.trigger == cr::TriggerMode::onRelease) hasRelease = true;
        if (hasRelease) startVoices (b, p, key, juce::jmax (0.5f, vel), true);
    }
    mirrorMidiOut (b, p, key, vel, on, offset);
}

void Engine::mirrorMidiOut (int b, int p, int key, float vel, bool on, int offset)
{
    auto& bank = sess.banks[(size_t) b];
    auto& pad = bank.pads[(size_t) p];
    const juce::String& dev = pad.midi.outDevice.isNotEmpty() ? pad.midi.outDevice : bank.midi.outDevice;
    if (dev.isEmpty()) return;
    if (offlineRender) return;
    if ((pad.channel.mute && behaviours.padMuteAlsoMidi) || (bank.strip.mute && behaviours.mixerMuteAlsoMidi)) return;
    int channel = pad.midi.outChannel > 0 ? pad.midi.outChannel : (pad.hasSound() ? bank.midi.outChannel : (p % 16) + 1);
    channel = juce::jlimit (1, 16, channel);
    const int note = juce::jlimit (0, 127, key >= 0 ? key : pad.padNote);
    auto it = midiOutBuffers.find (dev);
    if (it == midiOutBuffers.end()) return;
    auto& buf = it->second;
    if (on) buf.addEvent (juce::MidiMessage::noteOn (channel, note, juce::jlimit (0.0f, 1.0f, vel)), offset);
    else buf.addEvent (juce::MidiMessage::noteOff (channel, note), offset);
}

// ============================================================================
// Automation playback
// ============================================================================
void Engine::applyLanes (std::vector<cr::AutomationLane>& lanes, double tick)
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    for (auto& lane : lanes)
    {
        if (lane.points.empty() || lane.suspended || ! lane.address.isValid()) continue;
        auto it = touchedUntil.find (lane.param);
        if (it != touchedUntil.end() && it->second > now) continue;
        setParamNormalised (lane.address, lane.valueAt (tick), false);
    }
}

void Engine::applyAutomation (double tick)
{
    if (! sess.songAutomation.empty())
    {
        // song lanes of banks that are not following the song are ignored
        const double now = juce::Time::getMillisecondCounterHiRes();
        for (auto& lane : sess.songAutomation)
        {
            if (lane.points.empty() || lane.suspended || ! lane.address.isValid()) continue;
            const int b = lane.address.bankIndex();
            if (b >= 0 && sess.banks[(size_t) b].source != cr::TrackSource::song) continue;
            auto it = touchedUntil.find (lane.param);
            if (it != touchedUntil.end() && it->second > now) continue;
            setParamNormalised (lane.address, lane.valueAt (tick), false);
        }
    }
    for (int b = 0; b < cr::numBanks; ++b)
    {
        auto& bank = sess.banks[(size_t) b];
        if (! bank.created) continue;
        if (bank.source == cr::TrackSource::pattern)
        {
            if (! juce::isPositiveAndBelow (bank.playingPattern, (int) bank.patterns.size())) continue;
            auto& pat = bank.patterns[(size_t) bank.playingPattern];
            if (pat.automation.empty()) continue;
            const double L = juce::jmax (1, pat.lengthTicks());
            applyLanes (pat.automation, std::fmod (juce::jmax (0.0, tick - patternOrigin[(size_t) b]), L));
        }
        else if (bank.source == cr::TrackSource::song)
        {
            for (auto& c : sess.song)
            {
                if (c.bank != b || tick < c.start || tick >= c.start + c.length) continue;
                if (! juce::isPositiveAndBelow (c.pattern, (int) bank.patterns.size())) continue;
                auto& pat = bank.patterns[(size_t) c.pattern];
                if (pat.automation.empty()) continue;
                const double L = juce::jmax (1, pat.lengthTicks());
                applyLanes (pat.automation, std::fmod (tick - c.start + c.offset, L));
            }
        }
    }
}

void Engine::recordAutomationPoint (const juce::String& param, const cr::ParamAddress& a, float norm)
{
    if (! (automationRecord.load() && recording && playing) || inPreRoll.load()) return;
    const int b = a.bankIndex();
    const int q = juce::jmax (1, cr::quantizeTicks (behaviours.automationQuantize));
    cr::AutomationLane* lane = nullptr;
    double tick = position;
    if (b >= 0 && sess.banks[(size_t) b].source == cr::TrackSource::pattern)
    {
        auto& bank = sess.banks[(size_t) b];
        auto& pat = bank.patterns[(size_t) juce::jlimit (0, (int) bank.patterns.size() - 1, bank.playingPattern)];
        tick = std::fmod (juce::jmax (0.0, position - patternOrigin[(size_t) b]), (double) juce::jmax (1, pat.lengthTicks()));
        for (auto& l : pat.automation) if (l.param == param) lane = &l;
        if (lane == nullptr) { pat.automation.push_back ({}); lane = &pat.automation.back(); lane->param = param; lane->address = a; }
    }
    else
    {
        for (auto& l : sess.songAutomation) if (l.param == param) lane = &l;
        if (lane == nullptr) { sess.songAutomation.push_back ({}); lane = &sess.songAutomation.back(); lane->param = param; lane->address = a; }
    }
    lane->suspended = true;
    const int t = (int) std::round (tick / q) * q;
    auto& pts = lane->points;
    auto it = std::find_if (pts.begin(), pts.end(), [t] (const cr::AutoPoint& p) { return p.tick == t; });
    if (it != pts.end()) it->value = norm;
    else { pts.push_back ({ t, norm }); lane->sort(); }
}

// ============================================================================
// Undo
// ============================================================================
cr::SequencerSnapshot Engine::makeSnapshot() const
{
    cr::SequencerSnapshot s;
    for (int b = 0; b < cr::numBanks; ++b)
        if (sess.banks[(size_t) b].created) s.patterns.push_back ({ b, sess.banks[(size_t) b].patterns });
    s.song = sess.song;
    for (int t = 0; t < cr::maxAudioTracks; ++t)
        if (sess.audioTracks[(size_t) t].created) s.audioClips.push_back ({ t, sess.audioTracks[(size_t) t].clips });
    s.songAutomation = sess.songAutomation;
    s.scenes = sess.scenes;
    return s;
}

void Engine::restoreSnapshot (const cr::SequencerSnapshot& s)
{
    const juce::ScopedLock sl (lock);
    for (auto& [b, pats] : s.patterns)
    {
        auto& bank = sess.banks[(size_t) b];
        bank.patterns = pats;
        if (bank.patterns.empty()) bank.patterns.push_back ({});
        bank.workingPattern = juce::jlimit (0, (int) bank.patterns.size() - 1, bank.workingPattern);
        bank.playingPattern = juce::jlimit (0, (int) bank.patterns.size() - 1, bank.playingPattern);
    }
    sess.song = s.song;
    for (auto& [t, clips] : s.audioClips) sess.audioTracks[(size_t) t].clips = clips;
    sess.songAutomation = s.songAutomation;
    sess.scenes = s.scenes;
    pendingRecords.clear();
}

void Engine::pushUndo (UndoEntry e)
{
    undoStack.push_back (std::move (e));
    if (undoStack.size() > 100) undoStack.erase (undoStack.begin());
    redoStack.clear();
}

void Engine::checkpoint()
{
    UndoEntry e;
    {
        const juce::ScopedLock sl (lock);
        e.seq = makeSnapshot();
    }
    pushUndo (std::move (e));
}

void Engine::checkpointPad (int bank, int pad)
{
    pushUndo (makePadEntry (bank, pad));
}

bool Engine::undo()
{
    if (undoStack.empty()) return false;
    auto e = std::move (undoStack.back());
    undoStack.pop_back();
    UndoEntry current;
    if (e.isPad) current = makePadEntry (e.bank, e.pad);
    else { const juce::ScopedLock sl (lock); current.seq = makeSnapshot(); }
    if (e.isPad) restorePadEntry (e); else restoreSnapshot (e.seq);
    redoStack.push_back (std::move (current));
    sendChangeMessage();
    return true;
}

bool Engine::redo()
{
    if (redoStack.empty()) return false;
    auto e = std::move (redoStack.back());
    redoStack.pop_back();
    UndoEntry current;
    if (e.isPad) current = makePadEntry (e.bank, e.pad);
    else { const juce::ScopedLock sl (lock); current.seq = makeSnapshot(); }
    if (e.isPad) restorePadEntry (e); else restoreSnapshot (e.seq);
    undoStack.push_back (std::move (current));
    sendChangeMessage();
    return true;
}

int Engine::getSongLengthTicks() const
{
    int end = 0;
    for (auto& c : sess.song) end = juce::jmax (end, c.start + c.length);
    for (auto& t : sess.audioTracks)
        if (t.created)
            for (auto& c : t.clips)
                if (c.sample)
                {
                    const double secs = (double) c.length / c.sample->sampleRate;
                    end = juce::jmax (end, c.start + (int) std::ceil (secs * sess.bpm / 60.0 * cr::ppq));
                }
    return end;
}
