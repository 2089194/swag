#pragma once

#include "State/Session.h"
#include "UI/Widgets.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace bounce::ui
{

/** A row of chord cards: name, roman numeral, lock, inversion arrows and reharmonise. */
class ChordStrip : public juce::Component,
                   private juce::ChangeListener
{
public:
    explicit ChordStrip (Session& session);
    ~ChordStrip() override;

    void resized() override;
    void setPlayingSlot (int slot);

private:
    class Card : public juce::Component
    {
    public:
        Card (Session& session, int index);

        void update();
        void setPlaying (bool shouldGlow);
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;

    private:
        Session& session;
        int index;
        bool playing = false;
        IconButton lockButton { "Lock", Icons::lock (false) };
        IconButton invDown { "Invert down", Icons::chevron (false) };
        IconButton invUp { "Invert up", Icons::chevron (true) };
        IconButton reharm { "Reharmonise", Icons::refresh() };
    };

    Session& session;
    juce::OwnedArray<Card> cards;
    int playingSlot = -1;

    void rebuild();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
};

} // namespace bounce::ui
