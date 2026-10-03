#include <doctest/doctest.h>

#include "bounce/theory/Voicing.h"

#include <algorithm>
#include <set>

using namespace bounce::theory;

namespace
{
std::set<int> pcsOf (const Voicing& v)
{
    std::set<int> s;
    for (int n : v.notes)
        s.insert (wrapPc (n));
    return s;
}

bool ascending (const Voicing& v)
{
    return std::is_sorted (v.notes.begin(), v.notes.end())
        && std::adjacent_find (v.notes.begin(), v.notes.end()) == v.notes.end();
}
} // namespace

TEST_CASE ("voicings only contain chord tones and respect the register")
{
    for (int s = 0; s < static_cast<int> (VoicingStyle::NumStyles); ++s)
    {
        for (int q = 0; q < static_cast<int> (ChordQuality::NumQualities); ++q)
        {
            for (int root = 0; root < 12; ++root)
            {
                const Chord chord { root, static_cast<ChordQuality> (q), {} };
                VoicingParams p;
                p.style = static_cast<VoicingStyle> (s);
                const auto v = voiceChord (chord, p);

                INFO (chord.name (Spelling::Sharps), " ", voicingStyleName (p.style));
                REQUIRE (v.notes.size() >= 3);
                CHECK (ascending (v));
                CHECK (v.notes.front() >= p.lhLow);
                CHECK (v.notes.back() <= p.rhHigh);

                const auto chordPcs = chord.pitchClasses();
                for (int pc : pcsOf (v))
                    CHECK (std::find (chordPcs.begin(), chordPcs.end(), pc) != chordPcs.end());

                // The 3rd (or sus tone) is what makes the colour; it must never be dropped.
                CHECK (pcsOf (v).count (wrapPc (root + qualityIntervals (chord.quality)[1])) == 1);
            }
        }
    }
}

TEST_CASE ("spread voicing puts root and 5th in the left hand")
{
    VoicingParams p;
    p.style = VoicingStyle::Spread;
    const auto v = voiceChord ({ 9, ChordQuality::Minor9, {} }, p); // Am9
    REQUIRE (v.notes.size() >= 4);
    CHECK (wrapPc (v.notes[0]) == 9);
    CHECK (wrapPc (v.notes[1]) == 4);
    CHECK (v.notes[0] <= p.lhHigh);
}

TEST_CASE ("third on top")
{
    VoicingParams p;
    p.style = VoicingStyle::ThirdOnTop;
    for (int root = 0; root < 12; ++root)
    {
        const auto v = voiceChord ({ root, ChordQuality::Major7, {} }, p);
        CHECK (wrapPc (v.notes.back() - root) == 4);
    }
}

TEST_CASE ("slash chords put the bass note at the bottom")
{
    for (auto style : { VoicingStyle::Close, VoicingStyle::Spread, VoicingStyle::Open })
    {
        VoicingParams p;
        p.style = style;
        const auto v = voiceChord ({ 0, ChordQuality::Major, 4 }, p);
        CHECK (wrapPc (v.notes.front()) == 4);
    }
}

TEST_CASE ("voice leading prefers small movement")
{
    VoicingParams p;
    p.style = VoicingStyle::Close;
    const auto c = voiceChord ({ 0, ChordQuality::Major7, {} }, p);
    const auto withVL = voiceChord ({ 5, ChordQuality::Major7, {} }, p, &c);

    // Every other inversion of Fmaj7 in range moves at least as much.
    for (int inv = 0; inv < 4; ++inv)
    {
        auto forced = p;
        forced.inversion = inv;
        const auto alt = voiceChord ({ 5, ChordQuality::Major7, {} }, forced, &c);
        CHECK (voiceLeadingDistance (c, withVL) <= voiceLeadingDistance (c, alt));
    }

    // Common tones (C, E) are held.
    CHECK (std::count_if (withVL.notes.begin(), withVL.notes.end(), [&] (int n)
    {
        return std::find (c.notes.begin(), c.notes.end(), n) != c.notes.end();
    }) >= 2);
}

TEST_CASE ("forced inversion and octave shift")
{
    VoicingParams p;
    p.style = VoicingStyle::Close;
    p.inversion = 1;
    const auto v = voiceChord ({ 0, ChordQuality::Major, {} }, p);
    CHECK (wrapPc (v.notes.front()) == 4);

    p.inversion = 2;
    CHECK (wrapPc (voiceChord ({ 0, ChordQuality::Major, {} }, p).notes.front()) == 7);

    VoicingParams q;
    const auto base = voiceChord ({ 0, ChordQuality::Major7, {} }, q);
    q.octaveShift = 1;
    const auto up = voiceChord ({ 0, ChordQuality::Major7, {} }, q);
    REQUIRE (base.notes.size() == up.notes.size());
    for (size_t i = 0; i < base.notes.size(); ++i)
        CHECK (up.notes[i] == base.notes[i] + 12);
}

TEST_CASE ("reported inversion reproduces the voicing")
{
    for (auto style : { VoicingStyle::Close, VoicingStyle::Spread, VoicingStyle::Open, VoicingStyle::FlipStab })
    {
        VoicingParams p;
        p.style = style;
        const Voicing prev = voiceChord ({ 7, ChordQuality::Dominant9, {} }, p);
        const auto v = voiceChord ({ 0, ChordQuality::Major9, {} }, p, &prev);
        auto forced = p;
        forced.inversion = v.inversion;
        CHECK (voiceChord ({ 0, ChordQuality::Major9, {} }, forced, &prev).notes == v.notes);
    }
}

TEST_CASE ("voicing is deterministic")
{
    VoicingParams p;
    const auto a = voiceChord ({ 2, ChordQuality::Minor11, {} }, p);
    const auto b = voiceChord ({ 2, ChordQuality::Minor11, {} }, p);
    CHECK (a.notes == b.notes);
}
