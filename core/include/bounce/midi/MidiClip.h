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

/** One generated part (chords, bass, melody, drums...). */
struct MidiClip
{
    std::string name;          // track name, e.g. "Chords"
    double lengthBeats = 16.0; // loop length
    std::vector<Note> notes;

    void sortByTime();
};

} // namespace bounce::midi
