#pragma once

#include "bounce/theory/Chord.h"

#include <optional>
#include <string_view>
#include <vector>

namespace bounce::theory
{

enum class VoicingStyle
{
    Close,   // all tones packed inside an octave
    Spread,  // root + 5th in the left hand, colour tones (3rd/7th/9th) close in the right hand
    Open,    // drop-2 right hand over a root bass
    ThirdOnTop, // like Spread, but the 3rd is forced to the top voice
    FlipStab,   // compact mid-register cluster, no bass: sounds like a chopped sample stab
    NumStyles
};

std::string_view voicingStyleName (VoicingStyle style);
std::string_view voicingStyleId (VoicingStyle style);
std::optional<VoicingStyle> voicingStyleFromId (std::string_view id);

struct VoicingParams
{
    VoicingStyle style = VoicingStyle::Spread;

    /** Right-hand register (MIDI notes). */
    int rhLow = 55;
    int rhHigh = 84;

    /** Left-hand / bass register. */
    int lhLow = 36;
    int lhHigh = 55;

    /** Forces a right-hand inversion (0 = root position of the upper structure). */
    std::optional<int> inversion;

    /** Whole-voicing octave shift applied after voice leading. */
    int octaveShift = 0;

    /** Max number of right-hand voices (extensions are dropped first, then the 5th). */
    int maxUpperVoices = 5;
};

/** A voiced chord: MIDI notes, ascending. */
struct Voicing
{
    std::vector<int> notes;

    /** Which rotation of the upper structure was chosen (feed back into VoicingParams::inversion
        to step through inversions from the current one). */
    int inversion = 0;
};

/** Voices a chord, choosing the candidate that moves least from `previous` (if given)
    or sits nearest the middle of the register otherwise. Deterministic. */
Voicing voiceChord (const Chord& chord, const VoicingParams& params, const Voicing* previous = nullptr);

/** Cost of clashing / muddy spacing in an ascending voicing (minor 2nds between neighbours,
    close intervals in the low register). `belowNote` is the left-hand top note (or INT_MIN). */
int voicingClashPenalty (const std::vector<int>& notes, int belowNote);

/** Sum of absolute semitone motion between two voicings (nearest-note matching, symmetric). */
int voiceLeadingDistance (const Voicing& a, const Voicing& b);

} // namespace bounce::theory
