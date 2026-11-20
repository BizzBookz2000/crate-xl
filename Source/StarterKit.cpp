// A small original drum kit and two instruments, synthesised on first launch so the app
// makes sound straight away. Written to Library/Samples/Crate Starter Kit.
#include "Views.h"
#include "FileUtils.h"

namespace
{
constexpr double sr = 48000.0;

struct Synth
{
    juce::Random rnd { 1234 };
    float noise() { return rnd.nextFloat() * 2.0f - 1.0f; }
};

juce::AudioBuffer<float> make (double seconds, std::function<float (double t, int i)> fn)
{
    const int n = (int) (seconds * sr);
    juce::AudioBuffer<float> b (2, n);
    for (int i = 0; i < n; ++i)
    {
        const float v = juce::jlimit (-1.0f, 1.0f, fn (i / sr, i));
        b.setSample (0, i, v);
        b.setSample (1, i, v);
    }
    // tiny fade at the end
    b.applyGainRamp (juce::jmax (0, n - 256), juce::jmin (256, n), 1.0f, 0.0f);
    return b;
}

float env (double t, double decay) { return (float) std::exp (-t / decay); }

struct OnePole
{
    float z = 0.0f, a = 0.5f;
    OnePole (float cutoff) { a = (float) std::exp (-juce::MathConstants<double>::twoPi * cutoff / sr); }
    float lp (float x) { z = x * (1.0f - a) + z * a; return z; }
    float hp (float x) { return x - lp (x); }
};

std::vector<std::pair<juce::String, juce::AudioBuffer<float>>> buildKit()
{
    std::vector<std::pair<juce::String, juce::AudioBuffer<float>>> kit;
    Synth s;
    const double twoPi = juce::MathConstants<double>::twoPi;

    { double ph = 0.0; kit.push_back ({ "Kick", make (0.6, [&] (double t, int) { const double f = 45.0 + 110.0 * std::exp (-t / 0.035); ph += twoPi * f / sr; return (float) (std::tanh (1.6 * std::sin (ph)) * env (t, 0.22) + 0.3 * std::exp (-t / 0.002)); }) }); }
    { OnePole hp (900.0f); double ph = 0.0; kit.push_back ({ "Snare", make (0.35, [&] (double t, int) { ph += twoPi * 185.0 / sr; return 0.55f * hp.hp (s.noise()) * env (t, 0.11) + 0.5f * (float) std::sin (ph) * env (t, 0.05); }) }); }
    { OnePole hp (7000.0f); kit.push_back ({ "Closed Hat", make (0.12, [&] (double t, int) { return 0.6f * hp.hp (s.noise()) * env (t, 0.025); }) }); }
    { OnePole hp (6500.0f); kit.push_back ({ "Open Hat", make (0.6, [&] (double t, int) { return 0.5f * hp.hp (s.noise()) * env (t, 0.18); }) }); }
    { OnePole hp (1200.0f); kit.push_back ({ "Clap", make (0.45, [&] (double t, int) {
        const double tt = t < 0.03 ? std::fmod (t, 0.01) : t - 0.02;
        return 0.7f * hp.hp (s.noise()) * env (tt, t < 0.03 ? 0.004 : 0.12); }) }); }
    { double ph = 0.0; OnePole hp (2000.0f); kit.push_back ({ "Rim", make (0.15, [&] (double t, int) { ph += twoPi * 1650.0 / sr; return 0.6f * (float) std::sin (ph) * env (t, 0.012) + 0.3f * hp.hp (s.noise()) * env (t, 0.004); }) }); }
    auto tom = [&] (const char* name, double f0)
    {
        double ph = 0.0;
        kit.push_back ({ name, make (0.6, [&, f0] (double t, int) { const double f = f0 * (1.0 + 0.5 * std::exp (-t / 0.04)); ph += twoPi * f / sr; return 0.8f * (float) std::sin (ph) * env (t, 0.2); }) });
    };
    tom ("Low Tom", 90.0); tom ("Mid Tom", 130.0); tom ("High Tom", 185.0);
    { OnePole hp (4500.0f); kit.push_back ({ "Crash", make (1.8, [&] (double t, int) { return 0.45f * hp.hp (s.noise()) * env (t, 0.6) * (float) (1.0 - std::exp (-t / 0.002)); }) }); }
    { double a = 0.0, b = 0.0; OnePole lp (3000.0f); kit.push_back ({ "Cowbell", make (0.4, [&] (double t, int) {
        a += 540.0 / sr; b += 800.0 / sr;
        const float sq = (std::fmod (a, 1.0) < 0.5 ? 1.0f : -1.0f) + (std::fmod (b, 1.0) < 0.5 ? 1.0f : -1.0f);
        return 0.3f * lp.lp (sq) * env (t, 0.09); }) }); }
    { OnePole hp (5000.0f); kit.push_back ({ "Shaker", make (0.18, [&] (double t, int) { return 0.5f * hp.hp (s.noise()) * (float) (std::sin (juce::jmin (1.0, t / 0.06) * 3.14159)) * env (t, 0.08); }) }); }
    { double ph = 0.0; kit.push_back ({ "Conga", make (0.45, [&] (double t, int) { const double f = 300.0 * (1.0 + 0.3 * std::exp (-t / 0.01)); ph += twoPi * f / sr; return 0.8f * (float) std::sin (ph) * env (t, 0.12); }) }); }
    { OnePole hp (2500.0f); kit.push_back ({ "Snap", make (0.25, [&] (double t, int) { return 0.8f * hp.hp (s.noise()) * env (t, 0.035); }) }); }
    { double ph = 0.0; kit.push_back ({ "Sub Boom", make (1.6, [&] (double t, int) { const double f = 50.0 + 40.0 * std::exp (-t / 0.05); ph += twoPi * f / sr; return (float) std::tanh (2.0 * std::sin (ph)) * 0.8f * env (t, 0.55); }) }); }
    { OnePole lp (2500.0f); kit.push_back ({ "Noise Riser", make (1.0, [&] (double t, int) { return 0.4f * lp.lp (s.noise()) * (float) (t / 1.0); }) }); }

    // instruments (root C3)
    {
        double c = 0.0, m = 0.0;
        const double f = 130.81;
        kit.push_back ({ "Soft Keys", make (2.0, [&] (double t, int) {
            m += twoPi * f * 2.0 / sr;
            c += twoPi * f / sr;
            const double idx = 1.6 * std::exp (-t / 0.35);
            return 0.6f * (float) std::sin (c + idx * std::sin (m)) * env (t, 0.9) * (float) juce::jmin (1.0, t / 0.004); }) });
    }
    {
        double ph = 0.0;
        OnePole lp (900.0f);
        kit.push_back ({ "Round Bass", make (1.2, [&] (double t, int) {
            ph += 65.41 / sr;
            const float saw = (float) (2.0 * std::fmod (ph, 1.0) - 1.0);
            return 0.7f * lp.lp (saw) * env (t, 0.5) * (float) juce::jmin (1.0, t / 0.003); }) });
    }
    return kit;
}
} // namespace

// Writes the kit if needed and loads it into banks A (drums) and B (keys) of an empty session.
void loadStarterKit()
{
    auto& e = Engine::get();
    for (auto& p : e.session().banks[0].pads) if (p.hasSound()) return;

    auto folder = Engine::getSamplesFolder().getChildFile ("Crate Starter Kit");
    const char* names[] = { "Kick", "Snare", "Closed Hat", "Open Hat", "Clap", "Rim", "Low Tom", "Mid Tom", "High Tom", "Crash",
                            "Cowbell", "Shaker", "Conga", "Snap", "Sub Boom", "Noise Riser", "Soft Keys", "Round Bass" };
    bool missing = false;
    for (auto* n : names) if (! folder.getChildFile (juce::String (n) + ".wav").existsAsFile()) missing = true;
    if (missing)
    {
        folder.createDirectory();
        for (auto& [name, buf] : buildKit())
            if (auto w = createWavWriter (folder.getChildFile (name + ".wav"), sr, 2, 24)) w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
    }

    e.createBank (0);
    for (int i = 0; i < 16; ++i) e.loadSampleToPad (folder.getChildFile (juce::String (names[i]) + ".wav"), 0, i);
    {
        const juce::ScopedLock sl (e.getLock());
        auto& bank = e.session().banks[0];
        bank.name = "Starter Drums";
        bank.strip.name = bank.name;
        bank.pads[2].choke = bank.pads[3].choke = 1;   // hats choke each other
        const juce::Colour cols[] = { juce::Colour (0xffff7a45), juce::Colour (0xffff7a45), juce::Colour (0xffe7d84a), juce::Colour (0xffe7d84a),
                                      juce::Colour (0xffef4f6e), juce::Colour (0xffef4f6e), juce::Colour (0xff3dd68c), juce::Colour (0xff3dd68c),
                                      juce::Colour (0xff3dd68c), juce::Colour (0xff2fb8e8), juce::Colour (0xff2fb8e8), juce::Colour (0xff2fb8e8),
                                      juce::Colour (0xffd65cd6), juce::Colour (0xffd65cd6), juce::Colour (0xff7d6cf2), juce::Colour (0xff7d6cf2) };
        for (int i = 0; i < 16; ++i) bank.pads[(size_t) i].colour = cols[i];
        // a simple 1-bar groove to start from
        auto& pat = bank.patterns[0];
        pat.name = "Groove";
        pat.notes.clear();
        const int s16 = cr::ppq / 4;
        for (int st : { 0, 6, 10 }) pat.notes.push_back ({ 0, -1, st * s16, s16, 1.0f });
        for (int st : { 4, 12 }) pat.notes.push_back ({ 1, -1, st * s16, s16, 0.95f });
        for (int st = 0; st < 16; st += 2) pat.notes.push_back ({ 2, -1, st * s16, s16, st % 4 == 0 ? 0.8f : 0.55f });
        pat.notes.push_back ({ 3, -1, 14 * s16, s16 * 2, 0.6f });
    }

    e.createBank (1);
    e.loadSampleToPad (folder.getChildFile ("Soft Keys.wav"), 1, 0);
    e.loadSampleToPad (folder.getChildFile ("Round Bass.wav"), 1, 1);
    {
        const juce::ScopedLock sl (e.getLock());
        auto& bank = e.session().banks[1];
        bank.name = "Starter Keys";
        bank.strip.name = bank.name;
        for (int p : { 0, 1 })
        {
            auto& pad = bank.pads[(size_t) p];
            pad.keysMode = true;
            auto& l = pad.layers[0];
            l.p.trigger = cr::TriggerMode::hold;
            l.p.polyphony = p == 0 ? 8 : 1;
            l.p.legato = p == 1;
            l.p.release = p == 0 ? 0.4f : 0.08f;
        }
    }
    {
        const juce::ScopedLock sl (e.getLock());
        e.session().song.push_back ({ 0, 0, 0, 4 * cr::barTicks() });
    }
    e.launchPattern (0, 0);
    e.captureScene (0);
    e.playSong (-1);
    e.selectedBank = 0;
    e.selectedPad = 0;
}
