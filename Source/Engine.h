#pragma once
#include <JuceHeader.h>

// The CMake build defines the app name and version; the Android (Projucer) build doesn't.
#ifndef JUCE_APPLICATION_NAME_STRING
 #define JUCE_APPLICATION_NAME_STRING "Crate XL"
#endif
#ifndef JUCE_APPLICATION_VERSION_STRING
 #if defined (JUCE_APP_VERSION)
  #define CR_STRINGIFY2(x) #x
  #define CR_STRINGIFY(x) CR_STRINGIFY2(x)
  #define JUCE_APPLICATION_VERSION_STRING CR_STRINGIFY (JUCE_APP_VERSION)
 #else
  #define JUCE_APPLICATION_VERSION_STRING "dev"
 #endif
#endif
#include "Model.h"
#include "DspUtils.h"
#include "PluginManager.h"

// ============================================================================
// App-wide behaviour settings (Settings > Behaviours), persisted by the UI.
// ============================================================================
struct Behaviours
{
    bool omni = true;                    // route all unmapped MIDI to the selected pad/bank
    int stopBehaviour = 0;               // 0 = rewind to start (or loop start), 1 = return to last play position
    bool mixerMuteAlsoMidi = true;       // muted track also mutes its MIDI out
    bool padMuteAlsoMidi = true;
    cr::TriggerMode defaultTrigger = cr::TriggerMode::oneShot;
    int defaultPolyphony = 1;
    float defaultGainDb = -6.0f;
    bool sliceAutoSnap = true;
    float previewVolumeDb = -6.0f;
    bool browserDoubleClickLoads = true;
    juce::String clockOutDevice;         // MIDI clock destination (empty = off)
    bool streamLongSamples = true;       // samples longer than 30 s stream from disk automatically
    int recordBitDepth = 24;
    bool automationReenableOnLoop = true;
    int automationQuantize = 6;          // see quantizeTicks()
};

// ============================================================================
// MIDI focus actions: one controller drives the app (pads, modes, macros, banks, transport).
// ============================================================================
struct FocusActions
{
    enum Action
    {
        triggerPad1 = 0,                  // .. triggerPad1 + 63
        modeSelect = 64, modeScene, modeMute, modeSolo, toggleKeys,
        macro1 = 69,                      // .. macro1 + 15
        selectBankA = 85,                 // .. + 7
        prevBank = 93, nextBank,
        selectGroup1 = 95,                // .. + 15
        prevGroup = 111, nextGroup, octaveDown, octaveUp,
        repeat1 = 115,                    // .. + 5
        transportPlay = 121, transportStop, transportRecord, transportTap,
        transportLoop = 125, transportMetronome, undoAction, redoAction, rewindBar, forwardBar,
        viewPerformance = 131, viewEditor, viewSequencer, viewMixer,
        returnToStart = 135,
        numActions = 136
    };

    struct Binding { bool isCC = false; int channel = 1, number = -1; };
    std::array<Binding, numActions> bindings;

    static juce::String actionName (int a);
    juce::var toVar() const;
    void fromVar (const juce::var& v);
};

// Audio recorder (sampling onto pads)
struct RecorderSettings
{
    int source = 0;              // 0 hardware input, 1 main output, 2 + b bank b, 200 + a aux a
    int inputPair = 0;           // hardware stereo pair (or mono channel when mono)
    bool mono = false;
    int destBank = 0, destPad = 0;
    bool moveToNextPad = false;
    bool useThreshold = false;
    float thresholdDb = -30.0f;
    float maxSeconds = 300.0f;
};

// Mono ring buffer the audio thread writes and the analyzer display reads.
struct AnalyzerTap
{
    static constexpr int size = 16384;
    std::array<float, size> data {};
    std::atomic<int> write { 0 };
    void push (const juce::AudioBuffer<float>& b, int n)
    {
        int w = write.load (std::memory_order_relaxed);
        const float* l = b.getReadPointer (0);
        const float* r = b.getReadPointer (juce::jmin (1, b.getNumChannels() - 1));
        for (int i = 0; i < n; ++i) { data[(size_t) w] = 0.5f * (l[i] + r[i]); w = (w + 1) & (size - 1); }
        write.store (w, std::memory_order_release);
    }
    void read (float* dest, int count) const
    {
        const int w = write.load (std::memory_order_acquire);
        for (int i = 0; i < count; ++i) dest[i] = data[(size_t) ((w - count + i) & (size - 1))];
    }
};

struct ExportOptions
{
    bool stems = false;              // one file per track instead of the main mix
    std::vector<int> trackKeys;      // stems: 0..127 banks, 1000 + t audio tracks, 2000 + a aux (empty = all)
    int startTick = 0, endTick = 0;
    int bitDepth = 24;               // 16, 24, 32 (float) or 12 (vintage: 12-bit quantised, stored as 16-bit)
    bool normalize = false;
    double sampleRate = 0.0;         // 0 = device rate
    bool includeTail = true;         // render 2 extra seconds for reverb / delay tails
    bool bypassMaster = false;       // leave the main bus effects and fader out (bounces)
    bool includeReturns = false;     // stems: also print the AUX returns the channel feeds
    bool writeToExactFile = false;   // single stem: write to 'file' itself, not "file - name.wav"
};

struct BounceOptions
{
    bool printSends = false;         // include what the channel sends to the AUX returns
    bool freeze = false;             // freeze: render at unity, the new track takes over the fader and sends
    bool muteSource = true;
    bool removeSource = false;       // audio tracks only
    bool includeTail = true;
    int startTick = 0, endTick = 0;  // 0 / 0 = the whole song
};

// ============================================================================
class Engine : public juce::AudioIODeviceCallback,
               public juce::MidiInputCallback,
               public juce::ChangeBroadcaster,
               private juce::Timer,
               private juce::AsyncUpdater
{
public:
    Engine (bool openDevices = true);
    ~Engine() override;

    static Engine& get() { return *instance; }

    cr::Session& session() { return sess; }
    juce::CriticalSection& getLock() { return lock; }
    double getSampleRate() const { return sampleRate; }

    juce::AudioDeviceManager deviceManager;
    juce::AudioFormatManager formatManager;
    PluginManager plugins;
    Behaviours behaviours;
    FocusActions focus;
    juce::ThreadPool workers { juce::ThreadPoolOptions{}.withNumberOfThreads (2).withThreadName ("Crate worker") };

    // ---- UI / focus state ------------------------------------------------------
    std::atomic<int> selectedBank { 0 }, selectedPad { 0 }, selectedLayer { 0 }, selectedZone { 0 };
    std::atomic<int> padPage { 0 }, padsPerPage { 16 };       // pages over 128 pads
    std::atomic<int> padMode { 0 };                           // 0 play, 1 select, 2 mute, 3 solo, 4 scene
    std::atomic<int> repeatSlot { -1 };                       // -1 = repeat off
    std::array<int, cr::numRepeatSlots> repeatTicks { cr::ppq, cr::ppq / 2, cr::ppq / 3, cr::ppq / 4, cr::ppq / 6, cr::ppq / 8 };
    std::array<float, cr::numRepeatSlots> repeatSwing {};
    std::atomic<int> keysOctave { 3 };                        // keys-mode pad grid starts at C(keysOctave)
    std::atomic<bool> fixedVelocityOn { true }, yAxisVelocity { false }, fixedMidiVelocity { false };
    std::atomic<float> fixedVelocity { 100.0f / 127.0f };
    std::atomic<int> scaleIndex { 0 }, scaleRoot { 0 }, chordIndex { 0 };
    std::atomic<bool> uiStateChanged { false };   // set by focus actions; UI refreshes
    static juce::StringArray getScaleNames();
    static juce::StringArray getChordNames();
    bool isKeyInScale (int key) const;

    // ---- transport ------------------------------------------------------------------
    void play();
    void stop();
    void togglePlay() { if (playing) stop(); else play(); }
    bool isPlaying() const { return playing; }
    void setRecording (bool shouldRecord);
    bool isRecording() const { return recording; }
    bool isInPreRoll() const { return inPreRoll.load(); }
    std::atomic<bool> automationRecord { false };
    double getPositionTicks() const { return uiPosition.load(); }
    double getTrackPatternPosition (int bank) const;   // -1 if the track isn't looping a pattern
    void setPosition (double ticks);
    void rewind();
    void returnToStart() { setPosition (0.0); }   // back to bar 1 (keeps playing if playing)
    void fastForward();
    void setBpm (double bpm);
    double getBpm() const { return sess.bpm; }
    void setBeatsPerBar (int beats);
    void tapTempo();
    double ticksToSeconds (double ticks) const { return ticks / cr::ppq * 60.0 / sess.bpm; }
    float getCpuLoad() const { return (float) deviceManager.getCpuUsage(); }

    // ---- tracks, patterns, scenes ---------------------------------------------------
    void createBank (int bank);
    void deleteBank (int bank);
    void playSong (int bank = -1);                 // hand a track (or all with -1) back to the song
    void launchPattern (int bank, int pattern);    // quantised to the scene sync value while playing
    void stopTrack (int bank);
    void launchScene (int scene);
    void captureScene (int scene);
    void stopAllScenes();
    int addPattern (int bank, const juce::String& name = {}, int lengthTicks = 0);
    int duplicatePattern (int bank, int pattern);
    void deletePattern (int bank, int pattern);
    int createAudioTrack();
    void deleteAudioTrack (int track);
    int findTrackPatternOnSong (int bank) const;

    // ---- performance ------------------------------------------------------------------
    void padDown (int bank, int pad, float velocity, int key = -1);
    void padUp (int bank, int pad, int key = -1);
    void allNotesOff();
    void midiPanic();
    std::array<std::atomic<float>, cr::numBanks> pitchBend, modWheel, aftertouch;
    std::array<std::atomic<bool>, cr::numBanks> sustainPedal;
    std::atomic<float> padFlash[cr::numBanks][cr::numPads];
    std::atomic<int> lastPlayedKey { 60 };

    // ---- samples --------------------------------------------------------------------------
    enum class LoadMethod { replace = 0, newLayer, addToLayer };
    cr::SamplePtr loadSample (const juce::File& file, bool streamFromDisk = false);
    bool loadSampleToPad (const juce::File& file, int bank, int pad, LoadMethod method = LoadMethod::replace);
    bool setPadSample (int bank, int pad, cr::SamplePtr sample, LoadMethod method = LoadMethod::replace);
    void setZoneSample (int bank, int pad, int layer, int zone, cr::SamplePtr sample);
    int addLayer (int bank, int pad);                 // returns new layer index
    void removeLayer (int bank, int pad, int layer);
    void removeZone (int bank, int pad, int layer, int zone);
    void moveZone (int bank, int pad, int fromLayer, int zone, int toLayer);
    void clearPad (int bank, int pad);
    void setLayerStreaming (int bank, int pad, int layer, bool stream);
    void preview (const juce::File& file);
    void previewBuffer (cr::SamplePtr sample, float start = 0.0f, float end = 1.0f);
    void stopPreview();
    bool isPreviewing() const { return previewSample != nullptr; }
    float getPreviewPosition() const { return previewPosNorm.load(); }

    // recorder
    RecorderSettings recorder;
    void startSampling();                 // arms (threshold) or starts recording with 'recorder'
    void stopSampling();                  // finishes and loads the take onto the destination pad
    bool isSampling() const { return sampling.load(); }
    bool isWaitingForThreshold() const { return samplingWaiting.load(); }
    float getInputLevel() const { return inputLevel.load(); }
    double getSamplingSeconds() const { return samplingLength / juce::jmax (1.0, sampleRate); }
    juce::StringArray getInputPairNames() const;
    juce::StringArray getInputChannelNames() const;   // every channel of the input device
    juce::StringArray getOutputPairNames() const;

    // Input device choice (CoreAudio / WASAPI / DirectSound keep inputs and outputs separate;
    // ASIO uses one device for both, so the input device follows the output device).
    juce::StringArray getInputDeviceNames() const;
    juce::String getInputDeviceName() const;
    bool canChooseInputDevice() const;
    juce::String setInputDevice (const juce::String& name);
    // Makes sure the given hardware input channels are switched on (the device only opens
    // the channels it's asked for). Returns an error message, or empty.
    juce::String requireInputChannels (int firstChannel, int numChannels);

    enum class SampleEdit { normalize, reverse, trim, cut, copy, paste, deleteRange, silence, fadeIn, fadeOut, stretch, pitch, gain, dcRemove };
    // Edits operate on [selStart, selEnd) (normalised) of the zone's sample; a copy is made, the file is untouched.
    bool editSample (int bank, int pad, int layer, int zone, SampleEdit edit, float selStart, float selEnd, double amount = 0.0);
    bool hasAudioClipboard() const { return audioClipboard.getNumSamples() > 0; }
    float snapToZeroCrossing (const cr::SampleData& s, float pos) const;
    bool duplicateSampleFile (int bank, int pad, int layer, int zone);

    enum class SliceTarget { pads, singleLayer, multipleLayers, files };
    // mode 0 = split into 'amount' equal slices, 1 = grid ('amount' = quantize index of a beat division,
    // using the layer's sample BPM), 2 = transients ('sensitivity' 0..1)
    std::vector<float> autoSlice (int bank, int pad, int layer, int zone, int mode, int amount, float sensitivity);
    bool applySlices (int bank, int pad, int layer, int zone, const std::vector<float>& slicePoints,
                      SliceTarget target, int destBank, int startPadOrKey, int chokeGroup, bool createPattern,
                      float fadeInMs, float fadeOutMs, juce::String& error);
    double detectBpm (const cr::SampleData& s, int beats) const { return beats * 60.0 / juce::jmax (0.01, s.lengthSeconds()); }

    // ---- plugins & inserts -----------------------------------------------------------------
    void setInsertBuiltin (std::vector<cr::InsertSlot>& chain, int slot, const juce::String& type);   // slot == size() appends
    bool setInsertPlugin (std::vector<cr::InsertSlot>& chain, int slot, const juce::PluginDescription& desc, juce::String& error);
    void removeInsert (std::vector<cr::InsertSlot>& chain, int slot);
    void moveInsert (std::vector<cr::InsertSlot>& chain, int from, int to);
    void setLayerFx (int bank, int pad, int layer, int slot, const juce::String& builtinType);
    bool setLayerFxPlugin (int bank, int pad, int layer, int slot, const juce::PluginDescription& desc, juce::String& error);
    void clearLayerFx (int bank, int pad, int layer, int slot);
    bool setPadInstrument (int bank, int pad, const juce::PluginDescription& desc, juce::String& error);
    void clearPadInstrument (int bank, int pad);
    void copyPad (int bank, int pad);
    void pastePad (int bank, int pad);
    bool hasPadClipboard() const { return padClipboard != nullptr; }
    cr::Strip* getStrip (int key);
    // Spectrum analyzer: the mixer picks one channel to watch (nullptr = none).
    std::atomic<cr::Strip*> analyzerStrip { nullptr };
    AnalyzerTap analyzerPre, analyzerPost;   // into the EQ / out of the EQ   // 0..127 bank, 1000 + t audio, 2000 + a aux, 3000 master, 10000 + b * 128 + p pad

    // ---- parameters, macros, MIDI learn -----------------------------------------------------
    bool getParamInfo (const cr::ParamAddress& a, cr::ParamInfo& info);
    float getParamNormalised (const cr::ParamAddress& a);
    void setParamNormalised (const cr::ParamAddress& a, float norm, bool fromUser = true);
    void setParamValue (const cr::ParamAddress& a, float value, bool fromUser = true);
    float getParamValue (const cr::ParamAddress& a);
    juce::String getParamDisplayName (const juce::String& path);
    void setMacro (int index, float value);
    void bindMacro (int index, const juce::String& param);
    void unbindMacro (int index, int bindingIndex);
    int findMacroFor (const juce::String& param) const;
    void armMidiLearn (const juce::String& param) { learnTarget = param; learnArmed = true; }
    void cancelMidiLearn() { learnArmed = false; }
    bool isMidiLearnArmedFor (const juce::String& param) const { return learnArmed && learnTarget == param; }
    void forgetMidiMapping (const juce::String& param);
    bool hasMidiMapping (const juce::String& param) const;
    std::atomic<bool> focusLearnArmed { false };
    std::function<void (int view)> onShowView;                            // message thread: MIDI-mapped view buttons
    std::function<void (const juce::MidiMessage&)> onFocusLearnMessage;   // message thread
    std::function<void (int note)> onMappingLearnNote;                    // message thread (Mapping page LEARN)
    std::atomic<bool> mappingLearnArmed { false };
    void rebuildMidiTables();
    cr::AutomationLane* findOrCreateLane (const juce::String& param, bool songLevel, int bank);
    juce::StringArray getAutomatableParams (int trackKey);   // parameter paths for the lane picker

    // ---- undo ---------------------------------------------------------------------------------
    void checkpoint();                       // sequencer state
    void checkpointPad (int bank, int pad);  // pad / sample edits
    bool undo();
    bool redo();
    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }

    // ---- files (SessionIO.cpp) ----------------------------------------------------------------------
    void newSession();
    bool saveSession (const juce::File& file, juce::String& error, bool copySamples = false);
    bool loadSession (const juce::File& file, juce::String& error);
    bool saveBankPreset (int bank, const juce::File& file, juce::String& error, bool savePatterns = true);
    bool loadBankPreset (int bank, const juce::File& file, juce::String& error, bool loadPatterns = true);
    // Bank clipboard: copy a whole bank (pads, samples, effects, patterns) and paste it over another or into a new one.
    bool copyBank (int bank, juce::String& error);
    bool pasteBank (int bank, juce::String& error);
    bool hasBankClipboard() const;
    int firstFreeBank() const;   // -1 when all 128 banks are in use
    int duplicateBank (int bank, juce::String& error);   // copy of pads, samples, settings and patterns in the next free bank; -1 on failure

    // Pattern clipboard (works across banks: pads keep their numbers)
    void copyPattern (int bank, int pattern);
    int pastePattern (int bank);                         // new pattern index, -1 when the clipboard is empty
    bool hasPatternClipboard() const { return ! patternClipboard.empty(); }

    // Sequence clipboard: song clips together with their patterns
    int copySongClips();                                 // copies the selected song clips; returns how many
    int pasteSongClips (int atTick, int targetBank);     // targetBank -1 = each clip's own bank; patterns are pasted as new copies
    bool hasSongClipboard() const { return ! songClipboard.empty() || ! songAudioClipboard.empty(); }
    bool exportPack (const juce::File& file, const juce::String& packName, const juce::String& author, juce::String& error);
    bool installPack (const juce::File& file, juce::String& error);
    bool importMidi (const juce::File& file, int bank, juce::String& error, bool generalMidiMap = false);
    bool exportPatternMidi (int bank, int pattern, const juce::File& file, juce::String& error);
    bool importAudioToTracks (const juce::StringArray& files, juce::String& error);

    // Resampling (EngineResample.cpp). Strip keys as in getStrip(). Rendering runs offline on the calling thread.
    int bounceToTrack (int stripKey, const BounceOptions& options, juce::String& error, std::function<bool (float)> progress = {});
    bool unfreeze (int stripKey);
    bool isFrozen (int stripKey);
    juce::String stripDisplayName (int stripKey);
    cr::SamplePtr clipToSample (int track, int clip);                  // the clip's audio as a new in-memory sample
    bool exportClip (int track, int clip, const juce::File& file, juce::String& error);
    int firstEmptyPad (int bank, int from = 0);                         // -1 when the bank is full

    // Stem separation (EngineStems.cpp). renderStems runs on any thread and writes
    // Vocals / Drums / Bass / Other WAVs to Samples/Stems/<name>; placeStemsInNewBank
    // (message thread) loads them onto pads 1-4 of a new bank, with the full mix on pad 5.
    struct StemSet { juce::String name; juce::StringArray files; cr::SamplePtr full; };
    cr::SamplePtr padSample (int bank, int pad);
    bool renderStems (cr::SamplePtr source, const juce::String& name, StemSet& result, juce::String& error, std::function<bool (float)> progress = {});
    int placeStemsInNewBank (const StemSet& set, juce::String& error);

    // Stem import (EngineImport.cpp). prepareStem runs on any thread; addStemTracks on the message thread.
    struct Stem
    {
        juce::File file;
        juce::String name, error;
        cr::SamplePtr sample;
        double timeSeconds = -1.0;   // Broadcast WAV time reference, -1 when there isn't one
    };
    Stem prepareStem (const juce::File& file);
    int addStemTracks (std::vector<Stem>& stems, int originTick, juce::String& error);
    static juce::String importableExtensions();
    static bool isVideoFile (const juce::File& f);
    static double readBroadcastTimeSeconds (const juce::File& f);
    static bool decodeWithPlatform (const juce::File& file, juce::AudioBuffer<float>& out, double& sampleRate, juce::String& error);
    bool exportAudio (const juce::File& file, const ExportOptions& options, juce::String& error,
                      std::function<bool (float)> progress = {});
    bool saveSliceFiles (int bank, int pad, int layer, int zone, const std::vector<float>& slices, juce::String& error);
    int getSongLengthTicks() const;
    static juce::File getLibraryFolder();
    static juce::File getSamplesFolder() { return getLibraryFolder().getChildFile ("Samples"); }
    static juce::File getSessionsFolder() { return getLibraryFolder().getChildFile ("Sessions"); }
    static juce::File getBanksFolder() { return getLibraryFolder().getChildFile ("Banks"); }
    static juce::File getPacksFolder() { return getLibraryFolder().getChildFile ("Packs"); }
    static juce::File getRecordingsFolder() { return getLibraryFolder().getChildFile ("Recordings"); }
    static juce::File getExportsFolder() { return getLibraryFolder().getChildFile ("Exports"); }
    juce::File writeSampleToRecordings (const juce::AudioBuffer<float>& buffer, double sr, const juce::String& name);

    // ---- audio / MIDI device callbacks ---------------------------------------------------------------
    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message) override;

    juce::StringArray getMidiOutputNames() const { return midiOutputNames; }
    juce::StringArray getMidiInputNames() const;
    void refreshMidiOutputs();
    void updateLatencyCompensation();
    void notifyStructureChanged();   // instruments / overrides changed: rebuild audio-thread lists
    int getTotalLatency() const { return maxStripLatency; }
    int getActiveVoiceCount() const { return activeVoices.load(); }

    // Offline rendering (tests and export): renders n samples into 'out' without a device.
    void prepareOffline (double sr, int blockSize) { prepare (sr, blockSize); }
    void renderOffline (juce::AudioBuffer<float>& out, int numSamples);

private:
    struct Voice
    {
        bool active = false;
        int bank = -1, pad = -1, layer = -1, zone = -1, key = -1;
        float velocity = 1.0f;
        cr::SamplePtr sample;
        std::shared_ptr<juce::AudioBuffer<float>> stretched;
        juce::int64 len = 0;
        double pos = 0.0, baseInc = 1.0, startS = 0.0, endS = 0.0, loopS = 0.0, loopE = 0.0;
        bool reverse = false, forwardDir = true, released = false;
        cr::LoopMode loop = cr::LoopMode::off;
        float glideSemis = 0.0f, glideTarget = 0.0f, baseSemis = 0.0f;
        Envelope amp, modEnv;
        Svf svf[2][2];
        double lfoPhase[2] {};
        float lfoHeld[2] {}, randomValue = 0.0f;
        double stepPhase = 0.0;
        float stepValue = 0.0f;
        juce::uint32 age = 0;
        int fadeOut = -1, fadeIn = 0;
        float zoneGain = 1.0f;
        double lofiAcc = 1.0;
        float lofiHold[2] {};
        std::vector<float> winL, winR;       // disk streaming window
        juce::int64 winStart = 0;
        int winLen = 0;
    };

    struct Event
    {
        int offset = 0, bank = 0, pad = 0, key = -1;
        float velocity = 0.0f;
        bool on = true, fromSequencer = false;
    };

    struct LiveEvent { int bank, pad, key; float velocity; bool on; };
    struct MidiIn { juce::MidiMessage msg; juce::String device; };
    struct Bus { int key = -1; juce::AudioBuffer<float> buffer; };
    struct DelayComp { juce::AudioBuffer<float> buffer; int writePos = 0, delay = 0; };

    struct UndoEntry
    {
        bool isPad = false;
        cr::SequencerSnapshot seq;
        int bank = 0, pad = 0;
        juce::var padData;
        std::vector<cr::SamplePtr> samples;
    };

    void prepare (double sr, int blockSize);
    void renderBlock (const float* const* in, int numIn, float* const* out, int numOut, int numSamples);
    void renderChunk (const float* const* in, int numIn, float* const* out, int numOut, int numSamples);
    void handleLiveEvent (const LiveEvent& e, int offset, double tickAtOffset);
    void routeMidiInput (int n);
    void routeMidiMessage (const juce::MidiMessage& m, const juce::String& device, int offset);
    void deliverToPad (int bank, int pad, int key, float vel, bool on, int offset);
    void addEvent (const Event& e);
    void applyEvent (const Event& e);
    void mirrorMidiOut (int bank, int pad, int key, float vel, bool on, int offset);
    void collectSequencerEvents (int n);
    void collectRange (double from, double to, double offsetBase);
    void collectPatternNotes (int bank, cr::Pattern& pattern, double origin, double from, double to, double clipStart, double clipEnd, int clipOffset);
    void applyAutomation (double tick);
    void applyLanes (std::vector<cr::AutomationLane>& lanes, double tick);
    void applyLaunch (int bank, double atTick);
    void startVoices (int bank, int pad, int key, float velocity, bool releaseTrigger);
    void startLayerVoice (int bank, int pad, int layer, int zone, int key, float velocity);
    void stopVoices (int bank, int pad, int key);
    void chokeGroup (int bank, int group, int exceptPad);
    void renderVoices (int start, int end);
    void renderVoice (Voice& v, int start, int end);
    void fetchWindow (Voice& v, juce::int64 first, juce::int64 last);
    juce::AudioBuffer<float>* getBus (std::vector<Bus>& pool, int& used, int key, int n);
    void processStrip (cr::Strip& strip, juce::AudioBuffer<float>& buffer, int n);
    void processChannel (cr::Strip& st, juce::AudioBuffer<float>& buf, int n);
    // Live resampling sources beyond main / banks / AUX: 400 + t = audio track t, 10000 + b * 128 + p = pad (instrument) output.
    bool copyInternalSource (int source, int n, int requestingTrack);
    void processChain (std::vector<cr::InsertSlot>& chain, juce::AudioBuffer<float>& buffer, int n);
    void processSlot (cr::InsertSlot& slot, juce::AudioBuffer<float>& buffer, int n);
    void processPlugin (juce::AudioPluginInstance& plugin, juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, int n);
    void mixStripTo (cr::Strip& st, juce::AudioBuffer<float>& src, int n, bool audible, int delayKey, juce::AudioBuffer<float>& dest);
    void meter (cr::Strip& st, const juce::AudioBuffer<float>& b, int n);
    void renderAudioTracks (const float* const* in, int numIn, int n);
    void renderPreview (float* const* out, int numOut, int n);
    void captureSampling (const float* const* in, int numIn, int n);
    void prepareAllProcessors();
    void prepareChain (std::vector<cr::InsertSlot>& chain);
    void retire (cr::SamplePtr s) { if (s) graveyard.push_back (std::move (s)); }
    void retirePlugin (std::unique_ptr<juce::AudioPluginInstance> p);
    cr::SequencerSnapshot makeSnapshot() const;
    void restoreSnapshot (const cr::SequencerSnapshot& s);
    UndoEntry makePadEntry (int bank, int pad);
    void restorePadEntry (const UndoEntry& e);
    void pushUndo (UndoEntry e);
    void timerCallback() override;
    void handleAsyncUpdate() override;
    std::vector<int> expandKeys (int key) const;
    void resetSessionToDefaults();
    void initialiseBank (int b);
    void resetAudioTrack (cr::AudioTrack& tr, int t);
    void finishAudioRecording();
    void finishSampling();
    void requestStretches();
    cr::Layer makeDefaultLayer() const;
    float* resolveFloat (const cr::ParamAddress& a);
    cr::Strip* resolveStrip (const cr::ParamAddress& a);
    cr::InsertSlot* resolveSlot (const cr::ParamAddress& a);
    void recordAutomationPoint (const juce::String& param, const cr::ParamAddress& a, float norm);
    void handleFocusAction (int action, float value, bool on);
    void sendClock (int n);

    static Engine* instance;

    cr::Session sess;
    juce::CriticalSection lock;
    double sampleRate = 44100.0;
    int maxBlock = 512;

    // transport (guarded by lock)
    bool playing = false, recording = false;
    std::atomic<bool> inPreRoll { false };
    double preRollLeft = 0.0;            // ticks of count-in left
    double position = 0.0, playStartPosition = 0.0, recordStartTick = 0.0;
    std::array<bool, cr::numBanks> recordedInto {};   // banks that got notes during the current recording
    int lastLaunchedScene = 0;
    std::vector<cr::Pattern> patternClipboard;
    struct CopiedClip { cr::SongClip clip; cr::Pattern pattern; };
    std::vector<CopiedClip> songClipboard;
    struct CopiedAudioClip { int track; cr::AudioClip clip; };
    std::vector<CopiedAudioClip> songAudioClipboard;
    void commitRecordedPatterns (double endTick, bool backToSong, bool final = true);
    void growPatternTo (cr::Pattern& pat, double relTick);     // lock held: put what was recorded into the song and scenes
    std::array<double, cr::numBanks> patternOrigin {};
    std::atomic<double> uiPosition { 0.0 };
    std::vector<double> tapTimes;
    double lastClockTick = -1.0;
    struct TickSeg { int offset = 0, count = 0; double tick = 0.0; };
    std::array<TickSeg, 4> segs;
    int numSegs = 0;
    double curTps = 1.0;                 // ticks per sample
    int curBlock = 0;
    std::set<cr::SampleData*> pendingStretch;
    std::atomic<int> activeVoices { 0 };
    double freeClockSeconds = 0.0;      // free-running clock for "poly" LFOs
    juce::Random rng;

    // voices + events
    std::vector<Voice> voices;
    juce::uint32 voiceCounter = 0;
    std::vector<Event> events;
    std::vector<LiveEvent> liveQueue, liveScratch;
    juce::SpinLock liveLock;
    std::vector<MidiIn> midiQueue, midiScratch;
    juce::SpinLock midiQueueLock;

    // buses
    std::vector<Bus> padBuses, layerBuses;
    int padBusesUsed = 0, layerBusesUsed = 0;
    std::vector<int> instrumentPads;     // keys bank * numPads + pad of pads hosting an instrument
    std::vector<juce::MidiBuffer> instrumentMidi;   // parallel to instrumentPads
    std::array<juce::AudioBuffer<float>, cr::numBanks> bankBuffers;
    std::array<bool, cr::numBanks> bankActive {};
    std::array<int, cr::numBanks> bankTail {};
    std::array<juce::AudioBuffer<float>, cr::numAux> auxBuffers;
    std::array<juce::AudioBuffer<float>, cr::maxAudioTracks> trackBuffers;
    juce::AudioBuffer<float> masterBuffer, pluginBuffer, scratchBuffer, layerScratch;
    std::vector<juce::AudioBuffer<float>> hwOutBuffers;   // extra hardware output pairs
    juce::MidiBuffer emptyMidi;
    std::map<int, DelayComp> delayComps;  // key: 0..127 banks, 1000 + t audio tracks
    int maxStripLatency = 0;

    // note repeat + live recording
    float heldVelocity[cr::numBanks][cr::numPads] {};
    int heldKey[cr::numBanks][cr::numPads] {};
    double repeatNextTick = 0.0;
    struct PendingRecord { int bank, pad, key, noteIndex, pattern; double startTick; };
    std::vector<PendingRecord> pendingRecords;
    struct PendingRelease { int bank, pad, key; float velocity; };
    std::vector<PendingRelease> onReleaseNotes;
    std::array<bool, cr::numBanks> sustainWasDown {};
    std::vector<std::pair<int, int>> sustainedNotes;   // (bank * numPads + pad, key) held by the pedal
    struct ScheduledOff { double samplesLeft; int bank, pad, key; };
    std::vector<ScheduledOff> scheduledOffs;          // sequencer note-offs, counted down in samples

    // metronome
    int clickSamplesLeft = 0;
    double clickPhase = 0.0, clickFreq = 1000.0;

    // sampling onto a pad
    std::atomic<bool> sampling { false }, samplingWaiting { false }, samplingDone { false };
    int samplingLength = 0;
    juce::AudioBuffer<float> samplingBuffer;
    std::atomic<float> inputLevel { 0.0f };
    std::array<int, 32> activeInputToHardware {};    // device callback's packed input index -> hardware channel
    std::vector<float> clipStreamL, clipStreamR;     // disk-streamed clip window (pre-allocated)

    // audio track recording
    juce::TimeSliceThread diskWriterThread { "Crate disk writer" };
    std::array<std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter>, cr::maxAudioTracks> trackWriters;
    std::array<juce::File, cr::maxAudioTracks> trackRecordFiles;
    std::array<juce::int64, cr::maxAudioTracks> trackRecordedFrames {};
    double audioRecordStartTick = 0.0;
    bool audioRecordingActive = false;
    std::array<float, cr::maxAudioTracks> gateEnv {};

    // preview
    cr::SamplePtr previewSample;
    double previewPos = 0.0, previewEnd = 0.0;
    std::atomic<float> previewPosNorm { 0.0f };

    // MIDI out
    std::map<juce::String, std::unique_ptr<juce::MidiOutput>> midiOutputs;   // by device name
    std::map<juce::String, juce::MidiBuffer> midiOutBuffers;
    juce::StringArray midiOutputNames;
    double clockAccumulator = 0.0;
    bool clockRunning = false;

    // MIDI learn / focus lookup tables (read on the MIDI thread)
    std::array<std::atomic<bool>, 16 * 128> ccIsMapped;
    std::array<std::atomic<short>, 2 * 16 * 128> focusTable;    // [isCC][channel][number] -> action, -1 none
    juce::String learnTarget;
    std::atomic<bool> learnArmed { false };
    struct UiMidi { juce::MidiMessage msg; int action; };
    std::vector<UiMidi> uiMidiQueue;
    juce::SpinLock uiMidiLock;

    // export
    bool offlineRender = false;
    int exportSoloKey = -1;
    bool exportBypassMaster = false, exportIncludeReturns = false;

    // automation touch: parameters the user is currently moving are not overwritten by playback
    std::map<juce::String, double> touchedUntil;

    // clipboards
    juce::AudioBuffer<float> audioClipboard;
    double audioClipboardRate = 44100.0;
    struct PadClip { juce::var data; std::vector<cr::SamplePtr> samples; };
    std::shared_ptr<PadClip> padClipboard;

    std::vector<UndoEntry> undoStack, redoStack;

    std::vector<cr::SamplePtr> graveyard;
    std::vector<std::unique_ptr<juce::AudioPluginInstance>> pluginGraveyard;

    friend struct Serializer;
    friend struct EngineTestAccess;
};
