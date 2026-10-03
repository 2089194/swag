#include "UI/PianoRollPreview.h"

#include "UI/BounceLookAndFeel.h"
#include "UI/ChordWheel.h"

namespace bounce::ui
{

PianoRollPreview::PianoRollPreview (Session& s, std::function<double()> source)
    : session (s), loopPosition (std::move (source))
{
    session.addChangeListener (this);
}

PianoRollPreview::~PianoRollPreview()
{
    session.removeChangeListener (this);
}

void PianoRollPreview::tick()
{
    const double pos = loopPosition ? loopPosition() : -1.0;
    if (std::abs (pos - lastPos) > 1.0e-4)
    {
        lastPos = pos;
        repaint();
    }
}

juce::Rectangle<float> PianoRollPreview::gridArea() const
{
    return getLocalBounds().toFloat().reduced (2.0f).withTrimmedLeft (22.0f);
}

void PianoRollPreview::mouseDown (const juce::MouseEvent& e)
{
    const auto grid = gridArea();
    if (! grid.contains (e.position))
        return;
    const auto& prog = session.progression();
    const double beat = (e.position.x - grid.getX()) / grid.getWidth() * prog.lengthBeats();
    const int slot = prog.slotAt (beat);
    session.setSelectedSlot (slot);
    if (e.mods.isPopupMenu())
        showSlotMenu (session, slot, *this);
}

void PianoRollPreview::paint (juce::Graphics& g)
{
    const auto& prog = session.progression();
    const auto& clip = session.chordClip();
    const auto bounds = getLocalBounds().toFloat();
    const auto grid = gridArea();
    const auto accent = Colours::chords;

    g.setColour (Colours::well);
    g.fillRoundedRectangle (bounds, 8.0f);

    int lo = 127, hi = 0;
    for (const auto& n : clip.notes)
    {
        lo = juce::jmin (lo, n.pitch);
        hi = juce::jmax (hi, n.pitch);
    }
    if (lo > hi)
    {
        lo = 48;
        hi = 72;
    }
    lo -= 2;
    hi += 2;
    const int rows = hi - lo + 1;
    const float rowH = grid.getHeight() / static_cast<float> (rows);
    const double len = clip.lengthBeats > 0.0 ? clip.lengthBeats : prog.lengthBeats();
    const auto xOf = [&] (double beat) { return grid.getX() + static_cast<float> (beat / len) * grid.getWidth(); };
    const auto yOf = [&] (int pitch) { return grid.getBottom() - static_cast<float> (pitch - lo + 1) * rowH; };

    // Keyboard strip + black-key rows.
    for (int p = lo; p <= hi; ++p)
    {
        const bool black = juce::MidiMessage::isMidiNoteBlack (p);
        const float y = yOf (p);
        g.setColour (black ? juce::Colour (0xff0a0a0d) : juce::Colour (0xff121217));
        g.fillRect (grid.getX(), y, grid.getWidth(), rowH);
        g.setColour (black ? juce::Colour (0xff1c1c22) : juce::Colour (0xff6a6a75));
        g.fillRect (bounds.getX() + 4.0f, y + 0.5f, 16.0f, juce::jmax (0.5f, rowH - 1.0f));
        if (p % 12 == 0 && rowH > 4.0f)
        {
            g.setColour (Colours::textFaint);
            g.setFont (uiFont (juce::jmin (9.0f, rowH * 2.0f)));
            g.drawText ("C" + juce::String (p / 12 - 1), juce::Rectangle<float> (grid.getX() + 2.0f, y - 10.0f, 30.0f, 10.0f),
                        juce::Justification::bottomLeft);
        }
    }

    // Chord regions: selected chord tinted, alternate chords slightly lifted.
    for (int i = 0; i < static_cast<int> (prog.slots.size()); ++i)
    {
        const float x0 = xOf (prog.slotStart (i)), x1 = xOf (prog.slotStart (i) + prog.slotLength (i));
        if (i == session.getSelectedSlot())
        {
            g.setColour (accent.withAlpha (0.08f));
            g.fillRect (x0, grid.getY(), x1 - x0, grid.getHeight());
        }
        g.setColour (Colours::outline);
        g.drawVerticalLine (juce::roundToInt (x0), grid.getY(), grid.getBottom());
    }

    // Beat grid.
    for (int beat = 1; beat < static_cast<int> (len); ++beat)
    {
        g.setColour (beat % prog.beatsPerBar == 0 ? Colours::outline : Colours::outline.withAlpha (0.35f));
        g.drawVerticalLine (juce::roundToInt (xOf (beat)), grid.getY(), grid.getBottom());
    }

    // Notes.
    for (const auto& n : clip.notes)
    {
        const auto r = juce::Rectangle<float> (xOf (n.start), yOf (n.pitch), juce::jmax (2.0f, xOf (n.start + n.length) - xOf (n.start)), rowH)
                           .reduced (0.0f, juce::jmin (0.75f, rowH * 0.1f));
        const float vel = static_cast<float> (n.velocity) / 127.0f;
        g.setColour (accent.withMultipliedBrightness (0.55f + 0.45f * vel));
        g.fillRoundedRectangle (r, juce::jmin (2.5f, rowH * 0.4f));
        g.setColour (accent.brighter (0.5f).withAlpha (0.6f));
        g.drawRoundedRectangle (r, juce::jmin (2.5f, rowH * 0.4f), 0.8f);
    }

    // Playhead.
    if (lastPos >= 0.0)
    {
        const float x = xOf (lastPos);
        g.setColour (accent.withAlpha (0.25f));
        g.fillRect (x - 3.0f, grid.getY(), 6.0f, grid.getHeight());
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.drawVerticalLine (juce::roundToInt (x), grid.getY(), grid.getBottom());
    }

    g.setColour (Colours::outline);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);
}

} // namespace bounce::ui
