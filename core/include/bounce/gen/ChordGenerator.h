#pragma once

#include "bounce/gen/Progression.h"
#include "bounce/gen/StylePreset.h"

#include <cstdint>
#include <vector>

namespace bounce::gen
{

struct ChordGeneratorParams
{
    theory::Key key { 9, theory::ScaleType::NaturalMinor };
    int bars = 4;            // 2, 4 or 8
    int chordCount = 4;      // chords in the loop (1..8)
    double complexity = 0.65;// 0 = triads, 1 = 9ths/11ths
    double mood = 0.0;       // -1 dark .. +1 bright
    double borrowed = 0.35;  // 0 = strictly diatonic .. 1 = lots of modal mixture
    uint64_t seed = 1;
    /** Build from the style's loop shapes (true) or wander with the Markov chain (false). */
    bool useTemplates = true;
};

/** Weighted-Markov chord loop generator driven by a StylePreset.

    Pure and deterministic: the same params, preset, seed and locked slots always give the
    same progression. Each slot draws from its own random sub-stream, so locking one chord
    doesn't reshuffle the random choices made for the others. */
class ChordGenerator
{
public:
    explicit ChordGenerator (StylePreset preset);

    const StylePreset& preset() const { return style; }

    /** Generates a new loop. Slots in `existing` that are locked (and fit the new length)
        are kept as they are; everything else is regenerated. */
    Progression generate (const ChordGeneratorParams& params, const Progression* existing = nullptr) const;

    /** Replaces one chord with a different one that still fits its neighbours.
        `variation` picks a different alternative each time it changes. */
    Progression reharmonise (const Progression& progression, int slotIndex,
                             const ChordGeneratorParams& params, uint64_t variation) const;

    /** All harmony functions available for a key (diatonic + borrowed from the preset). */
    std::vector<HarmonyFunction> availableFunctions (const theory::Key& key, double borrowed) const;

    /** Chord qualities this generator may use for a function in a key. */
    std::vector<theory::ChordQuality> qualitiesFor (const HarmonyFunction& fn, const theory::Key& key) const;

private:
    StylePreset style;

    struct Candidate
    {
        HarmonyFunction function;
        double weight = 0.0;
    };

    double transitionWeight (const theory::Key& key, const std::string& from, const std::string& to) const;
    double startWeight (const theory::Key& key, const std::string& token) const;

    theory::Chord chooseChord (const HarmonyFunction& fn, const ChordGeneratorParams& params,
                               uint64_t streamSeed, const theory::Chord* avoid) const;

    /** Fills the unknown slots from a loop shape. Returns false if no shape fits the locks. */
    bool fillFromTemplate (Progression& prog, std::vector<bool>& known, const ChordGeneratorParams& params) const;

    std::vector<Candidate> candidatesFor (const Progression& prog, int index,
                                          const ChordGeneratorParams& params,
                                          const std::vector<bool>& known) const;
};

} // namespace bounce::gen
