#pragma once

#include "bounce/midi/MidiClip.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace bounce::ui
{

/** Drag source for MIDI: drag it straight into FL Studio's Piano Roll / Playlist (or any DAW or
    the desktop). On drag start the current part is written to a temp .mid named after its
    chords, key and tempo, and handed to the OS as an external file drag. */
class DragMidiButton : public juce::Component,
                       public juce::SettableTooltipClient
{
public:
    struct Payload
    {
        std::vector<midi::MidiClip> clips;
        double bpm = 140.0;
        juce::String fileName;
    };

    DragMidiButton (const juce::String& label, juce::Colour accent, std::function<Payload()> payloadSource);

    /** Compact: icon + short label only (for lane headers). */
    void setCompact (bool c) { compact = c; repaint(); }

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::String label;
    juce::Colour accent;
    std::function<Payload()> payloadSource;
    bool dragging = false;
    bool compact = false;
    juce::String status;
};

} // namespace bounce::ui
