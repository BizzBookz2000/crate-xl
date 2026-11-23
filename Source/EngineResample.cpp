// Resampling: bounce any channel to a new audio track, freeze / unfreeze, clip tools,
// and the extra live-resampling input sources.
#include "Engine.h"
#include "FileUtils.h"

bool Engine::copyInternalSource (int source, int n, int requestingTrack)
{
    if (source >= 400 && source < 400 + cr::maxAudioTracks)
    {
        // another audio track (tracks after this one arrive one block later)
        const int t = source - 400;
        if (t == requestingTrack || ! sess.audioTracks[(size_t) t].created) return false;
        auto& b = trackBuffers[(size_t) t];
        if (b.getNumSamples() < n) return false;
        for (int c = 0; c < 2; ++c) scratchBuffer.copyFrom (c, 0, b, c, 0, n);
        return true;
    }
    if (source >= 10000 && source < 10000 + cr::numBanks * cr::numPads)
    {
        const int key = source - 10000;
        for (int i = 0; i < padBusesUsed; ++i)
            if (padBuses[(size_t) i].key == key)
            {
                for (int c = 0; c < 2; ++c) scratchBuffer.copyFrom (c, 0, padBuses[(size_t) i].buffer, c, 0, n);
                return true;
            }
        return false;
    }
    return false;
}

juce::String Engine::stripDisplayName (int key)
{
    if (key == 3000) return "Main out";
    if (key >= 0 && key < cr::numBanks) return sess.banks[(size_t) key].name;
    if (key >= 1000 && key < 1000 + cr::maxAudioTracks) return sess.audioTracks[(size_t) (key - 1000)].name;
    if (key >= 2000 && key < 2000 + cr::numAux) return sess.aux[(size_t) (key - 2000)].name;
    if (key >= 10000)
    {
        const int k = key - 10000;
        const auto& p = sess.banks[(size_t) (k / cr::numPads)].pads[(size_t) (k % cr::numPads)];
        return p.name.isNotEmpty() ? p.name : cr::bankName (k / cr::numPads) + " Pad " + juce::String (k % cr::numPads + 1);
    }
    return "Channel";
}

bool Engine::isFrozen (int key)
{
    auto* st = getStrip (key);
    return st != nullptr && st->frozen;
}

int Engine::bounceToTrack (int key, const BounceOptions& o, juce::String& error, std::function<bool (float)> progress)
{
    auto* st = getStrip (key);
    if (st == nullptr || key == 3000) { error = "This channel can't be bounced."; return -1; }
    if (st->frozen) { error = "This channel is frozen. Unfreeze it first."; return -1; }
    if (o.freeze && key >= 10000) { error = "Freeze works on banks, audio tracks and AUX returns. Use Bounce for a single pad."; return -1; }

    const auto name = stripDisplayName (key);
    auto folder = getRecordingsFolder().getChildFile ("Bounces");
    folder.createDirectory();
    const auto file = folder.getNonexistentChildFile (juce::File::createLegalFileName (name + (o.freeze ? " (frozen)" : " (bounce)")), ".wav");

    ExportOptions eo;
    eo.stems = true;
    eo.trackKeys = { key };
    eo.startTick = o.startTick;
    eo.endTick = o.endTick;
    eo.includeTail = o.includeTail;
    eo.bitDepth = 24;
    eo.bypassMaster = true;
    eo.includeReturns = o.printSends && ! o.freeze;
    eo.writeToExactFile = true;

    // render the channel as heard (a freeze renders at unity: the new track takes over the fader)
    const float vol = st->volume, pan = st->pan;
    const bool mute = st->mute, solo = st->solo;
    if (o.freeze) { st->volume = 1.0f; st->pan = 0.0f; }
    st->mute = false;
    st->solo = false;
    const bool ok = exportAudio (file, eo, error, std::move (progress));
    st->volume = vol; st->pan = pan; st->mute = mute; st->solo = solo;
    if (! ok) { file.deleteFile(); return -1; }

    auto smp = loadSample (file, true);
    if (! smp) { error = "The bounce couldn't be loaded back."; return -1; }

    checkpoint();
    const int t = createAudioTrack();
    if (t < 0) { error = "No free audio tracks."; return -1; }
    {
        const juce::ScopedLock sl (lock);
        auto& tr = sess.audioTracks[(size_t) t];
        tr.name = name + (o.freeze ? " (frozen)" : " (bounce)");
        tr.strip.name = tr.name;
        cr::AudioClip c;
        c.sample = smp;
        c.start = juce::jmax (0, o.startTick);
        c.length = smp->numFrames;
        tr.clips.push_back (c);
        if (o.freeze)
        {
            tr.frozenFrom = key;
            tr.strip.volume = vol;
            tr.strip.pan = pan;
            tr.strip.mute = mute;
            tr.strip.output = st->output;
            for (int a = 0; a < cr::numAux; ++a) { tr.strip.send[a] = st->send[a]; tr.strip.sendPre[a] = st->sendPre[a]; }
            if (key < cr::numBanks) tr.colour = sess.banks[(size_t) key].colour;
            st->frozen = true;
        }
        else if (o.muteSource) st->mute = true;
    }
    if (o.removeSource && ! o.freeze && key >= 1000 && key < 1000 + cr::maxAudioTracks) deleteAudioTrack (key - 1000);
    sendChangeMessage();
    return t;
}

bool Engine::unfreeze (int key)
{
    auto* st = getStrip (key);
    if (st == nullptr || ! st->frozen) return false;
    checkpoint();
    for (int t = 0; t < cr::maxAudioTracks; ++t)
    {
        auto& tr = sess.audioTracks[(size_t) t];
        if (tr.created && tr.frozenFrom == key)
        {
            // hand the fader and sends back to the original
            const juce::ScopedLock sl (lock);
            st->volume = tr.strip.volume;
            st->pan = tr.strip.pan;
            for (int a = 0; a < cr::numAux; ++a) { st->send[a] = tr.strip.send[a]; st->sendPre[a] = tr.strip.sendPre[a]; }
        }
        if (tr.created && tr.frozenFrom == key) deleteAudioTrack (t);
    }
    {
        const juce::ScopedLock sl (lock);
        st->frozen = false;
        st->eqDsp.reset();
    }
    sendChangeMessage();
    return true;
}

cr::SamplePtr Engine::clipToSample (int t, int ci)
{
    if (! juce::isPositiveAndBelow (t, cr::maxAudioTracks)) return nullptr;
    cr::AudioClip clip;
    {
        const juce::ScopedLock sl (lock);
        auto& tr = sess.audioTracks[(size_t) t];
        if (! juce::isPositiveAndBelow (ci, (int) tr.clips.size())) return nullptr;
        clip = tr.clips[(size_t) ci];
    }
    if (! clip.sample) return nullptr;
    auto src = clip.sample;
    if (src->isStreamed()) src = loadSample (src->file, false);
    if (! src || src->isStreamed()) return nullptr;
    const auto start = juce::jlimit ((juce::int64) 0, src->numFrames, clip.offset);
    const int len = (int) juce::jmin (clip.length, src->numFrames - start);
    if (len < 2) return nullptr;
    auto s = std::make_shared<cr::SampleData>();
    s->buffer.setSize (2, len);
    for (int c = 0; c < 2; ++c) s->buffer.copyFrom (c, 0, src->buffer, juce::jmin (c, src->buffer.getNumChannels() - 1), (int) start, len);
    if (std::abs (clip.gainDb) > 0.01f) s->buffer.applyGain (dbToGain (clip.gainDb));
    s->numFrames = len;
    s->sampleRate = src->sampleRate;
    s->file = src->file;
    s->edited = true;   // written into the session folder on save
    s->name = sess.audioTracks[(size_t) t].name;
    return s;
}

bool Engine::exportClip (int t, int ci, const juce::File& file, juce::String& error)
{
    auto s = clipToSample (t, ci);
    if (! s) { error = "That clip has no audio."; return false; }
    if (auto w = createWavWriter (file, s->sampleRate, 2, 24))
    {
        w->writeFromAudioSampleBuffer (s->buffer, 0, s->buffer.getNumSamples());
        return true;
    }
    error = "Couldn't write " + file.getFullPathName();
    return false;
}

int Engine::firstEmptyPad (int bank, int from)
{
    if (! juce::isPositiveAndBelow (bank, cr::numBanks)) return -1;
    for (int p = juce::jmax (0, from); p < cr::numPads; ++p)
        if (! sess.banks[(size_t) bank].pads[(size_t) p].hasSound()) return p;
    return -1;
}
