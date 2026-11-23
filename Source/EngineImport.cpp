// Importing audio: OS decoders for formats JUCE doesn't read, video soundtracks,
// Broadcast WAV timestamps, and multi-stem import onto new audio tracks.
#include "Engine.h"
#include "FileUtils.h"

#if JUCE_WINDOWS
bool crDecodeMediaFoundation (const wchar_t* path, std::vector<float>& out, int& channels, double& sampleRate, double maxSeconds, std::string& error);
#elif JUCE_MAC
bool crDecodeAVFoundation (const char* utf8Path, std::vector<float>& out, int& channels, double& sampleRate, double maxSeconds, std::string& error);
#endif

juce::String Engine::importableExtensions()
{
    return "wav;bwf;aif;aiff;aifc;flac;ogg;oga;mp3;caf;m4a;aac;mp4;m4v;mov;mkv;avi;wma;opus;wv;dsf;dff";
}

bool Engine::isVideoFile (const juce::File& f)
{
    return f.hasFileExtension ("mp4;m4v;mov;mkv;avi");
}

bool Engine::decodeWithPlatform (const juce::File& file, juce::AudioBuffer<float>& out, double& sampleRate, juce::String& error)
{
    std::vector<float> data;
    int channels = 0;
    std::string err;
    bool ok = false;
   #if JUCE_WINDOWS
    ok = crDecodeMediaFoundation (file.getFullPathName().toWideCharPointer(), data, channels, sampleRate, 1200.0, err);
   #elif JUCE_MAC
    ok = crDecodeAVFoundation (file.getFullPathName().toRawUTF8(), data, channels, sampleRate, 1200.0, err);
   #else
    juce::ignoreUnused (file, sampleRate);
    err = "this format needs the Windows or macOS decoders";
   #endif
    if (! ok || channels <= 0)
    {
        error = juce::String (err);
        if (error.isEmpty()) error = "not a format this computer can decode";
        return false;
    }
    const int frames = (int) (data.size() / (size_t) channels);
    out.setSize (2, frames);
    for (int c = 0; c < 2; ++c)
    {
        auto* d = out.getWritePointer (c);
        const int src = juce::jmin (c, channels - 1);
        for (int i = 0; i < frames; ++i) d[i] = data[(size_t) i * (size_t) channels + (size_t) src];
    }
    return frames > 0;
}

double Engine::readBroadcastTimeSeconds (const juce::File& file)
{
    if (! file.hasFileExtension ("wav;bwf")) return -1.0;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> r (wav.createReaderFor (file.createInputStream().release(), true));
    if (r == nullptr || r->sampleRate <= 0) return -1.0;
    const auto ref = r->metadataValues.getValue (juce::WavAudioFormat::bwavTimeReference, {});
    if (ref.isEmpty()) return -1.0;
    return (double) ref.getLargeIntValue() / r->sampleRate;
}

Engine::Stem Engine::prepareStem (const juce::File& file)
{
    Stem st;
    st.file = file;
    st.name = file.getFileNameWithoutExtension();
    st.timeSeconds = readBroadcastTimeSeconds (file);

    if (isVideoFile (file))
    {
        // Pull the soundtrack out of the video into a WAV in the library, so the session
        // refers to plain audio from now on (the video itself is left untouched).
        juce::AudioBuffer<float> b;
        double sr = 0.0;
        juce::String err;
        if (! decodeWithPlatform (file, b, sr, err)) { st.error = file.getFileName() + ": " + err; return st; }
        auto folder = getRecordingsFolder().getChildFile ("Extracted");
        folder.createDirectory();
        auto wavFile = folder.getNonexistentChildFile (st.name, ".wav");
        if (auto w = createWavWriter (wavFile, sr, 2, 24))
        {
            w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            w.reset();
            st.sample = loadSample (wavFile, true);
        }
        if (! st.sample) st.error = file.getFileName() + ": couldn't write the extracted audio";
        return st;
    }

    // WAV / AIFF stems stream from disk (memory-mapped), so hundreds of them don't fill the RAM.
    st.sample = loadSample (file, file.hasFileExtension ("wav;bwf;aif;aiff;aifc"));
    if (! st.sample)
    {
        juce::String why = "not a supported audio format";
        if (file.hasFileExtension ("dsf;dff")) why = "DSD files (DSF / DFF) aren't supported yet - convert them to WAV first";
        else if (file.hasFileExtension ("wv")) why = "WavPack isn't supported yet - convert it to WAV or FLAC first";
        else if (file.hasFileExtension ("opus")) why = "Opus isn't supported yet - convert it to WAV, FLAC or OGG first";
        st.error = file.getFileName() + ": " + why;
    }
    return st;
}

int Engine::addStemTracks (std::vector<Stem>& stems, int originTick, juce::String& error)
{
    // Timestamps are kept relative to each other: the earliest stamped file lands on
    // originTick and the rest keep their exact distance from it. Files without a stamp
    // start at originTick, so a batch of plain stems stays perfectly lined up.
    double earliest = -1.0;
    for (auto& s : stems)
        if (s.sample && s.timeSeconds >= 0.0) earliest = earliest < 0.0 ? s.timeSeconds : juce::jmin (earliest, s.timeSeconds);

    checkpoint();
    int added = 0;
    const double ticksPerSecond = sess.bpm / 60.0 * cr::ppq;
    for (auto& s : stems)
    {
        if (! s.sample) { if (s.error.isNotEmpty()) error << s.error << "\n"; continue; }
        const int t = createAudioTrack();
        if (t < 0) { error << "No free audio tracks (the limit is " << cr::maxAudioTracks << ").\n"; break; }
        const double offsetSeconds = (s.timeSeconds >= 0.0 && earliest >= 0.0) ? s.timeSeconds - earliest : 0.0;
        // Clip starts are in ticks; round up and skip the few frames in between so the
        // audio stays sample-accurate against the other stems.
        const double exactTick = originTick + offsetSeconds * ticksPerSecond;
        const int startTick = (int) std::ceil (exactTick - 1.0e-9);
        const double skipSeconds = (startTick - exactTick) / ticksPerSecond;
        const juce::ScopedLock sl (lock);
        auto& tr = sess.audioTracks[(size_t) t];
        tr.name = s.name;
        tr.strip.name = s.name;
        cr::AudioClip c;
        c.sample = s.sample;
        c.start = startTick;
        c.offset = (juce::int64) std::llround (skipSeconds * s.sample->sampleRate);
        c.length = juce::jmax ((juce::int64) 1, s.sample->numFrames - c.offset);
        tr.clips.push_back (c);
        ++added;
    }
    sendChangeMessage();
    return added;
}
