#include "Engine/Voices.h"

#include <cmath>

namespace bounce
{

namespace
{
constexpr double twoPi = juce::MathConstants<double>::twoPi;

inline double polyBlep (double t, double dt)
{
    if (t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.0;
    }
    if (t > 1.0 - dt)
    {
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }
    return 0.0;
}

} // namespace

//==============================================================================
void Bass808::prepare (double sampleRate)
{
    sr = sampleRate;
    reset();
}

void Bass808::reset()
{
    env = pitchEnv = 0.0f;
    gate = false;
    numHeld = 0;
    phase = 0.0;
}

void Bass808::noteOn (int note, float vel, const Settings& s)
{
    juce::ignoreUnused (s);
    const bool legato = numHeld > 0 && env > 0.001f;
    if (numHeld < static_cast<int> (held.size()))
        held[static_cast<size_t> (numHeld++)] = note;

    targetHz = juce::MidiMessage::getMidiNoteInHertz (note);
    if (! legato)
    {
        currentHz = targetHz;
        phase = 0.0;
        env = 1.0f;
        pitchEnv = 1.0f;
        velocity = 0.35f + 0.65f * vel;
    }
    gate = true;
}

void Bass808::noteOff (int note)
{
    for (int i = 0; i < numHeld; ++i)
        if (held[static_cast<size_t> (i)] == note)
        {
            for (int k = i; k < numHeld - 1; ++k)
                held[static_cast<size_t> (k)] = held[static_cast<size_t> (k + 1)];
            --numHeld;
            break;
        }

    if (numHeld > 0)
        targetHz = juce::MidiMessage::getMidiNoteInHertz (held[static_cast<size_t> (numHeld - 1)]); // back to the held note
    else
        gate = false;
}

void Bass808::renderSpan (juce::AudioBuffer<float>& buffer, int start, int num, const Settings& s)
{
    if (env <= 1.0e-5f && ! gate)
        return;

    const float decay = std::exp (-1.0f / (static_cast<float> (sr) * juce::jlimit (0.05f, 6.0f, s.decaySeconds) / 5.0f));
    const float release = std::exp (-1.0f / (static_cast<float> (sr) * 0.03f));
    const float pitchDecay = std::exp (-1.0f / (static_cast<float> (sr) * 0.035f));
    const double glideMs = 25.0 + 225.0 * juce::jlimit (0.0f, 1.0f, s.glide);
    const double glideCoeff = 1.0 - std::exp (-1.0 / (sr * glideMs / 1000.0 / 4.0));
    const double punchSemis = 14.0 * juce::jlimit (0.0f, 1.0f, s.punch);

    auto* l = buffer.getWritePointer (0);
    auto* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int i = start; i < start + num; ++i)
    {
        currentHz += (targetHz - currentHz) * glideCoeff;
        const double hz = currentHz * std::pow (2.0, punchSemis * pitchEnv / 12.0);
        phase += hz / sr;
        if (phase >= 1.0)
            phase -= 1.0;

        const double x = std::sin (twoPi * phase);
        const float sat = static_cast<float> (std::tanh (x * 1.8) / std::tanh (1.8));
        const float out = sat * env * velocity * 0.55f;

        env *= gate ? decay : release;
        pitchEnv *= pitchDecay;

        l[i] += out;
        if (r != nullptr)
            r[i] += out;
    }
}

void Bass808::render (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, const Settings& s)
{
    const int numSamples = buffer.getNumSamples();
    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (0, numSamples, meta.samplePosition);
        if (at > pos)
        {
            renderSpan (buffer, pos, at - pos, s);
            pos = at;
        }
        const auto m = meta.getMessage();
        if (m.isNoteOn())
            noteOn (m.getNoteNumber(), m.getFloatVelocity(), s);
        else if (m.isNoteOff())
            noteOff (m.getNoteNumber());
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            numHeld = 0;
            gate = false;
        }
    }
    if (pos < numSamples)
        renderSpan (buffer, pos, numSamples - pos, s);
}

//==============================================================================
void LeadSynth::prepare (double sampleRate)
{
    sr = sampleRate;
    for (auto& v : voices)
        v.env.setSampleRate (sr);
    reset();
}

void LeadSynth::reset()
{
    for (auto& v : voices)
    {
        v.env.reset();
        v.note = -1;
    }
}

void LeadSynth::noteOn (int channel, int note, float velocity, Type type)
{
    Voice* target = nullptr;
    for (auto& v : voices)
        if (! v.env.isActive())
        {
            target = &v;
            break;
        }
    if (target == nullptr)
    {
        target = &voices[0];
        for (auto& v : voices)
            if (v.age < target->age)
                target = &v;
    }

    auto& v = *target;
    v.note = note;
    v.channel = channel;
    v.age = ++ageCounter;
    v.velocity = velocity;
    v.inc = juce::MidiMessage::getMidiNoteInHertz (note) / sr;
    v.phase = v.modPhase = 0.0;
    v.time = 0.0;
    v.lp = 0.0f;

    switch (type)
    {
        case Type::Bell:  v.env.setParameters ({ 0.002f, 1.6f, 0.0f, 0.5f }); break;
        case Type::Pluck: v.env.setParameters ({ 0.001f, 0.45f, 0.15f, 0.18f }); break;
        case Type::Flute: v.env.setParameters ({ 0.06f, 0.2f, 0.9f, 0.16f }); break;
        case Type::NumTypes: break;
    }
    v.env.noteOn();
}

void LeadSynth::noteOff (int channel, int note)
{
    for (auto& v : voices)
        if (v.note == note && v.channel == channel)
            v.env.noteOff();
}

void LeadSynth::renderSpan (juce::AudioBuffer<float>& buffer, int start, int num, Type type)
{
    auto* l = buffer.getWritePointer (0);
    auto* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    const double dt = 1.0 / sr;

    for (auto& v : voices)
    {
        if (! v.env.isActive())
            continue;
        const float amp = 0.16f * (0.3f + 0.7f * v.velocity);

        for (int i = start; i < start + num; ++i)
        {
            double s = 0.0;
            switch (type)
            {
                case Type::Bell:
                {
                    const double index = 3.5 * std::exp (-v.time * 5.0) + 0.3;
                    v.modPhase += v.inc * 3.5;
                    if (v.modPhase >= 1.0) v.modPhase -= 1.0;
                    s = std::sin (twoPi * v.phase + index * std::sin (twoPi * v.modPhase));
                    break;
                }
                case Type::Pluck:
                {
                    const double saw = 2.0 * v.phase - 1.0 - polyBlep (v.phase, v.inc);
                    const double cutoff = 500.0 + 7000.0 * std::exp (-v.time * 14.0);
                    const auto a = static_cast<float> (1.0 - std::exp (-twoPi * cutoff / sr));
                    v.lp += a * (static_cast<float> (saw) - v.lp);
                    s = v.lp;
                    break;
                }
                case Type::Flute:
                {
                    const double vib = v.time > 0.25 ? 0.004 * std::sin (twoPi * 5.2 * v.time) : 0.0;
                    v.noise = v.noise * 1664525u + 1013904223u;
                    const double n = (static_cast<double> (v.noise >> 8) / 8388608.0 - 1.0);
                    v.lp += 0.08f * (static_cast<float> (n) - v.lp);
                    s = std::sin (twoPi * v.phase) + 0.18 * std::sin (2.0 * twoPi * v.phase) + 0.35 * v.lp;
                    v.phase += v.inc * vib; // vibrato
                    break;
                }
                case Type::NumTypes: break;
            }

            v.phase += v.inc;
            if (v.phase >= 1.0) v.phase -= 1.0;
            v.time += dt;

            const float out = static_cast<float> (s) * amp * v.env.getNextSample();
            l[i] += out;
            if (r != nullptr)
                r[i] += out;
        }
        if (! v.env.isActive())
            v.note = -1;
    }
}

void LeadSynth::render (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, Type type)
{
    const int numSamples = buffer.getNumSamples();
    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (0, numSamples, meta.samplePosition);
        if (at > pos)
        {
            renderSpan (buffer, pos, at - pos, type);
            pos = at;
        }
        const auto m = meta.getMessage();
        if (m.isNoteOn())
            noteOn (m.getChannel(), m.getNoteNumber(), m.getFloatVelocity(), type);
        else if (m.isNoteOff())
            noteOff (m.getChannel(), m.getNoteNumber());
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            for (auto& v : voices)
                v.env.noteOff();
    }
    if (pos < numSamples)
        renderSpan (buffer, pos, numSamples - pos, type);
}

} // namespace bounce
