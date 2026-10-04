#include <doctest/doctest.h>

#include "bounce/gen/ChordGenerator.h"
#include "bounce/gen/StylePreset.h"

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

using namespace bounce;
using namespace bounce::gen;

namespace
{
std::string readFile (const std::filesystem::path& p)
{
    std::ifstream in (p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
} // namespace

TEST_CASE ("every bundled style preset parses cleanly and generates in key")
{
    std::set<std::string> ids;
    int count = 0;

    for (const auto& entry : std::filesystem::directory_iterator (BOUNCE_PRESET_DIR))
    {
        if (entry.path().extension() != ".json")
            continue;
        ++count;

        INFO (entry.path().filename().string());
        const auto parsed = util::Json::parse (readFile (entry.path()));
        REQUIRE_MESSAGE (parsed.value, parsed.error, " (line ", parsed.line, ")");

        std::vector<std::string> warnings;
        const auto preset = StylePreset::fromJson (*parsed.value, &warnings);
        CHECK_MESSAGE (warnings.empty(), (warnings.empty() ? std::string() : warnings.front()));
        CHECK (ids.insert (preset.id).second);
        CHECK (entry.path().stem().string() == preset.id);
        CHECK_FALSE (preset.name.empty());
        CHECK (preset.registerLow < preset.registerHigh);

        ChordGenerator gen (preset);
        for (uint64_t seed = 0; seed < 30; ++seed)
        {
            ChordGeneratorParams p;
            p.key = preset.defaultKey;
            p.bars = preset.defaultBars;
            p.chordCount = preset.defaultChordCount;
            p.complexity = preset.defaultComplexity;
            p.mood = preset.defaultMood;
            p.borrowed = 0.0;
            p.seed = seed;
            for (const auto& slot : gen.generate (p).slots)
                CHECK (slot.chord.isDiatonicTo (p.key.heptatonicParent()));
        }
    }

    CHECK (count >= 5);
}

TEST_CASE ("preset JSON round trip")
{
    auto p = StylePreset::defaults();
    p.id = "rt";
    p.name = "Round Trip";
    p.defaultKey = { 3, theory::ScaleType::Dorian };
    p.voicing = theory::VoicingStyle::FlipStab;
    p.stabSteps = { 0, 7 };
    p.colourWeights["min9"] = 3.0;
    p.minorTransitions["i"]["bVII"] = 9.0;

    std::vector<std::string> warnings;
    const auto back = StylePreset::fromJson (p.toJson(), &warnings);
    CHECK (warnings.empty());
    CHECK (back.id == "rt");
    CHECK (back.defaultKey == p.defaultKey);
    CHECK (back.voicing == p.voicing);
    CHECK (back.stabSteps == p.stabSteps);
    CHECK (back.colourWeights == p.colourWeights);
    CHECK (back.minorTransitions == p.minorTransitions);
    CHECK (back.majorStartWeights == p.majorStartWeights);
}

TEST_CASE ("bad preset values produce warnings, not failures")
{
    const auto parsed = util::Json::parse (R"({
        "harmony": { "key": "H", "scale": "lol", "minorTransitions": { "i": { "bVIII": 1 } },
                     "colours": { "maj13": 1 } },
        "voicing": { "style": "weird" }
    })");
    REQUIRE (parsed.value);
    std::vector<std::string> warnings;
    const auto p = StylePreset::fromJson (*parsed.value, &warnings);
    CHECK (warnings.size() == 5);
    CHECK (p.defaultKey == StylePreset::defaults().defaultKey);
}

TEST_CASE ("transition weight of zero forbids a move")
{
    auto preset = StylePreset::defaults();
    for (auto& [from, row] : preset.minorTransitions)
        row["bVI"] = 0.0;
    preset.minorStartWeights["bVI"] = 0.0;
    ChordGenerator gen (preset);

    for (uint64_t seed = 0; seed < 100; ++seed)
    {
        ChordGeneratorParams p;
        p.seed = seed;
        p.useTemplates = false; // the Markov tables (used for reharmonising / filling around locks)
        for (const auto& s : gen.generate (p).slots)
            CHECK (s.function.symbol != "bVI");
    }
}

TEST_CASE ("colour weight of zero disables a quality")
{
    auto preset = StylePreset::defaults();
    preset.colourWeights["maj7"] = 0.0;
    preset.colourWeights["min9"] = 0.0;
    ChordGenerator gen (preset);
    for (uint64_t seed = 0; seed < 100; ++seed)
    {
        ChordGeneratorParams p;
        p.seed = seed;
        p.complexity = 0.8;
        for (const auto& s : gen.generate (p).slots)
        {
            CHECK (s.chord.quality != theory::ChordQuality::Major7);
            CHECK (s.chord.quality != theory::ChordQuality::Minor9);
        }
    }
}
