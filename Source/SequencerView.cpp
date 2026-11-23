// Sequencer view: Song (arranger + audio tracks + automation), Scenes, Pattern editor.
#include "Views.h"

namespace
{
// ============================================================================
// Shared helpers
// ============================================================================
struct TimeAxis
{
    double pxPerTick = 0.12;
    double startTick = 0.0;
    float toX (double tick, int x0) const { return (float) x0 + (float) ((tick - startTick) * pxPerTick); }
    double toTick (float x, int x0) const { return startTick + (x - (float) x0) / pxPerTick; }
};

juce::StringArray gridNames() { return { "Off", "1 bar", "1/2", "1/4", "1/8", "1/16", "1/32", "1/8 T", "1/16 T" }; }
int gridTicks (int g)
{
    switch (g)
    {
        case 1: return cr::barTicks();
        case 2: return cr::ppq * 2;
        case 3: return cr::ppq;
        case 4: return cr::ppq / 2;
        case 5: return cr::ppq / 4;
        case 6: return cr::ppq / 8;
        case 7: return cr::ppq / 3;
        case 8: return cr::ppq / 6;
        default: return 1;
    }
}
int snapTo (double tick, int grid, bool floorIt = false)
{
    const int g = gridTicks (grid);
    if (g <= 1) return (int) std::round (tick);
    return (floorIt ? (int) std::floor (tick / g) : (int) std::round (tick / g)) * g;
}

void drawRuler (juce::Graphics& g, juce::Rectangle<int> r, const TimeAxis& ax, int maxTick, bool showLoop, double playhead)
{
    auto& s = Engine::get().session();
    g.setColour (Pal::panel);
    g.fillRect (r);
    const int bt = cr::barTicks();
    // bar labels: keep ~60 px between labels
    int barStep = 1;
    while (barStep * bt * ax.pxPerTick < 46.0) barStep *= 2;
    const int firstBar = juce::jmax (0, (int) (ax.startTick / bt));
    for (int bar = firstBar; ; ++bar)
    {
        const float x = ax.toX ((double) bar * bt, r.getX());
        if (x > (float) r.getRight()) break;
        if (bar % barStep != 0) { g.setColour (Pal::outline); g.fillRect (x, (float) r.getBottom() - 5.0f, 1.0f, 5.0f); continue; }
        g.setColour (Pal::line);
        g.fillRect (x, (float) r.getY() + 4.0f, 1.0f, (float) r.getHeight() - 4.0f);
        g.setColour (Pal::textDim);
        g.setFont (Fonts::ui (10.5f, true));
        g.drawText (juce::String (bar + 1), (int) x + 4, r.getY(), 40, r.getHeight(), juce::Justification::centredLeft);
    }
    juce::ignoreUnused (maxTick);
    if (showLoop)
    {
        const float a = ax.toX (s.loopStart, r.getX()), b = ax.toX (s.loopEnd, r.getX());
        g.setColour ((s.loopOn ? Pal::accent : Pal::textFaint).withAlpha (s.loopOn ? 0.35f : 0.2f));
        g.fillRect (juce::Rectangle<float> (a, (float) r.getY(), b - a, 6.0f));
    }
    if (playhead >= 0.0)
    {
        const float x = ax.toX (playhead, r.getX());
        juce::Path p;
        p.addTriangle (x - 5.0f, (float) r.getY(), x + 5.0f, (float) r.getY(), x, (float) r.getY() + 8.0f);
        g.setColour (Pal::text);
        g.fillPath (p);
    }
}

void drawGrid (juce::Graphics& g, juce::Rectangle<int> r, const TimeAxis& ax, int gridIndex)
{
    const int bt = cr::barTicks();
    int step = gridTicks (gridIndex);
    if (step <= 1) step = cr::ppq;
    while (step * ax.pxPerTick < 8.0) step *= 2;
    const double first = std::floor (ax.startTick / step) * step;
    for (double t = first; ; t += step)
    {
        const float x = ax.toX (t, r.getX());
        if (x > (float) r.getRight()) break;
        if (x < (float) r.getX()) continue;
        const bool bar = std::fmod (t, (double) bt) < 0.5;
        const bool beat = std::fmod (t, (double) cr::ppq) < 0.5;
        g.setColour (bar ? Pal::line : (beat ? Pal::outline : Pal::outline.withAlpha (0.5f)));
        g.fillRect (x, (float) r.getY(), 1.0f, (float) r.getHeight());
    }
}

// ============================================================================
// Automation lane editor
// ============================================================================
class LaneEditor : public juce::Component
{
public:
    bool songLevel = true;
    int bank = 0;
    juce::String param;
    TimeAxis* axis = nullptr;
    int* tool = nullptr;      // 0 edit, 1 draw
    int* grid = nullptr;
    juce::Colour colour = Pal::accent;
    int x0 = 0;

    cr::AutomationLane* lane()
    {
        auto& e = Engine::get();
        std::vector<cr::AutomationLane>* lanes = nullptr;
        if (songLevel) lanes = &e.session().songAutomation;
        else
        {
            auto& b = e.session().banks[(size_t) bank];
            if (! juce::isPositiveAndBelow (b.workingPattern, (int) b.patterns.size())) return nullptr;
            lanes = &b.patterns[(size_t) b.workingPattern].automation;
        }
        for (auto& l : *lanes) if (l.param == param) return &l;
        return nullptr;
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        g.setColour (Pal::bg);
        g.fillRect (r);
        if (axis) drawGrid (g, r, *axis, grid ? *grid : 3);
        g.setColour (Pal::outline);
        g.fillRect (r.getX(), r.getBottom() - 1, r.getWidth(), 1);
        g.setColour (Pal::outline.withAlpha (0.6f));
        g.fillRect (r.getX(), r.getCentreY(), r.getWidth(), 1);
        auto* l = lane();
        if (l == nullptr || axis == nullptr) return;
        juce::Path p;
        const float top = 4.0f, h = (float) getHeight() - 8.0f;
        auto yOf = [&] (float v) { return top + (1.0f - v) * h; };
        if (! l->points.empty())
        {
            p.startNewSubPath ((float) x0, yOf (l->points.front().value));
            for (auto& pt : l->points) p.lineTo (axis->toX (pt.tick, x0), yOf (pt.value));
            p.lineTo ((float) getWidth(), yOf (l->points.back().value));
        }
        g.setColour (colour.withAlpha (l->suspended ? 0.4f : 1.0f));
        g.strokePath (p, juce::PathStrokeType (1.6f));
        for (auto& pt : l->points)
        {
            const float x = axis->toX (pt.tick, x0);
            if (x < -6.0f || x > (float) getWidth() + 6.0f) continue;
            g.setColour (pt.selected ? juce::Colours::white : colour);
            g.fillRect (x - 3.5f, yOf (pt.value) - 3.5f, 7.0f, 7.0f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto* l = lane();
        if (l == nullptr || axis == nullptr) return;
        Engine::get().checkpoint();
        dragIndex = hit (e.position);
        if (e.mods.isPopupMenu())
        {
            if (dragIndex >= 0) { const juce::ScopedLock sl (Engine::get().getLock()); l->points.erase (l->points.begin() + dragIndex); }
            dragIndex = -1;
            repaint();
            return;
        }
        if (dragIndex < 0 && (tool == nullptr || *tool == 0)) dragIndex = addPoint (e.position);
        if (tool != nullptr && *tool == 1) drawAt (e.position);
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto* l = lane();
        if (l == nullptr || axis == nullptr) return;
        if (tool != nullptr && *tool == 1) { drawAt (e.position); repaint(); return; }
        if (dragIndex < 0 || dragIndex >= (int) l->points.size()) return;
        const juce::ScopedLock sl (Engine::get().getLock());
        auto& pt = l->points[(size_t) dragIndex];
        pt.tick = juce::jmax (0, snapTo (axis->toTick (e.position.x, x0), grid ? *grid : 0));
        pt.value = valueAt (e.position.y);
        const int t = pt.tick;
        const float v = pt.value;
        l->sort();
        for (int i = 0; i < (int) l->points.size(); ++i) if (l->points[(size_t) i].tick == t && l->points[(size_t) i].value == v) dragIndex = i;
        repaint();
    }

private:
    float valueAt (float y) const { return juce::jlimit (0.0f, 1.0f, 1.0f - (y - 4.0f) / ((float) getHeight() - 8.0f)); }
    int hit (juce::Point<float> p)
    {
        auto* l = lane();
        if (l == nullptr) return -1;
        for (int i = 0; i < (int) l->points.size(); ++i)
        {
            const auto& pt = l->points[(size_t) i];
            const float x = axis->toX (pt.tick, x0), y = 4.0f + (1.0f - pt.value) * ((float) getHeight() - 8.0f);
            if (std::abs (x - p.x) < 6.0f && std::abs (y - p.y) < 6.0f) return i;
        }
        return -1;
    }
    int addPoint (juce::Point<float> p)
    {
        auto* l = lane();
        const juce::ScopedLock sl (Engine::get().getLock());
        const int t = juce::jmax (0, snapTo (axis->toTick (p.x, x0), grid ? *grid : 0));
        l->points.push_back ({ t, valueAt (p.y) });
        l->sort();
        for (int i = 0; i < (int) l->points.size(); ++i) if (l->points[(size_t) i].tick == t) return i;
        return -1;
    }
    void drawAt (juce::Point<float> p)
    {
        auto* l = lane();
        const juce::ScopedLock sl (Engine::get().getLock());
        const int g = juce::jmax (cr::ppq / 8, gridTicks (grid ? *grid : 5));
        const int t = juce::jmax (0, (int) std::floor (axis->toTick (p.x, x0) / g) * g);
        auto it = std::find_if (l->points.begin(), l->points.end(), [t] (const cr::AutoPoint& a) { return a.tick == t; });
        if (it != l->points.end()) it->value = valueAt (p.y);
        else { l->points.push_back ({ t, valueAt (p.y) }); l->sort(); }
    }
    int dragIndex = -1;
};

// A stack of lane editors with headers and an "add lane" button.
class AutomationArea : public juce::Component
{
public:
    bool songLevel = true;
    int trackKey = 0;     // song: bank / 1000+t / 2000+a / 3000 ; pattern: bank
    TimeAxis* axis = nullptr;
    int* grid = nullptr;
    int x0 = 200;

    AutomationArea()
    {
        addBtn.onClick = [this] { addLaneMenu(); };
        addAndMakeVisible (addBtn);
        drawBtn.setClickingTogglesState (true);
        drawBtn.onClick = [this] { tool = drawBtn.getToggleState() ? 1 : 0; };
        addAndMakeVisible (drawBtn);
        clearBtn.onClick = [this] { removeAll(); };
        addAndMakeVisible (clearBtn);
    }

    std::vector<cr::AutomationLane>* lanes()
    {
        auto& e = Engine::get();
        if (songLevel) return &e.session().songAutomation;
        auto& b = e.session().banks[(size_t) juce::jlimit (0, cr::numBanks - 1, trackKey)];
        if (! juce::isPositiveAndBelow (b.workingPattern, (int) b.patterns.size())) return nullptr;
        return &b.patterns[(size_t) b.workingPattern].automation;
    }

    bool laneBelongs (const juce::String& param) const
    {
        if (! songLevel) return true;
        const auto a = cr::ParamAddress::parse (param);
        using O = cr::ParamAddress::Owner;
        if (trackKey < cr::numBanks) return a.bankIndex() == trackKey;
        if (trackKey >= 1000 && trackKey < 2000) return a.owner == O::audio && a.a == trackKey - 1000;
        if (trackKey >= 2000 && trackKey < 3000) return a.owner == O::aux && a.a == trackKey - 2000;
        return a.owner == O::master || a.owner == O::tempo || a.owner == O::macro;
    }

    void rebuild()
    {
        editors.clear();
        auto* ls = lanes();
        if (ls != nullptr)
            for (auto& l : *ls)
            {
                if (! laneBelongs (l.param)) continue;
                auto* ed = editors.add (new LaneEditor());
                ed->songLevel = songLevel;
                ed->bank = trackKey;
                ed->param = l.param;
                ed->axis = axis;
                ed->tool = &tool;
                ed->grid = grid;
                ed->colour = colourFor();
                addAndMakeVisible (ed);
            }
        resized();
        repaint();
    }

    juce::Colour colourFor() const
    {
        auto& e = Engine::get();
        if (trackKey < cr::numBanks) return e.session().banks[(size_t) trackKey].colour;
        if (trackKey >= 1000 && trackKey < 2000) return e.session().audioTracks[(size_t) (trackKey - 1000)].colour;
        return Pal::accent;
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Pal::panel);
        auto& e = Engine::get();
        int y = 32;
        for (auto* ed : editors)
        {
            auto hdr = juce::Rectangle<int> (0, y, x0, ed->getHeight());
            g.setColour (Pal::surface);
            g.fillRect (hdr.reduced (0, 1));
            g.setColour (Pal::text);
            g.setFont (Fonts::ui (11.5f, true));
            g.drawFittedText (e.getParamDisplayName (ed->param), hdr.reduced (10, 6).withTrimmedRight (24), juce::Justification::topLeft, 3);
            drawIcon (g, Icon::trash, juce::Rectangle<float> ((float) x0 - 26.0f, (float) y + 6.0f, 18.0f, 18.0f), Pal::textFaint);
            y += ed->getHeight();
        }
        if (editors.isEmpty())
            drawCaps (g, songLevel ? "No automation for this track yet. Add a lane, or arm 'A' in the transport and record knob moves."
                                   : "No pattern automation yet. Add a lane, or arm 'A' and record knob moves while the pattern plays.",
                      getLocalBounds().withTrimmedTop (36).reduced (12, 0).removeFromTop (24), juce::Justification::centredLeft, Pal::textFaint, 9.5f);
    }

    void resized() override
    {
        auto top = getLocalBounds().removeFromTop (32).reduced (8, 4);
        addBtn.setBounds (top.removeFromLeft (120));
        top.removeFromLeft (6);
        drawBtn.setBounds (top.removeFromLeft (80));
        top.removeFromLeft (6);
        clearBtn.setBounds (top.removeFromLeft (100));
        int y = 32;
        const int h = editors.isEmpty() ? 0 : juce::jlimit (60, 120, (getHeight() - 32) / juce::jmax (1, editors.size()));
        for (auto* ed : editors)
        {
            ed->x0 = 0;
            ed->setBounds (x0, y, getWidth() - x0, h);
            y += h;
        }
    }

    void mouseDown (const juce::MouseEvent& ev) override
    {
        int y = 32;
        for (auto* ed : editors)
        {
            if (ev.y >= y && ev.y < y + ed->getHeight() && ev.x > x0 - 30 && ev.x < x0)
            {
                Engine::get().checkpoint();
                auto* ls = lanes();
                const juce::ScopedLock sl (Engine::get().getLock());
                ls->erase (std::remove_if (ls->begin(), ls->end(), [&] (const cr::AutomationLane& l) { return l.param == ed->param; }), ls->end());
                juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<AutomationArea> (this)] { if (safe) safe->rebuild(); });
                return;
            }
            y += ed->getHeight();
        }
    }

    void addLane (const juce::String& param)
    {
        auto& e = Engine::get();
        e.checkpoint();
        const int b = songLevel ? cr::ParamAddress::parse (param).bankIndex() : trackKey;
        if (auto* l = e.findOrCreateLane (param, songLevel, b))
            if (l->points.empty()) { const juce::ScopedLock sl (e.getLock()); l->points.push_back ({ 0, e.getParamNormalised (l->address) }); }
        rebuild();
    }

    int tool = 0;

private:
    void addLaneMenu()
    {
        auto& e = Engine::get();
        auto params = e.getAutomatableParams (trackKey);
        if (songLevel && trackKey < cr::numBanks) params.addArray (e.getAutomatableParams (3000));
        juce::PopupMenu m;
        std::map<juce::String, juce::PopupMenu> groups;
        juce::StringArray order;
        for (int i = 0; i < params.size(); ++i)
        {
            const auto name = e.getParamDisplayName (params[i]);
            const auto group = name.upToFirstOccurrenceOf (" - ", false, false);
            if (! order.contains (group)) order.add (group);
            groups[group].addItem (1 + i, name.fromFirstOccurrenceOf (" - ", false, false).isNotEmpty() ? name.fromFirstOccurrenceOf (" - ", false, false) : name);
        }
        for (auto& gname : order) m.addSubMenu (gname, groups[gname]);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addBtn), [this, params] (int r) { if (r > 0) addLane (params[r - 1]); });
    }

    void removeAll()
    {
        auto& e = Engine::get();
        e.checkpoint();
        auto* ls = lanes();
        if (ls == nullptr) return;
        const juce::ScopedLock sl (e.getLock());
        ls->erase (std::remove_if (ls->begin(), ls->end(), [this] (const cr::AutomationLane& l) { return laneBelongs (l.param); }), ls->end());
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<AutomationArea> (this)] { if (safe) safe->rebuild(); });
    }

    juce::TextButton addBtn { "+ Add lane" }, drawBtn { "Draw" }, clearBtn { "Clear lanes" };
    juce::OwnedArray<LaneEditor> editors;
};

// ============================================================================
// SONG
// ============================================================================
struct TrackRow { int key; };   // 0..127 bank, 1000 + t audio track

class SongTimeline : public juce::Component, public juce::DragAndDropTarget, public juce::FileDragAndDropTarget
{
public:
    TimeAxis axis;
    int grid = 1;
    int tool = 0;           // 0 select/move, 1 draw, 2 split
    int rowHeight = 46;
    int scrollY = 0;
    std::vector<TrackRow> rows;
    int selectedRow = 0;
    std::function<void()> onSelectionChanged, onEditPattern;
    static constexpr int headerW = 220, rulerH = 26, sideW = 34;

    void rebuildRows()
    {
        auto& e = Engine::get();
        rows.clear();
        for (int b = 0; b < cr::numBanks; ++b) if (e.session().banks[(size_t) b].created) rows.push_back ({ b });
        for (int t = 0; t < cr::maxAudioTracks; ++t) if (e.session().audioTracks[(size_t) t].created) rows.push_back ({ 1000 + t });
        selectedRow = juce::jlimit (0, juce::jmax (0, (int) rows.size() - 1), selectedRow);
    }

    juce::Rectangle<int> laneArea() const { return { headerW, rulerH, getWidth() - headerW - sideW, getHeight() - rulerH }; }
    int rowY (int i) const { return rulerH + i * rowHeight - scrollY; }
    int rowAt (int y) const { return (y - rulerH + scrollY) / rowHeight; }

    void paint (juce::Graphics& g) override
    {
        auto& e = Engine::get();
        auto& s = e.session();
        g.fillAll (Pal::bg);
        auto lanes = laneArea();
        drawRuler (g, { headerW, 0, lanes.getWidth(), rulerH }, axis, e.getSongLengthTicks(), true, e.getPositionTicks());

        g.saveState();
        g.reduceClipRegion (lanes);
        drawGrid (g, lanes, axis, grid);
        g.restoreState();

        for (int i = 0; i < (int) rows.size(); ++i)
        {
            const int y = rowY (i);
            if (y + rowHeight < rulerH || y > getHeight()) continue;
            const int key = rows[(size_t) i].key;
            const bool isBank = key < cr::numBanks;
            const juce::String name = isBank ? s.banks[(size_t) key].name : s.audioTracks[(size_t) (key - 1000)].name;
            const auto col = isBank ? s.banks[(size_t) key].colour : s.audioTracks[(size_t) (key - 1000)].colour;
            auto& strip = isBank ? s.banks[(size_t) key].strip : s.audioTracks[(size_t) (key - 1000)].strip;

            // header
            auto hdr = juce::Rectangle<int> (0, y, headerW, rowHeight).reduced (0, 1);
            g.setColour (i == selectedRow ? Pal::surface2 : Pal::panel);
            g.fillRect (hdr);
            auto badge = hdr.reduced (8, 0).removeFromLeft (26).withSizeKeepingCentre (26, 26).toFloat();
            if (isBank) drawBankBadge (g, badge, key, true);
            else
            {
                g.setColour (col);
                g.fillRoundedRectangle (badge, 3.0f);
                drawIcon (g, Icon::wave, badge.reduced (5.0f), Pal::bg);
            }
            g.setColour (Pal::text);
            g.setFont (Fonts::ui (12.5f, true));
            g.drawFittedText (name, hdr.withTrimmedLeft (44).withTrimmedRight (78), juce::Justification::centredLeft, 1);
            auto ms = hdr.removeFromRight (74).reduced (4, 11);
            auto mb = ms.removeFromLeft (22), sb = ms.removeFromLeft (22).translated (2, 0), rb = ms.translated (4, 0).withWidth (22);
            g.setColour (strip.mute ? Pal::mute : Pal::surface3); g.fillRoundedRectangle (mb.toFloat(), 3.0f);
            g.setColour (strip.solo ? Pal::solo : Pal::surface3); g.fillRoundedRectangle (sb.toFloat(), 3.0f);
            g.setFont (Fonts::ui (10.5f, true));
            g.setColour (strip.mute ? Pal::bg : Pal::textDim); g.drawText ("M", mb, juce::Justification::centred);
            g.setColour (strip.solo ? Pal::bg : Pal::textDim); g.drawText ("S", sb, juce::Justification::centred);
            const bool armed = isBank ? s.banks[(size_t) key].recordArm : s.audioTracks[(size_t) (key - 1000)].armed;
            g.setColour (armed ? Pal::rec : Pal::surface3);
            g.fillEllipse (rb.toFloat().withSizeKeepingCentre (14.0f, 14.0f));

            // lane background
            auto lane = juce::Rectangle<int> (lanes.getX(), y, lanes.getWidth(), rowHeight);
            g.setColour (Pal::outline.withAlpha (0.6f));
            g.fillRect (lane.getX(), lane.getBottom() - 1, lane.getWidth(), 1);

            g.saveState();
            g.reduceClipRegion (lanes);
            if (isBank) paintBankClips (g, key, lane, col);
            else paintAudioClips (g, key - 1000, lane, col);
            g.restoreState();

            // follow-song indicator
            auto side = juce::Rectangle<int> (getWidth() - sideW, y, sideW, rowHeight);
            g.setColour (Pal::panel);
            g.fillRect (side);
            const bool following = isBank ? s.banks[(size_t) key].source == cr::TrackSource::song : s.audioTracks[(size_t) (key - 1000)].source == cr::TrackSource::song;
            drawIcon (g, following ? Icon::play : Icon::stopSquare, side.toFloat().reduced (9.0f, 14.0f), following ? Pal::play : Pal::textFaint);
        }

        // loop + playhead
        g.saveState();
        g.reduceClipRegion (lanes);
        if (s.loopOn)
        {
            const float a = axis.toX (s.loopStart, lanes.getX()), b = axis.toX (s.loopEnd, lanes.getX());
            g.setColour (Pal::accent.withAlpha (0.05f));
            g.fillRect (juce::Rectangle<float> (a, (float) lanes.getY(), b - a, (float) lanes.getHeight()));
        }
        const float px = axis.toX (e.getPositionTicks(), lanes.getX());
        g.setColour (Pal::text);
        g.fillRect (px, (float) lanes.getY(), 1.0f, (float) lanes.getHeight());
        if (rubber.getWidth() > 1)
        {
            g.setColour (Pal::text.withAlpha (0.1f));
            g.fillRect (rubber);
            g.setColour (Pal::text.withAlpha (0.5f));
            g.drawRect (rubber);
        }
        g.restoreState();

        if (rows.empty())
        {
            g.setColour (Pal::textFaint);
            g.setFont (Fonts::ui (13.0f));
            g.drawText ("No tracks yet: create a bank in the Performance view, or add an audio track.", lanes, juce::Justification::centred);
        }
    }

    void paintBankClips (juce::Graphics& g, int b, juce::Rectangle<int> lane, juce::Colour col)
    {
        auto& s = Engine::get().session();
        const auto& bank = s.banks[(size_t) b];
        for (auto& c : s.song)
        {
            if (c.bank != b) continue;
            const float x0 = axis.toX (c.start, lane.getX()), x1 = axis.toX ((double) c.start + c.length, lane.getX());
            if (x1 < (float) lane.getX() || x0 > (float) lane.getRight()) continue;
            auto r = juce::Rectangle<float> (x0, (float) lane.getY() + 3.0f, x1 - x0, (float) lane.getHeight() - 6.0f);
            g.setColour (col.withAlpha (c.selected ? 0.95f : 0.7f));
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (col.darker (0.6f));
            g.fillRoundedRectangle (r.removeFromTop (14.0f), 3.0f);
            const bool validPat = juce::isPositiveAndBelow (c.pattern, (int) bank.patterns.size());
            g.setColour (Pal::text);
            g.setFont (Fonts::ui (10.0f, true));
            g.drawFittedText (validPat ? juce::String (c.pattern + 1) + " " + bank.patterns[(size_t) c.pattern].name : "?",
                              juce::Rectangle<float> (x0 + 4.0f, (float) lane.getY() + 3.0f, x1 - x0 - 6.0f, 14.0f).toNearestInt(), juce::Justification::centredLeft, 1);
            // mini notes
            if (validPat)
            {
                const auto& pat = bank.patterns[(size_t) c.pattern];
                const int L = juce::jmax (1, pat.lengthTicks());
                std::vector<int> rowsUsed;
                for (auto& n : pat.notes) { const int k = n.key >= 0 ? n.key : n.pad; if (std::find (rowsUsed.begin(), rowsUsed.end(), k) == rowsUsed.end()) rowsUsed.push_back (k); }
                std::sort (rowsUsed.begin(), rowsUsed.end());
                const float h = r.getHeight() - 2.0f;
                const float rh = rowsUsed.empty() ? 1.0f : juce::jmin (4.0f, h / (float) rowsUsed.size());
                g.setColour (Pal::bg.withAlpha (0.6f));
                for (int rep = c.offset; rep < c.offset + c.length; rep += L)
                {
                    const int base = (rep / L) * L;
                    for (auto& n : pat.notes)
                    {
                        const int abs = base + n.start - c.offset;
                        if (abs < 0 || abs >= c.length) continue;
                        const int k = n.key >= 0 ? n.key : n.pad;
                        const int ri = (int) (std::find (rowsUsed.begin(), rowsUsed.end(), k) - rowsUsed.begin());
                        const float nx = axis.toX ((double) c.start + abs, lane.getX());
                        const float nw = juce::jmax (1.5f, (float) (n.length * axis.pxPerTick));
                        g.fillRect (nx, r.getBottom() - 1.0f - (float) (ri + 1) * rh, nw, juce::jmax (1.0f, rh - 0.5f));
                    }
                    // pattern loop marks
                    if (rep > c.offset) { g.setColour (Pal::bg.withAlpha (0.3f)); g.fillRect (axis.toX ((double) c.start + base - c.offset, lane.getX()), r.getY(), 1.0f, r.getHeight()); g.setColour (Pal::bg.withAlpha (0.6f)); }
                }
            }
            if (c.selected) { g.setColour (juce::Colours::white); g.drawRoundedRectangle (juce::Rectangle<float> (x0, (float) lane.getY() + 3.0f, x1 - x0, (float) lane.getHeight() - 6.0f), 3.0f, 1.5f); }
        }
    }

    void paintAudioClips (juce::Graphics& g, int t, juce::Rectangle<int> lane, juce::Colour col)
    {
        auto& e = Engine::get();
        auto& tr = e.session().audioTracks[(size_t) t];
        const double ticksPerSecond = e.getBpm() / 60.0 * cr::ppq;
        for (auto& c : tr.clips)
        {
            if (! c.sample) continue;
            const double lenTicks = (double) c.length / c.sample->sampleRate * ticksPerSecond;
            const float x0 = axis.toX (c.start, lane.getX()), x1 = axis.toX (c.start + lenTicks, lane.getX());
            if (x1 < (float) lane.getX() || x0 > (float) lane.getRight()) continue;
            auto r = juce::Rectangle<float> (x0, (float) lane.getY() + 3.0f, x1 - x0, (float) lane.getHeight() - 6.0f);
            g.setColour (col.withAlpha (0.35f));
            g.fillRoundedRectangle (r, 3.0f);
            // waveform
            if (! c.sample->isStreamed())
            {
                const auto& buf = c.sample->buffer;
                const int w = juce::jmax (1, (int) r.getWidth());
                g.setColour (col);
                for (int x = 0; x < w; x += 2)
                {
                    const float sx = r.getX() + (float) x;
                    if (sx < (float) lane.getX() || sx > (float) lane.getRight()) continue;
                    const juce::int64 a = c.offset + c.length * x / w, b = juce::jmin ((juce::int64) buf.getNumSamples(), c.offset + c.length * (x + 2) / w);
                    if (b <= a) continue;
                    const float m = buf.getMagnitude (0, (int) a, (int) juce::jmin ((juce::int64) 4096, b - a));
                    const float h = m * (r.getHeight() - 16.0f) * 0.5f;
                    g.fillRect (sx, r.getCentreY() + 6.0f - h, 1.5f, juce::jmax (1.0f, h * 2.0f));
                }
            }
            g.setColour (Pal::text);
            g.setFont (Fonts::ui (10.0f, true));
            g.drawFittedText (c.sample->name, r.reduced (4.0f, 2.0f).toNearestInt(), juce::Justification::topLeft, 1);
            if (c.selected) { g.setColour (juce::Colours::white); g.drawRoundedRectangle (r, 3.0f, 1.5f); }
        }
    }

    // ---- hit testing ------------------------------------------------------------------------
    struct Hit { int row = -1; int clip = -1; bool audio = false; bool rightEdge = false; };
    Hit hitTest (juce::Point<int> p)
    {
        Hit h;
        h.row = rowAt (p.y);
        if (! juce::isPositiveAndBelow (h.row, (int) rows.size())) { h.row = -1; return h; }
        auto& e = Engine::get();
        const int key = rows[(size_t) h.row].key;
        const auto lane = laneArea();
        if (key < cr::numBanks)
        {
            for (int i = (int) e.session().song.size() - 1; i >= 0; --i)
            {
                auto& c = e.session().song[(size_t) i];
                if (c.bank != key) continue;
                const float x0 = axis.toX (c.start, lane.getX()), x1 = axis.toX ((double) c.start + c.length, lane.getX());
                if (p.x >= x0 && p.x <= x1) { h.clip = i; h.rightEdge = x1 - p.x < 8.0f; return h; }
            }
        }
        else
        {
            auto& tr = e.session().audioTracks[(size_t) (key - 1000)];
            const double tps = e.getBpm() / 60.0 * cr::ppq;
            for (int i = (int) tr.clips.size() - 1; i >= 0; --i)
            {
                auto& c = tr.clips[(size_t) i];
                if (! c.sample) continue;
                const float x0 = axis.toX (c.start, lane.getX()), x1 = axis.toX (c.start + (double) c.length / c.sample->sampleRate * tps, lane.getX());
                if (p.x >= x0 && p.x <= x1) { h.clip = i; h.audio = true; return h; }
            }
        }
        return h;
    }

    void clearSelection()
    {
        auto& s = Engine::get().session();
        for (auto& c : s.song) c.selected = false;
        for (auto& t : s.audioTracks) for (auto& c : t.clips) c.selected = false;
    }

    void mouseDown (const juce::MouseEvent& ev) override
    {
        auto& e = Engine::get();
        auto& s = e.session();
        const auto lane = laneArea();
        dragStart = ev.getPosition();
        drag = {};
        rubber = {};
        if (ev.y < rulerH && ev.x > headerW && ev.x < getWidth() - sideW)
        {
            const int t = juce::jmax (0, snapTo (axis.toTick ((float) ev.x, lane.getX()), grid == 0 ? 3 : grid, true));
            if (ev.y < 10 || ev.mods.isShiftDown()) { loopDrag = true; loopAnchor = t; s.loopStart = t; s.loopEnd = t + gridTicks (grid == 0 ? 1 : grid); }
            else e.setPosition (t);
            repaint();
            return;
        }
        const int row = rowAt (ev.y);
        if (! juce::isPositiveAndBelow (row, (int) rows.size())) { clearSelection(); repaint(); return; }
        const int key = rows[(size_t) row].key;
        if (ev.x < headerW)
        {
            selectedRow = row;
            auto& strip = key < cr::numBanks ? s.banks[(size_t) key].strip : s.audioTracks[(size_t) (key - 1000)].strip;
            if (ev.x > headerW - 74 && ev.x < headerW - 52) strip.mute = ! strip.mute;
            else if (ev.x >= headerW - 52 && ev.x < headerW - 28) strip.solo = ! strip.solo;
            else if (ev.x >= headerW - 28)
            {
                if (key < cr::numBanks) s.banks[(size_t) key].recordArm = ! s.banks[(size_t) key].recordArm;
                else s.audioTracks[(size_t) (key - 1000)].armed = ! s.audioTracks[(size_t) (key - 1000)].armed;
            }
            else if (ev.mods.isPopupMenu()) trackMenu (key);
            else if (key < cr::numBanks) e.selectedBank = key;
            if (onSelectionChanged) onSelectionChanged();
            repaint();
            return;
        }
        if (ev.x >= getWidth() - sideW)
        {
            if (key < cr::numBanks) e.playSong (key);
            else s.audioTracks[(size_t) (key - 1000)].source = s.audioTracks[(size_t) (key - 1000)].source == cr::TrackSource::song ? cr::TrackSource::stopped : cr::TrackSource::song;
            repaint();
            return;
        }
        selectedRow = row;
        if (key < cr::numBanks) e.selectedBank = key;
        auto h = hitTest (ev.getPosition());
        const int tick = juce::jmax (0, snapTo (axis.toTick ((float) ev.x, lane.getX()), grid, true));

        if (ev.mods.isPopupMenu()) { if (h.clip >= 0) { if (! (h.audio ? s.audioTracks[(size_t) (key - 1000)].clips[(size_t) h.clip].selected : s.song[(size_t) h.clip].selected)) { clearSelection(); select (h); } clipMenu(); } return; }

        if (tool == 2 && h.clip >= 0) { splitAt (h, juce::jmax (0, snapTo (axis.toTick ((float) ev.x, lane.getX()), grid))); return; }
        if (h.clip >= 0)
        {
            e.checkpoint();
            const bool already = h.audio ? s.audioTracks[(size_t) (key - 1000)].clips[(size_t) h.clip].selected : s.song[(size_t) h.clip].selected;
            if (! already && ! ev.mods.isShiftDown()) clearSelection();
            select (h);
            drag.active = true;
            drag.resize = h.rightEdge && ! h.audio;
            drag.anchorTick = tick;
            drag.startPositions.clear();
            for (auto& c : s.song) drag.startPositions.push_back ({ c.start, c.length });
            for (auto& t : s.audioTracks) for (auto& c : t.clips) drag.startPositions.push_back ({ c.start, (int) c.length });
            if (onSelectionChanged) onSelectionChanged();
        }
        else if (tool == 1 || ev.getNumberOfClicks() >= 2)
        {
            if (key < cr::numBanks) addClip (key, s.banks[(size_t) key].workingPattern, tick);
        }
        else
        {
            if (! ev.mods.isShiftDown()) clearSelection();
            rubberActive = true;
        }
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& ev) override
    {
        auto& e = Engine::get();
        auto& s = e.session();
        const auto lane = laneArea();
        if (loopDrag)
        {
            const int t = juce::jmax (0, snapTo (axis.toTick ((float) ev.x, lane.getX()), grid == 0 ? 3 : grid));
            s.loopStart = juce::jmin (loopAnchor, t);
            s.loopEnd = juce::jmax (loopAnchor + 1, t);
            s.loopOn = true;
            repaint();
            return;
        }
        if (rubberActive)
        {
            rubber = juce::Rectangle<int> (dragStart, ev.getPosition());
            const double t0 = axis.toTick ((float) rubber.getX(), lane.getX()), t1 = axis.toTick ((float) rubber.getRight(), lane.getX());
            const int r0 = rowAt (rubber.getY()), r1 = rowAt (rubber.getBottom());
            for (auto& c : s.song)
            {
                int ri = -1;
                for (int i = 0; i < (int) rows.size(); ++i) if (rows[(size_t) i].key == c.bank) ri = i;
                c.selected = ri >= r0 && ri <= r1 && c.start < t1 && c.start + c.length > t0;
            }
            repaint();
            return;
        }
        if (! drag.active) return;
        const int tick = snapTo (axis.toTick ((float) ev.x, lane.getX()), grid, false);
        int delta = tick - drag.anchorTick;
        if (gridTicks (grid) > 1) delta = (delta / gridTicks (grid)) * gridTicks (grid);
        const juce::ScopedLock sl (e.getLock());
        size_t idx = 0;
        for (auto& c : s.song)
        {
            const auto& sp = drag.startPositions[idx++];
            if (! c.selected) continue;
            if (drag.resize) c.length = juce::jmax (gridTicks (grid) > 1 ? gridTicks (grid) : cr::ppq / 4, sp.second + delta);
            else c.start = juce::jmax (0, sp.first + delta);
        }
        for (auto& t : s.audioTracks)
            for (auto& c : t.clips)
            {
                const auto& sp = drag.startPositions[idx++];
                if (c.selected && ! drag.resize) c.start = juce::jmax (0, sp.first + delta);
            }
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        drag.active = false;
        loopDrag = false;
        rubberActive = false;
        rubber = {};
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& ev) override
    {
        auto h = hitTest (ev.getPosition());
        if (h.clip >= 0 && ! h.audio)
        {
            auto& e = Engine::get();
            auto& c = e.session().song[(size_t) h.clip];
            e.selectedBank = c.bank;
            e.session().banks[(size_t) c.bank].workingPattern = c.pattern;
            if (onEditPattern) onEditPattern();
        }
    }

    void mouseWheelMove (const juce::MouseEvent& ev, const juce::MouseWheelDetails& w) override
    {
        if (ev.mods.isCommandDown() || ev.mods.isCtrlDown())
        {
            const double anchor = axis.toTick ((float) ev.x, laneArea().getX());
            axis.pxPerTick = juce::jlimit (0.005, 2.0, axis.pxPerTick * (w.deltaY > 0 ? 1.2 : 1.0 / 1.2));
            axis.startTick = juce::jmax (0.0, anchor - (ev.x - laneArea().getX()) / axis.pxPerTick);
        }
        else if (ev.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
            axis.startTick = juce::jmax (0.0, axis.startTick - (w.deltaX != 0.0f ? w.deltaX : w.deltaY) * 400.0 / axis.pxPerTick * 0.25);
        else scrollY = juce::jlimit (0, juce::jmax (0, (int) rows.size() * rowHeight - (getHeight() - rulerH)), scrollY - (int) (w.deltaY * 120.0f));
        repaint();
    }

    // pattern chips dragged from the toolbar: "pattern:<bank>:<index>"
    bool isInterestedInDragSource (const SourceDetails& d) override { return d.description.toString().startsWith ("pattern:") || isFileDrag (d); }
    void itemDropped (const SourceDetails& d) override
    {
        const auto desc = d.description.toString();
        const int tick = juce::jmax (0, snapTo (axis.toTick ((float) d.localPosition.x, laneArea().getX()), grid, true));
        if (desc.startsWith ("pattern:"))
        {
            auto parts = juce::StringArray::fromTokens (desc, ":", {});
            addClip (parts[1].getIntValue(), parts[2].getIntValue(), tick);
        }
        else dropAudio (fileFromDrag (d), d.localPosition.y, tick);
    }
    bool isInterestedInFileDrag (const juce::StringArray& f) override { for (auto& x : f) if (isAudioFile (juce::File (x))) return true; return false; }
    void filesDropped (const juce::StringArray& files, int x, int y) override
    {
        int tick = juce::jmax (0, snapTo (axis.toTick ((float) x, laneArea().getX()), grid, true));
        int count = 0;
        for (auto& f : files) if (isAudioFile (juce::File (f))) ++count;
        if (count > 1) { importStems (files, 0); return; }   // stems: one track each, lined up from bar 1
        for (auto& f : files) if (isAudioFile (juce::File (f))) dropAudio (juce::File (f), y, tick);
    }

    void addClip (int bank, int pattern, int tick)
    {
        auto& e = Engine::get();
        auto& b = e.session().banks[(size_t) bank];
        if (! b.created || ! juce::isPositiveAndBelow (pattern, (int) b.patterns.size())) return;
        e.checkpoint();
        const juce::ScopedLock sl (e.getLock());
        clearSelection();
        cr::SongClip c;
        c.bank = bank; c.pattern = pattern; c.start = tick; c.length = b.patterns[(size_t) pattern].lengthTicks();
        c.selected = true;
        e.session().song.push_back (c);
        repaint();
    }

    void dropAudio (const juce::File& f, int y, int tick)
    {
        auto& e = Engine::get();
        auto smp = e.loadSample (f, false);
        if (! smp) return;
        e.checkpoint();
        int t = -1;
        const int row = rowAt (y);
        if (juce::isPositiveAndBelow (row, (int) rows.size()) && rows[(size_t) row].key >= 1000) t = rows[(size_t) row].key - 1000;
        if (t < 0) { t = e.createAudioTrack(); if (t < 0) return; e.session().audioTracks[(size_t) t].name = smp->name; e.session().audioTracks[(size_t) t].strip.name = smp->name; }
        const juce::ScopedLock sl (e.getLock());
        cr::AudioClip c;
        c.sample = smp; c.start = tick; c.length = smp->numFrames;
        e.session().audioTracks[(size_t) t].clips.push_back (c);
        rebuildRows();
        repaint();
    }

    void select (const Hit& h)
    {
        auto& s = Engine::get().session();
        if (h.audio) s.audioTracks[(size_t) (rows[(size_t) h.row].key - 1000)].clips[(size_t) h.clip].selected = true;
        else s.song[(size_t) h.clip].selected = true;
    }

    void splitAt (const Hit& h, int tick)
    {
        auto& e = Engine::get();
        e.checkpoint();
        const juce::ScopedLock sl (e.getLock());
        if (! h.audio)
        {
            auto c = e.session().song[(size_t) h.clip];
            if (tick <= c.start || tick >= c.start + c.length) return;
            auto right = c;
            const int leftLen = tick - c.start;
            e.session().song[(size_t) h.clip].length = leftLen;
            right.start = tick;
            right.length = c.length - leftLen;
            right.offset = c.offset + leftLen;
            e.session().song.push_back (right);
        }
        else
        {
            auto& tr = e.session().audioTracks[(size_t) (rows[(size_t) h.row].key - 1000)];
            auto c = tr.clips[(size_t) h.clip];
            const double fps = c.sample->sampleRate / (e.getBpm() / 60.0 * cr::ppq);
            const juce::int64 cut = (juce::int64) ((tick - c.start) * fps);
            if (cut <= 0 || cut >= c.length) return;
            tr.clips[(size_t) h.clip].length = cut;
            c.start = tick; c.offset += cut; c.length -= cut;
            tr.clips.push_back (c);
        }
        repaint();
    }

    void clipMenu()
    {
        juce::PopupMenu m;
        m.addItem (1, "Duplicate");
        m.addItem (2, "Delete");
        m.addItem (3, "Set loop to selection");
        m.addItem (4, "Repeat to fill 4 more times");
        m.addItem (5, "Edit pattern");
        m.addItem (6, "Make pattern unique (copy)");
        if (firstSelectedAudioClip().first >= 0)
        {
            m.addSeparator();
            m.addSectionHeader ("Audio clip");
            m.addItem (20, "Convert to sample (next empty pad)");
            m.addItem (21, "Convert to sampler instrument (keys mode)");
            m.addItem (22, "Slice to pads");
            m.addItem (23, "Slice to pads + MIDI pattern");
            m.addItem (24, "Export as WAV...");
        }
        m.showMenuAsync (juce::PopupMenu::Options(), [this] (int r) { if (r >= 20) audioClipAction (r); else clipAction (r); });
    }

    std::pair<int, int> firstSelectedAudioClip() const
    {
        auto& s = Engine::get().session();
        for (int t = 0; t < cr::maxAudioTracks; ++t)
            if (s.audioTracks[(size_t) t].created)
                for (int c = 0; c < (int) s.audioTracks[(size_t) t].clips.size(); ++c)
                    if (s.audioTracks[(size_t) t].clips[(size_t) c].selected) return { t, c };
        return { -1, -1 };
    }

    // Beatmaker tools: turn an arrangement clip into pads.
    void audioClipAction (int r)
    {
        auto& e = Engine::get();
        const auto [t, c] = firstSelectedAudioClip();
        if (t < 0) return;
        if (r == 24)
        {
            auto name = juce::File::createLegalFileName (e.session().audioTracks[(size_t) t].name);
            chooser = std::make_unique<juce::FileChooser> ("Export the clip as WAV", Engine::getExportsFolder().getChildFile (name + ".wav"), "*.wav");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [t = t, c = c] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (f == juce::File()) return;
                juce::String err;
                if (Engine::get().exportClip (t, c, f.withFileExtension ("wav"), err)) Nav::setStatus ("Exported " + f.getFileName());
                else showError ("Export", err);
            });
            return;
        }
        auto smp = e.clipToSample (t, c);
        if (! smp) { showError ("Convert", "That clip has no audio."); return; }
        const int bank = e.selectedBank;
        const int pad = e.firstEmptyPad (bank, e.selectedPad);
        if (pad < 0) { showError ("Convert", "There are no empty pads left in " + cr::bankName (bank) + "."); return; }
        e.checkpointPad (bank, pad);
        if (! e.setPadSample (bank, pad, smp)) return;
        auto& p = e.session().banks[(size_t) bank].pads[(size_t) pad];
        p.name = e.session().audioTracks[(size_t) t].name;
        if (r == 21) p.keysMode = true;
        e.selectedPad = pad;
        juce::String msg = "Loaded onto " + cr::bankLetter (bank) + "." + juce::String (pad + 1);
        if (r == 22 || r == 23)
        {
            auto points = e.autoSlice (bank, pad, 0, 0, 2, 0, 0.5f);
            const int dest = e.firstEmptyPad (bank, pad + 1);
            juce::String err;
            if (points.size() < 2 || dest < 0) msg << " (no room or no hits to slice)";
            else if (e.applySlices (bank, pad, 0, 0, points, Engine::SliceTarget::pads, bank, dest, 0, r == 23, 0.0f, 5.0f, err))
                msg << ", " << (int) points.size() << " slices from " << cr::bankLetter (bank) << "." << (dest + 1) << (r == 23 ? " with a pattern" : "");
            else msg << " (" << err << ")";
        }
        Nav::setStatus (msg);
        ViewBase::broadcast();
    }

public:
    void clipAction (int r)
    {
        auto& e = Engine::get();
        auto& s = e.session();
        if (r == 0) return;
        if (r == 5) { for (auto& c : s.song) if (c.selected) { e.selectedBank = c.bank; s.banks[(size_t) c.bank].workingPattern = c.pattern; if (onEditPattern) onEditPattern(); return; } return; }
        e.checkpoint();
        const juce::ScopedLock sl (e.getLock());
        int selStart = std::numeric_limits<int>::max(), selEnd = 0;
        for (auto& c : s.song) if (c.selected) { selStart = juce::jmin (selStart, c.start); selEnd = juce::jmax (selEnd, c.start + c.length); }
        const double tps = e.getBpm() / 60.0 * cr::ppq;
        for (auto& t : s.audioTracks) for (auto& c : t.clips) if (c.selected && c.sample) { selStart = juce::jmin (selStart, c.start); selEnd = juce::jmax (selEnd, c.start + (int) ((double) c.length / c.sample->sampleRate * tps)); }
        if (selStart == std::numeric_limits<int>::max()) return;
        const int span = selEnd - selStart;
        if (r == 1 || r == 4)
        {
            const int times = r == 1 ? 1 : 4;
            std::vector<cr::SongClip> add;
            for (auto& c : s.song) if (c.selected) for (int k = 1; k <= times; ++k) { auto n = c; n.start += span * k; n.selected = (k == times); add.push_back (n); }
            for (auto& c : s.song) c.selected = false;
            for (auto& c : add) s.song.push_back (c);
            for (auto& t : s.audioTracks)
            {
                std::vector<cr::AudioClip> addA;
                for (auto& c : t.clips) if (c.selected) for (int k = 1; k <= times; ++k) { auto n = c; n.start += span * k; addA.push_back (n); }
                for (auto& c : t.clips) c.selected = false;
                for (auto& c : addA) t.clips.push_back (c);
            }
        }
        else if (r == 2)
        {
            s.song.erase (std::remove_if (s.song.begin(), s.song.end(), [] (const cr::SongClip& c) { return c.selected; }), s.song.end());
            for (auto& t : s.audioTracks) t.clips.erase (std::remove_if (t.clips.begin(), t.clips.end(), [] (const cr::AudioClip& c) { return c.selected; }), t.clips.end());
        }
        else if (r == 3) { s.loopStart = selStart; s.loopEnd = selEnd; s.loopOn = true; }
        else if (r == 6)
        {
            for (auto& c : s.song)
                if (c.selected)
                {
                    auto& b = s.banks[(size_t) c.bank];
                    auto copy = b.patterns[(size_t) c.pattern];
                    copy.name << " copy";
                    b.patterns.push_back (copy);
                    c.pattern = (int) b.patterns.size() - 1;
                }
        }
        repaint();
        ViewBase::broadcast();
    }

private:
    void trackMenu (int key)
    {
        auto& e = Engine::get();
        juce::PopupMenu m;
        m.addItem (1, "Rename...");
        if (key < cr::numBanks) { m.addItem (2, "Play song on this track"); m.addItem (3, "Stop this track"); }
        else
        {
            m.addItem (4, "Delete audio track");
            auto& tr = e.session().audioTracks[(size_t) (key - 1000)];
            m.addItem (5, "Monitor input", true, tr.monitor);
            m.addItem (6, "Noise gate", true, tr.gate);
        }
        addResampleItems (m, key);
        m.showMenuAsync (juce::PopupMenu::Options(), [this, key] (int r)
        {
            auto& en = Engine::get();
            auto& s = en.session();
            if (r == 1)
                askForText ("Track name", key < cr::numBanks ? s.banks[(size_t) key].name : s.audioTracks[(size_t) (key - 1000)].name, [key] (const juce::String& n)
                {
                    auto& ss = Engine::get().session();
                    if (key < cr::numBanks) { ss.banks[(size_t) key].name = n; ss.banks[(size_t) key].strip.name = n; }
                    else { ss.audioTracks[(size_t) (key - 1000)].name = n; ss.audioTracks[(size_t) (key - 1000)].strip.name = n; }
                    ViewBase::broadcast();
                });
            else if (r == 2) en.playSong (key);
            else if (r == 3) en.stopTrack (key);
            else if (r == 4) { en.checkpoint(); en.deleteAudioTrack (key - 1000); rebuildRows(); }
            else if (r == 5) s.audioTracks[(size_t) (key - 1000)].monitor = ! s.audioTracks[(size_t) (key - 1000)].monitor;
            else if (r == 6) s.audioTracks[(size_t) (key - 1000)].gate = ! s.audioTracks[(size_t) (key - 1000)].gate;
            else if (handleResampleItem (r, key)) {}
            ViewBase::broadcast();
        });
    }

    std::unique_ptr<juce::FileChooser> chooser;
    struct Drag { bool active = false, resize = false; int anchorTick = 0; std::vector<std::pair<int, int>> startPositions; } drag;
    juce::Point<int> dragStart;
    juce::Rectangle<int> rubber;
    bool rubberActive = false, loopDrag = false;
    int loopAnchor = 0;
};

// ============================================================================
// SCENES
// ============================================================================
class ScenesGrid : public juce::Component
{
public:
    int selected = 0;
    int scrollX = 0;
    static constexpr int headerW = 220, colW = 150, rowH = 40, topH = 46;
    std::vector<int> banks;

    void rebuild()
    {
        banks.clear();
        for (int b = 0; b < cr::numBanks; ++b) if (Engine::get().session().banks[(size_t) b].created) banks.push_back (b);
    }

    void paint (juce::Graphics& g) override
    {
        auto& e = Engine::get();
        auto& s = e.session();
        g.fillAll (Pal::bg);
        // headers
        for (int i = 0; i <= (int) s.scenes.size(); ++i)
        {
            auto r = juce::Rectangle<int> (headerW + i * colW - scrollX, 0, colW, topH).reduced (2, 4);
            if (r.getRight() < headerW || r.getX() > getWidth()) continue;
            if (i == (int) s.scenes.size())
            {
                g.setColour (Pal::surface);
                g.fillRoundedRectangle (r.toFloat(), 4.0f);
                drawCaps (g, "+ New scene", r, juce::Justification::centred, Pal::textDim, 10.0f);
                continue;
            }
            const bool sel = i == selected;
            g.setColour (sel ? Pal::accent : Pal::surface2);
            g.fillRoundedRectangle (r.toFloat(), 4.0f);
            g.setColour (sel ? Pal::bg : Pal::text);
            g.setFont (Fonts::ui (12.5f, true));
            g.drawFittedText (s.scenes[(size_t) i].name, r.reduced (10, 0).withTrimmedRight (20), juce::Justification::centredLeft, 1);
            drawIcon (g, Icon::play, r.removeFromRight (26).toFloat().reduced (5.0f, 10.0f), sel ? Pal::bg : Pal::text);
        }
        // rows
        for (int ri = 0; ri < (int) banks.size(); ++ri)
        {
            const int b = banks[(size_t) ri];
            const auto& bank = s.banks[(size_t) b];
            const int y = topH + ri * rowH;
            auto hdr = juce::Rectangle<int> (0, y, headerW, rowH).reduced (0, 1);
            g.setColour (Pal::panel);
            g.fillRect (hdr);
            drawBankBadge (g, hdr.reduced (8, 0).removeFromLeft (24).withSizeKeepingCentre (24, 24).toFloat(), b, true);
            g.setColour (Pal::text);
            g.setFont (Fonts::ui (12.5f, true));
            g.drawFittedText (bank.name, hdr.withTrimmedLeft (42).withTrimmedRight (34), juce::Justification::centredLeft, 1);
            // stop button
            drawIcon (g, Icon::stopSquare, hdr.removeFromRight (30).toFloat().reduced (8.0f, 11.0f), bank.source == cr::TrackSource::stopped ? Pal::textFaint : Pal::textDim);

            for (int i = 0; i < (int) s.scenes.size(); ++i)
            {
                auto r = juce::Rectangle<int> (headerW + i * colW - scrollX, y, colW, rowH).reduced (2, 2);
                if (r.getRight() < headerW || r.getX() > getWidth()) continue;
                const int slot = s.scenes[(size_t) i].slots[(size_t) b];
                const bool playing = bank.source == cr::TrackSource::pattern && slot == bank.playingPattern;
                if (slot >= 0 && slot < (int) bank.patterns.size())
                {
                    g.setColour (bank.colour.withAlpha (playing ? 0.95f : 0.55f));
                    g.fillRoundedRectangle (r.toFloat(), 3.0f);
                    g.setColour (playing ? Pal::bg : Pal::text);
                    g.setFont (Fonts::ui (11.0f, true));
                    g.drawFittedText (juce::String (slot + 1) + " " + bank.patterns[(size_t) slot].name, r.reduced (8, 0), juce::Justification::centredLeft, 1);
                }
                else
                {
                    g.setColour (Pal::surface);
                    g.fillRoundedRectangle (r.toFloat(), 3.0f);
                    drawCaps (g, slot == -2 ? "Song" : "Stop", r.reduced (8, 0), juce::Justification::centredLeft, Pal::textFaint, 9.5f);
                }
                if (i == selected) { g.setColour (Pal::accent.withAlpha (0.5f)); g.drawRoundedRectangle (r.toFloat(), 3.0f, 1.0f); }
            }
        }
        if (banks.empty())
        {
            g.setColour (Pal::textFaint);
            g.setFont (Fonts::ui (13.0f));
            g.drawText ("Create banks in the Performance view, then capture scenes here.", getLocalBounds(), juce::Justification::centred);
        }
    }

    void mouseDown (const juce::MouseEvent& ev) override
    {
        auto& e = Engine::get();
        auto& s = e.session();
        const int col = (ev.x - headerW + scrollX) / colW;
        if (ev.y < topH)
        {
            if (ev.x < headerW) return;
            if (col == (int) s.scenes.size()) { e.captureScene (col); selected = col; }
            else if (juce::isPositiveAndBelow (col, (int) s.scenes.size()))
            {
                selected = col;
                if (ev.mods.isPopupMenu()) { if (onSceneMenu) onSceneMenu(); }
                else if (ev.x > headerW + (col + 1) * colW - scrollX - 30 || ev.getNumberOfClicks() > 1) e.launchScene (col);
            }
            repaint();
            ViewBase::broadcast();
            return;
        }
        const int ri = (ev.y - topH) / rowH;
        if (! juce::isPositiveAndBelow (ri, (int) banks.size())) return;
        const int b = banks[(size_t) ri];
        if (ev.x < headerW)
        {
            if (ev.x > headerW - 30) e.stopTrack (b);
            else e.selectedBank = b;
            repaint();
            return;
        }
        if (! juce::isPositiveAndBelow (col, (int) s.scenes.size())) return;
        selected = col;
        // choose the slot content
        auto& bank = s.banks[(size_t) b];
        juce::PopupMenu m;
        m.addItem (1, "Stop", true, s.scenes[(size_t) col].slots[(size_t) b] == -1);
        m.addItem (2, "Play song", true, s.scenes[(size_t) col].slots[(size_t) b] == -2);
        m.addSeparator();
        for (int p = 0; p < (int) bank.patterns.size(); ++p) m.addItem (100 + p, juce::String (p + 1) + "  " + bank.patterns[(size_t) p].name, true, s.scenes[(size_t) col].slots[(size_t) b] == p);
        m.addSeparator();
        m.addItem (3, "Launch this pattern now");
        m.showMenuAsync (juce::PopupMenu::Options(), [this, b, col] (int r)
        {
            auto& en = Engine::get();
            auto& sc = en.session().scenes;
            if (! juce::isPositiveAndBelow (col, (int) sc.size()) || r == 0) return;
            if (r == 3) { const int p = sc[(size_t) col].slots[(size_t) b]; if (p >= 0) en.launchPattern (b, p); return; }
            en.checkpoint();
            sc[(size_t) col].slots[(size_t) b] = r == 1 ? -1 : (r == 2 ? -2 : r - 100);
            repaint();
        });
        repaint();
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        const int maxScroll = juce::jmax (0, ((int) Engine::get().session().scenes.size() + 1) * colW - (getWidth() - headerW));
        scrollX = juce::jlimit (0, maxScroll, scrollX - (int) ((w.deltaX != 0.0f ? w.deltaX : w.deltaY) * 200.0f));
        repaint();
    }

    std::function<void()> onSceneMenu;
};

// ============================================================================
// PATTERN editor
// ============================================================================
class PianoRoll : public juce::Component
{
public:
    TimeAxis axis;
    int grid = 5;
    int tool = 1;       // 0 select, 1 draw, 2 paint (draw while dragging)
    int rowH = 22;
    int scrollY = 0;
    bool showAllPads = false;
    bool keysView = false;   // rows are keys of the selected pad
    bool forcePads = false;  // show the pad rows even when the selected pad is in keys mode
    std::vector<int> rowsList; // pad indices (pads view) or keys (top to bottom)
    static constexpr int headerW = 150, rulerH = 26, velH = 90;

    cr::Bank& bank() { return currentBank(); }
    cr::Pattern* pattern()
    {
        auto& b = bank();
        if (! b.created || b.patterns.empty()) return nullptr;
        return &b.patterns[(size_t) juce::jlimit (0, (int) b.patterns.size() - 1, b.workingPattern)];
    }

    void rebuildRows()
    {
        rowsList.clear();
        auto& b = bank();
        auto* pat = pattern();
        const int sp = Engine::get().selectedPad;
        keysView = b.created && b.pads[(size_t) sp].keysMode && ! forcePads;
        if (keysView)
        {
            for (int k = 127; k >= 0; --k) rowsList.push_back (k);
            // scroll to notes or C3
            int top = 72;
            if (pat) for (auto& n : pat->notes) if (n.pad == sp && n.key >= 0) top = juce::jmax (top, n.key + 4);
            if (scrollY == 0) scrollY = juce::jmax (0, (127 - top) * rowH);
        }
        else
        {
            std::vector<bool> used (cr::numPads, false);
            for (int p = 0; p < cr::numPads; ++p) if (showAllPads ? p < 16 || b.pads[(size_t) p].hasSound() : b.pads[(size_t) p].hasSound()) used[(size_t) p] = true;
            if (pat) for (auto& n : pat->notes) used[(size_t) n.pad] = true;
            for (int p = cr::numPads - 1; p >= 0; --p) if (used[(size_t) p]) rowsList.push_back (p);
            if (rowsList.empty()) for (int p = 15; p >= 0; --p) rowsList.push_back (p);
        }
    }

    juce::Rectangle<int> gridArea() const { return { headerW, rulerH, getWidth() - headerW, getHeight() - rulerH - velH }; }
    int rowOf (const cr::Note& n) const
    {
        const int k = keysView ? n.key : n.pad;
        for (int i = 0; i < (int) rowsList.size(); ++i) if (rowsList[(size_t) i] == k) return i;
        return -1;
    }
    bool noteVisible (const cr::Note& n) const
    {
        if (keysView) return n.pad == Engine::get().selectedPad && n.key >= 0;
        return true;
    }

    void paint (juce::Graphics& g) override
    {
        auto& e = Engine::get();
        g.fillAll (Pal::bg);
        auto* pat = pattern();
        auto ga = gridArea();
        const int L = pat ? pat->lengthTicks() : cr::barTicks();
        const double pp = e.getTrackPatternPosition (e.selectedBank);
        drawRuler (g, { headerW, 0, ga.getWidth(), rulerH }, axis, L, false, (e.isPlaying() && pp >= 0.0 && bank().playingPattern == bank().workingPattern) ? pp : -1.0);

        g.saveState();
        g.reduceClipRegion (ga.withX (0).withWidth (getWidth()));
        // rows
        for (int i = 0; i < (int) rowsList.size(); ++i)
        {
            const int y = ga.getY() + i * rowH - scrollY;
            if (y + rowH < ga.getY() || y > ga.getBottom()) continue;
            const int k = rowsList[(size_t) i];
            const bool black = keysView && juce::MidiMessage::isMidiNoteBlack (k);
            g.setColour (black ? Pal::panel : Pal::bg.brighter (0.02f));
            g.fillRect (ga.getX(), y, ga.getWidth(), rowH);
            g.setColour (Pal::outline.withAlpha (0.5f));
            g.fillRect (ga.getX(), y + rowH - 1, ga.getWidth(), 1);
            // header
            auto hdr = juce::Rectangle<int> (0, y, headerW, rowH);
            if (keysView)
            {
                g.setColour (black ? juce::Colour (0xff1c1d21) : juce::Colour (0xffe9eaec));
                g.fillRect (hdr.reduced (0, 0).withTrimmedRight (1));
                g.setColour (black ? Pal::textDim : Pal::bg);
                g.setFont (Fonts::ui (10.0f, k % 12 == 0));
                g.drawText (noteName (k), hdr.reduced (8, 0), juce::Justification::centredRight);
                if (! black) { g.setColour (juce::Colour (0xffb0b2b6)); g.fillRect (hdr.getX(), hdr.getBottom() - 1, hdr.getWidth(), 1); }
            }
            else
            {
                const auto& pad = bank().pads[(size_t) k];
                g.setColour (k == e.selectedPad ? Pal::surface2 : Pal::panel);
                g.fillRect (hdr.withTrimmedBottom (1));
                g.setColour (pad.hasSound() ? padColour (e.selectedBank, k) : Pal::surface3);
                g.fillRect (hdr.getX() + 4, hdr.getY() + 4, 4, rowH - 9);
                g.setColour (pad.hasSound() ? Pal::text : Pal::textFaint);
                g.setFont (Fonts::ui (11.0f, true));
                g.drawFittedText (juce::String (k + 1) + "  " + (pad.name.isNotEmpty() ? pad.name : juce::String ("-")), hdr.reduced (14, 0), juce::Justification::centredLeft, 1);
            }
        }
        // grid lines + out-of-pattern shade
        drawGrid (g, ga, axis, grid);
        const float endX = axis.toX (L, ga.getX());
        if (endX < (float) ga.getRight())
        {
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.fillRect (juce::Rectangle<float> (endX, (float) ga.getY(), (float) ga.getRight() - endX, (float) ga.getHeight()));
        }
        // notes
        if (pat)
        {
            for (auto& n : pat->notes)
            {
                if (! noteVisible (n)) continue;
                const int ri = rowOf (n);
                if (ri < 0) continue;
                const int y = ga.getY() + ri * rowH - scrollY;
                const float x0 = axis.toX (n.start, ga.getX()), x1 = axis.toX ((double) n.start + n.length, ga.getX());
                if (x1 < (float) ga.getX() || x0 > (float) ga.getRight()) continue;
                const auto col = padColour (e.selectedBank, keysView ? e.selectedPad.load() : n.pad);
                auto r = juce::Rectangle<float> (juce::jmax (x0, (float) ga.getX()), (float) y + 2.0f, juce::jmax (3.0f, x1 - juce::jmax (x0, (float) ga.getX()) - 1.0f), (float) rowH - 4.0f);
                g.setColour (col.withAlpha (0.35f + 0.65f * n.velocity));
                g.fillRoundedRectangle (r, 2.5f);
                if (n.selected) { g.setColour (juce::Colours::white); g.drawRoundedRectangle (r, 2.5f, 1.5f); }
            }
        }
        if (rubber.getWidth() > 1) { g.setColour (Pal::text.withAlpha (0.12f)); g.fillRect (rubber); g.setColour (Pal::text.withAlpha (0.5f)); g.drawRect (rubber); }
        // playhead
        if (e.isPlaying() && pp >= 0.0 && bank().playingPattern == bank().workingPattern)
        {
            g.setColour (Pal::text);
            g.fillRect (axis.toX (pp, ga.getX()), (float) ga.getY(), 1.0f, (float) ga.getHeight());
        }
        g.restoreState();

        // velocity lane
        auto vl = juce::Rectangle<int> (headerW, getHeight() - velH, getWidth() - headerW, velH);
        g.setColour (Pal::panel);
        g.fillRect (vl.withX (0).withWidth (getWidth()));
        g.setColour (Pal::outline);
        g.fillRect (0, vl.getY(), getWidth(), 1);
        drawCaps (g, "Velocity", juce::Rectangle<int> (10, vl.getY() + 6, headerW - 20, 16), juce::Justification::centredLeft, Pal::textFaint, 9.5f);
        if (pat)
        {
            g.saveState();
            g.reduceClipRegion (vl);
            for (auto& n : pat->notes)
            {
                if (! noteVisible (n) || rowOf (n) < 0) continue;
                const float x = axis.toX (n.start, vl.getX());
                const float h = n.velocity * (float) (velH - 12);
                const auto col = padColour (e.selectedBank, keysView ? e.selectedPad.load() : n.pad);
                g.setColour (n.selected ? juce::Colours::white : col);
                g.fillRect (x, (float) vl.getBottom() - h - 4.0f, 3.0f, h);
                g.fillEllipse (x - 2.0f, (float) vl.getBottom() - h - 7.0f, 7.0f, 7.0f);
            }
            g.restoreState();
        }
    }

    // ---- editing ----------------------------------------------------------------------------
    int hitNote (juce::Point<int> p, bool& onEdge)
    {
        auto* pat = pattern();
        onEdge = false;
        if (pat == nullptr) return -1;
        auto ga = gridArea();
        for (int i = (int) pat->notes.size() - 1; i >= 0; --i)
        {
            auto& n = pat->notes[(size_t) i];
            if (! noteVisible (n)) continue;
            const int ri = rowOf (n);
            if (ri < 0) continue;
            const int y = ga.getY() + ri * rowH - scrollY;
            const float x0 = axis.toX (n.start, ga.getX()), x1 = axis.toX ((double) n.start + n.length, ga.getX());
            if (p.y >= y && p.y < y + rowH && p.x >= x0 - 1 && p.x <= x1 + 1) { onEdge = x1 - p.x < 6.0f; return i; }
        }
        return -1;
    }

    void mouseDown (const juce::MouseEvent& ev) override
    {
        auto& e = Engine::get();
        auto* pat = pattern();
        if (pat == nullptr) return;
        auto ga = gridArea();
        dragStart = ev.getPosition();
        mode = none;
        if (ev.y >= getHeight() - velH && ev.x > headerW) { e.checkpoint(); mode = velocity; setVelocityAt (ev.getPosition()); return; }
        if (ev.y < rulerH) { if (ev.x > headerW) e.setPosition (e.getPositionTicks()); return; }
        const int ri = (ev.y - ga.getY() + scrollY) / rowH;
        if (ev.x < headerW)
        {
            if (juce::isPositiveAndBelow (ri, (int) rowsList.size()))
            {
                const int k = rowsList[(size_t) ri];
                if (keysView) { e.padDown (e.selectedBank, e.selectedPad, 0.85f, k); auditionKey = k; }
                else { e.selectedPad = k; e.padDown (e.selectedBank, k, 0.85f); auditionPad = k; }
                repaint();
            }
            return;
        }
        bool edge = false;
        const int hit = hitNote (ev.getPosition(), edge);
        if (ev.mods.isPopupMenu())
        {
            if (hit >= 0) { e.checkpoint(); const juce::ScopedLock sl (e.getLock()); pat->notes.erase (pat->notes.begin() + hit); }
            else noteMenu();
            repaint();
            return;
        }
        e.checkpoint();
        if (hit >= 0)
        {
            auto& n = pat->notes[(size_t) hit];
            if (! n.selected && ! ev.mods.isShiftDown()) for (auto& m : pat->notes) m.selected = false;
            n.selected = true;
            mode = edge ? resize : move;
            snapshot = pat->notes;
            anchorTick = axis.toTick ((float) ev.x, ga.getX());
            anchorRow = ri;
            if (! edge)
            {
                if (keysView) e.padDown (e.selectedBank, e.selectedPad, n.velocity, n.key);
                else e.padDown (e.selectedBank, n.pad, n.velocity);
                auditionKey = keysView ? n.key : -1;
                auditionPad = keysView ? -1 : n.pad;
            }
        }
        else if (tool >= 1 && juce::isPositiveAndBelow (ri, (int) rowsList.size()))
        {
            for (auto& m : pat->notes) m.selected = false;
            addNoteAt (ev.getPosition());
            mode = tool == 2 ? painting : resize;
            snapshot = pat->notes;
            anchorTick = axis.toTick ((float) ev.x, ga.getX());
        }
        else
        {
            if (! ev.mods.isShiftDown()) for (auto& m : pat->notes) m.selected = false;
            mode = rubberBand;
        }
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& ev) override
    {
        auto& e = Engine::get();
        auto* pat = pattern();
        if (pat == nullptr) return;
        auto ga = gridArea();
        if (mode == velocity) { setVelocityAt (ev.getPosition()); return; }
        if (mode == rubberBand)
        {
            rubber = juce::Rectangle<int> (dragStart, ev.getPosition());
            const double t0 = axis.toTick ((float) rubber.getX(), ga.getX()), t1 = axis.toTick ((float) rubber.getRight(), ga.getX());
            for (auto& n : pat->notes)
            {
                if (! noteVisible (n)) continue;
                const int y = ga.getY() + rowOf (n) * rowH - scrollY;
                n.selected = n.start < t1 && n.start + n.length > t0 && y + rowH > rubber.getY() && y < rubber.getBottom();
            }
            repaint();
            return;
        }
        if (mode == painting) { addNoteAt (ev.getPosition(), true); repaint(); return; }
        if (mode != move && mode != resize) return;
        const double tick = axis.toTick ((float) ev.x, ga.getX());
        const int g = juce::jmax (1, gridTicks (grid));
        int dt = (int) std::round ((tick - anchorTick) / g) * g;
        if (ev.mods.isAltDown()) dt = (int) std::round (tick - anchorTick);
        const int ri = (ev.y - ga.getY() + scrollY) / rowH;
        const int dr = ri - anchorRow;
        const juce::ScopedLock sl (e.getLock());
        const int L = pat->lengthTicks();
        for (size_t i = 0; i < pat->notes.size() && i < snapshot.size(); ++i)
        {
            auto& n = pat->notes[i];
            if (! n.selected) continue;
            const auto& o = snapshot[i];
            if (mode == move)
            {
                n.start = juce::jlimit (0, L - 1, o.start + dt);
                const int orow = rowOf (o);
                const int nrow = juce::jlimit (0, (int) rowsList.size() - 1, orow + dr);
                if (orow >= 0)
                {
                    if (keysView) n.key = rowsList[(size_t) nrow];
                    else n.pad = rowsList[(size_t) nrow];
                }
            }
            else n.length = juce::jmax (juce::jmin (g, cr::ppq / 8), o.length + dt);
        }
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        auto& e = Engine::get();
        if (auditionPad >= 0) e.padUp (e.selectedBank, auditionPad);
        if (auditionKey >= 0) e.padUp (e.selectedBank, e.selectedPad, auditionKey);
        auditionPad = auditionKey = -1;
        mode = none;
        rubber = {};
        repaint();
    }

    void mouseWheelMove (const juce::MouseEvent& ev, const juce::MouseWheelDetails& w) override
    {
        auto ga = gridArea();
        if (ev.mods.isCommandDown() || ev.mods.isCtrlDown())
        {
            const double anchor = axis.toTick ((float) ev.x, ga.getX());
            axis.pxPerTick = juce::jlimit (0.02, 4.0, axis.pxPerTick * (w.deltaY > 0 ? 1.2 : 1.0 / 1.2));
            axis.startTick = juce::jmax (0.0, anchor - (ev.x - ga.getX()) / axis.pxPerTick);
        }
        else if (ev.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
            axis.startTick = juce::jmax (0.0, axis.startTick - (w.deltaX != 0.0f ? w.deltaX : w.deltaY) * 60.0 / axis.pxPerTick);
        else scrollY = juce::jlimit (0, juce::jmax (0, (int) rowsList.size() * rowH - ga.getHeight()), scrollY - (int) (w.deltaY * 120.0f));
        repaint();
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        auto* pat = pattern();
        if (pat == nullptr) return false;
        if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) { deleteSelected(); return true; }
        if (k == juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0)) { for (auto& n : pat->notes) n.selected = noteVisible (n); repaint(); return true; }
        if (k == juce::KeyPress ('c', juce::ModifierKeys::commandModifier, 0)) { copySelected(); return true; }
        if (k == juce::KeyPress ('v', juce::ModifierKeys::commandModifier, 0)) { paste(); return true; }
        return false;
    }

    void deleteSelected()
    {
        auto& e = Engine::get();
        auto* pat = pattern();
        if (pat == nullptr) return;
        e.checkpoint();
        const juce::ScopedLock sl (e.getLock());
        pat->notes.erase (std::remove_if (pat->notes.begin(), pat->notes.end(), [] (const cr::Note& n) { return n.selected; }), pat->notes.end());
        repaint();
    }

    void quantizeSelected (int q, float strength)
    {
        auto& e = Engine::get();
        auto* pat = pattern();
        if (pat == nullptr) return;
        e.checkpoint();
        const juce::ScopedLock sl (e.getLock());
        bool any = false;
        for (auto& n : pat->notes) if (n.selected) any = true;
        const int g = cr::quantizeTicks (q);
        for (auto& n : pat->notes)
        {
            if (any && ! n.selected) continue;
            int target = (int) std::round ((double) n.start / g) * g;
            if (((target / g) % 2) == 1) target += (int) std::round (e.session().swing * g);
            n.start = juce::jlimit (0, pat->lengthTicks() - 1, n.start + (int) std::round ((target - n.start) * strength));
        }
        repaint();
    }

    void copySelected()
    {
        auto* pat = pattern();
        clipboard.clear();
        if (pat == nullptr) return;
        int first = std::numeric_limits<int>::max();
        for (auto& n : pat->notes) if (n.selected) first = juce::jmin (first, n.start);
        for (auto& n : pat->notes) if (n.selected) { auto c = n; c.start -= first; clipboard.push_back (c); }
    }

    void paste()
    {
        auto& e = Engine::get();
        auto* pat = pattern();
        if (pat == nullptr || clipboard.empty()) return;
        e.checkpoint();
        const juce::ScopedLock sl (e.getLock());
        int at = 0;
        for (auto& n : pat->notes) if (n.selected) at = juce::jmax (at, n.start + n.length);
        at = snapTo (at, grid);
        for (auto& n : pat->notes) n.selected = false;
        for (auto c : clipboard) { c.start += at; c.selected = true; if (c.start < pat->lengthTicks()) pat->notes.push_back (c); }
        repaint();
    }

    std::vector<cr::Note> clipboard;

private:
    void addNoteAt (juce::Point<int> p, bool painting = false)
    {
        auto& e = Engine::get();
        auto* pat = pattern();
        auto ga = gridArea();
        const int ri = (p.y - ga.getY() + scrollY) / rowH;
        if (! juce::isPositiveAndBelow (ri, (int) rowsList.size())) return;
        const int g = juce::jmax (cr::ppq / 16, gridTicks (grid));
        const int t = (int) std::floor (axis.toTick ((float) p.x, ga.getX()) / g) * g;
        if (t < 0 || t >= pat->lengthTicks()) return;
        cr::Note n;
        n.pad = keysView ? e.selectedPad.load() : rowsList[(size_t) ri];
        n.key = keysView ? rowsList[(size_t) ri] : -1;
        n.start = t;
        n.length = g;
        n.velocity = e.fixedVelocity.load();
        n.selected = ! painting;
        const juce::ScopedLock sl (e.getLock());
        for (auto& o : pat->notes) if (o.pad == n.pad && o.key == n.key && o.start == n.start) return;
        pat->notes.push_back (n);
        if (! painting)
        {
            if (keysView) { e.padDown (e.selectedBank, e.selectedPad, n.velocity, n.key); auditionKey = n.key; }
            else { e.padDown (e.selectedBank, n.pad, n.velocity); auditionPad = n.pad; }
        }
    }

    void setVelocityAt (juce::Point<int> p)
    {
        auto& e = Engine::get();
        auto* pat = pattern();
        if (pat == nullptr) return;
        const float v = juce::jlimit (0.02f, 1.0f, (float) (getHeight() - 4 - p.y) / (float) (velH - 12));
        auto ga = gridArea();
        bool anySel = false;
        for (auto& n : pat->notes) if (n.selected) anySel = true;
        const juce::ScopedLock sl (e.getLock());
        for (auto& n : pat->notes)
        {
            if (! noteVisible (n) || rowOf (n) < 0) continue;
            const float x = axis.toX (n.start, ga.getX());
            if (std::abs (x - (float) p.x) < 5.0f && (! anySel || n.selected || true)) n.velocity = v;
        }
        repaint();
    }

    void noteMenu()
    {
        juce::PopupMenu m;
        m.addItem (1, "Select all");
        m.addItem (2, "Delete selected");
        m.addItem (3, "Copy selected");
        m.addItem (4, "Paste", ! clipboard.empty());
        m.showMenuAsync (juce::PopupMenu::Options(), [this] (int r)
        {
            auto* pat = pattern();
            if (pat == nullptr) return;
            if (r == 1) for (auto& n : pat->notes) n.selected = noteVisible (n);
            else if (r == 2) deleteSelected();
            else if (r == 3) copySelected();
            else if (r == 4) paste();
            repaint();
        });
    }

    enum Mode { none, move, resize, rubberBand, velocity, painting } mode = none;
    std::vector<cr::Note> snapshot;
    double anchorTick = 0.0;
    int anchorRow = 0, auditionPad = -1, auditionKey = -1;
    juce::Point<int> dragStart;
    juce::Rectangle<int> rubber;
};

// A draggable pattern chip: drag onto the song timeline to place a clip.
class PatternChip : public juce::Component
{
public:
    void paint (juce::Graphics& g) override
    {
        auto& b = currentBank();
        auto r = getLocalBounds().toFloat();
        g.setColour (b.colour.withAlpha (0.8f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (Pal::bg);
        g.setFont (Fonts::ui (11.5f, true));
        const auto& p = b.patterns[(size_t) juce::jlimit (0, (int) b.patterns.size() - 1, b.workingPattern)];
        g.drawFittedText (juce::String (b.workingPattern + 1) + "  " + p.name, getLocalBounds().reduced (8, 0), juce::Justification::centredLeft, 1);
        drawIcon (g, Icon::hand, r.removeFromRight (24.0f).reduced (4.0f), Pal::bg);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.getDistanceFromDragStart() < 6) return;
        if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor (this))
            if (! c->isDragAndDropActive())
            {
                auto& en = Engine::get();
                c->startDragging ("pattern:" + juce::String (en.selectedBank.load()) + ":" + juce::String (currentBank().workingPattern), this);
            }
    }
};

// ============================================================================
// The view
// ============================================================================
class SequencerView : public ViewBase, private juce::Timer
{
public:
    SequencerView()
    {
        modeTabs.setTabs ({ "Song", "Scenes", "Pattern" });
        modeTabs.onChange = [this] (int) { updateMode(); };
        addAndMakeVisible (modeTabs);
        for (auto* b : { &zoomIn, &zoomOut }) addAndMakeVisible (b);
        zoomIn.onClick = [this] { zoom (1.4); };
        zoomOut.onClick = [this] { zoom (1.0 / 1.4); };
        gridBtn.onClick = [this] { gridMenu(); };
        addAndMakeVisible (gridBtn);

        // song
        addChildComponent (song);
        song.onSelectionChanged = [this] { if (autoArea.isVisible()) setupAutomation(); repaint(); };
        song.onEditPattern = [this] { modeTabs.setSelected (2, true); };
        const std::pair<Icon, const char*> tools[] = { { Icon::select, "Select / move (drag the right edge to resize)" }, { Icon::pencil, "Draw clips of the working pattern" }, { Icon::scissors, "Split clips" } };
        for (int i = 0; i < 3; ++i)
        {
            auto* b = songTools.add (new IconButton (tools[i].first, tools[i].second));
            b->onClick = [this, i] { song.tool = i; refreshTools(); };
            addChildComponent (b);
        }
        const char* ops[] = { "Duplicate", "Repeat x4", "Delete", "Set loop", "Edit pattern" };
        const int opIds[] = { 1, 4, 2, 3, 5 };
        for (int i = 0; i < 5; ++i)
        {
            auto* b = songOps.add (new juce::TextButton (ops[i]));
            const int id = opIds[i];
            b->onClick = [this, id] { song.clipAction (id); };
            addChildComponent (b);
        }
        addAudioBtn.onClick = [this] { Engine::get().createAudioTrack(); song.rebuildRows(); song.repaint(); };
        addChildComponent (addAudioBtn);
        addChildComponent (chip);
        automationBtn.setClickingTogglesState (true);
        automationBtn.onClick = [this] { autoArea.setVisible (automationBtn.getToggleState()); setupAutomation(); resized(); };
        addChildComponent (automationBtn);
        addChildComponent (autoArea);

        // scenes
        addChildComponent (scenes);
        scenes.onSceneMenu = [this] { sceneMenu(); };
        const char* sops[] = { "Capture", "Duplicate", "Delete", "Clear", "Paste to song", "Rename", "<", ">" };
        for (int i = 0; i < 8; ++i)
        {
            auto* b = sceneOps.add (new juce::TextButton (sops[i]));
            b->onClick = [this, i] { sceneAction (i); };
            addChildComponent (b);
        }
        syncBtn.onClick = [this] { syncMenu(); };
        stopAllBtn.onClick = [] { Engine::get().stopAllScenes(); };
        addChildComponent (syncBtn);
        addChildComponent (stopAllBtn);

        // pattern
        addChildComponent (roll);
        patternBtn.onClick = [this] { patternMenu(); };
        addChildComponent (patternBtn);
        lengthStep.getText = [] { auto& b = currentBank(); if (! b.created) return juce::String ("-"); const auto& p = b.patterns[(size_t) juce::jlimit (0, (int) b.patterns.size() - 1, b.workingPattern)];
                                  return juce::String (p.bars()) + (p.bars() == 1 ? " bar" : " bars"); };
        lengthStep.onStep = [this] (int d)
        {
            auto& e = Engine::get();
            auto& b = currentBank();
            if (! b.created) return;
            e.checkpoint();
            const juce::ScopedLock sl (e.getLock());
            auto& p = b.patterns[(size_t) juce::jlimit (0, (int) b.patterns.size() - 1, b.workingPattern)];
            p.length = juce::jlimit (cr::barTicks(), 64 * cr::barTicks(), (p.bars() + d) * cr::barTicks());
            roll.repaint();
        };
        addChildComponent (lengthStep);
        const std::pair<Icon, const char*> rtools[] = { { Icon::select, "Select / move notes" }, { Icon::pencil, "Draw notes (drag to set length)" }, { Icon::brush, "Paint notes" } };
        for (int i = 0; i < 3; ++i)
        {
            auto* b = rollTools.add (new IconButton (rtools[i].first, rtools[i].second));
            b->onClick = [this, i] { roll.tool = i; refreshTools(); };
            addChildComponent (b);
        }
        const char* rops[] = { "Quantize", "Delete", "Copy", "Paste", "Double" };
        for (int i = 0; i < 5; ++i)
        {
            auto* b = rollOps.add (new juce::TextButton (rops[i]));
            b->onClick = [this, i] { rollAction (i); };
            addChildComponent (b);
        }
        viewBtn.onClick = [this] { roll.forcePads = ! roll.forcePads; roll.scrollY = 0; roll.rebuildRows(); updateViewBtn(); roll.repaint(); };
        addChildComponent (viewBtn);
        allPadsBtn.setClickingTogglesState (true);
        allPadsBtn.onClick = [this] { roll.showAllPads = allPadsBtn.getToggleState(); roll.rebuildRows(); roll.repaint(); };
        addChildComponent (allPadsBtn);
        patAutoBtn.setClickingTogglesState (true);
        patAutoBtn.onClick = [this] { patAutoArea.setVisible (patAutoBtn.getToggleState()); setupPatternAutomation(); resized(); };
        addChildComponent (patAutoBtn);
        addChildComponent (patAutoArea);

        autoArea.axis = &song.axis;
        autoArea.grid = &song.grid;
        autoArea.x0 = SongTimeline::headerW;
        patAutoArea.axis = &roll.axis;
        patAutoArea.grid = &roll.grid;
        patAutoArea.x0 = PianoRoll::headerW;
        patAutoArea.songLevel = false;
        roll.axis.pxPerTick = 0.9;
        updateMode();
        startTimerHz (30);
    }

    void refresh() override
    {
        const int req = Nav::takeSequencerRequest();
        if (req >= 0) modeTabs.setSelected (req, false);
        bool songLevel = true; int bank = 0; juce::String param;
        if (Nav::takeAutomationRequest (songLevel, bank, param))
        {
            if (songLevel) { automationBtn.setToggleState (true, juce::dontSendNotification); autoArea.setVisible (true); }
            else { patAutoBtn.setToggleState (true, juce::dontSendNotification); patAutoArea.setVisible (true); }
        }
        song.rebuildRows();
        scenes.rebuild();
        roll.rebuildRows();
        auto& b = currentBank();
        if (b.created) patternBtn.setButtonText (juce::String (b.workingPattern + 1) + "  " + b.patterns[(size_t) juce::jlimit (0, (int) b.patterns.size() - 1, b.workingPattern)].name);
        else patternBtn.setButtonText ("No bank");
        gridBtn.setButtonText ("Grid " + gridNames()[mode() == 2 ? roll.grid : song.grid]);
        syncBtn.setButtonText ("Sync " + syncText());
        updateMode();
        if (autoArea.isVisible()) setupAutomation();
        if (patAutoArea.isVisible()) setupPatternAutomation();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Pal::bg);
        g.setColour (Pal::panel);
        g.fillRect (getLocalBounds().removeFromTop (48));
        g.fillRect (bottomBar);
        g.setColour (Pal::outline);
        g.fillRect (0, 47, getWidth(), 1);
        g.fillRect (bottomBar.getX(), bottomBar.getY(), bottomBar.getWidth(), 1);
        // overview
        auto& e = Engine::get();
        auto ov = overviewArea;
        g.setColour (Pal::bg);
        g.fillRoundedRectangle (ov.toFloat(), 3.0f);
        const int len = juce::jmax (cr::barTicks() * 16, e.getSongLengthTicks() + cr::barTicks() * 4);
        std::vector<int> rowsUsed;
        for (auto& c : e.session().song) if (std::find (rowsUsed.begin(), rowsUsed.end(), c.bank) == rowsUsed.end()) rowsUsed.push_back (c.bank);
        std::sort (rowsUsed.begin(), rowsUsed.end());
        const float rh = rowsUsed.empty() ? 0.0f : juce::jmin (4.0f, (float) (ov.getHeight() - 4) / (float) rowsUsed.size());
        for (auto& c : e.session().song)
        {
            const int ri = (int) (std::find (rowsUsed.begin(), rowsUsed.end(), c.bank) - rowsUsed.begin());
            g.setColour (e.session().banks[(size_t) c.bank].colour);
            g.fillRect ((float) ov.getX() + (float) ov.getWidth() * c.start / len, (float) ov.getY() + 2.0f + ri * rh,
                        juce::jmax (1.0f, (float) ov.getWidth() * c.length / len - 1.0f), juce::jmax (1.0f, rh - 1.0f));
        }
        if (mode() == 0)
        {
            const double visibleTicks = (song.getWidth() - SongTimeline::headerW - SongTimeline::sideW) / song.axis.pxPerTick;
            auto vr = juce::Rectangle<float> ((float) ov.getX() + (float) ov.getWidth() * (float) (song.axis.startTick / len), (float) ov.getY(),
                                              (float) ov.getWidth() * (float) (visibleTicks / len), (float) ov.getHeight());
            g.setColour (Pal::text.withAlpha (0.5f));
            g.drawRect (vr.constrainedWithin (ov.toFloat()), 1.0f);
        }
        g.setColour (Pal::text);
        g.fillRect ((float) ov.getX() + (float) ov.getWidth() * (float) (e.getPositionTicks() / len), (float) ov.getY(), 1.0f, (float) ov.getHeight());

        if (mode() == 0) drawCaps (g, "Drag to place:", chipCaption, juce::Justification::centredRight, Pal::textFaint, 9.5f);
        if (mode() == 2 && ! currentBank().created)
            drawCaps (g, "Select or create a bank to edit its patterns", roll.getBounds(), juce::Justification::centred, Pal::textFaint, 11.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (48).reduced (12, 8);
        modeTabs.setBounds (top.removeFromLeft (300));
        top.removeFromLeft (16);
        zoomIn.setBounds (top.removeFromRight (32));
        zoomOut.setBounds (top.removeFromRight (32));
        top.removeFromRight (8);
        gridBtn.setBounds (top.removeFromRight (110));
        top.removeFromRight (12);
        overviewArea = top;
        bottomBar = r.removeFromBottom (44);
        auto bb = bottomBar.reduced (12, 7);

        const int m = mode();
        if (m == 0)
        {
            for (auto* b : songTools) { b->setBounds (bb.removeFromLeft (34)); bb.removeFromLeft (2); }
            bb.removeFromLeft (12);
            for (auto* b : songOps) { b->setBounds (bb.removeFromLeft (b->getButtonText().length() > 9 ? 104 : 84)); bb.removeFromLeft (4); }
            automationBtn.setBounds (bb.removeFromRight (150));
            bb.removeFromRight (8);
            addAudioBtn.setBounds (bb.removeFromRight (130));
            bb.removeFromRight (12);
            chip.setBounds (bb.removeFromRight (170));
            chipCaption = bb.removeFromRight (100);
            if (autoArea.isVisible()) autoArea.setBounds (r.removeFromBottom (juce::jmin (260, r.getHeight() / 2)));
            song.setBounds (r);
        }
        else if (m == 1)
        {
            for (auto* b : sceneOps) { b->setBounds (bb.removeFromLeft (b->getButtonText().length() <= 1 ? 34 : 104)); bb.removeFromLeft (4); }
            stopAllBtn.setBounds (bb.removeFromRight (120));
            bb.removeFromRight (6);
            syncBtn.setBounds (bb.removeFromRight (130));
            scenes.setBounds (r);
        }
        else
        {
            auto pt = r.removeFromTop (44).reduced (12, 6);
            patternBtn.setBounds (pt.removeFromLeft (220));
            pt.removeFromLeft (10);
            lengthStep.setBounds (pt.removeFromLeft (150).withTrimmedTop (-12));
            pt.removeFromLeft (16);
            for (auto* b : rollTools) { b->setBounds (pt.removeFromLeft (34)); pt.removeFromLeft (2); }
            allPadsBtn.setBounds (pt.removeFromRight (110));
            pt.removeFromRight (6);
            viewBtn.setBounds (pt.removeFromRight (180));
            for (auto* b : rollOps) { b->setBounds (bb.removeFromLeft (84)); bb.removeFromLeft (4); }
            patAutoBtn.setBounds (bb.removeFromRight (170));
            if (patAutoArea.isVisible()) patAutoArea.setBounds (r.removeFromBottom (juce::jmin (240, r.getHeight() / 2)));
            roll.setBounds (r);
        }
    }

private:
    int mode() const { return modeTabs.getSelected(); }

    void updateMode()
    {
        const int m = mode();
        song.setVisible (m == 0);
        for (auto* b : songTools) b->setVisible (m == 0);
        for (auto* b : songOps) b->setVisible (m == 0);
        for (auto* c : std::initializer_list<juce::Component*> { &addAudioBtn, &chip, &automationBtn }) c->setVisible (m == 0);
        autoArea.setVisible (m == 0 && automationBtn.getToggleState());
        scenes.setVisible (m == 1);
        for (auto* b : sceneOps) b->setVisible (m == 1);
        syncBtn.setVisible (m == 1);
        stopAllBtn.setVisible (m == 1);
        for (auto* c : std::initializer_list<juce::Component*> { &roll, &patternBtn, &lengthStep, &allPadsBtn, &patAutoBtn }) c->setVisible (m == 2);
        updateViewBtn();
        for (auto* b : rollTools) b->setVisible (m == 2);
        for (auto* b : rollOps) b->setVisible (m == 2);
        patAutoArea.setVisible (m == 2 && patAutoBtn.getToggleState());
        gridBtn.setVisible (m != 1);
        zoomIn.setVisible (m != 1);
        zoomOut.setVisible (m != 1);
        gridBtn.setButtonText ("Grid " + gridNames()[m == 2 ? roll.grid : song.grid]);
        refreshTools();
        resized();
        repaint();
        if (m == 2) roll.grabKeyboardFocus();
    }

    void updateViewBtn()
    {
        auto& b = currentBank();
        const bool keysPad = b.created && b.pads[(size_t) Engine::get().selectedPad.load()].keysMode;
        viewBtn.setVisible (mode() == 2 && keysPad);
        viewBtn.setButtonText (roll.keysView ? "Show pad rows" : "Show keys of pad " + juce::String (Engine::get().selectedPad.load() + 1));
    }

    void refreshTools()
    {
        for (int i = 0; i < songTools.size(); ++i) songTools[i]->setToggleState (song.tool == i, juce::dontSendNotification);
        for (int i = 0; i < rollTools.size(); ++i) rollTools[i]->setToggleState (roll.tool == i, juce::dontSendNotification);
    }

    void zoom (double f)
    {
        if (mode() == 2) roll.axis.pxPerTick = juce::jlimit (0.02, 4.0, roll.axis.pxPerTick * f);
        else song.axis.pxPerTick = juce::jlimit (0.005, 2.0, song.axis.pxPerTick * f);
        repaint();
        song.repaint();
        roll.repaint();
        autoArea.repaint();
        patAutoArea.repaint();
    }

    void gridMenu()
    {
        juce::PopupMenu m;
        const int cur = mode() == 2 ? roll.grid : song.grid;
        const auto names = gridNames();
        for (int i = 0; i < names.size(); ++i) m.addItem (1 + i, names[i], true, i == cur);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&gridBtn), [this] (int r)
        {
            if (r <= 0) return;
            if (mode() == 2) roll.grid = r - 1; else song.grid = r - 1;
            gridBtn.setButtonText ("Grid " + gridNames()[r - 1]);
            repaint(); song.repaint(); roll.repaint();
        });
    }

    void setupAutomation()
    {
        auto& e = Engine::get();
        int key = e.selectedBank;
        if (juce::isPositiveAndBelow (song.selectedRow, (int) song.rows.size())) key = song.rows[(size_t) song.selectedRow].key;
        autoArea.trackKey = key;
        autoArea.rebuild();
    }

    void setupPatternAutomation()
    {
        patAutoArea.trackKey = Engine::get().selectedBank;
        patAutoArea.rebuild();
    }

    void patternMenu()
    {
        auto& e = Engine::get();
        const int b = e.selectedBank;
        e.createBank (b);
        auto& bank = currentBank();
        juce::PopupMenu m;
        for (int i = 0; i < (int) bank.patterns.size(); ++i) m.addItem (100 + i, juce::String (i + 1) + "  " + bank.patterns[(size_t) i].name, true, i == bank.workingPattern);
        m.addSeparator();
        m.addItem (1, "New pattern");
        m.addItem (2, "Duplicate pattern");
        m.addItem (3, "Rename pattern...");
        m.addItem (4, "Clear pattern");
        m.addItem (5, "Delete pattern");
        m.addItem (6, "Play this pattern");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&patternBtn), [b] (int r)
        {
            auto& en = Engine::get();
            auto& bk = en.session().banks[(size_t) b];
            if (r >= 100) bk.workingPattern = r - 100;
            else if (r == 1) bk.workingPattern = en.addPattern (b);
            else if (r == 2) { const int p = en.duplicatePattern (b, bk.workingPattern); if (p >= 0) bk.workingPattern = p; }
            else if (r == 3) askForText ("Pattern name", bk.patterns[(size_t) bk.workingPattern].name, [b] (const juce::String& n)
                             { auto& bb = Engine::get().session().banks[(size_t) b]; bb.patterns[(size_t) bb.workingPattern].name = n; ViewBase::broadcast(); });
            else if (r == 4) { en.checkpoint(); const juce::ScopedLock sl (en.getLock()); bk.patterns[(size_t) bk.workingPattern].notes.clear(); bk.patterns[(size_t) bk.workingPattern].automation.clear(); }
            else if (r == 5) en.deletePattern (b, bk.workingPattern);
            else if (r == 6) en.launchPattern (b, bk.workingPattern);
            ViewBase::broadcast();
        });
    }

    void rollAction (int i)
    {
        if (i == 0)
        {
            juce::PopupMenu m;
            for (int q = 1; q <= 6; ++q) m.addItem (q, "Quantize to " + cr::quantizeNames()[q]);
            m.addSeparator();
            for (int q = 1; q <= 6; ++q) m.addItem (100 + q, "50% towards " + cr::quantizeNames()[q]);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (rollOps[0]), [this] (int r)
            {
                if (r >= 100) roll.quantizeSelected (r - 100, 0.5f);
                else if (r > 0) roll.quantizeSelected (r, 1.0f);
            });
        }
        else if (i == 1) roll.deleteSelected();
        else if (i == 2) roll.copySelected();
        else if (i == 3) roll.paste();
        else if (i == 4)
        {
            auto& e = Engine::get();
            auto* pat = roll.pattern();
            if (pat == nullptr) return;
            e.checkpoint();
            const juce::ScopedLock sl (e.getLock());
            const int L = pat->length;
            if (L * 2 > 64 * cr::barTicks()) return;
            const auto copy = pat->notes;
            for (auto n : copy) { n.start += L; pat->notes.push_back (n); }
            for (auto& l : pat->automation) { const auto pts = l.points; for (auto p : pts) { p.tick += L; l.points.push_back (p); } }
            pat->length = L * 2;
            roll.repaint();
            lengthStep.repaint();
        }
    }

    juce::String syncText() const
    {
        const int s = Engine::get().session().sceneSyncTicks;
        if (s == cr::ppq) return "1 beat";
        if (s % cr::barTicks() == 0) { const int b = s / cr::barTicks(); return juce::String (b) + (b == 1 ? " bar" : " bars"); }
        return juce::String (s) + " ticks";
    }

    void syncMenu()
    {
        juce::PopupMenu m;
        const int cur = Engine::get().session().sceneSyncTicks;
        m.addItem (1, "1 beat", true, cur == cr::ppq);
        for (int b : { 1, 2, 4, 8 }) m.addItem (10 + b, juce::String (b) + (b == 1 ? " bar" : " bars"), true, cur == b * cr::barTicks());
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&syncBtn), [this] (int r)
        {
            auto& s = Engine::get().session();
            if (r == 1) s.sceneSyncTicks = cr::ppq;
            else if (r > 10) s.sceneSyncTicks = (r - 10) * cr::barTicks();
            syncBtn.setButtonText ("Sync " + syncText());
        });
    }

    void sceneMenu()
    {
        juce::PopupMenu m;
        m.addItem (1, "Launch");
        m.addItem (2, "Capture current patterns into this scene");
        m.addItem (3, "Rename...");
        m.addItem (4, "Duplicate");
        m.addItem (5, "Delete");
        m.showMenuAsync (juce::PopupMenu::Options(), [this] (int r)
        {
            auto& e = Engine::get();
            if (r == 1) e.launchScene (scenes.selected);
            else if (r == 2) e.captureScene (scenes.selected);
            else if (r == 3) sceneAction (5);
            else if (r == 4) sceneAction (1);
            else if (r == 5) sceneAction (2);
            scenes.repaint();
        });
    }

    void sceneAction (int i)
    {
        auto& e = Engine::get();
        auto& sc = e.session().scenes;
        const int s = scenes.selected;
        if (i == 0) { e.captureScene ((int) sc.size()); scenes.selected = (int) sc.size() - 1; scenes.repaint(); return; }
        if (! juce::isPositiveAndBelow (s, (int) sc.size())) return;
        if (i == 5)
        {
            askForText ("Scene name", sc[(size_t) s].name, [this, s] (const juce::String& n)
            {
                auto& scs = Engine::get().session().scenes;
                if (juce::isPositiveAndBelow (s, (int) scs.size())) scs[(size_t) s].name = n;
                scenes.repaint();
            });
            return;
        }
        e.checkpoint();
        const juce::ScopedLock sl (e.getLock());
        switch (i)
        {
            case 1: { auto copy = sc[(size_t) s]; copy.name << " copy"; sc.insert (sc.begin() + s + 1, copy); scenes.selected = s + 1; break; }
            case 2: sc.erase (sc.begin() + s); scenes.selected = juce::jmax (0, s - 1); break;
            case 3: sc[(size_t) s].slots.fill (-1); break;
            case 4:
            {
                // append the scene at the end of the song: one clip per track, as long as the longest pattern
                const int at = e.getSongLengthTicks();
                int len = cr::barTicks();
                for (int b = 0; b < cr::numBanks; ++b)
                {
                    const int p = sc[(size_t) s].slots[(size_t) b];
                    if (p >= 0 && p < (int) e.session().banks[(size_t) b].patterns.size()) len = juce::jmax (len, e.session().banks[(size_t) b].patterns[(size_t) p].lengthTicks());
                }
                for (int b = 0; b < cr::numBanks; ++b)
                {
                    const int p = sc[(size_t) s].slots[(size_t) b];
                    if (p >= 0 && p < (int) e.session().banks[(size_t) b].patterns.size()) e.session().song.push_back ({ b, p, at, len });
                }
                Nav::setStatus ("Scene added to the end of the song");
                break;
            }
            case 6: if (s > 0) { std::swap (sc[(size_t) s], sc[(size_t) s - 1]); scenes.selected = s - 1; } break;
            case 7: if (s + 1 < (int) sc.size()) { std::swap (sc[(size_t) s], sc[(size_t) s + 1]); scenes.selected = s + 1; } break;
            default: break;
        }
        scenes.repaint();
    }

    void timerCallback() override
    {
        auto& e = Engine::get();
        if (! isShowing()) return;
        if (e.isPlaying())
        {
            // follow the playhead in song mode
            if (mode() == 0)
            {
                const double visible = (song.getWidth() - SongTimeline::headerW - SongTimeline::sideW) / song.axis.pxPerTick;
                const double pos = e.getPositionTicks();
                if (pos > song.axis.startTick + visible * 0.9 || pos < song.axis.startTick) song.axis.startTick = juce::jmax (0.0, pos - visible * 0.1);
                song.repaint();
                autoArea.repaint();
            }
            else if (mode() == 2) roll.repaint();
            else scenes.repaint();
            repaint (overviewArea);
        }
    }

    TabStrip modeTabs;
    IconButton zoomIn { Icon::zoomIn, "Zoom in (Ctrl + wheel)" }, zoomOut { Icon::zoomOut, "Zoom out" };
    juce::TextButton gridBtn { "Grid" };
    SongTimeline song;
    juce::OwnedArray<IconButton> songTools, rollTools;
    juce::OwnedArray<juce::TextButton> songOps, sceneOps, rollOps;
    juce::TextButton addAudioBtn { "+ Audio track" }, automationBtn { "Track automation" };
    PatternChip chip;
    AutomationArea autoArea, patAutoArea;
    ScenesGrid scenes;
    juce::TextButton syncBtn { "Sync" }, stopAllBtn { "Stop all" };
    PianoRoll roll;
    juce::TextButton patternBtn, allPadsBtn { "All pads" }, patAutoBtn { "Pattern automation" }, viewBtn;
    Stepper lengthStep { "Length" };
    juce::Rectangle<int> overviewArea, bottomBar, chipCaption;
};
} // namespace

std::unique_ptr<ViewBase> createSequencerView() { return std::make_unique<SequencerView>(); }
