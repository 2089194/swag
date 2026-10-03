#pragma once

#include "UI/BounceLookAndFeel.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>

namespace bounce::ui
{

/** Vector icons, drawn into a unit square (0..1). */
namespace Icons
{
juce::Path dice();
juce::Path undo();
juce::Path redo();
juce::Path lock (bool closed);
juce::Path refresh();
juce::Path folder();
juce::Path play();
juce::Path stop();
juce::Path drag();
juce::Path chevron (bool right);
juce::Path history();
} // namespace Icons

/** Small flat button that draws a vector icon (optionally with text). */
class IconButton : public juce::Button
{
public:
    IconButton (const juce::String& name, juce::Path icon, juce::Colour accent = Colours::textDim);

    void setIcon (juce::Path newIcon) { icon = std::move (newIcon); repaint(); }
    void setActiveColour (juce::Colour c) { activeColour = c; repaint(); }

    std::function<void()> onRightClick;

    void mouseUp (const juce::MouseEvent& e) override;

private:
    juce::Path icon;
    juce::Colour accent, activeColour;

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

/** Arc knob with a caption underneath and the current value shown while hovering/dragging. */
class LabeledKnob : public juce::Component,
                    private juce::Slider::Listener
{
public:
    LabeledKnob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
                 const juce::String& caption, juce::Colour accent);

    void resized() override;
    void paint (juce::Graphics&) override;

    juce::Slider& getSlider() { return slider; }
    void setTooltip (const juce::String& text) { slider.setTooltip (text); }

private:
    juce::Slider slider;
    juce::String caption;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    void sliderValueChanged (juce::Slider*) override { repaint(); }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
};

/** ComboBox bound to a choice parameter, with a small caption above. */
class LabeledCombo : public juce::Component
{
public:
    LabeledCombo (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& caption);

    void resized() override;
    void paint (juce::Graphics&) override;

    juce::ComboBox& getCombo() { return combo; }

private:
    juce::ComboBox combo;
    juce::String caption;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

/** Pill toggle bound to a bool parameter. */
class PillToggle : public juce::ToggleButton
{
public:
    PillToggle (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
                const juce::String& text, juce::Colour accent);

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

} // namespace bounce::ui
