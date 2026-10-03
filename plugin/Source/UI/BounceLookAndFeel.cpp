#include "UI/BounceLookAndFeel.h"

namespace bounce::ui
{

juce::Colour accentOf (const juce::Component& c, juce::Colour fallback)
{
    const auto& v = c.getProperties()[accentProperty];
    return v.isVoid() ? fallback : juce::Colour (static_cast<juce::uint32> (static_cast<juce::int64> (v)));
}

void setAccent (juce::Component& c, juce::Colour accent)
{
    c.getProperties().set (accentProperty, static_cast<juce::int64> (accent.getARGB()));
    c.repaint();
}

juce::Font uiFont (float height, bool bold)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

void drawGlow (juce::Graphics& g, const juce::Path& p, juce::Colour colour, float radius, float strokeWidth)
{
    // Layered translucent strokes: cheap, resolution independent and looks like a bloom.
    for (int i = 4; i >= 1; --i)
    {
        const float w = strokeWidth + radius * static_cast<float> (i) / 2.0f;
        g.setColour (colour.withAlpha (0.06f * static_cast<float> (5 - i)));
        g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

void drawPanel (juce::Graphics& g, juce::Rectangle<float> b, const juce::String& title, juce::Colour accent)
{
    const float r = 10.0f;
    g.setGradientFill (juce::ColourGradient (Colours::panelLight, b.getX(), b.getY(),
                                             Colours::panel, b.getX(), b.getBottom(), false));
    g.fillRoundedRectangle (b, r);

    // Inner shadow along the top edge + hairline outline.
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.05f), b.getX(), b.getY(),
                                             juce::Colours::transparentWhite, b.getX(), b.getY() + 24.0f, false));
    g.fillRoundedRectangle (b.reduced (1.0f), r);
    g.setColour (Colours::outline);
    g.drawRoundedRectangle (b.reduced (0.5f), r, 1.0f);

    if (title.isNotEmpty())
    {
        auto header = b.reduced (14.0f, 10.0f).removeFromTop (18.0f);
        juce::Path bar;
        bar.addRoundedRectangle (header.removeFromLeft (3.0f).withSizeKeepingCentre (3.0f, 14.0f), 1.5f);
        drawGlow (g, bar, accent, 6.0f, 1.0f);
        g.setColour (accent);
        g.fillPath (bar);

        g.setColour (Colours::text);
        g.setFont (uiFont (13.0f, true));
        g.drawText (title.toUpperCase(), header.withTrimmedLeft (8.0f), juce::Justification::centredLeft);
    }
}

//==============================================================================
BounceLookAndFeel::BounceLookAndFeel()
{
    using namespace juce;
   #if JUCE_WINDOWS
    setDefaultSansSerifTypefaceName ("Segoe UI"); // JUCE's Windows default (Verdana) is much wider
   #endif
    setColour (ResizableWindow::backgroundColourId, Colours::background);
    setColour (Label::textColourId, Colours::text);
    setColour (Slider::rotarySliderFillColourId, Colours::chords);
    setColour (Slider::textBoxTextColourId, Colours::text);
    setColour (ComboBox::backgroundColourId, Colours::well);
    setColour (ComboBox::textColourId, Colours::text);
    setColour (ComboBox::outlineColourId, Colours::outline);
    setColour (ComboBox::arrowColourId, Colours::textDim);
    setColour (PopupMenu::backgroundColourId, Colours::panel);
    setColour (PopupMenu::textColourId, Colours::text);
    setColour (PopupMenu::highlightedBackgroundColourId, Colours::chords.withAlpha (0.25f));
    setColour (PopupMenu::highlightedTextColourId, Colours::text);
    setColour (PopupMenu::headerTextColourId, Colours::textDim);
    setColour (TextButton::buttonColourId, Colours::panelLight);
    setColour (TextButton::textColourOffId, Colours::text);
    setColour (TextButton::textColourOnId, Colours::text);
    setColour (TooltipWindow::backgroundColourId, Colours::panelLight);
    setColour (TooltipWindow::textColourId, Colours::text);
    setColour (AlertWindow::backgroundColourId, Colours::panel);
    setColour (AlertWindow::textColourId, Colours::text);
}

void BounceLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                          float start, float end, juce::Slider& slider)
{
    using namespace juce;
    const auto accent = accentOf (slider);
    const auto bounds = Rectangle<float> (static_cast<float> (x), static_cast<float> (y), static_cast<float> (w), static_cast<float> (h)).reduced (6.0f);
    const float radius = jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto c = bounds.getCentre();
    const float arcR = radius - 3.0f;
    const float lineW = jmax (2.5f, radius * 0.11f);

    // Track.
    Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, start, end, true);
    g.setColour (Colours::well);
    g.strokePath (track, PathStrokeType (lineW + 2.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (Colours::outline);
    g.strokePath (track, PathStrokeType (lineW, PathStrokeType::curved, PathStrokeType::rounded));

    // Value arc (bipolar sliders grow from the centre).
    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const float from = bipolar ? (start + end) / 2.0f : start;
    const float to = start + pos * (end - start);
    if (std::abs (to - from) > 0.001f)
    {
        Path value;
        value.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, jmin (from, to), jmax (from, to), true);
        if (slider.isEnabled())
            drawGlow (g, value, accent, 7.0f, lineW);
        g.setColour (slider.isEnabled() ? accent : Colours::textFaint);
        g.strokePath (value, PathStrokeType (lineW, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // Knob body.
    const float bodyR = arcR - lineW - 4.0f;
    g.setGradientFill (ColourGradient (Colour (0xff34343e), c.x, c.y - bodyR,
                                       Colour (0xff17171c), c.x, c.y + bodyR, false));
    g.fillEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawEllipse (c.x - bodyR + 1.0f, c.y - bodyR + 1.0f, bodyR * 2.0f - 2.0f, bodyR * 2.0f - 2.0f, 1.0f);

    // Pointer.
    const auto tip = c.getPointOnCircumference (bodyR - 3.0f, to);
    const auto base = c.getPointOnCircumference (bodyR * 0.35f, to);
    g.setColour (Colours::text);
    g.drawLine ({ base, tip }, 2.2f);
}

void BounceLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    using namespace juce;
    const auto accent = accentOf (b);
    const bool on = b.getToggleState();
    auto bounds = b.getLocalBounds().toFloat().reduced (1.0f);

    if (b.getProperties()[pillProperty])
    {
        // Pill switch with the label inside, e.g. "MIDI OUT".
        const float r = bounds.getHeight() / 2.0f;
        Path pill;
        pill.addRoundedRectangle (bounds, r);
        if (on)
        {
            drawGlow (g, pill, accent, 6.0f, 1.0f);
            g.setColour (accent.withAlpha (0.22f));
            g.fillPath (pill);
            g.setColour (accent);
            g.strokePath (pill, PathStrokeType (1.2f));
        }
        else
        {
            g.setColour (Colours::well);
            g.fillPath (pill);
            g.setColour (highlighted ? Colours::textFaint : Colours::outline);
            g.strokePath (pill, PathStrokeType (1.0f));
        }

        // Short labels (M / S) are centred without the status dot.
        const bool shortLabel = b.getButtonText().length() <= 2;
        if (! shortLabel)
        {
            auto dot = bounds.removeFromLeft (bounds.getHeight()).reduced (bounds.getHeight() * 0.33f);
            g.setColour (on ? accent : Colours::textFaint);
            g.fillEllipse (dot);
        }

        g.setColour (on ? Colours::text : Colours::textDim);
        g.setFont (uiFont (jmin (12.0f, bounds.getHeight() * 0.55f), true));
        g.drawText (b.getButtonText().toUpperCase(), shortLabel ? bounds : bounds.withTrimmedRight (8.0f), Justification::centred);
        return;
    }

    // Plain checkbox style.
    auto box = bounds.removeFromLeft (bounds.getHeight()).reduced (3.0f);
    g.setColour (on ? accent : Colours::well);
    g.fillRoundedRectangle (box, 3.0f);
    g.setColour (Colours::outline);
    g.drawRoundedRectangle (box, 3.0f, 1.0f);
    g.setColour (Colours::text);
    g.setFont (uiFont (13.0f));
    g.drawText (b.getButtonText(), bounds.withTrimmedLeft (6.0f), Justification::centredLeft);
}

void BounceLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                              bool highlighted, bool down)
{
    using namespace juce;
    const auto bounds = b.getLocalBounds().toFloat().reduced (1.0f);
    const auto accent = accentOf (b, Colours::outline);
    const bool primary = b.getProperties()["primary"];
    const float r = jmin (8.0f, bounds.getHeight() / 2.0f);

    Path shape;
    shape.addRoundedRectangle (bounds, r);

    if (primary)
    {
        if (b.isEnabled())
            drawGlow (g, shape, accent, highlighted ? 10.0f : 6.0f, 1.0f);
        if (! b.isEnabled())
        {
            g.setColour (Colours::panelLight);
            g.fillPath (shape);
            g.setColour (Colours::outline);
            g.strokePath (shape, PathStrokeType (1.0f));
            return;
        }
        g.setGradientFill (ColourGradient (accent.brighter (0.15f), bounds.getX(), bounds.getY(),
                                           accent.darker (0.35f), bounds.getX(), bounds.getBottom(), false));
        g.fillPath (shape);
        if (down)
        {
            g.setColour (juce::Colours::black.withAlpha (0.2f));
            g.fillPath (shape);
        }
        return;
    }

    g.setColour (down ? Colours::well : (highlighted ? Colours::panelLight.brighter (0.08f) : Colours::panelLight));
    g.fillPath (shape);
    g.setColour (b.getToggleState() ? accent : (highlighted ? Colours::textFaint : Colours::outline));
    g.strokePath (shape, PathStrokeType (1.0f));
}

void BounceLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    const bool primary = b.getProperties()["primary"];
    g.setColour (b.isEnabled() ? (primary ? juce::Colours::white : Colours::text) : Colours::textFaint);
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (6, 2), juce::Justification::centred, 1);
}

juce::Font BounceLookAndFeel::getTextButtonFont (juce::TextButton& b, int h)
{
    return uiFont (juce::jmin (14.0f, static_cast<float> (h) * 0.45f), static_cast<bool> (b.getProperties()["primary"]));
}

void BounceLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    using namespace juce;
    const auto bounds = Rectangle<float> (0.0f, 0.0f, static_cast<float> (w), static_cast<float> (h)).reduced (0.5f);
    g.setColour (Colours::well);
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (box.hasKeyboardFocus (true) || box.isMouseOver (true) ? Colours::textFaint : Colours::outline);
    g.drawRoundedRectangle (bounds, 6.0f, 1.0f);

    const float ax = static_cast<float> (w) - 14.0f, ay = static_cast<float> (h) * 0.5f;
    Path arrow;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (accentOf (box, Colours::textDim));
    g.fillPath (arrow);
}

juce::Font BounceLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return uiFont (juce::jmin (14.0f, static_cast<float> (box.getHeight()) * 0.5f));
}

void BounceLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (6, 1, box.getWidth() - 26, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

juce::Font BounceLookAndFeel::getPopupMenuFont()
{
    return uiFont (14.0f);
}

void BounceLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (Colours::panel);
    g.setColour (Colours::outline);
    g.drawRect (0, 0, w, h);
}

juce::Font BounceLookAndFeel::getLabelFont (juce::Label& l)
{
    return uiFont (l.getFont().getHeight());
}

void BounceLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    g.setColour (Colours::panelLight);
    g.fillRoundedRectangle (0.0f, 0.0f, static_cast<float> (w), static_cast<float> (h), 5.0f);
    g.setColour (Colours::outline);
    g.drawRoundedRectangle (0.5f, 0.5f, static_cast<float> (w) - 1.0f, static_cast<float> (h) - 1.0f, 5.0f, 1.0f);
    g.setColour (Colours::text);
    g.setFont (uiFont (13.0f));
    g.drawFittedText (text, 8, 4, w - 16, h - 8, juce::Justification::centredLeft, 4);
}

} // namespace bounce::ui
