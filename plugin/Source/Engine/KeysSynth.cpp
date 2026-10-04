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

/** Unison detune (cents) and stereo side (-1 left .. +1 right) of the pastel pad's 7 saws. */
constexpr double unisonCents[KeysSynth::maxOscs] { 0.0, -9.0, 9.0, -17.0, 17.0, -24.0, 24.0 };
constexpr double unisonSide[KeysSynth::maxOscs]  { 0.0, -0.6, 0.6, 0.9, -0.9, -0.3, 0.3 };
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


    // Electric-piano envelope: instant attack, a long natural decay to a soft sustain.
    juce::ADSR::Parameters ap { 0.002f, 2.6f, 0.32f, 0.38f };
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
    v.sound = sound;
    for (auto& ph : v.phase)
        ph = 0.0;
    v.lpR1 = v.lpR2 = 0.0f;

    const double lowness = juce::jlimit (0.0, 1.0, (60.0 - note) / 24.0);
    double cutoff = 0.0;
    switch (sound)
    {
        case Sound::Pastel:
            // 7 detuned saws, free-running phases so the unison shimmers from the first note.
            for (int o = 0; o < maxOscs; ++o)
            {
                v.inc[o] = freq * std::pow (2.0, unisonCents[o] / 1200.0) / sampleRate;
                v.phase[o] = std::fmod (0.137 * (o + 1) * (note + 3), 1.0);
            }
            v.env.setParameters ({ 0.008f, 0.35f, 0.82f, 0.12f }); // tight release: chops stay clean
            // Key-tracked filter: low notes stay round so the unison doesn't muddy the chord.
            cutoff = juce::jlimit (300.0, 8000.0, freq * (2.5 + 2.0 * velocity));
            break;

        case Sound::Bell:
            // Triton-style FM bell: inharmonic ratio-3.5 modulator that rings out, no sustain.
            v.inc[0] = freq / sampleRate;
            v.inc[1] = freq * 3.5 / sampleRate;
            v.inc[2] = freq * 7.0 / sampleRate;
            v.index = static_cast<float> ((1.2 + 1.8 * velocity) * (1.0 - 0.5 * lowness));
            v.tine = static_cast<float> (0.6 * velocity);
            v.indexDecay = static_cast<float> (std::exp (-1.0 / (0.6 * sampleRate)));
            v.tineDecay = static_cast<float> (std::exp (-1.0 / (0.05 * sampleRate)));
            v.env.setParameters ({ 0.001f, 1.6f, 0.0f, 0.5f });
            cutoff = 12000.0;
            break;

        case Sound::EPiano:
        case Sound::NumSounds:
            v.inc[0] = freq / sampleRate;           // carrier
            v.inc[1] = freq * 1.0008 / sampleRate;  // body modulator (ratio 1, a hair detuned for movement)
            v.inc[2] = freq * 14.0 / sampleRate;    // tine modulator: the bell-like "tink" of the attack
            // FM index: harder = brighter bark; low notes get less so spread voicings stay clean.
            v.index = static_cast<float> ((0.9 + 1.6 * velocity) * (1.0 - 0.55 * lowness));
            v.tine = static_cast<float> ((0.25 + 0.5 * velocity) * (1.0 - lowness));
            v.indexDecay = static_cast<float> (std::exp (-1.0 / (0.35 * sampleRate)));
            v.tineDecay = static_cast<float> (std::exp (-1.0 / (0.03 * sampleRate)));
            v.env.setParameters ({ 0.002f, 2.6f, 0.32f, 0.38f });
            cutoff = juce::jlimit (1500.0, 12000.0, 3000.0 + 6000.0 * velocity);
            break;
    }

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

        const float amp = (v.sound == Sound::Pastel ? 0.07f : 0.16f) * (0.35f + 0.65f * v.velocity);
        const float gl = std::sqrt (1.0f - v.pan), gr = std::sqrt (v.pan);
        // Lo-fi: wobble also darkens the tone.
        const float coeff = v.cutoffCoeff * (1.0f - 0.55f * wobble);

        constexpr double twoPi = juce::MathConstants<double>::twoPi;
        for (int i = start; i < start + num; ++i)
        {
            double sl = 0.0, sr = 0.0;
            if (v.sound == Sound::Pastel)
            {
                for (int o = 0; o < maxOscs; ++o)
                {
                    const double x = saw (v.phase[o], v.inc[o]);
                    sl += x * (1.0 - unisonSide[o]) * 0.5;
                    sr += x * (1.0 + unisonSide[o]) * 0.5;
                    v.phase[o] += v.inc[o] * mod[i];
                    if (v.phase[o] >= 1.0)
                        v.phase[o] -= 1.0;
                }
            }
            else
            {
                // E-piano keeps a little body brightness after the attack; the bell rings down to a sine.
                const double body = v.sound == Sound::Bell ? v.index : 0.35 + v.index;
                const double modulator = body * std::sin (twoPi * v.phase[1]) + v.tine * std::sin (twoPi * v.phase[2]);
                sl = sr = std::sin (twoPi * v.phase[0] + modulator);
                v.index *= v.indexDecay;
                v.tine *= v.tineDecay;
                for (int o = 0; o < 3; ++o)
                {
                    v.phase[o] += v.inc[o] * mod[i];
                    if (v.phase[o] >= 1.0)
                        v.phase[o] -= 1.0;
                }
            }

            v.lp1 += coeff * (static_cast<float> (sl) - v.lp1);
            v.lp2 += coeff * (v.lp1 - v.lp2);
            v.lpR1 += coeff * (static_cast<float> (sr) - v.lpR1);
            v.lpR2 += coeff * (v.lpR1 - v.lpR2);

            const float e = amp * v.env.getNextSample();
            left[i] += v.lp2 * e * gl;
            right[i] += v.lpR2 * e * gr;
        }

        if (! v.active())
            v.note = -1;
    }
}

void KeysSynth::render (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, float wobble, Sound newSound)
{
    sound = newSound; // applies to notes started from now on
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
