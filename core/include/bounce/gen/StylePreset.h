#pragma once

#include "bounce/gen/BassGenerator.h"
#include "bounce/gen/Harmony.h"
#include "bounce/gen/DrumGenerator.h"
#include "bounce/gen/MelodyGenerator.h"
#include "bounce/theory/Chord.h"
#include "bounce/theory/Voicing.h"
#include "bounce/util/Json.h"

#include <map>
#include <string>
#include <vector>

namespace bounce::gen
{

/** A loop shape the chord generator builds from, e.g. { "i", "bVI", "bIII", "bVII" }. */
struct ProgressionTemplate
{
    std::vector<std::string> chords; // canonical harmony tokens
    double weight = 1.0;

    bool operator== (const ProgressionTemplate&) const = default;
};

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

    /** Loop shapes, the backbone of generation. A preset's list replaces the built-in one.
        The Markov tables are still used to reharmonise single chords and to fill around locks. */
    std::vector<ProgressionTemplate> majorProgressions;
    std::vector<ProgressionTemplate> minorProgressions;

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

    // 808 defaults ("bass" section).
    BassMode bassMode = BassMode::SyncopatedBounce;
    double bassDensity = 0.5;
    double bassGlide = 0.45;
    bool bassLockToKick = true;

    // Melody defaults ("melody" section).
    double melodyDensity = 0.5;
    MelodyFeel melodyFeel = MelodyFeel::Straight;
    double melodyPentatonic = 0.6;

    // Drums ("drums" section): vocabulary + default feel.
    DrumStyle drums = DrumStyle::defaults();
    double drumSwing = 0.12;
    double rollAmount = 0.5;
    double percDensity = 0.4;
    double openHatAmount = 0.4;

    /** The default "Swag Bounce" preset, used when no JSON is available. */
    static StylePreset defaults();

    /** Builds a preset from JSON, starting from defaults(). Unknown keys are ignored.
        Errors (bad tokens etc.) are appended to `warnings` instead of failing. */
    static StylePreset fromJson (const util::Json& json, std::vector<std::string>* warnings = nullptr);

    util::Json toJson() const;
};

} // namespace bounce::gen
