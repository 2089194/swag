#pragma once

#include "bounce/gen/Progression.h"
#include "bounce/gen/StylePreset.h"
#include "bounce/midi/MidiClip.h"
#include "bounce/theory/Voicing.h"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace bounce::gen
{

enum class ChordRhythm
{
    Sustain, // one held chord per slot
    Half,    // re-struck every half bar
    Stabs,   // syncopated gated stabs (flip / chopped-sample feel)
    Pulse8,  // straight 8th pulses
    NumRhythms
};

std::string_view chordRhythmName (ChordRhythm r);
std::string_view chordRhythmId (ChordRhythm r);
std::optional<ChordRhythm> chordRhythmFromId (std::string_view id);

struct ChordPerformance
{
    theory::VoicingStyle voicing = theory::VoicingStyle::Spread;
    int registerLow = 55;
    int registerHigh = 82;

    ChordRhythm rhythm = ChordRhythm::Sustain;
    std::vector<int> stabSteps { 0, 3, 6, 10, 12 }; // 16th steps within a bar
    double stabGate = 1.5;                          // in 16ths

    double bpm = 140.0;     // converts the ms-based humanise values into beats
    double humanise = 0.5;  // 0..1, scales strum/velocity/timing randomness
    double strumMs = 12.0;
    double velocity = 92.0;
    double velocityRandom = 8.0;
    double timingRandomMs = 6.0;
    double swing = 0.0;     // 0..1 delays off-beat 16ths by up to a 32nd
    int channel = 0;

    uint64_t seed = 1;

    /** Fills voicing/rhythm/humanise defaults from a style preset. */
    static ChordPerformance fromPreset (const StylePreset& preset);
};

/** Voiced, voice-led chord tones for every slot (before rhythm/humanise). */
std::vector<theory::Voicing> voiceProgression (const Progression& progression, const ChordPerformance& perf);

/** Turns a progression into playable MIDI notes. Deterministic for a given seed.
    The first chord always starts exactly on beat 0 so loops land on the bar. */
midi::MidiClip renderChords (const Progression& progression, const ChordPerformance& perf);

} // namespace bounce::gen
