#pragma once
// Kolot's own visual identity: a concert hall at night —
// ink-blue lacquer panels, ivory engraving, warm stage-light amber.

#include <juce_gui_basics/juce_gui_basics.h>

namespace kolot
{
namespace Colours
{
    const juce::Colour bg        { 0xff0f1118 };
    const juce::Colour panel     { 0xff181b26 };
    const juce::Colour panelHi   { 0xff222636 };
    const juce::Colour edge      { 0xff2e3347 };
    const juce::Colour ivory     { 0xffefe6d5 };
    const juce::Colour ivoryDim  { 0xff9a9384 };
    const juce::Colour amber     { 0xffe8a33d };
    const juce::Colour amberHot  { 0xffffc46b };
    const juce::Colour rose      { 0xffc8665a };
}

class KolotLookAndFeel : public juce::LookAndFeel_V4
{
public:
    KolotLookAndFeel()
    {
        using namespace Colours;
        setColour (juce::ResizableWindow::backgroundColourId, bg);
        setColour (juce::ComboBox::backgroundColourId, panelHi);
        setColour (juce::ComboBox::textColourId, ivory);
        setColour (juce::ComboBox::outlineColourId, edge);
        setColour (juce::ComboBox::arrowColourId, amber);
        setColour (juce::PopupMenu::backgroundColourId, panel);
        setColour (juce::PopupMenu::textColourId, ivory);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, amber.withAlpha (0.25f));
        setColour (juce::PopupMenu::highlightedTextColourId, ivory);
        setColour (juce::Label::textColourId, ivory);
        setColour (juce::TextButton::textColourOffId, ivoryDim);
        setColour (juce::TextButton::textColourOnId, bg);
        setColour (juce::Slider::textBoxTextColourId, ivory);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
        setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        setColour (juce::TextEditor::focusedOutlineColourId, amber);
        setColour (juce::TooltipWindow::backgroundColourId, panelHi);
        setColour (juce::TooltipWindow::textColourId, ivory);
    }

    static juce::Font font (float h, bool bold = false)
    {
        return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (14.0f); }
    juce::Font getPopupMenuFont() override               { return font (14.0f); }
    juce::Font getTextButtonFont (juce::TextButton& b, int h) override
    {
        return font (juce::jmin (15.0f, h * 0.42f), b.getProperties()["big"]);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float a0, float a1, juce::Slider& s) override
    {
        using namespace Colours;
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.0f);
        const float size = juce::jmin (r.getWidth(), r.getHeight());
        r = r.withSizeKeepingCentre (size, size);
        const auto c = r.getCentre();
        const float rad = size * 0.5f;
        const float ang = a0 + pos * (a1 - a0);

        // track + value arc
        juce::Path track, val;
        track.addCentredArc (c.x, c.y, rad - 2, rad - 2, 0, a0, a1, true);
        g.setColour (edge);
        g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
        const float from = bipolar ? (a0 + a1) * 0.5f : a0;
        val.addCentredArc (c.x, c.y, rad - 2, rad - 2, 0, juce::jmin (from, ang), juce::jmax (from, ang), true);
        g.setColour (amber.withAlpha (s.isEnabled() ? 1.0f : 0.3f));
        g.strokePath (val, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // knob body: dark lacquer with a brass rim
        const float kr = rad - 9.0f;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3f52), c.x, c.y - kr,
                                                 juce::Colour (0xff11131b), c.x, c.y + kr, false));
        g.fillEllipse (c.x - kr, c.y - kr, kr * 2, kr * 2);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffc9a35d), c.x, c.y - kr,
                                                 juce::Colour (0xff6b5428), c.x, c.y + kr, false));
        g.drawEllipse (c.x - kr, c.y - kr, kr * 2, kr * 2, 1.5f);

        // pointer
        juce::Path p;
        p.addRoundedRectangle (-1.5f, -kr + 4.0f, 3.0f, kr * 0.45f, 1.5f);
        g.setColour (ivory);
        g.fillPath (p, juce::AffineTransform::rotation (ang).translated (c.x, c.y));
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                               bool over, bool down) override
    {
        using namespace Colours;
        auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        const bool on = b.getToggleState();
        const bool big = b.getProperties()["big"];
        const float cr = big ? 10.0f : 6.0f;

        if (big)
        {
            auto top = b.isEnabled() ? (over ? amberHot : amber) : edge;
            g.setGradientFill (juce::ColourGradient (top, r.getX(), r.getY(),
                                                     top.darker (0.45f), r.getX(), r.getBottom(), false));
            g.fillRoundedRectangle (r, cr);
            const float prog = (float) (double) b.getProperties().getWithDefault ("progress", -1.0);
            if (prog >= 0.0f)
            {
                g.setColour (bg.withAlpha (0.55f));
                g.fillRoundedRectangle (r, cr);
                g.setColour (amber);
                g.fillRoundedRectangle (r.withWidth (juce::jmax (cr * 2, r.getWidth() * prog)), cr);
            }
            g.setColour (juce::Colours::white.withAlpha (0.18f));
            g.drawRoundedRectangle (r.reduced (1.0f), cr, 1.0f);
            return;
        }

        g.setColour (on ? amber : (over ? panelHi.brighter (0.1f) : panelHi));
        g.fillRoundedRectangle (r, cr);
        g.setColour (on ? amberHot : edge);
        g.drawRoundedRectangle (r, cr, 1.0f);
        if (down) { g.setColour (juce::Colours::black.withAlpha (0.2f)); g.fillRoundedRectangle (r, cr); }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        using namespace Colours;
        const bool big = b.getProperties()["big"];
        g.setFont (getTextButtonFont (b, b.getHeight()));
        juce::Colour c = big ? bg : (b.getToggleState() ? bg : ivory);
        if (! b.isEnabled()) c = c.withAlpha (0.35f);
        g.setColour (c);
        g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (6, 2), juce::Justification::centred, 2);
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        using namespace Colours;
        auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
        g.setColour (panelHi);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (box.hasKeyboardFocus (true) ? amber : edge);
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
        juce::Path arrow;
        const float ax = (float) w - 16.0f, ay = h * 0.5f;
        arrow.addTriangle (ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
        g.setColour (amber);
        g.fillPath (arrow);
    }
};
} // namespace kolot
