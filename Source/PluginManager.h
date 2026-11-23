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

    // Scans the default plugin folders plus any extra folders on a background thread.
    // Each plugin is loaded in a separate helper process (this app started with
    // --crate-scan-plugin), so a plugin that crashes or hangs is marked as failed and
    // the scan carries on with the next one.
    void startScan (std::function<void (juce::String status, bool finished)> progress);
    void stopScan() { signalThreadShouldExit(); }
    bool isScanning() const { return isThreadRunning(); }
    float getScanProgress() const { return scanProgress.load(); }

    struct FailedPlugin { juce::String format, fileOrId, name, reason; };
    std::vector<FailedPlugin> getFailedPlugins() const;
    // Clears the failed list and the blacklist so the next scan tries those plugins again.
    void clearFailedPlugins();

    // Entry point for the helper process. Returns true (and sets exitCode) when the
    // command line asks for a single-plugin scan.
    static bool isScannerCommandLine (const juce::StringArray& args);
    static int runScannerProcess (const juce::StringArray& args);

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
    void saveFailed();
    juce::File getDataFile (const juce::String& name) const;
    void addFailed (const juce::String& format, const juce::String& id, const juce::String& reason);

    class OutOfProcessScanner;
    friend class OutOfProcessScanner;

    mutable juce::CriticalSection failedLock;
    std::vector<FailedPlugin> failed;
    std::atomic<float> scanProgress { 0.0f };

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
