#pragma once

#include "bounce/gen/Progression.h"
#include "bounce/midi/MidiClip.h"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace bounce::gen
{

enum class BassMode
{
    RootFollow,       // root on every chord, re-struck on kicks/beats
    SyncopatedBounce, // syncopated 16ths locked to the kick, 5ths and octaves
    OctaveJumper,     // root / octave alternation on the off-beats
    GlideHeavy,       // fewer, longer notes with slides between them
    Sustain,          // one held root per chord
    NumModes
};

std::string_view bassModeName (BassMode m);

struct BassParams
{
    BassMode mode = BassMode::SyncopatedBounce;
    double density = 0.5;    // 0..1 extra hits
    double glide = 0.4;      // 0..1 chance (and time) of slides
    int octaveRange = 2;     // 1 = stay in one octave, 2 = allow octave jumps
    bool lockToKick = true;  // onsets come from the kick pattern
    double noteLength = 0.8; // 0.1..1 of the gap to the next note
    int lowNote = 28;        // bottom of the root register (E1 / FL's E2)
    int channel = 1;
    uint64_t seed = 1;
};

/** Generates an 808 line that follows the chord roots (slash-chord bass notes when present).

    Glides are written the way FL Studio and most samplers expect: the earlier note is
    stretched to overlap the next one, and the clip opens with CC65 (portamento on) and CC5
    (portamento time). The built-in 808 slides whenever notes overlap. The first note always
    sits on beat 0 so loops start on the bar. */
midi::MidiClip generateBass (const Progression& progression, const std::vector<double>& kickTimes, const BassParams& params);

/** Bass notes that slide into the next one (overlap) - used by tests and the UI. */
int countGlides (const midi::MidiClip& clip);

} // namespace bounce::gen
