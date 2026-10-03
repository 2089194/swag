#pragma once

#include "bounce/theory/Chord.h"

#include <optional>
#include <string>

namespace bounce::gen
{

/** A chord "slot" the harmony engine can choose from, expressed relative to the key:
    a semitone offset from the tonic plus a triad family. E.g. bVI in major = {8, Major}. */
struct HarmonyFunction
{
    std::string symbol;        // roman-numeral token used in preset JSON, e.g. "vi", "bVII", "iv"
    int offset = 0;            // semitones above the tonic
    theory::ChordFamily family = theory::ChordFamily::Major;
    bool borrowed = false;     // not diatonic to the current mode

    bool operator== (const HarmonyFunction&) const = default;
};

/** Parses roman-numeral tokens relative to major ("I", "ii", "bVI", "iv", "bIII", "#iv°").
    Case gives the family; a trailing "°"/"o" makes it diminished. */
std::optional<HarmonyFunction> parseHarmonyToken (const std::string& token);

/** Builds a function with its canonical token (lowercase for minor/dim, "o" for dim, "+" for aug). */
HarmonyFunction makeHarmonyFunction (int offset, theory::ChordFamily family);

/** The plain triad a function stands for in a key. */
theory::Chord functionTriad (const HarmonyFunction& fn, const theory::Key& key);

/** True when the function's triad is diatonic to the key (pentatonics use their 7-note parent). */
bool isDiatonicFunction (const HarmonyFunction& fn, const theory::Key& key);

} // namespace bounce::gen
