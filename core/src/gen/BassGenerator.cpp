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

int fifthInterval (theory::ChordQuality q)
{
    const auto& iv = theory::qualityIntervals (q);
    return iv.size() >= 3 ? iv[2] : 7; // dim chords use their b5
}
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

    struct Onset
    {
        double t;
        int pitch;
        int velocity;
    };
    std::vector<Onset> onsets;

    for (int i = 0; i < n; ++i)
    {
        const auto& chord = prog.slots[static_cast<size_t> (i)].chord;
        const double s0 = prog.slotStart (i);
        const double s1 = s0 + prog.slotLength (i);
        util::Random rng (util::deriveSeed (p.seed, static_cast<uint64_t> (300 + i)));

        const int rootPc = chord.bass.value_or (chord.root);
        const int root = p.lowNote + theory::wrapPc (rootPc - p.lowNote);
        const int fifth = root + fifthInterval (chord.quality) - (root + fifthInterval (chord.quality) > p.lowNote + 14 ? 12 : 0);
        const int octave = p.octaveRange >= 2 ? root + 12 : root;

        // Candidate onset times inside this chord.
        std::set<double> times { s0 };
        auto addKicks = [&] (double keepChance)
        {
            for (double k : kicks)
                if (k > s0 + 1e-6 && k < s1 - 1e-6 && rng.chance (keepChance))
                    times.insert (k);
        };

        switch (p.mode)
        {
            case BassMode::Sustain:
            case BassMode::NumModes:
                break;

            case BassMode::RootFollow:
                if (p.lockToKick)
                    addKicks (0.4 + 0.6 * density);
                else
                    for (double t = std::ceil (s0 / 2.0) * 2.0; t < s1 - 1e-6; t += 2.0)
                        if (t > s0 + 1e-6 && rng.chance (0.5 + 0.5 * density))
                            times.insert (t);
                break;

            case BassMode::SyncopatedBounce:
            case BassMode::OctaveJumper:
                if (p.lockToKick)
                    addKicks (0.75 + 0.25 * density);
                else
                    for (int step : { 3, 6, 10, 11, 14 })
                        for (double bar = std::floor (s0 / 4.0) * 4.0; bar < s1; bar += 4.0)
                            if (const double t = bar + step * 0.25; t > s0 + 1e-6 && t < s1 - 1e-6 && rng.chance (0.35 + 0.5 * density))
                                times.insert (t);
                // Extra syncopated 16ths on top.
                for (double t = s0 + 0.75; t < s1 - 1e-6; t += 1.0)
                    if (rng.chance (density * 0.25))
                        times.insert (t);
                break;

            case BassMode::GlideHeavy:
                if (p.lockToKick)
                    addKicks (0.25 + 0.3 * density);
                else if (s1 - s0 >= 4.0 && rng.chance (0.5 + 0.4 * density))
                    times.insert (s0 + 2.5);
                break;
        }

        int k = 0;
        for (double t : times)
        {
            int pitch = root;
            if (t > s0 + 1e-6)
            {
                switch (p.mode)
                {
                    case BassMode::OctaveJumper:
                        pitch = (k % 2 == 1) ? octave : root;
                        break;
                    case BassMode::SyncopatedBounce:
                    {
                        const double w[] = { 3.0, 1.0, p.octaveRange >= 2 ? 1.2 : 0.0 };
                        const int c = rng.weightedIndex (w);
                        pitch = c == 1 ? fifth : c == 2 ? octave : root;
                        break;
                    }
                    case BassMode::GlideHeavy:
                    {
                        const double w[] = { 1.0, 1.0, p.octaveRange >= 2 ? 1.5 : 0.0 };
                        const int c = rng.weightedIndex (w);
                        pitch = c == 1 ? fifth : c == 2 ? octave : root;
                        break;
                    }
                    default:
                        pitch = root;
                        break;
                }
            }
            onsets.push_back ({ t, pitch, t <= s0 + 1e-6 ? 112 : 98 + rng.nextIntInclusive (-6, 6) });
            ++k;
        }
    }

    std::sort (onsets.begin(), onsets.end(), [] (const Onset& a, const Onset& b) { return a.t < b.t; });

    // Lengths, then glides (overlaps) between different pitches.
    util::Random glideRng (util::deriveSeed (p.seed, 777));
    const double glideChance = std::clamp (p.glide * (p.mode == BassMode::GlideHeavy ? 1.6 : 1.0), 0.0, 1.0);
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

        const bool canGlide = i + 1 < onsets.size() && onsets[i + 1].pitch != onsets[i].pitch
                           && std::abs (onsets[i + 1].pitch - onsets[i].pitch) <= 12;
        if (canGlide && glideRng.chance (glideChance))
            note.length = gap + 0.125; // overlap a 32nd: the next note slides from this one

        note.length = std::min (note.length, len - note.start);
        clip.notes.push_back (note);
    }

    // Portamento hints for FL's 808 presets / samplers.
    clip.controls.push_back ({ 0.0, 65, p.glide > 0.0 ? 127 : 0, p.channel });
    clip.controls.push_back ({ 0.0, 5, static_cast<int> (std::lround (std::clamp (p.glide, 0.0, 1.0) * 64.0)), p.channel });
    return clip;
}

} // namespace bounce::gen
