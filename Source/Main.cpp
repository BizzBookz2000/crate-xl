#include <JuceHeader.h>
#include "Engine.h"
#include "MainComponent.h"

class CrateApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String& commandLine) override
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        engine = std::make_unique<Engine>();
        mainWindow = std::make_unique<MainWindow> (getApplicationName());
        openFromCommandLine (commandLine);
    }

    void shutdown() override
    {
        PluginWindow::closeAll();
        mainWindow = nullptr;
        engine = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override
    {
        if (auto* mc = mainWindow != nullptr ? dynamic_cast<MainComponent*> (mainWindow->getContentComponent()) : nullptr)
            mc->prepareToQuit();
        quit();
    }

    void anotherInstanceStarted (const juce::String& commandLine) override { openFromCommandLine (commandLine); }

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
};

START_JUCE_APPLICATION (CrateApplication)
