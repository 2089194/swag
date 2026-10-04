#include "bounce/theory/Voicing.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cstdlib>

namespace bounce::theory
{

namespace
{
struct StyleInfo
{
    std::string_view name;
    std::string_view id;
};

constexpr std::array<StyleInfo, static_cast<size_t> (VoicingStyle::NumStyles)> styleTable { {
    { "Close",        "close" },
    { "Spread",       "spread" },
    { "Open",         "open" },
    { "3rd On Top",   "third_on_top" },
    { "Flip Stab",    "flip_stab" },
} };

/** Lowest MIDI note with the given pitch class that is >= low. */
int noteAtOrAbove (PitchClass pc, int low)
{
    return low + wrapPc (pc - low);
}

int nearestNoteInRange (PitchClass pc, int target, int low, int high)
{
    int best = noteAtOrAbove (pc, low);
    for (int n = best; n <= high; n += 12)
        if (std::abs (n - target) < std::abs (best - target))
            best = n;
    return best;
}

void removeFirst (std::vector<int>& v, int value)
{
    if (auto it = std::find (v.begin(), v.end(), value); it != v.end())
        v.erase (it);
}
} // namespace

int voicingClashPenalty (const std::vector<int>& notes, int belowNote)
{
    // Minor 2nds between neighbouring voices (a 9th tucked under the 3rd, a maj7 under the root)
    // read as a cluster rather than a chord; seconds and thirds low down turn into mud.
    int penalty = 0;
    int prev = belowNote;
    for (int n : notes)
    {
        if (prev > -1000)
        {
            const int d = n - prev;
            if (d == 1)
                penalty += 60;
            else if (d == 2 && prev < 60)
                penalty += 18;
            else if (d <= 4 && prev < 50)
                penalty += 10;
        }
        prev = n;
    }
    // A minor 9th between any two upper voices is just as harsh as a minor 2nd.
    for (size_t i = 0; i < notes.size(); ++i)
        for (size_t j = i + 1; j < notes.size(); ++j)
            if (notes[j] - notes[i] == 13)
                penalty += 40;
    return penalty;
}

std::string_view voicingStyleName (VoicingStyle s) { return styleTable[static_cast<size_t> (s)].name; }
std::string_view voicingStyleId (VoicingStyle s)   { return styleTable[static_cast<size_t> (s)].id; }

std::optional<VoicingStyle> voicingStyleFromId (std::string_view id)
{
    for (size_t i = 0; i < styleTable.size(); ++i)
        if (styleTable[i].id == id)
            return static_cast<VoicingStyle> (i);
    return std::nullopt;
}

int voiceLeadingDistance (const Voicing& a, const Voicing& b)
{
    if (a.notes.empty() || b.notes.empty())
        return 0;

    auto oneWay = [] (const std::vector<int>& from, const std::vector<int>& to)
    {
        int sum = 0;
        for (int n : from)
        {
            int best = INT_MAX;
            for (int m : to)
                best = std::min (best, std::abs (n - m));
            sum += best;
        }
        return sum;
    };

    return oneWay (a.notes, b.notes) + oneWay (b.notes, a.notes);
}

Voicing voiceChord (const Chord& chord, const VoicingParams& p, const Voicing* previous)
{
    const auto& intervals = qualityIntervals (chord.quality);
    const PitchClass bassPc = chord.bass.value_or (chord.root);
    const bool hasFifth = intervals.size() >= 3 && (intervals[2] == 7);
    const int thirdIv = intervals.size() >= 2 ? intervals[1] : -1;
    const bool hasThird = thirdIv == 3 || thirdIv == 4;

    // --- Decide which tones go to the left hand and which to the right hand.
    std::vector<int> lhPcs;
    std::vector<int> upperIntervals (intervals.begin(), intervals.end());

    switch (p.style)
    {
        case VoicingStyle::Spread:
        case VoicingStyle::ThirdOnTop:
            lhPcs.push_back (bassPc);
            if (hasFifth && ! chord.bass)
            {
                lhPcs.push_back (wrapPc (chord.root + 7));
                if (upperIntervals.size() >= 5)
                    removeFirst (upperIntervals, 7);
            }
            if (upperIntervals.size() >= 4)
                removeFirst (upperIntervals, 0);
            break;

        case VoicingStyle::Open:
            lhPcs.push_back (bassPc);
            if (upperIntervals.size() >= 4)
                removeFirst (upperIntervals, 0);
            break;

        case VoicingStyle::Close:
            if (chord.bass)
                lhPcs.push_back (*chord.bass);
            break;

        case VoicingStyle::FlipStab:
        case VoicingStyle::NumStyles:
            if (upperIntervals.size() >= 4)
                removeFirst (upperIntervals, 0);
            break;
    }

    const int maxUpper = std::max (3, std::min (p.maxUpperVoices, p.style == VoicingStyle::FlipStab ? 4 : 6));
    for (int drop : { 17, 7, 0, 14 })
        if (static_cast<int> (upperIntervals.size()) > maxUpper)
            removeFirst (upperIntervals, drop);

    // Order by pitch above the root so that rotations are true inversions of a close stack.
    std::stable_sort (upperIntervals.begin(), upperIntervals.end(),
                      [] (int a, int b) { return wrapPc (a) < wrapPc (b); });

    std::vector<PitchClass> upper;
    for (int iv : upperIntervals)
        upper.push_back (wrapPc (chord.root + iv));

    // --- Left hand: nearest to the previous lowest note, or to the middle of the LH range.
    Voicing result;
    if (! lhPcs.empty())
    {
        const int target = previous && ! previous->notes.empty() ? previous->notes.front()
                                                                 : (p.lhLow + p.lhHigh) / 2 - 3;
        const int bass = nearestNoteInRange (lhPcs[0], target, p.lhLow, p.lhHigh);
        result.notes.push_back (bass);
        for (size_t i = 1; i < lhPcs.size(); ++i)
            result.notes.push_back (noteAtOrAbove (lhPcs[i], bass + 1));
    }

    const int lhTop = result.notes.empty() ? INT_MIN : result.notes.back();

    // --- Right hand: enumerate inversions x octaves, pick the smoothest.
    const int k = static_cast<int> (upper.size());
    const int rhCentre = (p.rhLow + p.rhHigh) / 2 - 2;

    std::vector<int> best;
    int bestRotation = 0;
    long bestScore = LONG_MAX;

    // Every tone may sit in any octave of the register (not just packed close stacks), so the
    // search can open a cluster up the way a player would: A C# G# B rather than G# A B C#.
    const int maxSpan = [&]
    {
        switch (p.style)
        {
            case VoicingStyle::Close:      return 16;
            case VoicingStyle::FlipStab:   return 14;
            case VoicingStyle::Spread:     return 19;
            case VoicingStyle::ThirdOnTop: return 19;
            case VoicingStyle::Open:       return 24;
            case VoicingStyle::NumStyles:  break;
        }
        return 19;
    }();

    std::vector<int> prevUpper;
    if (previous && ! previous->notes.empty())
    {
        for (int n : previous->notes)
            if (n > lhTop)
                prevUpper.push_back (n);
        if (prevUpper.empty())
            prevUpper = previous->notes;
    }

    std::vector<std::vector<int>> choices (static_cast<size_t> (k));
    for (int i = 0; i < k; ++i)
        for (int n = noteAtOrAbove (upper[static_cast<size_t> (i)], p.rhLow); n <= p.rhHigh; n += 12)
            choices[static_cast<size_t> (i)].push_back (n);

    std::vector<int> pick (static_cast<size_t> (k), 0);
    std::vector<int> cand (static_cast<size_t> (k));
    bool more = std::all_of (choices.begin(), choices.end(), [] (const auto& c) { return ! c.empty(); });
    while (more)
    {
        for (int i = 0; i < k; ++i)
            cand[static_cast<size_t> (i)] = choices[static_cast<size_t> (i)][static_cast<size_t> (pick[static_cast<size_t> (i)])];

        // Odometer step to the next octave assignment.
        more = false;
        for (int i = 0; i < k; ++i)
        {
            if (++pick[static_cast<size_t> (i)] < static_cast<int> (choices[static_cast<size_t> (i)].size()))
            {
                more = true;
                break;
            }
            pick[static_cast<size_t> (i)] = 0;
        }

        auto sorted = cand;
        std::sort (sorted.begin(), sorted.end());
        if (std::adjacent_find (sorted.begin(), sorted.end()) != sorted.end())
            continue;
        if (sorted.back() - sorted.front() > maxSpan)
            continue;
        if (sorted.front() <= lhTop + 2)
            continue;

        const int lowestPc = wrapPc (sorted.front());
        const int rot = static_cast<int> (std::find (upper.begin(), upper.end(), lowestPc) - upper.begin());
        if (p.inversion && ((*p.inversion % k) + k) % k != rot)
            continue;

        if (p.style == VoicingStyle::ThirdOnTop && hasThird
            && wrapPc (sorted.back() - chord.root) != thirdIv)
            continue;

        long score = 0;
        if (! prevUpper.empty())
            score = 4L * voiceLeadingDistance (Voicing { sorted, rot }, Voicing { prevUpper, 0 })
                  + 2L * std::abs (sorted.back() - prevUpper.back());

        int sum = 0;
        for (int n : sorted)
            sum += n;
        score += std::abs (sum / k - rhCentre);
        score += voicingClashPenalty (sorted, lhTop);
        if (p.style == VoicingStyle::Close || p.style == VoicingStyle::FlipStab)
            score += (sorted.back() - sorted.front()) / 2; // keep them compact
        if (p.style == VoicingStyle::Open && sorted.back() - sorted.front() < 12)
            score += 20; // open means open

        if (score < bestScore)
        {
            bestScore = score;
            best = sorted;
            bestRotation = rot;
        }
    }

    // Constraints too tight (e.g. ThirdOnTop in a tiny range): relax by falling back to Spread/Close.
    if (best.empty())
    {
        if (p.style == VoicingStyle::ThirdOnTop || p.inversion)
        {
            auto relaxed = p;
            relaxed.style = p.style == VoicingStyle::ThirdOnTop ? VoicingStyle::Spread : p.style;
            relaxed.inversion.reset();
            return voiceChord (chord, relaxed, previous);
        }

        int n = noteAtOrAbove (upper.front(), std::max (p.rhLow, lhTop + 3));
        best.push_back (n);
        for (int i = 1; i < k; ++i)
            best.push_back (n = noteAtOrAbove (upper[static_cast<size_t> (i)], n + 1));
    }

    result.inversion = bestRotation;
    result.notes.insert (result.notes.end(), best.begin(), best.end());
    for (auto& n : result.notes)
        n = std::clamp (n + 12 * p.octaveShift, 0, 127);
    std::sort (result.notes.begin(), result.notes.end());
    result.notes.erase (std::unique (result.notes.begin(), result.notes.end()), result.notes.end());
    return result;
}

} // namespace bounce::theory
