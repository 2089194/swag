#include "bounce/gen/BassGenerator.h"

#include "bounce/util/Random.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>

namespace bounce::gen
{

namespace
{
constexpr std::array<std::string_view, static_cast<size_t> (BassMode::NumModes)> modeNames {
    "Root Follow", "Syncopated Bounce", "Octave Jumper", "Glide Heavy", "Sustain"
};

} // namespace

std::string_view bassModeName (BassMode m) { return modeNames[static_cast<size_t> (m)]; }

int countGlides (const midi::MidiClip& clip)
{
    int glides = 0;
    for (size_t i = 0; i + 1 < clip.notes.size(); ++i)
        if (clip.notes[i].start + clip.notes[i].length > clip.notes[i + 1].start + 1e-9)
            ++glides;
    return glides;
}

namespace
{
/** One-bar 808 rhythms (16th steps). Picked once per loop and repeated every bar, the way a
    producer programs a bar of 808 and loops it, so the line sounds intentional. */
struct BassRhythm
{
    std::vector<int> steps;
    std::vector<int> octaveSteps; // hits that pop up an octave (Octave Jumper / Bounce)
};

const std::vector<BassRhythm>& rhythmsFor (BassMode mode)
{
    static const std::vector<BassRhythm> rootFollow {
        { { 0, 8 }, {} },
        { { 0, 6, 8 }, {} },
        { { 0, 10 }, {} },
    };
    static const std::vector<BassRhythm> bounce {
        { { 0, 6, 10 }, { 10 } },
        { { 0, 3, 6, 10 }, { 6 } },
        { { 0, 3, 10, 14 }, { 14 } },
        { { 0, 7, 10, 13 }, { 13 } },
        { { 0, 3, 6, 10, 14 }, { 14 } },
        { { 0, 3, 8, 11, 14 }, { 11 } },
    };
    static const std::vector<BassRhythm> octave {
        { { 0, 3, 6, 10, 14 }, { 3, 10 } },
        { { 0, 2, 6, 8, 10, 14 }, { 2, 10 } },
        { { 0, 3, 8, 11 }, { 3, 11 } },
        { { 0, 6, 8, 14 }, { 6, 14 } },
    };
    static const std::vector<BassRhythm> glide {
        { { 0, 10 }, { 10 } },
        { { 0, 6 }, { 6 } },
        { { 0, 8, 14 }, { 14 } },
    };
    static const std::vector<BassRhythm> sustain { { { 0 }, {} } };

    switch (mode)
    {
        case BassMode::RootFollow:       return rootFollow;
        case BassMode::SyncopatedBounce: return bounce;
        case BassMode::OctaveJumper:     return octave;
        case BassMode::GlideHeavy:       return glide;
        case BassMode::Sustain:
        case BassMode::NumModes:         break;
    }
    return sustain;
}

int stepInBar (double t)
{
    const double inBar = t - std::floor (t / 4.0 + 1e-9) * 4.0;
    return static_cast<int> (std::lround (inBar * 4.0)) % 16;
}
} // namespace

midi::MidiClip generateBass (const Progression& prog, const std::vector<double>& kicks, const BassParams& p)
{
    midi::MidiClip clip;
    clip.name = "808";
    clip.lengthBeats = prog.lengthBeats();
    const int n = static_cast<int> (prog.slots.size());
    if (n == 0)
        return clip;

    const double density = std::clamp (p.density, 0.0, 1.0);
    const double len = prog.lengthBeats();
    const bool octaves = p.octaveRange >= 2;
    util::Random rng (util::deriveSeed (p.seed, 300));

    // --- One rhythm for the whole loop. Density leans towards the busier shapes.
    const auto& rhythms = rhythmsFor (p.mode);
    std::vector<double> rw;
    for (const auto& r : rhythms)
        rw.push_back (std::exp ((density - 0.5) * 2.5 * (static_cast<double> (r.steps.size()) - 3.5) / 2.0));
    const auto& rhythm = rhythms[static_cast<size_t> (std::max (0, rng.weightedIndex (rw)))];

    // --- Roots: each chord's bass note, in whichever octave keeps the line smooth, so it
    // moves like a bass player's (F G A, not F1 G1 A1 then a leap up to D#2).
    const int rootLow = p.lowNote;
    const int rootHigh = p.lowNote + 16;
    // Every octave assignment is tried (at most 2^8) and the one with the least total motion,
    // loop seam included, wins; leaps beyond a 5th cost extra, and so does hugging an edge.
    const int centre = p.lowNote + 8;
    std::vector<std::vector<int>> options (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
    {
        const auto& chord = prog.slots[static_cast<size_t> (i)].chord;
        const int pc = chord.bass.value_or (chord.root);
        for (int c = rootLow + theory::wrapPc (pc - rootLow); c <= rootHigh; c += 12)
            options[static_cast<size_t> (i)].push_back (c);
    }
    std::vector<int> roots (static_cast<size_t> (n));
    std::vector<int> pick (static_cast<size_t> (n), 0);
    double bestCost = 1e18;
    for (bool more = true; more;)
    {
        double cost = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const int a0 = options[static_cast<size_t> (i)][static_cast<size_t> (pick[static_cast<size_t> (i)])];
            const int j = (i + 1) % n;
            const int b0 = options[static_cast<size_t> (j)][static_cast<size_t> (pick[static_cast<size_t> (j)])];
            const int leap = std::abs (b0 - a0);
            cost += leap + (leap > 7 ? 3.0 * (leap - 7) : 0.0);
            cost += 0.15 * std::abs (a0 - centre);
        }
        if (cost < bestCost)
        {
            bestCost = cost;
            for (int i = 0; i < n; ++i)
                roots[static_cast<size_t> (i)] = options[static_cast<size_t> (i)][static_cast<size_t> (pick[static_cast<size_t> (i)])];
        }
        more = false;
        for (int i = 0; i < n; ++i)
        {
            if (++pick[static_cast<size_t> (i)] < static_cast<int> (options[static_cast<size_t> (i)].size()))
            {
                more = true;
                break;
            }
            pick[static_cast<size_t> (i)] = 0;
        }
    }

    struct Onset
    {
        double t;
        int pitch;
        int velocity;
        bool pop;
    };
    std::vector<Onset> onsets;

    for (int i = 0; i < n; ++i)
    {
        const double s0 = prog.slotStart (i);
        const double s1 = s0 + prog.slotLength (i);
        const int root = roots[static_cast<size_t> (i)];

        std::set<double> times { s0 };
        if (p.mode != BassMode::Sustain && p.mode != BassMode::NumModes)
        {
            if (p.lockToKick && ! kicks.empty())
            {
                // Every kick inside the chord. Sparse settings keep only kicks on the beat.
                for (double k : kicks)
                    if (k > s0 + 1e-6 && k < s1 - 1e-6
                        && (density >= 0.3 || std::abs (k - std::round (k)) < 1e-6))
                        times.insert (k);
            }
            else
            {
                for (double bar = std::floor (s0 / 4.0) * 4.0; bar < s1 - 1e-6; bar += 4.0)
                    for (int step : rhythm.steps)
                        if (const double t = bar + step * 0.25; t > s0 + 1e-6 && t < s1 - 1e-6)
                            times.insert (t);
            }
        }

        int hitInChord = 0;
        const int hitsInChord = static_cast<int> (times.size());
        for (double t : times)
        {
            const int step = stepInBar (t);
            bool pop = false;
            if (octaves && t > s0 + 1e-6)
            {
                const bool listed = std::find (rhythm.octaveSteps.begin(), rhythm.octaveSteps.end(), step) != rhythm.octaveSteps.end();
                switch (p.mode)
                {
                    case BassMode::OctaveJumper:
                        // Locked to the kick, pop on the off-beat hits; otherwise the pattern's own pops.
                        pop = p.lockToKick && ! kicks.empty() ? (step % 4 != 0) : listed;
                        break;
                    case BassMode::SyncopatedBounce:
                    case BassMode::GlideHeavy:
                        // One pop near the end of a chord: the classic 808 "bounce" up and back.
                        pop = p.lockToKick && ! kicks.empty() ? (hitInChord == hitsInChord - 1 && hitsInChord >= 3 && t - s0 >= 2.0)
                                                              : listed;
                        break;
                    default:
                        break;
                }
            }
            const int pitch = pop ? root + 12 : root;
            const int vel = t <= s0 + 1e-6 ? 112 : (pop ? 104 : 100);
            onsets.push_back ({ t, pitch, vel, pop });
            ++hitInChord;
        }
    }

    std::sort (onsets.begin(), onsets.end(), [] (const Onset& a, const Onset& b) { return a.t < b.t; });

    // --- Which chord changes slide: decided once per loop so the glides land in the same
    // places every time it repeats. More glide = more changes slide; the turnaround goes first.
    const double glide = std::clamp (p.glide * (p.mode == BassMode::GlideHeavy ? 1.6 : 1.0), 0.0, 1.0);
    std::set<double> glideInto;
    if (glide > 0.0 && n > 1)
    {
        std::vector<int> order;
        for (int i = n - 1; i >= 1; --i)
            order.push_back (i);
        const int count = std::clamp (static_cast<int> (std::lround (glide * (n - 1) + 0.25)), 1, n - 1);
        for (int j = 0; j < count; ++j)
            glideInto.insert (prog.slotStart (order[static_cast<size_t> (j)]));
    }
    const bool glidePops = glide >= 0.5;

    const double noteLength = std::clamp (p.noteLength, 0.1, 1.0);
    for (size_t i = 0; i < onsets.size(); ++i)
    {
        const double next = i + 1 < onsets.size() ? onsets[i + 1].t : len;
        const double gap = next - onsets[i].t;
        midi::Note note;
        note.pitch = std::clamp (onsets[i].pitch, 0, 127);
        note.start = onsets[i].t;
        note.velocity = onsets[i].velocity;
        note.channel = p.channel;
        note.length = p.mode == BassMode::Sustain ? gap : std::max (0.25, gap * noteLength);
        note.length = std::min (note.length, gap);

        if (i + 1 < onsets.size())
        {
            const auto& nx = onsets[i + 1];
            const int jump = std::abs (nx.pitch - onsets[i].pitch);
            const bool intoChange = glideInto.count (nx.t) > 0;
            const bool intoPop = glidePops && nx.pop;
            if (jump > 0 && jump <= 12 && (intoChange || intoPop))
                note.length = gap + 0.125; // overlap a 32nd: the next note slides from this one
        }

        note.length = std::min (note.length, len - note.start);
        clip.notes.push_back (note);
    }

    // Portamento hints for FL's 808 presets / samplers.
    clip.controls.push_back ({ 0.0, 65, p.glide > 0.0 ? 127 : 0, p.channel });
    clip.controls.push_back ({ 0.0, 5, static_cast<int> (std::lround (std::clamp (p.glide, 0.0, 1.0) * 64.0)), p.channel });
    return clip;
}

} // namespace bounce::gen
