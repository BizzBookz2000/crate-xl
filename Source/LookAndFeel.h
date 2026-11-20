#pragma once
#include <JuceHeader.h>
#include "BinaryData.h"

// ============================================================================
// Palette (original "graphite" theme)
// ============================================================================
namespace Pal
{
const juce::Colour bg        { 0xff121317 };   // window
const juce::Colour rail      { 0xff17181d };   // left rail / transport
const juce::Colour panel     { 0xff1b1d22 };   // panels
const juce::Colour surface   { 0xff23252b };   // controls
const juce::Colour surface2  { 0xff2b2e35 };
const juce::Colour surface3  { 0xff353841 };
const juce::Colour outline   { 0xff2f323a };
const juce::Colour line      { 0xff3b3f48 };
const juce::Colour text      { 0xffeceef1 };
const juce::Colour textDim   { 0xffa3a8b1 };
const juce::Colour textFaint { 0xff6b707a };
const juce::Colour accent    { 0xfff5b83d };   // amber: UI focus / toggles
const juce::Colour play      { 0xff45d483 };
const juce::Colour rec       { 0xffff4a5c };
const juce::Colour mute      { 0xffff7a45 };
const juce::Colour solo      { 0xfff2d13d };
const juce::Colour info      { 0xff4fb3ff };
const juce::Colour emptyPad  { 0xff24262c };

inline const juce::Array<juce::Colour>& padColours()
{
    static juce::Array<juce::Colour> c { juce::Colour (0xff2fb8e8), juce::Colour (0xff4f86f7), juce::Colour (0xff7d6cf2), juce::Colour (0xffd65cd6),
                                         juce::Colour (0xffef4f6e), juce::Colour (0xffff5b3a), juce::Colour (0xffff7a45), juce::Colour (0xffff9f1c),
                                         juce::Colour (0xffe7d84a), juce::Colour (0xff9bd94a), juce::Colour (0xff3dd68c), juce::Colour (0xff2ed3c0),
                                         juce::Colour (0xffa8b3c2), juce::Colour (0xffe8e8e8) };
    return c;
}
}

// ============================================================================
// Fonts (Inter, SIL Open Font License; embedded)
// ============================================================================
namespace Fonts
{
inline juce::Typeface::Ptr regular()
{
    static auto t = juce::Typeface::createSystemTypefaceFor (BinaryData::InterRegular_ttf, BinaryData::InterRegular_ttfSize);
    return t;
}
inline juce::Typeface::Ptr semibold()
{
    static auto t = juce::Typeface::createSystemTypefaceFor (BinaryData::InterSemiBold_ttf, BinaryData::InterSemiBold_ttfSize);
    return t;
}
inline juce::Typeface::Ptr bold()
{
    static auto t = juce::Typeface::createSystemTypefaceFor (BinaryData::InterBold_ttf, BinaryData::InterBold_ttfSize);
    return t;
}
inline juce::Font ui (float size, bool strong = false)
{
    return juce::Font (juce::FontOptions (strong ? semibold() : regular()).withHeight (size));
}
inline juce::Font heavy (float size) { return juce::Font (juce::FontOptions (bold()).withHeight (size)); }
// Small uppercase labels ("VOLUME", "CHOKE GROUP")
inline juce::Font caps (float size = 10.5f) { return juce::Font (juce::FontOptions (semibold()).withHeight (size)).withExtraKerningFactor (0.06f); }
// Numeric displays
inline juce::Font digits (float size) { return juce::Font (juce::FontOptions (semibold()).withHeight (size)).withExtraKerningFactor (0.02f); }
}

// ============================================================================
class CrateLookAndFeel : public juce::LookAndFeel_V4
{
public:
    CrateLookAndFeel()
    {
        auto scheme = juce::LookAndFeel_V4::getDarkColourScheme();
        scheme.setUIColour (ColourScheme::windowBackground, Pal::bg);
        scheme.setUIColour (ColourScheme::widgetBackground, Pal::surface);
        scheme.setUIColour (ColourScheme::menuBackground, Pal::panel);
        scheme.setUIColour (ColourScheme::outline, Pal::outline);
        scheme.setUIColour (ColourScheme::defaultText, Pal::text);
        scheme.setUIColour (ColourScheme::defaultFill, Pal::accent);
        scheme.setUIColour (ColourScheme::highlightedText, Pal::bg);
        scheme.setUIColour (ColourScheme::highlightedFill, Pal::accent);
        scheme.setUIColour (ColourScheme::menuText, Pal::text);
        setColourScheme (scheme);

        setColour (juce::ResizableWindow::backgroundColourId, Pal::bg);
        setColour (juce::DocumentWindow::backgroundColourId, Pal::bg);
        setColour (juce::TextButton::buttonColourId, Pal::surface2);
        setColour (juce::TextButton::buttonOnColourId, Pal::accent);
        setColour (juce::TextButton::textColourOffId, Pal::text);
        setColour (juce::TextButton::textColourOnId, Pal::bg);
        setColour (juce::ToggleButton::textColourId, Pal::textDim);
        setColour (juce::ToggleButton::tickColourId, Pal::accent);
        setColour (juce::ComboBox::backgroundColourId, Pal::surface2);
        setColour (juce::ComboBox::outlineColourId, Pal::outline);
        setColour (juce::ComboBox::textColourId, Pal::text);
        setColour (juce::ComboBox::arrowColourId, Pal::textDim);
        setColour (juce::PopupMenu::backgroundColourId, Pal::panel);
        setColour (juce::PopupMenu::textColourId, Pal::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Pal::surface3);
        setColour (juce::PopupMenu::highlightedTextColourId, Pal::text);
        setColour (juce::PopupMenu::headerTextColourId, Pal::textFaint);
        setColour (juce::Label::textColourId, Pal::text);
        setColour (juce::Slider::textBoxTextColourId, Pal::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::thumbColourId, Pal::text);
        setColour (juce::Slider::trackColourId, Pal::accent);
        setColour (juce::Slider::backgroundColourId, Pal::surface3);
        setColour (juce::Slider::rotarySliderFillColourId, Pal::accent);
        setColour (juce::Slider::rotarySliderOutlineColourId, Pal::surface3);
        setColour (juce::TextEditor::backgroundColourId, Pal::surface);
        setColour (juce::TextEditor::outlineColourId, Pal::outline);
        setColour (juce::TextEditor::focusedOutlineColourId, Pal::accent);
        setColour (juce::TextEditor::textColourId, Pal::text);
        setColour (juce::TextEditor::highlightColourId, Pal::accent.withAlpha (0.35f));
        setColour (juce::CaretComponent::caretColourId, Pal::accent);
        setColour (juce::ListBox::backgroundColourId, Pal::panel);
        setColour (juce::ListBox::outlineColourId, Pal::outline);
        setColour (juce::ScrollBar::thumbColourId, Pal::surface3);
        setColour (juce::AlertWindow::backgroundColourId, Pal::panel);
        setColour (juce::AlertWindow::textColourId, Pal::text);
        setColour (juce::AlertWindow::outlineColourId, Pal::line);
        setColour (juce::TooltipWindow::backgroundColourId, Pal::surface3);
        setColour (juce::TooltipWindow::textColourId, Pal::text);
        setColour (juce::TooltipWindow::outlineColourId, Pal::line);
        setColour (juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour (0xffe9eaec));
        setColour (juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour (0xff1c1d21));
        setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour (0xff9a9ca1));
        setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, Pal::accent.withAlpha (0.7f));
        setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, Pal::accent.withAlpha (0.25f));
        setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
        setColour (juce::MidiKeyboardComponent::upDownButtonBackgroundColourId, Pal::surface2);
        setColour (juce::MidiKeyboardComponent::upDownButtonArrowColourId, Pal::textDim);
        setColour (juce::TreeView::backgroundColourId, Pal::panel);
        setColour (juce::DirectoryContentsDisplayComponent::highlightColourId, Pal::surface3);
        setColour (juce::DirectoryContentsDisplayComponent::textColourId, Pal::text);
        setColour (juce::FileTreeComponent::linesColourId, Pal::line);
        setColour (juce::ProgressBar::backgroundColourId, Pal::surface);
        setColour (juce::ProgressBar::foregroundColourId, Pal::accent);
        setDefaultSansSerifTypeface (Fonts::regular());
    }

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font& f) override
    {
        const auto style = f.getTypefaceStyle();
        if (style.containsIgnoreCase ("SemiBold")) return Fonts::semibold();
        if (style.containsIgnoreCase ("Bold") || f.isBold()) return f.getTypefaceName() == "Inter" && style.containsIgnoreCase ("Bold") ? Fonts::bold() : Fonts::semibold();
        return Fonts::regular();
    }

    // ---- buttons ----------------------------------------------------------------------------
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bgColour, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        auto c = on ? b.findColour (juce::TextButton::buttonOnColourId) : bgColour;
        if (! b.isEnabled()) c = c.withMultipliedAlpha (0.45f);
        else if (down) c = c.brighter (0.12f);
        else if (over) c = c.brighter (0.06f);
        g.setColour (c);
        g.fillRoundedRectangle (r, 4.0f);
        if (! on)
        {
            g.setColour (Pal::line.withAlpha (b.isEnabled() ? 0.55f : 0.25f));
            g.drawRoundedRectangle (r, 4.0f, 1.0f);
        }
    }

    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return Fonts::caps (juce::jmin (12.0f, (float) h * 0.42f)); }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        g.setFont (getTextButtonFont (b, b.getHeight()));
        auto c = b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId);
        g.setColour (b.isEnabled() ? c : c.withMultipliedAlpha (0.4f));
        g.drawFittedText (b.getButtonText().toUpperCase(), b.getLocalBounds().reduced (4, 0), juce::Justification::centred, 1, 0.8f);
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool) override
    {
        auto r = b.getLocalBounds().toFloat();
        const bool on = b.getToggleState();
        auto sw = r.removeFromRight (34.0f).withSizeKeepingCentre (30.0f, 16.0f);
        g.setColour (on ? Pal::accent : (over ? Pal::surface3.brighter (0.05f) : Pal::surface3));
        g.fillRoundedRectangle (sw, 8.0f);
        const float x = on ? sw.getRight() - 14.0f : sw.getX() + 2.0f;
        g.setColour (on ? Pal::bg : Pal::textDim);
        g.fillEllipse (x, sw.getY() + 2.0f, 12.0f, 12.0f);
        g.setColour (b.isEnabled() ? Pal::textDim : Pal::textFaint);
        g.setFont (Fonts::caps (11.0f));
        g.drawFittedText (b.getButtonText().toUpperCase(), r.toNearestInt().withTrimmedRight (6), juce::Justification::centredLeft, 1);
    }

    // ---- sliders ----------------------------------------------------------------------------
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s) override
    {
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
        const float size = juce::jmin (r.getWidth(), r.getHeight());
        auto c = r.withSizeKeepingCentre (size, size);
        const float stroke = juce::jmax (2.5f, size * 0.085f);
        const float radius = size * 0.5f - stroke * 0.5f;
        auto fill = s.findColour (juce::Slider::rotarySliderFillColourId);
        if (! s.isEnabled()) fill = Pal::textFaint;

        juce::Path track;
        track.addCentredArc (c.getCentreX(), c.getCentreY(), radius, radius, 0.0f, start, end, true);
        g.setColour (Pal::surface3);
        g.strokePath (track, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
        const float zero = bipolar ? start + (end - start) * (float) s.valueToProportionOfLength (0.0) : start;
        const float angle = start + pos * (end - start);
        juce::Path arc;
        arc.addCentredArc (c.getCentreX(), c.getCentreY(), radius, radius, 0.0f, juce::jmin (zero, angle), juce::jmax (zero, angle), true);
        g.setColour (fill);
        g.strokePath (arc, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // pointer
        const float inner = radius - stroke * 1.4f;
        juce::Point<float> p1 (c.getCentreX() + inner * 0.25f * std::sin (angle), c.getCentreY() - inner * 0.25f * std::cos (angle));
        juce::Point<float> p2 (c.getCentreX() + inner * std::sin (angle), c.getCentreY() - inner * std::cos (angle));
        g.setColour (s.isEnabled() ? Pal::text : Pal::textFaint);
        g.drawLine ({ p1, p2 }, juce::jmax (1.8f, stroke * 0.7f));
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        auto fill = s.findColour (juce::Slider::trackColourId);
        if (! s.isEnabled()) fill = Pal::textFaint;
        if (style == juce::Slider::LinearBar || style == juce::Slider::LinearBarVertical)
        {
            g.setColour (Pal::surface);
            g.fillRoundedRectangle (r, 3.0f);
            auto f = style == juce::Slider::LinearBar ? r.withRight (pos) : r.withTop (pos);
            g.setColour (fill.withAlpha (0.8f));
            g.fillRoundedRectangle (f, 3.0f);
            return;
        }
        if (style == juce::Slider::LinearVertical)
        {
            // big touch fader (velocity / volume style): track + filled level + cap
            auto track = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), 6.0f), r.getHeight());
            g.setColour (Pal::surface);
            g.fillRoundedRectangle (track, 3.0f);
            const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
            const float zeroY = bipolar ? r.getY() + r.getHeight() * (1.0f - (float) s.valueToProportionOfLength (0.0)) : r.getBottom();
            g.setColour (fill);
            g.fillRoundedRectangle (track.withTop (juce::jmin (pos, zeroY)).withBottom (juce::jmax (pos, zeroY)), 3.0f);
            auto cap = juce::Rectangle<float> (r.getWidth() * 0.9f, 10.0f).withCentre ({ r.getCentreX(), pos });
            g.setColour (Pal::surface3.brighter (0.25f));
            g.fillRoundedRectangle (cap, 3.0f);
            g.setColour (Pal::text);
            g.fillRect (cap.withSizeKeepingCentre (cap.getWidth() * 0.6f, 2.0f));
            return;
        }
        auto track = r.withSizeKeepingCentre (r.getWidth(), 4.0f);
        g.setColour (Pal::surface3);
        g.fillRoundedRectangle (track, 2.0f);
        g.setColour (fill);
        g.fillRoundedRectangle (track.withRight (pos), 2.0f);
        g.setColour (Pal::text);
        g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ pos, r.getCentreY() }));
    }

    juce::Label* createSliderTextBox (juce::Slider& s) override
    {
        auto* l = juce::LookAndFeel_V4::createSliderTextBox (s);
        l->setFont (Fonts::digits (11.5f));
        l->setColour (juce::Label::textColourId, Pal::text);
        l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
        l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        return l;
    }

    // ---- combo boxes / menus -----------------------------------------------------------------------
    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId).withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.5f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (Pal::line.withAlpha (0.5f));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
        juce::Path arrow;
        const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
        arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
        g.setColour (Pal::textDim);
        g.fillPath (arrow);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return Fonts::ui (12.5f, true); }
    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (8, 1, box.getWidth() - 28, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }

    juce::Font getPopupMenuFont() override { return Fonts::ui (13.5f); }

    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
    {
        g.fillAll (Pal::panel);
        g.setColour (Pal::line);
        g.drawRect (0, 0, w, h, 1);
    }

    void drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& name) override
    {
        g.setFont (Fonts::caps (10.5f));
        g.setColour (Pal::textFaint);
        g.drawFittedText (name.toUpperCase(), area.reduced (12, 0).withTrimmedTop (4), juce::Justification::centredLeft, 1);
    }

    // ---- misc -------------------------------------------------------------------------------------------
    void drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart, int thumbSize, bool over, bool down) override
    {
        auto thumb = vertical ? juce::Rectangle<int> (x + 2, thumbStart, w - 4, thumbSize) : juce::Rectangle<int> (thumbStart, y + 2, thumbSize, h - 4);
        g.setColour (over || down ? Pal::line.brighter (0.2f) : Pal::line);
        g.fillRoundedRectangle (thumb.toFloat(), 3.0f);
    }

    void drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h) override
    {
        g.fillAll (Pal::surface3);
        g.setColour (Pal::line);
        g.drawRect (0, 0, w, h);
        g.setColour (Pal::text);
        g.setFont (Fonts::ui (12.5f));
        g.drawFittedText (text, juce::Rectangle<int> (w, h).reduced (8, 4), juce::Justification::centredLeft, 4);
    }

    juce::Rectangle<int> getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent) override
    {
        const int w = juce::jmin (360, (int) juce::GlyphArrangement::getStringWidth (Fonts::ui (12.5f), text) + 20);
        const int h = 26 + 16 * (text.length() / 52);
        return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - w - 8 : pos.x + 14, pos.y + 18, w, h).constrainedWithin (parent);
    }

    void fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor& e) override
    {
        g.setColour (e.findColour (juce::TextEditor::backgroundColourId));
        g.fillRoundedRectangle (juce::Rectangle<float> (0, 0, (float) w, (float) h), 4.0f);
    }

    void drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& e) override
    {
        g.setColour (e.hasKeyboardFocus (true) ? Pal::accent : Pal::line.withAlpha (0.6f));
        g.drawRoundedRectangle (juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f), 4.0f, 1.0f);
    }

    void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics& g, const juce::Path& path, juce::Image&) override
    {
        g.setColour (Pal::panel);
        g.fillPath (path);
        g.setColour (Pal::line);
        g.strokePath (path, juce::PathStrokeType (1.0f));
    }

    int getCallOutBoxBorderSize (const juce::CallOutBox&) override { return 14; }

    void drawProgressBar (juce::Graphics& g, juce::ProgressBar&, int w, int h, double progress, const juce::String& text) override
    {
        auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h);
        g.setColour (Pal::surface);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (Pal::accent);
        g.fillRoundedRectangle (r.withWidth (r.getWidth() * (float) juce::jlimit (0.0, 1.0, progress)), 4.0f);
        g.setColour (Pal::text);
        g.setFont (Fonts::ui (12.0f, true));
        g.drawText (text, r, juce::Justification::centred);
    }

    void drawLabel (juce::Graphics& g, juce::Label& l) override
    {
        g.fillAll (l.findColour (juce::Label::backgroundColourId));
        if (! l.isBeingEdited())
        {
            g.setColour (l.findColour (juce::Label::textColourId).withMultipliedAlpha (l.isEnabled() ? 1.0f : 0.5f));
            g.setFont (l.getFont());
            g.drawFittedText (l.getText(), l.getBorderSize().subtractedFrom (l.getLocalBounds()), l.getJustificationType(),
                              juce::jmax (1, (int) ((float) l.getHeight() / l.getFont().getHeight())), l.getMinimumHorizontalScale());
        }
    }

    juce::Font getAlertWindowTitleFont() override { return Fonts::ui (17.0f, true); }
    juce::Font getAlertWindowMessageFont() override { return Fonts::ui (14.0f); }
    juce::Font getAlertWindowFont() override { return Fonts::ui (13.0f); }
};
