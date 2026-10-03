#pragma once

#include "bounce/midi/MidiClip.h"
#include "bounce/util/Json.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bounce::gen
{

enum class DrumLane
{
    Kick,
    Snare,     // snare / clap
    ClosedHat,
    OpenHat,
    Perc,
    Rim,
    FX,
    NumLanes
};

inline constexpr int numDrumLanes = static_cast<int> (DrumLane::NumLanes);

std::string_view drumLaneName (DrumLane lane);

/** Hat-roll subdivisions, combinable as a bit mask. */
enum RollRate : int
{
    Roll16  = 1,
    Roll16T = 2,
    Roll32  = 4,
    Roll32T = 8,
};

/** Beats per hit for a single roll rate. */
double rollStep (RollRate rate);

enum class RollCurve
{
    Up,   // crescendo
    Down, // decrescendo
    Flat,
    Wave,
    NumCurves
};

std::string_view rollCurveName (RollCurve c);

struct DrumHit
{
    DrumLane lane = DrumLane::Kick;
    double start = 0.0;   // beats
    double length = 0.25; // beats
    int velocity = 100;
    int pitchOffset = 0;  // semitones (pitched rolls)
    bool roll = false;

    bool operator== (const DrumHit&) const = default;
};

struct DrumPattern
{
    int bars = 4;
    int beatsPerBar = 4;
    std::vector<DrumHit> hits;

    double lengthBeats() const { return static_cast<double> (bars * beatsPerBar); }

    /** Kick onsets in beats (sorted), used to lock the 808 to the kick. */
    std::vector<double> kickTimes() const;
    std::vector<DrumHit> lane (DrumLane l) const;
};

/** Style-specific drum vocabulary (from a style preset's "drums" section). */
struct DrumStyle
{
    /** Kick patterns as 16th steps within a bar (0..15), with a weight each. */
    std::vector<std::pair<std::vector<int>, double>> kickPatterns;
    std::vector<std::pair<std::vector<int>, double>> percPatterns;
    std::vector<int> openHatSteps { 6, 14 };
    bool hats16ths = false; // base hat grid: 8ths (false) or 16ths
    bool halfTime = true;   // snare/clap on beat 3 (vs 2 & 4)

    static DrumStyle defaults();
    static DrumStyle fromJson (const util::Json& json, std::vector<std::string>* warnings = nullptr);
    util::Json toJson() const;
};

struct DrumParams
{
    int bars = 4;
    double bpm = 150.0;          // for ms-based humanise
    double kickDensity = 0.5;    // 0..1
    double swing = 0.1;          // 0..1
    double humanise = 0.4;       // 0..1 timing + velocity
    double bounce = 0.3;         // nudges non-downbeat kicks and percs late (off-grid)
    double rollAmount = 0.5;     // chance of a hat roll per bar
    int rollRates = Roll16 | Roll16T | Roll32; // enabled subdivisions
    double rollPitch = 0.0;      // semitones ramp across a roll (-12..12)
    RollCurve rollCurve = RollCurve::Up;
    double openHatAmount = 0.4;  // chance per bar
    double percDensity = 0.4;    // percs, rims, ghost snares
    bool fx = true;              // crash/FX at the top of the loop
    uint64_t seed = 1;
};

/** Generates a bouncy, swung drum loop. Pure and deterministic for a seed.
    Kick on the loop downbeat, snare/clap on beat 3 (half-time), hats with rolls and stutters,
    open hats on off-beats, perc/rim fills. Swing/bounce/humanise are applied to the result
    (rolls stay straight so they sound tight). */
DrumPattern generateDrums (const DrumParams& params, const DrumStyle& style);

//==============================================================================
/** Lane -> MIDI note mapping for output to FPC, a GM kit or a custom sampler. */
struct DrumMap
{
    std::array<int, numDrumLanes> notes {};

    /** General MIDI layout, which FL's FPC default kits also follow:
        36 kick, 38 snare, 42 closed hat, 46 open hat, 63 conga (perc), 37 side stick (rim), 49 crash. */
    static DrumMap gm();
};

/** Renders a pattern to MIDI. If `pitchToNotes`, pitched-roll offsets shift the note number
    (useful when a lane drives a single pitched sample); otherwise they're ignored. */
midi::MidiClip renderDrums (const DrumPattern& pattern, const DrumMap& map, int channel, bool pitchToNotes);

/** Internal-sampler encoding used by the plugin: note = 8 + lane * 16 + pitchOffset (clamped
    to -8..7), so the built-in sampler can play pitched rolls without touching the MIDI-out map. */
midi::MidiClip renderDrumsInternal (const DrumPattern& pattern, int channel);
int internalDrumNote (DrumLane lane, int pitchOffset);
void decodeInternalDrumNote (int note, DrumLane& lane, int& pitchOffset);

} // namespace bounce::gen
