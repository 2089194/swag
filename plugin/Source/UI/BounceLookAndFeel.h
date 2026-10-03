#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace bounce::ui
{

namespace Colours
{
inline const juce::Colour background  { 0xff111115 };
inline const juce::Colour panel       { 0xff1a1a20 };
inline const juce::Colour panelLight  { 0xff22222a };
inline const juce::Colour well        { 0xff0c0c10 };
inline const juce::Colour outline     { 0xff2b2b35 };
inline const juce::Colour text        { 0xffeaeaf2 };
inline const juce::Colour textDim     { 0xff8a8a99 };
inline const juce::Colour textFaint   { 0xff55555f };

// One accent per module.
inline const juce::Colour chords      { 0xffff3d9a }; // magenta
inline const juce::Colour bass        { 0xffff7a1a }; // orange (808)
inline const juce::Colour melody      { 0xff22d3ee }; // cyan
inline const juce::Colour drums       { 0xffa855f7 }; // purple
inline const juce::Colour good        { 0xff4ade80 };
} // namespace Colours

/** Component property that switches a ToggleButton to the pill style. */
inline const juce::Identifier pillProperty ("bouncePill");
/** Component property holding an accent colour (as ARGB int64) for knobs/toggles/buttons. */
inline const juce::Identifier accentProperty ("bounceAccent");

juce::Colour accentOf (const juce::Component& c, juce::Colour fallback = Colours::chords);
void setAccent (juce::Component& c, juce::Colour accent);

/** Draws a soft coloured glow behind a path. */
void drawGlow (juce::Graphics& g, const juce::Path& p, juce::Colour colour, float radius, float strokeWidth);

/** Module panel: charcoal gradient, inner shadow, title and an accent bar. */
void drawPanel (juce::Graphics& g, juce::Rectangle<float> bounds, const juce::String& title, juce::Colour accent);

juce::Font uiFont (float height, bool bold = false);

class BounceLookAndFeel : public juce::LookAndFeel_V4
{
public:
    BounceLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;

    juce::Font getLabelFont (juce::Label&) override;

    void drawTooltip (juce::Graphics&, const juce::String& text, int w, int h) override;
};

} // namespace bounce::ui
