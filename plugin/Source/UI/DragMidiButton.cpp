#include "UI/DragMidiButton.h"

#include "Midi/MidiExport.h"
#include "UI/BounceLookAndFeel.h"
#include "UI/Widgets.h"

namespace bounce::ui
{

DragMidiButton::DragMidiButton (const juce::String& l, juce::Colour a, std::function<Payload()> source)
    : label (l), accent (a), payloadSource (std::move (source))
{
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    setTooltip ("Drag into FL Studio's Piano Roll or Playlist (or any DAW / folder)");
}

void DragMidiButton::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 5 || ! payloadSource)
        return;

    const auto payload = payloadSource();
    const auto file = MidiExport::writeTempFile (payload.clips, payload.bpm, payload.fileName);
    if (! file.existsAsFile())
    {
        status = "Couldn't write MIDI file";
        repaint();
        return;
    }

    dragging = true;
    status = file.getFileName();
    repaint();

    juce::Component::SafePointer<DragMidiButton> safe (this);
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this,
                                                                [safe]
    {
        if (safe != nullptr)
        {
            safe->dragging = false;
            safe->repaint();
        }
    });
}

void DragMidiButton::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
    repaint();
}

void DragMidiButton::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    const bool hot = isMouseOver() || dragging;

    juce::Path shape;
    shape.addRoundedRectangle (b, compact ? 6.0f : 10.0f);
    if (hot)
        drawGlow (g, shape, accent, 8.0f, 1.0f);

    g.setColour (hot ? accent.withAlpha (0.16f) : Colours::well);
    g.fillPath (shape);

    // Dashed border reads as "drag from here".
    juce::Path dashed;
    const float dashes[] = { 5.0f, 4.0f };
    juce::PathStrokeType (1.2f).createDashedStroke (dashed, shape, dashes, 2);
    g.setColour (hot ? accent : Colours::textFaint);
    g.fillPath (dashed);

    if (compact)
    {
        auto iconArea = b.removeFromLeft (b.getHeight()).reduced (5.0f);
        auto icon = Icons::drag();
        icon.applyTransform (juce::AffineTransform::scale (iconArea.getHeight()).translated (iconArea.getX(), iconArea.getY()));
        g.setColour (hot ? accent : Colours::text);
        g.fillPath (icon);
        g.setFont (uiFont (11.0f, true));
        g.drawFittedText (label, b.withTrimmedRight (4.0f).toNearestInt(), juce::Justification::centredLeft, 1);
        return;
    }

    auto content = b.reduced (12.0f, 8.0f);
    auto iconArea = content.removeFromLeft (content.getHeight()).withSizeKeepingCentre (26.0f, 26.0f);
    auto icon = Icons::drag();
    icon.applyTransform (juce::AffineTransform::scale (iconArea.getWidth()).translated (iconArea.getX(), iconArea.getY()));
    g.setColour (hot ? accent : Colours::text);
    g.fillPath (icon);

    content.removeFromLeft (10.0f);
    g.setColour (Colours::text);
    g.setFont (uiFont (14.0f, true));
    g.drawText (label, content.removeFromTop (content.getHeight() * 0.55f), juce::Justification::bottomLeft);

    g.setColour (Colours::textDim);
    g.setFont (uiFont (11.0f));
    g.drawFittedText (status.isNotEmpty() ? status : juce::String ("to Piano Roll / Playlist"),
                      content.toNearestInt(), juce::Justification::topLeft, 1);
}

} // namespace bounce::ui
