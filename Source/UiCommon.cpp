#include "UiCommon.h"
#include "Views.h"

// ============================================================================
// Icons
// ============================================================================
void drawIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> r, juce::Colour c)
{
    const float s = juce::jmin (r.getWidth(), r.getHeight());
    auto b = r.withSizeKeepingCentre (s, s);
    const float x = b.getX(), y = b.getY();
    auto P = [&] (float fx, float fy) { return juce::Point<float> (x + fx * s, y + fy * s); };
    const float lw = juce::jmax (1.4f, s * 0.085f);
    juce::PathStrokeType stroke (lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setColour (c);
    juce::Path p;
    auto line = [&] (float x1, float y1, float x2, float y2) { p.startNewSubPath (P (x1, y1)); p.lineTo (P (x2, y2)); };
    auto rect = [&] (float x1, float y1, float w, float h) { p.addRoundedRectangle (x + x1 * s, y + y1 * s, w * s, h * s, s * 0.06f); };
    bool fill = false;

    switch (icon)
    {
        case Icon::play: p.addTriangle (P (0.28f, 0.18f), P (0.28f, 0.82f), P (0.84f, 0.5f)); fill = true; break;
        case Icon::stop: case Icon::stopSquare: p.addRoundedRectangle (x + 0.24f * s, y + 0.24f * s, 0.52f * s, 0.52f * s, s * 0.06f); fill = true; break;
        case Icon::record: p.addEllipse (x + 0.22f * s, y + 0.22f * s, 0.56f * s, 0.56f * s); fill = true; break;
        case Icon::rewind:
            p.addTriangle (P (0.5f, 0.25f), P (0.5f, 0.75f), P (0.16f, 0.5f));
            p.addTriangle (P (0.84f, 0.25f), P (0.84f, 0.75f), P (0.5f, 0.5f)); fill = true; break;
        case Icon::forward:
            p.addTriangle (P (0.16f, 0.25f), P (0.16f, 0.75f), P (0.5f, 0.5f));
            p.addTriangle (P (0.5f, 0.25f), P (0.5f, 0.75f), P (0.84f, 0.5f)); fill = true; break;
        case Icon::loop:
            p.addCentredArc (x + 0.5f * s, y + 0.5f * s, 0.3f * s, 0.22f * s, 0.0f, 0.3f, juce::MathConstants<float>::twoPi - 0.3f, true);
            g.strokePath (p, stroke);
            p.clear();
            p.addTriangle (P (0.66f, 0.18f), P (0.66f, 0.38f), P (0.82f, 0.28f));
            fill = true; break;
        case Icon::metronome:
            p.startNewSubPath (P (0.3f, 0.85f)); p.lineTo (P (0.42f, 0.15f)); p.lineTo (P (0.58f, 0.15f)); p.lineTo (P (0.7f, 0.85f)); p.closeSubPath();
            line (0.5f, 0.7f, 0.72f, 0.3f); break;
        case Icon::undo:
            p.addCentredArc (x + 0.52f * s, y + 0.56f * s, 0.26f * s, 0.24f * s, 0.0f, -1.6f, 1.4f, true);
            line (0.26f, 0.32f, 0.26f, 0.56f); line (0.26f, 0.32f, 0.48f, 0.32f); break;
        case Icon::redo:
            p.addCentredArc (x + 0.48f * s, y + 0.56f * s, 0.26f * s, 0.24f * s, 0.0f, 1.6f, -1.4f, true);
            line (0.74f, 0.32f, 0.74f, 0.56f); line (0.74f, 0.32f, 0.52f, 0.32f); break;
        case Icon::menu: line (0.2f, 0.3f, 0.8f, 0.3f); line (0.2f, 0.5f, 0.8f, 0.5f); line (0.2f, 0.7f, 0.8f, 0.7f); break;
        case Icon::dots:
            for (float fy : { 0.25f, 0.5f, 0.75f }) p.addEllipse (x + 0.44f * s, y + (fy - 0.06f) * s, 0.12f * s, 0.12f * s);
            fill = true; break;
        case Icon::settings:
            p.addEllipse (x + 0.36f * s, y + 0.36f * s, 0.28f * s, 0.28f * s);
            for (int i = 0; i < 8; ++i)
            {
                const float a = (float) i * juce::MathConstants<float>::twoPi / 8.0f;
                p.startNewSubPath (x + s * (0.5f + 0.24f * std::cos (a)), y + s * (0.5f + 0.24f * std::sin (a)));
                p.lineTo (x + s * (0.5f + 0.38f * std::cos (a)), y + s * (0.5f + 0.38f * std::sin (a)));
            }
            break;
        case Icon::pads:
            for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) rect (0.14f + i * 0.26f, 0.14f + j * 0.26f, 0.2f, 0.2f);
            fill = true; break;
        case Icon::editor:
            p.startNewSubPath (P (0.1f, 0.5f));
            for (int i = 0; i <= 16; ++i) { const float fx = 0.1f + 0.8f * i / 16.0f; p.lineTo (P (fx, 0.5f + 0.3f * std::sin (i * 1.3f) * (i % 2 ? 1.0f : 0.5f))); }
            break;
        case Icon::sequencer:
            rect (0.12f, 0.2f, 0.42f, 0.14f); rect (0.32f, 0.43f, 0.5f, 0.14f); rect (0.18f, 0.66f, 0.3f, 0.14f); fill = true; break;
        case Icon::mixer: case Icon::sliders:
            line (0.25f, 0.15f, 0.25f, 0.85f); line (0.5f, 0.15f, 0.5f, 0.85f); line (0.75f, 0.15f, 0.75f, 0.85f);
            g.strokePath (p, stroke); p.clear();
            rect (0.17f, 0.55f, 0.16f, 0.1f); rect (0.42f, 0.3f, 0.16f, 0.1f); rect (0.67f, 0.65f, 0.16f, 0.1f); fill = true; break;
        case Icon::browser: case Icon::folder:
            p.startNewSubPath (P (0.12f, 0.25f)); p.lineTo (P (0.4f, 0.25f)); p.lineTo (P (0.48f, 0.34f)); p.lineTo (P (0.88f, 0.34f));
            p.lineTo (P (0.88f, 0.78f)); p.lineTo (P (0.12f, 0.78f)); p.closeSubPath(); break;
        case Icon::fx:
            p.addStar (P (0.5f, 0.5f), 5, s * 0.16f, s * 0.38f, 0.0f); break;
        case Icon::macro:
            p.addEllipse (x + 0.2f * s, y + 0.2f * s, 0.6f * s, 0.6f * s); line (0.5f, 0.5f, 0.68f, 0.32f); break;
        case Icon::keyboard:
            rect (0.1f, 0.25f, 0.8f, 0.5f);
            g.strokePath (p, stroke); p.clear();
            for (float fx : { 0.3f, 0.5f, 0.7f }) line (fx, 0.25f, fx, 0.75f);
            g.strokePath (p, juce::PathStrokeType (lw * 0.6f)); p.clear();
            for (float fx : { 0.25f, 0.45f, 0.65f }) rect (fx, 0.25f, 0.1f, 0.28f);
            fill = true; break;
        case Icon::plus: line (0.5f, 0.2f, 0.5f, 0.8f); line (0.2f, 0.5f, 0.8f, 0.5f); break;
        case Icon::minus: line (0.2f, 0.5f, 0.8f, 0.5f); break;
        case Icon::zoomIn: case Icon::zoomOut:
            p.addEllipse (x + 0.16f * s, y + 0.16f * s, 0.48f * s, 0.48f * s); line (0.56f, 0.56f, 0.84f, 0.84f);
            line (0.28f, 0.4f, 0.52f, 0.4f); if (icon == Icon::zoomIn) line (0.4f, 0.28f, 0.4f, 0.52f); break;
        case Icon::trash:
            line (0.2f, 0.28f, 0.8f, 0.28f); line (0.4f, 0.28f, 0.42f, 0.16f); line (0.42f, 0.16f, 0.58f, 0.16f); line (0.58f, 0.16f, 0.6f, 0.28f);
            p.startNewSubPath (P (0.28f, 0.3f)); p.lineTo (P (0.32f, 0.84f)); p.lineTo (P (0.68f, 0.84f)); p.lineTo (P (0.72f, 0.3f)); break;
        case Icon::copy: rect (0.18f, 0.18f, 0.44f, 0.5f); rect (0.38f, 0.34f, 0.44f, 0.5f); break;
        case Icon::paste: rect (0.22f, 0.2f, 0.56f, 0.66f); line (0.38f, 0.2f, 0.62f, 0.2f); break;
        case Icon::cut: case Icon::scissors:
            p.addEllipse (x + 0.16f * s, y + 0.6f * s, 0.22f * s, 0.22f * s); p.addEllipse (x + 0.62f * s, y + 0.6f * s, 0.22f * s, 0.22f * s);
            line (0.33f, 0.62f, 0.7f, 0.15f); line (0.67f, 0.62f, 0.3f, 0.15f); break;
        case Icon::close: line (0.25f, 0.25f, 0.75f, 0.75f); line (0.75f, 0.25f, 0.25f, 0.75f); break;
        case Icon::check: p.startNewSubPath (P (0.2f, 0.52f)); p.lineTo (P (0.42f, 0.74f)); p.lineTo (P (0.8f, 0.28f)); break;
        case Icon::power:
            p.addCentredArc (x + 0.5f * s, y + 0.54f * s, 0.28f * s, 0.28f * s, 0.0f, 0.6f, juce::MathConstants<float>::twoPi - 0.6f, true);
            line (0.5f, 0.16f, 0.5f, 0.5f); break;
        case Icon::up: p.startNewSubPath (P (0.25f, 0.62f)); p.lineTo (P (0.5f, 0.36f)); p.lineTo (P (0.75f, 0.62f)); break;
        case Icon::down: p.startNewSubPath (P (0.25f, 0.38f)); p.lineTo (P (0.5f, 0.64f)); p.lineTo (P (0.75f, 0.38f)); break;
        case Icon::left: p.startNewSubPath (P (0.62f, 0.25f)); p.lineTo (P (0.36f, 0.5f)); p.lineTo (P (0.62f, 0.75f)); break;
        case Icon::right: p.startNewSubPath (P (0.38f, 0.25f)); p.lineTo (P (0.64f, 0.5f)); p.lineTo (P (0.38f, 0.75f)); break;
        case Icon::mic:
            p.addRoundedRectangle (x + 0.38f * s, y + 0.12f * s, 0.24f * s, 0.44f * s, 0.12f * s);
            p.addCentredArc (x + 0.5f * s, y + 0.46f * s, 0.22f * s, 0.22f * s, 0.0f, 1.9f, 4.4f, true);
            line (0.5f, 0.68f, 0.5f, 0.86f); break;
        case Icon::wave:
            for (int i = 0; i < 9; ++i) { const float fx = 0.14f + i * 0.09f, h = 0.12f + 0.3f * std::abs (std::sin (i * 1.7f)); line (fx, 0.5f - h, fx, 0.5f + h); }
            break;
        case Icon::link:
            p.addRoundedRectangle (x + 0.12f * s, y + 0.38f * s, 0.42f * s, 0.24f * s, 0.12f * s);
            p.addRoundedRectangle (x + 0.46f * s, y + 0.38f * s, 0.42f * s, 0.24f * s, 0.12f * s); break;
        case Icon::lock:
            rect (0.24f, 0.44f, 0.52f, 0.4f);
            p.addCentredArc (x + 0.5f * s, y + 0.44f * s, 0.17f * s, 0.2f * s, 0.0f, -1.57f, 1.57f, true); break;
        case Icon::hand:
            for (int i = 0; i < 4; ++i) line (0.3f + i * 0.13f, 0.2f + (i == 0 || i == 3 ? 0.08f : 0.0f), 0.3f + i * 0.13f, 0.6f);
            p.startNewSubPath (P (0.24f, 0.55f)); p.quadraticTo (P (0.3f, 0.86f), P (0.55f, 0.86f)); p.quadraticTo (P (0.72f, 0.86f), P (0.7f, 0.55f)); break;
        case Icon::select:
            {
                const float dash[] = { s * 0.08f, s * 0.06f };
                juce::Path r2; r2.addRectangle (x + 0.18f * s, y + 0.18f * s, 0.64f * s, 0.64f * s);
                juce::Path dashed; juce::PathStrokeType (lw).createDashedStroke (dashed, r2, dash, 2);
                g.fillPath (dashed);
                return;
            }
        case Icon::pencil: case Icon::brush:
            p.startNewSubPath (P (0.2f, 0.8f)); p.lineTo (P (0.26f, 0.62f)); p.lineTo (P (0.68f, 0.2f)); p.lineTo (P (0.8f, 0.32f));
            p.lineTo (P (0.38f, 0.74f)); p.closeSubPath();
            if (icon == Icon::brush) { line (0.56f, 0.86f, 0.86f, 0.86f); } break;
        case Icon::split: line (0.5f, 0.12f, 0.5f, 0.88f); rect (0.14f, 0.32f, 0.26f, 0.36f); rect (0.6f, 0.32f, 0.26f, 0.36f); break;
        case Icon::magnet:
            p.addCentredArc (x + 0.5f * s, y + 0.45f * s, 0.26f * s, 0.26f * s, 0.0f, juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi * 3.0f, true);
            line (0.24f, 0.45f, 0.24f, 0.2f); line (0.76f, 0.45f, 0.76f, 0.2f); break;
        case Icon::headphones:
            p.addCentredArc (x + 0.5f * s, y + 0.55f * s, 0.32f * s, 0.32f * s, 0.0f, -1.57f, 1.57f, true);
            rect (0.14f, 0.52f, 0.14f, 0.28f); rect (0.72f, 0.52f, 0.14f, 0.28f); break;
        case Icon::grid:
            for (float f : { 0.3f, 0.5f, 0.7f }) { line (f, 0.15f, f, 0.85f); line (0.15f, f, 0.85f, f); } break;
        case Icon::tap: p.addEllipse (x + 0.3f * s, y + 0.3f * s, 0.4f * s, 0.4f * s); p.addEllipse (x + 0.16f * s, y + 0.16f * s, 0.68f * s, 0.68f * s); break;
        case Icon::save:
            p.startNewSubPath (P (0.18f, 0.18f)); p.lineTo (P (0.7f, 0.18f)); p.lineTo (P (0.82f, 0.3f)); p.lineTo (P (0.82f, 0.82f)); p.lineTo (P (0.18f, 0.82f)); p.closeSubPath();
            rect (0.32f, 0.18f, 0.32f, 0.2f); break;
        case Icon::eye:
            p.addEllipse (x + 0.14f * s, y + 0.3f * s, 0.72f * s, 0.4f * s); p.addEllipse (x + 0.42f * s, y + 0.42f * s, 0.16f * s, 0.16f * s); break;
        case Icon::star: p.addStar (P (0.5f, 0.52f), 5, s * 0.16f, s * 0.36f, 0.0f); fill = true; break;
        case Icon::chord: for (float fy : { 0.3f, 0.5f, 0.7f }) p.addEllipse (x + 0.38f * s, y + (fy - 0.08f) * s, 0.24f * s, 0.16f * s); fill = true; break;
        case Icon::automation:
            p.startNewSubPath (P (0.12f, 0.72f)); p.lineTo (P (0.36f, 0.32f)); p.lineTo (P (0.62f, 0.6f)); p.lineTo (P (0.88f, 0.24f));
            g.strokePath (p, stroke); p.clear();
            for (auto pt : { P (0.36f, 0.32f), P (0.62f, 0.6f) }) p.addEllipse (pt.x - s * 0.06f, pt.y - s * 0.06f, s * 0.12f, s * 0.12f);
            fill = true; break;
        case Icon::io:
            line (0.15f, 0.36f, 0.6f, 0.36f); p.addTriangle (P (0.6f, 0.26f), P (0.6f, 0.46f), P (0.75f, 0.36f));
            line (0.85f, 0.66f, 0.4f, 0.66f); p.addTriangle (P (0.4f, 0.56f), P (0.4f, 0.76f), P (0.25f, 0.66f)); break;
        default: break;
    }
    if (fill) g.fillPath (p); else g.strokePath (p, stroke);
}

IconButton::IconButton (Icon i, const juce::String& tooltip, bool toggle) : juce::Button (tooltip), icon (i)
{
    setTooltip (tooltip);
    setClickingTogglesState (toggle);
}

void IconButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat();
    const bool on = getToggleState();
    if (! flat || on || over)
    {
        auto bgc = on ? onColour.withAlpha (flat ? 0.18f : 1.0f) : (flat ? Pal::surface2 : Pal::surface2);
        if (down) bgc = bgc.brighter (0.1f);
        g.setColour (bgc);
        g.fillRoundedRectangle (r.reduced (1.0f), 5.0f);
    }
    auto c = on ? (flat ? onColour : Pal::bg) : (isEnabled() ? (over ? Pal::text : Pal::textDim) : Pal::textFaint);
    if (label.isNotEmpty())
    {
        auto lr = r.reduced (2.0f);
        if (lr.getHeight() > lr.getWidth() * 0.9f)
        {
            auto ir = lr.removeFromTop (lr.getHeight() * 0.6f);
            drawIcon (g, icon, ir.reduced (ir.getHeight() * 0.18f), c);
            g.setColour (c);
            g.setFont (Fonts::caps (9.0f));
            g.drawFittedText (label, lr.toNearestInt(), juce::Justification::centredTop, 1);
        }
        else
        {
            auto ir = lr.removeFromLeft (lr.getHeight());
            drawIcon (g, icon, ir.reduced (ir.getHeight() * 0.2f), c);
            g.setColour (c);
            g.setFont (Fonts::caps (10.5f));
            g.drawFittedText (label.toUpperCase(), lr.toNearestInt().withTrimmedLeft (2), juce::Justification::centredLeft, 1);
        }
        return;
    }
    const float pad = r.getHeight() * 0.24f;
    drawIcon (g, icon, r.reduced (pad), c);
}

// ============================================================================
// Tabs, steppers, choice fields, power buttons
// ============================================================================
void TabStrip::paint (juce::Graphics& g)
{
    if (tabs.isEmpty()) return;
    const float w = (float) getWidth() / (float) tabs.size();
    g.setColour (Pal::outline);
    g.fillRect (0, getHeight() - 1, getWidth(), 1);
    for (int i = 0; i < tabs.size(); ++i)
    {
        auto r = juce::Rectangle<float> ((float) i * w, 0.0f, w, (float) getHeight());
        const bool sel = i == selected;
        g.setColour (sel ? Pal::text : Pal::textFaint);
        g.setFont (Fonts::caps (11.0f));
        g.drawFittedText (tabs[i].toUpperCase(), r.toNearestInt().reduced (4, 0), juce::Justification::centred, 1);
        if (sel)
        {
            g.setColour (accent);
            g.fillRect (r.withTop (r.getBottom() - 2.0f).reduced (w * 0.12f, 0.0f));
        }
    }
}

void TabStrip::mouseDown (const juce::MouseEvent& e)
{
    if (tabs.isEmpty()) return;
    const int i = juce::jlimit (0, tabs.size() - 1, e.x * tabs.size() / juce::jmax (1, getWidth()));
    if (i != selected) { selected = i; repaint(); if (onChange) onChange (i); }
}

void Stepper::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    drawCaps (g, label, r.removeFromTop (14), juce::Justification::centred, Pal::textFaint, 9.5f);
    auto row = r.reduced (0, 1).toFloat();
    g.setColour (Pal::surface);
    g.fillRoundedRectangle (row, 4.0f);
    const float aw = juce::jmin (22.0f, row.getWidth() * 0.22f);
    drawIcon (g, Icon::left, row.withWidth (aw).reduced (3.0f), Pal::textDim);
    drawIcon (g, Icon::right, row.withX (row.getRight() - aw).withWidth (aw).reduced (3.0f), Pal::textDim);
    g.setColour (Pal::text);
    g.setFont (Fonts::ui (12.5f, true));
    g.drawFittedText (getText ? getText() : juce::String(), row.toNearestInt().reduced ((int) aw, 0), juce::Justification::centred, 1);
}

void Stepper::mouseDown (const juce::MouseEvent& e)
{
    const int aw = juce::jmin (22, getWidth() * 22 / 100);
    if (e.y < 14) return;
    if (e.x < aw) { if (onStep) onStep (-1); }
    else if (e.x > getWidth() - aw) { if (onStep) onStep (1); }
    else if (onClickValue) onClickValue();
    repaint();
}

void ChoiceField::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    drawCaps (g, label, r.removeFromTop (14), juce::Justification::centred, Pal::textFaint, 9.5f);
    juce::String text = getText ? getText() : (getIndex ? choices[getIndex()] : juce::String());
    g.setColour (isEnabled() ? accent : Pal::textFaint);
    g.setFont (Fonts::ui (12.5f, true));
    g.drawFittedText (text.toUpperCase(), r, juce::Justification::centred, 1, 0.75f);
}

void ChoiceField::mouseDown (const juce::MouseEvent&)
{
    if (! isEnabled()) return;
    if (onClick) { onClick(); return; }
    juce::PopupMenu m;
    const int cur = getIndex ? getIndex() : -1;
    for (int i = 0; i < choices.size(); ++i) m.addItem (i + 1, choices[i], true, i == cur);
    juce::Component::SafePointer<ChoiceField> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        if (safe->setIndex) safe->setIndex (r - 1);
        safe->repaint();
    });
}

void PowerButton::paintButton (juce::Graphics& g, bool over, bool)
{
    auto r = getLocalBounds().toFloat();
    const bool on = getToggleState();
    g.setColour (on ? accent.withAlpha (0.2f) : (over ? Pal::surface3 : Pal::surface2));
    g.fillEllipse (r.reduced (1.0f));
    drawIcon (g, Icon::power, r.reduced (r.getWidth() * 0.2f), on ? accent : Pal::textFaint);
}

// ============================================================================
// Knob
// ============================================================================
namespace { std::vector<Knob*>& liveKnobs() { static std::vector<Knob*> k; return k; } }

Knob::Knob (const juce::String& l) : label (l)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setRange (0.0, 1.0);
    slider.setMouseDragSensitivity (180);
    slider.setVelocityBasedMode (false);
    slider.addListener (this);
    slider.setPopupMenuEnabled (false);
    addAndMakeVisible (slider);
    liveKnobs().push_back (this);
}

Knob::~Knob()
{
    auto& k = liveKnobs();
    k.erase (std::remove (k.begin(), k.end(), this), k.end());
}

void Knob::refreshAll()
{
    for (auto* k : liveKnobs()) if (k->isShowing()) k->refreshValue();
}

void Knob::setAccent (juce::Colour c)
{
    slider.setColour (juce::Slider::rotarySliderFillColourId, c);
    repaint();
}

void Knob::setParam (const juce::String& p, const juce::String& labelOverride)
{
    isCustom = false;
    path = p;
    address = cr::ParamAddress::parse (p);
    auto& e = Engine::get();
    const bool ok = e.getParamInfo (address, info);
    setEnabled (ok);
    if (labelOverride.isNotEmpty()) label = labelOverride;
    else if (ok) label = info.name;
    if (ok)
    {
        slider.setRange (0.0, 1.0);
        const float def = info.toNormalised (info.defaultValue);
        slider.setDoubleClickReturnValue (true, def);
    }
    refreshValue();
    repaint();
}

void Knob::setCustom (float min, float max, float def, std::function<float()> get, std::function<void (float)> set,
                      std::function<juce::String (float)> format, bool logScale)
{
    isCustom = true;
    path.clear();
    address = {};
    info = {};
    info.minValue = min; info.maxValue = max; info.defaultValue = def; info.logScale = logScale;
    getter = std::move (get);
    setter = std::move (set);
    formatter = std::move (format);
    slider.setRange (0.0, 1.0);
    slider.setDoubleClickReturnValue (true, info.toNormalised (def));
    setEnabled (true);
    refreshValue();
}

void Knob::refreshValue()
{
    if (slider.isMouseButtonDown()) return;
    float norm = 0.0f;
    if (isCustom) { if (getter) norm = info.toNormalised (getter()); }
    else if (address.isValid())
    {
        auto& e = Engine::get();
        if (! e.getParamInfo (address, info)) { setEnabled (false); return; }
        norm = info.toNormalised (e.getParamValue (address));
    }
    if (std::abs ((float) slider.getValue() - norm) > 1.0e-5f)
    {
        updating = true;
        slider.setValue (norm, juce::dontSendNotification);
        updating = false;
        repaint();
    }
}

void Knob::sliderDragStarted (juce::Slider*) { checkpointed = false; }

void Knob::sliderValueChanged (juce::Slider*)
{
    if (updating) return;
    const float v = info.fromNormalised ((float) slider.getValue());
    if (isCustom) { if (setter) setter (v); }
    else if (address.isValid()) Engine::get().setParamValue (address, v, true);
    repaint();
    if (onUserChange) onUserChange();
}

juce::String Knob::valueText() const
{
    const float v = info.fromNormalised ((float) slider.getValue());
    if (isCustom) return formatter ? formatter (v) : juce::String (v, 2);
    if (! address.isValid()) return "-";
    if (address.owner == cr::ParamAddress::Owner::instrument || ((address.owner == cr::ParamAddress::Owner::layerFx || address.slot >= 0) && info.maxValue == 1.0f && info.name.isNotEmpty() && info.suffix.isEmpty() && info.choices.isEmpty() && info.display == 0))
    {
        // plugin parameters: ask the plugin for its text
        auto& e = Engine::get();
        juce::ignoreUnused (e);
    }
    return info.format (v);
}

void Knob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    const bool learn = path.isNotEmpty() && Engine::get().isMidiLearnArmedFor (path);
    drawCaps (g, label, r.removeFromTop (compact ? 12 : 14), juce::Justification::centred, learn ? Pal::accent : Pal::textFaint, compact ? 9.0f : 9.5f);
    g.setColour (isEnabled() ? Pal::text : Pal::textFaint);
    g.setFont (Fonts::digits (compact ? 10.5f : 11.5f));
    g.drawFittedText (isEnabled() ? valueText() : juce::String ("-"), r.removeFromBottom (compact ? 13 : 15), juce::Justification::centred, 1, 0.7f);
    if (path.isNotEmpty())
    {
        auto& e = Engine::get();
        if (e.hasMidiMapping (path) || e.findMacroFor (path) >= 0)
        {
            g.setColour (Pal::info);
            g.fillEllipse ((float) getWidth() - 7.0f, 2.0f, 5.0f, 5.0f);
        }
    }
}

void Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (compact ? 12 : 14);
    r.removeFromBottom (compact ? 13 : 15);
    slider.setBounds (r);
}

void Knob::mouseDown (const juce::MouseEvent& e) { if (e.mods.isPopupMenu()) showMenu(); }

void Knob::showMenu()
{
    if (isCustom || path.isEmpty())
    {
        juce::PopupMenu m;
        m.addItem (1, "Reset to default");
        m.addItem (2, "Set value...");
        juce::Component::SafePointer<Knob> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int r)
        {
            if (safe == nullptr) return;
            if (r == 1) { safe->slider.setValue (safe->info.toNormalised (safe->info.defaultValue)); }
            if (r == 2)
                askForNumber (safe->label, safe->info.fromNormalised ((float) safe->slider.getValue()), [safe] (double v)
                {
                    if (safe != nullptr) safe->slider.setValue (safe->info.toNormalised ((float) v));
                });
        });
        return;
    }
    auto& e = Engine::get();
    juce::PopupMenu m;
    m.addSectionHeader (e.getParamDisplayName (path));
    m.addItem (1, "MIDI learn (move a controller)");
    m.addItem (2, "Forget MIDI mapping", e.hasMidiMapping (path));
    juce::PopupMenu macros;
    for (int i = 0; i < cr::numMacros; ++i) macros.addItem (100 + i, e.session().macros[(size_t) i].name, true, false);
    m.addSubMenu ("Assign to macro", macros);
    const int macroIdx = e.findMacroFor (path);
    if (macroIdx >= 0) m.addItem (3, "Remove from " + e.session().macros[(size_t) macroIdx].name);
    m.addSeparator();
    const int bank = address.bankIndex();
    m.addItem (4, "Add song automation lane");
    m.addItem (5, "Add pattern automation lane", bank >= 0);
    m.addSeparator();
    m.addItem (6, "Set value...");
    m.addItem (7, "Reset to default");
    juce::Component::SafePointer<Knob> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe, bank, macroIdx] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& en = Engine::get();
        const auto p = safe->path;
        if (r == 1) { en.armMidiLearn (p); Nav::setStatus ("MIDI learn: move a knob or fader on your controller"); }
        else if (r == 2) en.forgetMidiMapping (p);
        else if (r == 3 && macroIdx >= 0)
        {
            auto& binds = en.session().macros[(size_t) macroIdx].bindings;
            for (int i = 0; i < (int) binds.size(); ++i) if (binds[(size_t) i].param == p) { en.unbindMacro (macroIdx, i); break; }
        }
        else if (r >= 100) { en.bindMacro (r - 100, p); Nav::setStatus ("Assigned to " + en.session().macros[(size_t) (r - 100)].name); }
        else if (r == 4 || r == 5)
        {
            en.checkpoint();
            auto* lane = en.findOrCreateLane (p, r == 4, bank);
            if (lane != nullptr && lane->points.empty())
            {
                const juce::ScopedLock sl (en.getLock());
                lane->points.push_back ({ 0, en.getParamNormalised (lane->address) });
            }
            Nav::setStatus ("Automation lane added: " + en.getParamDisplayName (p));
            Nav::showAutomation (r == 4, bank, p);
        }
        else if (r == 6)
            askForNumber (safe->label, en.getParamValue (safe->address), [safe] (double v)
            {
                if (safe != nullptr) { Engine::get().setParamValue (safe->address, (float) v, true); safe->refreshValue(); }
            });
        else if (r == 7) { en.setParamValue (safe->address, safe->info.defaultValue, true); safe->refreshValue(); }
        if (safe != nullptr) safe->repaint();
    });
}

// ============================================================================
// Views registry
// ============================================================================
namespace { std::vector<ViewBase*>& liveViews() { static std::vector<ViewBase*> v; return v; } }
ViewBase::ViewBase() { liveViews().push_back (this); }
ViewBase::~ViewBase() { auto& v = liveViews(); v.erase (std::remove (v.begin(), v.end(), this), v.end()); }
void ViewBase::broadcast()
{
    auto copy = liveViews();
    for (auto* v : copy)
        if (std::find (liveViews().begin(), liveViews().end(), v) != liveViews().end()) v->refresh();
}

// ============================================================================
// Effects
// ============================================================================
void showEffectMenu (juce::Component* target, std::function<void (const juce::String&, int)> chosen, bool instruments)
{
    auto& e = Engine::get();
    juce::PopupMenu m;
    const auto builtins = getBuiltinEffectTypes();
    if (! instruments)
    {
        m.addSectionHeader ("Built-in effects");
        for (int i = 0; i < builtins.size(); ++i) m.addItem (1 + i, builtins[i]);
    }
    const auto list = instruments ? e.plugins.getInstruments() : e.plugins.getEffects();
    juce::PopupMenu plugs;
    std::map<juce::String, juce::PopupMenu> byVendor;
    for (int i = 0; i < list.size(); ++i)
        byVendor[list[i].manufacturerName.isNotEmpty() ? list[i].manufacturerName : juce::String ("Other")].addItem (1000 + i, list[i].name + "  (" + list[i].pluginFormatName + ")");
    for (auto& [vendor, sub] : byVendor) plugs.addSubMenu (vendor, sub);
    if (list.isEmpty()) plugs.addItem (999, "No plugins found - scan in Settings > Plugins", false, false);
    if (instruments) m = plugs;
    else m.addSubMenu ("Plugins", plugs);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [chosen, builtins, list] (int r)
    {
        if (r >= 1 && r <= builtins.size()) chosen (builtins[r - 1], -1);
        else if (r >= 1000 && r - 1000 < list.size()) chosen ({}, r - 1000);
    });
}

EffectCard::EffectCard()
{
    addAndMakeVisible (power);
    addAndMakeVisible (menuBtn);
    addChildComponent (openBtn);
    power.onClick = [this] { if (slot) { const juce::ScopedLock sl (Engine::get().getLock()); slot->bypass = ! power.getToggleState(); } repaint(); };
    menuBtn.onClick = [this]
    {
        juce::PopupMenu m;
        m.addItem (1, "Replace...");
        m.addItem (2, "Remove");
        m.addItem (3, "Move up");
        m.addItem (4, "Move down");
        if (slot && slot->plugin) m.addItem (5, "Open plugin window");
        juce::Component::SafePointer<EffectCard> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menuBtn), [safe] (int r) { if (safe && r > 0 && safe->onAction) safe->onAction (r); });
    };
    openBtn.onClick = [this] { if (slot && slot->plugin) PluginWindow::show (*slot->plugin); };
}

void EffectCard::setTarget (cr::InsertSlot* s, const juce::String& b, int idx, juce::Colour a)
{
    slot = s;
    base = b;
    index = idx;
    accent = a;
    power.accent = a;
    power.setToggleState (s && ! s->bypass, juce::dontSendNotification);
    knobs.clear();
    choices.clear();
    openBtn.setVisible (s && s->plugin);
    if (s && s->builtin)
    {
        for (int i = 0; i < (int) s->builtin->params.size(); ++i)
        {
            const auto& p = s->builtin->params[(size_t) i];
            if (! p.choices.isEmpty())
            {
                auto* c = choices.add (new ChoiceField (p.name));
                c->choices = p.choices;
                c->accent = a;
                const juce::String path = base + juce::String (i);
                c->getIndex = [this, i] { return slot && slot->builtin ? slot->builtin->getChoice (i) : 0; };
                c->setIndex = [this, path] (int v) { Engine::get().setParamValue (cr::ParamAddress::parse (path), (float) v, true); repaint(); };
                addAndMakeVisible (c);
            }
            else
            {
                auto* k = knobs.add (new Knob());
                k->setCompact (true);
                k->setAccent (a);
                k->setParam (base + juce::String (i));
                k->onUserChange = [this] { repaint (graphArea); };
                addAndMakeVisible (k);
            }
        }
    }
    else if (s && s->plugin)
    {
        const int n = juce::jmin (8, s->plugin->getParameters().size());
        for (int i = 0; i < n; ++i)
        {
            auto* k = knobs.add (new Knob());
            k->setCompact (true);
            k->setAccent (a);
            k->setParam (base + juce::String (i));
            addAndMakeVisible (k);
        }
    }
    resized();
    repaint();
}

bool EffectCard::hasGraph() const
{
    return slot != nullptr && slot->builtin != nullptr && slot->builtin->hasFrequencyResponse();
}

int EffectCard::getPreferredHeight (int width) const
{
    const int perRow = juce::jmax (1, (width - 16) / 64);
    const int items = knobs.size() + choices.size();
    const int rows = (items + perRow - 1) / perRow;
    return 36 + (hasGraph() ? 104 : 0) + rows * 62 + (openBtn.isVisible() ? 30 : 0) + 8;
}

// Frequency response of a filter / EQ insert: log 20 Hz - 20 kHz, +-24 dB, live as you turn it.
void EffectCard::paintResponse (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (Pal::bg);
    g.fillRoundedRectangle (r, 4.0f);
    auto plot = r.reduced (4.0f, 4.0f).withTrimmedBottom (10.0f);
    const double lo = 20.0, hi = 20000.0;
    auto xOf = [&plot, lo, hi] (double f) { return plot.getX() + plot.getWidth() * (float) (std::log (f / lo) / std::log (hi / lo)); };
    auto fOf = [&plot, lo, hi] (float x) { return lo * std::pow (hi / lo, (double) ((x - plot.getX()) / plot.getWidth())); };
    auto yOf = [&plot] (double db) { return plot.getCentreY() - (float) (juce::jlimit (-24.0, 24.0, db) / 24.0) * plot.getHeight() * 0.5f; };
    g.setFont (Fonts::ui (8.5f));
    for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        g.setColour (Pal::outline.withAlpha (0.7f));
        g.fillRect (xOf (f), plot.getY(), 1.0f, plot.getHeight());
        if (f == 100.0 || f == 1000.0 || f == 10000.0)
        {
            g.setColour (Pal::textFaint);
            g.drawText (f >= 1000.0 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f), juce::Rectangle<float> (xOf (f) - 14.0f, plot.getBottom() + 1.0f, 28.0f, 9.0f), juce::Justification::centred);
        }
    }
    for (int db : { -12, 0, 12 })
    {
        g.setColour (db == 0 ? Pal::line : Pal::outline.withAlpha (0.5f));
        g.fillRect (plot.getX(), yOf (db), plot.getWidth(), 1.0f);
    }
    if (! hasGraph()) return;
    const double sr = juce::jmax (22050.0, Engine::get().getSampleRate());
    auto* fx = slot->builtin.get();
    juce::Path p;
    for (float x = plot.getX(); x <= plot.getRight(); x += 1.5f)
    {
        const float y = yOf (fx->getResponseDb (fOf (x), sr));
        if (x == plot.getX()) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    juce::Path fill (p);
    fill.lineTo (plot.getRight(), plot.getBottom());
    fill.lineTo (plot.getX(), plot.getBottom());
    fill.closeSubPath();
    const auto c = slot->bypass ? Pal::textFaint : accent;
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (plot.toNearestInt());
    g.setColour (c.withAlpha (0.18f));
    g.fillPath (fill);
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (1.6f));
}

void EffectCard::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Pal::surface);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (Pal::outline);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    g.setColour (slot && ! slot->bypass ? Pal::text : Pal::textFaint);
    g.setFont (Fonts::ui (13.0f, true));
    g.drawFittedText (slot ? slot->getName() : juce::String ("Empty"), juce::Rectangle<int> (38, 6, getWidth() - 76, 22), juce::Justification::centredLeft, 1);
    if (hasGraph()) paintResponse (g, graphArea.toFloat());
}

void EffectCard::resized()
{
    auto r = getLocalBounds().reduced (8, 6);
    auto top = r.removeFromTop (24);
    power.setBounds (top.removeFromLeft (24));
    menuBtn.setBounds (top.removeFromRight (24));
    r.removeFromTop (4);
    graphArea = {};
    if (hasGraph()) { graphArea = r.removeFromTop (98); r.removeFromTop (6); }
    if (openBtn.isVisible()) { openBtn.setBounds (r.removeFromTop (26).withWidth (juce::jmin (160, r.getWidth()))); r.removeFromTop (4); }
    const int perRow = juce::jmax (1, r.getWidth() / 64);
    const int w = r.getWidth() / perRow;
    int i = 0;
    auto place = [&] (juce::Component* c)
    {
        const int row = i / perRow, col = i % perRow;
        c->setBounds (r.getX() + col * w, r.getY() + row * 62, w, 58);
        ++i;
    };
    for (auto* c : choices) place (c);
    for (auto* k : knobs) place (k);
}

void EffectCard::refreshValues()
{
    for (auto* k : knobs) k->refreshValue();
    for (auto* c : choices) c->repaint();
    power.setToggleState (slot && ! slot->bypass, juce::dontSendNotification);
    if (hasGraph()) repaint (graphArea);
}

InsertChainView::InsertChainView()
{
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (6);
    addAndMakeVisible (viewport);
    content.addAndMakeVisible (addBtn);
    addBtn.onClick = [this] { addEffect (-1); };
    emptyLabel.setFont (Fonts::ui (12.5f));
    emptyLabel.setColour (juce::Label::textColourId, Pal::textFaint);
    emptyLabel.setJustificationType (juce::Justification::centred);
    content.addChildComponent (emptyLabel);
}

void InsertChainView::setChain (std::vector<cr::InsertSlot>* c, const juce::String& b, juce::Colour a)
{
    chain = c; base = b; accent = a;
    lfxBank = lfxPad = lfxLayer = -1;
    refresh();
}

void InsertChainView::setLayerFx (int bank, int pad, int layer, juce::Colour a)
{
    chain = nullptr;
    lfxBank = bank; lfxPad = pad; lfxLayer = layer; accent = a;
    refresh();
}

void InsertChainView::clear() { chain = nullptr; lfxBank = -1; refresh(); }

void InsertChainView::refresh()
{
    cards.clear();
    auto& e = Engine::get();
    if (chain != nullptr)
    {
        for (int i = 0; i < (int) chain->size(); ++i)
        {
            auto* c = cards.add (new EffectCard());
            c->setTarget (&(*chain)[(size_t) i], base + "fx" + juce::String (i) + "/", i, accent);
            c->onAction = [this, i] (int a) { handleAction (i, a); };
            content.addAndMakeVisible (c);
        }
        addBtn.setVisible (true);
        addBtn.setButtonText ("+ Add effect");
    }
    else if (lfxBank >= 0)
    {
        auto& pad = e.session().banks[(size_t) lfxBank].pads[(size_t) lfxPad];
        if (juce::isPositiveAndBelow (lfxLayer, (int) pad.layers.size()))
        {
            auto& layer = pad.layers[(size_t) lfxLayer];
            for (int i = 0; i < cr::numLayerFx; ++i)
            {
                if (layer.fx[(size_t) i].isEmpty()) continue;
                auto* c = cards.add (new EffectCard());
                c->setTarget (&layer.fx[(size_t) i], "lfx/" + juce::String (lfxBank) + "/" + juce::String (lfxPad) + "/" + juce::String (lfxLayer) + "/" + juce::String (i) + "/", i, accent);
                c->onAction = [this, i] (int a) { handleAction (i, a); };
                content.addAndMakeVisible (c);
            }
            int used = 0;
            for (auto& f : layer.fx) if (! f.isEmpty()) ++used;
            addBtn.setVisible (used < cr::numLayerFx);
            addBtn.setButtonText ("+ Add layer effect (" + juce::String (used) + "/" + juce::String (cr::numLayerFx) + ")");
        }
        else addBtn.setVisible (false);
    }
    else addBtn.setVisible (false);
    emptyLabel.setText (chain == nullptr && lfxBank < 0 ? "Nothing selected" : "No effects yet", juce::dontSendNotification);
    emptyLabel.setVisible (cards.isEmpty());
    resized();
}

void InsertChainView::refreshValues() { for (auto* c : cards) c->refreshValues(); }

void InsertChainView::paint (juce::Graphics&) {}

void InsertChainView::resized()
{
    viewport.setBounds (getLocalBounds());
    const int w = getWidth() - 8;
    int y = 0;
    for (auto* c : cards)
    {
        const int h = c->getPreferredHeight (w);
        c->setBounds (0, y, w, h);
        y += h + 8;
    }
    if (cards.isEmpty()) { emptyLabel.setBounds (0, y, w, 40); y += 44; }
    addBtn.setBounds (0, y, w, 32);
    y += 40;
    content.setSize (w, y);
}

void InsertChainView::addEffect (int slot)
{
    juce::Component::SafePointer<InsertChainView> safe (this);
    showEffectMenu (&addBtn, [safe, slot] (const juce::String& builtin, int pluginIndex)
    {
        if (safe == nullptr) return;
        auto& e = Engine::get();
        juce::String err;
        if (safe->chain != nullptr)
        {
            if (builtin.isNotEmpty()) e.setInsertBuiltin (*safe->chain, slot, builtin);
            else if (! e.setInsertPlugin (*safe->chain, slot, e.plugins.getEffects()[pluginIndex], err)) showError ("Plugin", err);
        }
        else if (safe->lfxBank >= 0)
        {
            auto& pad = e.session().banks[(size_t) safe->lfxBank].pads[(size_t) safe->lfxPad];
            if (! juce::isPositiveAndBelow (safe->lfxLayer, (int) pad.layers.size())) return;
            int s = slot;
            if (s < 0)
                for (int i = 0; i < cr::numLayerFx; ++i) if (pad.layers[(size_t) safe->lfxLayer].fx[(size_t) i].isEmpty()) { s = i; break; }
            if (s < 0) return;
            if (builtin.isNotEmpty()) e.setLayerFx (safe->lfxBank, safe->lfxPad, safe->lfxLayer, s, builtin);
            else if (! e.setLayerFxPlugin (safe->lfxBank, safe->lfxPad, safe->lfxLayer, s, e.plugins.getEffects()[pluginIndex], err)) showError ("Plugin", err);
        }
        safe->refresh();
        if (safe->onChanged) safe->onChanged();
    });
}

void InsertChainView::handleAction (int slot, int action)
{
    auto& e = Engine::get();
    if (action == 1) { addEffect (slot); return; }
    if (chain != nullptr)
    {
        if (action == 2) e.removeInsert (*chain, slot);
        else if (action == 3) e.moveInsert (*chain, slot, slot - 1);
        else if (action == 4) e.moveInsert (*chain, slot, slot + 1);
        else if (action == 5 && (*chain)[(size_t) slot].plugin) PluginWindow::show (*(*chain)[(size_t) slot].plugin);
    }
    else if (lfxBank >= 0)
    {
        auto& pad = e.session().banks[(size_t) lfxBank].pads[(size_t) lfxPad];
        if (! juce::isPositiveAndBelow (lfxLayer, (int) pad.layers.size())) return;
        auto& fx = pad.layers[(size_t) lfxLayer].fx;
        if (action == 2) e.clearLayerFx (lfxBank, lfxPad, lfxLayer, slot);
        else if (action == 3 || action == 4)
        {
            const int to = slot + (action == 3 ? -1 : 1);
            if (juce::isPositiveAndBelow (to, cr::numLayerFx)) { const juce::ScopedLock sl (e.getLock()); std::swap (fx[(size_t) slot], fx[(size_t) to]); }
        }
        else if (action == 5 && fx[(size_t) slot].plugin) PluginWindow::show (*fx[(size_t) slot].plugin);
    }
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<InsertChainView> (this)]
    {
        if (safe) { safe->refresh(); if (safe->onChanged) safe->onChanged(); }
    });
}

// ============================================================================
// Helpers
// ============================================================================
juce::Rectangle<int> drawSection (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title)
{
    g.setColour (Pal::panel);
    g.fillRoundedRectangle (r.toFloat(), 6.0f);
    g.setColour (Pal::outline);
    g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 6.0f, 1.0f);
    auto c = r.reduced (10, 8);
    if (title.isNotEmpty())
    {
        drawCaps (g, title, c.removeFromTop (16), juce::Justification::centredLeft, Pal::textDim, 10.5f);
        c.removeFromTop (4);
    }
    return c;
}

void drawBankBadge (juce::Graphics& g, juce::Rectangle<float> r, int bank, bool bright)
{
    auto& e = Engine::get();
    const auto& b = e.session().banks[(size_t) juce::jlimit (0, cr::numBanks - 1, bank)];
    auto c = b.created ? b.colour : cr::bankColour (bank);
    g.setColour (bright ? c : c.withAlpha (0.3f));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (bright ? Pal::bg : c);
    g.setFont (Fonts::heavy (r.getHeight() * 0.62f));
    g.drawText (cr::bankLetter (bank), r, juce::Justification::centred);
    if (bank >= cr::banksPerGroup)
    {
        g.setFont (Fonts::ui (r.getHeight() * 0.3f, true));
        g.drawText (juce::String (bank / cr::banksPerGroup + 1), r.reduced (2.0f, 1.0f), juce::Justification::bottomRight);
    }
}

void drawCaps (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r, juce::Justification j, juce::Colour c, float size)
{
    g.setColour (c);
    g.setFont (Fonts::caps (size));
    g.drawFittedText (text.toUpperCase(), r, j, 1, 0.8f);
}

juce::String noteName (int note)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    note = juce::jlimit (0, 127, note);
    return juce::String (names[note % 12]) + juce::String (note / 12 - 2);
}

juce::Colour padColour (int bank, int pad)
{
    auto& b = Engine::get().session().banks[(size_t) bank];
    const auto& p = b.pads[(size_t) pad];
    return p.colour.getAlpha() > 0 ? p.colour : b.colour;
}

cr::Bank& currentBank() { auto& e = Engine::get(); return e.session().banks[(size_t) e.selectedBank.load()]; }
cr::Pad& currentPad() { auto& e = Engine::get(); return currentBank().pads[(size_t) e.selectedPad.load()]; }
cr::Layer* currentLayer()
{
    auto& pad = currentPad();
    if (pad.layers.empty()) return nullptr;
    return &pad.layers[(size_t) juce::jlimit (0, (int) pad.layers.size() - 1, Engine::get().selectedLayer.load())];
}

juce::String layerPath (int bank, int pad, int layer, int field)
{
    return "layer/" + juce::String (bank) + "/" + juce::String (pad) + "/" + juce::String (layer) + "/" + juce::String (field);
}

juce::String stripPath (int key)
{
    if (key >= 0 && key < cr::numBanks) return "bank/" + juce::String (key) + "/";
    if (key >= 1000 && key < 1000 + cr::maxAudioTracks) return "audio/" + juce::String (key - 1000) + "/";
    if (key >= 2000 && key < 2000 + cr::numAux) return "aux/" + juce::String (key - 2000) + "/";
    if (key == 3000) return "master/";
    if (key >= 10000) { const int k = key - 10000; return "pad/" + juce::String (k / cr::numPads) + "/" + juce::String (k % cr::numPads) + "/"; }
    return {};
}

bool isAudioFile (const juce::File& f)
{
    return f.hasFileExtension (Engine::importableExtensions());
}

namespace
{
// Loads stems on a background thread with a progress window, then adds one track per
// file on the message thread. The UI keeps running the whole time.
class StemImportJob : public juce::ThreadWithProgressWindow
{
public:
    StemImportJob (juce::StringArray f, int tick)
        : juce::ThreadWithProgressWindow ("Importing audio", true, true), files (std::move (f)), originTick (tick) {}

    void run() override
    {
        for (int i = 0; i < files.size() && ! threadShouldExit(); ++i)
        {
            const juce::File f (files[i]);
            setStatusMessage ("Loading " + f.getFileName() + "  (" + juce::String (i + 1) + " of " + juce::String (files.size()) + ")");
            setProgress ((double) i / files.size());
            stems.push_back (Engine::get().prepareStem (f));
        }
        setProgress (1.0);
    }

    void threadComplete (bool cancelled) override
    {
        juce::String err;
        int added = 0;
        if (! cancelled) added = Engine::get().addStemTracks (stems, originTick, err);
        stems.clear();
        ViewBase::broadcast();
        if (! cancelled) Nav::setStatus ("Imported " + juce::String (added) + (added == 1 ? " track" : " tracks"));
        if (err.isNotEmpty()) showError ("Some files weren't imported", err);
        delete this;
    }

private:
    juce::StringArray files;
    int originTick = 0;
    std::vector<Engine::Stem> stems;
};
}

// ============================================================================
// Resampling menus
// ============================================================================
namespace
{
class BounceJob : public juce::ThreadWithProgressWindow
{
public:
    BounceJob (int k, BounceOptions o) : juce::ThreadWithProgressWindow (o.freeze ? "Freezing..." : "Bouncing...", true, true), key (k), options (o) {}
    void run() override
    {
        track = Engine::get().bounceToTrack (key, options, error, [this] (float p) { setProgress (p); return ! threadShouldExit(); });
    }
    void threadComplete (bool) override
    {
        ViewBase::broadcast();
        if (track < 0) showError (options.freeze ? "Freeze" : "Bounce", error.isNotEmpty() ? error : juce::String ("Cancelled."));
        else Nav::setStatus (options.freeze ? "Frozen: the new track plays it; Unfreeze to edit again" : "Bounced to a new audio track");
        delete this;
    }
private:
    int key, track = -1;
    BounceOptions options;
    juce::String error;
};
}

void addResampleItems (juce::PopupMenu& m, int key)
{
    auto& e = Engine::get();
    if (key == 3000 || e.getStrip (key) == nullptr) return;
    const bool frozen = e.isFrozen (key);
    juce::PopupMenu r;
    r.addItem (901, "Bounce to a new audio track", ! frozen);
    r.addItem (902, "Bounce with sends printed (AUX returns included)", ! frozen);
    if (key >= 1000 && key < 2000) r.addItem (903, "Bounce and remove this track", ! frozen);
    if (key < 10000)
    {
        r.addSeparator();
        if (frozen) r.addItem (905, "Unfreeze");
        else r.addItem (904, "Freeze (render it, save the CPU)");
    }
    m.addSubMenu ("Resample", r);
}

bool handleResampleItem (int r, int key)
{
    if (r < 901 || r > 905) return false;
    auto& e = Engine::get();
    if (r == 905) { e.unfreeze (key); ViewBase::broadcast(); return true; }
    BounceOptions o;
    o.printSends = r == 902;
    o.removeSource = r == 903;
    o.freeze = r == 904;
    if (e.getSongLengthTicks() <= 0) { showError ("Resample", "There's nothing in the song to render yet. Arrange some clips first."); return true; }
    (new BounceJob (key, o))->launchThread();
    return true;
}

void addLiveSourceItems (juce::PopupMenu& internal, int current, int excludeTrack)
{
    auto& e = Engine::get();
    auto& s = e.session();
    juce::PopupMenu tracks, pads;
    for (int t = 0; t < cr::maxAudioTracks; ++t)
        if (s.audioTracks[(size_t) t].created && t != excludeTrack)
            tracks.addItem (1000 + t, s.audioTracks[(size_t) t].name, true, current == 400 + t);
    const int b = e.selectedBank;
    for (int p = 0; p < cr::numPads; ++p)
    {
        const auto& pad = s.banks[(size_t) b].pads[(size_t) p];
        if (! pad.hasSound()) continue;
        const int key = b * cr::numPads + p;
        pads.addItem (20000 + key, cr::bankLetter (b) + "." + juce::String (p + 1) + "  " + (pad.name.isNotEmpty() ? pad.name : juce::String ("Pad")),
                      true, current == 10000 + key);
    }
    if (tracks.getNumItems() > 0) internal.addSubMenu ("Audio track", tracks);
    if (pads.getNumItems() > 0) internal.addSubMenu ("Pad / instrument (" + cr::bankName (b) + ")", pads);
}

int liveSourceForMenuResult (int r)
{
    if (r >= 20000) return 10000 + (r - 20000);
    if (r >= 1000 && r < 1000 + cr::maxAudioTracks) return 400 + (r - 1000);
    return -1;
}

juce::String liveSourceName (int source)
{
    auto& s = Engine::get().session();
    if (source >= 400 && source < 400 + cr::maxAudioTracks) return s.audioTracks[(size_t) (source - 400)].name;
    if (source >= 10000)
    {
        const int k = source - 10000;
        const auto& pad = s.banks[(size_t) (k / cr::numPads)].pads[(size_t) (k % cr::numPads)];
        return cr::bankLetter (k / cr::numPads) + "." + juce::String (k % cr::numPads + 1) + (pad.name.isNotEmpty() ? " " + pad.name : juce::String());
    }
    return {};
}

static const juce::Identifier midiActionId ("crMidiAction");
void setMidiAction (juce::Component& c, int action) { c.getProperties().set (midiActionId, action); }
int getMidiAction (const juce::Component& c) { return c.getProperties().getWithDefault (midiActionId, -1); }

// ============================================================================
// Banks menu (Editor header): pick, create, duplicate, copy / paste banks
// ============================================================================
void showBanksMenu (juce::Component* target)
{
    auto& e = Engine::get();
    auto& s = e.session();
    const int cur = e.selectedBank;
    juce::PopupMenu m;
    m.addSectionHeader ("Banks");
    juce::PopupMenu all;
    int created = 0;
    for (int g = 0; g < cr::numBanks / 8; ++g)
    {
        juce::PopupMenu group;
        bool any = false;
        for (int i = 0; i < 8; ++i)
        {
            const int b = g * 8 + i;
            const auto& bk = s.banks[(size_t) b];
            if (bk.created) { any = true; ++created; }
            group.addItem (1000 + b, cr::bankLetter (b) + "  " + (bk.created ? bk.name : juce::String ("(empty)")), true, b == cur);
        }
        all.addSubMenu ("Group " + juce::String (g + 1) + (any ? "  *" : ""), group, true, nullptr, g == cur / 8);
    }
    for (int b = 0; b < cr::numBanks; ++b)
        if (s.banks[(size_t) b].created)
            m.addItem (1000 + b, cr::bankLetter (b) + "  " + s.banks[(size_t) b].name, true, b == cur);
    m.addSubMenu ("All 128 banks", all);
    m.addSeparator();
    const int freeBank = e.firstFreeBank();
    m.addItem (1, "New bank", freeBank >= 0);
    m.addItem (2, "Duplicate " + s.banks[(size_t) cur].name + " to a new bank", freeBank >= 0 && s.banks[(size_t) cur].created);
    m.addSeparator();
    m.addItem (3, "Copy bank", s.banks[(size_t) cur].created);
    m.addItem (4, "Paste over " + s.banks[(size_t) cur].name, e.hasBankClipboard());
    m.addItem (5, "Paste as a new bank", e.hasBankClipboard() && freeBank >= 0);
    m.addSeparator();
    m.addItem (6, "Rename bank...");
    m.addItem (7, "Clear all pads in this bank");
    m.addItem (8, "Delete bank", s.banks[(size_t) cur].created);
    juce::ignoreUnused (created);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [] (int r)
    {
        auto& en = Engine::get();
        auto& ss = en.session();
        const int b = en.selectedBank;
        juce::String err;
        auto go = [&en] (int bank) { en.createBank (bank); en.selectedBank = bank; en.selectedPad = 0; };
        if (r >= 1000) go (r - 1000);
        else if (r == 1) { const int f = en.firstFreeBank(); if (f >= 0) { en.checkpoint(); go (f); } }
        else if (r == 2) { if (en.copyBank (b, err)) { const int f = en.firstFreeBank(); if (f >= 0 && en.pasteBank (f, err)) go (f); } }
        else if (r == 3) { if (en.copyBank (b, err)) Nav::setStatus ("Copied " + ss.banks[(size_t) b].name); }
        else if (r == 4) en.pasteBank (b, err);
        else if (r == 5) { const int f = en.firstFreeBank(); if (f >= 0 && en.pasteBank (f, err)) go (f); }
        else if (r == 6) askForText ("Bank name", ss.banks[(size_t) b].name, [b] (const juce::String& n) { auto& bk = Engine::get().session().banks[(size_t) b]; bk.name = n; bk.strip.name = n; ViewBase::broadcast(); });
        else if (r == 7) { for (int p = 0; p < cr::numPads; ++p) if (ss.banks[(size_t) b].pads[(size_t) p].hasSound()) en.clearPad (b, p); }
        else if (r == 8) { en.checkpoint(); en.deleteBank (b); }
        if (err.isNotEmpty()) showError ("Banks", err);
        ViewBase::broadcast();
    });
}

void importStems (const juce::StringArray& files, int originTick)
{
    juce::StringArray audio;
    for (auto& f : files) if (isAudioFile (juce::File (f))) audio.add (f);
    if (audio.isEmpty()) return;
    (new StemImportJob (audio, originTick))->launchThread();
}

void showError (const juce::String& title, const juce::String& message)
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon)
                                      .withTitle (title).withMessage (message).withButton ("OK"), nullptr);
}

void askForText (const juce::String& title, const juce::String& initial, std::function<void (const juce::String&)> done)
{
    auto* w = new juce::AlertWindow (title, {}, juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("t", initial);
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, done] (int r)
    {
        if (r == 1) done (w->getTextEditorContents ("t"));
    }), true);
    if (auto* ed = w->getTextEditor ("t")) { ed->selectAll(); ed->grabKeyboardFocus(); }
}

void askForNumber (const juce::String& title, double initial, std::function<void (double)> done)
{
    askForText (title, juce::String (initial, 3).trimCharactersAtEnd ("0").trimCharactersAtEnd ("."), [done] (const juce::String& s)
    {
        if (s.trim().isNotEmpty()) done (s.getDoubleValue());
    });
}

void showPadColourMenu (int bank, int pad, juce::Component* target)
{
    juce::PopupMenu m;
    const auto& cols = Pal::padColours();
    m.addItem (1, "Bank colour");
    for (int i = 0; i < cols.size(); ++i)
    {
        juce::Image img (juce::Image::ARGB, 14, 14, true);
        { juce::Graphics g (img); g.setColour (cols[i]); g.fillRoundedRectangle (0, 0, 14, 14, 3); }
        juce::PopupMenu::Item it ("Colour " + juce::String (i + 1));
        it.itemID = 10 + i;
        it.image = std::make_unique<juce::DrawableImage> (img);
        m.addItem (std::move (it));
    }
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [bank, pad] (int r)
    {
        if (r == 0) return;
        auto& p = Engine::get().session().banks[(size_t) bank].pads[(size_t) pad];
        p.colour = r == 1 ? juce::Colour() : Pal::padColours()[r - 10];
        ViewBase::broadcast();
    });
}

juce::StringArray auxNames()
{
    juce::StringArray n;
    for (int a = 0; a < Engine::get().session().auxCount; ++a) n.add (Engine::get().session().aux[(size_t) a].name);
    return n;
}

// ============================================================================
void XYPad::paint (juce::Graphics& g)
{
    auto& e = Engine::get();
    auto r = getLocalBounds().toFloat();
    juce::ColourGradient grad (colourA, r.getX(), r.getBottom(), colourB, r.getRight(), r.getY(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (juce::Colours::black.withAlpha (0.12f));
    for (float x = r.getX() + 8.0f; x < r.getRight(); x += 8.0f)
        for (float y = r.getY() + 8.0f; y < r.getBottom(); y += 8.0f) g.fillRect (x, y, 1.0f, 1.0f);
    const auto& mx = e.session().macros[(size_t) macroX];
    const auto& my = e.session().macros[(size_t) macroY];
    const float px = r.getX() + mx.value * r.getWidth();
    const float py = r.getBottom() - my.value * r.getHeight();
    g.setColour (juce::Colours::white);
    g.fillEllipse (px - 6.0f, py - 6.0f, 12.0f, 12.0f);
    g.setColour (juce::Colours::white.withAlpha (0.25f));
    g.drawEllipse (px - 11.0f, py - 11.0f, 22.0f, 22.0f, 1.5f);
    drawCaps (g, "MACRO " + juce::String (macroX + 1) + ": " + mx.name, getLocalBounds().reduced (8, 6), juce::Justification::bottomLeft, Pal::text.withAlpha (0.85f), 9.5f);
    auto ry = getLocalBounds().reduced (6, 8);
    g.saveState();
    g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, (float) ry.getX() + 8.0f, (float) ry.getBottom()));
    drawCaps (g, "MACRO " + juce::String (macroY + 1) + ": " + my.name, juce::Rectangle<int> (ry.getX() + 8, ry.getBottom() - 6, ry.getHeight(), 14), juce::Justification::centredLeft, Pal::text.withAlpha (0.85f), 9.5f);
    g.restoreState();
}

void XYPad::update (const juce::MouseEvent& ev)
{
    auto& e = Engine::get();
    e.setMacro (macroX, juce::jlimit (0.0f, 1.0f, (float) ev.x / (float) juce::jmax (1, getWidth())));
    e.setMacro (macroY, juce::jlimit (0.0f, 1.0f, 1.0f - (float) ev.y / (float) juce::jmax (1, getHeight())));
    repaint();
}
