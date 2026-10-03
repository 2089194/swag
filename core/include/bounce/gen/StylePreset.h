#pragma once

#include "bounce/theory/Chord.h"
#include "bounce/theory/Voicing.h"
#include "bounce/util/Json.h"

#include <map>
#include <string>
#include <vector>

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

/** Musical style definition loaded from JSON. Every field has a sensible default so a preset
    only needs to override what makes it distinctive. See presets/styles/README.md. */
struct StylePreset
{
    std::string id = "swag_bounce";
    std::string name = "Swag Bounce";
    std::string description;

    // Tempo defaults (the plugin follows the host; these drive the standalone/preview clock).
    double bpm = 150.0;
    bool halfTimeFeel = true;

    // Harmony defaults.
    theory::Key defaultKey { 9, theory::ScaleType::NaturalMinor };
    int defaultBars = 4;
    int defaultChordCount = 4;
    double defaultComplexity = 0.65;  // 0 = triads, 1 = 9ths/11ths
    double defaultMood = 0.0;         // -1 dark .. +1 bright
    double borrowedChordAmount = 0.35;// 0 = strictly diatonic

    /** Transition weights between harmony functions, separately for major- and minor-tonic modes.
        Missing entries fall back to the built-in functional-harmony table. */
    std::map<std::string, std::map<std::string, double>> majorTransitions;
    std::map<std::string, std::map<std::string, double>> minorTransitions;

    /** Weight of each function as the first chord of the loop. */
    std::map<std::string, double> majorStartWeights;
    std::map<std::string, double> minorStartWeights;

    /** Relative preference for colour qualities, by quality id ("maj7", "min9", "add9", "6_9", ...).
        Applied on top of complexity: a weight of 0 disables that colour. */
    std::map<std::string, double> colourWeights;

    // Voicing / performance defaults.
    theory::VoicingStyle voicing = theory::VoicingStyle::Spread;
    int registerLow = 55;
    int registerHigh = 82;
    std::string chordRhythm = "sustain"; // "sustain" | "half" | "stabs" | "pulse8"

    /** Stab pattern for "stabs": per-bar onsets in 16th steps (0..15) and a gate length in 16ths. */
    std::vector<int> stabSteps { 0, 3, 6, 10, 12 };
    double stabGate = 1.5;

    double swing = 0.0;          // 0..1 (16th swing)
    double strumMs = 12.0;
    double velocity = 92.0;
    double velocityRandom = 8.0;
    double timingRandomMs = 6.0;

    /** The default "Swag Bounce" preset, used when no JSON is available. */
    static StylePreset defaults();

    /** Builds a preset from JSON, starting from defaults(). Unknown keys are ignored.
        Errors (bad tokens etc.) are appended to `warnings` instead of failing. */
    static StylePreset fromJson (const util::Json& json, std::vector<std::string>* warnings = nullptr);

    util::Json toJson() const;
};

} // namespace bounce::gen
