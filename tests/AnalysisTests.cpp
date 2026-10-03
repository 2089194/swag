#include <doctest/doctest.h>

#include "bounce/analysis/AudioAnalysis.h"
#include "bounce/analysis/Fft.h"

#include <cmath>
#include <map>

using namespace bounce;
using namespace bounce::analysis;

namespace
{
constexpr double pi = 3.14159265358979323846;

/** Synthetic "song": a chord per bar (harmonic tones, 3 octaves) + a kick on every beat. */
std::vector<float> synthSong (double sr, double bpm, const std::vector<theory::Chord>& chords, int repeats)
{
    const double beat = 60.0 / bpm;
    const double bar = beat * 4.0;
    const auto total = static_cast<size_t> (sr * bar * static_cast<double> (chords.size() * static_cast<size_t> (repeats)));
    std::vector<float> out (total, 0.0f);

    for (size_t i = 0; i < total; ++i)
    {
        const double t = static_cast<double> (i) / sr;
        const auto barIndex = static_cast<size_t> (t / bar);
        const auto& chord = chords[barIndex % chords.size()];
        double s = 0;
        for (int pc : chord.pitchClasses())
        {
            for (int octave : { 3, 4, 5 })
            {
                const double f = 440.0 * std::pow (2.0, (pc + 12 * (octave + 1) - 69) / 12.0);
                s += 0.05 * (std::sin (2 * pi * f * t) + 0.3 * std::sin (4 * pi * f * t));
            }
        }
        // Bass root.
        const double fr = 440.0 * std::pow (2.0, (chord.root + 36 - 69) / 12.0);
        s += 0.1 * std::sin (2 * pi * fr * t);

        // Kick: decaying 55 Hz thump on every beat.
        const double tb = std::fmod (t, beat);
        s += 0.6 * std::exp (-tb * 30.0) * std::sin (2 * pi * 55.0 * tb) + 0.2 * std::exp (-tb * 300.0);
        out[i] = static_cast<float> (s);
    }
    return out;
}
} // namespace

TEST_CASE ("fft finds a sine")
{
    Fft fft (10);
    std::vector<float> x (1024);
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = static_cast<float> (std::sin (2 * pi * 64.0 * static_cast<double> (i) / 1024.0));
    std::vector<float> mags;
    fft.magnitudes (x.data(), mags);
    const auto peak = std::max_element (mags.begin(), mags.end()) - mags.begin();
    CHECK (peak == 64);
}

TEST_CASE ("analysis: tempo, key and chords of a synthetic A minor loop")
{
    using Q = theory::ChordQuality;
    const std::vector<theory::Chord> chords { { 9, Q::Minor, {} }, { 2, Q::Minor, {} }, { 4, Q::Major, {} }, { 9, Q::Minor, {} } };
    const double sr = 44100.0, bpm = 140.0;
    const auto audio = synthSong (sr, bpm, chords, 3);

    double lastProgress = -1;
    const auto r = analyse (audio.data(), audio.size(), sr, {}, [&] (double p) { CHECK (p >= lastProgress); lastProgress = p; return true; });
    CHECK (lastProgress == doctest::Approx (1.0));

    CHECK (r.tempo.bpm == doctest::Approx (bpm).epsilon (0.002));

    // The downbeat is found from where chords change: a bar line (multiple of 4 beats).
    const double beats = r.tempo.firstBeat * bpm / 60.0;
    CHECK (std::abs (beats - 4.0 * std::round (beats / 4.0)) < 0.3);
    CHECK (std::abs (beats) < 0.5); // the song starts on the one

    // Sending the first 4 bars gives the four chords as played.
    const auto prog = progressionFromDetected (r.chords, r.keys[0].key, r.tempo.bpm, r.tempo.firstBeat, 4);
    REQUIRE (prog.slots.size() == 4);
    for (size_t i = 0; i < 4; ++i)
    {
        CHECK (prog.slots[i].chord.root == chords[i].root);
        CHECK (prog.slots[i].chord.family() == chords[i].family());
    }
    REQUIRE (r.keys.size() == 3);
    CHECK (r.keys[0].key == theory::Key { 9, theory::ScaleType::NaturalMinor });
    CHECK (r.keys[0].confidence > r.keys[1].confidence);

    // Most of the time, the detected chord matches the one being played.
    double matched = 0, total = 0;
    const double barSec = 4 * 60.0 / bpm;
    for (const auto& c : r.chords)
    {
        for (double t = c.start; t < c.end; t += 0.05)
        {
            const auto& truth = chords[static_cast<size_t> (t / barSec) % chords.size()];
            matched += (c.chord.root == truth.root && c.chord.family() == truth.family()) ? 1 : 0;
            total += 1;
        }
    }
    REQUIRE (total > 0);
    CHECK (matched / total > 0.8);
}

TEST_CASE ("analysis: cancel and empty input")
{
    std::vector<float> audio (44100 * 4, 0.0f);
    const auto cancelled = analyse (audio.data(), audio.size(), 44100.0, {}, [] (double) { return false; });
    CHECK (cancelled.keys.empty());
    const auto empty = analyse (nullptr, 0, 44100.0);
    CHECK (empty.durationSeconds == 0.0);
    const auto silent = analyse (audio.data(), audio.size(), 44100.0);
    CHECK (silent.chords.empty());
}

TEST_CASE ("speed helper")
{
    const theory::Key am { 9, theory::ScaleType::NaturalMinor };
    const auto up = speedBySemitones (am, 140.0, 3.0);
    CHECK (up.key == theory::Key { 0, theory::ScaleType::NaturalMinor });
    CHECK (up.bpm == doctest::Approx (140.0 * std::pow (2.0, 0.25)));
    CHECK (up.cents == 0);

    const auto pct = speedByPercent (am, 100.0, 25.0);
    CHECK (pct.bpm == doctest::Approx (125.0));
    CHECK (pct.semitones == doctest::Approx (3.863).epsilon (0.001));
    CHECK (pct.key.tonic == theory::wrapPc (9 + 4));
    CHECK (pct.cents == -14);
}

TEST_CASE ("detected chords become a locked progression with custom lengths")
{
    using Q = theory::ChordQuality;
    const double bpm = 120.0; // 0.5 s per beat
    std::vector<DetectedChord> det {
        { { 9, Q::Minor, {} }, 0.0, 3.0, 0.9 },   // 6 beats
        { { 2, Q::Minor, {} }, 3.0, 4.0, 0.9 },   // 2 beats
        { { 2, Q::Minor, {} }, 4.0, 4.05, 0.9 },  // blip, merged
        { { 5, Q::Major, {} }, 4.05, 4.3, 0.9 },  // under a beat: dropped
        { { 4, Q::Major, {} }, 4.3, 8.0, 0.9 },   // rest of the loop
    };
    const auto prog = progressionFromDetected (det, { 9, theory::ScaleType::NaturalMinor }, bpm, 0.0, 4);
    REQUIRE (prog.slots.size() == 3);
    REQUIRE (prog.hasCustomLengths());
    CHECK (prog.slotLength (0) == 6.0);
    CHECK (prog.slotLength (1) == 2.0);
    CHECK (prog.slotLength (2) == 8.0);
    for (const auto& s : prog.slots)
        CHECK (s.locked);
    CHECK (prog.slots[2].function.symbol == "V");
    CHECK (prog.slots[2].function.borrowed);
}
