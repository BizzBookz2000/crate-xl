// Sampler voices: multi-layer / multi-zone playback, envelopes, filters, modulation matrix,
// loops with crossfade, reverse, legato / glide, choke groups, disk streaming, live stretch.
#include "Engine.h"
#include "Stretch.h"

namespace
{
constexpr int padKey (int bank, int pad) { return bank * cr::numPads + pad; }
constexpr int controlBlock = 32;

inline float hermite (float xm1, float x0, float x1, float x2, float t)
{
    const float c = (x1 - xm1) * 0.5f;
    const float v = x0 - x1;
    const float w = c + v;
    const float a = w + v + (x2 - x0) * 0.5f;
    const float b = w + a;
    return ((((a * t) - b) * t + c) * t + x0);
}

float lofiRate (cr::LofiMode m)
{
    switch (m)
    {
        case cr::LofiMode::vintage12_26: return 26040.0f;
        case cr::LofiMode::vintage12_32: return 32000.0f;
        case cr::LofiMode::crunch8_22:   return 22050.0f;
        default: return 0.0f;
    }
}

float lofiBits (cr::LofiMode m) { return m == cr::LofiMode::crunch8_22 ? 8.0f : 12.0f; }
} // namespace

// ============================================================================
void Engine::startVoices (int b, int p, int key, float velocity, bool releaseTrigger)
{
    auto& pad = sess.banks[(size_t) b].pads[(size_t) p];
    if (pad.layers.empty()) return;
    if (pad.choke > 0 && ! releaseTrigger) chokeGroup (b, pad.choke, p);

    const int noteKey = key >= 0 ? key : pad.padNote;
    const int vel127 = juce::jlimit (1, 127, juce::roundToInt (velocity * 127.0f));
    for (int l = 0; l < (int) pad.layers.size(); ++l)
    {
        auto& layer = pad.layers[(size_t) l];
        if (layer.p.mute) continue;
        if (releaseTrigger != (layer.p.trigger == cr::TriggerMode::onRelease)) continue;
        for (int z = 0; z < (int) layer.zones.size(); ++z)
        {
            const auto& zone = layer.zones[(size_t) z];
            if (! zone.sample || zone.mute) continue;
            if (noteKey < zone.lowKey || noteKey > zone.highKey || vel127 < zone.lowVel || vel127 > zone.highVel) continue;
            startLayerVoice (b, p, l, z, key, velocity);
        }
    }
}

void Engine::chokeGroup (int b, int group, int exceptPad)
{
    const auto& bank = sess.banks[(size_t) b];
    for (auto& v : voices)
        if (v.active && v.bank == b && v.pad != exceptPad && bank.pads[(size_t) v.pad].choke == group && v.fadeOut < 0)
            v.fadeOut = 96;
}

void Engine::startLayerVoice (int b, int p, int l, int z, int key, float velocity)
{
    auto& pad = sess.banks[(size_t) b].pads[(size_t) p];
    auto& layer = pad.layers[(size_t) l];
    auto& zone = layer.zones[(size_t) z];
    const auto& lp = layer.p;
    const int noteKey = key >= 0 ? key : pad.padNote;
    const float semisNew = (zone.pitchTrack ? (float) (noteKey - zone.rootKey) : 0.0f);

    // polyphony & legato
    int count = 0;
    Voice* oldest = nullptr;
    for (auto& v : voices)
    {
        if (! v.active || v.bank != b || v.pad != p || v.layer != l || v.fadeOut >= 0) continue;
        if (lp.legato && lp.polyphony <= 1 && ! v.released)
        {
            v.glideTarget = semisNew - v.baseSemis;
            if (lp.glide <= 0.0005f) v.glideSemis = v.glideTarget;
            v.key = key;
            v.velocity = velocity;
            return;
        }
        ++count;
        if (oldest == nullptr || v.age < oldest->age) oldest = &v;
    }
    if (count >= juce::jmax (1, lp.polyphony) && oldest != nullptr) oldest->fadeOut = 64;

    // find a voice
    Voice* voice = nullptr;
    for (auto& v : voices) if (! v.active) { voice = &v; break; }
    if (voice == nullptr)
    {
        for (auto& v : voices)
            if (voice == nullptr || (v.released && ! voice->released) || (v.released == voice->released && v.age < voice->age)) voice = &v;
    }
    if (voice == nullptr) return;
    auto& v = *voice;

    auto s = zone.sample;
    v.active = true;
    v.bank = b; v.pad = p; v.layer = l; v.zone = z; v.key = key;
    v.velocity = velocity;
    v.sample = s;
    v.stretched.reset();
    v.len = s->numFrames;
    double sr = s->sampleRate;
    if (lp.liveStretch && ! s->isStreamed() && s->stretched != nullptr)
    {
        const double want = lp.sampleBpm / juce::jmax (1.0, sess.bpm);
        if (std::abs (s->stretchedRatio - want) < 0.01)
        {
            v.stretched = s->stretched;
            v.len = v.stretched->getNumSamples();
        }
    }
    v.baseInc = sr / sampleRate;
    const double len = (double) v.len;
    v.startS = juce::jlimit (0.0, len - 1.0, (double) zone.start * len);
    v.endS = juce::jlimit (v.startS + 1.0, len, (double) zone.end * len);
    v.loopS = juce::jlimit (v.startS, v.endS - 1.0, (double) zone.loopStart * len);
    v.loopE = juce::jlimit (v.loopS + 1.0, v.endS, (double) zone.loopEnd * len);
    v.loop = lp.loop;
    v.reverse = lp.reverse;
    v.forwardDir = ! lp.reverse;
    v.released = false;
    v.fadeOut = -1;
    v.age = ++voiceCounter;
    v.randomValue = rng.nextFloat() * 2.0f - 1.0f;
    v.baseSemis = semisNew;
    v.glideSemis = 0.0f;
    v.glideTarget = 0.0f;
    v.lofiAcc = 1.0;
    v.lofiHold[0] = v.lofiHold[1] = 0.0f;
    for (auto& st : v.svf) for (auto& f : st) f.reset();
    for (int i = 0; i < 2; ++i)
    {
        const auto& lfo = layer.lfo[(size_t) i];
        if (lfo.retrigger) v.lfoPhase[i] = 0.0;
        else
        {
            const double hz = lfo.sync ? sess.bpm / 60.0 / syncBeats (lfo.syncIndex) : lfo.rate;
            v.lfoPhase[i] = std::fmod (freeClockSeconds * hz, 1.0);
        }
        v.lfoHeld[i] = rng.nextFloat() * 2.0f - 1.0f;
    }
    v.stepPhase = layer.step.retrigger ? 0.0 : std::fmod (freeClockSeconds * sess.bpm / 60.0 / syncBeats (layer.step.syncIndex) / juce::jmax (1, layer.step.numSteps), 1.0);
    v.stepValue = layer.step.steps[0];
    v.zoneGain = dbToGain (zone.gainDb + lp.gainDb) * std::pow (juce::jlimit (0.0f, 1.0f, velocity), 1.2f);

    // sample start modulation (static sources only)
    double startMod = 0.0;
    for (auto& r : layer.mods)
    {
        if (r.bypass || r.target != cr::ModTarget::sampleStart) continue;
        float src = 0.0f;
        switch (r.source)
        {
            case cr::ModSource::velocity: src = velocity; break;
            case cr::ModSource::random:   src = v.randomValue * 0.5f + 0.5f; break;
            case cr::ModSource::keyTrack: src = (float) (noteKey - 60) / 60.0f; break;
            case cr::ModSource::modWheel: src = modWheel[(size_t) b].load(); break;
            default:
                if ((int) r.source >= (int) cr::ModSource::macro1 && (int) r.source < (int) cr::ModSource::count)
                    src = sess.macros[(size_t) ((int) r.source - (int) cr::ModSource::macro1)].value;
                break;
        }
        startMod += r.amount * src;
    }
    const double region = v.endS - v.startS;
    v.pos = v.reverse ? v.endS - 1.0 - juce::jlimit (0.0, 0.99, startMod) * region
                      : v.startS + juce::jlimit (0.0, 0.99, startMod) * region;
    v.fadeIn = (v.pos > v.startS + 1.0 && ! v.reverse) ? 48 : 0;

    if (lp.envOn)
        v.amp.setParams (sampleRate, lp.attack, lp.hold, lp.decay, lp.sustain, lp.release, lp.envMode == cr::EnvMode::ahd);
    else
        v.amp.setParams (sampleRate, 0.0005f, 0.0f, 0.05f, 1.0f, 0.008f, false);
    v.amp.level = 0.0;
    v.amp.noteOn();
    v.modEnv.setParams (sampleRate, layer.modEnv.attack, layer.modEnv.hold, layer.modEnv.decay, layer.modEnv.sustain, layer.modEnv.release, false);
    v.modEnv.level = 0.0;
    v.modEnv.noteOn();
    v.winStart = 0;
    v.winLen = 0;
}

void Engine::stopVoices (int b, int p, int key)
{
    auto& pad = sess.banks[(size_t) b].pads[(size_t) p];
    for (auto& v : voices)
    {
        if (! v.active || v.bank != b || v.pad != p || v.released) continue;
        if (key >= 0 && v.key != key) continue;
        if (key < 0 && v.key >= 0) continue;
        if (! juce::isPositiveAndBelow (v.layer, (int) pad.layers.size())) continue;
        const auto& lp = pad.layers[(size_t) v.layer].p;
        if (lp.trigger == cr::TriggerMode::hold || v.loop != cr::LoopMode::off)
        {
            v.released = true;
            v.amp.noteOff();
            v.modEnv.noteOff();
            if (! lp.envOn) v.fadeOut = juce::jmax (v.fadeOut, 128);
            if (lp.envOn && lp.envMode == cr::EnvMode::ahd) v.fadeOut = (int) (sampleRate * 0.01);
        }
    }
}

void Engine::renderVoices (int start, int end)
{
    if (end <= start) return;
    for (auto& v : voices)
        if (v.active) renderVoice (v, start, end);
}

void Engine::fetchWindow (Voice& v, juce::int64 first, juce::int64 last)
{
    if (v.winLen > 0 && first >= v.winStart && last < v.winStart + v.winLen) return;
    const int cap = (int) v.winL.size();
    if (cap < 64 || ! v.sample || ! v.sample->mapped) { v.winLen = 0; return; }
    juce::int64 from = juce::jmax ((juce::int64) 0, first - 8);
    if (! v.forwardDir) from = juce::jmax ((juce::int64) 0, last - cap + 8);
    const int count = (int) juce::jmin ((juce::int64) cap, v.sample->numFrames - from);
    if (count <= 0) { v.winLen = 0; return; }
    float* chans[2] = { v.winL.data(), v.winR.data() };
    v.sample->mapped->read (chans, 2, from, count);
    if (v.sample->mapped->numChannels == 1) std::copy (v.winL.begin(), v.winL.begin() + count, v.winR.begin());
    v.winStart = from;
    v.winLen = count;
}

void Engine::renderVoice (Voice& v, int start, int end)
{
    auto& bank = sess.banks[(size_t) v.bank];
    auto& pad = bank.pads[(size_t) v.pad];
    if (! juce::isPositiveAndBelow (v.layer, (int) pad.layers.size()) || ! v.sample) { v.active = false; v.sample.reset(); v.stretched.reset(); return; }
    auto& layer = pad.layers[(size_t) v.layer];
    const auto& lp = layer.p;
    const int n = end - start;

    juce::AudioBuffer<float>* bus = nullptr;
    if (layer.hasFx()) bus = getBus (layerBuses, layerBusesUsed, (padKey (v.bank, v.pad)) * 8 + juce::jmin (7, v.layer), curBlock);
    else bus = getBus (padBuses, padBusesUsed, padKey (v.bank, v.pad), curBlock);
    if (bus == nullptr) return;
    float* outL = bus->getWritePointer (0);
    float* outR = bus->getWritePointer (1);

    const bool streamed = v.sample->isStreamed();
    const juce::AudioBuffer<float>* src = v.stretched ? v.stretched.get() : (streamed ? nullptr : &v.sample->buffer);
    if (! streamed && (src == nullptr || src->getNumSamples() < 2)) { v.active = false; return; }
    if (streamed && v.winL.size() < 4096) { v.active = false; return; }   // windows are pre-allocated
    const juce::int64 srcLen = v.len;
    const int srcCh = src != nullptr ? src->getNumChannels() : 2;
    const float* dL = src != nullptr ? src->getReadPointer (0) : nullptr;
    const float* dR = src != nullptr ? src->getReadPointer (juce::jmin (1, srcCh - 1)) : nullptr;

    auto frame = [&] (juce::int64 i, float& l, float& r)
    {
        if (i < 0 || i >= srcLen) { l = r = 0.0f; return; }
        if (! streamed) { l = dL[i]; r = dR[i]; return; }
        const juce::int64 w = i - v.winStart;
        if (w < 0 || w >= v.winLen) { l = r = 0.0f; return; }
        l = v.winL[(size_t) w]; r = v.winR[(size_t) w];
    };
    auto readAt = [&] (double pos, float& l, float& r)
    {
        const juce::int64 i0 = (juce::int64) std::floor (pos);
        const float t = (float) (pos - (double) i0);
        float lm1, rm1, l0, r0, l1, r1, l2, r2;
        frame (i0 - 1, lm1, rm1); frame (i0, l0, r0); frame (i0 + 1, l1, r1); frame (i0 + 2, l2, r2);
        l = hermite (lm1, l0, l1, l2, t);
        r = hermite (rm1, r0, r1, r2, t);
    };

    const int bIdx = v.bank;
    const int filterType = (int) lp.filter;
    const float lofiTarget = lofiRate (lp.lofi);
    const float lofiStep = lofiTarget > 0.0f ? lofiTarget / (float) sampleRate : 1.0f;
    const float lofiQ = lofiTarget > 0.0f ? std::pow (2.0f, lofiBits (lp.lofi) - 1.0f) : 0.0f;
    const int noteKey = v.key >= 0 ? v.key : pad.padNote;
    const float glideCoef = lp.glide > 0.0005f ? (float) (1.0 - std::exp (-1.0 / (lp.glide * sampleRate / controlBlock))) : 1.0f;

    int i = start;
    while (i < end && v.active)
    {
        const int sub = juce::jmin (controlBlock, end - i);

        // ---- modulation sources (control rate) --------------------------------------------
        float srcVal[(int) cr::ModSource::count] {};
        for (int k = 0; k < 2; ++k)
        {
            const auto& lfo = layer.lfo[(size_t) k];
            float rate = lfo.sync ? (float) (sess.bpm / 60.0 / syncBeats (lfo.syncIndex)) : lfo.rate;
            for (auto& r : layer.mods)
                if (! r.bypass && r.target == (k == 0 ? cr::ModTarget::lfo1Rate : cr::ModTarget::lfo2Rate) && r.source != (k == 0 ? cr::ModSource::lfo1 : cr::ModSource::lfo2))
                    rate *= std::pow (2.0f, r.amount * 3.0f);
            const double prev = v.lfoPhase[k];
            v.lfoPhase[k] = std::fmod (v.lfoPhase[k] + rate * sub / sampleRate, 1.0);
            if (v.lfoPhase[k] < prev) v.lfoHeld[k] = rng.nextFloat() * 2.0f - 1.0f;
            srcVal[k == 0 ? (int) cr::ModSource::lfo1 : (int) cr::ModSource::lfo2] = lfoShapeValue ((int) lfo.shape, v.lfoPhase[k], v.lfoHeld[k]);
        }
        float me = 0.0f;
        for (int s = 0; s < sub; ++s) me = v.modEnv.next();
        srcVal[(int) cr::ModSource::modEnv] = me;
        {
            const auto& st = layer.step;
            const int steps = juce::jlimit (1, 32, st.numSteps);
            const double stepHz = sess.bpm / 60.0 / syncBeats (st.syncIndex) / steps;
            v.stepPhase = std::fmod (v.stepPhase + stepHz * sub / sampleRate, 1.0);
            const float target = st.steps[(size_t) juce::jlimit (0, steps - 1, (int) (v.stepPhase * steps))] * 2.0f - 1.0f;
            const float smooth = juce::jlimit (0.0f, 0.99f, st.smooth);
            v.stepValue = v.stepValue * smooth + target * (1.0f - smooth);
            srcVal[(int) cr::ModSource::stepMod] = v.stepValue;
        }
        srcVal[(int) cr::ModSource::velocity] = v.velocity;
        srcVal[(int) cr::ModSource::keyTrack] = (float) (noteKey - 60) / 60.0f;
        srcVal[(int) cr::ModSource::modWheel] = modWheel[(size_t) bIdx].load();
        srcVal[(int) cr::ModSource::pitchBend] = pitchBend[(size_t) bIdx].load();
        srcVal[(int) cr::ModSource::aftertouch] = aftertouch[(size_t) bIdx].load();
        srcVal[(int) cr::ModSource::random] = v.randomValue;
        for (int m = 0; m < cr::numMacros; ++m) srcVal[(int) cr::ModSource::macro1 + m] = sess.macros[(size_t) m].value;

        float mod[(int) cr::ModTarget::count] {};
        for (auto& r : layer.mods)
            if (! r.bypass && r.source != cr::ModSource::none)
                mod[(int) r.target] += r.amount * srcVal[(int) r.source];

        // ---- pitch -------------------------------------------------------------------------------
        v.glideSemis += (v.glideTarget - v.glideSemis) * glideCoef;
        const float semis = v.baseSemis + v.glideSemis + lp.tune + lp.fine / 100.0f + layer.zones[(size_t) juce::jlimit (0, (int) layer.zones.size() - 1, v.zone)].tune
                            + pitchBend[(size_t) bIdx].load() * 2.0f + mod[(int) cr::ModTarget::pitch] * 24.0f + sess.globalTuneCents / 100.0f;
        const double inc = v.baseInc * std::pow (2.0, semis / 12.0);

        // ---- filter --------------------------------------------------------------------------------
        if (filterType != 0)
        {
            const float cutoff = juce::jlimit (20.0f, 20000.0f, lp.cutoff * std::pow (2.0f, mod[(int) cr::ModTarget::cutoff] * 5.0f));
            const float q = juce::jlimit (0.3f, 20.0f, lp.resonance * std::pow (2.0f, mod[(int) cr::ModTarget::resonance] * 2.0f));
            for (auto& st : v.svf) for (auto& f : st) f.setParams (sampleRate, cutoff, q);
        }
        const int svfMode = (filterType == 1 || filterType == 2) ? Svf::lowPass : (filterType == 3 || filterType == 4) ? Svf::highPass
                          : filterType == 5 ? Svf::bandPass : Svf::notch;
        const bool twoStage = filterType == 2 || filterType == 4;

        const float gain = v.zoneGain * juce::jlimit (0.0f, 2.0f, 1.0f + mod[(int) cr::ModTarget::volume]);
        const float pan = juce::jlimit (-1.0f, 1.0f, lp.pan + mod[(int) cr::ModTarget::pan]);
        const float gl = gain * juce::jmin (1.0f, 1.0f - pan), gr = gain * juce::jmin (1.0f, 1.0f + pan);
        const float sat = juce::jlimit (0.0f, 1.0f, lp.saturate + mod[(int) cr::ModTarget::saturate]);
        const float drive = 1.0f + sat * 12.0f;
        const float satNorm = sat > 0.001f ? 1.0f / std::tanh (drive) : 1.0f;
        const double xfLen = v.loop != cr::LoopMode::off ? lp.crossfade * (v.loopE - v.loopS) : 0.0;

        if (streamed)
        {
            const double span = inc * sub + 8.0;
            if (v.forwardDir) fetchWindow (v, (juce::int64) v.pos - 2, (juce::int64) (v.pos + span));
            else fetchWindow (v, (juce::int64) (v.pos - span), (juce::int64) v.pos + 2);
        }

        for (int s = 0; s < sub; ++s)
        {
            const int o = i + s;
            float l, r;
            readAt (v.pos, l, r);
            if (xfLen > 1.0 && v.forwardDir && v.pos > v.loopE - xfLen)
            {
                const float t = (float) ((v.pos - (v.loopE - xfLen)) / xfLen);
                float l2, r2;
                readAt (v.pos - (v.loopE - v.loopS), l2, r2);
                l = l * (1.0f - t) + l2 * t;
                r = r * (1.0f - t) + r2 * t;
            }

            // advance position
            if (v.forwardDir)
            {
                v.pos += inc;
                if (v.loop != cr::LoopMode::off && v.pos >= v.loopE)
                {
                    if (v.loop == cr::LoopMode::forward) v.pos = v.loopS + (v.pos - v.loopE);
                    else { v.pos = v.loopE - (v.pos - v.loopE) - 1.0; v.forwardDir = false; }
                }
                else if (v.pos >= v.endS) { v.fadeOut = juce::jmin (v.fadeOut < 0 ? 16 : v.fadeOut, 16); }
            }
            else
            {
                v.pos -= inc;
                if (v.loop != cr::LoopMode::off && v.pos <= v.loopS)
                {
                    if (v.loop == cr::LoopMode::alternate) { v.pos = v.loopS + (v.loopS - v.pos); v.forwardDir = true; }
                    else v.pos = v.loopE - (v.loopS - v.pos);
                }
                else if (v.pos <= v.startS) { v.fadeOut = juce::jmin (v.fadeOut < 0 ? 16 : v.fadeOut, 16); }
            }

            float env = (float) v.amp.next();
            if (v.fadeIn > 0) { env *= 1.0f - (float) v.fadeIn / 48.0f; --v.fadeIn; }
            if (v.fadeOut >= 0)
            {
                if (v.fadeOut == 0) { v.active = false; break; }
                env *= juce::jmin (1.0f, (float) v.fadeOut / 64.0f);
                --v.fadeOut;
            }
            if (! v.amp.isActive()) { v.active = false; break; }

            if (sat > 0.001f) { l = std::tanh (l * drive) * satNorm; r = std::tanh (r * drive) * satNorm; }
            if (lofiTarget > 0.0f)
            {
                v.lofiAcc += lofiStep;
                if (v.lofiAcc >= 1.0)
                {
                    v.lofiAcc -= 1.0;
                    v.lofiHold[0] = std::round (l * lofiQ) / lofiQ;
                    v.lofiHold[1] = std::round (r * lofiQ) / lofiQ;
                }
                l = v.lofiHold[0];
                r = v.lofiHold[1];
            }
            if (filterType != 0)
            {
                l = v.svf[0][0].process (l, svfMode);
                r = v.svf[0][1].process (r, svfMode);
                if (twoStage) { l = v.svf[1][0].process (l, svfMode); r = v.svf[1][1].process (r, svfMode); }
            }
            outL[o] += l * env * gl;
            outR[o] += r * env * gr;
        }
        i += sub;
    }
    if (! v.active) { v.sample.reset(); v.stretched.reset(); }
}

// ============================================================================
// Live stretch: (re)build stretched copies on a worker thread
// ============================================================================
void Engine::requestStretches()
{
    std::vector<std::pair<cr::SamplePtr, double>> jobs;
    {
        const juce::ScopedLock sl (lock);
        for (auto& bank : sess.banks)
        {
            if (! bank.created) continue;
            for (auto& pad : bank.pads)
                for (auto& layer : pad.layers)
                {
                    if (! layer.p.liveStretch) continue;
                    const double want = layer.p.sampleBpm / juce::jmax (1.0, sess.bpm);
                    for (auto& z : layer.zones)
                    {
                        auto s = z.sample;
                        if (! s || s->isStreamed()) continue;
                        if (s->stretched && std::abs (s->stretchedRatio - want) < 0.001) continue;
                        if (pendingStretch.count (s.get()) > 0) continue;
                        pendingStretch.insert (s.get());
                        jobs.push_back ({ s, want });
                    }
                }
        }
    }
    for (auto& [s, ratio] : jobs)
    {
        auto sp = s;
        const double r = ratio;
        workers.addJob ([this, sp, r]
        {
            auto out = std::make_shared<juce::AudioBuffer<float>> (stretch::timeStretch (sp->buffer, r, sp->sampleRate));
            const juce::ScopedLock sl2 (lock);
            sp->stretched = out;
            sp->stretchedRatio = r;
            pendingStretch.erase (sp.get());
        });
    }
}
