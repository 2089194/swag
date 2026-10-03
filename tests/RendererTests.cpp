#include <doctest/doctest.h>

#include "bounce/gen/ChordGenerator.h"
#include "bounce/gen/ChordRenderer.h"

#include <algorithm>
#include <cmath>

using namespace bounce;
using namespace bounce::gen;

namespace
{
Progression testProgression (int bars = 4, int chords = 4)
{
    ChordGenerator gen (StylePreset::defaults());
    ChordGeneratorParams p;
    p.key = { 9, theory::ScaleType::NaturalMinor };
    p.bars = bars;
    p.chordCount = chords;
    p.seed = 77;
    return gen.generate (p);
}
} // namespace

TEST_CASE ("rendered chords stay inside the loop and start on the bar")
{
    const auto prog = testProgression();
    for (int r = 0; r < static_cast<int> (ChordRhythm::NumRhythms); ++r)
    {
        ChordPerformance perf;
        perf.rhythm = static_cast<ChordRhythm> (r);
        perf.humanise = 1.0;
        perf.swing = 0.5;
        const auto clip = renderChords (prog, perf);

        INFO (chordRhythmName (perf.rhythm));
        REQUIRE_FALSE (clip.notes.empty());
        CHECK (clip.lengthBeats == 16.0);
        CHECK (clip.notes.front().start == 0.0);
        for (const auto& n : clip.notes)
        {
            CHECK (n.start >= 0.0);
            CHECK (n.start + n.length <= 16.0 + 1e-9);
            CHECK (n.length > 0.0);
            CHECK (n.velocity >= 1);
            CHECK (n.velocity <= 127);
        }
    }
}

TEST_CASE ("rendered notes are chord tones of the sounding slot")
{
    const auto prog = testProgression();
    ChordPerformance perf;
    perf.humanise = 0.0;
    perf.rhythm = ChordRhythm::Stabs;
    for (const auto& n : renderChords (prog, perf).notes)
    {
        const auto& chord = prog.slots[static_cast<size_t> (prog.slotAt (n.start + 1e-6))].chord;
        const auto pcs = chord.pitchClasses();
        CHECK (std::find (pcs.begin(), pcs.end(), theory::wrapPc (n.pitch)) != pcs.end());
    }
}

TEST_CASE ("sustain with no humanise gives block chords exactly on the slot grid")
{
    const auto prog = testProgression (4, 4);
    ChordPerformance perf;
    perf.humanise = 0.0;
    const auto clip = renderChords (prog, perf);
    for (const auto& n : clip.notes)
    {
        CHECK (n.start == doctest::Approx (prog.slotStart (prog.slotAt (n.start + 1e-6))));
        CHECK (std::fmod (n.start, 4.0) == doctest::Approx (0.0));
    }
}

TEST_CASE ("stabs follow the step pattern")
{
    const auto prog = testProgression (2, 2);
    ChordPerformance perf;
    perf.humanise = 0.0;
    perf.rhythm = ChordRhythm::Stabs;
    perf.stabSteps = { 0, 6, 12 };
    const auto clip = renderChords (prog, perf);

    std::vector<double> onsets;
    for (const auto& n : clip.notes)
        if (onsets.empty() || n.start > onsets.back() + 1e-9)
            onsets.push_back (n.start);
    CHECK (onsets == std::vector<double> { 0.0, 1.5, 3.0, 4.0, 5.5, 7.0 });
}

TEST_CASE ("renderer is deterministic")
{
    const auto prog = testProgression();
    ChordPerformance perf;
    perf.humanise = 1.0;
    perf.seed = 5;
    const auto a = renderChords (prog, perf);
    const auto b = renderChords (prog, perf);
    CHECK (a.notes == b.notes);

    perf.seed = 6;
    CHECK (renderChords (prog, perf).notes != a.notes);
}

TEST_CASE ("voice-led progression moves smoothly")
{
    const auto prog = testProgression (8, 8);
    ChordPerformance perf;
    perf.voicing = theory::VoicingStyle::Close;
    const auto v = voiceProgression (prog, perf);
    REQUIRE (v.size() == 8);
    for (size_t i = 1; i < v.size(); ++i)
    {
        // Upper voices of close voicings never jump more than a 5th on average.
        const auto d = theory::voiceLeadingDistance (v[i - 1], v[i]);
        CHECK (d <= 7 * static_cast<int> (v[i].notes.size() + v[i - 1].notes.size()) / 2);
    }
}
