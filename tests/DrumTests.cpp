#include <doctest/doctest.h>

#include "bounce/gen/DrumGenerator.h"

#include <cmath>
#include <set>

using namespace bounce;
using namespace bounce::gen;

namespace
{
DrumParams plain (uint64_t seed)
{
    DrumParams p;
    p.seed = seed;
    p.swing = 0.0;
    p.humanise = 0.0;
    p.bounce = 0.0;
    return p;
}
} // namespace

TEST_CASE ("drums: half-time backbone and loop downbeat")
{
    for (uint64_t seed = 0; seed < 40; ++seed)
    {
        auto p = plain (seed);
        const auto pat = generateDrums (p, DrumStyle::defaults());
        CHECK (pat.lengthBeats() == 16.0);

        const auto kicks = pat.kickTimes();
        REQUIRE_FALSE (kicks.empty());
        CHECK (kicks.front() == 0.0);

        // Snare/clap on beat 3 of every bar.
        std::set<int> snareBars;
        for (const auto& h : pat.lane (DrumLane::Snare))
            if (h.velocity > 100)
            {
                CHECK (std::fmod (h.start, 4.0) == doctest::Approx (2.0));
                snareBars.insert (static_cast<int> (h.start / 4.0));
            }
        CHECK (snareBars.size() == 4);

        for (const auto& h : pat.hits)
        {
            CHECK (h.start >= 0.0);
            CHECK (h.start < 16.0);
            CHECK (h.start + h.length <= 16.0 + 1e-9);
            CHECK (h.velocity >= 1);
            CHECK (h.velocity <= 127);
        }
    }
}

TEST_CASE ("drums: deterministic, and feel controls don't change the groove's choices")
{
    auto p = plain (11);
    const auto a = generateDrums (p, DrumStyle::defaults());
    CHECK (a.hits == generateDrums (p, DrumStyle::defaults()).hits);

    // Swing only moves notes: same number of hits per lane.
    p.swing = 0.6;
    const auto b = generateDrums (p, DrumStyle::defaults());
    for (int l = 0; l < numDrumLanes; ++l)
        CHECK (a.lane (static_cast<DrumLane> (l)).size() == b.lane (static_cast<DrumLane> (l)).size());
    CHECK (a.hits != b.hits);
}

TEST_CASE ("drums: rolls use only enabled rates and ramp pitch")
{
    for (int rate : { Roll16, Roll16T, Roll32, Roll32T })
    {
        auto p = plain (5);
        p.rollAmount = 1.0;
        p.rollRates = rate;
        p.rollPitch = 7.0;
        const auto pat = generateDrums (p, DrumStyle::defaults());

        std::vector<DrumHit> rolls;
        for (const auto& h : pat.lane (DrumLane::ClosedHat))
            if (h.roll && h.pitchOffset != 0)
                rolls.push_back (h);
        REQUIRE_FALSE (rolls.empty());

        // Consecutive pitched-roll hits within a window are spaced by the roll step.
        bool sawStep = false;
        for (size_t i = 1; i < rolls.size(); ++i)
        {
            const double d = rolls[i].start - rolls[i - 1].start;
            if (d < 0.3)
            {
                CHECK (d == doctest::Approx (rollStep (static_cast<RollRate> (rate))).epsilon (1e-6));
                sawStep = true;
            }
            CHECK (rolls[i].pitchOffset >= 0);
            CHECK (rolls[i].pitchOffset <= 7);
        }
        CHECK (sawStep);
    }
}

TEST_CASE ("drums: velocity curve up")
{
    auto p = plain (8);
    p.rollAmount = 1.0;
    p.rollRates = Roll32;
    p.rollCurve = RollCurve::Up;
    const auto pat = generateDrums (p, DrumStyle::defaults());
    std::vector<DrumHit> first;
    for (const auto& h : pat.lane (DrumLane::ClosedHat))
        if (h.roll && std::abs (h.start - (first.empty() ? h.start : first.back().start + 0.125)) < 1e-6)
            first.push_back (h);
    REQUIRE (first.size() >= 3);
    CHECK (first.back().velocity > first.front().velocity);
}

TEST_CASE ("drums: GM map and internal encoding")
{
    const auto pat = generateDrums (plain (3), DrumStyle::defaults());
    const auto clip = renderDrums (pat, DrumMap::gm(), 9, false);
    REQUIRE (clip.notes.size() == pat.hits.size());
    std::set<int> allowed { 36, 38, 42, 46, 63, 37, 49 };
    for (const auto& n : clip.notes)
    {
        CHECK (allowed.count (n.pitch) == 1);
        CHECK (n.channel == 9);
    }

    for (int l = 0; l < numDrumLanes; ++l)
        for (int off = -8; off <= 7; ++off)
        {
            DrumLane lane;
            int o = 0;
            decodeInternalDrumNote (internalDrumNote (static_cast<DrumLane> (l), off), lane, o);
            CHECK (static_cast<int> (lane) == l);
            CHECK (o == off);
        }
}

TEST_CASE ("drums: style JSON")
{
    const auto parsed = util::Json::parse (R"({ "kickPatterns": [[0, 6], {"steps": [0, 11], "weight": 3}],
                                                "openHatSteps": [2], "hats16ths": true, "halfTime": false })");
    REQUIRE (parsed.value);
    std::vector<std::string> warnings;
    const auto style = DrumStyle::fromJson (*parsed.value, &warnings);
    CHECK (warnings.empty());
    CHECK (style.kickPatterns.size() == 2);
    CHECK (style.kickPatterns[1].second == 3.0);
    CHECK_FALSE (style.halfTime);

    auto p = plain (2);
    p.rollAmount = 0.0;
    const auto pat = generateDrums (p, style);
    int snares = 0;
    for (const auto& h : pat.lane (DrumLane::Snare))
        snares += h.velocity > 100 ? 1 : 0;
    CHECK (snares == 8); // 2 & 4 in each of 4 bars

    const auto back = DrumStyle::fromJson (style.toJson());
    CHECK (back.kickPatterns == style.kickPatterns);
}

TEST_CASE ("drums: jerk / Jersey clap patterns repeat every 2-bar phrase")
{
    const auto parsed = util::Json::parse (R"({
        "halfTime": false,
        "snarePatterns": [ { "steps": [4, 12, 20, 28, 30], "weight": 1 } ]
    })");
    REQUIRE (parsed.value);
    std::vector<std::string> warnings;
    const auto style = DrumStyle::fromJson (*parsed.value, &warnings);
    CHECK (warnings.empty());
    REQUIRE (style.snarePatterns.size() == 1);
    CHECK (DrumStyle::fromJson (style.toJson()).snarePatterns == style.snarePatterns);

    for (uint64_t seed = 0; seed < 20; ++seed)
    {
        DrumParams p;
        p.seed = seed;
        p.swing = p.humanise = p.bounce = 0.0;
        p.percDensity = 0.0; // no ghost notes
        const auto pat = generateDrums (p, style);
        std::set<long> claps, kicks;
        for (const auto& h : pat.lane (DrumLane::Snare))
            claps.insert (std::lround (h.start * 4.0));
        for (double k : pat.kickTimes())
            kicks.insert (std::lround (k * 4.0));
        const std::set<long> expected { 4, 12, 20, 28, 30, 36, 44, 52, 60, 62 };
        CHECK (claps == expected);
        for (long c : claps)
            CHECK (kicks.count (c) == 0); // the clap owns its spot
    }
}
