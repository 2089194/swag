#include "bounce/gen/ChordRenderer.h"

#include "bounce/util/Random.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace bounce::gen
{

namespace
{
struct RhythmInfo
{
    std::string_view name;
    std::string_view id;
};

constexpr std::array<RhythmInfo, static_cast<size_t> (ChordRhythm::NumRhythms)> rhythmTable { {
    { "Sustain",  "sustain" },
    { "Half",     "half" },
    { "Stabs",    "stabs" },
    { "8th Pulse","pulse8" },
} };

/** Onsets (start, length) for one slot. */
std::vector<std::pair<double, double>> slotHits (double start, double length, int beatsPerBar,
                                                 const ChordPerformance& perf)
{
    std::vector<std::pair<double, double>> hits;
    const double end = start + length;

    switch (perf.rhythm)
    {
        case ChordRhythm::Sustain:
        case ChordRhythm::NumRhythms:
            hits.emplace_back (start, length);
            break;

        case ChordRhythm::Half:
        {
            const double step = beatsPerBar / 2.0;
            for (double t = start; t < end - 1e-9; t += step)
                hits.emplace_back (t, std::min (step, end - t));
            break;
        }

        case ChordRhythm::Pulse8:
            for (double t = start; t < end - 1e-9; t += 0.5)
                hits.emplace_back (t, std::min (0.4, end - t));
            break;

        case ChordRhythm::Stabs:
        {
            const double gate = std::max (0.0625, perf.stabGate * 0.25);
            const int firstBar = static_cast<int> (std::floor (start / beatsPerBar));
            const int lastBar = static_cast<int> (std::ceil (end / beatsPerBar));
            for (int bar = firstBar; bar < lastBar; ++bar)
            {
                for (int step : perf.stabSteps)
                {
                    const double t = bar * beatsPerBar + std::clamp (step, 0, beatsPerBar * 4 - 1) * 0.25;
                    if (t >= start - 1e-9 && t < end - 1e-9)
                        hits.emplace_back (t, std::min (gate, end - t));
                }
            }
            std::sort (hits.begin(), hits.end());
            if (hits.empty() || hits.front().first > start + 1e-9)
                hits.insert (hits.begin(), { start, std::min (gate, length) }); // chord change always speaks
            break;
        }
    }
    return hits;
}

double applySwing (double t, double swing)
{
    // Off-beat 16ths (x.25, x.75) are pushed late by up to a 32nd note.
    const double frac = t * 4.0 - std::floor (t * 4.0 + 1e-9);
    const auto sixteenth = static_cast<long> (std::llround (t * 4.0));
    if (frac < 1e-6 && (sixteenth % 2) != 0)
        return t + std::clamp (swing, 0.0, 1.0) * 0.125;
    return t;
}
} // namespace

std::string_view chordRhythmName (ChordRhythm r) { return rhythmTable[static_cast<size_t> (r)].name; }
std::string_view chordRhythmId (ChordRhythm r)   { return rhythmTable[static_cast<size_t> (r)].id; }

std::optional<ChordRhythm> chordRhythmFromId (std::string_view id)
{
    for (size_t i = 0; i < rhythmTable.size(); ++i)
        if (rhythmTable[i].id == id)
            return static_cast<ChordRhythm> (i);
    return std::nullopt;
}

ChordPerformance ChordPerformance::fromPreset (const StylePreset& p)
{
    ChordPerformance perf;
    perf.voicing = p.voicing;
    perf.registerLow = p.registerLow;
    perf.registerHigh = p.registerHigh;
    perf.rhythm = chordRhythmFromId (p.chordRhythm).value_or (ChordRhythm::Sustain);
    perf.stabSteps = p.stabSteps;
    perf.stabGate = p.stabGate;
    perf.bpm = p.bpm;
    perf.strumMs = p.strumMs;
    perf.velocity = p.velocity;
    perf.velocityRandom = p.velocityRandom;
    perf.timingRandomMs = p.timingRandomMs;
    perf.swing = p.swing;
    return perf;
}

std::vector<theory::Voicing> voiceProgression (const Progression& prog, const ChordPerformance& perf)
{
    std::vector<theory::Voicing> voicings;
    std::optional<theory::Voicing> seam;

    // Two passes: the second starts from the last chord of the first, so chord 1 also leads
    // smoothly out of chord N and the loop seam doesn't jump.
    const int passes = prog.slots.size() > 1 ? 2 : 1;
    for (int pass = 0; pass < passes; ++pass)
    {
        voicings.clear();
        std::optional<theory::Voicing> previous = seam;

        for (const auto& slot : prog.slots)
        {
            theory::VoicingParams vp;
            vp.style = slot.voicing.value_or (perf.voicing);
            vp.rhLow = perf.registerLow;
            vp.rhHigh = perf.registerHigh;
            vp.lhLow = std::max (24, perf.registerLow - 22);
            vp.lhHigh = perf.registerLow;
            vp.inversion = slot.inversion;
            vp.octaveShift = slot.octave;

            // Octave-shifted chords shouldn't drag their neighbours' voice leading around.
            auto v = theory::voiceChord (slot.chord, vp, slot.octave == 0 && previous ? &*previous : nullptr);
            if (slot.octave == 0)
                previous = v;
            voicings.push_back (std::move (v));
        }

        if (! voicings.empty())
            seam = voicings.back();
    }
    return voicings;
}

midi::MidiClip renderChords (const Progression& prog, const ChordPerformance& perf)
{
    midi::MidiClip clip;
    clip.name = "Chords";
    clip.lengthBeats = prog.lengthBeats();

    const auto voicings = voiceProgression (prog, perf);
    const double beatsPerMs = std::max (1.0, perf.bpm) / 60000.0;
    const double h = std::clamp (perf.humanise, 0.0, 1.0);

    for (size_t i = 0; i < prog.slots.size(); ++i)
    {
        const int si = static_cast<int> (i);
        const auto& notes = voicings[i].notes;
        util::Random rng (util::deriveSeed (perf.seed, 5000 + i));

        for (const auto& [hitStart, hitLen] : slotHits (prog.slotStart (si), prog.slotLength (si), prog.beatsPerBar, perf))
        {
            const double t0 = applySwing (hitStart, perf.swing);
            const double hitJitter = rng.nextGaussianish() * perf.timingRandomMs * h * beatsPerMs * 0.5;
            const double hitAccent = rng.nextGaussianish() * perf.velocityRandom * h * 0.5;
            const double strumStep = perf.strumMs * h * beatsPerMs;

            for (size_t k = 0; k < notes.size(); ++k)
            {
                midi::Note n;
                n.pitch = notes[k];
                n.channel = perf.channel;

                double start = t0 + strumStep * static_cast<double> (k) + hitJitter
                             + rng.nextGaussianish() * perf.timingRandomMs * h * beatsPerMs * 0.25;
                // Nothing may land before the loop start; the downbeat of the loop stays exact.
                if (hitStart <= 1e-9 && k == 0)
                    start = 0.0;
                start = std::max (0.0, start);

                const double end = std::min (prog.lengthBeats(), hitStart + hitLen);
                n.start = start;
                n.length = std::max (0.03, end - start - 0.01);

                // Top voice slightly louder so the melody of the voicing reads.
                double vel = perf.velocity + hitAccent + rng.nextGaussianish() * perf.velocityRandom * h * 0.5;
                if (k + 1 == notes.size())
                    vel += 6.0;
                if (k == 0 && notes.size() > 3)
                    vel -= 4.0;
                n.velocity = std::clamp (static_cast<int> (std::lround (vel)), 1, 127);

                clip.notes.push_back (n);
            }
        }
    }

    clip.sortByTime();
    return clip;
}

} // namespace bounce::gen
