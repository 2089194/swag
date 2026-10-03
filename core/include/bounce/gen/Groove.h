#pragma once

#include <algorithm>
#include <cmath>

namespace bounce::gen
{

/** Delays off-beat 16ths (x.25, x.75) by up to a 32nd note. Positions that aren't on the
    16th grid (triplets, rolls) are left alone. */
inline double swingTime (double beat, double swing)
{
    const double sixteenths = beat * 4.0;
    const double nearest = std::round (sixteenths);
    if (std::abs (sixteenths - nearest) > 1e-6)
        return beat;
    const auto step = static_cast<long long> (nearest);
    if ((step % 2) != 0)
        return beat + std::clamp (swing, 0.0, 1.0) * 0.125;
    return beat;
}

/** Converts milliseconds to beats at a tempo. */
inline double msToBeats (double ms, double bpm)
{
    return ms * std::max (1.0, bpm) / 60000.0;
}

} // namespace bounce::gen
