#pragma once
#include <JuceHeader.h>

// VST3 scanning, the known-plugin list and instance creation.
class PluginManager : private juce::Thread
{
public:
    PluginManager();
    ~PluginManager() override;

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;

    // Scans the default VST3 folders plus any extra folders on a background thread.
    void startScan (std::function<void (juce::String status, bool finished)> progress);
    bool isScanning() const { return isThreadRunning(); }

    juce::Array<juce::PluginDescription> getInstruments() const;
    juce::Array<juce::PluginDescription> getEffects() const;

    std::unique_ptr<juce::AudioPluginInstance> create (const juce::PluginDescription& desc,
                                                       double sampleRate, int blockSize,
                                                       juce::String& error);

    juce::FileSearchPath getSearchPath() const;
    void addSearchFolder (const juce::File& folder);

private:
    void run() override;
    void saveList();
    juce::File getDataFile (const juce::String& name) const;

    std::function<void (juce::String, bool)> progressCallback;
    juce::StringArray extraFolders;
};

// A floating window that shows a hosted plugin's own editor.
class PluginWindow : public juce::DocumentWindow
{
public:
    static void show (juce::AudioProcessor& processor);
    static void closeFor (juce::AudioProcessor* processor);
    static void closeAll();

    void closeButtonPressed() override;

private:
    explicit PluginWindow (juce::AudioProcessor& p);
    juce::AudioProcessor& processor;
    static juce::OwnedArray<PluginWindow>& windows();
};
