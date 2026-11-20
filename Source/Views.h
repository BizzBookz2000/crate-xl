#pragma once
#include "UiCommon.h"

// Main views (Performance -> Editor -> Sequencer -> Mixer)
std::unique_ptr<ViewBase> createPerformanceView();
std::unique_ptr<ViewBase> createEditorView();
std::unique_ptr<ViewBase> createSequencerView();
std::unique_ptr<ViewBase> createMixerView();

// Side panels (Panels.cpp)
std::unique_ptr<ViewBase> createBrowserPanel();
std::unique_ptr<ViewBase> createFxPanel();
std::unique_ptr<ViewBase> createMacrosPanel();
std::unique_ptr<ViewBase> createKeyboardPanel();

// Computer keyboard pad playing (PerformanceView.cpp): rows 1-4 / Q-R / A-F / Z-V
bool handlePadKeyDown (const juce::KeyPress& key);
void handlePadKeyStates();

// First-launch content (StarterKit.cpp)
void loadStarterKit();

// Settings (Settings.cpp)
void showSettingsDialog (int initialTab = 0);
void loadAppSettings();
void saveAppSettings();
void showBankMidiSetup (int bank, juce::Component* target);
void showPadMidiSetup (int bank, int pad, juce::Component* target);

// Dialogs (Dialogs.cpp)
void showRecorderDialog (int bank, int pad);
void showExportDialog();
void showChordsAndScales (juce::Component* target);
void showQuantizeMenu (juce::Component* target);

// Sample tools (SampleEditor.cpp)
std::unique_ptr<juce::Component> createAudioEditor (int bank, int pad, int layer, int zone);
void showAutoSliceMenu (juce::Component* target, int bank, int pad, int layer, int zone, std::function<void()> done);
void showSliceSaveDialog (int bank, int pad, int layer, int zone);

// Navigation helpers implemented by the main window
namespace Nav
{
    enum View { performance = 0, editor, sequencer, mixer };
    enum SequencerMode { song = 0, scenes, pattern };
    enum Panel { browserPanel = 0, fxPanel, macrosPanel, keyboardPanel };
    void showView (int view);
    int currentView();
    void showSequencer (int mode);
    int takeSequencerRequest();                                        // -1 if none
    void showAutomation (bool songLevel, int bank, const juce::String& param);
    bool takeAutomationRequest (bool& songLevel, int& bank, juce::String& param);
    void setStatus (const juce::String& text);
    void togglePanel (int panel);
    bool isPanelOpen (int panel);
    void showOverlay (std::unique_ptr<juce::Component> overlay);       // full-window overlay (audio editor)
    void closeOverlay();
    void openSession();
    void saveSession (bool saveAs);
}

// Drag & drop of files inside the app: description is "file:" + full path
inline bool isFileDrag (const juce::DragAndDropTarget::SourceDetails& d) { return d.description.toString().startsWith ("file:"); }
inline juce::File fileFromDrag (const juce::DragAndDropTarget::SourceDetails& d) { return juce::File (d.description.toString().substring (5)); }
