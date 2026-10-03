#pragma once

#include "bounce/gen/DrumGenerator.h"
#include "bounce/gen/Progression.h"
#include "bounce/midi/MidiClip.h"
#include "bounce/util/Json.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace bounce::gen
{

enum class Part
{
    Chords,
    Bass,
    Melody,
    Counter,
    Drums,
    NumParts
};

inline constexpr int numParts = static_cast<int> (Part::NumParts);
std::string_view partName (Part p);
std::string_view partId (Part p);

/** Hand edits made in a part's mini piano roll, applied on top of the generated notes.
    They survive knob tweaks (which re-render the part) and are cleared when the part is
    re-rolled with a new seed. For drums, `pitch` holds the lane index. */
struct PartEdits
{
    struct NoteRef
    {
        int pitch = 0;
        int64_t tick = 0; // start in 1/960 beats

        bool operator== (const NoteRef&) const = default;
    };

    std::vector<midi::Note> added;
    std::vector<NoteRef> removed;

    bool operator== (const PartEdits&) const = default;
    bool empty() const { return added.empty() && removed.empty(); }

    static int64_t toTick (double beats);

    /** Removes the generated note (if `n` came from the generator) or the added note. */
    void removeNote (const midi::Note& n);
    void addNote (const midi::Note& n);

    void apply (midi::MidiClip& clip) const;
    void applyToDrums (DrumPattern& pattern) const;

    util::Json toJson() const;
    static PartEdits fromJson (const util::Json& j);
};

/** Everything that defines one idea apart from host parameters: the chord loop, a seed per
    part, the melody's locked bars, and hand edits. The generated parts are derived from this,
    so turning a knob re-renders deterministically. Undo/redo and the idea history store Ideas. */
struct Idea
{
    Progression chords;
    uint64_t bassSeed = 1;
    uint64_t melodySeed = 1;
    uint64_t drumSeed = 1;

    uint32_t melodyLockedBars = 0;
    std::vector<midi::Note> melodyLockedNotes;

    std::array<PartEdits, numParts> edits;

    bool operator== (const Idea&) const = default;

    PartEdits& editsFor (Part p) { return edits[static_cast<size_t> (p)]; }
    const PartEdits& editsFor (Part p) const { return edits[static_cast<size_t> (p)]; }

    /** Transposes chords, locked melody notes and pitched edits (not drum edits). */
    Idea transposed (int semitones) const;

    util::Json toJson() const;
    static std::optional<Idea> fromJson (const util::Json& j);
};

} // namespace bounce::gen
