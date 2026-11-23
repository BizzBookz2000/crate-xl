#include <JuceHeader.h>
#include "Engine.h"
#include "MainComponent.h"

class CrateApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override
    {
        // The plugin-scan helper is this same app started again with a special argument.
        return PluginManager::isScannerCommandLine (getCommandLineParameterArray());
    }

    void initialise (const juce::String& commandLine) override
    {
        const auto args = getCommandLineParameterArray();
        if (PluginManager::isScannerCommandLine (args))
        {
            // Helper mode: scan one plugin on a background thread (so plugins that need the
            // message thread can still load), then quit. No window, no audio device.
           #if JUCE_MAC
            juce::Process::setDockIconVisible (false);
           #endif
            scanHelper = std::make_unique<ScanHelper> (args);
            return;
        }

        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        engine = std::make_unique<Engine>();
        mainWindow = std::make_unique<MainWindow> (getApplicationName());
        openFromCommandLine (commandLine);
    }

    void shutdown() override
    {
        if (scanHelper != nullptr) { scanHelper = nullptr; return; }
        PluginWindow::closeAll();
        mainWindow = nullptr;
        engine = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override
    {
        if (scanHelper != nullptr) { quit(); return; }
        if (auto* mc = mainWindow != nullptr ? dynamic_cast<MainComponent*> (mainWindow->getContentComponent()) : nullptr)
            mc->prepareToQuit();
        quit();
    }

    void anotherInstanceStarted (const juce::String& commandLine) override { if (scanHelper == nullptr) openFromCommandLine (commandLine); }

    struct ScanHelper : public juce::Thread
    {
        explicit ScanHelper (juce::StringArray a) : juce::Thread ("Plugin scan helper"), args (std::move (a)) { startThread(); }
        ~ScanHelper() override { stopThread (2000); }
        void run() override
        {
            const int code = PluginManager::runScannerProcess (args);
            juce::MessageManager::callAsync ([code]
            {
                juce::JUCEApplicationBase::getInstance()->setApplicationReturnValue (code);
                juce::JUCEApplicationBase::quit();
            });
        }
        juce::StringArray args;
    };

    void openFromCommandLine (const juce::String& commandLine)
    {
        const auto arg = commandLine.unquoted().trim();
        if (arg.endsWithIgnoreCase (".crsession") && juce::File (arg).existsAsFile() && mainWindow != nullptr)
            if (auto* mc = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
                mc->openSessionFile (juce::File (arg));
    }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name, Pal::bg, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            setResizable (true, true);
            setResizeLimits (1100, 700, 10000, 10000);
            centreWithSize (juce::jmin (1440, getParentWidth() - 40), juce::jmin (900, getParentHeight() - 60));
            setVisible (true);
        }
        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

private:
    CrateLookAndFeel lookAndFeel;
    std::unique_ptr<Engine> engine;
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<ScanHelper> scanHelper;
};

START_JUCE_APPLICATION (CrateApplication)
