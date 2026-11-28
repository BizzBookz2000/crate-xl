// Stem separation: split a sample into vocals / drums / bass / other pads.
#include "Engine.h"
#include "FileUtils.h"
#include "StemSplit.h"

cr::SamplePtr Engine::padSample (int b, int p)
{
    if (! juce::isPositiveAndBelow (b, cr::numBanks) || ! juce::isPositiveAndBelow (p, cr::numPads)) return nullptr;
    const juce::ScopedLock sl (lock);
    for (auto& l : sess.banks[(size_t) b].pads[(size_t) p].layers)
        for (auto& z : l.zones)
            if (z.sample) return z.sample;
    return nullptr;
}

bool Engine::renderStems (cr::SamplePtr src, const juce::String& name, StemSet& result, juce::String& error,
                          std::function<bool (float)> progress)
{
    if (src && src->isStreamed()) src = loadSample (src->file, false);
    if (! src || src->isStreamed() || src->numFrames < 64) { error = "There's no audio to separate."; return false; }

    const auto clean = juce::File::createLegalFileName (name.isNotEmpty() ? name : juce::String ("Untitled")).trim();
    auto folder = getSamplesFolder().getChildFile ("Stems").getNonexistentChildFile (clean, {}, true);
    if (! folder.createDirectory()) { error = "Couldn't create " + folder.getFullPathName(); return false; }

    std::array<std::unique_ptr<juce::AudioFormatWriter>, cr::stems::numStems> writers;
    juce::StringArray files;
    for (int s = 0; s < cr::stems::numStems; ++s)
    {
        auto f = folder.getChildFile (clean + " - " + cr::stems::stemName (s) + ".wav");
        writers[(size_t) s] = createWavWriter (f, src->sampleRate, 2, 24);
        if (writers[(size_t) s] == nullptr) { error = "Couldn't write " + f.getFullPathName(); folder.deleteRecursively(); return false; }
        files.add (f.getFullPathName());
    }

    cr::stems::Separator sep;
    bool writeOk = true;
    const bool ok = sep.run (src->buffer, (int) src->numFrames, src->sampleRate,
        [&writers, &writeOk] (int s, const float* l, const float* r, int n)
        {
            const float* ch[2] = { l, r };
            writeOk = writers[(size_t) s]->writeFromFloatArrays (ch, 2, n) && writeOk;
        }, progress);
    for (auto& w : writers) w.reset();   // flush + close
    if (! ok || ! writeOk)
    {
        folder.deleteRecursively();
        error = ok ? "Couldn't write the stem files (disk full?)." : "Cancelled.";
        return false;
    }
    result.name = name;
    result.files = files;
    result.full = src;
    return true;
}

int Engine::placeStemsInNewBank (const StemSet& set, juce::String& error)
{
    const int b = firstFreeBank();
    if (b < 0) { error = "All 128 banks are in use."; return -1; }
    static const juce::uint32 colours[] = { 0xffd9579c, 0xffe07a3f, 0xff7e63d6, 0xff36a7b8, 0xff8a8f98 };
    checkpoint();
    createBank (b);
    for (int s = 0; s < set.files.size() && s < cr::stems::numStems; ++s)
    {
        const juce::File f (set.files[s]);
        // long songs stream from disk so four full-length copies don't sit in memory
        auto smp = loadSample (f, false);
        if (smp && smp->lengthSeconds() > 60.0) if (auto st = loadSample (f, true)) smp = st;
        if (! smp) { error << "Couldn't load " << f.getFileName() << "\n"; continue; }
        smp->name = cr::stems::stemName (s);
        setPadSample (b, s, smp);
    }
    if (set.full)
    {
        auto full = set.full;
        if (full->lengthSeconds() > 60.0 && full->file.existsAsFile() && ! full->edited)
            if (auto st = loadSample (full->file, true)) full = st;
        setPadSample (b, cr::stems::numStems, full);
    }
    {
        const juce::ScopedLock sl (lock);
        auto& bank = sess.banks[(size_t) b];
        bank.name = (set.name.isNotEmpty() ? set.name : juce::String ("Song")) + " stems";
        bank.strip.name = bank.name;
        for (int s = 0; s <= cr::stems::numStems; ++s)
        {
            auto& pad = bank.pads[(size_t) s];
            if (! pad.hasSound()) continue;
            pad.name = s < cr::stems::numStems ? juce::String (cr::stems::stemName (s)) : juce::String ("Full mix");
            pad.colour = juce::Colour (colours[s]);
            for (auto& l : pad.layers) l.name = pad.name;
        }
    }
    selectedBank = b;
    selectedPad = 0;
    notifyStructureChanged();
    sendChangeMessage();
    return b;
}
