#pragma once

#include "bounce/gen/Progression.h"
#include "bounce/theory/Chord.h"
#include "bounce/theory/Scale.h"

#include <array>
#include <cstddef>
#include <functional>
#include <vector>

namespace bounce::analysis
{

struct TempoResult
{
    double bpm = 0.0;
    double confidence = 0.0;   // 0..1
    double firstBeat = 0.0;    // seconds: the first downbeat (bar start), estimated from chord changes
    double halfTime = 0.0;     // bpm / 2
    double doubleTime = 0.0;   // bpm * 2
};

struct KeyCandidate
{
    theory::Key key;
    double confidence = 0.0; // 0..1, the top 3 sum to <= 1
    double correlation = 0.0;
};

struct DetectedChord
{
    theory::Chord chord;
    double start = 0.0; // seconds
    double end = 0.0;
    double confidence = 0.0;

    bool operator== (const DetectedChord&) const = default;
};

struct AnalysisResult
{
    double durationSeconds = 0.0;
    TempoResult tempo;
    std::vector<KeyCandidate> keys; // best first, up to 3
    std::vector<DetectedChord> chords;
    std::array<double, 12> chroma {}; // whole-track profile, normalised
};

struct AnalysisOptions
{
    double minBpm = 60.0;
    double maxBpm = 200.0;
    double preferredBpm = 140.0; // tempo prior centre (half/double ambiguity)
    bool sevenths = true;        // detect maj7 / m7 / 7 as well as triads
};

/** Progress callback: 0..1. Return false to cancel (analyse() then returns an empty result). */
using ProgressCallback = std::function<bool (double)>;

/** Offline analysis of a mono signal: tempo (spectral-flux onsets + autocorrelation with a
    tempo prior), key (chromagram vs Krumhansl-Kessler and Temperley profiles, top 3 with
    confidence) and a beat-synchronous chord lane (chroma templates + Viterbi smoothing).
    Never call this on the audio thread. */
AnalysisResult analyse (const float* mono, size_t numSamples, double sampleRate,
                        const AnalysisOptions& options = {}, const ProgressCallback& progress = {});

//==============================================================================
/** Flip helper: what key and tempo a sample ends up in when sped up / slowed down. */
struct SpeedChange
{
    double semitones = 0.0; // pitch shift that comes with the speed change (varispeed)
    double ratio = 1.0;     // playback speed ratio
    double bpm = 0.0;
    theory::Key key;
    int cents = 0;          // leftover detune after rounding to the nearest key
};

/** Varispeed by a number of semitones (e.g. +3 = "sped up" a minor third). */
SpeedChange speedBySemitones (const theory::Key& key, double bpm, double semitones);

/** Varispeed by a percentage (e.g. +25 = 125% speed). */
SpeedChange speedByPercent (const theory::Key& key, double bpm, double percent);

/** Turns detected chords into a progression: chords are snapped to a half-beat grid at the
    given tempo, starting at `startSeconds`, filling `bars` bars. Chords shorter than a beat
    inside the window are dropped (their time goes to the neighbours). All slots come back locked so Generate keeps them until you unlock some. */
gen::Progression progressionFromDetected (const std::vector<DetectedChord>& chords, const theory::Key& key,
                                          double bpm, double startSeconds, int bars, int maxChords = 8);

} // namespace bounce::analysis
