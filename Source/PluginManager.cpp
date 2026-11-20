#include "PluginManager.h"

PluginManager::PluginManager() : juce::Thread ("Plugin scanner")
{
    formatManager.addFormat (std::make_unique<juce::VST3PluginFormat>());
   #if JUCE_PLUGINHOST_AU && JUCE_MAC
    formatManager.addFormat (std::make_unique<juce::AudioUnitPluginFormat>());
   #endif

    if (auto xml = juce::parseXML (getDataFile ("plugins.xml")))
        knownPlugins.recreateFromXml (*xml);

    auto folders = getDataFile ("plugin_folders.txt");
    if (folders.existsAsFile())
        extraFolders.addLines (folders.loadFileAsString());
    extraFolders.removeEmptyStrings();
}

PluginManager::~PluginManager()
{
    stopThread (10000);
}

juce::File PluginManager::getDataFile (const juce::String& name) const
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Crate");
    dir.createDirectory();
    return dir.getChildFile (name);
}

juce::FileSearchPath PluginManager::getSearchPath() const
{
    juce::FileSearchPath path;
    for (auto* f : formatManager.getFormats())
        path.addPath (f->getDefaultLocationsToSearch());
    for (auto& folder : extraFolders)
        path.add (juce::File (folder));
    path.removeRedundantPaths();
    return path;
}

void PluginManager::addSearchFolder (const juce::File& folder)
{
    extraFolders.addIfNotAlreadyThere (folder.getFullPathName());
    getDataFile ("plugin_folders.txt").replaceWithText (extraFolders.joinIntoString ("\n"));
}

void PluginManager::startScan (std::function<void (juce::String, bool)> progress)
{
    if (isThreadRunning()) return;
    progressCallback = std::move (progress);
    startThread();
}

void PluginManager::run()
{
    auto report = [this] (juce::String s, bool done)
    {
        juce::MessageManager::callAsync ([cb = progressCallback, s, done] { if (cb) cb (s, done); });
    };

    for (auto* format : formatManager.getFormats())
    {
        // A "dead man's pedal" file lets JUCE skip a plugin that crashed a previous scan.
        juce::PluginDirectoryScanner scanner (knownPlugins, *format, getSearchPath(), true,
                                              getDataFile ("scan_deadmanspedal.txt"), false);
        juce::String name;
        while (! threadShouldExit())
        {
            report ("Scanning: " + scanner.getNextPluginFileThatWillBeScanned(), false);
            if (! scanner.scanNextFile (true, name))
                break;
        }
    }

    saveList();
    report ("Found " + juce::String (knownPlugins.getNumTypes()) + " plugins", true);
}

void PluginManager::saveList()
{
    if (auto xml = knownPlugins.createXml())
        xml->writeTo (getDataFile ("plugins.xml"));
}

juce::Array<juce::PluginDescription> PluginManager::getInstruments() const
{
    juce::Array<juce::PluginDescription> out;
    for (auto& d : knownPlugins.getTypes())
        if (d.isInstrument) out.add (d);
    return out;
}

juce::Array<juce::PluginDescription> PluginManager::getEffects() const
{
    juce::Array<juce::PluginDescription> out;
    for (auto& d : knownPlugins.getTypes())
        if (! d.isInstrument) out.add (d);
    return out;
}

std::unique_ptr<juce::AudioPluginInstance> PluginManager::create (const juce::PluginDescription& desc,
                                                                  double sampleRate, int blockSize,
                                                                  juce::String& error)
{
    auto instance = formatManager.createPluginInstance (desc, sampleRate, blockSize, error);
    if (instance != nullptr)
    {
        // Prefer a plain stereo layout when the plugin supports it.
        auto layout = instance->getBusesLayout();
        if (layout.outputBuses.size() > 0)
        {
            juce::AudioProcessor::BusesLayout stereo = layout;
            for (auto& b : stereo.outputBuses) b = juce::AudioChannelSet::stereo();
            for (auto& b : stereo.inputBuses)  b = juce::AudioChannelSet::stereo();
            if (instance->checkBusesLayoutSupported (stereo))
                instance->setBusesLayout (stereo);
        }
        instance->prepareToPlay (sampleRate, blockSize);
    }
    return instance;
}

// ============================================================================
juce::OwnedArray<PluginWindow>& PluginWindow::windows()
{
    static juce::OwnedArray<PluginWindow> list;
    return list;
}

PluginWindow::PluginWindow (juce::AudioProcessor& p)
    : juce::DocumentWindow (p.getName(), juce::Colour (0xff1e2128), juce::DocumentWindow::closeButton),
      processor (p)
{
    setUsingNativeTitleBar (true);
    juce::AudioProcessorEditor* editor = p.hasEditor() ? p.createEditorAndMakeActive() : nullptr;
    if (editor == nullptr)
        editor = new juce::GenericAudioProcessorEditor (p);
    setContentOwned (editor, true);
    setResizable (editor->isResizable(), false);
    setAlwaysOnTop (true);
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
}

void PluginWindow::show (juce::AudioProcessor& processor)
{
    for (auto* w : windows())
        if (&w->processor == &processor) { w->toFront (true); return; }
    windows().add (new PluginWindow (processor));
}

void PluginWindow::closeFor (juce::AudioProcessor* processor)
{
    for (int i = windows().size(); --i >= 0;)
        if (&windows()[i]->processor == processor)
            windows().remove (i);
}

void PluginWindow::closeAll() { windows().clear(); }

void PluginWindow::closeButtonPressed()
{
    closeFor (&processor);
}
