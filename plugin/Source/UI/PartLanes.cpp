#include "UI/PartLanes.h"

#include "UI/BounceLookAndFeel.h"
#include "UI/ChordWheel.h"

namespace bounce::ui
{

using gen::Part;

juce::Colour partColour (Part part)
{
    switch (part)
    {
        case Part::Chords:  return Colours::chords;
        case Part::Bass:    return Colours::bass;
        case Part::Melody:  return Colours::melody;
        case Part::Counter: return Colours::melody.withRotatedHue (0.08f);
        case Part::Drums:   return Colours::drums;
        case Part::NumParts: break;
    }
    return Colours::text;
}

//==============================================================================
PartRoll::PartRoll (Session& s, Part p, std::function<double()> source)
    : session (s), part (p), loopPosition (std::move (source))
{
    session.addChangeListener (this);
    updateRange();
}

PartRoll::~PartRoll()
{
    session.removeChangeListener (this);
}

void PartRoll::updateRange()
{
    lo = 127;
    hi = 0;
    for (auto pt : { part, part == Part::Melody ? Part::Counter : part })
        for (const auto& n : session.clip (pt).notes)
        {
            lo = juce::jmin (lo, n.pitch);
            hi = juce::jmax (hi, n.pitch);
        }
    if (lo > hi)
    {
        lo = part == Part::Bass ? 24 : 55;
        hi = lo + 24;
    }
    lo -= 2;
    hi += 2;
    if (hi - lo < 12)
    {
        lo -= (12 - (hi - lo)) / 2;
        hi = lo + 12;
    }
}

void PartRoll::tick()
{
    const double pos = loopPosition ? loopPosition() : -1.0;
    if (std::abs (pos - lastPos) > 1.0e-4)
    {
        lastPos = pos;
        repaint();
    }
}

juce::Rectangle<float> PartRoll::gridArea() const
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    if (part == Part::Melody)
        b.removeFromTop (12.0f); // bar lock strip
    return b;
}

juce::Rectangle<float> PartRoll::lockArea (int bar) const
{
    const auto g = gridArea();
    const auto& prog = session.progression();
    const float w = g.getWidth() / static_cast<float> (juce::jmax (1, prog.bars));
    return { g.getX() + w * static_cast<float> (bar) + 2.0f, 1.0f, 11.0f, 11.0f };
}

juce::Rectangle<float> PartRoll::noteRect (const midi::Note& n) const
{
    const auto g = gridArea();
    const double len = session.progression().lengthBeats();
    const float rowH = g.getHeight() / static_cast<float> (hi - lo + 1);
    const float x0 = g.getX() + static_cast<float> (n.start / len) * g.getWidth();
    const float x1 = g.getX() + static_cast<float> ((n.start + n.length) / len) * g.getWidth();
    return { x0, g.getBottom() - static_cast<float> (n.pitch - lo + 1) * rowH, juce::jmax (2.0f, x1 - x0), rowH };
}

double PartRoll::beatAt (float x) const
{
    const auto g = gridArea();
    return juce::jlimit (0.0, session.progression().lengthBeats() - 1e-6,
                         (x - g.getX()) / g.getWidth() * session.progression().lengthBeats());
}

int PartRoll::pitchAt (float y) const
{
    const auto g = gridArea();
    const float rowH = g.getHeight() / static_cast<float> (hi - lo + 1);
    return juce::jlimit (0, 127, lo + static_cast<int> ((g.getBottom() - y) / rowH));
}

const midi::Note* PartRoll::noteAt (juce::Point<float> p, Part& which) const
{
    for (auto pt : { part, part == Part::Melody ? Part::Counter : part })
        for (const auto& n : session.clip (pt).notes)
            if (noteRect (n).expanded (1.0f, 1.0f).contains (p))
            {
                which = pt;
                return &n;
            }
    return nullptr;
}

void PartRoll::mouseMove (const juce::MouseEvent& e)
{
    Part which = part;
    setMouseCursor (noteAt (e.position, which) != nullptr ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void PartRoll::mouseDown (const juce::MouseEvent& e)
{
    clickedNote = false;
    const auto& prog = session.progression();

    if (part == Part::Melody && e.position.y < 13.0f)
    {
        for (int bar = 0; bar < prog.bars; ++bar)
            if (lockArea (bar).expanded (3.0f).contains (e.position))
                session.toggleMelodyBarLock (bar);
        return;
    }

    if (part == Part::Chords)
    {
        const int slot = prog.slotAt (beatAt (e.position.x));
        session.setSelectedSlot (slot);
        if (e.mods.isPopupMenu())
        {
            showSlotMenu (session, slot, *this);
            return;
        }
    }

    Part which = part;
    if (const auto* n = noteAt (e.position, which); n != nullptr && ! e.mods.isPopupMenu())
    {
        clickedNote = true;
        session.removeNote (which, *n);
    }
}

void PartRoll::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (clickedNote || ! gridArea().contains (e.position))
        return;
    const double grid = part == Part::Chords ? 0.5 : 0.25;
    midi::Note n;
    n.start = std::floor (beatAt (e.position.x) / grid) * grid;
    n.length = part == Part::Chords ? 1.0 : 0.5;
    n.pitch = pitchAt (e.position.y);
    n.velocity = 96;
    const auto& existing = session.clip (part).notes;
    n.channel = existing.empty() ? 0 : existing.front().channel;
    session.addNote (part, n);
}

void PartRoll::paint (juce::Graphics& g)
{
    const auto& prog = session.progression();
    const auto bounds = getLocalBounds().toFloat();
    const auto grid = gridArea();
    const auto accent = partColour (part);
    const double len = prog.lengthBeats();
    const auto xOf = [&] (double beat) { return grid.getX() + static_cast<float> (beat / len) * grid.getWidth(); };

    g.setColour (Colours::well);
    g.fillRoundedRectangle (bounds, 6.0f);

    // Rows (black keys darker).
    const float rowH = grid.getHeight() / static_cast<float> (hi - lo + 1);
    for (int p = lo; p <= hi; ++p)
        if (juce::MidiMessage::isMidiNoteBlack (p))
        {
            g.setColour (juce::Colour (0xff0a0a0d));
            g.fillRect (grid.getX(), grid.getBottom() - static_cast<float> (p - lo + 1) * rowH, grid.getWidth(), rowH);
        }

    // Chord regions (chords lane) and beat grid.
    if (part == Part::Chords)
        for (int i = 0; i < static_cast<int> (prog.slots.size()); ++i)
            if (i == session.getSelectedSlot())
            {
                g.setColour (accent.withAlpha (0.08f));
                g.fillRect (xOf (prog.slotStart (i)), grid.getY(), xOf (prog.slotStart (i) + prog.slotLength (i)) - xOf (prog.slotStart (i)), grid.getHeight());
            }
    for (int beat = 1; beat < static_cast<int> (len); ++beat)
    {
        g.setColour (beat % prog.beatsPerBar == 0 ? Colours::outline : Colours::outline.withAlpha (0.3f));
        g.drawVerticalLine (juce::roundToInt (xOf (beat)), grid.getY(), grid.getBottom());
    }
    if (part == Part::Chords)
        for (int i = 1; i < static_cast<int> (prog.slots.size()); ++i)
        {
            g.setColour (accent.withAlpha (0.35f));
            g.drawVerticalLine (juce::roundToInt (xOf (prog.slotStart (i))), grid.getY(), grid.getBottom());
        }

    // Locked melody bars.
    if (part == Part::Melody)
        for (int bar = 0; bar < prog.bars; ++bar)
        {
            const bool locked = session.isMelodyBarLocked (bar);
            if (locked)
            {
                g.setColour (accent.withAlpha (0.07f));
                g.fillRect (xOf (bar * 4.0), grid.getY(), xOf ((bar + 1) * 4.0) - xOf (bar * 4.0), grid.getHeight());
            }
            auto icon = Icons::lock (locked);
            const auto a = lockArea (bar);
            icon.applyTransform (juce::AffineTransform::scale (a.getWidth()).translated (a.getX(), a.getY()));
            g.setColour (locked ? accent : Colours::textFaint);
            g.fillPath (icon);
        }

    // Notes (counter-melody first, underneath).
    auto drawNotes = [&] (Part pt, juce::Colour c)
    {
        for (const auto& n : session.clip (pt).notes)
        {
            const auto r = noteRect (n).reduced (0.0f, juce::jmin (0.6f, rowH * 0.1f));
            const float vel = static_cast<float> (n.velocity) / 127.0f;
            g.setColour (c.withMultipliedBrightness (0.55f + 0.45f * vel));
            g.fillRoundedRectangle (r, juce::jmin (2.0f, rowH * 0.4f));
        }
    };
    if (part == Part::Melody)
        drawNotes (Part::Counter, partColour (Part::Counter).withAlpha (0.55f));
    drawNotes (part, accent);

    // Glides in the 808 lane: a thin line from note to note.
    if (part == Part::Bass)
    {
        const auto& notes = session.clip (Part::Bass).notes;
        for (size_t i = 0; i + 1 < notes.size(); ++i)
            if (notes[i].start + notes[i].length > notes[i + 1].start + 1e-9)
            {
                const auto a = noteRect (notes[i]), b = noteRect (notes[i + 1]);
                g.setColour (accent.brighter (0.4f));
                g.drawLine (b.getX() - 6.0f, a.getCentreY(), b.getX(), b.getCentreY(), 1.5f);
            }
    }

    if (lastPos >= 0.0)
    {
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.drawVerticalLine (juce::roundToInt (xOf (lastPos)), grid.getY(), grid.getBottom());
    }

    g.setColour (Colours::outline);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);
}

//==============================================================================
DrumGrid::DrumGrid (Session& s, std::function<double()> source) : session (s), loopPosition (std::move (source))
{
    session.addChangeListener (this);
}

DrumGrid::~DrumGrid()
{
    session.removeChangeListener (this);
}

void DrumGrid::tick()
{
    const double pos = loopPosition ? loopPosition() : -1.0;
    if (std::abs (pos - lastPos) > 1.0e-4)
    {
        lastPos = pos;
        repaint();
    }
}

juce::Rectangle<float> DrumGrid::gridArea() const
{
    return getLocalBounds().toFloat().reduced (1.0f).withTrimmedLeft (58.0f);
}

void DrumGrid::mouseDown (const juce::MouseEvent& e)
{
    const auto grid = gridArea();
    const float rowH = grid.getHeight() / static_cast<float> (gen::numDrumLanes);
    const int row = juce::jlimit (0, gen::numDrumLanes - 1, static_cast<int> ((e.position.y - grid.getY()) / rowH));
    const auto lane = static_cast<gen::DrumLane> (row);

    if (e.position.x < grid.getX())
    {
        if (onLaneMenu)
            onLaneMenu (lane);
        return;
    }
    const double len = session.drumPattern().lengthBeats();
    const double beat = std::floor ((e.position.x - grid.getX()) / grid.getWidth() * len * 4.0) / 4.0;
    session.toggleDrumHit (lane, juce::jlimit (0.0, len - 0.25, beat));
}

void DrumGrid::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto grid = gridArea();
    const auto accent = partColour (Part::Drums);
    const auto& pat = session.drumPattern();
    const double len = pat.lengthBeats();
    const float rowH = grid.getHeight() / static_cast<float> (gen::numDrumLanes);
    const auto xOf = [&] (double beat) { return grid.getX() + static_cast<float> (beat / len) * grid.getWidth(); };

    g.setColour (Colours::well);
    g.fillRoundedRectangle (bounds, 6.0f);

    static const char* shortNames[] = { "KICK", "CLAP", "HAT", "OPEN", "PERC", "RIM", "FX" };
    for (int l = 0; l < gen::numDrumLanes; ++l)
    {
        const float y = grid.getY() + rowH * static_cast<float> (l);
        if (l % 2 == 1)
        {
            g.setColour (juce::Colour (0xff101015));
            g.fillRect (grid.getX(), y, grid.getWidth(), rowH);
        }
        g.setColour (Colours::textDim);
        g.setFont (uiFont (juce::jmin (10.0f, rowH * 0.85f), true));
        g.drawText (shortNames[l], juce::Rectangle<float> (bounds.getX() + 6.0f, y, 50.0f, rowH), juce::Justification::centredLeft);
    }

    for (int step = 1; step < static_cast<int> (len * 4.0); ++step)
    {
        g.setColour (step % 16 == 0 ? Colours::outline : step % 4 == 0 ? Colours::outline.withAlpha (0.45f) : Colours::outline.withAlpha (0.15f));
        g.drawVerticalLine (juce::roundToInt (xOf (step / 4.0)), grid.getY(), grid.getBottom());
    }

    for (const auto& h : pat.hits)
    {
        const float y = grid.getY() + rowH * static_cast<float> (h.lane);
        const float x = xOf (h.start);
        const float w = h.roll ? juce::jmax (2.0f, xOf (h.start + h.length) - x) : juce::jmax (3.0f, xOf (h.start + 0.22) - x);
        const float vel = static_cast<float> (h.velocity) / 127.0f;
        g.setColour (accent.withMultipliedBrightness (0.45f + 0.55f * vel).withAlpha (h.roll ? 0.85f : 1.0f));
        g.fillRoundedRectangle (x, y + 1.0f, w, rowH - 2.0f, 1.5f);
    }

    if (lastPos >= 0.0)
    {
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.drawVerticalLine (juce::roundToInt (xOf (lastPos)), grid.getY(), grid.getBottom());
    }
    g.setColour (Colours::outline);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);
}

//==============================================================================
PartLane::PartLane (Session& s, Part p, std::function<double()> source)
    : session (s),
      part (p),
      dice ("Re-roll " + juce::String (std::string (gen::partName (p))) + " (new seed, keeps everything else)", Icons::dice(), partColour (p)),
      clear ("Clear hand edits", Icons::undo()),
      counterDice ("Re-roll the counter-melody", Icons::dice(), partColour (Part::Counter)),
      drag (juce::String (std::string (gen::partName (p))), partColour (p), [this]
      {
          DragMidiButton::Payload payload;
          const auto id = juce::String (std::string (gen::partId (part)));
          payload.clips = session.clipsForExport (id);
          payload.bpm = session.tempo();
          payload.fileName = session.exportFileName (id);
          return payload;
      })
{
    dice.onClick = [this] { session.regeneratePart (part); };
    clear.onClick = [this] { session.clearEdits (part); };
    counterDice.onClick = [this] { session.regeneratePart (Part::Counter); };
    drag.setCompact (true);
    for (auto* c : std::initializer_list<juce::Component*> { &dice, &clear, &drag })
        addAndMakeVisible (c);
    addChildComponent (counterDice);

    if (part == Part::Drums)
        addAndMakeVisible (*(drumGrid = std::make_unique<DrumGrid> (session, source)));
    else
        addAndMakeVisible (*(roll = std::make_unique<PartRoll> (session, part, source)));

    session.addChangeListener (this);
    changeListenerCallback (nullptr);
}

PartLane::~PartLane()
{
    session.removeChangeListener (this);
}

void PartLane::changeListenerCallback (juce::ChangeBroadcaster*)
{
    clear.setVisible (session.hasEdits (part) || (part == Part::Melody && session.hasEdits (Part::Counter)));
    counterDice.setVisible (part == Part::Melody && ! session.clip (Part::Counter).notes.empty());
}

void PartLane::tick()
{
    if (roll != nullptr)
        roll->tick();
    if (drumGrid != nullptr)
        drumGrid->tick();
}

void PartLane::resized()
{
    auto b = getLocalBounds();
    auto header = b.removeFromLeft (150);
    b.removeFromLeft (8);
    if (roll != nullptr)
        roll->setBounds (b);
    if (drumGrid != nullptr)
        drumGrid->setBounds (b);

    header.removeFromLeft (10);
    auto top = header.removeFromTop (header.getHeight() / 2).reduced (0, 2);
    top.removeFromLeft (70); // name
    dice.setBounds (top.removeFromLeft (26));
    counterDice.setBounds (top.removeFromLeft (26));
    clear.setBounds (top.removeFromLeft (26));
    drag.setBounds (header.reduced (0, 3).withWidth (130));
}

void PartLane::paint (juce::Graphics& g)
{
    const auto accent = partColour (part);
    auto header = getLocalBounds().removeFromLeft (150).toFloat();
    juce::Path bar;
    bar.addRoundedRectangle (header.removeFromLeft (3.0f).reduced (0.0f, 4.0f), 1.5f);
    drawGlow (g, bar, accent, 5.0f, 1.0f);
    g.setColour (accent);
    g.fillPath (bar);

    g.setColour (Colours::text);
    g.setFont (uiFont (13.0f, true));
    g.drawText (juce::String (std::string (gen::partName (part))).toUpperCase(),
                header.withTrimmedLeft (7.0f).removeFromTop (header.getHeight() / 2.0f), juce::Justification::centredLeft);
}

} // namespace bounce::ui
