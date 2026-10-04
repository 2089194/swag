#include <doctest/doctest.h>

#include "bounce/gen/BassGenerator.h"
#include "bounce/gen/ChordGenerator.h"
#include "bounce/gen/DrumGenerator.h"

#include <algorithm>
#include <cmath>
#include <set>

using namespace bounce;
using namespace bounce::gen;

namespace
{
Progression prog (uint64_t seed)
{
    ChordGenerator gen (StylePreset::defaults());
    ChordGeneratorParams p;
    p.seed = seed;
    return gen.generate (p);
}

std::vector<double> kicks (uint64_t seed)
{
    DrumParams d;
    d.seed = seed;
    return generateDrums (d, DrumStyle::defaults()).kickTimes();
}
} // namespace

TEST_CASE ("808 follows chord roots and stays in register")
{
    for (int mode = 0; mode < static_cast<int> (BassMode::NumModes); ++mode)
    {
        for (uint64_t seed = 0; seed < 20; ++seed)
        {
            const auto pr = prog (seed);
            BassParams p;
            p.mode = static_cast<BassMode> (mode);
            p.seed = seed;
            const auto clip = generateBass (pr, kicks (seed), p);

            INFO (bassModeName (p.mode));
            REQUIRE_FALSE (clip.notes.empty());
            CHECK (clip.notes.front().start == 0.0);
            for (const auto& n : clip.notes)
            {
                CHECK (n.pitch >= p.lowNote);
                CHECK (n.pitch < p.lowNote + 12 * p.octaveRange + 12);
                CHECK (n.start + n.length <= clip.lengthBeats + 1e-9);

                // Always the bass note of the chord sounding at that point (or its octave):
                // no stray 5ths or passing notes muddying the low end.
                const auto& chord = pr.slots[static_cast<size_t> (pr.slotAt (n.start + 1e-6))].chord;
                CHECK (theory::wrapPc (n.pitch - chord.bass.value_or (chord.root)) == 0);
            }

            // Every chord change gets a new root on its downbeat.
            for (int i = 0; i < static_cast<int> (pr.slots.size()); ++i)
            {
                const double t = pr.slotStart (i);
                CHECK (std::any_of (clip.notes.begin(), clip.notes.end(), [t] (const midi::Note& n) { return std::abs (n.start - t) < 1e-9; }));
            }
        }
    }
}

TEST_CASE ("808 sustain mode: one note per chord, no gaps")
{
    const auto pr = prog (4);
    BassParams p;
    p.mode = BassMode::Sustain;
    p.glide = 0.0;
    const auto clip = generateBass (pr, {}, p);
    REQUIRE (clip.notes.size() == pr.slots.size());
    for (size_t i = 0; i < clip.notes.size(); ++i)
        CHECK (clip.notes[i].length == doctest::Approx (pr.slotLength (static_cast<int> (i))));
}

TEST_CASE ("808 lock to kick")
{
    const auto pr = prog (9);
    const auto k = kicks (9);
    BassParams p;
    p.lockToKick = true;
    p.density = 1.0;
    const auto clip = generateBass (pr, k, p);
    for (const auto& n : clip.notes)
    {
        const bool onKick = std::any_of (k.begin(), k.end(), [&] (double t) { return std::abs (t - n.start) < 1e-9; });
        const bool onChange = [&] { for (int i = 0; i < static_cast<int> (pr.slots.size()); ++i) if (std::abs (pr.slotStart (i) - n.start) < 1e-9) return true; return false; }();
        const bool extra16th = std::abs (std::fmod (n.start - pr.slotStart (pr.slotAt (n.start + 1e-6)), 1.0) - 0.75) < 1e-9;
        CHECK ((onKick || onChange || extra16th));
    }
}

TEST_CASE ("808 glides are overlaps, with portamento CCs")
{
    const auto pr = prog (2);
    BassParams p;
    p.mode = BassMode::GlideHeavy;
    p.glide = 1.0;
    p.density = 1.0;
    int total = 0;
    for (uint64_t seed = 0; seed < 10; ++seed)
    {
        p.seed = seed;
        const auto clip = generateBass (pr, kicks (seed), p);
        total += countGlides (clip);
        CHECK (std::any_of (clip.controls.begin(), clip.controls.end(), [] (const midi::ControlChange& c) { return c.controller == 65 && c.value == 127; }));
    }
    CHECK (total > 5);

    p.glide = 0.0;
    CHECK (countGlides (generateBass (pr, kicks (1), p)) == 0);
}

TEST_CASE ("808 is deterministic")
{
    const auto pr = prog (3);
    BassParams p;
    p.seed = 42;
    CHECK (generateBass (pr, kicks (1), p).notes == generateBass (pr, kicks (1), p).notes);
}

TEST_CASE ("808 roots move to the nearest octave between chords")
{
    for (uint64_t seed = 0; seed < 40; ++seed)
    {
        const auto pr = prog (seed);
        BassParams p;
        p.mode = BassMode::Sustain;
        p.glide = 0.0;
        const auto clip = generateBass (pr, {}, p);
        REQUIRE (clip.notes.size() == pr.slots.size());
        for (size_t i = 1; i < clip.notes.size(); ++i)
            CHECK (std::abs (clip.notes[i].pitch - clip.notes[i - 1].pitch) <= 9); // a 6th at most, e.g. D2 down to F1
    }
}

TEST_CASE ("808 repeats one bar rhythm across the loop")
{
    ChordGenerator gen (StylePreset::defaults());
    ChordGeneratorParams cp;
    cp.bars = 4;
    cp.chordCount = 4; // one chord per bar
    for (auto mode : { BassMode::RootFollow, BassMode::SyncopatedBounce, BassMode::OctaveJumper, BassMode::GlideHeavy })
        for (uint64_t seed = 0; seed < 20; ++seed)
        {
            cp.seed = seed;
            const auto pr = gen.generate (cp);
            BassParams p;
            p.mode = mode;
            p.lockToKick = false;
            p.seed = seed;
            const auto clip = generateBass (pr, {}, p);

            std::vector<std::vector<int>> bars (4); // 16th steps hit in each bar
            for (const auto& n : clip.notes)
            {
                const int bar = static_cast<int> (n.start / 4.0);
                bars[static_cast<size_t> (bar)].push_back (static_cast<int> (std::lround ((n.start - bar * 4.0) * 4.0)));
            }
            INFO (bassModeName (mode), " seed ", seed);
            for (size_t b = 1; b < bars.size(); ++b)
                CHECK (bars[b] == bars[0]);
        }
}
