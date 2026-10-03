#include "bounce/gen/MelodyGenerator.h"

#include "bounce/gen/Groove.h"
#include "bounce/util/Random.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>

namespace bounce::gen
{

namespace
{
constexpr std::array<std::string_view, static_cast<size_t> (MelodyFeel::NumFeels)> feelNames { "Straight", "Swung", "Triplet" };

/** Allowed pitches (in key) within a range, ascending. */
std::vector<int> scalePool (const theory::Key& key, int lo, int hi)
{
    const auto parent = key.heptatonicParent();
    std::vector<int> pool;
    for (int p = std::max (0, lo); p <= std::min (127, hi); ++p)
        if (parent.contains (p))
            pool.push_back (p);
    return pool;
}

bool isPentatonic (const theory::Key& key, int pitch)
{
    const auto pent = theory::Key { key.tonic, theory::isMinorMode (key.scale) ? theory::ScaleType::MinorPentatonic
                                                                               : theory::ScaleType::MajorPentatonic };
    return pent.contains (pitch);
}

bool isChordTone (const theory::Chord& c, int pitch)
{
    const auto pcs = c.pitchClasses();
    return std::find (pcs.begin(), pcs.end(), theory::wrapPc (pitch)) != pcs.end();
}

int nearestIndex (const std::vector<int>& pool, int pitch)
{
    int best = 0;
    for (int i = 0; i < static_cast<int> (pool.size()); ++i)
        if (std::abs (pool[static_cast<size_t> (i)] - pitch) < std::abs (pool[static_cast<size_t> (best)] - pitch))
            best = i;
    return best;
}

/** Nearest chord tone to `pitch` inside the pool, searching outwards. */
int nearestChordTone (const std::vector<int>& pool, const theory::Chord& chord, int pitch, bool preferBelow = false)
{
    const int idx = nearestIndex (pool, pitch);
    for (int d = 0; d < static_cast<int> (pool.size()); ++d)
    {
        for (int sign : preferBelow ? std::array<int, 2> { -1, 1 } : std::array<int, 2> { 1, -1 })
        {
            const int i = idx + sign * d;
            if (i >= 0 && i < static_cast<int> (pool.size()) && isChordTone (chord, pool[static_cast<size_t> (i)]))
                return pool[static_cast<size_t> (i)];
        }
    }
    return pool[static_cast<size_t> (idx)];
}

double stepWeight (int d)
{
    switch (std::abs (d))
    {
        case 0:  return 0.9;
        case 1:  return 3.0;
        case 2:  return 2.0;
        case 3:  return 0.9;
        case 4:  return 0.45;
        default: return 0.12;
    }
}

std::vector<int> pickRhythm (util::Random& rng, int gridSteps, double density, bool triplet)
{
    const int k = std::clamp (static_cast<int> (std::lround (2.0 + density * 6.0)), 2, triplet ? 8 : 9);
    std::vector<double> w (static_cast<size_t> (gridSteps));
    for (int s = 0; s < gridSteps; ++s)
    {
        if (triplet)
            w[static_cast<size_t> (s)] = s % 3 == 0 ? 3.0 : 1.4;
        else
            w[static_cast<size_t> (s)] = s % 4 == 0 ? 3.0 : s % 2 == 0 ? 2.2 : 0.6 + density;
    }

    std::set<int> chosen;
    if (rng.chance (0.7))
        chosen.insert (rng.chance (0.6) ? 0 : (triplet ? 3 : 2));
    while (static_cast<int> (chosen.size()) < k)
    {
        const int s = rng.weightedIndex (w);
        if (s < 0)
            break;
        chosen.insert (s);
        w[static_cast<size_t> (s)] = 0.0;
    }
    return { chosen.begin(), chosen.end() };
}

std::vector<int> pickContour (util::Random& rng, size_t count)
{
    std::vector<int> moves { 0 };
    const double w[] = { 0.8, 3.0, 3.0, 2.0, 2.0, 0.8, 0.8, 0.35, 0.35 };
    const int deltas[] = { 0, 1, -1, 2, -2, 3, -3, 4, -4 };
    while (moves.size() < count)
        moves.push_back (deltas[rng.weightedIndex (w)]);
    return moves;
}
} // namespace

std::string_view melodyFeelName (MelodyFeel f) { return feelNames[static_cast<size_t> (f)]; }

midi::MidiClip generateMelody (const Progression& prog, const MelodyParams& p)
{
    midi::MidiClip clip;
    clip.name = "Melody";
    clip.lengthBeats = prog.lengthBeats();
    if (prog.slots.empty())
        return clip;

    const int lo = std::min (p.rangeLow, p.rangeHigh - 7), hi = std::max (p.rangeHigh, p.rangeLow + 7);
    const auto pool = scalePool (prog.key, lo, hi);
    if (pool.empty())
        return clip;

    const bool triplet = p.feel == MelodyFeel::Triplet;
    const int grid = triplet ? 12 : 16;
    const double stepBeats = 4.0 / grid;
    const double density = std::clamp (p.density, 0.0, 1.0);
    const double pent = std::clamp (p.pentatonic, 0.0, 1.0);

    util::Random motifRng (util::deriveSeed (p.seed, 500));
    const auto motifRhythm = pickRhythm (motifRng, grid, density, triplet);
    const auto motifContour = pickContour (motifRng, 12);

    int prevPitch = pool[pool.size() / 2];
    std::vector<midi::Note> notes;

    for (int bar = 0; bar < prog.bars; ++bar)
    {
        const double barStart = bar * 4.0;

        if (bar < 32 && (p.lockedBars >> bar) & 1u)
        {
            for (const auto& n : p.lockedNotes)
                if (n.start >= barStart - 1e-9 && n.start < barStart + 4.0 - 1e-9)
                {
                    notes.push_back (n);
                    prevPitch = n.pitch;
                }
            continue;
        }

        util::Random rng (util::deriveSeed (p.seed, static_cast<uint64_t> (600 + bar)));
        const bool response = p.callResponse && bar % 2 == 1;

        auto rhythm = (response || rng.chance (p.repetition)) ? motifRhythm : pickRhythm (rng, grid, density, triplet);
        if (response && rhythm.size() > 2 && rng.chance (0.5))
            rhythm.pop_back(); // answer with one note less and let the last one ring

        const bool reuseContour = rng.chance (p.catchiness);
        auto contour = reuseContour ? motifContour : pickContour (rng, 12);
        if (reuseContour && rng.chance (0.35))
            contour[static_cast<size_t> (1 + rng.nextInt (static_cast<int> (contour.size()) - 1))] *= -1; // variation

        int idx = nearestIndex (pool, prevPitch);
        for (size_t k = 0; k < rhythm.size(); ++k)
        {
            const double t = barStart + rhythm[k] * stepBeats;
            const auto& chord = prog.slots[static_cast<size_t> (prog.slotAt (t + 1e-6))].chord;
            const bool strong = std::abs (t - std::round (t)) < 1e-6;
            const bool last = k + 1 == rhythm.size();

            int pitch;
            if (k == 0)
                pitch = nearestChordTone (pool, chord, prevPitch);
            else if (reuseContour)
            {
                idx = std::clamp (idx + contour[k % contour.size()], 0, static_cast<int> (pool.size()) - 1);
                pitch = pool[static_cast<size_t> (idx)];
            }
            else
            {
                std::vector<double> w (pool.size());
                for (size_t c = 0; c < pool.size(); ++c)
                {
                    double wt = stepWeight (static_cast<int> (c) - idx);
                    wt *= isPentatonic (prog.key, pool[c]) ? 1.0 + 2.0 * pent : 1.0 - 0.7 * pent;
                    if (strong && isChordTone (chord, pool[c]))
                        wt *= 4.0;
                    w[c] = wt;
                }
                pitch = pool[static_cast<size_t> (std::max (0, rng.weightedIndex (w)))];
            }

            if (strong && ! isChordTone (chord, pitch))
                pitch = nearestChordTone (pool, chord, pitch);
            if (response && last)
                pitch = nearestChordTone (pool, chord, pitch, true); // resolve downwards

            idx = nearestIndex (pool, pitch);

            const double next = last ? barStart + 4.0 : barStart + rhythm[k + 1] * stepBeats;
            double length = (next - t) * (0.82 + 0.1 * density);
            if (last)
                length = std::min (next - t, response ? 2.0 : 1.0 + (1.0 - density));

            midi::Note n;
            n.pitch = pitch;
            n.start = p.feel == MelodyFeel::Swung ? swingTime (t, p.swing) : t;
            n.length = std::max (stepBeats * 0.5, std::min (length, prog.lengthBeats() - n.start));
            n.velocity = std::clamp ((strong ? 100 : 86) - (response ? 4 : 0) + rng.nextIntInclusive (-5, 5), 1, 127);
            n.channel = p.channel;
            notes.push_back (n);
            prevPitch = pitch;
        }
    }

    clip.notes = std::move (notes);
    clip.sortByTime();

    // Keep the line monophonic after swing moved some onsets (locked bars stay untouched).
    for (size_t i = 0; i + 1 < clip.notes.size(); ++i)
    {
        auto& n = clip.notes[i];
        const auto bar = static_cast<int> (n.start / 4.0);
        if (bar < 32 && ((p.lockedBars >> bar) & 1u))
            continue;
        const double limit = clip.notes[i + 1].start - 0.01;
        if (n.start + n.length > limit)
            n.length = std::max (0.03, limit - n.start);
    }
    return clip;
}

midi::MidiClip generateCounterMelody (const Progression& prog, const midi::MidiClip& main, const CounterParams& p)
{
    midi::MidiClip clip;
    clip.name = "Counter";
    clip.lengthBeats = prog.lengthBeats();
    const auto pool = scalePool (prog.key, std::min (p.rangeLow, p.rangeHigh - 7), std::max (p.rangeHigh, p.rangeLow + 7));
    if (pool.empty() || prog.slots.empty())
        return clip;

    const double len = prog.lengthBeats();
    auto mainOnsetNear = [&] (double t, double tol)
    {
        for (const auto& n : main.notes)
            if (std::abs (n.start - t) < tol)
                return true;
        return false;
    };
    auto mainSounding = [&] (double t) -> const midi::Note*
    {
        const midi::Note* found = nullptr;
        for (const auto& n : main.notes)
            if (n.start <= t + 1e-9 && n.start + n.length > t + 1e-9)
                found = &n;
        return found;
    };

    util::Random rng (util::deriveSeed (p.seed, 900));
    const double density = std::clamp (p.density, 0.0, 1.0);
    std::vector<double> onsets;
    for (double t = 0.0; t < len - 1e-9; t += 0.5)
    {
        if (mainOnsetNear (t, 0.26))
            continue;
        const auto* m = mainSounding (t);
        const double chance = m == nullptr ? 0.3 + 0.55 * density : 0.12 + 0.3 * density;
        if (rng.chance (chance))
            onsets.push_back (t);
    }

    int prev = pool[pool.size() / 2];
    int prevMain = -1;
    for (size_t k = 0; k < onsets.size(); ++k)
    {
        const double t = onsets[k];
        const auto& chord = prog.slots[static_cast<size_t> (prog.slotAt (t + 1e-6))].chord;
        const auto* m = mainSounding (t);
        const bool strong = std::abs (t - std::round (t)) < 1e-6;
        const int mainDir = (m != nullptr && prevMain >= 0) ? (m->pitch > prevMain) - (m->pitch < prevMain) : 0;
        const int idx = nearestIndex (pool, prev);

        std::vector<double> w (pool.size());
        for (size_t c = 0; c < pool.size(); ++c)
        {
            const int cand = pool[c];
            double wt = stepWeight (static_cast<int> (c) - idx);
            if (isChordTone (chord, cand))
                wt *= strong ? 3.0 : 1.6;
            if (m != nullptr)
            {
                const int iv = theory::wrapPc (std::abs (cand - m->pitch));
                if (iv == 1 || iv == 11 || iv == 6)
                    wt = 0.0;          // minor 2nd / major 7th / tritone against the melody
                else if (iv == 2 || iv == 10)
                    wt *= 0.15;
                else if (cand == m->pitch)
                    wt *= 0.05;        // don't double the melody
                const int candDir = (cand > prev) - (cand < prev);
                if (mainDir != 0)
                    wt *= candDir == -mainDir ? 2.5 : candDir == mainDir ? 0.5 : 1.0;
            }
            w[c] = wt;
        }

        const int pick = rng.weightedIndex (w);
        if (pick < 0)
            continue;
        const int pitch = pool[static_cast<size_t> (pick)];

        // Ends at the next counter onset, the next main onset (so no new clash can start
        // under it) or after two beats, whichever comes first.
        double end = std::min (len, t + 2.0);
        if (k + 1 < onsets.size())
            end = std::min (end, onsets[k + 1]);
        for (const auto& n : main.notes)
            if (n.start > t + 1e-6 && n.start < end)
                end = n.start;

        midi::Note note;
        note.pitch = pitch;
        note.start = t;
        note.length = std::max (0.1, (end - t) * 0.92);
        note.velocity = std::clamp ((strong ? 84 : 74) + rng.nextIntInclusive (-5, 5), 1, 127);
        note.channel = p.channel;
        clip.notes.push_back (note);

        prev = pitch;
        if (m != nullptr)
            prevMain = m->pitch;
    }

    clip.sortByTime();
    return clip;
}

} // namespace bounce::gen
