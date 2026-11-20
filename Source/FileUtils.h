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
