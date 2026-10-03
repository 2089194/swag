#include "Engine/KeysSynth.h"

#include <cmath>

namespace bounce
{

namespace
{
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

inline double saw (double phase, double inc)
{
    return 2.0 * phase - 1.0 - polyBlep (phase, inc);
}
} // namespace

void KeysSynth::prepare (double newSampleRate, int maxBlockSize, int numChannels)
{
    sampleRate = newSampleRate;
    scratch.setSize (2, juce::jmax (1, maxBlockSize), false, true, true);
    pitchMod.setSize (1, juce::jmax (1, maxBlockSize), false, true, true);

    juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32> (juce::jmax (1, maxBlockSize)), 2 };
    chorus.prepare (spec);
    chorus.setRate (0.35f);
    chorus.setDepth (0.22f);
    chorus.setCentreDelay (9.0f);
    chorus.setFeedback (0.0f);
    chorus.setMix (0.35f);


    juce::ADSR::Parameters ap { 0.006f, 0.9f, 0.55f, 0.45f };
    for (auto& v : voices)
    {
        v.env.setSampleRate (sampleRate);
        v.env.setParameters (ap);
    }

    juce::ignoreUnused (numChannels);
    reset();
}

void KeysSynth::reset()
{
    for (auto& v : voices)
    {
        v.env.reset();
        v.note = -1;
        v.lp1 = v.lp2 = 0.0f;
    }
    chorus.reset();
}

void KeysSynth::noteOn (int channel, int note, float velocity)
{
    // Re-use a voice already playing this key, else a free one, else steal the oldest.
    Voice* target = nullptr;
    for (auto& v : voices)
        if (v.note == note && v.channel == channel && v.active())
            target = &v;
    if (target == nullptr)
        for (auto& v : voices)
            if (! v.active())
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
    const double freq = juce::MidiMessage::getMidiNoteInHertz (note);
    v.note = note;
    v.channel = channel;
    v.age = ++ageCounter;
    v.velocity = velocity;
    v.inc[0] = freq * 1.0035 / sampleRate;
    v.inc[1] = freq * 0.9965 / sampleRate;
    v.inc[2] = freq / sampleRate;
    v.phase[0] = 0.0;
    v.phase[1] = 0.37;
    v.phase[2] = 0.0;

    // Brighter when played harder, darker for low notes so spread voicings stay clean.
    const double cutoff = juce::jlimit (300.0, 9000.0, 900.0 + 3800.0 * velocity + freq * 1.5);
    v.cutoffCoeff = static_cast<float> (1.0 - std::exp (-juce::MathConstants<double>::twoPi * cutoff / sampleRate));
    v.pan = juce::jlimit (0.2f, 0.8f, 0.5f + static_cast<float> (note - 64) * 0.008f);
    v.env.noteOn();
}

void KeysSynth::noteOff (int channel, int note)
{
    for (auto& v : voices)
        if (v.note == note && v.channel == channel)
            v.env.noteOff();
}

void KeysSynth::renderVoices (int start, int num, float wobble)
{
    auto* left = scratch.getWritePointer (0);
    auto* right = scratch.getWritePointer (1);
    auto* mod = pitchMod.getWritePointer (0);

    // Shared tape wow (0.55 Hz) + flutter (7 Hz): one pitch factor per sample for all voices.
    const double wowInc = 0.55 / sampleRate, flutterInc = 7.0 / sampleRate;
    for (int i = start; i < start + num; ++i)
    {
        const double cents = wobble * (22.0 * std::sin (juce::MathConstants<double>::twoPi * wowPhase)
                                       + 4.0 * std::sin (juce::MathConstants<double>::twoPi * flutterPhase));
        mod[i] = static_cast<float> (std::pow (2.0, cents / 1200.0));
        wowPhase += wowInc;
        flutterPhase += flutterInc;
        if (wowPhase >= 1.0) wowPhase -= 1.0;
        if (flutterPhase >= 1.0) flutterPhase -= 1.0;
    }

    for (auto& v : voices)
    {
        if (! v.active())
            continue;

        const float amp = 0.11f * (0.35f + 0.65f * v.velocity);
        const float gl = std::sqrt (1.0f - v.pan), gr = std::sqrt (v.pan);
        // Lo-fi: wobble also darkens the tone.
        const float coeff = v.cutoffCoeff * (1.0f - 0.55f * wobble);

        for (int i = start; i < start + num; ++i)
        {
            double s = 0.0;
            s += 0.5 * saw (v.phase[0], v.inc[0]);
            s += 0.5 * saw (v.phase[1], v.inc[1]);
            s += 0.8 * std::sin (juce::MathConstants<double>::twoPi * v.phase[2]);

            for (int o = 0; o < 3; ++o)
            {
                v.phase[o] += v.inc[o] * mod[i];
                if (v.phase[o] >= 1.0)
                    v.phase[o] -= 1.0;
            }

            v.lp1 += coeff * (static_cast<float> (s) - v.lp1);
            v.lp2 += coeff * (v.lp1 - v.lp2);

            const float out = v.lp2 * amp * v.env.getNextSample();
            left[i] += out * gl;
            right[i] += out * gr;
        }

        if (! v.active())
            v.note = -1;
    }
}

void KeysSynth::render (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, float wobble)
{
    const int numSamples = buffer.getNumSamples();
    if (numSamples > scratch.getNumSamples())
        return; // host exceeded the promised block size; skip rather than allocate

    scratch.clear (0, numSamples);
    wobble = juce::jlimit (0.0f, 1.0f, wobble);

    int pos = 0;
    for (const auto meta : midi)
    {
        const int evPos = juce::jlimit (0, numSamples, meta.samplePosition);
        if (evPos > pos)
        {
            renderVoices (pos, evPos - pos, wobble);
            pos = evPos;
        }

        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
            noteOn (msg.getChannel(), msg.getNoteNumber(), msg.getFloatVelocity());
        else if (msg.isNoteOff())
            noteOff (msg.getChannel(), msg.getNoteNumber());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            for (auto& v : voices)
                v.env.noteOff();
    }
    if (pos < numSamples)
        renderVoices (pos, numSamples - pos, wobble);

    juce::dsp::AudioBlock<float> block (scratch.getArrayOfWritePointers(), 2, static_cast<size_t> (numSamples));
    chorus.process (juce::dsp::ProcessContextReplacing<float> (block));

    float blockPeak = 0.0f;
    const int outChannels = buffer.getNumChannels();
    for (int i = 0; i < numSamples; ++i)
    {
        // Tape hiss, only with wobble and only while something is sounding.
        hissState = hissState * 1664525u + 1013904223u;
        const float white = static_cast<float> (hissState >> 8) / 8388608.0f - 1.0f;
        hissLp += 0.3f * (white - hissLp);
        const float level = std::abs (scratch.getSample (0, i)) + std::abs (scratch.getSample (1, i));
        const float hiss = wobble * 0.004f * hissLp * juce::jmin (1.0f, level * 20.0f);

        const float l = scratch.getSample (0, i) + hiss;
        const float r = scratch.getSample (1, i) + hiss;
        if (outChannels == 1)
            buffer.addSample (0, i, 0.5f * (l + r));
        else if (outChannels >= 2)
        {
            buffer.addSample (0, i, l);
            buffer.addSample (1, i, r);
        }
        blockPeak = juce::jmax (blockPeak, std::abs (l), std::abs (r));
    }

    peak.store (juce::jmax (blockPeak, peak.load (std::memory_order_relaxed) * 0.9f), std::memory_order_relaxed);
}

} // namespace bounce
