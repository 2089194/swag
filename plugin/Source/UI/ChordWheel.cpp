#include "UI/ChordWheel.h"

#include "UI/BounceLookAndFeel.h"
#include "UI/Widgets.h"

#include "bounce/theory/Voicing.h"

namespace bounce::ui
{

using juce::MathConstants;

void showSlotMenu (Session& session, int slot, juce::Component& target)
{
    const auto& prog = session.progression();
    if (slot < 0 || slot >= static_cast<int> (prog.slots.size()))
        return;

    const auto& s = prog.slots[static_cast<size_t> (slot)];
    juce::PopupMenu m;
    m.addSectionHeader (juce::String (s.chord.name (prog.key.preferredSpelling())) + "   "
                        + juce::String (theory::romanNumeral (s.chord, prog.key)));
    m.addItem (1, s.locked ? "Unlock" : "Lock", true, s.locked);
    m.addItem (2, "Reharmonise");
    m.addSeparator();
    m.addItem (3, "Invert up");
    m.addItem (4, "Invert down");
    m.addItem (5, "Octave up", s.octave < 2);
    m.addItem (6, "Octave down", s.octave > -2);

    juce::PopupMenu voicing;
    voicing.addItem (100, "Follow global", true, ! s.voicing.has_value());
    for (int i = 0; i < static_cast<int> (theory::VoicingStyle::NumStyles); ++i)
    {
        const auto style = static_cast<theory::VoicingStyle> (i);
        voicing.addItem (101 + i, juce::String (std::string (theory::voicingStyleName (style))), true, s.voicing == style);
    }
    m.addSubMenu ("Voicing", voicing);
    m.addItem (7, "Reset voicing edits", s.inversion.has_value() || s.voicing.has_value() || s.octave != 0);
    m.addSeparator();
    const bool canResize = prog.slots.size() > 1;
    m.addItem (8, "Longer (+1 beat)", canResize);
    m.addItem (9, "Shorter (-1 beat)", canResize && prog.slotLength (slot) > 1.0);
    m.addItem (10, "Even chord lengths", prog.hasCustomLengths());

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target),
                     [&session, slot] (int result)
    {
        switch (result)
        {
            case 1: session.toggleLock (slot); break;
            case 2: session.reharmonise (slot); break;
            case 3: session.invert (slot, 1); break;
            case 4: session.invert (slot, -1); break;
            case 5: session.shiftOctave (slot, 1); break;
            case 6: session.shiftOctave (slot, -1); break;
            case 7:
                session.setSlotVoicing (slot, std::nullopt);
                session.shiftOctave (slot, -session.progression().slots[static_cast<size_t> (slot)].octave);
                break;
            case 8: session.setSlotLength (slot, session.progression().slotLength (slot) + 1.0); break;
            case 9: session.setSlotLength (slot, session.progression().slotLength (slot) - 1.0); break;
            case 10: session.resetSlotLengths(); break;
            case 100: session.setSlotVoicing (slot, std::nullopt); break;
            default:
                if (result > 100)
                    session.setSlotVoicing (slot, static_cast<theory::VoicingStyle> (result - 101));
                break;
        }
    });
}

//==============================================================================
ChordWheel::ChordWheel (Session& s, std::function<double()> source)
    : session (s), loopPosition (std::move (source))
{
    session.addChangeListener (this);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

ChordWheel::~ChordWheel()
{
    session.removeChangeListener (this);
}

void ChordWheel::tick()
{
    const double pos = loopPosition ? loopPosition() : -1.0;
    const auto& prog = session.progression();
    const int playing = pos >= 0.0 ? prog.slotAt (pos) : -1;

    bool glowChanged = false;
    for (int i = 0; i < 16; ++i)
    {
        const float target = i == playing ? 1.0f : 0.0f;
        const float next = activeGlow[i] + (target - activeGlow[i]) * (target > activeGlow[i] ? 0.5f : 0.12f);
        if (std::abs (next - activeGlow[i]) > 0.002f)
            glowChanged = true;
        activeGlow[i] = std::abs (next - target) < 0.002f ? target : next;
    }

    if (glowChanged || std::abs (pos - lastPos) > 1.0e-4)
    {
        lastPos = pos;
        repaint();
    }
}

juce::Rectangle<float> ChordWheel::wheelBounds() const
{
    const auto b = getLocalBounds().toFloat();
    const float size = juce::jmin (b.getWidth(), b.getHeight()) - 16.0f;
    return b.withSizeKeepingCentre (size, size);
}

int ChordWheel::slotAtPoint (juce::Point<float> p) const
{
    const auto wb = wheelBounds();
    const auto c = wb.getCentre();
    const float r = c.getDistanceFrom (p);
    const float outer = wb.getWidth() / 2.0f - 14.0f;
    if (r > outer || r < outer * 0.6f)
        return -1;

    float angle = std::atan2 (p.x - c.x, c.y - p.y); // clockwise from 12 o'clock
    if (angle < 0.0f)
        angle += MathConstants<float>::twoPi;
    const auto& prog = session.progression();
    return prog.slotAt (angle / MathConstants<float>::twoPi * prog.lengthBeats());
}

void ChordWheel::mouseDown (const juce::MouseEvent& e)
{
    const int slot = slotAtPoint (e.position);
    if (slot < 0)
        return;
    session.setSelectedSlot (slot);
    if (e.mods.isPopupMenu())
        showSlotMenu (session, slot, *this);
}

void ChordWheel::mouseMove (const juce::MouseEvent& e)
{
    const int slot = slotAtPoint (e.position);
    if (slot != hoverSlot)
    {
        hoverSlot = slot;
        repaint();
    }
}

void ChordWheel::mouseExit (const juce::MouseEvent&)
{
    hoverSlot = -1;
    repaint();
}

void ChordWheel::paint (juce::Graphics& g)
{
    using juce::Path;
    using juce::PathStrokeType;

    const auto& prog = session.progression();
    const auto spelling = prog.key.preferredSpelling();
    const auto accent = Colours::chords;
    const auto wb = wheelBounds();
    const auto c = wb.getCentre();
    const float outer = wb.getWidth() / 2.0f - 14.0f;
    const float inner = outer * 0.6f;
    const double len = prog.lengthBeats();
    const int n = static_cast<int> (prog.slots.size());
    const int selected = session.getSelectedSlot();
    const double pos = lastPos;

    // Backdrop disc + soft accent haze.
    g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.10f), c.x, c.y, juce::Colours::transparentBlack,
                                             c.x + outer * 1.15f, c.y, true));
    g.fillEllipse (wb.expanded (8.0f));
    g.setColour (Colours::well);
    g.fillEllipse (c.x - outer - 6.0f, c.y - outer - 6.0f, (outer + 6.0f) * 2.0f, (outer + 6.0f) * 2.0f);

    // Beat ticks around the rim.
    for (int beat = 0; beat < static_cast<int> (len); ++beat)
    {
        const float a = static_cast<float> (beat / len) * MathConstants<float>::twoPi;
        const bool barLine = beat % prog.beatsPerBar == 0;
        const auto p1 = c.getPointOnCircumference (outer + 3.0f, a);
        const auto p2 = c.getPointOnCircumference (outer + (barLine ? 11.0f : 7.0f), a);
        g.setColour (barLine ? Colours::textDim : Colours::textFaint);
        g.drawLine ({ p1, p2 }, barLine ? 1.6f : 1.0f);
    }

    // Segments.
    const float gap = n > 1 ? 0.03f : 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const auto& slot = prog.slots[static_cast<size_t> (i)];
        const float a0 = static_cast<float> (prog.slotStart (i) / len) * MathConstants<float>::twoPi + gap;
        const float a1 = static_cast<float> ((prog.slotStart (i) + prog.slotLength (i)) / len) * MathConstants<float>::twoPi - gap;

        Path seg;
        seg.addPieSegment (c.x - outer, c.y - outer, outer * 2.0f, outer * 2.0f, a0, a1, inner / outer);

        const float glow = i < 16 ? activeGlow[i] : 0.0f;
        const bool isSel = i == selected;

        auto base = Colours::panelLight.interpolatedWith (accent.withMultipliedSaturation (0.7f).withMultipliedBrightness (0.55f), 0.15f + 0.6f * glow);
        if (isSel)
            base = base.interpolatedWith (accent, 0.18f);
        if (i == hoverSlot)
            base = base.brighter (0.12f);

        if (glow > 0.01f)
            drawGlow (g, seg, accent.withAlpha (glow), 12.0f * glow, 1.0f);

        g.setGradientFill (juce::ColourGradient (base.brighter (0.15f), c.x, c.y - outer, base.darker (0.25f), c.x, c.y + outer, false));
        g.fillPath (seg);
        g.setColour (isSel ? accent : Colours::outline.brighter (0.1f));
        g.strokePath (seg, PathStrokeType (isSel ? 1.6f : 1.0f));

        // Labels at the middle of the segment.
        const float mid = (a0 + a1) / 2.0f;
        const auto lp = c.getPointOnCircumference ((inner + outer) / 2.0f, mid);
        const float span = (a1 - a0) * (inner + outer) / 2.0f;
        const float fontSize = juce::jlimit (11.0f, 19.0f, span / 4.2f);

        g.setColour (glow > 0.5f ? juce::Colours::white : Colours::text);
        g.setFont (uiFont (fontSize, true));
        g.drawText (juce::String (slot.chord.name (spelling)),
                    juce::Rectangle<float> (span + 20.0f, fontSize + 4.0f).withCentre (lp.translated (0.0f, -fontSize * 0.4f)),
                    juce::Justification::centred);

        g.setColour (glow > 0.5f ? accent.brighter (0.6f) : Colours::textDim);
        g.setFont (uiFont (fontSize * 0.68f));
        g.drawText (juce::String (theory::romanNumeral (slot.chord, prog.key)),
                    juce::Rectangle<float> (span + 20.0f, fontSize).withCentre (lp.translated (0.0f, fontSize * 0.62f)),
                    juce::Justification::centred);

        if (slot.locked)
        {
            auto icon = Icons::lock (true);
            const auto ip = c.getPointOnCircumference (outer - 14.0f, mid);
            icon.applyTransform (juce::AffineTransform::scale (11.0f).translated (ip.x - 5.5f, ip.y - 5.5f));
            g.setColour (accent);
            g.fillPath (icon);
        }
    }

    // Playhead: progress arc + glowing dot on the rim.
    if (pos >= 0.0)
    {
        const float a = static_cast<float> (pos / len) * MathConstants<float>::twoPi;
        Path progress;
        progress.addCentredArc (c.x, c.y, outer + 7.0f, outer + 7.0f, 0.0f, 0.0f, a, true);
        g.setColour (accent.withAlpha (0.5f));
        g.strokePath (progress, PathStrokeType (2.0f));

        const auto dot = c.getPointOnCircumference (outer + 7.0f, a);
        Path d;
        d.addEllipse (dot.x - 4.0f, dot.y - 4.0f, 8.0f, 8.0f);
        drawGlow (g, d, accent, 8.0f, 1.0f);
        g.setColour (juce::Colours::white);
        g.fillPath (d);
    }

    // Centre: the sounding (or selected) chord, big.
    const float hubR = inner - 10.0f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1f1f27), c.x, c.y - hubR, juce::Colour (0xff0e0e12), c.x, c.y + hubR, false));
    g.fillEllipse (c.x - hubR, c.y - hubR, hubR * 2.0f, hubR * 2.0f);
    g.setColour (Colours::outline);
    g.drawEllipse (c.x - hubR, c.y - hubR, hubR * 2.0f, hubR * 2.0f, 1.0f);

    const int shown = pos >= 0.0 ? prog.slotAt (pos) : selected;
    if (shown >= 0 && shown < n)
    {
        const auto& slot = prog.slots[static_cast<size_t> (shown)];
        g.setColour (Colours::text);
        g.setFont (uiFont (hubR * 0.36f, true));
        g.drawText (juce::String (slot.chord.name (spelling)),
                    juce::Rectangle<float> (hubR * 1.9f, hubR * 0.45f).withCentre (c.translated (0.0f, -hubR * 0.12f)),
                    juce::Justification::centred);
        g.setColour (accent);
        g.setFont (uiFont (hubR * 0.16f, true));
        g.drawText (juce::String (theory::romanNumeral (slot.chord, prog.key)),
                    juce::Rectangle<float> (hubR * 1.6f, hubR * 0.2f).withCentre (c.translated (0.0f, hubR * 0.22f)),
                    juce::Justification::centred);
    }

    g.setColour (Colours::textDim);
    g.setFont (uiFont (juce::jmax (10.0f, hubR * 0.11f)));
    juce::String footer = juce::String (prog.key.name());
    if (pos >= 0.0)
    {
        const int bar = static_cast<int> (pos / prog.beatsPerBar) + 1;
        const int beat = static_cast<int> (std::fmod (pos, static_cast<double> (prog.beatsPerBar))) + 1;
        footer << "   " << bar << "." << beat;
    }
    else
        footer << "   " << prog.bars << " bars";
    g.drawText (footer, juce::Rectangle<float> (hubR * 1.6f, hubR * 0.16f).withCentre (c.translated (0.0f, hubR * 0.5f)),
                juce::Justification::centred);
}

} // namespace bounce::ui
