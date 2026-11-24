#pragma once
#include <JuceHeader.h>
#include "Views.h"

class TrackHelper;
class TransportDisplay;

// Main window content: transport bar on top, navigation rail on the left, the active
// view in the centre, dockable panels (Browser left, FX / Macros right, Keyboard bottom).
class MainComponent : public juce::Component,
                      public juce::DragAndDropContainer,
                      public juce::FileDragAndDropTarget,
                      private juce::Timer,
                      private juce::ChangeListener
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics& g) override;
    void paintOverChildren (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;
    bool keyStateChanged (bool isKeyDown) override { handlePadKeyStates(); juce::ignoreUnused (isKeyDown); return false; }

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    void showView (int index);
    int getView() const { return currentView; }
    bool openSessionFile (const juce::File& f);
    void openSession();
    void saveSession (bool saveAs);
    void setStatus (const juce::String& s);
    void togglePanel (int which);
    bool isPanelOpen (int which) const { return panelOpen[(size_t) which]; }
    void showOverlay (std::unique_ptr<juce::Component> c);
    void closeOverlay();
    void prepareToQuit();

    int sequencerRequest = -1;
    bool automationRequest = false, automationSong = true;
    int automationBank = 0;
    juce::String automationParam;

    static MainComponent* instance;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void showFileMenu();
    void newSession();
    void autosave();
    void updateTransport();
    void loadBankPresetDialog (bool save);

    // transport bar
    IconButton menuBtn { Icon::menu, "Menu" };
    std::unique_ptr<TrackHelper> trackHelper;
    std::unique_ptr<TransportDisplay> display;
    IconButton stopBtn { Icon::stopSquare, "Stop (rewinds on second press)" }, playBtn { Icon::play, "Play / pause (Space)", true },
               recBtn { Icon::record, "Record (R)", true }, rewBtn { Icon::rewind, "Previous bar" }, startBtn { Icon::toStart, "Back to the start (Home)" }, ffBtn { Icon::forward, "Next bar" },
               loopBtn { Icon::loop, "Loop (L)", true };
    juce::TextButton undoBtn { "Undo" }, redoBtn { "Redo" }, midiMapBtn { "MIDI" };
    std::unique_ptr<juce::Component> midiMap;
    void setMidiMapMode (bool on);
    juce::Label cpuLabel, statusLabel;

    // navigation rail
    juce::OwnedArray<IconButton> navButtons, panelButtons;
    IconButton settingsBtn { Icon::settings, "Settings" };

    std::array<std::unique_ptr<ViewBase>, 4> views;
    std::array<std::unique_ptr<ViewBase>, 4> panels;   // browser, fx, macros, keyboard
    std::array<bool, 4> panelOpen { false, false, false, false };
    std::unique_ptr<juce::Component> overlay;
    int currentView = 0;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::uint32 lastAutosave = 0;
    int statusTicks = 0, tick = 0;
    juce::Rectangle<int> transportArea, railArea;
    juce::TooltipWindow tooltips { this, 700 };
    juce::OpenGLContext gl;
};
