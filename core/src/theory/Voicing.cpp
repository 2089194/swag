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

    for (int rot = 0; rot < k; ++rot)
    {
        if (p.inversion && ((*p.inversion % k) + k) % k != rot)
            continue;

        for (int start = p.rhLow; start < p.rhLow + 12; ++start)
        {
            if (wrapPc (start) != upper[static_cast<size_t> (rot)])
                continue;

            for (int octave = 0; octave < 4; ++octave)
            {
                std::vector<int> cand;
                int prev = start + octave * 12;
                cand.push_back (prev);
                for (int i = 1; i < k; ++i)
                {
                    prev = noteAtOrAbove (upper[static_cast<size_t> ((rot + i) % k)], prev + 1);
                    cand.push_back (prev);
                }

                if (p.style == VoicingStyle::Open && cand.size() >= 3)
                {
                    cand[cand.size() - 2] -= 12;
                    std::sort (cand.begin(), cand.end());
                }

                if (cand.front() < p.rhLow || cand.back() > p.rhHigh)
                    continue;
                if (cand.front() <= lhTop + 2)
                    continue;

                if (p.style == VoicingStyle::ThirdOnTop && hasThird
                    && wrapPc (cand.back() - chord.root) != thirdIv)
                    continue;

                long score = 0;
                if (previous && ! previous->notes.empty())
                {
                    Voicing c { cand, rot };
                    std::vector<int> prevUpper;
                    for (int n : previous->notes)
                        if (n > lhTop)
                            prevUpper.push_back (n);
                    if (prevUpper.empty())
                        prevUpper = previous->notes;
                    score = 4L * voiceLeadingDistance (c, Voicing { prevUpper, 0 })
                          + 2L * std::abs (cand.back() - prevUpper.back());
                }

                int sum = 0;
                for (int n : cand)
                    sum += n;
                score += std::abs (sum / k - rhCentre);

                if (score < bestScore)
                {
                    bestScore = score;
                    best = cand;
                    bestRotation = rot;
                }
            }
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
