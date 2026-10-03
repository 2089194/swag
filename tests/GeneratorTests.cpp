#include <doctest/doctest.h>

#include "bounce/gen/ChordGenerator.h"
#include "bounce/gen/IdeaHistory.h"

#include <set>

using namespace bounce;
using namespace bounce::gen;
using theory::Key;
using theory::ScaleType;

namespace
{
ChordGeneratorParams paramsFor (Key key, uint64_t seed)
{
    ChordGeneratorParams p;
    p.key = key;
    p.seed = seed;
    return p;
}
} // namespace

TEST_CASE ("harmony tokens")
{
    auto fn = parseHarmonyToken ("bVI");
    REQUIRE (fn);
    CHECK (fn->offset == 8);
    CHECK (fn->family == theory::ChordFamily::Major);

    fn = parseHarmonyToken ("iv");
    REQUIRE (fn);
    CHECK (fn->offset == 5);
    CHECK (fn->family == theory::ChordFamily::Minor);

    fn = parseHarmonyToken ("ii\xC2\xB0");
    REQUIRE (fn);
    CHECK (fn->symbol == "iio");
    CHECK (fn->family == theory::ChordFamily::Diminished);

    CHECK (parseHarmonyToken ("viidim")->symbol == "viio");
    CHECK_FALSE (parseHarmonyToken ("Iv").has_value());
    CHECK_FALSE (parseHarmonyToken ("VIII").has_value());
    CHECK_FALSE (parseHarmonyToken ("x").has_value());

    for (int off = 0; off < 12; ++off)
        for (auto fam : { theory::ChordFamily::Major, theory::ChordFamily::Minor, theory::ChordFamily::Diminished })
        {
            const auto f = makeHarmonyFunction (off, fam);
            const auto parsed = parseHarmonyToken (f.symbol);
            REQUIRE (parsed);
            CHECK (parsed->offset == off);
            CHECK (parsed->family == fam);
        }
}

TEST_CASE ("generator is deterministic for a seed")
{
    ChordGenerator gen (StylePreset::defaults());
    const auto params = paramsFor ({ 9, ScaleType::NaturalMinor }, 1234);
    const auto a = gen.generate (params);
    const auto b = gen.generate (params);
    CHECK (a == b);
    REQUIRE (a.slots.size() == 4);

    // Different seeds explore different ideas.
    std::set<std::string> distinct;
    for (uint64_t s = 0; s < 40; ++s)
        distinct.insert (gen.generate (paramsFor ({ 9, ScaleType::NaturalMinor }, s)).chordNames());
    CHECK (distinct.size() > 20);
}

TEST_CASE ("with borrowed = 0 every chord stays in key")
{
    ChordGenerator gen (StylePreset::defaults());
    for (int scale = 0; scale < static_cast<int> (ScaleType::NumTypes); ++scale)
    {
        for (int tonic = 0; tonic < 12; tonic += 5)
        {
            for (uint64_t seed = 0; seed < 25; ++seed)
            {
                auto params = paramsFor ({ tonic, static_cast<ScaleType> (scale) }, seed);
                params.borrowed = 0.0;
                params.complexity = static_cast<double> (seed % 5) / 4.0;
                const auto prog = gen.generate (params);
                for (const auto& slot : prog.slots)
                {
                    INFO (prog.key.name(), ": ", prog.chordNames());
                    CHECK (slot.chord.isDiatonicTo (params.key.heptatonicParent()));
                    CHECK_FALSE (slot.function.borrowed);
                }
            }
        }
    }
}

TEST_CASE ("borrowed chords come from the parallel mode only")
{
    ChordGenerator gen (StylePreset::defaults());
    const Key c { 0, ScaleType::Major };
    const Key cMinor { 0, ScaleType::NaturalMinor };
    int borrowedCount = 0;

    for (uint64_t seed = 0; seed < 200; ++seed)
    {
        auto params = paramsFor (c, seed);
        params.borrowed = 1.0;
        params.mood = -0.6;
        for (const auto& slot : gen.generate (params).slots)
        {
            if (slot.chord.isDiatonicTo (c))
                continue;
            ++borrowedCount;
            CHECK (slot.function.borrowed);
            // Every tone is in C major or C minor.
            for (int pc : slot.chord.pitchClasses())
                CHECK ((c.contains (pc) || cMinor.contains (pc)));
        }
    }
    CHECK (borrowedCount > 20);
}

TEST_CASE ("complexity controls extensions")
{
    ChordGenerator gen (StylePreset::defaults());
    auto countTones = [&] (double complexity)
    {
        int total = 0, chords = 0;
        for (uint64_t seed = 0; seed < 60; ++seed)
        {
            auto params = paramsFor ({ 5, ScaleType::Major }, seed);
            params.complexity = complexity;
            for (const auto& s : gen.generate (params).slots)
            {
                total += theory::qualityToneCount (s.chord.quality);
                ++chords;
            }
        }
        return static_cast<double> (total) / chords;
    };

    const double simple = countTones (0.0);
    const double lush = countTones (1.0);
    CHECK (simple < 3.4);
    CHECK (lush > 4.3);
}

TEST_CASE ("mood pushes toward minor or major chords")
{
    ChordGenerator gen (StylePreset::defaults());
    auto minorShare = [&] (double mood)
    {
        int minor = 0, total = 0;
        for (uint64_t seed = 0; seed < 100; ++seed)
        {
            auto params = paramsFor ({ 0, ScaleType::Major }, seed);
            params.mood = mood;
            for (const auto& s : gen.generate (params).slots)
            {
                minor += s.chord.family() == theory::ChordFamily::Minor ? 1 : 0;
                ++total;
            }
        }
        return static_cast<double> (minor) / total;
    };
    CHECK (minorShare (-1.0) > minorShare (1.0) + 0.15);
}

TEST_CASE ("no chord repeats back to back, including the loop seam")
{
    ChordGenerator gen (StylePreset::defaults());
    for (uint64_t seed = 0; seed < 200; ++seed)
    {
        auto params = paramsFor ({ 2, ScaleType::NaturalMinor }, seed);
        params.chordCount = 2 + static_cast<int> (seed % 4);
        const auto prog = gen.generate (params);
        const auto n = prog.slots.size();
        for (size_t i = 0; i < n; ++i)
        {
            INFO (prog.chordNames());
            CHECK (prog.slots[i].function.symbol != prog.slots[(i + 1) % n].function.symbol);
        }
    }
}

TEST_CASE ("locks are respected")
{
    ChordGenerator gen (StylePreset::defaults());
    auto params = paramsFor ({ 9, ScaleType::NaturalMinor }, 7);
    auto prog = gen.generate (params);

    prog.slots[1].locked = true;
    prog.slots[1].inversion = 2;
    prog.slots[3].locked = true;

    for (uint64_t seed = 100; seed < 150; ++seed)
    {
        params.seed = seed;
        const auto next = gen.generate (params, &prog);
        REQUIRE (next.slots.size() == 4);
        CHECK (next.slots[1] == prog.slots[1]);
        CHECK (next.slots[3] == prog.slots[3]);
        CHECK_FALSE (next.slots[0].locked);
        CHECK (next.seed == seed);
    }

    // Locking doesn't disturb the other slots' random streams: a slot whose context is
    // unchanged by the lock gets the same chord as in a fully unlocked run.
    params.seed = 99;
    const auto base = gen.generate (params);
    auto locked = base;
    locked.slots[2].locked = true;
    const auto again = gen.generate (params, &locked);
    CHECK (again.slots[0].chord == base.slots[0].chord);
    CHECK (again.slots[2] == locked.slots[2]);
}

TEST_CASE ("locked chords move with a tonic change")
{
    ChordGenerator gen (StylePreset::defaults());
    auto params = paramsFor ({ 9, ScaleType::NaturalMinor }, 3);
    auto prog = gen.generate (params);
    prog.slots[0].locked = true;

    params.key.tonic = 4; // Am -> Em
    const auto moved = gen.generate (params, &prog);
    CHECK (moved.slots[0].chord == prog.slots[0].chord.transposed (7));
    CHECK (moved.key.tonic == 4);
}

TEST_CASE ("reharmonise changes only the target slot")
{
    ChordGenerator gen (StylePreset::defaults());
    const auto params = paramsFor ({ 0, ScaleType::Major }, 42);
    const auto prog = gen.generate (params);

    int changed = 0;
    for (uint64_t v = 0; v < 20; ++v)
    {
        const auto r = gen.reharmonise (prog, 2, params, v);
        for (size_t i = 0; i < prog.slots.size(); ++i)
            if (i != 2)
                CHECK (r.slots[i] == prog.slots[i]);
        changed += r.slots[2].chord != prog.slots[2].chord ? 1 : 0;
        CHECK (r == gen.reharmonise (prog, 2, params, v)); // deterministic
    }
    CHECK (changed >= 18);
}

TEST_CASE ("transpose")
{
    ChordGenerator gen (StylePreset::defaults());
    const auto prog = gen.generate (paramsFor ({ 9, ScaleType::NaturalMinor }, 5));
    const auto up = prog.transposed (3);
    CHECK (up.key.tonic == 0);
    for (size_t i = 0; i < prog.slots.size(); ++i)
    {
        CHECK (up.slots[i].chord.root == theory::wrapPc (prog.slots[i].chord.root + 3));
        CHECK (up.slots[i].chord.quality == prog.slots[i].chord.quality);
        CHECK (theory::romanNumeral (up.slots[i].chord, up.key) == theory::romanNumeral (prog.slots[i].chord, prog.key));
    }
    CHECK (up.transposed (-3) == prog);
}

TEST_CASE ("slot timing")
{
    Progression p;
    p.bars = 4;
    p.slots.resize (4);
    CHECK (p.slotStart (0) == 0.0);
    CHECK (p.slotStart (1) == 4.0);
    CHECK (p.slotLength (3) == 4.0);
    CHECK (p.slotAt (5.0) == 1);
    CHECK (p.slotAt (16.5) == 0);
    CHECK (p.slotAt (-0.5) == 3);

    p.slots.resize (3); // 16 / 3 beats, rounded to half beats
    CHECK (p.slotStart (1) == 5.5);
    CHECK (p.slotStart (3) == 16.0);
}

TEST_CASE ("progression JSON round trip")
{
    ChordGenerator gen (StylePreset::defaults());
    auto prog = gen.generate (paramsFor ({ 6, ScaleType::Dorian }, 0xDEADBEEFCAFEull));
    prog.slots[0].locked = true;
    prog.slots[1].inversion = -1;
    prog.slots[2].voicing = theory::VoicingStyle::FlipStab;
    prog.slots[3].octave = 1;
    prog.slots[3].chord.bass = 1;

    const auto text = prog.toJson().dump();
    const auto parsed = util::Json::parse (text);
    REQUIRE (parsed.value);
    const auto back = Progression::fromJson (*parsed.value);
    REQUIRE (back);
    CHECK (*back == prog);
}

TEST_CASE ("idea history and undo")
{
    ChordGenerator gen (StylePreset::defaults());
    IdeaHistory h (3);
    auto params = paramsFor ({ 0, ScaleType::Major }, 1);
    auto ideaFor = [&] (uint64_t seed)
    {
        params.seed = seed;
        Idea idea;
        idea.chords = gen.generate (params);
        idea.bassSeed = seed + 1;
        return idea;
    };

    const auto p1 = ideaFor (1);
    h.reset (p1);
    CHECK_FALSE (h.canUndo());

    const auto p2 = ideaFor (2);
    h.addIdea (p2, "idea 2");
    auto p3 = p2;
    p3.chords.slots[0].locked = true;
    h.push (p3);

    CHECK (*h.undo() == p2);
    CHECK (*h.undo() == p1);
    CHECK_FALSE (h.undo());
    CHECK (*h.redo() == p2);

    h.push (p1.transposed (2)); // new edit drops the redo branch
    CHECK_FALSE (h.canRedo());

    for (uint64_t s = 10; s < 20; ++s)
        h.addIdea (ideaFor (s), "x");
    CHECK (h.ideas().size() == 3);
    CHECK (h.ideas().front().idea.chords.seed == 19);

    IdeaHistory restored (3);
    restored.ideasFromJson (h.ideasToJson());
    REQUIRE (restored.ideas().size() == 3);
    CHECK (restored.ideas()[1].idea == h.ideas()[1].idea);
}

TEST_CASE ("custom chord lengths")
{
    ChordGenerator gen (StylePreset::defaults());
    auto prog = gen.generate (paramsFor ({ 9, ScaleType::NaturalMinor }, 3));
    prog.setSlotLength (0, 6.0); // chord 1 takes 1.5 bars, chord 2 gets what's left of its 2 bars
    REQUIRE (prog.hasCustomLengths());
    CHECK (prog.slotLength (0) == 6.0);
    CHECK (prog.slotLength (1) == 2.0);
    CHECK (prog.slotStart (2) == 8.0);
    CHECK (prog.slotAt (7.0) == 1);

    prog.setSlotLength (3, 0.1); // clamped to half a beat
    CHECK (prog.slotLength (3) == 0.5);
    CHECK (prog.slotStart (4) == 16.0);

    // Survives JSON and regeneration with the same shape; dropped when the shape changes.
    const auto back = Progression::fromJson (*util::Json::parse (prog.toJson().dump()).value);
    REQUIRE (back);
    CHECK (back->customLengths == prog.customLengths);

    auto params = paramsFor ({ 9, ScaleType::NaturalMinor }, 4);
    CHECK (gen.generate (params, &prog).customLengths == prog.customLengths);
    params.chordCount = 3;
    CHECK_FALSE (gen.generate (params, &prog).hasCustomLengths());
}
