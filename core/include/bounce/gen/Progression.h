#pragma once

#include "bounce/gen/Harmony.h"
#include "bounce/theory/Chord.h"
#include "bounce/theory/Voicing.h"
#include "bounce/util/Json.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bounce::gen
{

/** One chord in the loop plus the per-chord edits the user has made. */
struct ChordSlot
{
    theory::Chord chord;
    HarmonyFunction function;
    bool locked = false;

    /** Per-chord voicing overrides (nullopt = follow the global setting / voice leading). */
    std::optional<int> inversion;
    std::optional<theory::VoicingStyle> voicing;
    int octave = 0;

    bool operator== (const ChordSlot&) const = default;
};

/** A chord loop. By default chords share the loop evenly (on a half-beat grid); with
    `customLengths` (one entry per slot, summing to the loop length) each chord has its own
    duration, e.g. for progressions detected from audio or "one chord, then two" patterns. */
struct Progression
{
    theory::Key key;
    int bars = 4;
    int beatsPerBar = 4;
    std::vector<ChordSlot> slots;
    std::vector<double> customLengths; // empty = even split
    uint64_t seed = 0;
    std::string styleId;

    bool operator== (const Progression&) const = default;

    double lengthBeats() const { return static_cast<double> (bars * beatsPerBar); }

    bool hasCustomLengths() const;

    /** Sets slot i's length (beats, half-beat grid), taking/giving time from the next slot
        (or the previous one for the last slot). Converts to custom lengths. */
    void setSlotLength (int i, double beats);

    /** Back to an even split. */
    void clearCustomLengths() { customLengths.clear(); }

    /** Start beat / length of slot i. */
    double slotStart (int i) const;
    double slotLength (int i) const;

    /** Index of the slot sounding at a beat position (wrapped into the loop). */
    int slotAt (double beat) const;

    /** "Fmaj7 - Em9 - Am9 - G6/9" */
    std::string chordNames() const;

    /** Every chord transposed (key too). Locks and edits are kept. */
    Progression transposed (int semitones) const;

    util::Json toJson() const;
    static std::optional<Progression> fromJson (const util::Json& json);
};

} // namespace bounce::gen
