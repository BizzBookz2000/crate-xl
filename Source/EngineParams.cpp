// Parameter addressing, values, macros, MIDI learn tables and automation lanes.
#include "Engine.h"

// ============================================================================
// Names
// ============================================================================
juce::StringArray cr::modSourceNames()
{
    juce::StringArray s { "None", "LFO 1", "LFO 2", "Mod Envelope", "Step Modulator", "Velocity", "Key Track",
                          "Mod Wheel", "Pitch Bend", "Aftertouch", "Random" };
    for (int i = 0; i < numMacros; ++i) s.add ("Macro " + juce::String (i + 1));
    return s;
}

juce::StringArray cr::modTargetNames()
{
    return { "Pitch", "Filter Cutoff", "Filter Resonance", "Volume", "Pan", "Sample Start", "Saturate", "LFO 1 Rate", "LFO 2 Rate" };
}

juce::StringArray cr::filterTypeNames() { return { "Off", "Low Pass 12", "Low Pass 24", "High Pass 12", "High Pass 24", "Band Pass", "Notch" }; }
juce::StringArray cr::lofiNames()       { return { "Hi-fi", "12-bit / 26 kHz", "12-bit / 32 kHz", "8-bit / 22 kHz" }; }
juce::StringArray cr::lfoShapeNames()   { return { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Sample & Hold" }; }

juce::String cr::ParamInfo::format (float v) const
{
    if (! choices.isEmpty()) return choices[juce::jlimit (0, choices.size() - 1, juce::roundToInt (v))];
    switch (display)
    {
        case gainDb:
        {
            const float db = gainToDb (v);
            return db <= -96.0f ? juce::String ("-inf dB") : juce::String (db, 1) + " dB";
        }
        case panLR:
        {
            if (std::abs (v) < 0.01f) return "center";
            return juce::String (juce::roundToInt (std::abs (v) * 100.0f)) + (v < 0 ? " L" : " R");
        }
        case percent: return juce::String (juce::roundToInt (v * 100.0f)) + "%";
        case seconds:
            if (v < 1.0f) return juce::String (juce::roundToInt (v * 1000.0f)) + " ms";
            return juce::String (v, 2) + " s";
        case hertz:
            if (v >= 1000.0f) return juce::String (v / 1000.0f, 2) + " kHz";
            return juce::String (v, v < 10.0f ? 2 : 0) + " Hz";
        case semitones: return (v > 0 ? "+" : "") + juce::String (v, std::abs (v - std::round (v)) < 0.01f ? 0 : 2) + " st";
        default: break;
    }
    const float a = std::abs (v);
    const int decimals = a >= 1000.0f ? 0 : (a >= 100.0f ? 1 : 2);
    return juce::String (v, decimals) + suffix;
}

// ============================================================================
// ParamAddress text form
// ============================================================================
namespace
{
juce::String stripTail (const cr::ParamAddress& a)
{
    if (a.slot >= 0) return "fx" + juce::String (a.slot) + "/" + juce::String (a.field);
    if (a.field == cr::stripVolume) return "vol";
    if (a.field == cr::stripPan) return "pan";
    return "send" + juce::String (a.field - cr::stripSend0);
}

bool parseStripTail (const juce::StringArray& t, int i, cr::ParamAddress& a)
{
    if (i >= t.size()) return false;
    const auto& f = t[i];
    if (f == "vol") a.field = cr::stripVolume;
    else if (f == "pan") a.field = cr::stripPan;
    else if (f.startsWith ("send")) a.field = cr::stripSend0 + f.substring (4).getIntValue();
    else if (f.startsWith ("fx") && i + 1 < t.size()) { a.slot = f.substring (2).getIntValue(); a.field = t[i + 1].getIntValue(); }
    else return false;
    return true;
}
} // namespace

juce::String cr::ParamAddress::toString() const
{
    using O = Owner;
    switch (owner)
    {
        case O::bank:   return "bank/" + juce::String (a) + "/" + stripTail (*this);
        case O::pad:    return "pad/" + juce::String (a) + "/" + juce::String (b) + "/" + stripTail (*this);
        case O::aux:    return "aux/" + juce::String (a) + "/" + stripTail (*this);
        case O::audio:  return "audio/" + juce::String (a) + "/" + stripTail (*this);
        case O::master: return "master/" + stripTail (*this);
        case O::layer:  return "layer/" + juce::String (a) + "/" + juce::String (b) + "/" + juce::String (c) + "/" + juce::String (field);
        case O::layerFx: return "lfx/" + juce::String (a) + "/" + juce::String (b) + "/" + juce::String (c) + "/" + juce::String (slot) + "/" + juce::String (field);
        case O::instrument: return "inst/" + juce::String (a) + "/" + juce::String (b) + "/" + juce::String (field);
        case O::macro:  return "macro/" + juce::String (a);
        case O::midi:
        {
            juce::String f = field == midiPitchBend ? "pb" : field == midiModWheel ? "mod" : field == midiAftertouch ? "at" : "cc" + juce::String (field - midiCC0);
            return "midi/" + juce::String (a) + "/" + f;
        }
        case O::tempo:  return "tempo";
        default: return {};
    }
}

cr::ParamAddress cr::ParamAddress::parse (const juce::String& text)
{
    ParamAddress a;
    auto t = juce::StringArray::fromTokens (text, "/", {});
    if (t.isEmpty()) return a;
    const auto& k = t[0];
    using O = Owner;
    if (k == "bank" && t.size() >= 3) { a.owner = O::bank; a.a = t[1].getIntValue(); if (! parseStripTail (t, 2, a)) a.owner = O::none; }
    else if (k == "pad" && t.size() >= 4) { a.owner = O::pad; a.a = t[1].getIntValue(); a.b = t[2].getIntValue(); if (! parseStripTail (t, 3, a)) a.owner = O::none; }
    else if (k == "aux" && t.size() >= 3) { a.owner = O::aux; a.a = t[1].getIntValue(); if (! parseStripTail (t, 2, a)) a.owner = O::none; }
    else if (k == "audio" && t.size() >= 3) { a.owner = O::audio; a.a = t[1].getIntValue(); if (! parseStripTail (t, 2, a)) a.owner = O::none; }
    else if (k == "master" && t.size() >= 2) { a.owner = O::master; if (! parseStripTail (t, 1, a)) a.owner = O::none; }
    else if (k == "layer" && t.size() >= 5) { a.owner = O::layer; a.a = t[1].getIntValue(); a.b = t[2].getIntValue(); a.c = t[3].getIntValue(); a.field = t[4].getIntValue(); }
    else if (k == "lfx" && t.size() >= 6) { a.owner = O::layerFx; a.a = t[1].getIntValue(); a.b = t[2].getIntValue(); a.c = t[3].getIntValue(); a.slot = t[4].getIntValue(); a.field = t[5].getIntValue(); }
    else if (k == "inst" && t.size() >= 4) { a.owner = O::instrument; a.a = t[1].getIntValue(); a.b = t[2].getIntValue(); a.field = t[3].getIntValue(); }
    else if (k == "macro" && t.size() >= 2) { a.owner = O::macro; a.a = t[1].getIntValue(); }
    else if (k == "midi" && t.size() >= 3)
    {
        a.owner = O::midi; a.a = t[1].getIntValue();
        if (t[2] == "pb") a.field = midiPitchBend;
        else if (t[2] == "mod") a.field = midiModWheel;
        else if (t[2] == "at") a.field = midiAftertouch;
        else if (t[2].startsWith ("cc")) a.field = midiCC0 + t[2].substring (2).getIntValue();
        else a.owner = O::none;
    }
    else if (k == "tempo") a.owner = O::tempo;

    // range checks
    auto inRange = [] (int v, int n) { return v >= 0 && v < n; };
    switch (a.owner)
    {
        case O::bank: case O::midi: if (! inRange (a.a, numBanks)) a.owner = O::none; break;
        case O::pad: case O::layer: case O::layerFx: case O::instrument:
            if (! inRange (a.a, numBanks) || ! inRange (a.b, numPads)) a.owner = O::none; break;
        case O::aux: if (! inRange (a.a, numAux)) a.owner = O::none; break;
        case O::audio: if (! inRange (a.a, maxAudioTracks)) a.owner = O::none; break;
        case O::macro: if (! inRange (a.a, numMacros)) a.owner = O::none; break;
        default: break;
    }
    return a;
}

// ============================================================================
// Resolution
// ============================================================================
cr::Strip* Engine::getStrip (int key)
{
    if (key >= 0 && key < cr::numBanks) return &sess.banks[(size_t) key].strip;
    if (key >= 1000 && key < 1000 + cr::maxAudioTracks) return &sess.audioTracks[(size_t) (key - 1000)].strip;
    if (key >= 2000 && key < 2000 + cr::numAux) return &sess.aux[(size_t) (key - 2000)];
    if (key == 3000) return &sess.master;
    if (key >= 10000 && key < 10000 + cr::numBanks * cr::numPads)
    {
        const int k = key - 10000;
        return &sess.banks[(size_t) (k / cr::numPads)].pads[(size_t) (k % cr::numPads)].channel;
    }
    return nullptr;
}

cr::Strip* Engine::resolveStrip (const cr::ParamAddress& a)
{
    using O = cr::ParamAddress::Owner;
    switch (a.owner)
    {
        case O::bank:   return &sess.banks[(size_t) a.a].strip;
        case O::pad:    return &sess.banks[(size_t) a.a].pads[(size_t) a.b].channel;
        case O::aux:    return &sess.aux[(size_t) a.a];
        case O::audio:  return &sess.audioTracks[(size_t) a.a].strip;
        case O::master: return &sess.master;
        default: return nullptr;
    }
}

cr::InsertSlot* Engine::resolveSlot (const cr::ParamAddress& a)
{
    using O = cr::ParamAddress::Owner;
    if (a.owner == O::layerFx)
    {
        auto& pad = sess.banks[(size_t) a.a].pads[(size_t) a.b];
        if (! juce::isPositiveAndBelow (a.c, (int) pad.layers.size()) || ! juce::isPositiveAndBelow (a.slot, cr::numLayerFx)) return nullptr;
        return &pad.layers[(size_t) a.c].fx[(size_t) a.slot];
    }
    if (a.slot < 0) return nullptr;
    if (auto* st = resolveStrip (a))
        if (juce::isPositiveAndBelow (a.slot, (int) st->inserts.size())) return &st->inserts[(size_t) a.slot];
    return nullptr;
}

float* Engine::resolveFloat (const cr::ParamAddress& a)
{
    using O = cr::ParamAddress::Owner;
    if (a.owner == O::layer)
    {
        auto& pad = sess.banks[(size_t) a.a].pads[(size_t) a.b];
        if (! juce::isPositiveAndBelow (a.c, (int) pad.layers.size())) return nullptr;
        auto& layer = pad.layers[(size_t) a.c];
        auto& p = layer.p;
        switch (a.field)
        {
            case cr::lfTune: return &p.tune;
            case cr::lfFine: return &p.fine;
            case cr::lfGain: return &p.gainDb;
            case cr::lfPan: return &p.pan;
            case cr::lfSaturate: return &p.saturate;
            case cr::lfAttack: return &p.attack;
            case cr::lfHold: return &p.hold;
            case cr::lfDecay: return &p.decay;
            case cr::lfSustain: return &p.sustain;
            case cr::lfRelease: return &p.release;
            case cr::lfCutoff: return &p.cutoff;
            case cr::lfResonance: return &p.resonance;
            case cr::lfGlide: return &p.glide;
            case cr::lfLfo1Rate: return &layer.lfo[0].rate;
            case cr::lfLfo2Rate: return &layer.lfo[1].rate;
            default: return nullptr;
        }
    }
    if (auto* st = resolveStrip (a))
    {
        if (a.slot >= 0) return nullptr;
        if (a.field == cr::stripVolume) return &st->volume;
        if (a.field == cr::stripPan) return &st->pan;
        const int s = a.field - cr::stripSend0;
        if (juce::isPositiveAndBelow (s, cr::numAux)) return &st->send[s];
    }
    if (a.owner == O::macro) return &sess.macros[(size_t) a.a].value;
    return nullptr;
}

// ============================================================================
// Info / get / set
// ============================================================================
bool Engine::getParamInfo (const cr::ParamAddress& a, cr::ParamInfo& info)
{
    using O = cr::ParamAddress::Owner;
    info = {};
    if (! a.isValid()) return false;

    if (a.owner == O::layerFx || a.slot >= 0)
    {
        auto* slot = resolveSlot (a);
        if (slot == nullptr) return false;
        if (slot->builtin)
        {
            if (! juce::isPositiveAndBelow (a.field, (int) slot->builtin->params.size())) return false;
            const auto& p = slot->builtin->params[(size_t) a.field];
            info.name = p.name; info.minValue = p.minValue; info.maxValue = p.maxValue; info.defaultValue = p.defaultValue;
            info.suffix = p.suffix; info.choices = p.choices;
            info.logScale = p.suffix.trim() == "Hz" && p.minValue > 0.0f;
            if (p.suffix.trim() == "Hz") info.display = cr::ParamInfo::hertz;
            else if (p.name == "Mix" || p.name == "Depth" || p.name == "Feedback") info.display = cr::ParamInfo::percent;
            return true;
        }
        if (slot->plugin)
        {
            auto& params = slot->plugin->getParameters();
            if (! juce::isPositiveAndBelow (a.field, params.size())) return false;
            info.name = params[a.field]->getName (40);
            info.defaultValue = params[a.field]->getDefaultValue();
            return true;
        }
        return false;
    }

    switch (a.owner)
    {
        case O::bank: case O::pad: case O::aux: case O::audio: case O::master:
            if (a.field == cr::stripVolume) { info.name = "Volume"; info.maxValue = 1.25f; info.defaultValue = 0.8f; info.display = cr::ParamInfo::gainDb; }
            else if (a.field == cr::stripPan) { info.name = "Pan"; info.minValue = -1.0f; info.defaultValue = 0.0f; info.display = cr::ParamInfo::panLR; }
            else { info.name = "Send " + juce::String (a.field - cr::stripSend0 + 1); info.maxValue = 1.0f; info.defaultValue = 0.0f; info.display = cr::ParamInfo::gainDb; }
            return true;
        case O::layer:
        {
            auto set = [&info] (const char* n, float mn, float mx, float def, int disp, bool lg = false)
            { info.name = n; info.minValue = mn; info.maxValue = mx; info.defaultValue = def; info.display = disp; info.logScale = lg; };
            using P = cr::ParamInfo;
            switch (a.field)
            {
                case cr::lfTune: set ("Tune", -36.0f, 36.0f, 0.0f, P::semitones); break;
                case cr::lfFine: set ("Fine Tune", -100.0f, 100.0f, 0.0f, P::plain); info.suffix = " ct"; break;
                case cr::lfGain: set ("Gain", -48.0f, 12.0f, -6.0f, P::plain); info.suffix = " dB"; break;
                case cr::lfPan: set ("Pan", -1.0f, 1.0f, 0.0f, P::panLR); break;
                case cr::lfSaturate: set ("Saturate", 0.0f, 1.0f, 0.0f, P::percent); break;
                case cr::lfAttack: set ("Attack", 0.0005f, 10.0f, 0.001f, P::seconds, true); break;
                case cr::lfHold: set ("Hold", 0.0f, 10.0f, 0.0f, P::seconds); break;
                case cr::lfDecay: set ("Decay", 0.005f, 20.0f, 0.5f, P::seconds, true); break;
                case cr::lfSustain: set ("Sustain", 0.0f, 1.0f, 1.0f, P::percent); break;
                case cr::lfRelease: set ("Release", 0.001f, 20.0f, 0.05f, P::seconds, true); break;
                case cr::lfCutoff: set ("Cutoff", 20.0f, 20000.0f, 20000.0f, P::hertz, true); break;
                case cr::lfResonance: set ("Reso", 0.3f, 12.0f, 0.707f, P::plain, true); break;
                case cr::lfGlide: set ("Glide Time", 0.0f, 2.0f, 0.05f, P::seconds); break;
                case cr::lfLfo1Rate: set ("LFO 1 Rate", 0.01f, 40.0f, 2.0f, P::hertz, true); break;
                case cr::lfLfo2Rate: set ("LFO 2 Rate", 0.01f, 40.0f, 2.0f, P::hertz, true); break;
                default: return false;
            }
            return true;
        }
        case O::instrument:
        {
            auto& pad = sess.banks[(size_t) a.a].pads[(size_t) a.b];
            if (! pad.instrument) return false;
            auto& params = pad.instrument->getParameters();
            if (! juce::isPositiveAndBelow (a.field, params.size())) return false;
            info.name = params[a.field]->getName (40);
            info.defaultValue = params[a.field]->getDefaultValue();
            return true;
        }
        case O::macro:
            info.name = sess.macros[(size_t) a.a].name; info.display = cr::ParamInfo::percent; return true;
        case O::midi:
            if (a.field == cr::midiPitchBend) { info.name = "Pitch Bend"; info.minValue = -1.0f; }
            else if (a.field == cr::midiModWheel) info.name = "Mod Wheel";
            else if (a.field == cr::midiAftertouch) info.name = "Aftertouch";
            else info.name = "CC " + juce::String (a.field - cr::midiCC0);
            return true;
        case O::tempo:
            info.name = "Tempo"; info.minValue = 20.0f; info.maxValue = 400.0f; info.defaultValue = 120.0f; info.suffix = " BPM"; return true;
        default: return false;
    }
}

float Engine::getParamValue (const cr::ParamAddress& a)
{
    using O = cr::ParamAddress::Owner;
    if (a.owner == O::layerFx || a.slot >= 0)
    {
        if (auto* slot = resolveSlot (a))
        {
            if (slot->builtin && juce::isPositiveAndBelow (a.field, (int) slot->builtin->params.size())) return slot->builtin->get (a.field);
            if (slot->plugin)
            {
                auto& params = slot->plugin->getParameters();
                if (juce::isPositiveAndBelow (a.field, params.size())) return params[a.field]->getValue();
            }
        }
        return 0.0f;
    }
    if (a.owner == O::instrument)
    {
        auto& pad = sess.banks[(size_t) a.a].pads[(size_t) a.b];
        if (pad.instrument)
        {
            auto& params = pad.instrument->getParameters();
            if (juce::isPositiveAndBelow (a.field, params.size())) return params[a.field]->getValue();
        }
        return 0.0f;
    }
    if (a.owner == O::midi)
    {
        if (a.field == cr::midiPitchBend) return pitchBend[(size_t) a.a].load();
        if (a.field == cr::midiModWheel) return modWheel[(size_t) a.a].load();
        if (a.field == cr::midiAftertouch) return aftertouch[(size_t) a.a].load();
        return 0.0f;
    }
    if (a.owner == O::tempo) return (float) sess.bpm;
    if (auto* f = resolveFloat (a)) return *f;
    return 0.0f;
}

void Engine::setParamValue (const cr::ParamAddress& a, float value, bool fromUser)
{
    using O = cr::ParamAddress::Owner;
    if (! a.isValid()) return;
    cr::ParamInfo info;
    const bool hasInfo = getParamInfo (a, info);

    if (fromUser)
    {
        const auto path = a.toString();
        const juce::ScopedLock sl (lock);
        touchedUntil[path] = juce::Time::getMillisecondCounterHiRes() + 600.0;
        if (hasInfo) recordAutomationPoint (path, a, info.toNormalised (value));
    }

    const juce::ScopedLock sl (lock);
    if (a.owner == O::layerFx || a.slot >= 0)
    {
        if (auto* slot = resolveSlot (a))
        {
            if (slot->builtin) slot->builtin->setParam (a.field, value);
            else if (slot->plugin)
            {
                auto& params = slot->plugin->getParameters();
                if (juce::isPositiveAndBelow (a.field, params.size())) params[a.field]->setValue (juce::jlimit (0.0f, 1.0f, value));
            }
        }
        return;
    }
    switch (a.owner)
    {
        case O::instrument:
        {
            auto& pad = sess.banks[(size_t) a.a].pads[(size_t) a.b];
            if (pad.instrument)
            {
                auto& params = pad.instrument->getParameters();
                if (juce::isPositiveAndBelow (a.field, params.size())) params[a.field]->setValue (juce::jlimit (0.0f, 1.0f, value));
            }
            return;
        }
        case O::midi:
        {
            const int b = a.a;
            if (a.field == cr::midiPitchBend) pitchBend[(size_t) b] = juce::jlimit (-1.0f, 1.0f, value);
            else if (a.field == cr::midiModWheel) modWheel[(size_t) b] = juce::jlimit (0.0f, 1.0f, value);
            else if (a.field == cr::midiAftertouch) aftertouch[(size_t) b] = juce::jlimit (0.0f, 1.0f, value);
            // forward to plugin instruments of the bank
            for (size_t i = 0; i < instrumentPads.size(); ++i)
            {
                if (instrumentPads[i] / cr::numPads != b) continue;
                juce::MidiMessage m;
                if (a.field == cr::midiPitchBend) m = juce::MidiMessage::pitchWheel (1, juce::jlimit (0, 16383, (int) ((value + 1.0f) * 8191.5f)));
                else if (a.field == cr::midiModWheel) m = juce::MidiMessage::controllerEvent (1, 1, juce::jlimit (0, 127, (int) (value * 127.0f)));
                else if (a.field == cr::midiAftertouch) m = juce::MidiMessage::channelPressureChange (1, juce::jlimit (0, 127, (int) (value * 127.0f)));
                else m = juce::MidiMessage::controllerEvent (1, juce::jlimit (0, 127, a.field - cr::midiCC0), juce::jlimit (0, 127, (int) (value * 127.0f)));
                instrumentMidi[i].addEvent (m, 0);
            }
            return;
        }
        case O::tempo:
            sess.bpm = juce::jlimit (20.0, 400.0, (double) value);
            return;
        case O::macro:
            sess.macros[(size_t) a.a].value = juce::jlimit (0.0f, 1.0f, value);
            for (auto& bnd : sess.macros[(size_t) a.a].bindings)
                if (bnd.address.isValid() && bnd.address.owner != O::macro)
                    setParamNormalised (bnd.address, bnd.min + sess.macros[(size_t) a.a].value * (bnd.max - bnd.min), false);
            return;
        default: break;
    }
    if (auto* f = resolveFloat (a))
        *f = hasInfo ? juce::jlimit (info.minValue, info.maxValue, value) : value;
}

float Engine::getParamNormalised (const cr::ParamAddress& a)
{
    cr::ParamInfo info;
    if (! getParamInfo (a, info)) return 0.0f;
    return info.toNormalised (getParamValue (a));
}

void Engine::setParamNormalised (const cr::ParamAddress& a, float norm, bool fromUser)
{
    cr::ParamInfo info;
    if (! getParamInfo (a, info)) return;
    setParamValue (a, info.fromNormalised (norm), fromUser);
}

juce::String Engine::getParamDisplayName (const juce::String& path)
{
    const auto a = cr::ParamAddress::parse (path);
    cr::ParamInfo info;
    if (! getParamInfo (a, info)) return path;
    using O = cr::ParamAddress::Owner;
    juce::String owner;
    switch (a.owner)
    {
        case O::bank: owner = sess.banks[(size_t) a.a].name; break;
        case O::pad: owner = cr::bankName (a.a) + " Pad " + juce::String (a.b + 1); break;
        case O::aux: owner = sess.aux[(size_t) a.a].name; break;
        case O::audio: owner = sess.audioTracks[(size_t) a.a].name; break;
        case O::master: owner = "Main"; break;
        case O::layer: owner = cr::bankName (a.a) + " Pad " + juce::String (a.b + 1) + " L" + juce::String (a.c + 1); break;
        case O::layerFx: owner = cr::bankName (a.a) + " Pad " + juce::String (a.b + 1) + " L" + juce::String (a.c + 1) + " FX" + juce::String (a.slot + 1); break;
        case O::instrument: owner = cr::bankName (a.a) + " Pad " + juce::String (a.b + 1) + " Plugin"; break;
        case O::midi: owner = cr::bankName (a.a) + " MIDI"; break;
        default: break;
    }
    if (a.slot >= 0 && a.owner != O::layerFx)
        if (auto* slot = resolveSlot (a)) owner << " " << slot->getName();
    return owner.isEmpty() ? info.name : owner + " - " + info.name;
}

// ============================================================================
// Macros
// ============================================================================
void Engine::setMacro (int index, float value)
{
    if (! juce::isPositiveAndBelow (index, cr::numMacros)) return;
    cr::ParamAddress a;
    a.owner = cr::ParamAddress::Owner::macro;
    a.a = index;
    setParamValue (a, value, true);
}

void Engine::bindMacro (int index, const juce::String& param)
{
    if (! juce::isPositiveAndBelow (index, cr::numMacros)) return;
    const juce::ScopedLock sl (lock);
    auto& m = sess.macros[(size_t) index];
    for (auto& b : m.bindings) if (b.param == param) return;
    cr::MacroBinding b;
    b.param = param;
    b.address = cr::ParamAddress::parse (param);
    b.min = 0.0f; b.max = 1.0f;
    m.bindings.push_back (b);
    if (m.bindings.size() == 1) m.name = getParamDisplayName (param).fromLastOccurrenceOf (" - ", false, false);
}

void Engine::unbindMacro (int index, int bindingIndex)
{
    if (! juce::isPositiveAndBelow (index, cr::numMacros)) return;
    const juce::ScopedLock sl (lock);
    auto& m = sess.macros[(size_t) index];
    if (juce::isPositiveAndBelow (bindingIndex, (int) m.bindings.size())) m.bindings.erase (m.bindings.begin() + bindingIndex);
    if (m.bindings.empty()) m.name = "Macro " + juce::String (index + 1);
}

int Engine::findMacroFor (const juce::String& param) const
{
    for (int m = 0; m < cr::numMacros; ++m)
        for (auto& b : sess.macros[(size_t) m].bindings) if (b.param == param) return m;
    return -1;
}

// ============================================================================
// MIDI learn / focus tables
// ============================================================================
void Engine::forgetMidiMapping (const juce::String& param)
{
    {
        const juce::ScopedLock sl (lock);
        auto& mm = sess.midiMappings;
        mm.erase (std::remove_if (mm.begin(), mm.end(), [&] (const cr::MidiMapping& m) { return m.param == param; }), mm.end());
    }
    rebuildMidiTables();
}

bool Engine::hasMidiMapping (const juce::String& param) const
{
    for (auto& m : sess.midiMappings) if (m.param == param) return true;
    return false;
}

void Engine::rebuildMidiTables()
{
    for (auto& v : ccIsMapped) v = false;
    for (auto& m : sess.midiMappings)
        if (juce::isPositiveAndBelow (m.channel - 1, 16) && juce::isPositiveAndBelow (m.controller, 128))
            ccIsMapped[(size_t) ((m.channel - 1) * 128 + m.controller)] = true;
    for (auto& v : focusTable) v = -1;
    for (int i = 0; i < FocusActions::numActions; ++i)
    {
        const auto& bnd = focus.bindings[(size_t) i];
        if (bnd.number < 0 || ! juce::isPositiveAndBelow (bnd.channel - 1, 16) || bnd.number > 127) continue;
        focusTable[(size_t) ((bnd.isCC ? 1 : 0) * 2048 + (bnd.channel - 1) * 128 + bnd.number)] = (short) i;
    }
}

// ============================================================================
// Automation lanes
// ============================================================================
cr::AutomationLane* Engine::findOrCreateLane (const juce::String& param, bool songLevel, int bank)
{
    const juce::ScopedLock sl (lock);
    std::vector<cr::AutomationLane>* lanes = nullptr;
    if (songLevel) lanes = &sess.songAutomation;
    else
    {
        if (! juce::isPositiveAndBelow (bank, cr::numBanks)) return nullptr;
        auto& b = sess.banks[(size_t) bank];
        if (! juce::isPositiveAndBelow (b.workingPattern, (int) b.patterns.size())) return nullptr;
        lanes = &b.patterns[(size_t) b.workingPattern].automation;
    }
    for (auto& l : *lanes) if (l.param == param) return &l;
    cr::AutomationLane lane;
    lane.param = param;
    lane.address = cr::ParamAddress::parse (param);
    lanes->push_back (std::move (lane));
    return &lanes->back();
}

juce::StringArray Engine::getAutomatableParams (int trackKey)
{
    juce::StringArray out;
    auto addStrip = [&] (const juce::String& prefix, cr::Strip& st)
    {
        out.add (prefix + "vol");
        out.add (prefix + "pan");
        for (int a = 0; a < sess.auxCount; ++a) out.add (prefix + "send" + juce::String (a));
        for (int s = 0; s < (int) st.inserts.size(); ++s)
        {
            auto& slot = st.inserts[(size_t) s];
            const int np = slot.builtin ? (int) slot.builtin->params.size() : (slot.plugin ? juce::jmin (128, slot.plugin->getParameters().size()) : 0);
            for (int i = 0; i < np; ++i) out.add (prefix + "fx" + juce::String (s) + "/" + juce::String (i));
        }
    };
    if (trackKey >= 0 && trackKey < cr::numBanks)
    {
        const int b = trackKey;
        auto& bank = sess.banks[(size_t) b];
        addStrip ("bank/" + juce::String (b) + "/", bank.strip);
        out.add ("midi/" + juce::String (b) + "/pb");
        out.add ("midi/" + juce::String (b) + "/mod");
        out.add ("midi/" + juce::String (b) + "/at");
        for (int p = 0; p < cr::numPads; ++p)
        {
            auto& pad = bank.pads[(size_t) p];
            if (! pad.hasSound()) continue;
            addStrip ("pad/" + juce::String (b) + "/" + juce::String (p) + "/", pad.channel);
            for (int l = 0; l < (int) pad.layers.size(); ++l)
            {
                for (int f = 0; f < cr::lfCount; ++f) out.add ("layer/" + juce::String (b) + "/" + juce::String (p) + "/" + juce::String (l) + "/" + juce::String (f));
                for (int s = 0; s < cr::numLayerFx; ++s)
                {
                    auto& slot = pad.layers[(size_t) l].fx[(size_t) s];
                    const int np = slot.builtin ? (int) slot.builtin->params.size() : (slot.plugin ? juce::jmin (128, slot.plugin->getParameters().size()) : 0);
                    for (int i = 0; i < np; ++i)
                        out.add ("lfx/" + juce::String (b) + "/" + juce::String (p) + "/" + juce::String (l) + "/" + juce::String (s) + "/" + juce::String (i));
                }
            }
            if (pad.instrument)
                for (int i = 0; i < juce::jmin (256, pad.instrument->getParameters().size()); ++i)
                    out.add ("inst/" + juce::String (b) + "/" + juce::String (p) + "/" + juce::String (i));
        }
    }
    else if (trackKey >= 1000 && trackKey < 1000 + cr::maxAudioTracks)
        addStrip ("audio/" + juce::String (trackKey - 1000) + "/", sess.audioTracks[(size_t) (trackKey - 1000)].strip);
    else if (trackKey >= 2000 && trackKey < 2000 + cr::numAux)
        addStrip ("aux/" + juce::String (trackKey - 2000) + "/", sess.aux[(size_t) (trackKey - 2000)]);
    else if (trackKey == 3000)
    {
        addStrip ("master/", sess.master);
        out.add ("tempo");
        for (int m = 0; m < cr::numMacros; ++m) out.add ("macro/" + juce::String (m));
    }
    return out;
}
