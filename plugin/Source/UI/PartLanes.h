#pragma once

#include "State/Session.h"
#include "UI/DragMidiButton.h"
#include "UI/Widgets.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace bounce::ui
{

juce::Colour partColour (gen::Part part);

/** Mini piano roll of one generated part. Click a note to delete it, double-click empty space to
    add one (edits survive knob tweaks, cleared when the part is re-rolled). The chords lane
    shades each chord and selects it on click (right-click = chord menu); the melody lane draws
    the counter-melody too and has a lock toggle per bar. */
class PartRoll : public juce::Component,
                 private juce::ChangeListener
{
public:
    PartRoll (Session& session, gen::Part part, std::function<double()> loopPositionSource);
    ~PartRoll() override;

    void tick();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    Session& session;
    gen::Part part;
    std::function<double()> loopPosition;
    double lastPos = -2.0;
    int lo = 48, hi = 72;
    bool clickedNote = false;

    juce::Rectangle<float> gridArea() const;
    juce::Rectangle<float> lockArea (int bar) const;
    juce::Rectangle<float> noteRect (const midi::Note& n) const;
    double beatAt (float x) const;
    int pitchAt (float y) const;
    const midi::Note* noteAt (juce::Point<float> p, gen::Part& which) const;
    void updateRange();
    void changeListenerCallback (juce::ChangeBroadcaster*) override { updateRange(); repaint(); }
};

/** Step view of the drum pattern: one row per lane, hits as blocks (rolls in finer ticks).
    Click toggles a hit on the 16th grid; right-click a lane name to load a custom sample. */
class DrumGrid : public juce::Component,
                 private juce::ChangeListener
{
public:
    DrumGrid (Session& session, std::function<double()> loopPositionSource);
    ~DrumGrid() override;

    std::function<void (gen::DrumLane)> onLaneMenu;

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

/** Header (name, re-roll dice, clear edits, drag tile) + roll or drum grid for one part. */
class PartLane : public juce::Component,
                 private juce::ChangeListener
{
public:
    PartLane (Session& session, gen::Part part, std::function<double()> loopPositionSource);
    ~PartLane() override;

    void tick();
    void paint (juce::Graphics&) override;
    void resized() override;

    DrumGrid* getDrumGrid() { return drumGrid.get(); }

private:
    Session& session;
    gen::Part part;
    IconButton listen;
    IconButton dice;
    IconButton clear;
    IconButton counterDice;
    DragMidiButton drag;
    std::unique_ptr<PartRoll> roll;
    std::unique_ptr<DrumGrid> drumGrid;

    bool shownListening = false;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
};

} // namespace bounce::ui
