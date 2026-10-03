#pragma once

#include "bounce/gen/Progression.h"
#include "bounce/midi/MidiClip.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace bounce::gen
{

enum class MelodyFeel
{
    Straight,
    Swung,
    Triplet,
    NumFeels
};

std::string_view melodyFeelName (MelodyFeel f);

struct MelodyParams
{
    double density = 0.5;      // notes per bar, 0 = sparse (2) .. 1 = busy (8)
    int rangeLow = 67;         // MIDI notes (G4 .. D6 by default: bell/pluck register)
    int rangeHigh = 86;
    MelodyFeel feel = MelodyFeel::Straight;
    double swing = 0.55;       // used when feel == Swung
    double repetition = 0.6;   // how often bars reuse the motif's rhythm
    double catchiness = 0.6;   // how often they reuse its contour (with small variations)
    bool callResponse = true;  // odd bars answer even bars and resolve
    double pentatonic = 0.6;   // preference for pentatonic scale tones
    int channel = 2;
    uint64_t seed = 1;

    /** Bars to keep from `lockedNotes` (bit i = bar i). Everything else is regenerated. */
    uint32_t lockedBars = 0;
    std::vector<midi::Note> lockedNotes;
};

/** Generates a catchy scale/chord-tone motif with repetition, variation and call & response.
    Strong-beat notes land on chord tones; every note is in the key. Deterministic per seed;
    each bar has its own random stream so locking/regenerating a bar leaves the others alone. */
midi::MidiClip generateMelody (const Progression& progression, const MelodyParams& params);

struct CounterParams
{
    int rangeLow = 55;
    int rangeHigh = 74;
    double density = 0.5;
    int channel = 3;
    uint64_t seed = 1;
};

/** A second line that fills the main melody's gaps (onsets only where the main line rests),
    moves against it (contrary motion) and avoids 2nds/7ths/tritones against sounding notes. */
midi::MidiClip generateCounterMelody (const Progression& progression, const midi::MidiClip& main, const CounterParams& params);

} // namespace bounce::gen
