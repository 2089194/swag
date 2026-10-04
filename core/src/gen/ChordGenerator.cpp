#include "bounce/gen/ChordGenerator.h"

#include "bounce/util/Random.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace bounce::gen
{

using theory::ChordFamily;
using theory::ChordQuality;
using theory::Key;

namespace
{
const std::vector<ChordQuality>& qualitiesForFamily (ChordFamily f)
{
    static const std::vector<ChordQuality> major {
        ChordQuality::Major, ChordQuality::Major7, ChordQuality::Add9, ChordQuality::SixNine,
        ChordQuality::Major9, ChordQuality::Major7Sharp11, ChordQuality::Dominant7, ChordQuality::Dominant9,
        ChordQuality::Sus2, ChordQuality::Major7Sus2, ChordQuality::Major6, ChordQuality::Dominant7Sus4,
    };
    static const std::vector<ChordQuality> minor {
        ChordQuality::Minor, ChordQuality::Minor7, ChordQuality::MinorAdd9, ChordQuality::Minor9,
        ChordQuality::Minor11, ChordQuality::Minor6, ChordQuality::MinorMajor7, ChordQuality::MinorSixNine,
    };
    static const std::vector<ChordQuality> dim { ChordQuality::Diminished, ChordQuality::HalfDiminished7 };
    static const std::vector<ChordQuality> aug { ChordQuality::Augmented };
    static const std::vector<ChordQuality> sus { ChordQuality::Sus2, ChordQuality::Sus4, ChordQuality::Major7Sus2 };

    switch (f)
    {
        case ChordFamily::Major:      return major;
        case ChordFamily::Minor:      return minor;
        case ChordFamily::Diminished: return dim;
        case ChordFamily::Augmented:  return aug;
        case ChordFamily::Suspended:  return sus;
    }
    return major;
}

/** Key a borrowed chord is taken from: the parallel major/minor. */
Key borrowSource (const Key& key)
{
    const auto parent = key.heptatonicParent();
    return theory::isMinorMode (parent.scale) ? Key { parent.tonic, theory::ScaleType::Major }
                                              : Key { parent.tonic, theory::ScaleType::NaturalMinor };
}

unsigned chordMask (const theory::Chord& c)
{
    unsigned m = 0;
    for (auto pc : c.pitchClasses())
        m |= 1u << pc;
    return m;
}

double moodSign (ChordFamily f)
{
    switch (f)
    {
        case ChordFamily::Minor:
        case ChordFamily::Diminished: return -1.0;
        default:                      return 1.0;
    }
}
} // namespace

ChordGenerator::ChordGenerator (StylePreset preset) : style (std::move (preset)) {}

double ChordGenerator::transitionWeight (const Key& key, const std::string& from, const std::string& to) const
{
    const auto& table = theory::isMinorMode (key.scale) ? style.minorTransitions : style.majorTransitions;
    const auto row = table.find (from);
    if (row == table.end())
        return 0.5; // unknown origin (e.g. a locked chord the table never mentions): stay open
    const auto it = row->second.find (to);
    return it != row->second.end() ? std::max (0.0, it->second) : 0.08;
}

double ChordGenerator::startWeight (const Key& key, const std::string& token) const
{
    const auto& m = theory::isMinorMode (key.scale) ? style.minorStartWeights : style.majorStartWeights;
    const auto it = m.find (token);
    return it != m.end() ? std::max (0.0, it->second) : 0.4;
}

std::vector<HarmonyFunction> ChordGenerator::availableFunctions (const Key& key, double borrowed) const
{
    const auto parent = key.heptatonicParent();
    const bool minor = theory::isMinorMode (key.scale);
    const auto& table = minor ? style.minorTransitions : style.majorTransitions;
    const auto& starts = minor ? style.minorStartWeights : style.majorStartWeights;

    std::vector<std::string> tokens;
    auto addToken = [&tokens] (const std::string& t)
    {
        if (std::find (tokens.begin(), tokens.end(), t) == tokens.end())
            tokens.push_back (t);
    };

    // Diatonic triads first so the order (and therefore the RNG mapping) is stable.
    for (int d = 0; d < 7; ++d)
    {
        const auto c = theory::diatonicChord (parent, d, 3);
        addToken (makeHarmonyFunction (c.root - key.tonic, c.family()).symbol);
    }
    for (const auto& [from, row] : table)
    {
        addToken (from);
        for (const auto& [to, w] : row)
            addToken (to);
    }
    for (const auto& [t, w] : starts)
        addToken (t);

    std::vector<HarmonyFunction> result;
    for (const auto& t : tokens)
    {
        auto fn = parseHarmonyToken (t);
        if (! fn)
            continue;
        fn->borrowed = ! isDiatonicFunction (*fn, key);
        if (fn->borrowed && borrowed <= 0.0)
            continue;
        if (qualitiesFor (*fn, key).empty())
            continue;
        result.push_back (*fn);
    }
    return result;
}

std::vector<ChordQuality> ChordGenerator::qualitiesFor (const HarmonyFunction& fn, const Key& key) const
{
    const auto parent = key.heptatonicParent();
    const auto triad = functionTriad (fn, key);
    const bool diatonic = triad.isDiatonicTo (parent);
    const unsigned allowed = diatonic ? parent.mask() : (borrowSource (key).mask() | chordMask (triad));

    std::vector<ChordQuality> result;
    for (auto q : qualitiesForFamily (fn.family))
    {
        const auto it = style.colourWeights.find (std::string (theory::qualityId (q)));
        if (it != style.colourWeights.end() && it->second <= 0.0)
            continue;
        const theory::Chord c { triad.root, q, std::nullopt };
        if ((chordMask (c) & ~allowed) == 0)
            result.push_back (q);
    }
    return result;
}

theory::Chord ChordGenerator::chooseChord (const HarmonyFunction& fn, const ChordGeneratorParams& params,
                                           uint64_t streamSeed, const theory::Chord* avoid) const
{
    const auto qualities = qualitiesFor (fn, params.key);
    const auto root = theory::wrapPc (params.key.tonic + fn.offset);
    if (qualities.empty())
        return functionTriad (fn, params.key);

    const double target = std::clamp (params.complexity, 0.0, 1.0) * 2.6;
    std::vector<double> weights;
    for (auto q : qualities)
    {
        const auto it = style.colourWeights.find (std::string (theory::qualityId (q)));
        const double colour = it != style.colourWeights.end() ? it->second : 0.5;
        const double tier = theory::qualityToneCount (q) - 3;
        // Squared so the preset's favourite colours clearly win; complexity picks the richness.
        double w = colour * colour * std::exp (-(tier - target) * (tier - target) / 0.4);
        if (avoid && avoid->root == root && avoid->quality == q && qualities.size() > 1)
            w = 0.0;
        weights.push_back (w);
    }

    util::Random rng (streamSeed);
    const int idx = rng.weightedIndex (weights);
    return { root, qualities[static_cast<size_t> (std::max (0, idx))], std::nullopt };
}

std::vector<ChordGenerator::Candidate> ChordGenerator::candidatesFor (const Progression& prog, int index,
                                                                      const ChordGeneratorParams& params,
                                                                      const std::vector<bool>& known) const
{
    const int n = static_cast<int> (prog.slots.size());
    const auto& key = params.key;
    const double borrowed = std::clamp (params.borrowed, 0.0, 1.0);
    const double mood = std::clamp (params.mood, -1.0, 1.0);

    const int prevIdx = index > 0 ? index - 1 : n - 1;
    const int nextIdx = index < n - 1 ? index + 1 : 0;
    const bool hasPrev = n > 1 && known[static_cast<size_t> (prevIdx)];
    const bool hasNext = n > 1 && nextIdx != index && known[static_cast<size_t> (nextIdx)];

    // Look-ahead/seam constraints are soft (a small floor keeps options open), but a weight
    // the preset explicitly set to 0 always forbids that move.
    const auto soft = [] (double t) { return t > 0.0 ? t + 0.15 : 0.0; };

    std::vector<Candidate> out;
    for (const auto& fn : availableFunctions (key, borrowed))
    {
        double w = 1.0;

        if (index == 0)
        {
            w *= startWeight (key, fn.symbol);
            if (hasPrev) // loop seam: the last chord leads back into the first
                w *= soft (transitionWeight (key, prog.slots[static_cast<size_t> (prevIdx)].function.symbol, fn.symbol));
        }
        else if (hasPrev)
        {
            w *= transitionWeight (key, prog.slots[static_cast<size_t> (prevIdx)].function.symbol, fn.symbol);
        }

        if (hasNext)
            w *= soft (transitionWeight (key, fn.symbol, prog.slots[static_cast<size_t> (nextIdx)].function.symbol));

        w *= std::exp (mood * moodSign (fn.family) * 0.8);
        if (fn.borrowed)
        {
            w *= borrowed * 1.6;
            if (! theory::isMinorMode (key.scale))
                w *= std::exp (-mood * 0.5); // modal mixture darkens major keys
        }
        if (fn.family == ChordFamily::Diminished)
            w *= 0.5;

        // Variety: no immediate repeats, and avoid reusing chords inside short loops.
        for (int j = 0; j < n; ++j)
        {
            if (j == index || ! known[static_cast<size_t> (j)])
                continue;
            if (prog.slots[static_cast<size_t> (j)].function.symbol != fn.symbol)
                continue;
            const bool adjacent = j == prevIdx || j == nextIdx;
            w *= adjacent ? 0.03 : (n <= 4 ? 0.35 : 0.7);
        }

        if (w > 0.0)
            out.push_back ({ fn, w });
    }
    return out;
}

Progression ChordGenerator::generate (const ChordGeneratorParams& params, const Progression* existing) const
{
    const int n = std::clamp (params.chordCount, 1, 8);

    Progression prog;
    prog.key = params.key;
    prog.bars = std::clamp (params.bars, 1, 16);
    prog.seed = params.seed;
    prog.styleId = style.id;
    prog.slots.resize (static_cast<size_t> (n));

    std::vector<bool> known (static_cast<size_t> (n), false);

    if (existing != nullptr)
    {
        // Locked chords survive regeneration. If only the tonic changed, they move with it.
        const bool sameScale = existing->key.scale == params.key.scale;
        const int shift = params.key.tonic - existing->key.tonic;
        for (int i = 0; i < n && i < static_cast<int> (existing->slots.size()); ++i)
        {
            const auto& slot = existing->slots[static_cast<size_t> (i)];
            if (! slot.locked)
                continue;
            auto copy = slot;
            if (sameScale && shift != 0)
                copy.chord = copy.chord.transposed (shift);
            copy.function.borrowed = ! isDiatonicFunction (copy.function, params.key);
            prog.slots[static_cast<size_t> (i)] = copy;
            known[static_cast<size_t> (i)] = true;
        }
    }

    // Keep hand-set / detected chord durations when the loop shape is unchanged.
    if (existing != nullptr && existing->hasCustomLengths()
        && static_cast<int> (existing->slots.size()) == n && existing->bars == prog.bars)
        prog.customLengths = existing->customLengths;

    if (params.useTemplates && fillFromTemplate (prog, known, params))
        return prog;

    for (int i = 0; i < n; ++i)
    {
        if (known[static_cast<size_t> (i)])
            continue;

        auto candidates = candidatesFor (prog, i, params, known);
        std::vector<double> weights;
        for (const auto& c : candidates)
            weights.push_back (c.weight);

        util::Random rng (util::deriveSeed (params.seed, static_cast<uint64_t> (2 * i + 1)));
        const int pick = rng.weightedIndex (weights);

        HarmonyFunction fn;
        if (pick >= 0)
            fn = candidates[static_cast<size_t> (pick)].function;
        else
            fn = makeHarmonyFunction (0, theory::isMinorMode (params.key.scale) ? ChordFamily::Minor : ChordFamily::Major);

        auto& slot = prog.slots[static_cast<size_t> (i)];
        slot = {};
        slot.function = fn;
        slot.chord = chooseChord (fn, params, util::deriveSeed (params.seed, static_cast<uint64_t> (2 * i + 2)), nullptr);
        known[static_cast<size_t> (i)] = true;
    }

    return prog;
}

bool ChordGenerator::fillFromTemplate (Progression& prog, std::vector<bool>& known, const ChordGeneratorParams& params) const
{
    const int n = static_cast<int> (prog.slots.size());
    const auto& templates = theory::isMinorMode (params.key.scale) ? style.minorProgressions : style.majorProgressions;
    if (templates.empty() || n == 0)
        return false;

    const double borrowed = std::clamp (params.borrowed, 0.0, 1.0);
    const double mood = std::clamp (params.mood, -1.0, 1.0);

    struct Option
    {
        std::vector<HarmonyFunction> seq;
        double weight;
    };
    std::vector<Option> options;

    for (const auto& t : templates)
    {
        const int len = static_cast<int> (t.chords.size());
        if (len == 0)
            continue;

        std::vector<HarmonyFunction> seq;
        bool ok = true;
        bool hasBorrowed = false;
        double sign = 0.0;
        for (int i = 0; i < n && ok; ++i)
        {
            auto fn = parseHarmonyToken (t.chords[static_cast<size_t> (i % len)]);
            if (! fn)
            {
                ok = false;
                break;
            }
            fn->borrowed = ! isDiatonicFunction (*fn, params.key);
            if ((fn->borrowed && borrowed <= 0.0) || qualitiesFor (*fn, params.key).empty())
                ok = false;
            if (known[static_cast<size_t> (i)] && prog.slots[static_cast<size_t> (i)].function.symbol != fn->symbol)
                ok = false;
            hasBorrowed |= fn->borrowed;
            sign += moodSign (fn->family);
            seq.push_back (*fn);
        }
        if (! ok)
            continue;

        // Loops flow: no chord repeated back to back, including across the loop seam.
        for (int i = 0; i < n && ok; ++i)
            if (n > 1 && seq[static_cast<size_t> (i)].symbol == seq[static_cast<size_t> ((i + 1) % n)].symbol)
                ok = false;
        if (! ok)
            continue;

        double w = t.weight;
        if (len == n)
            w *= 2.0;
        else if (n % len != 0 && n > len)
            w *= 0.3; // a shape stretched unevenly over the loop
        else if (len <= 2 && n >= 4)
            w *= 0.6; // two-chord vamps are a flavour, not the default
        w *= std::exp (mood * (sign / n) * 3.0);
        if (hasBorrowed)
            w *= borrowed * 2.0;
        if (w > 0.0)
            options.push_back ({ std::move (seq), w });
    }

    if (options.empty())
        return false;

    std::vector<double> weights;
    for (const auto& o : options)
        weights.push_back (o.weight);
    util::Random rng (util::deriveSeed (params.seed, 7777));
    const int pick = rng.weightedIndex (weights);
    if (pick < 0)
        return false;

    // The same chord keeps the same colour wherever it comes back in the loop.
    std::map<std::string, theory::Chord> chosen;
    for (int i = 0; i < n; ++i)
        if (known[static_cast<size_t> (i)])
            chosen.emplace (prog.slots[static_cast<size_t> (i)].function.symbol, prog.slots[static_cast<size_t> (i)].chord);

    const auto& seq = options[static_cast<size_t> (pick)].seq;
    for (int i = 0; i < n; ++i)
    {
        if (known[static_cast<size_t> (i)])
            continue;
        auto& slot = prog.slots[static_cast<size_t> (i)];
        slot = {};
        slot.function = seq[static_cast<size_t> (i)];
        if (auto it = chosen.find (slot.function.symbol); it != chosen.end())
            slot.chord = it->second;
        else
        {
            slot.chord = chooseChord (slot.function, params, util::deriveSeed (params.seed, static_cast<uint64_t> (2 * i + 2)), nullptr);
            chosen.emplace (slot.function.symbol, slot.chord);
        }
        known[static_cast<size_t> (i)] = true;
    }
    return true;
}

Progression ChordGenerator::reharmonise (const Progression& progression, int slotIndex,
                                         const ChordGeneratorParams& params, uint64_t variation) const
{
    const int n = static_cast<int> (progression.slots.size());
    if (slotIndex < 0 || slotIndex >= n)
        return progression;

    auto local = params;
    local.key = progression.key;

    auto prog = progression;
    std::vector<bool> known (static_cast<size_t> (n), true);
    known[static_cast<size_t> (slotIndex)] = false;

    const auto current = prog.slots[static_cast<size_t> (slotIndex)];
    auto candidates = candidatesFor (prog, slotIndex, local, known);

    std::vector<double> weights;
    for (const auto& c : candidates)
        weights.push_back (c.function.symbol == current.function.symbol ? c.weight * 0.15 : c.weight);

    const uint64_t base = util::deriveSeed (progression.seed ^ util::mixSeed (variation + 1), 1000);
    util::Random rng (util::deriveSeed (base, static_cast<uint64_t> (slotIndex)));
    const int pick = rng.weightedIndex (weights);
    if (pick < 0)
        return progression;

    auto& slot = prog.slots[static_cast<size_t> (slotIndex)];
    slot.function = candidates[static_cast<size_t> (pick)].function;
    slot.chord = chooseChord (slot.function, local, util::deriveSeed (base, 64 + static_cast<uint64_t> (slotIndex)),
                              &current.chord);
    slot.inversion.reset();
    return prog;
}

} // namespace bounce::gen
