// Headless engine tests: cmake -DCRATE_BUILD_TESTS=ON, then run CrateTests.
#include <JuceHeader.h>
#include "../Source/Engine.h"
#include "../Source/Stretch.h"
#include "../Source/FileUtils.h"

namespace
{
int failures = 0, checks = 0;
void check (bool ok, const juce::String& what)
{
    ++checks;
    if (! ok) { ++failures; std::cout << "  FAIL: " << what << std::endl; }
    else std::cout << "  ok:   " << what << std::endl;
}

constexpr double sr = 48000.0;
constexpr int block = 256;

juce::File tempDir()
{
    static auto d = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("CrateTests");
    return d;
}

juce::File makeWav (const juce::String& name, double freq, double seconds, float amp = 0.8f, bool clickEveryBeat = false, double bpm = 120.0)
{
    auto f = tempDir().getChildFile (name + ".wav");
    juce::AudioBuffer<float> b (2, (int) (seconds * sr));
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        float v = amp * (float) std::sin (juce::MathConstants<double>::twoPi * freq * i / sr);
        if (clickEveryBeat)
        {
            const int beatLen = (int) (60.0 / bpm * sr);
            v = (i % beatLen) < 2000 ? amp * (float) std::sin (juce::MathConstants<double>::twoPi * 200.0 * i / sr) : 0.0f;
        }
        b.setSample (0, i, v);
        b.setSample (1, i, v);
    }
    if (auto w = createWavWriter (f, sr, 2, 24)) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    return f;
}

juce::AudioBuffer<float> render (Engine& e, double seconds)
{
    const int total = (int) (seconds * sr);
    juce::AudioBuffer<float> out (2, total), blk (2, block);
    int done = 0;
    while (done < total)
    {
        const int n = juce::jmin (block, total - done);
        e.renderOffline (blk, n);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, done, blk, c, 0, n);
        done += n;
    }
    return out;
}

// counts note starts: silence -> sound transitions
int countOnsets (const juce::AudioBuffer<float>& b, float thr = 0.02f, int minGap = 1200)
{
    int count = 0, lastOn = -minGap * 2;
    float env = 0.0f;
    bool on = false;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        env = juce::jmax (std::abs (b.getSample (0, i)), env * 0.995f);
        if (! on && env > thr && i - lastOn > minGap) { ++count; on = true; lastOn = i; }
        else if (on && env < thr * 0.3f) on = false;
    }
    return count;
}

int zeroCrossings (const juce::AudioBuffer<float>& b, int from, int to)
{
    int z = 0;
    for (int i = from + 1; i < to; ++i) if ((b.getSample (0, i - 1) <= 0.0f) != (b.getSample (0, i) <= 0.0f)) ++z;
    return z;
}

void pumpMessages (int ms)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    while (juce::Time::getMillisecondCounter() < end) juce::Thread::sleep (10);
}
} // namespace

int main (int argc, char** argv)
{
    {
        juce::StringArray args;
        for (int i = 1; i < argc; ++i) args.add (argv[i]);
        if (PluginManager::isScannerCommandLine (args))
        {
            juce::ScopedJuceInitialiser_GUI init;
            return PluginManager::runScannerProcess (args);
        }
    }
    juce::ScopedJuceInitialiser_GUI juceInit;
    tempDir().deleteRecursively();
    tempDir().createDirectory();
    auto sine = makeWav ("sine440", 440.0, 1.0);
    auto loop = makeWav ("loop", 0.0, 2.0, 0.8f, true, 120.0);

    auto enginePtr = std::make_unique<Engine> (false);
    Engine& e = *enginePtr;
    e.prepareOffline (sr, block);
    e.behaviours.defaultGainDb = 0.0f;

    std::cout << "Pads and voices" << std::endl;
    {
        check (e.loadSampleToPad (sine, 0, 0), "load sample onto pad");
        e.padDown (0, 0, 1.0f);
        auto out = render (e, 0.2);
        check (out.getMagnitude (0, out.getNumSamples()) > 0.3f, "pad plays audio");
        e.padUp (0, 0);
        render (e, 1.2);
        auto quiet = render (e, 0.1);
        check (quiet.getMagnitude (0, quiet.getNumSamples()) < 0.001f, "one-shot ends at sample end");
    }

    std::cout << "Keys mode pitch" << std::endl;
    {
        e.padDown (0, 0, 1.0f, 72);
        auto out = render (e, 0.3);
        e.padUp (0, 0, 72);
        const int zc = zeroCrossings (out, 2000, 2000 + 4800);   // 0.1 s
        check (std::abs (zc - 176) < 8, "C4 plays an octave up (" + juce::String (zc) + " crossings, want ~176)");
        render (e, 1.0);
    }

    std::cout << "Choke group" << std::endl;
    {
        e.loadSampleToPad (sine, 0, 1);
        {
            const juce::ScopedLock sl (e.getLock());
            e.session().banks[0].pads[0].choke = 1;
            e.session().banks[0].pads[1].choke = 1;
            e.session().banks[0].pads[1].layers[0].p.gainDb = -60.0f;
        }
        e.padDown (0, 0, 1.0f);
        render (e, 0.05);
        e.padDown (0, 1, 1.0f);
        auto out = render (e, 0.1);
        check (out.getMagnitude (0, 2400, 2400) < 0.01f, "choked pad stops");
        {
            const juce::ScopedLock sl (e.getLock());
            e.session().banks[0].pads[0].choke = 0;
            e.session().banks[0].pads[1].choke = 0;
        }
        render (e, 1.2);
    }

    std::cout << "Velocity zones" << std::endl;
    {
        auto low = makeWav ("low", 220.0, 0.5);
        e.loadSampleToPad (low, 0, 2);
        e.loadSampleToPad (sine, 0, 2, Engine::LoadMethod::addToLayer);
        {
            const juce::ScopedLock sl (e.getLock());
            auto& zones = e.session().banks[0].pads[2].layers[0].zones;
            check (zones.size() == 2, "second zone added to the layer");
            zones[0].highVel = 63;
            zones[1].lowVel = 64;
        }
        e.padDown (0, 2, 0.3f);
        auto soft = render (e, 0.2);
        e.padUp (0, 2);
        render (e, 0.6);
        e.padDown (0, 2, 1.0f);
        auto hard = render (e, 0.2);
        render (e, 1.1);
        const int zs = zeroCrossings (soft, 2000, 6800), zh = zeroCrossings (hard, 2000, 6800);
        check (zs < zh && std::abs (zh - 2 * zs) < 10, "velocity picks the zone (" + juce::String (zs) + " vs " + juce::String (zh) + ")");
    }

    std::cout << "Pattern playback" << std::endl;
    {
        e.setBpm (120.0);
        {
            const juce::ScopedLock sl (e.getLock());
            auto& bank = e.session().banks[0];
            bank.patterns[0].notes.clear();
            for (int i = 0; i < 4; ++i) bank.patterns[0].notes.push_back ({ 0, -1, i * cr::ppq, cr::ppq / 4, 1.0f });
            bank.pads[0].layers[0].p.trigger = cr::TriggerMode::hold;
        }
        e.launchPattern (0, 0);
        e.play();
        auto out = render (e, 2.0);   // one bar at 120
        e.stop();
        render (e, 0.2);
        check (countOnsets (out) == 4, "4 notes in one bar (" + juce::String (countOnsets (out)) + ")");
    }

    std::cout << "Note repeat" << std::endl;
    {
        e.repeatSlot = 3;   // 1/16
        e.padDown (0, 0, 1.0f);
        auto out = render (e, 1.0);   // 2 beats = 8 sixteenths
        e.padUp (0, 0);
        e.repeatSlot = -1;
        render (e, 0.5);
        const int n = countOnsets (out, 0.02f, 600);
        check (n >= 7 && n <= 9, "repeat retriggers every 1/16 (" + juce::String (n) + ")");
    }

    std::cout << "Song clips loop their pattern" << std::endl;
    {
        {
            const juce::ScopedLock sl (e.getLock());
            auto& bank = e.session().banks[0];
            bank.patterns[0].notes = { { 0, -1, 0, cr::ppq / 4, 1.0f } };
            bank.patterns[0].length = cr::barTicks();
            e.session().song = { { 0, 0, 0, 2 * cr::barTicks() } };
        }
        e.playSong();
        e.setPosition (0);
        e.play();
        auto out = render (e, 4.2);
        e.stop();
        render (e, 0.2);
        check (countOnsets (out) == 2, "2-bar clip plays the 1-bar pattern twice (" + juce::String (countOnsets (out)) + ")");
    }

    std::cout << "Scenes launch on the bar" << std::endl;
    {
        e.addPattern (0, "B");
        {
            const juce::ScopedLock sl (e.getLock());
            auto& bank = e.session().banks[0];
            bank.patterns[1].notes = { { 0, -1, 0, 24, 1.0f }, { 0, -1, 2 * cr::ppq, 24, 1.0f } };
        }
        e.launchPattern (0, 0);
        e.captureScene (0);
        e.launchPattern (0, 1);
        e.captureScene (1);
        e.launchScene (0);
        e.play();
        render (e, 0.5);
        e.launchScene (1);
        check (e.session().banks[0].source == cr::TrackSource::pattern && e.session().banks[0].playingPattern == 0, "scene change waits for the bar");
        render (e, 1.6);
        check (e.session().banks[0].playingPattern == 1, "scene 2 playing after the bar line");
        e.stop();
        render (e, 0.2);
    }

    std::cout << "Live recording with quantize" << std::endl;
    {
        const int pi = e.addPattern (0, "Rec");
        e.session().preRollBars = 0;
        e.session().quantize = 2;
        e.setPosition (0);
        e.setRecording (true);
        render (e, 0.26);   // a bit past beat 1 (0.25 s = 1/2 beat)
        e.padDown (0, 3, 0.9f);
        render (e, 0.1);
        e.padUp (0, 3);
        render (e, 0.4);
        e.stop();
        const auto& notes = e.session().banks[0].patterns[(size_t) pi].notes;
        check (notes.size() == 1, "one note recorded");
        if (! notes.empty()) check (notes[0].start % (cr::ppq / 4) == 0 && notes[0].start == cr::ppq / 2, "start quantised to 1/16 (" + juce::String (notes[0].start) + ")");
        bool inSong = false;
        for (auto& c : e.session().song) if (c.bank == 0 && c.pattern == pi && c.start == 0) inSong = true;
        check (inSong, "the recorded pattern is placed in the song where it was recorded");
        bool inScene = false;
        for (auto& sc : e.session().scenes) if (sc.slots[0] == pi) inScene = true;
        check (inScene, "the recorded pattern is placed in the scene grid");
        check (e.session().banks[0].source == cr::TrackSource::song, "after stopping, the bank follows the song again");
        e.session().preRollBars = 1;
    }

    std::cout << "Mixer, automation, macros" << std::endl;
    {
        {
            const juce::ScopedLock sl (e.getLock());
            e.session().song.clear();
            e.session().banks[0].pads[0].layers[0].p.trigger = cr::TriggerMode::oneShot;
        }
        e.setParamValue (cr::ParamAddress::parse ("bank/0/vol"), 0.0f);
        e.padDown (0, 0, 1.0f);
        auto out = render (e, 0.1);
        check (out.getMagnitude (0, out.getNumSamples()) < 0.0001f, "bank volume 0 silences the bank");
        render (e, 1.0);
        e.bindMacro (0, "bank/0/vol");
        e.setMacro (0, 1.0f);
        check (std::abs (e.session().banks[0].strip.volume - 1.25f) < 0.001f, "macro drives its parameter");
        e.setMacro (0, 0.64f);
        juce::Thread::sleep (700);   // let the 'user is touching this knob' window expire
        auto* lane = e.findOrCreateLane ("bank/0/vol", true, 0);
        lane->points = { { 0, 0.0f }, { cr::barTicks(), 1.0f } };
        e.playSong();
        e.setPosition (0);
        e.play();
        render (e, 1.0);
        const float v = e.session().banks[0].strip.volume;
        check (v > 0.5f && v < 0.75f, "song automation moves the volume (" + juce::String (v) + ")");
        e.stop();
        e.session().songAutomation.clear();
        e.setParamValue (cr::ParamAddress::parse ("bank/0/vol"), 0.8f);
        auto a = cr::ParamAddress::parse ("lfx/3/4/1/2/5");
        check (a.toString() == "lfx/3/4/1/2/5" && a.owner == cr::ParamAddress::Owner::layerFx, "param address round trip");
    }

    std::cout << "Effects" << std::endl;
    {
        for (auto& type : getBuiltinEffectTypes())
        {
            auto fx = createBuiltinEffect (type);
            fx->prepare (sr, block);
            juce::AudioBuffer<float> b (2, block);
            bool finite = true;
            for (int k = 0; k < 50; ++k)
            {
                for (int i = 0; i < block; ++i) { const float x = (float) std::sin (i * 0.05 + k); b.setSample (0, i, x); b.setSample (1, i, x); }
                fx->process (b, block);
                for (int i = 0; i < block; ++i) if (! std::isfinite (b.getSample (0, i))) finite = false;
            }
            check (finite, type + " is stable");
        }
    }

    std::cout << "Sample editing" << std::endl;
    {
        auto len = [&e] { return e.session().banks[0].pads[0].layers[0].zones[0].sample->numFrames; };
        const auto before = len();
        check (e.editSample (0, 0, 0, 0, Engine::SampleEdit::cut, 0.0f, 0.5f), "cut");
        check (std::abs ((double) len() - before * 0.5) < 2, "cut halves the length");
        check (e.editSample (0, 0, 0, 0, Engine::SampleEdit::paste, 0.0f, 0.0f), "paste");
        check (std::abs ((double) len() - before) < 3, "paste restores the length");
        e.editSample (0, 0, 0, 0, Engine::SampleEdit::gain, 0.0f, 1.0f, -12.0);
        e.editSample (0, 0, 0, 0, Engine::SampleEdit::normalize, 0.0f, 1.0f);
        check (std::abs (e.session().banks[0].pads[0].layers[0].zones[0].sample->buffer.getMagnitude (0, 0, (int) len()) - 0.98f) < 0.01f, "normalize to -0.2 dB");
        e.editSample (0, 0, 0, 0, Engine::SampleEdit::trim, 0.25f, 0.75f);
        check (std::abs ((double) len() - before * 0.5) < 3, "trim keeps the selection");
        check (e.undo(), "undo the trim");
        check (std::abs ((double) len() - before) < 3, "undo restores the sample");
        check (e.redo(), "redo the trim");
        check (std::abs ((double) len() - before * 0.5) < 3, "redo trims again");
        e.undo();
        {
            auto& z = e.session().banks[0].pads[0].layers[0].zones[0];
            z.slices = { 0.1f, 0.3f, 0.6f, 0.9f };
            z.start = 0.2f; z.end = 0.8f;
        }
        e.editSample (0, 0, 0, 0, Engine::SampleEdit::trim, 0.2f, 0.8f);
        {
            const auto& z = e.session().banks[0].pads[0].layers[0].zones[0];
            check (z.slices.size() == 2 && std::abs (z.slices[0] - 1.0f / 6.0f) < 0.002f && std::abs (z.slices[1] - 2.0f / 3.0f) < 0.002f,
                   "trim to S/E keeps the chops inside, moved to the new length");
            check (z.start == 0.0f && z.end == 1.0f, "trim resets S/E to the new ends");
        }
        e.undo();
        e.editSample (0, 0, 0, 0, Engine::SampleEdit::stretch, 0.0f, 1.0f, 2.0);
        check (std::abs ((double) len() - before * 2.0) < 10, "time stretch x2");
        e.undo();

        juce::AudioBuffer<float> b (2, 48000);
        for (int i = 0; i < 48000; ++i) { const float x = (float) std::sin (juce::MathConstants<double>::twoPi * 440.0 * i / sr); b.setSample (0, i, x); b.setSample (1, i, x); }
        auto up = stretch::pitchShift (b, 12.0, sr);
        const int zc = zeroCrossings (up, 4800, 9600);
        check (up.getNumSamples() == 48000 && std::abs (zc - 176) < 8, "pitch shift +12 keeps length, doubles pitch (" + juce::String (zc) + ")");
    }

    std::cout << "Slicing" << std::endl;
    {
        e.loadSampleToPad (loop, 0, 10);
        {
            const juce::ScopedLock sl (e.getLock());
            e.session().banks[0].pads[10].layers[0].p.sampleBpm = 120.0f;
        }
        auto split = e.autoSlice (0, 10, 0, 0, 0, 4, 0.5f);
        check (split.size() == 4, "split into 4");
        auto grid = e.autoSlice (0, 10, 0, 0, 1, 2, 0.5f);
        check (grid.size() == 4, "grid 1/4 on a 4-beat loop gives 4 slices (" + juce::String ((int) grid.size()) + ")");
        auto trans = e.autoSlice (0, 10, 0, 0, 2, 0, 0.6f);
        check (trans.size() == 4, "transients found on each beat (" + juce::String ((int) trans.size()) + ")");
        juce::String err;
        const int patsBefore = (int) e.session().banks[1].patterns.size();
        check (e.applySlices (0, 10, 0, 0, split, Engine::SliceTarget::pads, 1, 0, 1, true, 2.0f, 5.0f, err), "slice to pads " + err);
        int withSound = 0;
        for (int p = 0; p < 4; ++p) if (e.session().banks[1].pads[(size_t) p].hasSound()) ++withSound;
        check (withSound == 4 && e.session().banks[1].pads[1].choke == 1, "4 pads filled in bank B1 with choke group");
        check ((int) e.session().banks[1].patterns.size() == patsBefore + 1 && e.session().banks[1].patterns.back().notes.size() == 4, "slice pattern created");
        check (e.applySlices (0, 10, 0, 0, split, Engine::SliceTarget::singleLayer, 0, 48, 0, false, 0.0f, 0.0f, err), "slice to single layer");
        check (e.session().banks[0].pads[10].layers[0].zones.size() == 4 && e.session().banks[0].pads[10].keysMode, "4 zones over keys");
    }

    std::cout << "Disk streaming and live stretch" << std::endl;
    {
        auto s = e.loadSample (sine, true);
        check (s && s->isStreamed(), "WAV memory-mapped for streaming");
        e.setPadSample (0, 20, s);
        e.padDown (0, 20, 1.0f);
        auto out = render (e, 0.2);
        check (out.getMagnitude (0, out.getNumSamples()) > 0.3f, "streamed pad plays");
        render (e, 1.0);
        e.loadSampleToPad (loop, 0, 21);
        {
            const juce::ScopedLock sl (e.getLock());
            auto& l = e.session().banks[0].pads[21].layers[0];
            l.p.liveStretch = true;
            l.p.sampleBpm = 120.0f;
        }
        e.setBpm (60.0);
        pumpMessages (1500);
        auto zs = e.session().banks[0].pads[21].layers[0].zones[0].sample;
        check (zs->stretched != nullptr && std::abs (zs->stretched->getNumSamples() - 2 * zs->numFrames) < 50, "live stretch built at half tempo");
        e.setBpm (120.0);
    }

    std::cout << "Pads: copy, paste, clear, undo" << std::endl;
    {
        e.copyPad (0, 0);
        e.pastePad (0, 30);
        check (e.session().banks[0].pads[30].hasSound(), "paste pad");
        e.clearPad (0, 30);
        check (! e.session().banks[0].pads[30].hasSound(), "clear pad");
        e.undo();
        check (e.session().banks[0].pads[30].hasSound(), "undo clear");
        e.redo();
        check (! e.session().banks[0].pads[30].hasSound(), "redo clear");
    }

    std::cout << "Files" << std::endl;
    {
        juce::String err;
        {
            const juce::ScopedLock sl (e.getLock());
            e.session().banks[0].name = "Drums";
        }
        e.findOrCreateLane ("pad/0/0/pan", true, 0)->points = { { 0, 0.2f }, { 96, 0.8f } };
        auto sessFile = tempDir().getChildFile ("Test.crsession");
        check (e.saveSession (sessFile, err), "save session " + err);
        const int pats = (int) e.session().banks[0].patterns.size();
        const int scenes = (int) e.session().scenes.size();
        e.newSession();
        check (! e.session().banks[0].pads[0].hasSound(), "new session is empty");
        check (e.loadSession (sessFile, err), "load session " + err);
        check (e.session().banks[0].name == "Drums" && e.session().banks[0].pads[0].hasSound(), "bank and samples restored");
        check ((int) e.session().banks[0].patterns.size() == pats && (int) e.session().scenes.size() == scenes, "patterns and scenes restored");
        check (e.session().macros[0].bindings.size() == 1 && e.session().songAutomation.size() == 1, "macros and automation restored");
        check (e.session().banks[1].pads[2].hasSound(), "sliced pads restored");

        auto bankFile = tempDir().getChildFile ("Kit.crbank");
        check (e.saveBankPreset (0, bankFile, err), "save bank preset");
        check (e.loadBankPreset (5, bankFile, err), "load bank preset into F1 " + err);
        check (e.session().banks[5].pads[0].hasSound() && e.session().banks[5].name == "Drums", "bank preset contents");

        auto mid = tempDir().getChildFile ("p.mid");
        check (e.exportPatternMidi (0, 0, mid, err), "export MIDI");
        const int n0 = (int) e.session().banks[0].patterns[0].notes.size();
        check (e.importMidi (mid, 2, err), "import MIDI");
        check ((int) e.session().banks[2].patterns.back().notes.size() == n0, "MIDI round trip keeps notes");

        auto pack = tempDir().getChildFile ("Test.crpack");
        check (e.exportPack (pack, "Test", "Me", err), "export pack " + err);
        check (e.installPack (pack, err), "install pack " + err);
    }

    std::cout << "Export audio" << std::endl;
    {
        juce::String err;
        auto wav = tempDir().getChildFile ("mix.wav");
        ExportOptions o;
        o.startTick = 0;
        o.endTick = cr::barTicks();
        o.includeTail = false;
        o.bitDepth = 16;
        {
            const juce::ScopedLock sl (e.getLock());
            e.session().song = { { 0, 0, 0, cr::barTicks() } };
        }
        e.playSong();
        check (e.exportAudio (wav, o, err), "export " + err);
        std::unique_ptr<juce::AudioFormatReader> r (e.formatManager.createReaderFor (wav));
        check (r != nullptr && std::abs ((double) r->lengthInSamples - 2.0 * sr) < 600, "export length is one bar");
        if (r)
        {
            juce::AudioBuffer<float> b (2, (int) r->lengthInSamples);
            r->read (&b, 0, b.getNumSamples(), 0, true, true);
            check (b.getMagnitude (0, b.getNumSamples()) > 0.05f, "export has audio");
        }
        o.stems = true;
        check (e.exportAudio (tempDir().getChildFile ("stems.wav"), o, err), "export stems");
        check (tempDir().getChildFile ("stems - Drums.wav").existsAsFile(), "stem file per track");
    }

    std::cout << "Channel EQ" << std::endl;
    {
        auto sineLevelDb = [sr] (cr::ChannelEqSettings q, double freq)
        {
            cr::ChannelEqDsp dsp;
            const int len = 48000;
            juce::AudioBuffer<float> b (2, len);
            for (int i = 0; i < len; ++i) { const float x = 0.25f * (float) std::sin (juce::MathConstants<double>::twoPi * freq * i / sr); b.setSample (0, i, x); b.setSample (1, i, x); }
            for (int start = 0; start < len; start += 512) { juce::AudioBuffer<float> v (b.getArrayOfWritePointers(), 2, start, juce::jmin (512, len - start)); dsp.process (q, v, v.getNumSamples(), sr); }
            return gainToDb (b.getRMSLevel (0, len / 2, len / 2) / (0.25f / std::sqrt (2.0f)));
        };
        cr::ChannelEqSettings q;
        q.band[2].on = true; q.band[2].freq = 2000.0f; q.band[2].gain = 12.0f; q.band[2].q = 1.0f;
        const float boost = sineLevelDb (q, 2000.0);
        check (std::abs (boost - 12.0f) < 0.3f, "HMF +12 dB bell boosts 2 kHz by 12 dB (" + juce::String (boost, 2) + ")");
        check (std::abs (cr::responseDb (q, 2000.0, sr) - 12.0) < 0.05, "drawn curve matches the audio at the band centre");
        cr::ChannelEqSettings h;
        h.hpfOn = true; h.hpfFreq = 1000.0f; h.hpfSlope = 1;
        const float cut = sineLevelDb (h, 250.0);
        check (cut < -45.0f && cut > -51.0f, "HPF 24 dB/oct at 1 kHz: 2 octaves below is about -48 dB (" + juce::String (cut, 1) + ")");
        check (std::abs (cut - (float) cr::responseDb (h, 250.0, sr)) < 0.5f, "HPF curve matches the audio");
        h.hpfSlope = 2;
        check (sineLevelDb (h, 250.0) < -85.0f, "48 dB/oct slope is twice as steep");
        cr::ChannelEqSettings l;
        l.lpfOn = true; l.lpfFreq = 1000.0f; l.lpfSlope = 0;
        check (std::abs (sineLevelDb (l, 1000.0) + 3.0f) < 0.3f, "LPF 12 dB/oct is -3 dB at the cutoff");
        cr::ChannelEqSettings sh;
        sh.band[0].on = true; sh.band[0].freq = 200.0f; sh.band[0].gain = -9.0f;
        check (std::abs (sineLevelDb (sh, 40.0) + 9.0f) < 0.5f, "LF shelf -9 dB cuts the lows by 9 dB");
        sh.bypassEq = true;
        check (std::abs (sineLevelDb (sh, 40.0)) < 0.1f, "EQ bypass passes the signal untouched");

        // the full channel path: trim, phase, width
        {
            const juce::ScopedLock sl (e.getLock());
            auto& st = e.session().master;
            st.eq.trimDb = -6.0f;
        }
        check (std::abs (e.getParamValue (cr::ParamAddress::parse ("master/trim")) + 6.0f) < 0.001f, "trim is an addressable parameter (MIDI learn / automation)");
        e.setParamValue (cr::ParamAddress::parse ("master/eq2g"), 4.5f, false);
        check (std::abs (e.session().master.eq.band[2].gain - 4.5f) < 0.001f, "EQ gain is an addressable parameter");
        e.session().master.eq = {};
    }

    std::cout << "Stem import" << std::endl;
    {
        // two impulse stems: B's Broadcast WAV stamp is 24007 frames after A's
        auto writeStem = [] (const juce::String& name, juce::int64 timeRef, bool stamp)
        {
            auto f = tempDir().getChildFile (name + ".wav");
            juce::AudioBuffer<float> b (2, 48000);
            b.clear();
            b.setSample (0, 1000, 0.9f); b.setSample (1, 1000, 0.9f);
            std::unordered_map<juce::String, juce::String> meta;
            if (stamp) meta[juce::WavAudioFormat::bwavTimeReference] = juce::String (timeRef);
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
            auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (48000.0).withNumChannels (2).withBitsPerSample (24).withMetadataValues (meta));
            if (w) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            return f;
        };
        const juce::int64 refA = 48000LL * 3600, gap = 24007;
        std::vector<Engine::Stem> stems { e.prepareStem (writeStem ("Kick stem", refA, true)),
                                          e.prepareStem (writeStem ("Snare stem", refA + gap, true)) };
        check (std::abs (stems[0].timeSeconds - 3600.0) < 1.0e-9, "Broadcast WAV time reference is read");
        check (stems[0].sample && stems[0].sample->isStreamed(), "WAV stems stream from disk");
        // mute everything else, then play the two new tracks
        for (auto& b : e.session().banks) b.strip.mute = true;
        for (auto& t : e.session().audioTracks) t.strip.mute = true;
        juce::String err;
        const int before = [&e] { int n = 0; for (auto& t : e.session().audioTracks) if (t.created) ++n; return n; }();
        check (e.addStemTracks (stems, 0, err) == 2 && err.isEmpty(), "one track per stem " + err);
        int after = 0, tA = -1, tB = -1;
        for (int t = 0; t < cr::maxAudioTracks; ++t)
            if (e.session().audioTracks[(size_t) t].created)
            {
                ++after;
                if (e.session().audioTracks[(size_t) t].name == "Kick stem") tA = t;
                if (e.session().audioTracks[(size_t) t].name == "Snare stem") tB = t;
            }
        check (after == before + 2 && tA >= 0 && tB >= 0, "tracks are named after the files");
        if (tA >= 0 && tB >= 0)
        {
            check (e.session().audioTracks[(size_t) tA].clips[0].start == 0, "earliest stem starts at bar 1");
            e.session().audioTracks[(size_t) tA].strip.mute = false;
            e.session().audioTracks[(size_t) tB].strip.mute = false;
            e.stop();
            e.setPosition (0.0);
            e.play();
            auto out = render (e, 1.5);
            e.stop();
            std::vector<int> peaks;
            for (int i = 0; i < out.getNumSamples(); ++i) if (std::abs (out.getSample (0, i)) > 0.3f) peaks.push_back (i);
            const bool ok = peaks.size() == 2 && peaks[1] - peaks[0] == (int) gap;
            check (ok, "stems keep their exact sample distance (" + (peaks.size() == 2 ? juce::String (peaks[1] - peaks[0]) : juce::String ((int) peaks.size()) + " peaks") + ", want 24007)");
            check (! peaks.empty() && peaks[0] == 1000, "first stem plays sample-accurately from bar 1");
        }
        for (auto& b : e.session().banks) b.strip.mute = false;
        for (auto& t : e.session().audioTracks) t.strip.mute = false;
    }

    std::cout << "Loop button" << std::endl;
    {
        // a one-bar pattern with one hit, played in pattern mode for three bars
        {
            const juce::ScopedLock sl (e.getLock());
            auto& b = e.session().banks[0];
            b.patterns.assign (1, cr::Pattern());
            b.patterns[0].length = cr::barTicks();
            cr::Note n; n.pad = 0; n.start = 0; n.length = cr::ppq / 2; n.velocity = 1.0f;
            b.patterns[0].notes.push_back (n);
            b.strip.mute = false;
        }
        auto hitsOver3Bars = [&e] (bool loop)
        {
            e.stop();
            e.session().loopOn = loop;
            e.session().loopStart = 0; e.session().loopEnd = 4 * cr::barTicks();
            e.setPosition (0.0);
            e.launchPattern (0, 0);
            e.play();
            auto out = render (e, 3.0 * 60.0 / e.session().bpm * 4.0 + 0.05);
            e.stop();
            // sound at the start of each bar (first 1/16th, before any tail could matter much)
            const int barLen = (int) (60.0 / e.session().bpm * 4.0 * sr);
            int hits = 0;
            for (int k = 0; k < 3; ++k)
                if (out.getRMSLevel (0, k * barLen, barLen / 16) > 0.01f) ++hits;
            return hits;
        };
        const int off = hitsOver3Bars (false), on = hitsOver3Bars (true);
        check (off == 1, "Loop off: the pattern plays once (" + juce::String (off) + " hits)");
        check (on == 3, "Loop on: the pattern repeats (" + juce::String (on) + " hits)");
        e.session().loopOn = false;
    }

    std::cout << "Bank, pattern and sequence copies" << std::endl;
    {
        juce::String err;
        auto& s0 = e.session();
        s0.banks[0].strip.volume = 0.42f;
        s0.banks[0].strip.eq.band[1].on = true; s0.banks[0].strip.eq.band[1].gain = 5.0f;
        int padsWithSound = 0; for (auto& p : s0.banks[0].pads) if (p.hasSound()) ++padsWithSound;
        const int f = e.duplicateBank (0, err);
        check (f > 0 && err.isEmpty(), "duplicate a bank into the next free bank " + err);
        if (f > 0)
        {
            const auto& nb = e.session().banks[(size_t) f];
            int n = 0; for (auto& p : nb.pads) if (p.hasSound()) ++n;
            check (n == padsWithSound && n > 0, "the duplicate has the same pads and samples (" + juce::String (n) + ")");
            check (std::abs (nb.strip.volume - 0.42f) < 0.001f && nb.strip.eq.band[1].on && std::abs (nb.strip.eq.band[1].gain - 5.0f) < 0.01f, "the duplicate keeps the bank settings (volume, EQ)");
            check (nb.patterns.size() == e.session().banks[0].patterns.size(), "the duplicate has the patterns");
            e.deleteBank (f);
        }
        // pattern copy into another bank
        e.copyPattern (0, 0);
        e.createBank (5);
        const int before = (int) e.session().banks[5].patterns.size();
        const int pp = e.pastePattern (5);
        check (pp == before && e.session().banks[5].patterns[(size_t) pp].notes.size() == e.session().banks[0].patterns[0].notes.size(), "copy a pattern into another bank");
        // sequence copy: the clip and its pattern
        {
            const juce::ScopedLock sl (e.getLock());
            e.session().song = { { 0, 0, 0, cr::barTicks() } };
            e.session().song[0].selected = true;
        }
        check (e.copySongClips() == 1, "copy a song clip");
        const int patsBefore = (int) e.session().banks[0].patterns.size();
        check (e.pasteSongClips (4 * cr::barTicks(), -1) == 1 && e.session().song.size() == 2, "paste it at a new spot");
        const auto& pasted = e.session().song.back();
        check (pasted.start == 4 * cr::barTicks() && pasted.pattern == patsBefore && (int) e.session().banks[0].patterns.size() == patsBefore + 1,
               "the pasted clip gets its own copy of the pattern");
        e.deleteBank (5);
    }

    std::cout << "Resampling" << std::endl;
    {
        // a bank that plays: pad 1 of bank 0, a note every beat for one bar
        {
            const juce::ScopedLock sl (e.getLock());
            auto& b = e.session().banks[0];
            b.strip.mute = false; b.strip.frozen = false;
            b.patterns.assign (1, cr::Pattern());
            for (int k = 0; k < 4; ++k) { cr::Note n; n.pad = 0; n.start = k * cr::ppq; n.length = cr::ppq / 2; n.velocity = 1.0f; b.patterns[0].notes.push_back (n); }
            e.session().song = { { 0, 0, 0, cr::barTicks() } };
        }
        juce::String err;
        BounceOptions bo;
        bo.endTick = cr::barTicks();
        bo.includeTail = false;
        const int t = e.bounceToTrack (0, bo, err);
        check (t >= 0, "bounce a bank to a new audio track " + err);
        if (t >= 0)
        {
            auto& tr = e.session().audioTracks[(size_t) t];
            auto smp = tr.clips.empty() ? nullptr : e.clipToSample (t, 0);
            check (smp != nullptr && smp->buffer.getMagnitude (0, smp->buffer.getNumSamples()) > 0.05f, "the bounce has the bank's audio");
            check (e.session().banks[0].strip.mute, "the source is muted after bouncing");
            check (tr.name.endsWith ("(bounce)"), "the bounce track is named after the source");
            e.deleteAudioTrack (t);
            e.session().banks[0].strip.mute = false;
        }
        bo.freeze = true;
        const int f = e.bounceToTrack (0, bo, err);
        check (f >= 0 && e.isFrozen (0) && e.session().audioTracks[(size_t) f].frozenFrom == 0, "freeze renders the bank and freezes it");
        check (e.unfreeze (0) && ! e.isFrozen (0) && ! e.session().audioTracks[(size_t) juce::jmax (0, f)].created, "unfreeze removes the frozen track and restores the bank");
    }

    // ---- plugin scan survives plugins that crash or hang ----
    if (auto dir = juce::SystemStats::getEnvironmentVariable ("CRATE_TEST_PLUGIN_DIR", {}); dir.isNotEmpty())
    {
        PluginManager pm;
        pm.clearFailedPlugins();
        pm.addSearchFolder (juce::File (dir));
        pm.startScan ({});
        for (int i = 0; i < 2000 && pm.isScanning(); ++i) juce::Thread::sleep (100);
        check (! pm.isScanning(), "plugin scan finished");
        const auto failed = pm.getFailedPlugins();
        bool crash = false, hang = false;
        for (auto& f : failed)
        {
            std::cout << "  failed: " << f.name << " - " << f.reason << std::endl;
            if (f.fileOrId.contains ("Crasher")) crash = true;
            if (f.fileOrId.contains ("Hanger")) hang = true;
        }
        check (crash, "crashing plugin marked failed, scan carried on");
        check (hang, "hanging plugin marked failed, scan carried on");
        pm.clearFailedPlugins();
    }

    std::cout << std::endl << (checks - failures) << " / " << checks << " checks passed" << std::endl;
    tempDir().deleteRecursively();
    return failures == 0 ? 0 : 1;
}
