#pragma once
#include <JuceHeader.h>
#include "ChannelEq.h"
#include "Effects.h"

// ============================================================================
// Session data model.
//
// Threading rule: the UI edits the model on the message thread while holding
// Engine::getLock() for anything the audio thread reads; the audio thread
// holds the same lock (try-lock) for the duration of one block.
// ============================================================================

namespace cr
{
constexpr int numBanks       = 128;  // 16 groups x 8 letters (A1..H16)
constexpr int banksPerGroup  = 8;
constexpr int numGroups      = 16;
constexpr int numPads        = 128;
constexpr int numAux         = 8;
constexpr int numLayerFx     = 4;
constexpr int numMacros      = 16;
constexpr int maxAudioTracks = 256;
constexpr int numRepeatSlots = 6;
constexpr int ppq            = 96;   // ticks per quarter note
constexpr int songBars       = 512;

// Time signature: beats (quarter notes) per bar, shared by the whole session.
inline std::atomic<int>& beatsPerBarRef() { static std::atomic<int> v { 4 }; return v; }
inline int barTicks() { return beatsPerBarRef().load() * ppq; }

inline juce::String bankLetter (int b) { return juce::String::charToString ((juce::juce_wchar) ('A' + b % banksPerGroup)); }
inline juce::String bankName (int b)   { return bankLetter (b) + juce::String (b / banksPerGroup + 1); }

// ---------------------------------------------------------------------------
// Parameter addressing shared by automation, macros, MIDI learn and the UI.
// Text form (stored in files):
//   bank/<b>/vol|pan|send<i>|fx<slot>/<n>       pad/<b>/<p>/vol|pan|send<i>|fx<slot>/<n>
//   aux/<a>/...   audio/<t>/...   master/...     layer/<b>/<p>/<l>/<field>
//   lfx/<b>/<p>/<l>/<slot>/<n>   inst/<b>/<p>/<n>   macro/<i>   midi/<b>/pb|mod|at|cc<n>   tempo
struct ParamAddress
{
    enum class Owner { none = 0, bank, pad, aux, audio, master, layer, layerFx, instrument, macro, midi, tempo };
    Owner owner = Owner::none;
    int a = 0, b = 0, c = 0;   // indices (bank, pad, layer | aux | track | macro)
    int slot = -1;             // insert / layer-fx slot
    int field = 0;             // strip field, layer field, param index or midi controller

    bool isValid() const { return owner != Owner::none; }
    int bankIndex() const
    {
        switch (owner)
        {
            case Owner::bank: case Owner::pad: case Owner::layer: case Owner::layerFx:
            case Owner::instrument: case Owner::midi: return a;
            default: return -1;
        }
    }
    bool operator== (const ParamAddress& o) const
    {
        return owner == o.owner && a == o.a && b == o.b && c == o.c && slot == o.slot && field == o.field;
    }
    juce::String toString() const;
    static ParamAddress parse (const juce::String& text);
};

// strip fields
enum StripField { stripVolume = 0, stripPan = 1, stripSend0 = 2,   // send i = stripSend0 + i
                  stripTrim = 20, stripWidth = 21,
                  stripEq0 = 30,                         // band b: freq 30 + 3b, gain 31 + 3b, q 32 + 3b (b = 0..3)
                  stripHpf = 42, stripLpf = 43 };

// layer fields
enum LayerField
{
    lfTune = 0, lfFine, lfGain, lfPan, lfSaturate, lfAttack, lfHold, lfDecay, lfSustain, lfRelease,
    lfCutoff, lfResonance, lfGlide, lfLfo1Rate, lfLfo2Rate, lfCount
};

// midi controller fields
enum MidiField { midiPitchBend = 0, midiModWheel = 1, midiAftertouch = 2, midiCC0 = 100 };

struct ParamInfo
{
    juce::String name;
    float minValue = 0.0f, maxValue = 1.0f, defaultValue = 0.0f;
    juce::String suffix;
    bool logScale = false;
    juce::StringArray choices;
    enum Display { plain = 0, gainDb, panLR, percent, seconds, hertz, semitones };
    int display = plain;

    float toNormalised (float v) const
    {
        if (maxValue <= minValue) return 0.0f;
        if (logScale && minValue > 0.0f)
            return juce::jlimit (0.0f, 1.0f, std::log (juce::jmax (minValue, v) / minValue) / std::log (maxValue / minValue));
        return juce::jlimit (0.0f, 1.0f, (v - minValue) / (maxValue - minValue));
    }
    float fromNormalised (float n) const
    {
        n = juce::jlimit (0.0f, 1.0f, n);
        if (logScale && minValue > 0.0f) return minValue * std::pow (maxValue / minValue, n);
        float v = minValue + n * (maxValue - minValue);
        if (! choices.isEmpty()) v = std::round (v);
        return v;
    }
    juce::String format (float v) const;
};

// ---------------------------------------------------------------------------
enum class TriggerMode { oneShot = 0, hold, onRelease };
enum class LoopMode    { off = 0, forward, alternate };
enum class FilterType  { off = 0, lp12, lp24, hp12, hp24, bandPass, notch };
enum class EnvMode     { ahd = 0, ahdsr };
enum class LofiMode    { hifi = 0, vintage12_26, vintage12_32, crunch8_22 };
enum class LfoShape    { sine = 0, triangle, sawUp, sawDown, square, sampleHold };
enum class TrackSource { song = 0, pattern, stopped };

enum class ModSource
{
    none = 0, lfo1, lfo2, modEnv, stepMod, velocity, keyTrack, modWheel, pitchBend, aftertouch, random,
    macro1, // macro1 .. macro1 + 15
    count = macro1 + numMacros
};

enum class ModTarget
{
    pitch = 0, cutoff, resonance, volume, pan, sampleStart, saturate, lfo1Rate, lfo2Rate, count
};

juce::StringArray modSourceNames();
juce::StringArray modTargetNames();
juce::StringArray filterTypeNames();
juce::StringArray lofiNames();
juce::StringArray lfoShapeNames();

// ---------------------------------------------------------------------------
struct SampleData
{
    juce::AudioBuffer<float> buffer;                                   // stereo; empty when streamed from disk
    std::unique_ptr<juce::MemoryMappedAudioFormatReader> mapped;       // set when disk streaming
    juce::int64 numFrames = 0;
    double sampleRate = 44100.0;
    juce::File file;
    bool edited = false;          // only in memory: written out on save
    juce::String name;

    // live-stretch cache (built off the audio thread)
    std::shared_ptr<juce::AudioBuffer<float>> stretched;
    double stretchedRatio = 0.0;  // output length / input length

    bool isStreamed() const { return mapped != nullptr; }
    double lengthSeconds() const { return sampleRate > 0 ? (double) numFrames / sampleRate : 0.0; }
};
using SamplePtr = std::shared_ptr<SampleData>;

// A sample mapped to a key / velocity range of a layer.
struct Zone
{
    SamplePtr sample;
    int rootKey = 60, lowKey = 0, highKey = 127, lowVel = 1, highVel = 127;
    bool pitchTrack = true;
    bool mute = false;
    float start = 0.0f, end = 1.0f, loopStart = 0.0f, loopEnd = 1.0f; // normalised
    float tune = 0.0f;       // semitone offset for this zone
    float gainDb = 0.0f;
    std::vector<float> slices;   // slice-mode markers (normalised, sorted), kept with the zone
};

struct LfoSettings
{
    LfoShape shape = LfoShape::sine;
    float rate = 2.0f;        // Hz when not synced
    bool sync = false;
    int syncIndex = 5;        // see syncNames()
    bool retrigger = true;    // "mono": restart with each note; otherwise free-running ("poly")
};

struct ModEnvSettings
{
    float attack = 0.005f, hold = 0.0f, decay = 0.3f, sustain = 0.0f, release = 0.2f;
};

struct StepModSettings
{
    int numSteps = 16;
    int syncIndex = 1;        // 1/16
    std::array<float, 32> steps {};
    float smooth = 0.0f;
    bool retrigger = true;
};

struct ModRoute
{
    ModSource source = ModSource::lfo1;
    ModTarget target = ModTarget::cutoff;
    float amount = 0.5f;      // -1..1
    bool bypass = false;
};

struct LayerParams
{
    TriggerMode trigger = TriggerMode::oneShot;
    LoopMode loop = LoopMode::off;
    float crossfade = 0.0f;   // fraction of loop length
    int polyphony = 1;
    bool legato = false;
    float glide = 0.05f;      // seconds
    float tune = 0.0f, fine = 0.0f, gainDb = -6.0f, pan = 0.0f, saturate = 0.0f;
    bool reverse = false, liveStretch = false, diskStream = false;
    float sampleBpm = 120.0f; // tempo of the source loop (for live stretch)
    int sampleBeats = 4;      // musical duration of the sample, in beats
    bool envOn = true;
    EnvMode envMode = EnvMode::ahdsr;
    float attack = 0.001f, hold = 0.0f, decay = 0.5f, sustain = 1.0f, release = 0.05f;
    FilterType filter = FilterType::off;
    float cutoff = 20000.0f, resonance = 0.707f;
    LofiMode lofi = LofiMode::hifi;
    bool mute = false;
};

struct InsertSlot
{
    std::unique_ptr<BuiltinEffect> builtin;
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    bool bypass = false;

    bool isEmpty() const { return builtin == nullptr && plugin == nullptr; }
    juce::String getName() const
    {
        if (builtin) return builtin->getType();
        if (plugin)  return plugin->getName();
        return {};
    }
};

struct Layer
{
    juce::String name { "Layer" };
    std::vector<Zone> zones;
    LayerParams p;
    std::array<LfoSettings, 2> lfo;
    ModEnvSettings modEnv;
    StepModSettings step;
    std::vector<ModRoute> mods;
    std::array<InsertSlot, numLayerFx> fx;

    bool hasFx() const
    {
        for (auto& s : fx) if (! s.isEmpty() && ! s.bypass) return true;
        return false;
    }
};

// A mixer channel (bank, pad, audio track, AUX, main).
struct Strip
{
    juce::String name;
    float volume = 0.8f;      // linear 0..1.25
    float pan = 0.0f;
    bool mute = false, solo = false;
    float send[numAux] {};
    bool sendPre[numAux] {};
    std::vector<InsertSlot> inserts;   // unlimited
    int output = 0;                    // 0 = main, k > 0 = hardware output pair k (outs 2k+1, 2k+2)
    std::atomic<float> meterL { 0.0f }, meterR { 0.0f }, peakHold { 0.0f };
    std::atomic<float> rmsL { 0.0f }, rmsR { 0.0f };
    std::atomic<bool> clipped { false };          // latched until the clip light is clicked
    int latency = 0;                   // computed: plugin latency of this strip's inserts

    // channel strip: trim, phase, EQ, HPF / LPF, width, bypasses (see ChannelEq.h)
    ChannelEqSettings eq;
    ChannelEqDsp eqDsp;
    bool frozen = false;               // rendered to an audio track; skipped by the engine until unfrozen
};

struct PadMidi
{
    juce::String inDevice;   // empty = follow the bank
    int inChannel = 0;       // 0 = follow the bank
    juce::String outDevice;  // empty = bank output
    int outChannel = 0;      // 0 = bank channel
};

struct Pad
{
    juce::String name;
    juce::Colour colour;    // transparent = use the bank colour
    std::vector<Layer> layers;
    std::unique_ptr<juce::AudioPluginInstance> instrument;  // VST3 / AU instrument on this pad
    int padNote = 60;       // note played when the pad is hit (C3 = original pitch)
    bool keysMode = false;  // play this pad chromatically
    int choke = 0, link = 0;
    PadMidi midi;
    Strip channel;          // per-pad mixer channel (level, pan, sends, inserts)

    bool hasSound() const
    {
        if (instrument) return true;
        for (auto& l : layers) for (auto& z : l.zones) if (z.sample) return true;
        return false;
    }
    SamplePtr firstSample() const
    {
        for (auto& l : layers) for (auto& z : l.zones) if (z.sample) return z.sample;
        return nullptr;
    }
};

// ---------------------------------------------------------------------------
struct Note
{
    int pad = 0;
    int key = -1;          // -1 = pad note, otherwise MIDI key (keys mode)
    int start = 0;         // ticks from pattern start
    int length = ppq / 4;  // ticks
    float velocity = 0.8f;
    bool skipThisPass = false;
    bool selected = false; // editor selection (not saved)
};

struct AutoPoint { int tick = 0; float value = 0.0f; bool selected = false; };

struct AutomationLane
{
    juce::String param;            // parameter path (see ParamAddress)
    ParamAddress address;          // parsed form of 'param'
    std::vector<AutoPoint> points; // sorted by tick, values normalised 0..1
    bool suspended = false;        // being overwritten by a live recording

    float valueAt (double tick) const
    {
        if (points.empty()) return 0.0f;
        if (tick <= points.front().tick) return points.front().value;
        if (tick >= points.back().tick) return points.back().value;
        auto it = std::upper_bound (points.begin(), points.end(), tick,
                                    [] (double t, const AutoPoint& p) { return t < p.tick; });
        const auto& b = *it;
        const auto& a = *(it - 1);
        const double f = (tick - a.tick) / juce::jmax (1.0, (double) (b.tick - a.tick));
        return (float) (a.value + (b.value - a.value) * f);
    }
    void sort() { std::stable_sort (points.begin(), points.end(), [] (const AutoPoint& x, const AutoPoint& y) { return x.tick < y.tick; }); }
};

struct Pattern
{
    juce::String name { "Pattern" };
    int length = 4 * ppq;                   // ticks
    std::vector<Note> notes;
    std::vector<AutomationLane> automation; // pattern-local automation + MIDI params
    int lengthTicks() const { return length; }
    int bars() const { return juce::jmax (1, (length + barTicks() - 1) / barTicks()); }
};

struct BankMidi
{
    juce::String inDevice;    // empty = all devices
    int inChannel = 0;        // 0 = all channels
    bool multiChannel = false;
    juce::String outDevice;   // empty = no MIDI out
    int outChannel = 1;
    int baseNote = 0;         // MIDI note that triggers pad 1 (single channel mode)
};

struct LaunchRequest
{
    bool pending = false;
    TrackSource source = TrackSource::pattern;
    int pattern = 0;
};

struct Bank
{
    bool created = false;
    juce::String name;
    juce::Colour colour;
    std::array<Pad, numPads> pads;
    std::vector<Pattern> patterns { Pattern() };
    int workingPattern = 0;                  // pattern shown / recorded into
    TrackSource source = TrackSource::song;
    int playingPattern = 0;
    LaunchRequest queued;
    BankMidi midi;
    Strip strip;
    bool recordArm = true;
};

struct SongClip
{
    int bank = 0, pattern = 0;
    int start = 0, length = 4 * ppq;   // ticks; the pattern loops inside the clip
    int offset = 0;                    // ticks into the pattern where the clip starts (after a split)
    bool selected = false;
};

struct AudioClip
{
    SamplePtr sample;
    int start = 0;                      // ticks
    juce::int64 offset = 0;             // frames into the sample
    juce::int64 length = 0;             // frames
    float gainDb = 0.0f;
    bool selected = false;
};

struct AudioTrack
{
    bool created = false;
    juce::String name;
    juce::Colour colour;
    Strip strip;
    int inputSource = 0;     // 0 = hardware input, 1 = main output (resample), 2 + b = bank b, 200 + a = aux a
    int inputPair = 0;       // hardware stereo pair (or mono channel when inputMono)
    bool inputMono = false;
    float inputGainDb = 0.0f;
    bool gate = false;
    float gateThresholdDb = -50.0f;
    bool armed = false, monitor = false;
    TrackSource source = TrackSource::song;
    std::vector<AudioClip> clips;
    int frozenFrom = -1;     // strip key this track is the frozen render of (-1 = an ordinary track)
};

struct Scene
{
    juce::String name;
    std::array<int, numBanks> slots; // >= 0 pattern, -1 stop track, -2 play song
    Scene() { slots.fill (-1); }
};

struct MacroBinding { juce::String param; ParamAddress address; float min = 0.0f, max = 1.0f; };

struct Macro
{
    juce::String name;
    float value = 0.0f;
    std::vector<MacroBinding> bindings;
};

struct MidiMapping { int channel = 1, controller = 0; juce::String param; ParamAddress address; };

struct Session
{
    double bpm = 120.0;
    int beatsPerBar = 4;
    std::array<Bank, numBanks> banks;
    std::array<AudioTrack, maxAudioTracks> audioTracks;
    std::array<Strip, numAux> aux;
    int auxCount = 2;                         // AUX tracks shown (1..numAux)
    Strip master;
    std::vector<SongClip> song;
    std::vector<AutomationLane> songAutomation;
    std::vector<Scene> scenes;
    int sceneSyncTicks = 4 * ppq;
    bool loopOn = false;
    int loopStart = 0, loopEnd = 4 * 4 * ppq;   // ticks
    int quantize = 2;           // 0 off, 1 = 1/8, 2 = 1/16, 3 = 1/32, 4 = 1/8T, 5 = 1/16T, 6 = 1/64
    float swing = 0.0f;         // 0..0.5
    int preRollBars = 1;
    bool metronome = false;
    float metronomeVolume = 0.5f;
    bool metronomeOnlyWhenRecording = false;
    bool metronomeDuringPreRoll = true;
    float globalTuneCents = 0.0f;
    std::array<Macro, numMacros> macros;
    std::vector<MidiMapping> midiMappings;
    juce::File file;
};

// Undoable state
struct SequencerSnapshot
{
    std::vector<std::pair<int, std::vector<Pattern>>> patterns; // created banks only
    std::vector<SongClip> song;
    std::vector<std::pair<int, std::vector<AudioClip>>> audioClips;
    std::vector<AutomationLane> songAutomation;
    std::vector<Scene> scenes;
};

inline int quantizeTicks (int q)
{
    switch (q)
    {
        case 1: return ppq / 2;
        case 2: return ppq / 4;
        case 3: return ppq / 8;
        case 4: return ppq / 3;
        case 5: return ppq / 6;
        case 6: return ppq / 16;
        default: return 1;
    }
}

inline juce::StringArray quantizeNames() { return { "Off", "1/8", "1/16", "1/32", "1/8 T", "1/16 T", "1/64" }; }

inline juce::Colour bankColour (int b)
{
    static const juce::uint32 colours[] = { 0xff2fb8e8, 0xffff7a45, 0xffe7d84a, 0xffd65cd6,
                                            0xffff9f1c, 0xff4f86f7, 0xff3dd68c, 0xffef4f6e };
    return juce::Colour (colours[b % 8]).withRotatedHue ((float) (b / 8) * 0.017f);
}

inline juce::String formatTicks (double ticks)   // "bar:beat" (1-based)
{
    const int bt = barTicks();
    const int bar = (int) (ticks / bt) + 1;
    const int beat = (int) (std::fmod (ticks, (double) bt) / ppq) + 1;
    return juce::String (bar).paddedLeft ('0', 3) + " : " + juce::String (beat);
}
} // namespace cr
