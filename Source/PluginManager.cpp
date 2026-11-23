#include "PluginManager.h"
#include "FileUtils.h"

static void addFormats (juce::AudioPluginFormatManager& fm);

PluginManager::PluginManager() : juce::Thread ("Plugin scanner")
{
    addFormats (formatManager);

    if (auto fx = juce::parseXML (getDataFile ("plugins_failed.xml")))
        for (auto* e : fx->getChildIterator())
            failed.push_back ({ e->getStringAttribute ("format"), e->getStringAttribute ("id"),
                                e->getStringAttribute ("name"), e->getStringAttribute ("reason") });

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
    auto dir = appFolderIn (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory));
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

// ============================================================================
// Out-of-process scanning
// ============================================================================
static const char* scanFlag = "--crate-scan-plugin";
static int scanTimeoutMs()
{
    const int ms = juce::SystemStats::getEnvironmentVariable ("CRATE_SCAN_TIMEOUT_MS", {}).getIntValue();
    return ms > 0 ? ms : 90 * 1000;
}

static void addFormats (juce::AudioPluginFormatManager& fm)
{
   #if JUCE_PLUGINHOST_VST3
    fm.addFormat (std::make_unique<juce::VST3PluginFormat>());
   #else
    juce::ignoreUnused (fm);
   #endif
   #if JUCE_PLUGINHOST_AU && JUCE_MAC
    fm.addFormat (std::make_unique<juce::AudioUnitPluginFormat>());
   #endif
}

bool PluginManager::isScannerCommandLine (const juce::StringArray& args)
{
    return args.contains (scanFlag);
}

// Runs in the helper process: load one plugin file, write what it contains to an XML
// file, exit. If the plugin crashes, only this process dies.
int PluginManager::runScannerProcess (const juce::StringArray& args)
{
    const int i = args.indexOf (scanFlag);
    if (i < 0 || i + 3 >= args.size()) return 3;
    const auto formatName = args[i + 1].unquoted();
    const auto fileOrId   = args[i + 2].unquoted();
    const juce::File outFile (args[i + 3].unquoted());

    juce::AudioPluginFormatManager fm;
    addFormats (fm);
    juce::AudioPluginFormat* format = nullptr;
    for (auto* f : fm.getFormats())
        if (f->getName() == formatName) format = f;
    if (format == nullptr) return 3;

    juce::OwnedArray<juce::PluginDescription> found;
    format->findAllTypesForFile (found, fileOrId);

    juce::XmlElement xml ("PLUGINS");
    for (auto* d : found)
        xml.addChildElement (d->createXml().release());
    // Write to a side file and rename, so the main app never reads a half-written result.
    const auto tmp = outFile.getSiblingFile (outFile.getFileName() + ".part");
    if (! xml.writeTo (tmp) || ! tmp.moveFileTo (outFile)) return 3;
    return found.isEmpty() ? 2 : 0;
}

class PluginManager::OutOfProcessScanner : public juce::KnownPluginList::CustomScanner
{
public:
    explicit OutOfProcessScanner (PluginManager& m) : owner (m) {}

    bool findPluginTypesFor (juce::AudioPluginFormat& format, juce::OwnedArray<juce::PluginDescription>& result,
                             const juce::String& fileOrId) override
    {
        const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
        const auto outFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                 .getNonexistentChildFile ("crate_scan", ".xml", false);

        juce::ChildProcess child;
        if (! child.start (juce::StringArray { exe.getFullPathName(), scanFlag, format.getName(), fileOrId, outFile.getFullPathName() }, 0))
        {
            // Couldn't start the helper (very unusual): fall back to scanning in this process.
            format.findAllTypesForFile (result, fileOrId);
            return true;
        }

        int waited = 0, sinceResult = -1;
        while (! child.waitForProcessToFinish (100))
        {
            waited += 100;
            // Some plugins hang or crash while being unloaded. Once the result is written,
            // give the helper a few seconds to exit, then stop it and keep the result.
            if (sinceResult < 0 && outFile.existsAsFile()) sinceResult = 0;
            if (sinceResult >= 0 && (sinceResult += 100) > 5000) { child.kill(); break; }
            if (shouldExit())         { child.kill(); outFile.deleteFile(); return true; }   // user stopped the scan: don't mark it failed
            if (waited > scanTimeoutMs())
            {
                child.kill();
                outFile.deleteFile();
                owner.addFailed (format.getName(), fileOrId, "Stopped responding while loading");
                return false;
            }
        }

        const auto code = child.getExitCode();
        auto xml = juce::parseXML (outFile);
        outFile.deleteFile();
        outFile.getSiblingFile (outFile.getFileName() + ".part").deleteFile();

        if (xml == nullptr || ! xml->hasTagName ("PLUGINS"))
        {
            owner.addFailed (format.getName(), fileOrId, code != 0 ? "Crashed while loading (exit code " + juce::String ((int) code) + ")"
                                                                  : juce::String ("Crashed while loading"));
            return false;
        }

        for (auto* e : xml->getChildIterator())
        {
            auto d = std::make_unique<juce::PluginDescription>();
            if (d->loadFromXml (*e)) result.add (d.release());
        }

        if (result.isEmpty())
        {
            owner.addFailed (format.getName(), fileOrId, "Loaded, but no usable plugin was found in it");
            return false;
        }
        return true;
    }

private:
    PluginManager& owner;
};

void PluginManager::addFailed (const juce::String& format, const juce::String& id, const juce::String& reason)
{
    const juce::ScopedLock sl (failedLock);
    for (auto& f : failed)
        if (f.fileOrId == id && f.format == format) { f.reason = reason; return; }
    juce::String name = id;
    for (auto* fmt : formatManager.getFormats())
        if (fmt->getName() == format) name = fmt->getNameOfPluginFromIdentifier (id);
    if (name.isEmpty() || name.containsChar ('/') || name.containsChar ('\\'))
        name = juce::File::isAbsolutePath (id) ? juce::File (id).getFileNameWithoutExtension() : id;
    failed.push_back ({ format, id, name, reason });
}

std::vector<PluginManager::FailedPlugin> PluginManager::getFailedPlugins() const
{
    const juce::ScopedLock sl (failedLock);
    return failed;
}

void PluginManager::clearFailedPlugins()
{
    if (isScanning()) return;
    {
        const juce::ScopedLock sl (failedLock);
        failed.clear();
    }
    knownPlugins.clearBlacklistedFiles();
    getDataFile ("scan_deadmanspedal.txt").deleteFile();
    saveFailed();
    saveList();
}

void PluginManager::saveFailed()
{
    juce::XmlElement xml ("FAILED");
    {
        const juce::ScopedLock sl (failedLock);
        for (auto& f : failed)
        {
            auto* e = xml.createNewChildElement ("PLUGIN");
            e->setAttribute ("format", f.format);
            e->setAttribute ("id", f.fileOrId);
            e->setAttribute ("name", f.name);
            e->setAttribute ("reason", f.reason);
        }
    }
    xml.writeTo (getDataFile ("plugins_failed.xml"));
}

void PluginManager::startScan (std::function<void (juce::String, bool)> progress)
{
    if (isThreadRunning()) return;
    progressCallback = std::move (progress);
    scanProgress = 0.0f;
    startThread();
}

void PluginManager::run()
{
    auto report = [this] (juce::String s, bool done)
    {
        juce::MessageManager::callAsync ([cb = progressCallback, s, done] { if (cb) cb (s, done); });
    };

    knownPlugins.setCustomScanner (std::make_unique<OutOfProcessScanner> (*this));
    const auto pedal = getDataFile ("scan_deadmanspedal.txt");

    // Anything left in the dead man's pedal crashed the whole app during an older scan.
    {
        juce::StringArray crashed;
        crashed.addLines (pedal.loadFileAsString());
        crashed.removeEmptyStrings();
        for (auto& id : crashed)
            for (auto* format : formatManager.getFormats())
                if (format->fileMightContainThisPluginType (id))
                    { addFailed (format->getName(), id, "Crashed an earlier scan"); break; }
    }

    const auto formats = formatManager.getFormats();
    for (int fi = 0; fi < formats.size() && ! threadShouldExit(); ++fi)
    {
        auto* format = formats[fi];
        juce::PluginDirectoryScanner scanner (knownPlugins, *format, getSearchPath(), true, pedal, false);
        juce::String name;
        while (! threadShouldExit())
        {
            const auto next = scanner.getNextPluginFileThatWillBeScanned();
            const float prog = ((float) fi + juce::jlimit (0.0f, 1.0f, scanner.getProgress())) / (float) juce::jmax (1, formats.size());
            scanProgress = prog;
            report ("Scanning " + format->getName() + " (" + juce::String (juce::roundToInt (prog * 100.0f)) + "%): " + next, false);
            const int failedBefore = (int) getFailedPlugins().size();
            if (! scanner.scanNextFile (true, name))
                break;
            if ((int) getFailedPlugins().size() > failedBefore)
                saveFailed();
            saveList();   // keep what we have, even if something goes wrong later
        }
        if (! threadShouldExit())
            for (auto& f : scanner.getFailedFiles())
                addFailed (format->getName(), f, "Couldn't be loaded");
    }

    knownPlugins.setCustomScanner (nullptr);
    saveList();
    saveFailed();
    scanProgress = 1.0f;
    const int numFailed = (int) getFailedPlugins().size();
    juce::String msg = threadShouldExit() ? "Scan stopped. " : "Scan finished. ";
    msg << "Found " << knownPlugins.getNumTypes() << " plugins";
    if (numFailed > 0) msg << ", " << numFailed << " failed and skipped";
    report (msg + ".", true);
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
