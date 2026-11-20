// Samples, layers and zones; the audio editor; slicing; inserts and plugins.
#include "Engine.h"
#include "Stretch.h"
#include "FileUtils.h"

namespace
{
std::pair<int, double> guessBeats (double seconds)
{
    if (seconds <= 0.05) return { 1, 120.0 };
    int beats = 4;
    double bpm = 60.0 * beats / seconds;
    while (bpm < 70.0 && beats < 256) { beats *= 2; bpm = 60.0 * beats / seconds; }
    while (bpm > 180.0 && beats > 1) { beats /= 2; bpm = 60.0 * beats / seconds; }
    return { beats, bpm };
}
} // namespace

// ============================================================================
// Loading
// ============================================================================
cr::SamplePtr Engine::loadSample (const juce::File& file, bool streamFromDisk)
{
    if (! file.existsAsFile()) return nullptr;
    auto* format = formatManager.findFormatForFileExtension (file.getFileExtension());
    if (format == nullptr) return nullptr;

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0) return nullptr;
    const double seconds = (double) reader->lengthInSamples / reader->sampleRate;
    const bool wantStream = streamFromDisk || (behaviours.streamLongSamples && seconds > 30.0);

    auto s = std::make_shared<cr::SampleData>();
    s->file = file;
    s->name = file.getFileNameWithoutExtension();
    s->sampleRate = reader->sampleRate;
    s->numFrames = reader->lengthInSamples;

    if (wantStream)
    {
        if (auto* mm = format->createMemoryMappedReader (file))
        {
            std::unique_ptr<juce::MemoryMappedAudioFormatReader> mapped (mm);
            if (mapped->mapEntireFile())
            {
                s->mapped = std::move (mapped);
                return s;
            }
        }
    }

    const auto maxLen = (juce::int64) (reader->sampleRate * 1200.0);   // 20 minutes in memory at most
    const int len = (int) juce::jmin (reader->lengthInSamples, maxLen);
    s->buffer.setSize (2, len);
    reader->read (&s->buffer, 0, len, 0, true, true);
    if (reader->numChannels == 1) s->buffer.copyFrom (1, 0, s->buffer, 0, 0, len);
    s->numFrames = len;
    return s;
}

bool Engine::setPadSample (int b, int p, cr::SamplePtr s, LoadMethod method)
{
    if (! s || ! juce::isPositiveAndBelow (b, cr::numBanks) || ! juce::isPositiveAndBelow (p, cr::numPads)) return false;
    createBank (b);
    const auto guess = guessBeats (s->lengthSeconds());
    {
        const juce::ScopedLock sl (lock);
        auto& pad = sess.banks[(size_t) b].pads[(size_t) p];
        cr::Zone zone;
        zone.sample = s;
        if (method == LoadMethod::addToLayer && ! pad.layers.empty())
        {
            const int l = juce::jlimit (0, (int) pad.layers.size() - 1, selectedLayer.load());
            pad.layers[(size_t) l].zones.push_back (zone);
        }
        else
        {
            if (method == LoadMethod::replace)
            {
                for (auto& v : voices) if (v.active && v.bank == b && v.pad == p && v.fadeOut < 0) v.fadeOut = 64;
                for (auto& l : pad.layers)
                {
                    for (auto& z : l.zones) retire (z.sample);
                    for (auto& f : l.fx) if (f.plugin) retirePlugin (std::move (f.plugin));
                }
                pad.layers.clear();
                pad.name = s->name;
            }
            auto layer = makeDefaultLayer();
            layer.name = s->name;
            layer.zones.push_back (zone);
            layer.p.sampleBeats = guess.first;
            layer.p.sampleBpm = (float) guess.second;
            layer.p.diskStream = s->isStreamed();
            pad.layers.push_back (std::move (layer));
            if (pad.name.isEmpty()) pad.name = s->name;
        }
    }
    selectedLayer = 0;
    selectedZone = 0;
    sendChangeMessage();
    return true;
}

bool Engine::loadSampleToPad (const juce::File& file, int b, int p, LoadMethod method)
{
    auto s = loadSample (file);
    if (! s) return false;
    return setPadSample (b, p, s, method);
}

void Engine::setZoneSample (int b, int p, int l, int z, cr::SamplePtr s)
{
    const juce::ScopedLock sl (lock);
    auto& pad = sess.banks[(size_t) b].pads[(size_t) p];
    if (! juce::isPositiveAndBelow (l, (int) pad.layers.size())) return;
    auto& layer = pad.layers[(size_t) l];
    if (! juce::isPositiveAndBelow (z, (int) layer.zones.size())) return;
    for (auto& v : voices) if (v.active && v.bank == b && v.pad == p && v.layer == l && v.zone == z && v.fadeOut < 0) v.fadeOut = 64;
    retire (layer.zones[(size_t) z].sample);
    layer.zones[(size_t) z].sample = s;
}

int Engine::addLayer (int b, int p)
{
    createBank (b);
    const juce::ScopedLock sl (lock);
    auto& pad = sess.banks[(size_t) b].pads[(size_t) p];
    auto layer = makeDefaultLayer();
    layer.name = "Layer " + juce::String (pad.layers.size() + 1);
    pad.layers.push_back (std::move (layer));
    return (int) pad.layers.size() - 1;
}

void Engine::removeLayer (int b, int p, int l)
{
    const juce::ScopedLock sl (lock);
    auto& pad = sess.banks[(size_t) b].pads[(size_t) p];
    if (! juce::isPositiveAndBelow (l, (int) pad.layers.size())) return;
    for (auto& v : voices) if (v.active && v.bank == b && v.pad == p) { if (v.layer == l) { v.active = false; v.sample.reset(); v.stretched.reset(); } else if (v.layer > l) --v.layer; }
    for (auto& z : pad.layers[(size_t) l].zones) retire (z.sample);
    for (auto& f : pad.layers[(size_t) l].fx) if (f.plugin) retirePlugin (std::move (f.plugin));
    pad.layers.erase (pad.layers.begin() + l);
    selectedLayer = juce::jlimit (0, juce::jmax (0, (int) pad.layers.size() - 1), selectedLayer.load());
}

void Engine::removeZone (int b, int p, int l, int z)
{
    const juce::ScopedLock sl (lock);
    auto& pad = sess.banks[(size_t) b].pads[(size_t) p];
    if (! juce::isPositiveAndBelow (l, (int) pad.layers.size())) return;
    auto& layer = pad.layers[(size_t) l];
    if (! juce::isPositiveAndBelow (z, (int) layer.zones.size())) return;
    for (auto& v : voices) if (v.active && v.bank == b && v.pad == p && v.layer == l) { if (v.zone == z) { v.active = false; v.sample.reset(); v.stretched.reset(); } else if (v.zone > z) --v.zone; }
    retire (layer.zones[(size_t) z].sample);
    layer.zones.erase (layer.zones.begin() + z);
    if (layer.zones.empty() && pad.layers.size() > 1) pad.layers.erase (pad.layers.begin() + l);
}

void Engine::moveZone (int b, int p, int fromLayer, int z, int toLayer)
{
    const juce::ScopedLock sl (lock);
    auto& pad = sess.banks[(size_t) b].pads[(size_t) p];
    if (! juce::isPositiveAndBelow (fromLayer, (int) pad.layers.size()) || ! juce::isPositiveAndBelow (toLayer, (int) pad.layers.size()) || fromLayer == toLayer) return;
    auto& from = pad.layers[(size_t) fromLayer];
    if (! juce::isPositiveAndBelow (z, (int) from.zones.size())) return;
    for (auto& v : voices) if (v.active && v.bank == b && v.pad == p) { v.active = false; v.sample.reset(); v.stretched.reset(); }
    pad.layers[(size_t) toLayer].zones.push_back (from.zones[(size_t) z]);
    from.zones.erase (from.zones.begin() + z);
}

void Engine::clearPad (int b, int p)
{
    checkpointPad (b, p);
    {
        const juce::ScopedLock sl (lock);
        auto& pad = sess.banks[(size_t) b].pads[(size_t) p];
        for (auto& v : voices) if (v.active && v.bank == b && v.pad == p) { v.active = false; v.sample.reset(); v.stretched.reset(); }
        for (auto& l : pad.layers)
        {
            for (auto& z : l.zones) retire (z.sample);
            for (auto& f : l.fx) if (f.plugin) retirePlugin (std::move (f.plugin));
        }
        pad.layers.clear();
        if (pad.instrument) retirePlugin (std::move (pad.instrument));
        for (auto& s : pad.channel.inserts) if (s.plugin) retirePlugin (std::move (s.plugin));
        pad.channel.inserts.clear();
        pad.name.clear();
        pad.keysMode = false;
        pad.choke = pad.link = 0;
        pad.padNote = 60;
        pad.channel.volume = 0.8f;
        pad.channel.pan = 0.0f;
        pad.channel.mute = pad.channel.solo = false;
        for (auto& s : pad.channel.send) s = 0.0f;
    }
    notifyStructureChanged();
    sendChangeMessage();
}

void Engine::setLayerStreaming (int b, int p, int l, bool stream)
{
    std::vector<std::pair<int, juce::File>> files;
    {
        const juce::ScopedLock sl (lock);
        auto& pad = sess.banks[(size_t) b].pads[(size_t) p];
        if (! juce::isPositiveAndBelow (l, (int) pad.layers.size())) return;
        auto& layer = pad.layers[(size_t) l];
        layer.p.diskStream = stream;
        for (int z = 0; z < (int) layer.zones.size(); ++z)
            if (auto s = layer.zones[(size_t) z].sample)
                if (s->isStreamed() != stream && ! s->edited && s->file.existsAsFile()) files.push_back ({ z, s->file });
    }
    for (auto& [z, f] : files)
        if (auto s = loadSample (f, stream))
        {
            if (stream && ! s->isStreamed()) continue;   // format can't be memory-mapped: stays in RAM
            setZoneSample (b, p, l, z, s);
        }
}

// ============================================================================
// Audio editor
// ============================================================================
float Engine::snapToZeroCrossing (const cr::SampleData& s, float pos) const
{
    if (s.isStreamed() || s.buffer.getNumSamples() < 4) return pos;
    const auto* d = s.buffer.getReadPointer (0);
    const int len = s.buffer.getNumSamples();
    const int c = juce::jlimit (1, len - 2, (int) (pos * len));
    for (int r = 0; r < 4096; ++r)
    {
        for (int dir : { 1, -1 })
        {
            const int i = c + dir * r;
            if (i <= 0 || i >= len - 1) continue;
            if ((d[i - 1] <= 0.0f && d[i] > 0.0f) || (d[i - 1] >= 0.0f && d[i] < 0.0f)) return (float) i / (float) len;
        }
    }
    return pos;
}

bool Engine::editSample (int b, int p, int l, int z, SampleEdit edit, float selStart, float selEnd, double amount)
{
    cr::SamplePtr src;
    {
        const juce::ScopedLock sl (lock);
        auto& pad = sess.banks[(size_t) b].pads[(size_t) p];
        if (! juce::isPositiveAndBelow (l, (int) pad.layers.size())) return false;
        auto& layer = pad.layers[(size_t) l];
        if (! juce::isPositiveAndBelow (z, (int) layer.zones.size())) return false;
        src = layer.zones[(size_t) z].sample;
    }
    if (! src) return false;
    if (src->isStreamed())
    {
        src = loadSample (src->file, false);   // edits work in memory
        if (! src || src->isStreamed()) return false;
    }

    const auto& in = src->buffer;
    const int len = in.getNumSamples();
    if (selEnd < selStart) std::swap (selStart, selEnd);
    int s0 = juce::jlimit (0, len, (int) std::round (selStart * len));
    int s1 = juce::jlimit (0, len, (int) std::round (selEnd * len));
    const bool hasSel = s1 - s0 > 1;
    if (! hasSel) { s0 = 0; s1 = len; }

    if (edit == SampleEdit::copy)
    {
        audioClipboard.setSize (2, s1 - s0);
        for (int c = 0; c < 2; ++c) audioClipboard.copyFrom (c, 0, in, c, s0, s1 - s0);
        audioClipboardRate = src->sampleRate;
        return true;
    }

    checkpointPad (b, p);
    juce::AudioBuffer<float> out;
    auto region = [&] () { juce::AudioBuffer<float> r (2, s1 - s0); for (int c = 0; c < 2; ++c) r.copyFrom (c, 0, in, c, s0, s1 - s0); return r; };
    auto splice = [&] (const juce::AudioBuffer<float>& middle)
    {
        out.setSize (2, s0 + middle.getNumSamples() + (len - s1));
        for (int c = 0; c < 2; ++c)
        {
            if (s0 > 0) out.copyFrom (c, 0, in, c, 0, s0);
            if (middle.getNumSamples() > 0) out.copyFrom (c, s0, middle, juce::jmin (c, middle.getNumChannels() - 1), 0, middle.getNumSamples());
            if (len - s1 > 0) out.copyFrom (c, s0 + middle.getNumSamples(), in, c, s1, len - s1);
        }
    };

    switch (edit)
    {
        case SampleEdit::normalize:
        {
            auto r = region();
            const float peak = r.getMagnitude (0, r.getNumSamples());
            if (peak > 0.00001f) r.applyGain (0.98f / peak);
            splice (r);
            break;
        }
        case SampleEdit::gain: { auto r = region(); r.applyGain (dbToGain ((float) amount)); splice (r); break; }
        case SampleEdit::reverse: { auto r = region(); r.reverse (0, r.getNumSamples()); splice (r); break; }
        case SampleEdit::silence: { auto r = region(); r.clear(); splice (r); break; }
        case SampleEdit::fadeIn: { auto r = region(); r.applyGainRamp (0, r.getNumSamples(), 0.0f, 1.0f); splice (r); break; }
        case SampleEdit::fadeOut: { auto r = region(); r.applyGainRamp (0, r.getNumSamples(), 1.0f, 0.0f); splice (r); break; }
        case SampleEdit::dcRemove:
        {
            auto r = region();
            for (int c = 0; c < 2; ++c)
            {
                double sum = 0.0;
                auto* d = r.getWritePointer (c);
                for (int i = 0; i < r.getNumSamples(); ++i) sum += d[i];
                const float mean = (float) (sum / juce::jmax (1, r.getNumSamples()));
                for (int i = 0; i < r.getNumSamples(); ++i) d[i] -= mean;
            }
            splice (r);
            break;
        }
        case SampleEdit::stretch: splice (stretch::timeStretch (region(), juce::jlimit (0.1, 10.0, amount), src->sampleRate)); break;
        case SampleEdit::pitch: splice (stretch::pitchShift (region(), amount, src->sampleRate)); break;
        case SampleEdit::trim:
            if (! hasSel) return false;
            out = region();
            break;
        case SampleEdit::cut:
            if (! hasSel) return false;
            audioClipboard = region();
            audioClipboardRate = src->sampleRate;
            splice ({});
            break;
        case SampleEdit::deleteRange:
            if (! hasSel) return false;
            splice ({});
            break;
        case SampleEdit::paste:
        {
            if (audioClipboard.getNumSamples() == 0) return false;
            juce::AudioBuffer<float> clip = audioClipboard;
            if (std::abs (audioClipboardRate - src->sampleRate) > 1.0) clip = stretch::resample (clip, audioClipboardRate / src->sampleRate);
            if (! hasSel) { s0 = s1 = juce::jlimit (0, len, (int) std::round (selStart * len)); }
            splice (clip);
            break;
        }
        default: return false;
    }
    if (out.getNumSamples() < 2) return false;

    auto ns = std::make_shared<cr::SampleData>();
    ns->buffer = std::move (out);
    ns->numFrames = ns->buffer.getNumSamples();
    ns->sampleRate = src->sampleRate;
    ns->file = src->file;
    ns->edited = true;
    ns->name = src->name.endsWith (" (edited)") ? src->name : src->name + " (edited)";
    const bool lengthChanged = ns->numFrames != (juce::int64) len;
    {
        const juce::ScopedLock sl (lock);
        auto& zone = sess.banks[(size_t) b].pads[(size_t) p].layers[(size_t) l].zones[(size_t) z];
        if (lengthChanged)
        {
            zone.start = 0.0f; zone.end = 1.0f; zone.loopStart = 0.0f; zone.loopEnd = 1.0f;
            zone.slices.clear();
        }
    }
    setZoneSample (b, p, l, z, ns);
    sendChangeMessage();
    return true;
}

bool Engine::duplicateSampleFile (int b, int p, int l, int z)
{
    cr::SamplePtr s;
    {
        const juce::ScopedLock sl (lock);
        auto& pad = sess.banks[(size_t) b].pads[(size_t) p];
        if (! juce::isPositiveAndBelow (l, (int) pad.layers.size()) || ! juce::isPositiveAndBelow (z, (int) pad.layers[(size_t) l].zones.size())) return false;
        s = pad.layers[(size_t) l].zones[(size_t) z].sample;
    }
    if (! s) return false;
    if (s->isStreamed()) s = loadSample (s->file, false);
    if (! s || s->isStreamed()) return false;
    auto folder = getSamplesFolder();
    folder.createDirectory();
    auto f = folder.getNonexistentChildFile (s->name.upToFirstOccurrenceOf (" (edited)", false, false) + " copy", ".wav");
    if (auto w = createWavWriter (f, s->sampleRate, 2, 24))
    {
        w->writeFromAudioSampleBuffer (s->buffer, 0, s->buffer.getNumSamples());
        w.reset();
        if (auto ns = loadSample (f)) { setZoneSample (b, p, l, z, ns); sendChangeMessage(); return true; }
    }
    return false;
}

// ============================================================================
// Slicing
// ============================================================================
std::vector<float> Engine::autoSlice (int b, int p, int l, int z, int mode, int amount, float sensitivity)
{
    cr::SamplePtr s;
    float zs = 0.0f, ze = 1.0f;
    double bpm = 120.0;
    {
        const juce::ScopedLock sl (lock);
        auto& pad = sess.banks[(size_t) b].pads[(size_t) p];
        if (! juce::isPositiveAndBelow (l, (int) pad.layers.size()) || ! juce::isPositiveAndBelow (z, (int) pad.layers[(size_t) l].zones.size())) return {};
        const auto& zone = pad.layers[(size_t) l].zones[(size_t) z];
        s = zone.sample; zs = zone.start; ze = zone.end;
        bpm = pad.layers[(size_t) l].p.sampleBpm;
    }
    if (! s) return {};
    std::vector<float> pts;
    if (mode == 0)
    {
        const int n = juce::jlimit (1, 128, amount);
        for (int i = 0; i < n; ++i) pts.push_back (zs + (ze - zs) * (float) i / (float) n);
    }
    else if (mode == 1)
    {
        static const double beatsPerDiv[] = { 4.0, 2.0, 1.0, 0.5, 0.25, 0.125 };   // 1 bar, 1/2, 1/4, 1/8, 1/16, 1/32
        const double divSec = beatsPerDiv[juce::jlimit (0, 5, amount)] * 60.0 / juce::jmax (1.0, bpm);
        const double total = s->lengthSeconds();
        for (double t = zs * total; t < ze * total - 0.001 && pts.size() < 128; t += divSec) pts.push_back ((float) (t / total));
    }
    else
    {
        // transient detection on the energy envelope
        juce::AudioBuffer<float> buf;
        if (s->isStreamed()) { auto m = loadSample (s->file, false); if (m) buf = m->buffer; }
        else buf = s->buffer;
        const int len = buf.getNumSamples();
        if (len < 2048) return { zs };
        const int hop = 256;
        std::vector<float> energy;
        for (int i = 0; i + hop < len; i += hop)
        {
            float e = 0.0f;
            for (int c = 0; c < 2; ++c) { const auto* d = buf.getReadPointer (c); for (int k = 0; k < hop; ++k) e += d[i + k] * d[i + k]; }
            energy.push_back (std::sqrt (e / (2 * hop)));
        }
        const float thr = juce::jmap (juce::jlimit (0.0f, 1.0f, sensitivity), 4.0f, 1.25f);
        const int minGap = (int) (0.06 * s->sampleRate / hop);
        int last = -minGap;
        float avg = energy.empty() ? 0.0f : energy[0];
        pts.push_back (zs);
        for (int i = 1; i < (int) energy.size(); ++i)
        {
            const float pos = (float) (i * hop) / (float) len;
            if (pos <= zs || pos >= ze) { avg = avg * 0.9f + energy[(size_t) i] * 0.1f; continue; }
            if (energy[(size_t) i] > avg * thr && energy[(size_t) i] > 0.01f && i - last >= minGap && pts.size() < 128)
            {
                // step back to the start of the attack
                int j = i;
                while (j > 0 && energy[(size_t) j - 1] < energy[(size_t) j] && i - j < 8) --j;
                pts.push_back ((float) (j * hop) / (float) len);
                last = i;
            }
            avg = avg * 0.9f + energy[(size_t) i] * 0.1f;
        }
    }
    std::sort (pts.begin(), pts.end());
    pts.erase (std::unique (pts.begin(), pts.end()), pts.end());
    {
        const juce::ScopedLock sl (lock);
        sess.banks[(size_t) b].pads[(size_t) p].layers[(size_t) l].zones[(size_t) z].slices = pts;
    }
    return pts;
}

bool Engine::applySlices (int b, int p, int l, int z, const std::vector<float>& slicePoints, SliceTarget target, int destBank,
                          int startPadOrKey, int chokeGroupNum, bool createPattern, float fadeInMs, float fadeOutMs, juce::String& error)
{
    cr::SamplePtr s;
    cr::Layer srcLayer;
    float zoneEnd = 1.0f;
    {
        const juce::ScopedLock sl (lock);
        auto& pad = sess.banks[(size_t) b].pads[(size_t) p];
        if (! juce::isPositiveAndBelow (l, (int) pad.layers.size()) || ! juce::isPositiveAndBelow (z, (int) pad.layers[(size_t) l].zones.size()))
        { error = "Nothing to slice."; return false; }
        s = pad.layers[(size_t) l].zones[(size_t) z].sample;
        zoneEnd = pad.layers[(size_t) l].zones[(size_t) z].end;
        auto& L = pad.layers[(size_t) l];
        srcLayer.name = L.name; srcLayer.p = L.p; srcLayer.lfo = L.lfo; srcLayer.modEnv = L.modEnv; srcLayer.step = L.step; srcLayer.mods = L.mods;
    }
    if (! s) { error = "Nothing to slice."; return false; }
    std::vector<float> pts = slicePoints;
    std::sort (pts.begin(), pts.end());
    if (pts.empty()) { error = "Add some slices first (tap the ruler above the waveform, or use Auto-slice)."; return false; }
    if (target == SliceTarget::files) return saveSliceFiles (b, p, l, z, pts, error);

    const double total = s->lengthSeconds();
    const int count = (int) pts.size();
    auto sliceEnd = [&] (int i) { return i + 1 < count ? pts[(size_t) i + 1] : zoneEnd; };
    auto configure = [&] (cr::Layer& layer, int i)
    {
        layer.p.trigger = cr::TriggerMode::oneShot;
        layer.p.liveStretch = false;
        const double len = (sliceEnd (i) - pts[(size_t) i]) * total;
        if (fadeInMs > 0.0f || fadeOutMs > 0.0f)
        {
            layer.p.envOn = true;
            layer.p.envMode = cr::EnvMode::ahd;
            layer.p.attack = juce::jmax (0.0005f, fadeInMs / 1000.0f);
            layer.p.decay = juce::jmax (0.005f, fadeOutMs / 1000.0f);
            layer.p.hold = (float) juce::jmax (0.0, len - layer.p.attack - layer.p.decay);
        }
    };

    if (target == SliceTarget::pads)
    {
        const int db = juce::jlimit (0, cr::numBanks - 1, destBank);
        createBank (db);
        for (int i = 0; i < count && startPadOrKey + i < cr::numPads; ++i) checkpointPad (db, startPadOrKey + i);
        const juce::ScopedLock sl (lock);
        auto& bank = sess.banks[(size_t) db];
        for (int i = 0; i < count && startPadOrKey + i < cr::numPads; ++i)
        {
            const int dp = startPadOrKey + i;
            auto& pad = bank.pads[(size_t) dp];
            for (auto& v : voices) if (v.active && v.bank == db && v.pad == dp) { v.active = false; v.sample.reset(); v.stretched.reset(); }
            for (auto& ly : pad.layers) for (auto& zz : ly.zones) retire (zz.sample);
            pad.layers.clear();
            cr::Layer layer = makeDefaultLayer();
            layer.p = srcLayer.p; layer.lfo = srcLayer.lfo; layer.modEnv = srcLayer.modEnv; layer.step = srcLayer.step; layer.mods = srcLayer.mods;
            layer.name = s->name + " #" + juce::String (i + 1);
            cr::Zone zone;
            zone.sample = s;
            zone.start = pts[(size_t) i];
            zone.end = sliceEnd (i);
            zone.loopStart = zone.start; zone.loopEnd = zone.end;
            layer.zones.push_back (zone);
            configure (layer, i);
            pad.layers.push_back (std::move (layer));
            pad.name = "Slice " + juce::String (i + 1);
            pad.choke = chokeGroupNum;
            pad.keysMode = false;
        }
    }
    else
    {
        checkpointPad (b, p);
        const juce::ScopedLock sl (lock);
        auto& pad = sess.banks[(size_t) b].pads[(size_t) p];
        for (auto& v : voices) if (v.active && v.bank == b && v.pad == p) { v.active = false; v.sample.reset(); v.stretched.reset(); }
        if (target == SliceTarget::singleLayer)
        {
            auto& layer = pad.layers[(size_t) l];
            layer.zones.clear();
            for (int i = 0; i < count && startPadOrKey + i < 128; ++i)
            {
                cr::Zone zone;
                zone.sample = s;
                zone.start = pts[(size_t) i]; zone.end = sliceEnd (i);
                zone.loopStart = zone.start; zone.loopEnd = zone.end;
                zone.rootKey = zone.lowKey = zone.highKey = startPadOrKey + i;
                zone.pitchTrack = false;
                layer.zones.push_back (zone);
            }
            layer.zones.front().slices = pts;
            configure (layer, 0);
            layer.p.envMode = cr::EnvMode::ahdsr;
            layer.p.hold = 0.0f; layer.p.sustain = 1.0f; layer.p.release = juce::jmax (0.005f, fadeOutMs / 1000.0f);
        }
        else
        {
            std::vector<cr::Layer> layers;
            for (int i = 0; i < count && startPadOrKey + i < 128; ++i)
            {
                cr::Layer layer = makeDefaultLayer();
                layer.p = srcLayer.p; layer.lfo = srcLayer.lfo; layer.modEnv = srcLayer.modEnv; layer.step = srcLayer.step; layer.mods = srcLayer.mods;
                layer.name = "Slice " + juce::String (i + 1);
                cr::Zone zone;
                zone.sample = s;
                zone.start = pts[(size_t) i]; zone.end = sliceEnd (i);
                zone.loopStart = zone.start; zone.loopEnd = zone.end;
                zone.rootKey = zone.lowKey = zone.highKey = startPadOrKey + i;
                zone.pitchTrack = false;
                layer.zones.push_back (zone);
                configure (layer, i);
                layers.push_back (std::move (layer));
            }
            for (auto& ly : pad.layers) for (auto& f : ly.fx) if (f.plugin) retirePlugin (std::move (f.plugin));
            pad.layers = std::move (layers);
        }
        pad.keysMode = true;
    }

    if (createPattern)
    {
        const int pb = target == SliceTarget::pads ? juce::jlimit (0, cr::numBanks - 1, destBank) : b;
        const double bpm = srcLayer.p.sampleBpm;
        const double startSec = pts.front() * total;
        const double totalBeats = (zoneEnd * total - startSec) * bpm / 60.0;
        const int lenTicks = juce::jmax (cr::barTicks(), (int) std::ceil (totalBeats * cr::ppq / cr::barTicks()) * cr::barTicks());
        const int pi = addPattern (pb, s->name + " slices", lenTicks);
        const juce::ScopedLock sl (lock);
        auto& pat = sess.banks[(size_t) pb].patterns[(size_t) pi];
        for (int i = 0; i < count && startPadOrKey + i < 128; ++i)
        {
            cr::Note note;
            note.pad = target == SliceTarget::pads ? startPadOrKey + i : p;
            note.key = target == SliceTarget::pads ? -1 : startPadOrKey + i;
            note.start = (int) std::round ((pts[(size_t) i] * total - startSec) * bpm / 60.0 * cr::ppq);
            note.length = juce::jmax (cr::ppq / 8, (int) std::round ((sliceEnd (i) - pts[(size_t) i]) * total * bpm / 60.0 * cr::ppq));
            note.velocity = 0.8f;
            if (note.start < lenTicks) pat.notes.push_back (note);
        }
    }
    sendChangeMessage();
    return true;
}

// ============================================================================
// Inserts, layer FX, plugin instruments
// ============================================================================
void Engine::setInsertBuiltin (std::vector<cr::InsertSlot>& chain, int slot, const juce::String& type)
{
    auto fx = createBuiltinEffect (type);
    if (! fx) return;
    fx->prepare (sampleRate, maxBlock);
    fx->setTempo (sess.bpm);
    const juce::ScopedLock sl (lock);
    if (slot < 0 || slot >= (int) chain.size())
    {
        if (chain.capacity() == chain.size()) chain.reserve (chain.size() + 8);
        cr::InsertSlot s;
        s.builtin = std::move (fx);
        chain.push_back (std::move (s));
    }
    else
    {
        auto& s = chain[(size_t) slot];
        if (s.plugin) retirePlugin (std::move (s.plugin));
        s.builtin = std::move (fx);
        s.bypass = false;
    }
}

bool Engine::setInsertPlugin (std::vector<cr::InsertSlot>& chain, int slot, const juce::PluginDescription& desc, juce::String& error)
{
    auto inst = plugins.create (desc, sampleRate, maxBlock, error);
    if (! inst) return false;
    inst->enableAllBuses();
    inst->prepareToPlay (sampleRate, maxBlock);
    {
        const juce::ScopedLock sl (lock);
        if (slot < 0 || slot >= (int) chain.size())
        {
            cr::InsertSlot s;
            s.plugin = std::move (inst);
            chain.push_back (std::move (s));
        }
        else
        {
            auto& s = chain[(size_t) slot];
            if (s.plugin) retirePlugin (std::move (s.plugin));
            s.builtin.reset();
            s.plugin = std::move (inst);
            s.bypass = false;
        }
    }
    updateLatencyCompensation();
    return true;
}

void Engine::removeInsert (std::vector<cr::InsertSlot>& chain, int slot)
{
    {
        const juce::ScopedLock sl (lock);
        if (! juce::isPositiveAndBelow (slot, (int) chain.size())) return;
        if (chain[(size_t) slot].plugin) retirePlugin (std::move (chain[(size_t) slot].plugin));
        chain.erase (chain.begin() + slot);
    }
    updateLatencyCompensation();
}

void Engine::moveInsert (std::vector<cr::InsertSlot>& chain, int from, int to)
{
    const juce::ScopedLock sl (lock);
    if (! juce::isPositiveAndBelow (from, (int) chain.size()) || ! juce::isPositiveAndBelow (to, (int) chain.size()) || from == to) return;
    auto s = std::move (chain[(size_t) from]);
    chain.erase (chain.begin() + from);
    chain.insert (chain.begin() + to, std::move (s));
}

void Engine::setLayerFx (int b, int p, int l, int slot, const juce::String& type)
{
    auto fx = createBuiltinEffect (type);
    if (! fx) return;
    fx->prepare (sampleRate, maxBlock);
    const juce::ScopedLock sl (lock);
    auto& pad = sess.banks[(size_t) b].pads[(size_t) p];
    if (! juce::isPositiveAndBelow (l, (int) pad.layers.size()) || ! juce::isPositiveAndBelow (slot, cr::numLayerFx)) return;
    auto& s = pad.layers[(size_t) l].fx[(size_t) slot];
    if (s.plugin) retirePlugin (std::move (s.plugin));
    s.builtin = std::move (fx);
    s.bypass = false;
}

bool Engine::setLayerFxPlugin (int b, int p, int l, int slot, const juce::PluginDescription& desc, juce::String& error)
{
    auto inst = plugins.create (desc, sampleRate, maxBlock, error);
    if (! inst) return false;
    inst->enableAllBuses();
    inst->prepareToPlay (sampleRate, maxBlock);
    const juce::ScopedLock sl (lock);
    auto& pad = sess.banks[(size_t) b].pads[(size_t) p];
    if (! juce::isPositiveAndBelow (l, (int) pad.layers.size()) || ! juce::isPositiveAndBelow (slot, cr::numLayerFx)) return false;
    auto& s = pad.layers[(size_t) l].fx[(size_t) slot];
    if (s.plugin) retirePlugin (std::move (s.plugin));
    s.builtin.reset();
    s.plugin = std::move (inst);
    s.bypass = false;
    return true;
}

void Engine::clearLayerFx (int b, int p, int l, int slot)
{
    const juce::ScopedLock sl (lock);
    auto& pad = sess.banks[(size_t) b].pads[(size_t) p];
    if (! juce::isPositiveAndBelow (l, (int) pad.layers.size()) || ! juce::isPositiveAndBelow (slot, cr::numLayerFx)) return;
    auto& s = pad.layers[(size_t) l].fx[(size_t) slot];
    if (s.plugin) retirePlugin (std::move (s.plugin));
    s.builtin.reset();
    s.bypass = false;
}

bool Engine::setPadInstrument (int b, int p, const juce::PluginDescription& desc, juce::String& error)
{
    auto inst = plugins.create (desc, sampleRate, maxBlock, error);
    if (! inst) return false;
    inst->enableAllBuses();
    inst->prepareToPlay (sampleRate, maxBlock);
    createBank (b);
    {
        const juce::ScopedLock sl (lock);
        auto& pad = sess.banks[(size_t) b].pads[(size_t) p];
        if (pad.instrument) retirePlugin (std::move (pad.instrument));
        pad.instrument = std::move (inst);
        pad.keysMode = true;
        if (pad.name.isEmpty() || pad.layers.empty()) pad.name = desc.name;
    }
    notifyStructureChanged();
    updateLatencyCompensation();
    sendChangeMessage();
    return true;
}

void Engine::clearPadInstrument (int b, int p)
{
    {
        const juce::ScopedLock sl (lock);
        auto& pad = sess.banks[(size_t) b].pads[(size_t) p];
        if (pad.instrument) retirePlugin (std::move (pad.instrument));
    }
    notifyStructureChanged();
    updateLatencyCompensation();
    sendChangeMessage();
}
