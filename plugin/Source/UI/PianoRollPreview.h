#pragma once

#include "State/Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace bounce::ui
{

/** Mini piano roll of the rendered chord part: note blocks over shaded chord regions, a moving
    playhead, click to select a chord, right-click for its menu. */
class PianoRollPreview : public juce::Component,
                         private juce::ChangeListener
{
public:
    PianoRollPreview (Session& session, std::function<double()> loopPositionSource);
    ~PianoRollPreview() override;

    void tick();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    Session& session;
    std::function<double()> loopPosition;
    double lastPos = -2.0;

    juce::Rectangle<float> gridArea() const;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
};

} // namespace bounce::ui
