#include "bounce/theory/Chord.h"

#include <algorithm>
#include <array>

namespace bounce::theory
{

namespace
{
struct QualityInfo
{
    std::string_view suffix;
    std::string_view id;
    std::vector<int> intervals;
    ChordFamily family;
};

using Q = ChordQuality;
using F = ChordFamily;

const std::array<QualityInfo, static_cast<size_t> (Q::NumQualities)>& qualityTable()
{
    static const std::array<QualityInfo, static_cast<size_t> (Q::NumQualities)> table { {
        { "",        "maj",       { 0, 4, 7 },             F::Major },
        { "m",       "min",       { 0, 3, 7 },             F::Minor },
        { "dim",     "dim",       { 0, 3, 6 },             F::Diminished },
        { "aug",     "aug",       { 0, 4, 8 },             F::Augmented },
        { "sus2",    "sus2",      { 0, 2, 7 },             F::Suspended },
        { "sus4",    "sus4",      { 0, 5, 7 },             F::Suspended },
        { "maj7",    "maj7",      { 0, 4, 7, 11 },         F::Major },
        { "m7",      "min7",      { 0, 3, 7, 10 },         F::Minor },
        { "7",       "dom7",      { 0, 4, 7, 10 },         F::Major },
        { "m7b5",    "m7b5",      { 0, 3, 6, 10 },         F::Diminished },
        { "dim7",    "dim7",      { 0, 3, 6, 9 },          F::Diminished },
        { "m(maj7)", "minmaj7",   { 0, 3, 7, 11 },         F::Minor },
        { "6",       "maj6",      { 0, 4, 7, 9 },          F::Major },
        { "m6",      "min6",      { 0, 3, 7, 9 },          F::Minor },
        { "add9",    "add9",      { 0, 4, 7, 14 },         F::Major },
        { "madd9",   "madd9",     { 0, 3, 7, 14 },         F::Minor },
        { "6/9",     "6_9",       { 0, 4, 7, 9, 14 },      F::Major },
        { "m6/9",    "m6_9",      { 0, 3, 7, 9, 14 },      F::Minor },
        { "maj9",    "maj9",      { 0, 4, 7, 11, 14 },     F::Major },
        { "m9",      "min9",      { 0, 3, 7, 10, 14 },     F::Minor },
        { "9",       "dom9",      { 0, 4, 7, 10, 14 },     F::Major },
        { "m11",     "min11",     { 0, 3, 7, 10, 14, 17 }, F::Minor },
        { "maj7#11", "maj7s11",   { 0, 4, 7, 11, 18 },     F::Major },
        { "7sus4",   "dom7sus4",  { 0, 5, 7, 10 },         F::Suspended },
        { "maj7sus2","maj7sus2",  { 0, 2, 7, 11 },         F::Suspended },
    } };
    return table;
}

const QualityInfo& info (Q q)
{
    return qualityTable()[static_cast<size_t> (q)];
}

unsigned intervalMask (const std::vector<int>& intervals)
{
    unsigned m = 0;
    for (int iv : intervals)
        m |= 1u << wrapPc (iv);
    return m;
}

constexpr const char* kDegreeNames[] = { "I", "II", "III", "IV", "V", "VI", "VII" };

std::string toLower (std::string s)
{
    for (auto& c : s)
        c = static_cast<char> (c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    return s;
}
} // namespace

std::string_view qualitySuffix (ChordQuality q)     { return info (q).suffix; }
std::string_view qualityId (ChordQuality q)         { return info (q).id; }
const std::vector<int>& qualityIntervals (ChordQuality q) { return info (q).intervals; }
ChordFamily qualityFamily (ChordQuality q)          { return info (q).family; }
int qualityToneCount (ChordQuality q)               { return static_cast<int> (info (q).intervals.size()); }

std::optional<ChordQuality> qualityFromId (std::string_view id)
{
    for (int i = 0; i < static_cast<int> (Q::NumQualities); ++i)
        if (info (static_cast<Q> (i)).id == id)
            return static_cast<Q> (i);
    return std::nullopt;
}

std::optional<ChordQuality> qualityFromIntervalSet (const std::vector<int>& intervals)
{
    const auto mask = intervalMask (intervals);
    for (int i = 0; i < static_cast<int> (Q::NumQualities); ++i)
        if (intervalMask (info (static_cast<Q> (i)).intervals) == mask)
            return static_cast<Q> (i);
    return std::nullopt;
}

//==============================================================================
std::string Chord::name (Spelling spelling) const
{
    auto s = pitchClassName (root, spelling) + std::string (qualitySuffix (quality));
    if (bass && *bass != root)
        s += "/" + pitchClassName (*bass, spelling);
    return s;
}

std::vector<PitchClass> Chord::pitchClasses() const
{
    std::vector<PitchClass> pcs;
    for (int iv : qualityIntervals (quality))
        pcs.push_back (wrapPc (root + iv));
    if (bass && std::find (pcs.begin(), pcs.end(), *bass) == pcs.end())
        pcs.push_back (*bass);
    return pcs;
}

Chord Chord::transposed (int semitones) const
{
    Chord c = *this;
    c.root = wrapPc (root + semitones);
    if (bass)
        c.bass = wrapPc (*bass + semitones);
    return c;
}

bool Chord::isDiatonicTo (const Key& key) const
{
    const auto m = key.mask();
    for (auto pc : pitchClasses())
        if (! ((m >> pc) & 1u))
            return false;
    return true;
}

//==============================================================================
std::optional<Chord> parseChord (std::string_view symbol)
{
    if (symbol.empty())
        return std::nullopt;

    Chord chord;
    if (const auto slash = symbol.rfind ('/'); slash != std::string_view::npos)
    {
        // "6/9" also contains a slash; only treat it as a bass note if it parses as one.
        if (auto bass = parsePitchClass (symbol.substr (slash + 1)))
        {
            chord.bass = *bass;
            symbol = symbol.substr (0, slash);
        }
    }

    size_t rootLen = 1;
    while (rootLen < symbol.size() && (symbol[rootLen] == '#' || symbol[rootLen] == 'b'))
        ++rootLen;

    const auto root = parsePitchClass (symbol.substr (0, rootLen));
    if (! root)
        return std::nullopt;
    chord.root = *root;

    const std::string suffix (symbol.substr (rootLen));

    static const std::array<std::pair<std::string_view, Q>, 22> aliases { {
        { "M", Q::Major },        { "maj", Q::Major },        { "min", Q::Minor },
        { "-", Q::Minor },        { "o", Q::Diminished },     { "+", Q::Augmented },
        { "M7", Q::Major7 },      { "min7", Q::Minor7 },      { "-7", Q::Minor7 },
        { "\xC3\xB8", Q::HalfDiminished7 }, { "\xC3\xB8" "7", Q::HalfDiminished7 },
        { "o7", Q::Diminished7 }, { "mM7", Q::MinorMajor7 },  { "mmaj7", Q::MinorMajor7 },
        { "add2", Q::Add9 },      { "69", Q::SixNine },       { "m69", Q::MinorSixNine },
        { "M9", Q::Major9 },      { "min9", Q::Minor9 },      { "-9", Q::Minor9 },
        { "maj7(#11)", Q::Major7Sharp11 }, { "min11", Q::Minor11 },
    } };

    for (int i = 0; i < static_cast<int> (Q::NumQualities); ++i)
    {
        if (info (static_cast<Q> (i)).suffix == suffix)
        {
            chord.quality = static_cast<Q> (i);
            return chord;
        }
    }

    for (const auto& [alias, q] : aliases)
    {
        if (alias == suffix)
        {
            chord.quality = q;
            return chord;
        }
    }

    return std::nullopt;
}

Chord diatonicChord (const Key& key, int degree, int numTones)
{
    const auto parent = key.heptatonicParent();
    const auto pcs = parent.pitchClasses();
    const int n = static_cast<int> (pcs.size());
    degree = ((degree % n) + n) % n;
    const PitchClass root = pcs[static_cast<size_t> (degree)];

    for (int tones = std::clamp (numTones, 3, 6); tones >= 3; --tones)
    {
        std::vector<int> intervals;
        for (int k = 0; k < tones; ++k)
        {
            const auto pc = pcs[static_cast<size_t> ((degree + 2 * k) % n)];
            intervals.push_back (wrapPc (pc - root) + (k >= 4 ? 12 : 0));
        }

        if (auto q = qualityFromIntervalSet (intervals))
            return { root, *q, std::nullopt };
    }

    return { root, ChordQuality::Major, std::nullopt }; // unreachable for heptatonic scales
}

std::string romanNumeral (const Chord& chord, const Key& key)
{
    // Numerals are measured against the major scale of the tonic in every mode, the way
    // producers talk about loops: in A minor, F is "bVI" and G is "bVII".
    static constexpr int majorOffsets[] = { 0, 2, 4, 5, 7, 9, 11 };
    const int offset = wrapPc (chord.root - key.tonic);

    std::string accidental;
    int degree = -1;
    for (int d = 0; d < 7; ++d)
        if (majorOffsets[d] == offset)
            degree = d;

    if (degree < 0)
    {
        // Chromatic root: prefer "flat of the degree above" (bII, bIII, bV, bVI, bVII).
        for (int d = 0; d < 7; ++d)
            if (majorOffsets[d] == wrapPc (offset + 1))
                { degree = d; accidental = "b"; }
    }

    std::string numeral = kDegreeNames[degree];
    const auto fam = chord.family();
    if (fam == ChordFamily::Minor || fam == ChordFamily::Diminished)
        numeral = toLower (numeral);

    std::string suffix;
    switch (chord.quality)
    {
        case Q::Major: case Q::Minor:  break;
        case Q::Diminished:            suffix = "\xC2\xB0"; break;           // °
        case Q::Diminished7:           suffix = "\xC2\xB0" "7"; break;
        case Q::HalfDiminished7:       suffix = "\xC3\xB8" "7"; break;       // ø7
        case Q::Augmented:             suffix = "+"; break;
        default:
        {
            std::string s (qualitySuffix (chord.quality));
            if (fam == ChordFamily::Minor && ! s.empty() && s[0] == 'm' && s.rfind ("maj", 0) != 0)
                s.erase (0, 1); // "m9" -> "9" because the lowercase numeral already says minor
            suffix = s;
            break;
        }
    }

    auto result = accidental + numeral + suffix;
    if (chord.bass && *chord.bass != chord.root)
        result += "/" + pitchClassName (*chord.bass, key.preferredSpelling());
    return result;
}

} // namespace bounce::theory
