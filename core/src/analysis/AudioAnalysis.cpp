#include "bounce/analysis/AudioAnalysis.h"

#include "bounce/analysis/Fft.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace bounce::analysis
{

namespace
{
constexpr double kPi = 3.14159265358979323846;

// Key profiles: Krumhansl-Kessler (probe-tone ratings) and Temperley (Kostka-Payne corpus).
constexpr std::array<double, 12> kkMajor { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
constexpr std::array<double, 12> kkMinor { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
constexpr std::array<double, 12> tpMajor { 0.748, 0.060, 0.488, 0.082, 0.670, 0.460, 0.096, 0.715, 0.104, 0.366, 0.057, 0.400 };
constexpr std::array<double, 12> tpMinor { 0.712, 0.084, 0.474, 0.618, 0.049, 0.460, 0.105, 0.747, 0.404, 0.067, 0.133, 0.330 };

double pearson (const std::array<double, 12>& a, const std::array<double, 12>& profile, int rotation)
{
    double ma = 0, mb = 0;
    for (int i = 0; i < 12; ++i)
    {
        ma += a[static_cast<size_t> (i)];
        mb += profile[static_cast<size_t> (i)];
    }
    ma /= 12;
    mb /= 12;
    double num = 0, da = 0, db = 0;
    for (int i = 0; i < 12; ++i)
    {
        const double x = a[static_cast<size_t> ((i + rotation) % 12)] - ma;
        const double y = profile[static_cast<size_t> (i)] - mb;
        num += x * y;
        da += x * x;
        db += y * y;
    }
    return da > 0 && db > 0 ? num / std::sqrt (da * db) : 0.0;
}

/** Low-pass + decimate to roughly 22 kHz. */
std::vector<float> downsample (const float* x, size_t n, double sr, double& outRate)
{
    const int factor = std::max (1, static_cast<int> (std::floor (sr / 22050.0 + 1e-9)));
    outRate = sr / factor;
    if (factor == 1)
        return { x, x + n };

    const int taps = 63;
    const double fc = 0.45 / factor;
    std::vector<double> h (static_cast<size_t> (taps));
    double sum = 0;
    for (int i = 0; i < taps; ++i)
    {
        const double m = i - (taps - 1) / 2.0;
        const double sinc = std::abs (m) < 1e-9 ? 2 * fc : std::sin (2 * kPi * fc * m) / (kPi * m);
        const double w = 0.42 - 0.5 * std::cos (2 * kPi * i / (taps - 1)) + 0.08 * std::cos (4 * kPi * i / (taps - 1));
        h[static_cast<size_t> (i)] = sinc * w;
        sum += sinc * w;
    }
    for (auto& v : h)
        v /= sum;

    std::vector<float> out;
    out.reserve (n / static_cast<size_t> (factor) + 1);
    for (size_t i = 0; i < n; i += static_cast<size_t> (factor))
    {
        double acc = 0;
        for (int k = 0; k < taps; ++k)
        {
            const long idx = static_cast<long> (i) - k + taps / 2;
            if (idx >= 0 && idx < static_cast<long> (n))
                acc += h[static_cast<size_t> (k)] * x[idx];
        }
        out.push_back (static_cast<float> (acc));
    }
    return out;
}

double interp (const std::vector<float>& v, double idx)
{
    const auto i = static_cast<size_t> (idx);
    if (i + 1 >= v.size())
        return 0.0;
    const double f = idx - static_cast<double> (i);
    return v[i] * (1.0 - f) + v[i + 1] * f;
}

struct ChordTemplate
{
    theory::Chord chord;
    std::array<double, 12> v {};
};

std::vector<ChordTemplate> chordTemplates (bool sevenths)
{
    using Q = theory::ChordQuality;
    std::vector<std::pair<Q, std::vector<std::pair<int, double>>>> shapes {
        { Q::Major, { { 0, 1.0 }, { 4, 0.9 }, { 7, 0.8 } } },
        { Q::Minor, { { 0, 1.0 }, { 3, 0.9 }, { 7, 0.8 } } },
    };
    if (sevenths)
    {
        shapes.push_back ({ Q::Major7,    { { 0, 1.0 }, { 4, 0.9 }, { 7, 0.8 }, { 11, 0.7 } } });
        shapes.push_back ({ Q::Minor7,    { { 0, 1.0 }, { 3, 0.9 }, { 7, 0.8 }, { 10, 0.7 } } });
        shapes.push_back ({ Q::Dominant7, { { 0, 1.0 }, { 4, 0.9 }, { 7, 0.8 }, { 10, 0.7 } } });
    }

    std::vector<ChordTemplate> out;
    for (const auto& [q, tones] : shapes)
        for (int root = 0; root < 12; ++root)
        {
            ChordTemplate t;
            t.chord = { root, q, std::nullopt };
            double norm = 0;
            for (const auto& [iv, w] : tones)
            {
                t.v[static_cast<size_t> ((root + iv) % 12)] = w;
                norm += w * w;
            }
            for (auto& x : t.v)
                x /= std::sqrt (norm);
            out.push_back (t);
        }
    return out;
}

double cosine (const std::array<double, 12>& a, const std::array<double, 12>& b)
{
    double num = 0, na = 0, nb = 0;
    for (size_t i = 0; i < 12; ++i)
    {
        num += a[i] * b[i];
        na += a[i] * a[i];
        nb += b[i] * b[i];
    }
    return na > 0 && nb > 0 ? num / std::sqrt (na * nb) : 0.0;
}
} // namespace

//==============================================================================
AnalysisResult analyse (const float* mono, size_t numSamples, double sampleRate, const AnalysisOptions& opt,
                        const ProgressCallback& progress)
{
    AnalysisResult result;
    if (mono == nullptr || numSamples == 0 || sampleRate <= 0)
        return result;
    result.durationSeconds = static_cast<double> (numSamples) / sampleRate;

    auto report = [&progress] (double v) { return ! progress || progress (v); };

    double sr = sampleRate;
    const auto x = downsample (mono, numSamples, sampleRate, sr);
    if (! report (0.1))
        return {};

    // --- Onset strength envelope (log-magnitude spectral flux).
    const int onsetOrder = 10, hop = 256;
    const Fft onsetFft (onsetOrder);
    const int frame = onsetFft.size();
    std::vector<float> env;
    {
        std::vector<float> mags, prev;
        const size_t maxBin = static_cast<size_t> (std::min (frame / 2.0, 8000.0 * frame / sr));
        for (size_t pos = 0; pos + static_cast<size_t> (frame) <= x.size(); pos += hop)
        {
            onsetFft.magnitudes (&x[pos], mags);
            for (auto& m : mags)
                m = std::log1p (100.0f * m);
            double flux = 0;
            if (! prev.empty())
                for (size_t k = 1; k < maxBin; ++k)
                    flux += std::max (0.0f, mags[k] - prev[k]);
            env.push_back (static_cast<float> (flux));
            prev.swap (mags);
        }

        // Remove the slowly varying part so only onsets remain.
        const double frameRate = sr / hop;
        const int w = std::max (1, static_cast<int> (frameRate * 0.25));
        std::vector<float> detrended (env.size());
        double run = 0;
        for (size_t i = 0; i < env.size(); ++i)
        {
            run += env[i];
            if (i >= static_cast<size_t> (2 * w + 1))
                run -= env[i - static_cast<size_t> (2 * w + 1)];
            const auto count = std::min<size_t> (i + 1, static_cast<size_t> (2 * w + 1));
            const double mean = run / static_cast<double> (count);
            const size_t centre = i >= static_cast<size_t> (w) ? i - static_cast<size_t> (w) : 0;
            detrended[centre] = static_cast<float> (std::max (0.0, env[centre] - mean));
        }
        env.swap (detrended);
    }
    if (! report (0.35))
        return {};

    // --- Tempo: autocorrelation at fractional lags (and their multiples), times a log-normal prior.
    const double frameRate = sr / hop;
    {
        const size_t limit = std::min (env.size(), static_cast<size_t> (frameRate * 120.0));
        std::vector<float> e (env.begin(), env.begin() + static_cast<long> (limit));
        const double mean = e.empty() ? 0.0 : std::accumulate (e.begin(), e.end(), 0.0) / static_cast<double> (e.size());
        for (auto& v : e)
            v = static_cast<float> (v - mean);

        auto acf = [&e] (double lag)
        {
            if (lag >= static_cast<double> (e.size()) - 2)
                return 0.0;
            double s = 0;
            const size_t count = e.size() - static_cast<size_t> (std::ceil (lag)) - 1;
            for (size_t t = 0; t < count; ++t)
                s += e[t] * interp (e, static_cast<double> (t) + lag);
            return s / static_cast<double> (count);
        };

        double bestScore = -1e300, bestBpm = 0;
        std::vector<double> scores;
        for (double bpm = opt.minBpm; bpm <= opt.maxBpm + 1e-9; bpm += 0.1)
        {
            const double lag = 60.0 * frameRate / bpm;
            const double raw = acf (lag) + 0.5 * acf (2 * lag) + 0.25 * acf (4 * lag);
            const double prior = std::exp (-0.5 * std::pow (std::log2 (bpm / opt.preferredBpm) / 0.9, 2.0));
            const double s = raw * prior;
            scores.push_back (s);
            if (s > bestScore)
            {
                bestScore = s;
                bestBpm = bpm;
            }
        }

        // Refine on a 0.01 BPM grid with longer multiples (sharper peak, no prior).
        if (bestBpm > 0)
        {
            double refined = bestBpm, refinedScore = -1e300;
            for (double bpm = bestBpm - 0.25; bpm <= bestBpm + 0.25 + 1e-9; bpm += 0.01)
            {
                const double lag = 60.0 * frameRate / bpm;
                const double sc = acf (lag) + acf (2 * lag) + acf (4 * lag) + acf (8 * lag);
                if (sc > refinedScore)
                {
                    refinedScore = sc;
                    refined = bpm;
                }
            }
            bestBpm = refined;
        }

        std::vector<double> sorted = scores;
        std::sort (sorted.begin(), sorted.end());
        const double median = sorted.empty() ? 0.0 : sorted[sorted.size() / 2];
        result.tempo.bpm = std::round (bestBpm * 100.0) / 100.0;
        result.tempo.confidence = bestScore > 0 ? std::clamp ((bestScore - median) / bestScore, 0.0, 1.0) : 0.0;
        result.tempo.halfTime = result.tempo.bpm / 2.0;
        result.tempo.doubleTime = result.tempo.bpm * 2.0;

        // Beat phase: the offset whose pulse train collects the most onset energy.
        if (bestBpm > 0)
        {
            const double period = 60.0 * frameRate / bestBpm;
            double bestPhase = 0, bestSum = -1;
            for (double phase = 0; phase < period; phase += 0.5)
            {
                double s = 0;
                for (double t = phase; t < static_cast<double> (env.size()) - 1; t += period)
                    s += interp (env, t);
                if (s > bestSum)
                {
                    bestSum = s;
                    bestPhase = phase;
                }
            }
            result.tempo.firstBeat = (bestPhase * hop + frame / 2.0) / sr;
        }
    }
    if (! report (0.55))
        return {};

    // --- Chromagram.
    const int chromaOrder = 13, chromaHop = 2048;
    const Fft chromaFft (chromaOrder);
    const int chromaFrame = chromaFft.size();
    std::vector<std::array<double, 12>> chroma;
    std::vector<double> chromaTime, chromaEnergy;
    {
        std::vector<float> mags;
        std::vector<int> binPc (static_cast<size_t> (chromaFrame / 2 + 1), -1);
        std::vector<double> binW (binPc.size(), 0.0);
        for (size_t k = 1; k < binPc.size(); ++k)
        {
            const double f = static_cast<double> (k) * sr / chromaFrame;
            if (f < 60.0 || f > 4200.0)
                continue;
            const double midi = 69.0 + 12.0 * std::log2 (f / 440.0);
            const double nearest = std::round (midi);
            const double w = 1.0 - 2.0 * std::abs (midi - nearest); // triangular: favour in-tune bins
            if (w <= 0)
                continue;
            binPc[k] = static_cast<int> (static_cast<long> (nearest) % 12);
            binW[k] = w;
        }

        const size_t frames = x.size() >= static_cast<size_t> (chromaFrame) ? (x.size() - static_cast<size_t> (chromaFrame)) / chromaHop + 1 : 0;
        for (size_t fi = 0; fi < frames; ++fi)
        {
            chromaFft.magnitudes (&x[fi * chromaHop], mags);
            std::array<double, 12> c {};
            double energy = 0;
            for (size_t k = 0; k < binPc.size(); ++k)
                if (binPc[k] >= 0)
                {
                    c[static_cast<size_t> (binPc[k])] += mags[k] * binW[k];
                    energy += mags[k] * mags[k];
                }
            chroma.push_back (c);
            chromaTime.push_back ((static_cast<double> (fi * chromaHop) + chromaFrame / 2.0) / sr);
            chromaEnergy.push_back (energy);
            if (fi % 64 == 0 && ! report (0.55 + 0.3 * static_cast<double> (fi) / static_cast<double> (frames)))
                return {};
        }
    }

    const double maxEnergy = chromaEnergy.empty() ? 0.0 : *std::max_element (chromaEnergy.begin(), chromaEnergy.end());
    const double silence = maxEnergy * 1e-4;

    // --- Key.
    {
        std::array<double, 12> total {};
        for (size_t i = 0; i < chroma.size(); ++i)
        {
            if (chromaEnergy[i] <= silence)
                continue;
            double s = 0;
            for (double v : chroma[i])
                s += v;
            if (s <= 0)
                continue;
            for (size_t k = 0; k < 12; ++k)
                total[k] += chroma[i][k] / s;
        }
        const double sum = std::accumulate (total.begin(), total.end(), 0.0);
        if (sum > 0)
            for (auto& v : total)
                v /= sum;
        result.chroma = total;

        std::vector<KeyCandidate> all;
        for (int tonic = 0; tonic < 12; ++tonic)
        {
            const double maj = 0.5 * (pearson (total, kkMajor, tonic) + pearson (total, tpMajor, tonic));
            const double min = 0.5 * (pearson (total, kkMinor, tonic) + pearson (total, tpMinor, tonic));
            all.push_back ({ { tonic, theory::ScaleType::Major }, 0.0, maj });
            all.push_back ({ { tonic, theory::ScaleType::NaturalMinor }, 0.0, min });
        }
        double z = 0;
        for (auto& k : all)
            z += std::exp (k.correlation * 12.0);
        for (auto& k : all)
            k.confidence = z > 0 ? std::exp (k.correlation * 12.0) / z : 0.0;
        std::sort (all.begin(), all.end(), [] (const auto& a, const auto& b) { return a.correlation > b.correlation; });
        if (sum > 0)
            result.keys.assign (all.begin(), all.begin() + 3);
    }

    // --- Chords: beat-synchronous chroma -> templates -> Viterbi.
    {
        std::vector<double> bounds;
        const double dur = result.durationSeconds;
        if (result.tempo.bpm > 0 && result.tempo.confidence > 0.1)
        {
            const double beat = 60.0 / result.tempo.bpm;
            double t = std::fmod (result.tempo.firstBeat, beat);
            bounds.push_back (0.0);
            for (; t < dur; t += beat)
                if (t > 1e-3)
                    bounds.push_back (t);
        }
        else
            for (double t = 0; t < dur; t += 0.5)
                bounds.push_back (t);
        bounds.push_back (dur);

        const auto templates = chordTemplates (opt.sevenths);
        const auto nStates = templates.size() + 1; // + "no chord"
        const size_t nSeg = bounds.size() - 1;
        std::vector<std::vector<double>> emit (nSeg, std::vector<double> (nStates, 0.0));
        std::vector<std::vector<double>> rawScore (nSeg, std::vector<double> (nStates, 0.0));

        const auto key = result.keys.empty() ? theory::Key {} : result.keys.front().key;
        size_t ci = 0;
        for (size_t s = 0; s < nSeg; ++s)
        {
            std::array<double, 12> seg {};
            double energy = 0;
            int count = 0;
            while (ci < chroma.size() && chromaTime[ci] < bounds[s])
                ++ci;
            for (size_t k = ci; k < chroma.size() && chromaTime[k] < bounds[s + 1]; ++k)
            {
                for (size_t p = 0; p < 12; ++p)
                    seg[p] += chroma[k][p];
                energy += chromaEnergy[k];
                ++count;
            }
            if (count == 0)
            {
                // Segment shorter than a chroma hop: borrow the nearest frame.
                const size_t k = std::min (ci, chroma.size() - 1);
                if (! chroma.empty())
                {
                    seg = chroma[k];
                    energy = chromaEnergy[k];
                    count = 1;
                }
            }
            const bool quiet = count == 0 || energy / std::max (1, count) <= silence;

            for (size_t t = 0; t < templates.size(); ++t)
            {
                double sc = cosine (seg, templates[t].v);
                if (theory::qualityToneCount (templates[t].chord.quality) > 3)
                    sc -= 0.025; // triads unless the 7th is clearly there
                if (templates[t].chord.isDiatonicTo (key.heptatonicParent()))
                    sc += 0.04;
                rawScore[s][t] = sc;
                emit[s][t] = quiet ? -5.0 : sc * 14.0;
            }
            emit[s][templates.size()] = quiet ? 0.0 : 0.45 * 14.0;
        }

        // Viterbi with a constant switching penalty.
        const double switchPenalty = 2.0;
        std::vector<std::vector<int>> back (nSeg, std::vector<int> (nStates, 0));
        std::vector<double> score (emit.empty() ? 0 : nStates);
        if (! emit.empty())
            score = emit[0];
        for (size_t s = 1; s < nSeg; ++s)
        {
            const auto bestPrev = static_cast<int> (std::max_element (score.begin(), score.end()) - score.begin());
            std::vector<double> next (nStates);
            for (size_t st = 0; st < nStates; ++st)
            {
                const double stay = score[st];
                const double sw = score[static_cast<size_t> (bestPrev)] - switchPenalty;
                if (stay >= sw)
                {
                    next[st] = stay + emit[s][st];
                    back[s][st] = static_cast<int> (st);
                }
                else
                {
                    next[st] = sw + emit[s][st];
                    back[s][st] = bestPrev;
                }
            }
            score.swap (next);
        }

        std::vector<int> path (nSeg, 0);
        if (nSeg > 0)
        {
            path[nSeg - 1] = static_cast<int> (std::max_element (score.begin(), score.end()) - score.begin());
            for (size_t s = nSeg - 1; s > 0; --s)
                path[s - 1] = back[s][static_cast<size_t> (path[s])];
        }

        for (size_t s = 0; s < nSeg;)
        {
            size_t e = s;
            double conf = 0;
            while (e < nSeg && path[e] == path[s])
            {
                conf += path[s] < static_cast<int> (templates.size()) ? rawScore[e][static_cast<size_t> (path[s])] : 0.0;
                ++e;
            }
            if (path[s] < static_cast<int> (templates.size()))
                result.chords.push_back ({ templates[static_cast<size_t> (path[s])].chord, bounds[s], bounds[e],
                                           std::clamp (conf / static_cast<double> (e - s), 0.0, 1.0) });
            s = e;
        }
    }

    // --- Downbeat: of the four beat phases, pick the one where most chord changes start a bar.
    if (result.tempo.bpm > 0.0 && result.chords.size() > 1)
    {
        const double beat = 60.0 / result.tempo.bpm;
        int bestPhase = 0;
        double bestHits = -1.0;
        for (int phase = 0; phase < 4; ++phase)
        {
            const double origin = result.tempo.firstBeat + phase * beat;
            double hits = 0.0;
            for (size_t i = 1; i < result.chords.size(); ++i)
            {
                const double bars = (result.chords[i].start - origin) / (4.0 * beat);
                const double dist = std::abs (bars - std::round (bars)) * 4.0; // in beats
                hits += dist < 0.5 ? 1.0 : 0.0;
            }
            if (hits > bestHits)
            {
                bestHits = hits;
                bestPhase = phase;
            }
        }
        // Earliest downbeat, allowed to sit up to half a beat before 0 so a song that starts on
        // the one doesn't get wrapped to bar 2.
        double first = std::fmod (result.tempo.firstBeat + bestPhase * beat, 4.0 * beat);
        if (first > 4.0 * beat - 0.5 * beat)
            first -= 4.0 * beat;
        result.tempo.firstBeat = first;
    }

    report (1.0);
    return result;
}

//==============================================================================
SpeedChange speedBySemitones (const theory::Key& key, double bpm, double semitones)
{
    SpeedChange c;
    c.semitones = semitones;
    c.ratio = std::pow (2.0, semitones / 12.0);
    c.bpm = bpm * c.ratio;
    const double rounded = std::round (semitones);
    c.key = key.transposed (static_cast<int> (rounded));
    c.cents = static_cast<int> (std::lround ((semitones - rounded) * 100.0));
    return c;
}

SpeedChange speedByPercent (const theory::Key& key, double bpm, double percent)
{
    const double ratio = std::max (0.05, 1.0 + percent / 100.0);
    return speedBySemitones (key, bpm, 12.0 * std::log2 (ratio));
}

gen::Progression progressionFromDetected (const std::vector<DetectedChord>& chords, const theory::Key& key,
                                          double bpm, double startSeconds, int bars, int maxChords)
{
    gen::Progression prog;
    prog.key = key;
    prog.bars = std::clamp (bars, 1, 16);
    const double len = prog.lengthBeats();

    struct Span
    {
        theory::Chord chord;
        double a, b;
    };
    std::vector<Span> spans;
    for (const auto& c : chords)
    {
        double a = std::round ((c.start - startSeconds) * bpm / 60.0 * 2.0) / 2.0;
        double b = std::round ((c.end - startSeconds) * bpm / 60.0 * 2.0) / 2.0;
        a = std::clamp (a, 0.0, len);
        b = std::clamp (b, 0.0, len);
        // Ignore slivers: anything under a beat (tempo drift / a chord bleeding over the edge).
        if (b - a < 1.0)
            continue;
        if (! spans.empty() && spans.back().chord == c.chord)
            spans.back().b = b;
        else
            spans.push_back ({ c.chord, a, b });
    }

    if (spans.empty())
    {
        spans.push_back ({ { key.tonic, theory::isMinorMode (key.scale) ? theory::ChordQuality::Minor : theory::ChordQuality::Major, std::nullopt }, 0.0, len });
    }

    // Too many chords: absorb the shortest into its longer neighbour.
    while (static_cast<int> (spans.size()) > std::max (1, maxChords))
    {
        size_t shortest = 0;
        for (size_t i = 1; i < spans.size(); ++i)
            if (spans[i].b - spans[i].a < spans[shortest].b - spans[shortest].a)
                shortest = i;
        if (shortest == 0)
            spans[1].a = spans[0].a;
        else
            spans[shortest - 1].b = spans[shortest].b;
        spans.erase (spans.begin() + static_cast<long> (shortest));
    }

    // Close gaps so the loop is fully covered and starts on the bar.
    spans.front().a = 0.0;
    for (size_t i = 1; i < spans.size(); ++i)
        spans[i].a = spans[i - 1].b;
    spans.back().b = len;

    for (const auto& s : spans)
    {
        gen::ChordSlot slot;
        slot.chord = s.chord;
        slot.function = gen::makeHarmonyFunction (s.chord.root - key.tonic, s.chord.family());
        slot.function.borrowed = ! gen::isDiatonicFunction (slot.function, key);
        slot.locked = true;
        prog.slots.push_back (slot);
        prog.customLengths.push_back (s.b - s.a);
    }
    if (prog.slots.size() == 1)
        prog.customLengths.clear();
    return prog;
}

} // namespace bounce::analysis
