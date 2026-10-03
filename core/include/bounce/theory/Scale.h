#pragma once

#include "bounce/theory/Pitch.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bounce::theory
{

enum class ScaleType
{
    Major,
    NaturalMinor,
    Dorian,
    Phrygian,
    Lydian,
    Mixolydian,
    HarmonicMinor,
    MelodicMinor,
    MajorPentatonic,
    MinorPentatonic,
    NumTypes
};

/** Display name, e.g. "Natural Minor". */
std::string_view scaleTypeName (ScaleType type);

/** Stable identifier used in presets/state, e.g. "natural_minor". */
std::string_view scaleTypeId (ScaleType type);
std::optional<ScaleType> scaleTypeFromId (std::string_view id);

/** Semitone offsets from the tonic. */
const std::vector<int>& scaleIntervals (ScaleType type);

/** True for modes whose tonic triad is minor. */
bool isMinorMode (ScaleType type);

/** A tonic + scale type. */
struct Key
{
    PitchClass tonic = 0;
    ScaleType scale = ScaleType::Major;

    bool operator== (const Key&) const = default;

    /** Pitch classes of the scale in ascending order from the tonic. */
    std::vector<PitchClass> pitchClasses() const;

    /** Bit mask of the scale's pitch classes (bit n = pc n). */
    unsigned mask() const;

    bool contains (PitchClass pc) const;

    /** Seven-note diatonic parent used for chord building (pentatonics map to major/minor). */
    Key heptatonicParent() const;

    /** Accidental spelling that reads naturally in this key. */
    Spelling preferredSpelling() const;

    /** e.g. "C", "Am", "F# Dorian". */
    std::string name() const;

    /** Relative major/minor (Ionian <-> Aeolian); other modes return themselves. */
    Key relative() const;

    /** Transpose the tonic by n semitones. */
    Key transposed (int semitones) const;

    /** Nearest scale degree (0-based) for a pitch class, or nullopt if not in the scale. */
    std::optional<int> degreeOf (PitchClass pc) const;
};

} // namespace bounce::theory
