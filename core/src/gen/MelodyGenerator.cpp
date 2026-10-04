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

/** Hand-picked one-bar rhythms (16th steps, or triplet 8ths/16ths on a 12 grid), from sparse to
    busy. Melodies in this style are built from short rhythmic cells that repeat, so a cell is
    chosen once and the phrase is built from it. */
const std::vector<std::vector<int>>& rhythmCells (bool triplet, int tier)
{
    static const std::vector<std::vector<int>> straight[3] {
        { { 0, 6, 8 }, { 0, 3, 8 }, { 2, 8, 10 }, { 0, 6, 10 }, { 0, 3, 6 } },
        { { 0, 3, 6, 8, 11 }, { 0, 2, 6, 8, 14 }, { 0, 3, 6, 10, 12 }, { 2, 4, 8, 10, 14 }, { 0, 4, 6, 10 }, { 0, 3, 6, 8, 12 } },
        { { 0, 2, 3, 6, 8, 10, 11, 14 }, { 0, 1, 3, 6, 8, 9, 11, 14 }, { 0, 2, 4, 7, 8, 10, 12, 14 }, { 0, 3, 4, 6, 8, 11, 12, 14 } },
    };
    static const std::vector<std::vector<int>> trip[3] {
        { { 0, 4, 6 }, { 0, 3, 8 }, { 0, 5, 6 } },
        { { 0, 2, 3, 6, 9 }, { 0, 1, 2, 6, 8 }, { 0, 3, 5, 6, 9 }, { 0, 2, 4, 6, 8 } },
        { { 0, 1, 2, 3, 5, 6, 8, 9, 11 }, { 0, 2, 3, 4, 6, 8, 9, 10 }, { 0, 1, 2, 4, 6, 7, 8, 10 } },
    };
    return triplet ? trip[std::clamp (tier, 0, 2)] : straight[std::clamp (tier, 0, 2)];
}

/** A motif contour in pool steps: mostly steps, the odd small leap, never wandering far. */
std::vector<int> pickContour (util::Random& rng, size_t count)
{
    std::vector<int> moves { 0 };
    const double w[] = { 1.2, 3.0, 3.0, 1.4, 1.4 };
    const int deltas[] = { 0, 1, -1, 2, -2 };
    int pos = 0;
    while (moves.size() < count)
    {
        int d = deltas[rng.weightedIndex (w)];
        if (std::abs (pos + d) > 3)
            d = -d; // stay within a few notes of where the motif started
        pos += d;
        moves.push_back (d);
    }
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
    const auto scale = scalePool (prog.key, lo, hi);
    if (scale.empty())
        return clip;

    // The line moves through the pentatonic scale (the backbone of this sound) unless the
    // pentatonic knob is turned down; chord tones are always available on top.
    const double pent = std::clamp (p.pentatonic, 0.0, 1.0);
    std::vector<int> pool;
    for (int n : scale)
        if (pent < 0.5 || isPentatonic (prog.key, n))
            pool.push_back (n);
    if (pool.size() < 4)
        pool = scale;

    const bool triplet = p.feel == MelodyFeel::Triplet;
    const int grid = triplet ? 12 : 16;
    const double stepBeats = 4.0 / grid;
    const double density = std::clamp (p.density, 0.0, 1.0);
    const int tier = density < 0.34 ? 0 : density < 0.72 ? 1 : 2;

    util::Random motifRng (util::deriveSeed (p.seed, 500));
    const auto& cells = rhythmCells (triplet, tier);
    const auto rhythmA = cells[static_cast<size_t> (motifRng.nextInt (static_cast<int> (cells.size())))];
    const auto rhythmB = cells[static_cast<size_t> (motifRng.nextInt (static_cast<int> (cells.size())))];
    const auto contourA = pickContour (motifRng, 12);
    auto contourB = contourA; // the answer mirrors the call
    for (size_t i = 1; i < contourB.size(); ++i)
        contourB[i] = -contourB[i];

    // A narrow home register: the motif is re-anchored here every bar, so the phrase follows
    // the chords (a sequence) without drifting up or down the keyboard.
    const int home = (lo + hi) / 2 - 2;

    // The note the whole loop resolves to: the tonic, else the nearest tone of the last chord.
    const auto& lastChord = prog.slots.back().chord;

    std::vector<midi::Note> notes;
    const int phraseBars = prog.bars >= 4 ? 4 : prog.bars;

    for (int bar = 0; bar < prog.bars; ++bar)
    {
        const double barStart = bar * 4.0;

        if (bar < 32 && (p.lockedBars >> bar) & 1u)
        {
            for (const auto& n : p.lockedNotes)
                if (n.start >= barStart - 1e-9 && n.start < barStart + 4.0 - 1e-9)
                    notes.push_back (n);
            continue;
        }

        util::Random rng (util::deriveSeed (p.seed, static_cast<uint64_t> (600 + bar)));
        const int inPhrase = bar % std::max (1, phraseBars);
        const bool phraseEnd = inPhrase == phraseBars - 1;
        const bool loopEnd = bar == prog.bars - 1;
        const bool response = p.callResponse && inPhrase % 2 == 1;

        // Call & response: A B A B'. Otherwise A A A A', with repetition deciding how often a
        // bar keeps the motif's rhythm and catchiness how often it keeps its shape.
        auto rhythm = response ? rhythmB : rhythmA;
        if (! response && ! rng.chance (std::max (p.repetition, 0.0)))
            rhythm = rhythmB;
        if ((response || phraseEnd) && rhythm.size() > 3 && rhythm.back() >= grid - 4)
            rhythm.pop_back(); // leave room at the end of the phrase so the last note rings

        auto contour = response ? contourB : contourA;
        if (! rng.chance (std::max (p.catchiness, 0.0)) && contour.size() > 2)
            contour[static_cast<size_t> (1 + rng.nextInt (static_cast<int> (contour.size()) - 1))] *= -1;

        int idx = 0;
        for (size_t k = 0; k < rhythm.size(); ++k)
        {
            const double t = barStart + rhythm[k] * stepBeats;
            const auto& chord = prog.slots[static_cast<size_t> (prog.slotAt (t + 1e-6))].chord;
            const bool strong = std::abs (t - std::round (t)) < 1e-6;
            const bool last = k + 1 == rhythm.size();

            int pitch;
            if (k == 0)
            {
                // Start each bar on a chord tone near home.
                pitch = nearestChordTone (scale, chord, home);
                idx = nearestIndex (pool, pitch);
            }
            else
            {
                idx = std::clamp (idx + contour[k % contour.size()], 0, static_cast<int> (pool.size()) - 1);
                pitch = pool[static_cast<size_t> (idx)];
            }

            if (strong && ! isChordTone (chord, pitch))
                pitch = nearestChordTone (scale, chord, pitch);

            if (last && (phraseEnd || response))
            {
                // Cadence: the end of a phrase lands on the chord's root (the tonic at the very end
                // of the loop when it fits), approached from above like a sung answer.
                const auto& target = loopEnd ? lastChord : chord;
                int rootPc = target.root;
                if (loopEnd && isChordTone (target, prog.key.tonic))
                    rootPc = prog.key.tonic;
                int best = -1;
                for (int n : scale)
                    if (theory::wrapPc (n) == rootPc && (best < 0 || std::abs (n - pitch) < std::abs (best - pitch)))
                        best = n;
                if (best >= 0)
                    pitch = best;
                else
                    pitch = nearestChordTone (scale, chord, pitch, true);
            }
            idx = nearestIndex (pool, pitch);

            const double next = last ? barStart + 4.0 : barStart + rhythm[k + 1] * stepBeats;
            double length = (next - t) * (0.8 + 0.12 * density);
            if (last)
                length = std::min (next - t, (phraseEnd || response) ? 2.0 : 1.0 + (1.0 - density));

            midi::Note n;
            n.pitch = pitch;
            n.start = p.feel == MelodyFeel::Swung ? swingTime (t, p.swing) : t;
            n.length = std::max (stepBeats * 0.5, std::min (length, prog.lengthBeats() - n.start));
            n.velocity = std::clamp ((strong ? 100 : 88) - (response ? 4 : 0) + rng.nextIntInclusive (-4, 4), 1, 127);
            n.channel = p.channel;
            notes.push_back (n);
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
