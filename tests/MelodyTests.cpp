#include <doctest/doctest.h>

#include "bounce/gen/ChordGenerator.h"
#include "bounce/gen/MelodyGenerator.h"

#include <algorithm>
#include <cmath>
#include <set>

using namespace bounce;
using namespace bounce::gen;

namespace
{
Progression prog (uint64_t seed, theory::Key key = { 9, theory::ScaleType::NaturalMinor })
{
    ChordGenerator gen (StylePreset::defaults());
    ChordGeneratorParams p;
    p.key = key;
    p.seed = seed;
    p.borrowed = 0.0;
    return gen.generate (p);
}

bool isChordTone (const theory::Chord& c, int pitch)
{
    const auto pcs = c.pitchClasses();
    return std::find (pcs.begin(), pcs.end(), theory::wrapPc (pitch)) != pcs.end();
}
} // namespace

TEST_CASE ("melody stays in key and range; strong beats are chord tones")
{
    for (int scale : { 0, 1, 2, 5 })
        for (uint64_t seed = 0; seed < 30; ++seed)
        {
            const theory::Key key { static_cast<int> (seed % 12), static_cast<theory::ScaleType> (scale) };
            const auto pr = prog (seed, key);
            MelodyParams p;
            p.seed = seed;
            p.feel = static_cast<MelodyFeel> (seed % 3);
            p.density = static_cast<double> (seed % 5) / 4.0;
            const auto clip = generateMelody (pr, p);

            REQUIRE_FALSE (clip.notes.empty());
            for (const auto& n : clip.notes)
            {
                CHECK (key.heptatonicParent().contains (n.pitch));
                CHECK (n.pitch >= p.rangeLow);
                CHECK (n.pitch <= p.rangeHigh);
                CHECK (n.start >= 0.0);
                CHECK (n.start + n.length <= clip.lengthBeats + 1e-9);
                if (p.feel != MelodyFeel::Swung && std::abs (n.start - std::round (n.start)) < 1e-9)
                    CHECK (isChordTone (pr.slots[static_cast<size_t> (pr.slotAt (n.start + 1e-6))].chord, n.pitch));
            }
            // Monophonic: no overlaps.
            for (size_t i = 1; i < clip.notes.size(); ++i)
                CHECK (clip.notes[i - 1].start + clip.notes[i - 1].length <= clip.notes[i].start + 1e-9);
        }
}

TEST_CASE ("density changes the number of notes")
{
    const auto pr = prog (5);
    MelodyParams sparse, busy;
    sparse.density = 0.0;
    busy.density = 1.0;
    size_t s = 0, b = 0;
    for (uint64_t seed = 0; seed < 20; ++seed)
    {
        sparse.seed = busy.seed = seed;
        s += generateMelody (pr, sparse).notes.size();
        b += generateMelody (pr, busy).notes.size();
    }
    CHECK (b > s * 2);
}

TEST_CASE ("repetition and catchiness reuse the motif rhythm")
{
    const auto pr = prog (6);
    MelodyParams p;
    p.repetition = 1.0;
    p.catchiness = 1.0;
    p.callResponse = false;
    p.seed = 77;
    const auto clip = generateMelody (pr, p);

    std::vector<std::set<long>> rhythms (4);
    for (const auto& n : clip.notes)
        rhythms[static_cast<size_t> (n.start / 4.0)].insert (std::lround (std::fmod (n.start, 4.0) * 12.0));
    for (int bar = 1; bar < 3; ++bar)
        CHECK (rhythms[static_cast<size_t> (bar)] == rhythms[0]);
    // The phrase's last bar (A') keeps the rhythm but may leave out the final hit to breathe.
    CHECK (std::includes (rhythms[0].begin(), rhythms[0].end(), rhythms[3].begin(), rhythms[3].end()));
    CHECK (rhythms[3].size() + 1 >= rhythms[0].size());
}

TEST_CASE ("melody phrases resolve: the loop ends on the tonic or the last chord's root")
{
    for (uint64_t seed = 0; seed < 40; ++seed)
    {
        const auto pr = prog (seed);
        MelodyParams p;
        p.seed = seed;
        const auto clip = generateMelody (pr, p);
        REQUIRE_FALSE (clip.notes.empty());
        const auto& lastChord = pr.slots.back().chord;
        const int pc = theory::wrapPc (clip.notes.back().pitch);
        CHECK ((pc == pr.key.tonic || pc == lastChord.root));
    }
}

TEST_CASE ("melody stays in a singable range and moves mostly by step")
{
    int steps = 0, leaps = 0;
    for (uint64_t seed = 0; seed < 40; ++seed)
    {
        const auto pr = prog (seed);
        MelodyParams p;
        p.seed = seed;
        const auto clip = generateMelody (pr, p);
        int lo = 127, hi = 0;
        for (size_t i = 0; i < clip.notes.size(); ++i)
        {
            lo = std::min (lo, clip.notes[i].pitch);
            hi = std::max (hi, clip.notes[i].pitch);
            if (i > 0)
                (std::abs (clip.notes[i].pitch - clip.notes[i - 1].pitch) <= 5 ? steps : leaps)++;
        }
        CHECK (hi - lo <= 19); // an octave and a 5th at most
    }
    CHECK (steps > leaps * 3);
}

TEST_CASE ("locked bars are kept, others regenerate")
{
    const auto pr = prog (7);
    MelodyParams p;
    p.seed = 1;
    const auto first = generateMelody (pr, p);

    p.lockedBars = 0b0101;
    p.lockedNotes = first.notes;
    p.seed = 2;
    const auto second = generateMelody (pr, p);

    auto barNotes = [] (const midi::MidiClip& c, int bar)
    {
        std::vector<midi::Note> out;
        for (const auto& n : c.notes)
            if (static_cast<int> (n.start / 4.0) == bar)
                out.push_back (n);
        return out;
    };
    CHECK (barNotes (second, 0) == barNotes (first, 0));
    CHECK (barNotes (second, 2) == barNotes (first, 2));
    CHECK ((barNotes (second, 1) != barNotes (first, 1) || barNotes (second, 3) != barNotes (first, 3)));
}

TEST_CASE ("counter-melody fills gaps and avoids clashes")
{
    for (uint64_t seed = 0; seed < 30; ++seed)
    {
        const auto pr = prog (seed);
        MelodyParams mp;
        mp.seed = seed;
        const auto main = generateMelody (pr, mp);
        CounterParams cp;
        cp.seed = seed;
        cp.density = 0.8;
        const auto counter = generateCounterMelody (pr, main, cp);

        for (const auto& c : counter.notes)
        {
            CHECK (pr.key.heptatonicParent().contains (c.pitch));
            for (const auto& m : main.notes)
            {
                CHECK (std::abs (m.start - c.start) > 0.2); // never on top of a melody onset
                const bool overlap = m.start < c.start + c.length - 1e-9 && c.start < m.start + m.length - 1e-9;
                if (overlap)
                {
                    const int iv = theory::wrapPc (std::abs (c.pitch - m.pitch));
                    CHECK (iv != 1);
                    CHECK (iv != 11);
                    CHECK (iv != 6);
                }
            }
        }
    }
}
