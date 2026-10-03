#include "bounce/theory/Scale.h"

#include <algorithm>

namespace bounce::theory
{

namespace
{
struct ScaleInfo
{
    std::string_view name;
    std::string_view id;
    std::vector<int> intervals;
    bool minor;
    int ionianOffset; // semitones from the relative Ionian tonic up to this mode's tonic
};

const std::array<ScaleInfo, static_cast<size_t> (ScaleType::NumTypes)>& scaleTable()
{
    static const std::array<ScaleInfo, static_cast<size_t> (ScaleType::NumTypes)> table { {
        { "Major",            "major",            { 0, 2, 4, 5, 7, 9, 11 }, false, 0 },
        { "Natural Minor",    "natural_minor",    { 0, 2, 3, 5, 7, 8, 10 }, true,  9 },
        { "Dorian",           "dorian",           { 0, 2, 3, 5, 7, 9, 10 }, true,  2 },
        { "Phrygian",         "phrygian",         { 0, 1, 3, 5, 7, 8, 10 }, true,  4 },
        { "Lydian",           "lydian",           { 0, 2, 4, 6, 7, 9, 11 }, false, 5 },
        { "Mixolydian",       "mixolydian",       { 0, 2, 4, 5, 7, 9, 10 }, false, 7 },
        { "Harmonic Minor",   "harmonic_minor",   { 0, 2, 3, 5, 7, 8, 11 }, true,  9 },
        { "Melodic Minor",    "melodic_minor",    { 0, 2, 3, 5, 7, 9, 11 }, true,  9 },
        { "Major Pentatonic", "major_pentatonic", { 0, 2, 4, 7, 9 },        false, 0 },
        { "Minor Pentatonic", "minor_pentatonic", { 0, 3, 5, 7, 10 },       true,  9 },
    } };
    return table;
}

const ScaleInfo& info (ScaleType t)
{
    return scaleTable()[static_cast<size_t> (t)];
}
} // namespace

std::string_view scaleTypeName (ScaleType type) { return info (type).name; }
std::string_view scaleTypeId (ScaleType type)   { return info (type).id; }
const std::vector<int>& scaleIntervals (ScaleType type) { return info (type).intervals; }
bool isMinorMode (ScaleType type) { return info (type).minor; }

std::optional<ScaleType> scaleTypeFromId (std::string_view id)
{
    for (int i = 0; i < static_cast<int> (ScaleType::NumTypes); ++i)
    {
        const auto t = static_cast<ScaleType> (i);
        if (info (t).id == id)
            return t;
    }

    // A few friendly aliases for hand-written presets.
    if (id == "minor" || id == "aeolian") return ScaleType::NaturalMinor;
    if (id == "ionian")                   return ScaleType::Major;
    return std::nullopt;
}

std::vector<PitchClass> Key::pitchClasses() const
{
    std::vector<PitchClass> pcs;
    for (int iv : scaleIntervals (scale))
        pcs.push_back (wrapPc (tonic + iv));
    return pcs;
}

unsigned Key::mask() const
{
    unsigned m = 0;
    for (auto pc : pitchClasses())
        m |= 1u << pc;
    return m;
}

bool Key::contains (PitchClass pc) const
{
    return (mask() >> wrapPc (pc)) & 1u;
}

Key Key::heptatonicParent() const
{
    switch (scale)
    {
        case ScaleType::MajorPentatonic: return { tonic, ScaleType::Major };
        case ScaleType::MinorPentatonic: return { tonic, ScaleType::NaturalMinor };
        default:                         return *this;
    }
}

Spelling Key::preferredSpelling() const
{
    const int ionianTonic = wrapPc (tonic - info (scale).ionianOffset);
    // F, Bb, Eb, Ab, Db major (and their modes) read best with flats.
    switch (ionianTonic)
    {
        case 5: case 10: case 3: case 8: case 1: return Spelling::Flats;
        default:                                 return Spelling::Sharps;
    }
}

std::string Key::name() const
{
    const auto root = pitchClassName (tonic, preferredSpelling());
    switch (scale)
    {
        case ScaleType::Major:        return root;
        case ScaleType::NaturalMinor: return root + "m";
        default:                      return root + " " + std::string (scaleTypeName (scale));
    }
}

Key Key::relative() const
{
    switch (scale)
    {
        case ScaleType::Major:           return { wrapPc (tonic + 9), ScaleType::NaturalMinor };
        case ScaleType::NaturalMinor:    return { wrapPc (tonic + 3), ScaleType::Major };
        case ScaleType::MajorPentatonic: return { wrapPc (tonic + 9), ScaleType::MinorPentatonic };
        case ScaleType::MinorPentatonic: return { wrapPc (tonic + 3), ScaleType::MajorPentatonic };
        default:                         return *this;
    }
}

Key Key::transposed (int semitones) const
{
    return { wrapPc (tonic + semitones), scale };
}

std::optional<int> Key::degreeOf (PitchClass pc) const
{
    const auto pcs = pitchClasses();
    const auto it = std::find (pcs.begin(), pcs.end(), wrapPc (pc));
    if (it == pcs.end())
        return std::nullopt;
    return static_cast<int> (it - pcs.begin());
}

} // namespace bounce::theory
