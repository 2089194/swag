#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace bounce::theory
{

/** Pitch class 0..11, C = 0. */
using PitchClass = int;

/** Wraps any integer into the 0..11 range. */
constexpr PitchClass wrapPc (int semitones) noexcept
{
    const int r = semitones % 12;
    return r < 0 ? r + 12 : r;
}

/** How accidentals are spelled when naming notes. */
enum class Spelling
{
    Sharps,
    Flats
};

/** Returns e.g. "C", "F#" or "Gb" for a pitch class. */
std::string pitchClassName (PitchClass pc, Spelling spelling);

/** Parses "C", "c#", "Db", "Bb", "E#", "Cb" ... Returns nullopt on failure. */
std::optional<PitchClass> parsePitchClass (std::string_view text);

/** MIDI note name with octave, using C4 = 60 (FL Studio uses C5 = 60; see octaveOffset). */
std::string midiNoteName (int midiNote, Spelling spelling, int octaveOffset = -1);

} // namespace bounce::theory
