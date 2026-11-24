#pragma once
#include <JuceHeader.h>
#include "Engine.h"
#include "LookAndFeel.h"

// ============================================================================
// Icons (vector, drawn in code)
// ============================================================================
enum class Icon
{
    none, play, stop, record, loop, metronome, undo, redo, rewind, forward, menu, dots, settings,
    pads, editor, sequencer, mixer, browser, fx, macro, keyboard, plus, minus, trash, copy, paste, cut,
    close, power, up, down, left, right, mic, wave, scissors, link, lock, hand, select, pencil, brush,
    split, magnet, headphones, grid, tap, zoomIn, zoomOut, save, folder, check, eye, star, chord, automation,
    io, sliders, stopSquare, toStart
};

void drawIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> r, juce::Colour c);

class IconButton : public juce::Button
{
public:
    IconButton (Icon i, const juce::String& tooltip = {}, bool toggle = false);
    void setIcon (Icon i) { icon = i; repaint(); }
    void setLabel (const juce::String& l) { label = l; repaint(); }
    void setOnColour (juce::Colour c) { onColour = c; repaint(); }
    void setFlat (bool f) { flat = f; repaint(); }
    void paintButton (juce::Graphics& g, bool over, bool down) override;
private:
    Icon icon;
    juce::String label;
    juce::Colour onColour = Pal::accent;
    bool flat = true;
};

// Uppercase tab strip with an underline on the selected tab.
class TabStrip : public juce::Component
{
public:
    TabStrip (juce::StringArray names = {}) : tabs (std::move (names)) {}
    void setTabs (const juce::StringArray& t) { tabs = t; repaint(); }
    void setSelected (int i, bool notify = false) { if (i != selected) { selected = i; repaint(); if (notify && onChange) onChange (i); } }
    int getSelected() const { return selected; }
    void setAccent (juce::Colour c) { accent = c; repaint(); }
    std::function<void (int)> onChange;
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
private:
    juce::StringArray tabs;
    int selected = 0;
    juce::Colour accent = Pal::accent;
};

// ============================================================================
// Knob: label on top, arc, value text below. Bound to an engine parameter path
// (MIDI learn / macro / automation menu on right-click) or to custom lambdas.
// ============================================================================
class Knob : public juce::Component, private juce::Slider::Listener
{
public:
    Knob (const juce::String& label = {});
    ~Knob() override;

    // engine parameter binding
    void setParam (const juce::String& path, const juce::String& labelOverride = {});
    // custom binding
    void setCustom (float min, float max, float def, std::function<float()> get, std::function<void (float)> set,
                    std::function<juce::String (float)> format = {}, bool logScale = false);
    void setLabel (const juce::String& l) { label = l; repaint(); }
    void setAccent (juce::Colour c);
    void setCompact (bool c) { compact = c; resized(); }
    void refreshValue();
    const juce::String& getParamPath() const { return path; }
    std::function<void()> onUserChange;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;

    static void refreshAll();   // all knobs on screen re-read their values (timer driven)

private:
    void sliderValueChanged (juce::Slider*) override;
    void sliderDragStarted (juce::Slider*) override;
    void showMenu();
    juce::String valueText() const;

    struct KnobSlider : juce::Slider
    {
        Knob& owner;
        explicit KnobSlider (Knob& k) : owner (k) {}
        void mouseDown (const juce::MouseEvent& e) override { if (e.mods.isPopupMenu()) owner.showMenu(); else juce::Slider::mouseDown (e); }
    } slider { *this };
    juce::String label, path;
    cr::ParamAddress address;
    cr::ParamInfo info;
    std::function<float()> getter;
    std::function<void (float)> setter;
    std::function<juce::String (float)> formatter;
    bool compact = false, updating = false, isCustom = false, checkpointed = false;
};

// "< value >" stepper with a caption (choke group, pad note, polyphony...)
class Stepper : public juce::Component
{
public:
    Stepper (const juce::String& caption) : label (caption) {}
    std::function<juce::String()> getText;
    std::function<void (int delta)> onStep;
    std::function<void()> onClickValue;   // optional: click on the value
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override { if (onStep) { onStep (w.deltaY > 0 ? 1 : -1); repaint(); } }
private:
    juce::String label;
};

// Caption + value; click pops up a menu of choices (layer trigger, loop mode...)
class ChoiceField : public juce::Component
{
public:
    ChoiceField (const juce::String& caption) : label (caption) {}
    juce::StringArray choices;
    std::function<int()> getIndex;
    std::function<void (int)> setIndex;
    std::function<juce::String()> getText;   // optional override of the displayed value
    std::function<void()> onClick;            // optional: replaces the menu
    juce::Colour accent = Pal::accent;
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent&) override;
private:
    juce::String label;
};

// A labelled power button (envelope on/off, effect bypass...)
class PowerButton : public juce::Button
{
public:
    PowerButton() : juce::Button ("power") { setClickingTogglesState (true); }
    juce::Colour accent = Pal::accent;
    void paintButton (juce::Graphics& g, bool over, bool) override;
};

// ============================================================================
// Views
// ============================================================================
class ViewBase : public juce::Component
{
public:
    ViewBase();
    ~ViewBase() override;
    virtual void refresh() {}
    static void broadcast();   // refresh every live view (after a model change)
};

// ============================================================================
// Effects
// ============================================================================
// Card that edits one insert slot: header (power, name, menu) + parameter knobs.
class EffectCard : public juce::Component
{
public:
    EffectCard();
    void setTarget (cr::InsertSlot* slot, const juce::String& baseAddress, int index, juce::Colour accent);
    int getPreferredHeight (int width) const;
    std::function<void (int action)> onAction;   // 1 replace, 2 remove, 3 up, 4 down, 5 open plugin window
    // Dragging the card by its title bar: phase 0 start, 1 move, 2 drop
    std::function<void (int phase, const juce::MouseEvent&)> onReorderDrag;
    void paint (juce::Graphics& g) override;
    void resized() override;
    void refreshValues();
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
private:
    cr::InsertSlot* slot = nullptr;
    int index = 0;
    juce::String base;
    juce::Colour accent = Pal::accent;
    PowerButton power;
    IconButton menuBtn { Icon::dots, "Effect options" };
    juce::TextButton openBtn { "Open plugin" };
    juce::OwnedArray<Knob> knobs;
    juce::OwnedArray<ChoiceField> choices;
    juce::Rectangle<int> graphArea;     // frequency response, for filters and EQs
    bool reordering = false;
    bool hasGraph() const;
    void paintResponse (juce::Graphics& g, juce::Rectangle<float> r);
};

// Unlimited (or fixed-size) insert chain: list of effect cards + "Add effect".
class InsertChainView : public juce::Component
{
public:
    InsertChainView();
    // chain: strip inserts; base: e.g. "bank/3/" (slot appended as fx<n>/)
    void setChain (std::vector<cr::InsertSlot>* chain, const juce::String& base, juce::Colour accent);
    // layer FX: fixed 4 slots
    void setLayerFx (int bank, int pad, int layer, juce::Colour accent);
    void clear();
    void refresh();
    void refreshValues();
    std::function<void()> onChanged;
    void paint (juce::Graphics& g) override;
    void resized() override;
private:
    void addEffect (int slot);
    void handleAction (int slot, int action);
    void reorderDrag (int cardIndex, int phase, const juce::MouseEvent& e);
    std::vector<int> cardSlots;          // slot index of each card
    int dragCard = -1, dropPos = -1, dragGrabY = 0;
    struct DropLine : juce::Component { void paint (juce::Graphics& g) override { g.fillAll (Pal::accent); } } dropLine;
    std::vector<cr::InsertSlot>* chain = nullptr;
    int lfxBank = -1, lfxPad = -1, lfxLayer = -1;
    juce::String base;
    juce::Colour accent = Pal::accent;
    juce::Viewport viewport;
    juce::Component content;
    juce::OwnedArray<EffectCard> cards;
    juce::TextButton addBtn { "+ Add effect" };
    juce::Label emptyLabel;
};

// Menu of built-in effects + scanned plugins. Calls back with builtin type or plugin index.
void showEffectMenu (juce::Component* target, std::function<void (const juce::String& builtin, int pluginIndex)> chosen, bool instruments = false);

// ============================================================================
// Helpers
// ============================================================================
juce::Rectangle<int> drawSection (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title = {});   // returns content area
void drawBankBadge (juce::Graphics& g, juce::Rectangle<float> r, int bank, bool bright = true);
void drawCaps (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r, juce::Justification j = juce::Justification::centredLeft,
               juce::Colour c = Pal::textDim, float size = 10.5f);
juce::String noteName (int note);   // 60 = C3
juce::Colour padColour (int bank, int pad);
cr::Bank& currentBank();
cr::Pad& currentPad();
cr::Layer* currentLayer();
juce::String layerPath (int bank, int pad, int layer, int field);
juce::String stripPath (int stripKey);   // "bank/3/", "pad/3/5/", "aux/0/", "audio/2/", "master/"
bool isAudioFile (const juce::File& f);
// Drops / imports audio as new tracks, one per file, in the background (stems keep their
// Broadcast WAV positions relative to each other; otherwise they all start at originTick).
void importStems (const juce::StringArray& files, int originTick);
// File dialogs: the OS dialog on Windows / macOS. On Android the built-in browser is used,
// because Android's document picker hands back content URIs instead of files.
inline std::unique_ptr<juce::FileChooser> makeFileChooser (const juce::String& title, const juce::File& initial = {}, const juce::String& patterns = {})
{
   #if JUCE_ANDROID
    return std::make_unique<juce::FileChooser> (title, initial, patterns, false);
   #else
    return std::make_unique<juce::FileChooser> (title, initial, patterns);
   #endif
}

// MIDI map mode: tag a control with the FocusActions action it performs, so it can be
// mapped by clicking it while MIDI map mode is on (knobs are mappable by their parameter).
void setMidiAction (juce::Component& c, int action);
int getMidiAction (const juce::Component& c);   // -1 when untagged

// Bank chooser with new / duplicate / copy / paste / rename / clear / delete.
void showBanksMenu (juce::Component* target);

// Resample submenu for a channel (bounce / bounce with sends / freeze / unfreeze); ids 901-905.
void addResampleItems (juce::PopupMenu& m, int stripKey);
bool handleResampleItem (int menuResult, int stripKey);
// Live resampling sources for input menus: audio tracks (ids 1000+) and pads of the selected bank (ids 20000+).
void addLiveSourceItems (juce::PopupMenu& internal, int currentSource, int excludeTrack);
int liveSourceForMenuResult (int menuResult);   // -1 when it isn't one of those
juce::String liveSourceName (int source);
void showError (const juce::String& title, const juce::String& message);
void askForText (const juce::String& title, const juce::String& initial, std::function<void (const juce::String&)> done);
void askForNumber (const juce::String& title, double initial, std::function<void (double)> done);
void showPadColourMenu (int bank, int pad, juce::Component* target);
juce::StringArray auxNames();

// Two macros on an X/Y field (Performance keys mode, Macros panel)
class XYPad : public juce::Component
{
public:
    int macroX = 0, macroY = 1;
    juce::Colour colourA { 0xff3a6e3a }, colourB { 0xff7a3a2a };
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override { update (e); }
    void mouseDrag (const juce::MouseEvent& e) override { update (e); }
private:
    void update (const juce::MouseEvent& e);
};
