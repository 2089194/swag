#include "UI/Widgets.h"

namespace bounce::ui
{

namespace Icons
{
juce::Path dice()
{
    juce::Path p;
    p.addRoundedRectangle (0.08f, 0.08f, 0.84f, 0.84f, 0.18f);
    p.setUsingNonZeroWinding (false);
    for (auto [x, y] : { std::pair { 0.3f, 0.3f }, { 0.7f, 0.3f }, { 0.5f, 0.5f }, { 0.3f, 0.7f }, { 0.7f, 0.7f } })
        p.addEllipse (x - 0.075f, y - 0.075f, 0.15f, 0.15f);
    return p;
}

static juce::Path arrowArc (bool clockwise)
{
    juce::Path arc;
    const float s = clockwise ? 1.0f : -1.0f;
    arc.addCentredArc (0.5f, 0.55f, 0.32f, 0.32f, 0.0f, -2.2f * s, 1.0f * s, true);
    juce::Path stroked;
    juce::PathStrokeType (0.11f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (stroked, arc);
    const auto start = arc.getPointAlongPath (0.0f);
    juce::Path head;
    head.addTriangle (start.x - 0.16f, start.y + 0.02f * s, start.x + 0.16f, start.y + 0.02f * s, start.x, start.y - 0.18f);
    head.applyTransform (juce::AffineTransform::rotation (clockwise ? -1.1f : 1.1f, start.x, start.y));
    stroked.addPath (head);
    return stroked;
}

juce::Path undo() { return arrowArc (false); }
juce::Path redo() { return arrowArc (true); }

juce::Path refresh()
{
    auto p = arrowArc (true);
    p.applyTransform (juce::AffineTransform::rotation (0.6f, 0.5f, 0.5f));
    return p;
}

juce::Path lock (bool closed)
{
    juce::Path p;
    p.addRoundedRectangle (0.18f, 0.45f, 0.64f, 0.47f, 0.08f);
    juce::Path shackle;
    const float x0 = 0.3f, x1 = 0.7f;
    shackle.startNewSubPath (x0, 0.45f);
    shackle.lineTo (x0, 0.3f);
    shackle.cubicTo (x0, 0.04f, x1, 0.04f, x1, 0.3f);
    if (closed)
        shackle.lineTo (x1, 0.45f);
    else
        shackle.lineTo (x1, 0.33f);
    juce::Path stroked;
    juce::PathStrokeType (0.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (stroked, shackle);
    if (! closed)
        stroked.applyTransform (juce::AffineTransform::translation (0.12f, -0.04f));
    p.addPath (stroked);
    return p;
}

juce::Path folder()
{
    juce::Path p;
    p.startNewSubPath (0.06f, 0.24f);
    p.lineTo (0.4f, 0.24f);
    p.lineTo (0.48f, 0.34f);
    p.lineTo (0.94f, 0.34f);
    p.lineTo (0.94f, 0.84f);
    p.lineTo (0.06f, 0.84f);
    p.closeSubPath();
    return p;
}

juce::Path play()
{
    juce::Path p;
    p.addTriangle (0.22f, 0.12f, 0.22f, 0.88f, 0.88f, 0.5f);
    return p;
}

juce::Path stop()
{
    juce::Path p;
    p.addRoundedRectangle (0.2f, 0.2f, 0.6f, 0.6f, 0.08f);
    return p;
}

juce::Path drag()
{
    // A note glyph with an outward arrow.
    juce::Path p;
    p.addEllipse (0.08f, 0.62f, 0.3f, 0.24f);
    p.addRectangle (0.32f, 0.12f, 0.07f, 0.64f);
    p.addTriangle (0.39f, 0.12f, 0.39f, 0.34f, 0.6f, 0.26f);
    juce::Path arrow;
    arrow.startNewSubPath (0.58f, 0.78f);
    arrow.lineTo (0.9f, 0.46f);
    juce::Path stroked;
    juce::PathStrokeType (0.08f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (stroked, arrow);
    p.addPath (stroked);
    p.addTriangle (0.95f, 0.4f, 0.72f, 0.42f, 0.93f, 0.63f);
    return p;
}

juce::Path chevron (bool right)
{
    juce::Path line;
    if (right)
    {
        line.startNewSubPath (0.38f, 0.2f);
        line.lineTo (0.66f, 0.5f);
        line.lineTo (0.38f, 0.8f);
    }
    else
    {
        line.startNewSubPath (0.62f, 0.2f);
        line.lineTo (0.34f, 0.5f);
        line.lineTo (0.62f, 0.8f);
    }
    juce::Path p;
    juce::PathStrokeType (0.12f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (p, line);
    return p;
}

juce::Path history()
{
    juce::Path ring;
    ring.addCentredArc (0.5f, 0.5f, 0.38f, 0.38f, 0.0f, 0.0f, juce::MathConstants<float>::twoPi, true);
    juce::Path hands;
    hands.startNewSubPath (0.5f, 0.26f);
    hands.lineTo (0.5f, 0.5f);
    hands.lineTo (0.68f, 0.6f);
    juce::Path p;
    juce::PathStrokeType (0.09f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (p, ring);
    juce::Path h;
    juce::PathStrokeType (0.09f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (h, hands);
    p.addPath (h);
    return p;
}
} // namespace Icons

//==============================================================================
IconButton::IconButton (const juce::String& name, juce::Path i, juce::Colour a)
    : juce::Button (name), icon (std::move (i)), accent (a), activeColour (Colours::chords)
{
    setTooltip (name);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void IconButton::mouseUp (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && onRightClick)
    {
        onRightClick();
        return;
    }
    juce::Button::mouseUp (e);
}

void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    const bool on = getToggleState();

    if (highlighted || down || on)
    {
        g.setColour (down ? Colours::well : Colours::panelLight);
        g.fillRoundedRectangle (b, 6.0f);
    }

    const bool hasText = getButtonText().isNotEmpty() && getButtonText() != getName();
    auto iconArea = hasText ? b.removeFromLeft (b.getHeight()) : b;
    const float s = juce::jmin (iconArea.getWidth(), iconArea.getHeight()) * 0.56f;
    const auto area = iconArea.withSizeKeepingCentre (s, s);

    juce::Path p (icon);
    p.applyTransform (juce::AffineTransform::scale (s).translated (area.getX(), area.getY()));

    const auto colour = ! isEnabled() ? Colours::textFaint
                      : on ? activeColour
                      : highlighted ? Colours::text : accent;
    if (on)
        drawGlow (g, p, activeColour, 4.0f, 0.5f);
    g.setColour (colour);
    g.fillPath (p);

    if (hasText)
    {
        g.setFont (uiFont (13.0f, true));
        g.drawText (getButtonText(), b.withTrimmedRight (6.0f), juce::Justification::centredLeft);
    }
}

//==============================================================================
LabeledKnob::LabeledKnob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
                          const juce::String& c, juce::Colour accent)
    : caption (c), param (state.getParameter (paramId))
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    slider.setMouseDragSensitivity (220);
    slider.setVelocityModeParameters (0.8, 1, 0.05, true, juce::ModifierKeys::shiftModifier);
    setAccent (slider, accent);
    slider.addListener (this);
    slider.addMouseListener (this, false);
    addAndMakeVisible (slider);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);
    slider.setDoubleClickReturnValue (true, static_cast<double> (param != nullptr ? param->convertFrom0to1 (param->getDefaultValue()) : 0.0f));
    slider.setPopupDisplayEnabled (false, false, nullptr);
}

void LabeledKnob::resized()
{
    auto b = getLocalBounds();
    b.removeFromBottom (30);
    slider.setBounds (b);
}

void LabeledKnob::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    auto text = b.removeFromBottom (30.0f);

    const bool active = slider.isMouseOverOrDragging();
    const auto valueText = param != nullptr ? param->getCurrentValueAsText() : juce::String (slider.getValue(), 2);

    g.setFont (uiFont (12.0f, true));
    g.setColour (Colours::textDim);
    g.drawText (caption.toUpperCase(), text.removeFromTop (14.0f), juce::Justification::centred);

    g.setFont (uiFont (12.0f));
    g.setColour (active ? accentOf (slider) : Colours::text);
    g.drawText (valueText, text, juce::Justification::centred);
}

//==============================================================================
LabeledCombo::LabeledCombo (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& c)
    : caption (c)
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId)))
        combo.addItemList (p->choices, 1);
    else if (auto* pi = dynamic_cast<juce::AudioParameterInt*> (state.getParameter (paramId)))
    {
        // Int params are shown as a list of their values; the attachment maps index <-> value.
        for (int v = pi->getRange().getStart(); v <= pi->getRange().getEnd(); ++v)
            combo.addItem (juce::String (v), v - pi->getRange().getStart() + 1);
    }
    addAndMakeVisible (combo);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, paramId, combo);
}

void LabeledCombo::resized()
{
    combo.setBounds (getLocalBounds().withTrimmedTop (16));
}

void LabeledCombo::paint (juce::Graphics& g)
{
    g.setColour (Colours::textDim);
    g.setFont (uiFont (11.5f, true));
    g.drawText (caption.toUpperCase(), getLocalBounds().removeFromTop (14), juce::Justification::centredLeft);
}

//==============================================================================
PillToggle::PillToggle (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
                        const juce::String& buttonText, juce::Colour accent)
    : juce::ToggleButton (buttonText)
{
    getProperties().set (pillProperty, true);
    setAccent (*this, accent);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, paramId, *this);
}

} // namespace bounce::ui
