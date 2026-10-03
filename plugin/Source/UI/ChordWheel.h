#pragma once

#include "State/Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace bounce::ui
{

/** Pops up the per-chord menu (lock, reharmonise, invert, voicing, octave). */
void showSlotMenu (Session& session, int slot, juce::Component& target);

/** Central circular display of the loop: one segment per chord, lighting up in time with the
    playhead. Click selects a chord, right-click opens its menu. */
class ChordWheel : public juce::Component,
                   private juce::ChangeListener
{
public:
    ChordWheel (Session& session, std::function<double()> loopPositionSource);
    ~ChordWheel() override;

    /** Called from the editor's vblank callback (message thread, ~60 fps). */
    void tick();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    Session& session;
    std::function<double()> loopPosition;
    double lastPos = -2.0;
    int hoverSlot = -1;
    float activeGlow[16] {}; // per-slot glow, eased for smooth fades

    juce::Rectangle<float> wheelBounds() const;
    int slotAtPoint (juce::Point<float>) const;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
};

} // namespace bounce::ui
