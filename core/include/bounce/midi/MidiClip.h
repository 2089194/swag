#pragma once

#include <string>
#include <vector>

namespace bounce::midi
{

/** A note in beats (quarter notes) from the start of the clip. */
struct Note
{
    int pitch = 60;
    double start = 0.0;
    double length = 1.0;
    int velocity = 100;
    int channel = 0; // 0-based

    bool operator== (const Note&) const = default;
};

/** A controller change (e.g. CC65 portamento on, CC5 portamento time, CC74 filter sweep). */
struct ControlChange
{
    double time = 0.0;
    int controller = 0;
    int value = 0;
    int channel = 0;

    bool operator== (const ControlChange&) const = default;
};

/** A marker (SMF meta 0x06), e.g. arrangement section names. */
struct Marker
{
    double time = 0.0;
    std::string text;

    bool operator== (const Marker&) const = default;
};

/** One generated part (chords, bass, melody, drums...). */
struct MidiClip
{
    std::string name;          // track name, e.g. "Chords"
    double lengthBeats = 16.0; // loop length
    std::vector<Note> notes;
    std::vector<ControlChange> controls;
    std::vector<Marker> markers;

    void sortByTime();

    /** Returns this clip repeated to fill `beats` (notes crossing the end are clipped). */
    MidiClip tiled (double beats) const;
};

} // namespace bounce::midi
