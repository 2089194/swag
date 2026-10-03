#include "bounce/gen/DrumGenerator.h"

#include "bounce/gen/Groove.h"
#include "bounce/util/Random.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace bounce::gen
{

namespace
{
constexpr std::array<std::string_view, numDrumLanes> laneNames { "Kick", "Snare/Clap", "Closed Hat", "Open Hat", "Perc", "Rim", "FX" };
constexpr std::array<std::string_view, static_cast<size_t> (RollCurve::NumCurves)> curveNames { "Up", "Down", "Flat", "Wave" };

using Patterns = std::vector<std::pair<std::vector<int>, double>>;

const std::vector<int>& pickPattern (const Patterns& patterns, util::Random& rng)
{
    static const std::vector<int> fallback { 0, 10 };
    if (patterns.empty())
        return fallback;
    std::vector<double> w;
    for (const auto& p : patterns)
        w.push_back (p.second);
    const int idx = rng.weightedIndex (w);
    return idx >= 0 ? patterns[static_cast<size_t> (idx)].first : fallback;
}

Patterns patternsFromJson (const util::Json& arr, std::vector<std::string>* warnings, const char* what)
{
    Patterns out;
    for (const auto& item : arr.asArray())
    {
        std::vector<int> steps;
        double weight = 1.0;
        const auto& stepsJson = item.isArray() ? item : item["steps"];
        if (item.isObject())
            weight = item["weight"].asNumber (1.0);
        for (const auto& s : stepsJson.asArray())
        {
            const int step = s.asInt (-1);
            if (step < 0 || step > 15)
            {
                if (warnings)
                    warnings->push_back (std::string (what) + ": step out of range 0..15");
                continue;
            }
            steps.push_back (step);
        }
        if (! steps.empty())
            out.emplace_back (std::move (steps), weight);
    }
    return out;
}

util::Json patternsToJson (const Patterns& p)
{
    util::Json arr;
    arr.asArray();
    for (const auto& [steps, weight] : p)
    {
        util::Json item, s;
        s.asArray();
        for (int st : steps)
            s.push (st);
        item.set ("steps", std::move (s));
        item.set ("weight", weight);
        arr.push (std::move (item));
    }
    return arr;
}

uint64_t laneStream (DrumLane lane, int bar, int salt = 0)
{
    return static_cast<uint64_t> (static_cast<int> (lane) * 1000 + bar * 7 + salt * 100000 + 17);
}
} // namespace

std::string_view drumLaneName (DrumLane lane) { return laneNames[static_cast<size_t> (lane)]; }
std::string_view rollCurveName (RollCurve c)  { return curveNames[static_cast<size_t> (c)]; }

double rollStep (RollRate rate)
{
    switch (rate)
    {
        case Roll16:  return 0.25;
        case Roll16T: return 1.0 / 6.0;
        case Roll32:  return 0.125;
        case Roll32T: return 1.0 / 12.0;
    }
    return 0.25;
}

std::vector<double> DrumPattern::kickTimes() const
{
    std::vector<double> t;
    for (const auto& h : hits)
        if (h.lane == DrumLane::Kick)
            t.push_back (h.start);
    std::sort (t.begin(), t.end());
    return t;
}

std::vector<DrumHit> DrumPattern::lane (DrumLane l) const
{
    std::vector<DrumHit> out;
    for (const auto& h : hits)
        if (h.lane == l)
            out.push_back (h);
    return out;
}

//==============================================================================
DrumStyle DrumStyle::defaults()
{
    DrumStyle s;
    // Half-time bounce kicks (snare lands on step 8).
    s.kickPatterns = {
        { { 0, 10 }, 2.0 },        { { 0, 7, 10 }, 2.0 },   { { 0, 3, 10 }, 1.5 },
        { { 0, 6, 11 }, 1.5 },     { { 0, 10, 11 }, 1.2 },  { { 0, 3, 7, 10 }, 1.0 },
        { { 0, 2, 10, 13 }, 1.0 }, { { 0, 7, 9, 14 }, 0.8 },
    };
    s.percPatterns = {
        { { 3, 11 }, 1.5 }, { { 6, 13 }, 1.2 }, { { 5, 9, 14 }, 1.0 }, { { 2, 10 }, 1.0 }, { { 7, 15 }, 0.8 },
    };
    return s;
}

DrumStyle DrumStyle::fromJson (const util::Json& j, std::vector<std::string>* warnings)
{
    auto s = defaults();
    if (j.has ("kickPatterns"))
        if (auto p = patternsFromJson (j["kickPatterns"], warnings, "kickPatterns"); ! p.empty())
            s.kickPatterns = std::move (p);
    if (j.has ("percPatterns"))
        if (auto p = patternsFromJson (j["percPatterns"], warnings, "percPatterns"); ! p.empty())
            s.percPatterns = std::move (p);
    if (j.has ("openHatSteps"))
    {
        s.openHatSteps.clear();
        for (const auto& v : j["openHatSteps"].asArray())
            if (const int st = v.asInt (-1); st >= 0 && st <= 15)
                s.openHatSteps.push_back (st);
    }
    s.hats16ths = j["hats16ths"].asBool (s.hats16ths);
    s.halfTime = j["halfTime"].asBool (s.halfTime);
    return s;
}

util::Json DrumStyle::toJson() const
{
    util::Json j;
    j.set ("kickPatterns", patternsToJson (kickPatterns));
    j.set ("percPatterns", patternsToJson (percPatterns));
    util::Json oh;
    oh.asArray();
    for (int s : openHatSteps)
        oh.push (s);
    j.set ("openHatSteps", std::move (oh));
    j.set ("hats16ths", hats16ths);
    j.set ("halfTime", halfTime);
    return j;
}

//==============================================================================
DrumPattern generateDrums (const DrumParams& p, const DrumStyle& style)
{
    DrumPattern pat;
    pat.bars = std::clamp (p.bars, 1, 16);
    const int spb = 16; // 16th steps per 4/4 bar
    const double len = pat.lengthBeats();
    const double density = std::clamp (p.kickDensity, 0.0, 1.0);
    const int snareStep = 8;

    auto add = [&pat] (DrumLane lane, double start, double length, int vel, int pitch = 0, bool roll = false)
    {
        pat.hits.push_back ({ lane, start, length, std::clamp (vel, 1, 127), pitch, roll });
    };

    // --- Kick: an A pattern, a B variation on every second bar, pickups at phrase ends.
    util::Random phraseRng (util::deriveSeed (p.seed, 1));
    const auto patternA = pickPattern (style.kickPatterns, phraseRng);
    auto patternB = patternA;
    {
        util::Random vr (util::deriveSeed (p.seed, 2));
        const std::vector<int> movable { 3, 6, 7, 11, 13, 14 };
        if (vr.chance (0.5) && patternB.size() > 1)
            patternB.erase (patternB.begin() + 1 + vr.nextInt (static_cast<int> (patternB.size()) - 1));
        patternB.push_back (movable[static_cast<size_t> (vr.nextInt (static_cast<int> (movable.size())))]);
    }

    for (int bar = 0; bar < pat.bars; ++bar)
    {
        util::Random rng (util::deriveSeed (p.seed, laneStream (DrumLane::Kick, bar)));
        std::set<int> steps;
        for (int s : (bar % 2 == 1 ? patternB : patternA))
            steps.insert (s);

        for (int extra : { 3, 6, 11, 13, 14, 15 })
            if (! steps.count (extra) && rng.chance (density * 0.22))
                steps.insert (extra);
        for (auto it = steps.begin(); it != steps.end();)
            it = (*it != 0 && rng.chance ((1.0 - density) * 0.25)) ? steps.erase (it) : std::next (it);

        if (bar == pat.bars - 1 && pat.bars > 1 && rng.chance (0.5))
            steps.insert (rng.chance (0.5) ? 14 : 15); // pickup into the loop
        if (style.halfTime)
            steps.erase (snareStep);
        steps.insert (0);

        for (int s : steps)
            add (DrumLane::Kick, bar * 4 + s * 0.25, 0.5, s == 0 ? 115 : 100 + rng.nextIntInclusive (-6, 4));
    }

    // --- Snare / clap: beat 3 (half-time) or 2 & 4, with occasional ghost notes.
    for (int bar = 0; bar < pat.bars; ++bar)
    {
        util::Random rng (util::deriveSeed (p.seed, laneStream (DrumLane::Snare, bar)));
        if (style.halfTime)
            add (DrumLane::Snare, bar * 4 + snareStep * 0.25, 0.5, 112);
        else
        {
            add (DrumLane::Snare, bar * 4 + 1.0, 0.5, 110);
            add (DrumLane::Snare, bar * 4 + 3.0, 0.5, 112);
        }
        if (rng.chance (std::clamp (p.percDensity, 0.0, 1.0) * 0.3))
            add (DrumLane::Snare, bar * 4 + (rng.chance (0.5) ? 15 : 7) * 0.25, 0.2, 52 + rng.nextInt (12));
    }

    // --- Hats: base grid, rolls, stutters, open hats on off-beats.
    int enabledRates[4];
    int numRates = 0;
    for (int r : { Roll16, Roll16T, Roll32, Roll32T })
        if (p.rollRates & r)
            enabledRates[numRates++] = r;

    for (int bar = 0; bar < pat.bars; ++bar)
    {
        util::Random rng (util::deriveSeed (p.seed, laneStream (DrumLane::ClosedHat, bar)));
        const double barStart = bar * 4.0;

        double rollFrom = -1.0, rollTo = -1.0;
        if (rng.chance (std::clamp (p.rollAmount, 0.0, 1.0)))
        {
            static const std::pair<int, int> windows[] = { { 12, 16 }, { 14, 16 }, { 6, 8 }, { 10, 12 } };
            const double ww[] = { 3.0, 3.0, 1.5, 1.5 };
            const auto w = windows[rng.weightedIndex (ww)];
            rollFrom = barStart + w.first * 0.25;
            rollTo = barStart + w.second * 0.25;
        }

        double stutterAt = -1.0;
        if (rng.chance (std::clamp (p.rollAmount, 0.0, 1.0) * 0.35))
        {
            const double t = barStart + 0.5 * (2 * rng.nextInt (4) + 1); // an off-beat 8th
            if (t < rollFrom - 1e-9 || t >= rollTo - 1e-9)
                stutterAt = t;
        }

        std::set<int> openSteps;
        util::Random ohRng (util::deriveSeed (p.seed, laneStream (DrumLane::OpenHat, bar)));
        if (! style.openHatSteps.empty() && ohRng.chance (std::clamp (p.openHatAmount, 0.0, 1.0)))
            openSteps.insert (style.openHatSteps[static_cast<size_t> (ohRng.nextInt (static_cast<int> (style.openHatSteps.size())))]);

        const int stride = style.hats16ths ? 1 : 2;
        for (int s = 0; s < spb; s += stride)
        {
            const double t = barStart + s * 0.25;
            if (t >= rollFrom - 1e-9 && t < rollTo - 1e-9)
                continue;
            if (stutterAt >= 0.0 && t >= stutterAt - 1e-9 && t < stutterAt + 0.5 - 1e-9)
                continue;
            if (openSteps.count (s))
            {
                add (DrumLane::OpenHat, t, 0.5, 92);
                continue;
            }
            const int vel = s % 4 == 0 ? 100 : s % 2 == 0 ? 80 : 64;
            add (DrumLane::ClosedHat, t, 0.2, vel + rng.nextIntInclusive (-4, 4));
        }

        if (rollFrom >= 0.0)
        {
            const auto rate = numRates > 0 ? static_cast<RollRate> (enabledRates[rng.nextInt (numRates)]) : Roll16;
            const double step = rollStep (rate);
            const int count = static_cast<int> (std::floor ((rollTo - rollFrom) / step + 1e-6));
            for (int k = 0; k < count; ++k)
            {
                const double frac = count > 1 ? static_cast<double> (k) / (count - 1) : 0.0;
                double vel = 85.0;
                switch (p.rollCurve)
                {
                    case RollCurve::Up:   vel = 55.0 + 55.0 * frac; break;
                    case RollCurve::Down: vel = 110.0 - 55.0 * frac; break;
                    case RollCurve::Flat: vel = 85.0; break;
                    case RollCurve::Wave: vel = 75.0 + 30.0 * std::sin (frac * 3.0 * 3.14159265358979); break;
                    case RollCurve::NumCurves: break;
                }
                add (DrumLane::ClosedHat, rollFrom + k * step, step * 0.9, static_cast<int> (std::lround (vel)),
                     static_cast<int> (std::lround (std::clamp (p.rollPitch, -12.0, 12.0) * frac)), true);
            }
        }

        if (stutterAt >= 0.0)
            for (int k = 0; k < 3; ++k)
                add (DrumLane::ClosedHat, stutterAt + k / 6.0, 1.0 / 7.0, 78 - k * 6, 0, true);
    }

    // --- Percs and rims.
    const double pd = std::clamp (p.percDensity, 0.0, 1.0);
    for (int bar = 0; bar < pat.bars; ++bar)
    {
        util::Random rng (util::deriveSeed (p.seed, laneStream (DrumLane::Perc, bar)));
        if (rng.chance (pd))
            for (int s : pickPattern (style.percPatterns, rng))
                add (DrumLane::Perc, bar * 4 + s * 0.25, 0.25, 82 + rng.nextIntInclusive (-8, 10));

        util::Random rim (util::deriveSeed (p.seed, laneStream (DrumLane::Rim, bar)));
        if (bar % 2 == 1 && rim.chance (pd * 0.6))
        {
            const bool triple = rim.chance (0.5);
            for (int s : triple ? std::vector<int> { 13, 14, 15 } : std::vector<int> { 11, 15 })
                add (DrumLane::Rim, bar * 4 + s * 0.25, 0.2, 68 + rim.nextIntInclusive (-6, 8));
        }
    }

    if (p.fx)
        add (DrumLane::FX, 0.0, 4.0, 96);

    // --- Feel: swing, bounce, humanise. The loop downbeat stays exactly on the bar.
    util::Random feel (util::deriveSeed (p.seed, 99));
    const double jitter = msToBeats (6.0, p.bpm) * std::clamp (p.humanise, 0.0, 1.0);
    for (auto& h : pat.hits)
    {
        const bool downbeat = h.start < 1e-9;
        if (! h.roll)
            h.start = swingTime (h.start, p.swing);

        if (h.lane == DrumLane::Kick && std::fmod (h.start, 4.0) > 1e-9)
            h.start += std::clamp (p.bounce, 0.0, 1.0) * 0.05;
        if (h.lane == DrumLane::Perc)
            h.start += std::clamp (p.bounce, 0.0, 1.0) * 0.07;

        if (! downbeat && ! h.roll)
            h.start += feel.nextGaussianish() * jitter;
        h.velocity = std::clamp (static_cast<int> (std::lround (h.velocity + feel.nextGaussianish() * 6.0 * p.humanise)), 1, 127);

        h.start = downbeat ? 0.0 : std::clamp (h.start, 0.0, len - 1e-3);
        h.length = std::max (0.02, std::min (h.length, len - h.start));
    }

    std::stable_sort (pat.hits.begin(), pat.hits.end(), [] (const DrumHit& a, const DrumHit& b)
    {
        return a.start < b.start || (a.start == b.start && a.lane < b.lane);
    });
    return pat;
}

//==============================================================================
DrumMap DrumMap::gm()
{
    DrumMap m;
    m.notes = { 36, 38, 42, 46, 63, 37, 49 };
    return m;
}

midi::MidiClip renderDrums (const DrumPattern& pattern, const DrumMap& map, int channel, bool pitchToNotes)
{
    midi::MidiClip clip;
    clip.name = "Drums";
    clip.lengthBeats = pattern.lengthBeats();
    for (const auto& h : pattern.hits)
    {
        midi::Note n;
        n.pitch = std::clamp (map.notes[static_cast<size_t> (h.lane)] + (pitchToNotes ? h.pitchOffset : 0), 0, 127);
        n.start = h.start;
        n.length = h.length;
        n.velocity = h.velocity;
        n.channel = channel;
        clip.notes.push_back (n);
    }
    clip.sortByTime();
    return clip;
}

int internalDrumNote (DrumLane lane, int pitchOffset)
{
    return 8 + static_cast<int> (lane) * 16 + std::clamp (pitchOffset, -8, 7);
}

void decodeInternalDrumNote (int note, DrumLane& lane, int& pitchOffset)
{
    const int laneIndex = std::clamp (note / 16, 0, numDrumLanes - 1);
    lane = static_cast<DrumLane> (laneIndex);
    pitchOffset = note - (8 + laneIndex * 16);
}

midi::MidiClip renderDrumsInternal (const DrumPattern& pattern, int channel)
{
    midi::MidiClip clip;
    clip.name = "Drums (internal)";
    clip.lengthBeats = pattern.lengthBeats();
    for (const auto& h : pattern.hits)
        clip.notes.push_back ({ internalDrumNote (h.lane, h.pitchOffset), h.start, h.length, h.velocity, channel });
    clip.sortByTime();
    return clip;
}

} // namespace bounce::gen
