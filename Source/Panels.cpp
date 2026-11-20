// Docked panels: Browser (left), Effects and Macros (right), Keyboard (bottom).
#include "Views.h"

juce::StringArray& browserFavourites();   // Settings.cpp

namespace
{
// ============================================================================
// Browser
// ============================================================================
class BrowserPanel : public ViewBase, private juce::ListBoxModel, private juce::Timer
{
public:
    BrowserPanel()
    {
        tabs.setTabs ({ "Samples", "Banks", "Sessions", "Packs", "Files" });
        tabs.onChange = [this] (int) { goToRoot(); };
        addAndMakeVisible (tabs);
        upBtn.onClick = [this] { if (current != root && current.getParentDirectory().exists()) { current = current.getParentDirectory(); search.clear(); scan(); } };
        addAndMakeVisible (upBtn);
        favBtn.onClick = [this] { favouritesMenu(); };
        addAndMakeVisible (favBtn);
        search.setTextToShowWhenEmpty ("Search", Pal::textFaint);
        search.onTextChange = [this] { startTimer (250); };
        addAndMakeVisible (search);
        list.setModel (this);
        list.setRowHeight (30);
        list.setColour (juce::ListBox::backgroundColourId, Pal::panel);
        addAndMakeVisible (list);
        methodField.choices = { "Replace pad", "New layer", "Add to layer" };
        methodField.getIndex = [this] { return loadMethod; };
        methodField.setIndex = [this] (int i) { loadMethod = i; };
        addAndMakeVisible (methodField);
        previewToggle.setButtonText ("Auto preview");
        previewToggle.setToggleState (true, juce::dontSendNotification);
        addAndMakeVisible (previewToggle);
        loadBtn.onClick = [this] { activate (list.getSelectedRow()); };
        addAndMakeVisible (loadBtn);
        revealBtn.onClick = [this] { current.revealToUser(); };
        addAndMakeVisible (revealBtn);
        goToRoot();
    }

    void refresh() override { if (isVisible()) scan(); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Pal::panel);
        drawCaps (g, "Browser", juce::Rectangle<int> (14, 8, getWidth() - 28, 20), juce::Justification::centredLeft, Pal::textDim, 11.0f);
        g.setColour (Pal::textFaint);
        g.setFont (Fonts::ui (11.0f));
        g.drawFittedText (current.getFullPathName(), pathArea, juce::Justification::centredLeft, 1, 0.6f);
        if (entries.isEmpty())
            drawCaps (g, search.isEmpty() ? "This folder is empty" : "Nothing found", list.getBounds().withHeight (60), juce::Justification::centred, Pal::textFaint, 10.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 8);
        r.removeFromTop (22);
        tabs.setBounds (r.removeFromTop (30));
        r.removeFromTop (8);
        auto nav = r.removeFromTop (28);
        upBtn.setBounds (nav.removeFromLeft (28));
        favBtn.setBounds (nav.removeFromRight (28));
        nav.removeFromLeft (6);
        pathArea = nav;
        r.removeFromTop (6);
        search.setBounds (r.removeFromTop (28));
        r.removeFromTop (6);
        auto bottom = r.removeFromBottom (96);
        auto b1 = bottom.removeFromTop (44);
        methodField.setBounds (b1.removeFromLeft (b1.getWidth() / 2));
        previewToggle.setBounds (b1.reduced (4, 10));
        bottom.removeFromTop (8);
        auto b2 = bottom.removeFromTop (32);
        loadBtn.setBounds (b2.removeFromLeft (b2.getWidth() / 2).reduced (2, 0));
        revealBtn.setBounds (b2.reduced (2, 0));
        r.removeFromBottom (6);
        list.setBounds (r);
    }

private:
    juce::File rootFor (int tab)
    {
        switch (tab)
        {
            case 0: return Engine::getSamplesFolder();
            case 1: return Engine::getBanksFolder();
            case 2: return Engine::getSessionsFolder();
            case 3: return Engine::getPacksFolder();
            default: return juce::File::getSpecialLocation (juce::File::userHomeDirectory);
        }
    }

    void goToRoot()
    {
        root = rootFor (tabs.getSelected());
        root.createDirectory();
        current = root;
        search.clear();
        scan();
    }

    void scan()
    {
        entries.clear();
        const auto q = search.getText().trim();
        const bool recursive = q.isNotEmpty();
        juce::Array<juce::File> folders, files;
        int count = 0;
        for (const auto& entry : juce::RangedDirectoryIterator (current, recursive, "*", juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles))
        {
            const auto f = entry.getFile();
            if (recursive && ! f.getFileName().containsIgnoreCase (q)) continue;
            if (entry.isDirectory()) { if (! recursive) folders.add (f); }
            else if (isAudioFile (f) || f.hasFileExtension ("crbank;crsession;crpack;mid;midi")) files.add (f);
            if (++count > 4000) break;
        }
        std::sort (folders.begin(), folders.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; });
        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; });
        entries.addArray (folders);
        entries.addArray (files);
        list.updateContent();
        list.repaint();
        upBtn.setEnabled (current != root);
        repaint();
    }

    int getNumRows() override { return entries.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (! juce::isPositiveAndBelow (row, entries.size())) return;
        const auto& f = entries[row];
        if (selected) { g.setColour (Pal::surface3); g.fillRoundedRectangle (juce::Rectangle<float> (2.0f, 1.0f, (float) w - 4.0f, (float) h - 2.0f), 4.0f); }
        Icon ic = f.isDirectory() ? Icon::folder : (isAudioFile (f) ? Icon::wave : (f.hasFileExtension ("crbank") ? Icon::pads : (f.hasFileExtension ("crsession") ? Icon::sequencer : (f.hasFileExtension ("mid;midi") ? Icon::keyboard : Icon::star))));
        drawIcon (g, ic, juce::Rectangle<float> (8.0f, 5.0f, 20.0f, 20.0f), f.isDirectory() ? Pal::accent : Pal::textDim);
        g.setColour (Pal::text);
        g.setFont (Fonts::ui (12.5f, f.isDirectory()));
        g.drawFittedText (f.isDirectory() ? f.getFileName() : f.getFileNameWithoutExtension(), juce::Rectangle<int> (36, 0, w - 90, h), juce::Justification::centredLeft, 1);
        if (! f.isDirectory())
            drawCaps (g, f.getFileExtension().substring (1), juce::Rectangle<int> (w - 56, 0, 48, h), juce::Justification::centredRight, Pal::textFaint, 9.0f);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        if (! juce::isPositiveAndBelow (row, entries.size())) return;
        const auto& f = entries[row];
        if (e.mods.isPopupMenu()) { itemMenu (f); return; }
        if (isAudioFile (f) && previewToggle.getToggleState()) Engine::get().preview (f);
    }

    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { activate (row); }
    void returnKeyPressed (int row) override { activate (row); }

    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override
    {
        if (rows.size() == 0) return {};
        const auto& f = entries[rows[0]];
        if (f.isDirectory()) return {};
        return "file:" + f.getFullPathName();
    }

    void activate (int row)
    {
        if (! juce::isPositiveAndBelow (row, entries.size())) return;
        const auto f = entries[row];
        auto& e = Engine::get();
        juce::String err;
        if (f.isDirectory()) { current = f; search.clear(); scan(); return; }
        if (isAudioFile (f))
        {
            e.stopPreview();
            e.checkpointPad (e.selectedBank, e.selectedPad);
            e.loadSampleToPad (f, e.selectedBank, e.selectedPad, (Engine::LoadMethod) loadMethod);
            Nav::setStatus ("Loaded " + f.getFileNameWithoutExtension() + " onto pad " + juce::String (e.selectedPad.load() + 1));
            if (loadMethod == 0 && e.selectedPad.load() < cr::numPads - 1 && juce::ModifierKeys::currentModifiers.isShiftDown()) e.selectedPad = e.selectedPad.load() + 1;
        }
        else if (f.hasFileExtension ("crbank")) { e.loadBankPreset (e.selectedBank, f, err); Nav::setStatus ("Bank loaded into " + cr::bankName (e.selectedBank)); }
        else if (f.hasFileExtension ("crsession")) { Nav::setStatus ("Opening..."); e.loadSession (f, err); }
        else if (f.hasFileExtension ("crpack")) { if (e.installPack (f, err)) { tabs.setSelected (3, true); } }
        else if (f.hasFileExtension ("mid;midi")) e.importMidi (f, e.selectedBank, err);
        if (err.isNotEmpty()) showError ("Browser", err);
        ViewBase::broadcast();
    }

    void itemMenu (const juce::File& f)
    {
        auto& e = Engine::get();
        juce::PopupMenu m;
        if (isAudioFile (f))
        {
            m.addItem (1, "Load onto the selected pad (replace)");
            m.addItem (2, "Load as a new layer");
            m.addItem (3, "Add to the selected layer");
            m.addItem (4, "Load into an audio track");
            m.addItem (5, "Preview");
        }
        m.addItem (6, "Show in file manager");
        m.addItem (7, "Add this folder to favourites");
        m.showMenuAsync (juce::PopupMenu::Options(), [this, f] (int r)
        {
            auto& en = Engine::get();
            juce::String err;
            if (r >= 1 && r <= 3) { en.checkpointPad (en.selectedBank, en.selectedPad); en.loadSampleToPad (f, en.selectedBank, en.selectedPad, (Engine::LoadMethod) (r - 1)); }
            else if (r == 4) en.importAudioToTracks ({ f.getFullPathName() }, err);
            else if (r == 5) en.preview (f);
            else if (r == 6) f.revealToUser();
            else if (r == 7) { browserFavourites().addIfNotAlreadyThere ((f.isDirectory() ? f : f.getParentDirectory()).getFullPathName()); saveAppSettings(); }
            ViewBase::broadcast();
        });
        juce::ignoreUnused (e);
    }

    void favouritesMenu()
    {
        juce::PopupMenu m;
        m.addSectionHeader ("Places");
        m.addItem (1, "Library");
        m.addItem (2, "Home folder");
        m.addItem (3, "Desktop");
        m.addItem (4, "Music");
        auto& favs = browserFavourites();
        if (! favs.isEmpty()) m.addSectionHeader ("Favourites");
        for (int i = 0; i < favs.size(); ++i) m.addItem (100 + i, juce::File (favs[i]).getFileName());
        m.addSeparator();
        m.addItem (5, "Add the current folder to favourites");
        m.addItem (6, "Choose a folder...");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&favBtn), [this] (int r)
        {
            auto& fv = browserFavourites();
            juce::File target;
            if (r == 1) target = Engine::getLibraryFolder();
            else if (r == 2) target = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
            else if (r == 3) target = juce::File::getSpecialLocation (juce::File::userDesktopDirectory);
            else if (r == 4) target = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
            else if (r == 5) { fv.addIfNotAlreadyThere (current.getFullPathName()); saveAppSettings(); return; }
            else if (r == 6)
            {
                chooser = std::make_unique<juce::FileChooser> ("Choose a folder", current);
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
                {
                    if (fc.getResult().isDirectory()) { root = fc.getResult(); current = root; tabs.setSelected (4, false); scan(); }
                });
                return;
            }
            else if (r >= 100) target = juce::File (fv[r - 100]);
            if (target.isDirectory()) { tabs.setSelected (4, false); root = target; current = target; search.clear(); scan(); }
        });
    }

    void timerCallback() override { stopTimer(); scan(); }

    TabStrip tabs;
    IconButton upBtn { Icon::up, "Up one folder" }, favBtn { Icon::star, "Places and favourites" };
    juce::TextEditor search;
    juce::ListBox list;
    ChoiceField methodField { "Load method" };
    juce::ToggleButton previewToggle;
    juce::TextButton loadBtn { "Load" }, revealBtn { "Show folder" };
    juce::Array<juce::File> entries;
    juce::File root, current;
    juce::Rectangle<int> pathArea;
    int loadMethod = 0;
    std::unique_ptr<juce::FileChooser> chooser;
};

// ============================================================================
// Effects panel: the selected pad's or bank's inserts
// ============================================================================
class FxPanel : public ViewBase
{
public:
    FxPanel()
    {
        tabs.setTabs ({ "Pad", "Bank", "Main" });
        tabs.onChange = [this] (int) { refresh(); };
        addAndMakeVisible (tabs);
        addAndMakeVisible (chain);
        lockBtn.setClickingTogglesState (true);
        addAndMakeVisible (lockBtn);
    }

    void refresh() override
    {
        auto& e = Engine::get();
        if (! lockBtn.getToggleState()) { bank = e.selectedBank; pad = e.selectedPad; }
        auto& b = e.session().banks[(size_t) bank];
        const int t = tabs.getSelected();
        if (t == 0) chain.setChain (&b.pads[(size_t) pad].channel.inserts, "pad/" + juce::String (bank) + "/" + juce::String (pad) + "/", padColour (bank, pad));
        else if (t == 1) chain.setChain (&b.strip.inserts, "bank/" + juce::String (bank) + "/", b.colour);
        else chain.setChain (&e.session().master.inserts, "master/", Pal::rec);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Pal::panel);
        auto& e = Engine::get();
        drawCaps (g, "Effects", juce::Rectangle<int> (14, 8, 200, 20), juce::Justification::centredLeft, Pal::textDim, 11.0f);
        const auto& p = e.session().banks[(size_t) bank].pads[(size_t) pad];
        juce::String who = tabs.getSelected() == 0 ? cr::bankLetter (bank) + "." + juce::String (pad + 1) + "  " + p.name
                         : tabs.getSelected() == 1 ? e.session().banks[(size_t) bank].name : juce::String ("Main out");
        g.setColour (Pal::text);
        g.setFont (Fonts::ui (14.0f, true));
        g.drawFittedText (who, titleArea, juce::Justification::centredLeft, 1);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 8);
        r.removeFromTop (22);
        auto t = r.removeFromTop (30);
        lockBtn.setBounds (t.removeFromRight (30));
        tabs.setBounds (t);
        r.removeFromTop (8);
        titleArea = r.removeFromTop (26);
        r.removeFromTop (6);
        chain.setBounds (r);
    }

private:
    TabStrip tabs;
    InsertChainView chain;
    IconButton lockBtn { Icon::lock, "Keep showing this pad / bank", true };
    juce::Rectangle<int> titleArea;
    int bank = 0, pad = 0;
};

// ============================================================================
// Macros panel: 16 macro knobs, bindings, X/Y
// ============================================================================
class MacrosPanel : public ViewBase, private juce::Timer
{
public:
    MacrosPanel()
    {
        tabs.setTabs ({ "Macros 1-8", "Macros 9-16", "X/Y" });
        tabs.onChange = [this] (int) { refresh(); };
        addAndMakeVisible (tabs);
        for (int i = 0; i < 8; ++i)
        {
            auto* k = knobs.add (new Knob());
            addAndMakeVisible (k);
            auto* b = editButtons.add (new IconButton (Icon::link, "Show what this macro controls"));
            b->onClick = [this, i] { selected = tabs.getSelected() * 8 + i; refresh(); };
            addAndMakeVisible (b);
        }
        addChildComponent (xy1);
        addChildComponent (xy2);
        xy2.macroX = 2; xy2.macroY = 3;
        xy1.colourA = juce::Colour (0xff2e5d36); xy1.colourB = juce::Colour (0xff6b4a1f);
        xy2.colourA = juce::Colour (0xff2d4060); xy2.colourB = juce::Colour (0xff5b2d55);
        renameBtn.onClick = [this] { askForText ("Macro name", Engine::get().session().macros[(size_t) selected].name, [this] (const juce::String& n) { Engine::get().session().macros[(size_t) selected].name = n; refresh(); }); };
        addAndMakeVisible (renameBtn);
        addAndMakeVisible (bindingsView);
        bindingsView.setViewedComponent (&bindingsContent, false);
        bindingsView.setScrollBarsShown (true, false);
        startTimerHz (10);
    }

    void refresh() override
    {
        auto& e = Engine::get();
        const int page = tabs.getSelected();
        const bool xy = page == 2;
        for (int i = 0; i < 8; ++i)
        {
            knobs[i]->setVisible (! xy);
            editButtons[i]->setVisible (! xy);
            if (! xy) knobs[i]->setParam ("macro/" + juce::String (page * 8 + i), e.session().macros[(size_t) (page * 8 + i)].name);
            knobs[i]->setAccent (page * 8 + i == selected ? Pal::accent : Pal::info);
        }
        xy1.setVisible (xy);
        xy2.setVisible (xy);
        rows.clear();
        auto& m = e.session().macros[(size_t) selected];
        for (int i = 0; i < (int) m.bindings.size(); ++i)
        {
            auto* row = rows.add (new BindingRow (selected, i));
            row->onRemove = [this] { juce::MessageManager::callAsync ([this] { refresh(); }); };
            bindingsContent.addAndMakeVisible (row);
        }
        resized();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Pal::panel);
        drawCaps (g, "Macros", juce::Rectangle<int> (14, 8, 200, 20), juce::Justification::centredLeft, Pal::textDim, 11.0f);
        auto& m = Engine::get().session().macros[(size_t) selected];
        g.setColour (Pal::text);
        g.setFont (Fonts::ui (13.5f, true));
        g.drawFittedText ("Macro " + juce::String (selected + 1) + ": " + m.name, bindTitle, juce::Justification::centredLeft, 1);
        if (m.bindings.empty())
        {
            g.setColour (Pal::textFaint);
            g.setFont (Fonts::ui (12.0f));
            g.drawFittedText ("Nothing assigned. Right-click any knob in the app and choose 'Assign to macro'. Macros are also modulation sources in the Editor.",
                              bindingsView.getBounds().reduced (4), juce::Justification::topLeft, 4);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 8);
        r.removeFromTop (22);
        tabs.setBounds (r.removeFromTop (30));
        r.removeFromTop (10);
        auto top = r.removeFromTop (juce::jmin (300, r.getHeight() / 2));
        if (tabs.getSelected() == 2)
        {
            xy1.setBounds (top.removeFromTop (top.getHeight() / 2).reduced (0, 3));
            xy2.setBounds (top.reduced (0, 3));
        }
        else
        {
            const int w = top.getWidth() / 4, h = top.getHeight() / 2;
            for (int i = 0; i < 8; ++i)
            {
                auto cell = juce::Rectangle<int> (top.getX() + (i % 4) * w, top.getY() + (i / 4) * h, w, h);
                editButtons[i]->setBounds (cell.removeFromBottom (22).withSizeKeepingCentre (22, 22));
                knobs[i]->setBounds (cell);
            }
        }
        r.removeFromTop (10);
        auto t = r.removeFromTop (28);
        renameBtn.setBounds (t.removeFromRight (80));
        bindTitle = t;
        r.removeFromTop (6);
        bindingsView.setBounds (r);
        int y = 0;
        for (auto* row : rows) { row->setBounds (0, y, r.getWidth() - 8, 70); y += 74; }
        bindingsContent.setSize (r.getWidth() - 8, juce::jmax (10, y));
    }

private:
    class BindingRow : public juce::Component
    {
    public:
        BindingRow (int m, int i) : macro (m), index (i)
        {
            auto& b = Engine::get().session().macros[(size_t) m].bindings[(size_t) i];
            name = Engine::get().getParamDisplayName (b.param);
            range.setSliderStyle (juce::Slider::TwoValueHorizontal);
            range.setRange (0.0, 1.0);
            range.setMinAndMaxValues (b.min, b.max, juce::dontSendNotification);
            range.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            range.onValueChange = [this]
            {
                auto& mm = Engine::get().session().macros[(size_t) macro];
                if (index < (int) mm.bindings.size()) { mm.bindings[(size_t) index].min = (float) range.getMinValue(); mm.bindings[(size_t) index].max = (float) range.getMaxValue(); }
            };
            invert.onClick = [this] { const double a = range.getMinValue(), b2 = range.getMaxValue(); auto& mm = Engine::get().session().macros[(size_t) macro]; if (index < (int) mm.bindings.size()) std::swap (mm.bindings[(size_t) index].min, mm.bindings[(size_t) index].max); juce::ignoreUnused (a, b2); };
            remove.onClick = [this] { Engine::get().unbindMacro (macro, index); if (onRemove) onRemove(); };
            addAndMakeVisible (range);
            addAndMakeVisible (invert);
            addAndMakeVisible (remove);
        }
        std::function<void()> onRemove;
        void paint (juce::Graphics& g) override
        {
            g.setColour (Pal::surface);
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);
            g.setColour (Pal::text);
            g.setFont (Fonts::ui (11.5f, true));
            g.drawFittedText (name, getLocalBounds().reduced (10, 6).removeFromTop (18).withTrimmedRight (60), juce::Justification::centredLeft, 1);
            drawCaps (g, "range", getLocalBounds().reduced (10, 6).removeFromBottom (14), juce::Justification::centredLeft, Pal::textFaint, 8.5f);
        }
        void resized() override
        {
            auto r = getLocalBounds().reduced (10, 6);
            auto top = r.removeFromTop (20);
            remove.setBounds (top.removeFromRight (24));
            invert.setBounds (top.removeFromRight (60).reduced (2, 0));
            range.setBounds (r.withTrimmedBottom (12));
        }
    private:
        int macro, index;
        juce::String name;
        juce::Slider range;
        juce::TextButton invert { "Invert" };
        IconButton remove { Icon::trash, "Remove" };
    };

    void timerCallback() override
    {
        if (tabs.getSelected() == 2 && isShowing()) { xy1.repaint(); xy2.repaint(); }
    }

    TabStrip tabs;
    juce::OwnedArray<Knob> knobs;
    juce::OwnedArray<IconButton> editButtons;
    XYPad xy1, xy2;
    juce::TextButton renameBtn { "Rename" };
    juce::Viewport bindingsView;
    juce::Component bindingsContent;
    juce::OwnedArray<BindingRow> rows;
    juce::Rectangle<int> bindTitle;
    int selected = 0;
};

// ============================================================================
// Keyboard panel
// ============================================================================
class KeyboardPanel : public ViewBase, private juce::MidiKeyboardState::Listener
{
public:
    KeyboardPanel()
    {
        state.addListener (this);
        keyboard.setAvailableRange (0, 127);
        keyboard.setLowestVisibleKey (36);
        keyboard.setKeyWidth (28.0f);
        keyboard.setScrollButtonsVisible (true);
        keyboard.setVelocity (0.85f, true);
        addAndMakeVisible (keyboard);
        downBtn.onClick = [this] { keyboard.setLowestVisibleKey (juce::jmax (0, keyboard.getLowestVisibleKey() - 12)); };
        upBtn.onClick = [this] { keyboard.setLowestVisibleKey (juce::jmin (108, keyboard.getLowestVisibleKey() + 12)); };
        addAndMakeVisible (downBtn);
        addAndMakeVisible (upBtn);
    }
    ~KeyboardPanel() override { state.removeListener (this); }

    void refresh() override { repaint(); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Pal::panel);
        auto& e = Engine::get();
        auto r = getLocalBounds().removeFromLeft (150).reduced (10, 8);
        drawCaps (g, "Keyboard", r.removeFromTop (16), juce::Justification::centredLeft, Pal::textDim, 10.0f);
        g.setColour (padColour (e.selectedBank, e.selectedPad));
        g.setFont (Fonts::ui (12.5f, true));
        g.drawFittedText (cr::bankLetter (e.selectedBank) + "." + juce::String (e.selectedPad.load() + 1) + "  " + currentPad().name, r.removeFromTop (34), juce::Justification::centredLeft, 2);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto left = r.removeFromLeft (150).reduced (10, 8);
        auto b = left.removeFromBottom (30);
        downBtn.setBounds (b.removeFromLeft (b.getWidth() / 2).reduced (2, 0));
        upBtn.setBounds (b.reduced (2, 0));
        keyboard.setBounds (r);
    }

private:
    void handleNoteOn (juce::MidiKeyboardState*, int, int note, float vel) override
    {
        auto& e = Engine::get();
        e.padDown (e.selectedBank, e.selectedPad, e.fixedVelocityOn.load() ? e.fixedVelocity.load() : vel, note);
    }
    void handleNoteOff (juce::MidiKeyboardState*, int, int note, float) override
    {
        auto& e = Engine::get();
        e.padUp (e.selectedBank, e.selectedPad, note);
    }

    juce::MidiKeyboardState state;
    juce::MidiKeyboardComponent keyboard { state, juce::MidiKeyboardComponent::horizontalKeyboard };
    juce::TextButton downBtn { "Oct -" }, upBtn { "Oct +" };
};
} // namespace

std::unique_ptr<ViewBase> createBrowserPanel() { return std::make_unique<BrowserPanel>(); }
std::unique_ptr<ViewBase> createFxPanel() { return std::make_unique<FxPanel>(); }
std::unique_ptr<ViewBase> createMacrosPanel() { return std::make_unique<MacrosPanel>(); }
std::unique_ptr<ViewBase> createKeyboardPanel() { return std::make_unique<KeyboardPanel>(); }
