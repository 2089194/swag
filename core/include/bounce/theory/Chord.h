#pragma once

#include "bounce/theory/Scale.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bounce::theory
{

enum class ChordQuality
{
    Major,
    Minor,
    Diminished,
    Augmented,
    Sus2,
    Sus4,
    Major7,
    Minor7,
    Dominant7,
    HalfDiminished7,
    Diminished7,
    MinorMajor7,
    Major6,
    Minor6,
    Add9,
    MinorAdd9,
    SixNine,
    MinorSixNine,
    Major9,
    Minor9,
    Dominant9,
    Minor11,
    Major7Sharp11,
    Dominant7Sus4,
    Major7Sus2,
    NumQualities
};

/** Broad triad family, used for roman numeral case and mood decisions. */
enum class ChordFamily
{
    Major,
    Minor,
    Diminished,
    Augmented,
    Suspended
};

/** Chord symbol suffix, e.g. "m9", "maj7", "6/9". */
std::string_view qualitySuffix (ChordQuality q);

/** Stable identifier for presets/state, e.g. "min9". */
std::string_view qualityId (ChordQuality q);
std::optional<ChordQuality> qualityFromId (std::string_view id);

/** Intervals above the root in chord-tone order (root, 3rd, 5th, 7th, 9th, ...).
    Extensions are kept above the octave (9th = 14) to help voicing. */
const std::vector<int>& qualityIntervals (ChordQuality q);

ChordFamily qualityFamily (ChordQuality q);

/** Number of distinct tones (3 for triads, 4 for 7ths/add9 ...). */
int qualityToneCount (ChordQuality q);

/** Finds the quality whose pitch-class set exactly matches the given intervals (mod 12). */
std::optional<ChordQuality> qualityFromIntervalSet (const std::vector<int>& intervals);

struct Chord
{
    PitchClass root = 0;
    ChordQuality quality = ChordQuality::Major;
    std::optional<PitchClass> bass; // slash-chord bass, if different from the root

    bool operator== (const Chord&) const = default;

    /** e.g. "Fmaj7", "C#m9", "Ab6/9", "C/E". */
    std::string name (Spelling spelling) const;

    /** Pitch classes in chord-tone order (root first), plus the slash bass if any. */
    std::vector<PitchClass> pitchClasses() const;

    ChordFamily family() const { return qualityFamily (quality); }

    Chord transposed (int semitones) const;

    /** True when every chord tone belongs to the key. */
    bool isDiatonicTo (const Key& key) const;
};

/** Parses chord symbols like "Fmaj7", "Em9", "Bbadd9", "C/E", "F#m7b5". */
std::optional<Chord> parseChord (std::string_view symbol);

/** Builds a chord by stacking scale thirds on the given degree (0-based).
    numTones: 3 = triad, 4 = 7th, 5 = 9th, 6 = 11th. Falls back to fewer tones
    when the stacked result is not a recognised quality (e.g. a b9). */
Chord diatonicChord (const Key& key, int degree, int numTones);

/** Roman numeral of a chord relative to the key's tonic, e.g. "IVmaj7", "vi9", "bVI", "ii\u00F87".
    Always measured against the major scale, so in A minor the F chord is "bVI". */
std::string romanNumeral (const Chord& chord, const Key& key);

} // namespace bounce::theory
