#include "Engine/DrumSampler.h"

#include <cmath>

namespace bounce
{

using gen::DrumLane;

namespace
{
constexpr double twoPi = juce::MathConstants<double>::twoPi;

struct Noise
{
    uint32_t s = 0x1234567u;
    float next()
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return static_cast<float> (s) / 2147483648.0f - 1.0f;
    }
};

/** Simple RBJ biquad for shaping the synthesised kit offline. */
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    static Biquad make (double sr, double freq, double q, int type) // 0 LP, 1 HP, 2 BP
    {
        const double w = twoPi * freq / sr, c = std::cos (w), s = std::sin (w), alpha = s / (2 * q);
        Biquad f;
        double a0 = 1 + alpha;
        if (type == 0)      { f.b0 = (1 - c) / 2; f.b1 = 1 - c;    f.b2 = (1 - c) / 2; }
        else if (type == 1) { f.b0 = (1 + c) / 2; f.b1 = -(1 + c); f.b2 = (1 + c) / 2; }
        else                { f.b0 = alpha;       f.b1 = 0;        f.b2 = -alpha; }
        f.a1 = -2 * c;
        f.a2 = 1 - alpha;
        f.b0 /= a0; f.b1 /= a0; f.b2 /= a0; f.a1 /= a0; f.a2 /= a0;
        return f;
    }

    float process (float x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return static_cast<float> (y);
    }
};

void normalise (std::vector<float>& d, float peak = 0.9f)
{
    float m = 0.0f;
    for (float v : d)
        m = std::max (m, std::abs (v));
    if (m > 0.0f)
        for (auto& v : d)
            v *= peak / m;
}
} // namespace

std::unique_ptr<DrumSample> DrumKit::synthesise (DrumLane lane, double sr)
{
    auto s = std::make_unique<DrumSample>();
    s->sampleRate = sr;
    Noise noise;
    auto len = [sr] (double seconds) { return static_cast<size_t> (seconds * sr); };

    switch (lane)
    {
        case DrumLane::Kick:
        {
            s->name = "Bounce Kick";
            s->data.resize (len (0.9));
            double phase = 0;
            for (size_t i = 0; i < s->data.size(); ++i)
            {
                const double t = static_cast<double> (i) / sr;
                const double f = 48.0 + 110.0 * std::exp (-t * 28.0);
                phase += f / sr;
                double v = std::sin (twoPi * phase) * std::exp (-t * 4.2);
                v += 0.35 * noise.next() * std::exp (-t * 900.0); // click
                s->data[i] = static_cast<float> (std::tanh (v * 1.6));
            }
            break;
        }
        case DrumLane::Snare:
        {
            s->name = "Bounce Clap";
            s->data.resize (len (0.45));
            auto bp = Biquad::make (sr, 1250.0, 0.9, 2);
            auto hp = Biquad::make (sr, 600.0, 0.7, 1);
            for (size_t i = 0; i < s->data.size(); ++i)
            {
                const double t = static_cast<double> (i) / sr;
                double e = std::exp (-t * 16.0);
                for (double burst : { 0.0, 0.011, 0.022 }) // the three hands of a clap
                    if (t >= burst && t < burst + 0.009)
                        e = std::max (e, std::exp (-(t - burst) * 250.0));
                s->data[i] = hp.process (bp.process (noise.next() * static_cast<float> (e)) * 3.0f);
            }
            break;
        }
        case DrumLane::ClosedHat:
        case DrumLane::OpenHat:
        {
            const bool open = lane == DrumLane::OpenHat;
            s->name = open ? "Bounce Open Hat" : "Bounce Hat";
            s->data.resize (len (open ? 0.7 : 0.14));
            // TR-808 style: six detuned square waves + noise through a high-pass.
            const double freqs[] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
            auto hp1 = Biquad::make (sr, 7000.0, 0.8, 1);
            auto hp2 = Biquad::make (sr, 7000.0, 0.8, 1);
            for (size_t i = 0; i < s->data.size(); ++i)
            {
                const double t = static_cast<double> (i) / sr;
                double metal = 0;
                for (double f : freqs)
                    metal += std::sin (twoPi * f * 2.6 * t) > 0 ? 1.0 : -1.0;
                const double e = std::exp (-t * (open ? 6.5 : 55.0));
                const auto x = static_cast<float> ((metal / 6.0 * 0.6 + noise.next() * 0.5) * e);
                s->data[i] = hp2.process (hp1.process (x));
            }
            break;
        }
        case DrumLane::Perc:
        {
            s->name = "Bounce Conga";
            s->data.resize (len (0.35));
            double phase = 0;
            for (size_t i = 0; i < s->data.size(); ++i)
            {
                const double t = static_cast<double> (i) / sr;
                phase += (290.0 + 60.0 * std::exp (-t * 40.0)) / sr;
                s->data[i] = static_cast<float> (std::sin (twoPi * phase) * std::exp (-t * 13.0) + 0.1 * noise.next() * std::exp (-t * 200.0));
            }
            break;
        }
        case DrumLane::Rim:
        {
            s->name = "Bounce Rim";
            s->data.resize (len (0.09));
            auto bp = Biquad::make (sr, 2200.0, 3.0, 2);
            for (size_t i = 0; i < s->data.size(); ++i)
            {
                const double t = static_cast<double> (i) / sr;
                const double tone = std::sin (twoPi * 1700.0 * t) * 0.6 + std::sin (twoPi * 480.0 * t) * 0.4;
                s->data[i] = static_cast<float> (tone * std::exp (-t * 70.0)) + bp.process (noise.next() * static_cast<float> (std::exp (-t * 150.0)));
            }
            break;
        }
        case DrumLane::FX:
        case DrumLane::NumLanes:
        {
            s->name = "Bounce Crash";
            s->data.resize (len (2.5));
            auto hp = Biquad::make (sr, 3500.0, 0.7, 1);
            auto lp = Biquad::make (sr, 12000.0, 0.7, 0);
            for (size_t i = 0; i < s->data.size(); ++i)
            {
                const double t = static_cast<double> (i) / sr;
                const double e = std::min (1.0, t / 0.004) * std::exp (-t * 1.6);
                s->data[i] = lp.process (hp.process (noise.next() * static_cast<float> (e)));
            }
            break;
        }
    }

    normalise (s->data);
    // Short fade-out so nothing ends with a click.
    const size_t fade = std::min<size_t> (s->data.size(), static_cast<size_t> (sr * 0.005));
    for (size_t i = 0; i < fade; ++i)
        s->data[s->data.size() - 1 - i] *= static_cast<float> (i) / static_cast<float> (fade);
    return s;
}

//==============================================================================
DrumSampler::DrumSampler()
{
    for (int l = 0; l < gen::numDrumLanes; ++l)
    {
        builtIn[static_cast<size_t> (l)] = DrumKit::synthesise (static_cast<DrumLane> (l));
        active[static_cast<size_t> (l)].store (builtIn[static_cast<size_t> (l)].get());
    }
}

DrumSampler::~DrumSampler() = default;

void DrumSampler::prepare (double sampleRate)
{
    sr = sampleRate;
    reset();
}

void DrumSampler::reset()
{
    for (auto& v : voices)
        v.sample = nullptr;
}

void DrumSampler::setSample (DrumLane lane, std::unique_ptr<DrumSample> sample)
{
    const auto i = static_cast<size_t> (lane);
    const DrumSample* next = sample != nullptr ? sample.get() : builtIn[i].get();
    active[i].store (next);
    if (custom[i] != nullptr)
        retired.push_back ({ std::move (custom[i]), juce::Time::getMillisecondCounter() });
    custom[i] = std::move (sample);
}

juce::String DrumSampler::getSampleName (DrumLane lane) const
{
    const auto* s = active[static_cast<size_t> (lane)].load();
    return s != nullptr ? s->name : juce::String();
}

bool DrumSampler::isCustom (DrumLane lane) const
{
    return custom[static_cast<size_t> (lane)] != nullptr;
}

void DrumSampler::collectGarbage()
{
    // A voice can keep reading a retired sample until it finishes; 30 s covers any one-shot.
    const auto now = juce::Time::getMillisecondCounter();
    retired.erase (std::remove_if (retired.begin(), retired.end(), [now] (const Retired& r) { return now - r.time > 30000; }),
                   retired.end());
}

void DrumSampler::trigger (int note, float velocity)
{
    DrumLane lane;
    int pitch = 0;
    gen::decodeInternalDrumNote (note, lane, pitch);
    const int laneIndex = static_cast<int> (lane);
    const auto* sample = active[static_cast<size_t> (laneIndex)].load (std::memory_order_acquire);
    if (sample == nullptr || sample->data.empty())
        return;

    // Closed hat chokes the open hat; a lane re-trigger chokes itself for hats.
    if (lane == DrumLane::ClosedHat || lane == DrumLane::OpenHat)
        for (auto& v : voices)
            if (v.sample != nullptr && v.lane == static_cast<int> (DrumLane::OpenHat))
                v.choking = true;

    Voice* target = nullptr;
    for (auto& v : voices)
        if (v.sample == nullptr)
        {
            target = &v;
            break;
        }
    if (target == nullptr)
    {
        target = &voices[0];
        for (auto& v : voices)
            if (v.pos / static_cast<double> (v.sample->data.size()) > target->pos / static_cast<double> (target->sample->data.size()))
                target = &v;
    }

    target->sample = sample;
    target->lane = laneIndex;
    target->pos = 0.0;
    target->rate = sample->sampleRate / sr * std::pow (2.0, pitch / 12.0);
    target->gain = std::pow (velocity, 1.3f) * 0.8f;
    target->fade = 1.0f;
    target->choking = false;
}

void DrumSampler::render (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi)
{
    const int numSamples = buffer.getNumSamples();
    auto* l = buffer.getWritePointer (0);
    auto* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    const float chokeStep = static_cast<float> (1.0 / (0.004 * sr));

    auto renderSpan = [&] (int start, int num)
    {
        for (auto& v : voices)
        {
            if (v.sample == nullptr)
                continue;
            const auto& d = v.sample->data;
            const double size = static_cast<double> (d.size());
            for (int i = start; i < start + num; ++i)
            {
                if (v.pos >= size - 1.0 || v.fade <= 0.0f)
                {
                    v.sample = nullptr;
                    break;
                }
                const auto idx = static_cast<size_t> (v.pos);
                const auto frac = static_cast<float> (v.pos - static_cast<double> (idx));
                const float s = (d[idx] + (d[idx + 1] - d[idx]) * frac) * v.gain * v.fade;
                l[i] += s;
                if (r != nullptr)
                    r[i] += s;
                v.pos += v.rate;
                if (v.choking)
                    v.fade -= chokeStep;
            }
        }
    };

    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (0, numSamples, meta.samplePosition);
        if (at > pos)
        {
            renderSpan (pos, at - pos);
            pos = at;
        }
        const auto m = meta.getMessage();
        if (m.isNoteOn())
            trigger (m.getNoteNumber(), m.getFloatVelocity());
    }
    if (pos < numSamples)
        renderSpan (pos, numSamples - pos);
}

} // namespace bounce
