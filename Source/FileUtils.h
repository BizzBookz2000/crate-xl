#pragma once
#include <JuceHeader.h>

// Creates a WAV writer for a file (overwrites it). Returns nullptr on failure.
inline std::unique_ptr<juce::AudioFormatWriter> createWavWriter (const juce::File& file, double sampleRate,
                                                                 int numChannels, int bitsPerSample)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    if (stream == nullptr) return nullptr;
    juce::WavAudioFormat wav;
    return wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                            .withSampleRate (sampleRate)
                                            .withNumChannels (numChannels)
                                            .withBitsPerSample (bitsPerSample));
}

// The app's own folder inside 'parent' (Documents, or the per-user app data folder).
// Folders made by earlier versions (called "Crate") are renamed to the new name once.
inline juce::File appFolderIn (const juce::File& parent)
{
    auto folder = parent.getChildFile ("Crate XL");
    auto old = parent.getChildFile ("Crate");
    if (! folder.exists() && old.isDirectory())
        old.moveFileTo (folder);
    return folder;
}
