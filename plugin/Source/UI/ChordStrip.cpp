#include "UI/ChordStrip.h"

#include "UI/ChordWheel.h"

#include "bounce/theory/Voicing.h"

namespace bounce::ui
{

ChordStrip::Card::Card (Session& s, int i) : session (s), index (i)
{
    for (auto* b : { &lockButton, &invDown, &invUp, &reharm })
        addAndMakeVisible (b);

    lockButton.setClickingTogglesState (false);
    lockButton.setActiveColour (Colours::chords);
    lockButton.onClick = [this] { session.toggleLock (index); };
    invDown.onClick = [this] { session.invert (index, -1); };
    invUp.onClick = [this] { session.invert (index, 1); };
    reharm.onClick = [this] { session.reharmonise (index); };

    lockButton.setTooltip ("Lock this chord: Generate keeps it and rebuilds the others around it");
    reharm.setTooltip ("Reharmonise: swap for another chord that fits its neighbours");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    update();
}

void ChordStrip::Card::update()
{
    const auto& prog = session.progression();
    if (index >= static_cast<int> (prog.slots.size()))
        return;
    const bool locked = prog.slots[static_cast<size_t> (index)].locked;
    lockButton.setToggleState (locked, juce::dontSendNotification);
    lockButton.setIcon (Icons::lock (locked));
    repaint();
}

void ChordStrip::Card::setPlaying (bool shouldGlow)
{
    if (playing != shouldGlow)
    {
        playing = shouldGlow;
        repaint();
    }
}

void ChordStrip::Card::resized()
{
    auto b = getLocalBounds().reduced (6);
    auto row = b.removeFromBottom (26);
    const int w = row.getWidth() / 4;
    lockButton.setBounds (row.removeFromLeft (w));
    invDown.setBounds (row.removeFromLeft (w));
    invUp.setBounds (row.removeFromLeft (w));
    reharm.setBounds (row);
}

void ChordStrip::Card::mouseDown (const juce::MouseEvent& e)
{
    session.setSelectedSlot (index);
    if (e.mods.isPopupMenu())
        showSlotMenu (session, index, *this);
}

void ChordStrip::Card::paint (juce::Graphics& g)
{
    const auto& prog = session.progression();
    if (index >= static_cast<int> (prog.slots.size()))
        return;
    const auto& slot = prog.slots[static_cast<size_t> (index)];
    const bool selected = session.getSelectedSlot() == index;
    const auto accent = Colours::chords;

    auto b = getLocalBounds().toFloat().reduced (2.0f);
    juce::Path shape;
    shape.addRoundedRectangle (b, 8.0f);

    if (playing)
        drawGlow (g, shape, accent, 8.0f, 1.0f);

    g.setGradientFill (juce::ColourGradient (playing ? Colours::panelLight.interpolatedWith (accent, 0.18f) : Colours::panelLight,
                                             b.getX(), b.getY(), Colours::panel, b.getX(), b.getBottom(), false));
    g.fillPath (shape);
    g.setColour (selected ? accent : Colours::outline);
    g.strokePath (shape, juce::PathStrokeType (selected ? 1.5f : 1.0f));

    auto text = b.reduced (10.0f, 8.0f);
    text.removeFromBottom (28.0f);

    g.setColour (Colours::textFaint);
    g.setFont (uiFont (11.0f, true));
    g.drawText (juce::String (index + 1), text.removeFromTop (12.0f), juce::Justification::centredLeft);

    g.setColour (Colours::text);
    g.setFont (uiFont (juce::jlimit (14.0f, 22.0f, b.getWidth() / 6.0f), true));
    g.drawFittedText (juce::String (slot.chord.name (prog.key.preferredSpelling())),
                      text.removeFromTop (26.0f).toNearestInt(), juce::Justification::centredLeft, 1);

    g.setColour (slot.function.borrowed ? Colours::bass : accent);
    g.setFont (uiFont (13.0f, true));
    g.drawText (juce::String (theory::romanNumeral (slot.chord, prog.key)) + (slot.function.borrowed ? "  borrowed" : ""),
                text.removeFromTop (16.0f), juce::Justification::centredLeft);

    // Small badges for per-chord voicing edits.
    juce::StringArray badges;
    if (slot.voicing)
        badges.add (juce::String (std::string (theory::voicingStyleName (*slot.voicing))));
    if (slot.inversion)
        badges.add ("inv " + juce::String (*slot.inversion));
    if (slot.octave != 0)
        badges.add ((slot.octave > 0 ? "+" : "") + juce::String (slot.octave) + " oct");
    if (badges.size() > 0)
    {
        g.setColour (Colours::textDim);
        g.setFont (uiFont (11.0f));
        g.drawText (badges.joinIntoString (juce::String::fromUTF8 ("  \xc2\xb7  ")), text.removeFromTop (14.0f), juce::Justification::centredLeft);
    }
}

//==============================================================================
ChordStrip::ChordStrip (Session& s) : session (s)
{
    session.addChangeListener (this);
    rebuild();
}

ChordStrip::~ChordStrip()
{
    session.removeChangeListener (this);
}

void ChordStrip::rebuild()
{
    cards.clear();
    for (int i = 0; i < static_cast<int> (session.progression().slots.size()); ++i)
        addAndMakeVisible (cards.add (new Card (session, i)));
    resized();
}

void ChordStrip::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (cards.size() != static_cast<int> (session.progression().slots.size()))
        rebuild();
    for (auto* c : cards)
        c->update();
}

void ChordStrip::setPlayingSlot (int slot)
{
    if (slot == playingSlot)
        return;
    playingSlot = slot;
    for (int i = 0; i < cards.size(); ++i)
        cards[i]->setPlaying (i == slot);
}

void ChordStrip::resized()
{
    if (cards.isEmpty())
        return;
    auto b = getLocalBounds();
    const int gap = 8;
    const int w = (b.getWidth() - gap * (cards.size() - 1)) / cards.size();
    for (auto* c : cards)
    {
        c->setBounds (b.removeFromLeft (w));
        b.removeFromLeft (gap);
    }
}

} // namespace bounce::ui
