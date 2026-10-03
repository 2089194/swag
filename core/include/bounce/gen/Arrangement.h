#pragma once

#include "bounce/gen/Idea.h"
#include "bounce/midi/MidiClip.h"
#include "bounce/util/Json.h"

#include <array>
#include <string>
#include <vector>

namespace bounce::gen
{

/** One section of a sketched arrangement. */
struct Section
{
    std::string name = "Hook";
    int bars = 8;
    std::array<bool, numParts> parts { true, true, true, true, true }; // which parts play
    bool filterSweep = false; // CC74 ramp up on the melodic parts across the section
    bool dropLastBar = false; // mute drums + 808 in the last bar (the classic pre-hook drop)

    bool operator== (const Section&) const = default;
};

struct Arrangement
{
    std::vector<Section> sections;

    bool operator== (const Arrangement&) const = default;

    int totalBars() const;

    /** Intro 8 (chords, filtered) / Hook 16 / Verse 16 / Hook 16 / Bridge 8 / Outro 8. */
    static Arrangement defaultTemplate();

    util::Json toJson() const;
    static Arrangement fromJson (const util::Json& j);
};

/** Tiles each part's loop across the sections. Returns one clip per part (same order as
    `parts`, indexed by Part), each the full song length, plus section markers on the first
    clip. Muted parts get empty sections; filter sweeps add CC74 ramps. */
std::vector<midi::MidiClip> renderArrangement (const Arrangement& arrangement,
                                               const std::array<midi::MidiClip, numParts>& loops,
                                               int beatsPerBar = 4);

} // namespace bounce::gen
