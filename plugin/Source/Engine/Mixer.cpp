#include "Engine/Mixer.h"

#include <cmath>

namespace bounce
{

void Mixer::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate;
    blockSize = juce::jmax (1, maxBlockSize);
    for (auto& p : parts)
        p.setSize (2, blockSize, false, true, false);
    delaySendBus.setSize (2, blockSize, false, true, false);
    reverbSendBus.setSize (2, blockSize, false, true, false);
    delayLine.setSize (2, static_cast<int> (sr * 4.0) + 1, false, true, false);

    juce::dsp::ProcessSpec spec { sr, static_cast<juce::uint32> (blockSize), 2 };
    for (auto& f : filters)
    {
        f.prepare (spec);
        f.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        f.setResonance (0.75f);
    }
    for (auto& g : gains)
        g.reset (sr, 0.03);
    for (auto& c : cutoffs)
        c.reset (sr, 0.05);

    reverb.setSampleRate (sr);
    juce::Reverb::Parameters rp;
    rp.roomSize = 0.78f;
    rp.damping = 0.5f;
    rp.wetLevel = 1.0f;
    rp.dryLevel = 0.0f;
    rp.width = 1.0f;
    reverb.setParameters (rp);
    reset();
}

void Mixer::reset()
{
    for (auto& f : filters)
        f.reset();
    delayLine.clear();
    delayWrite = 0;
    delayLp[0] = delayLp[1] = 0.0f;
    reverb.reset();
}

void Mixer::beginBlock (int numSamples)
{
    for (auto& p : parts)
        p.clear (0, juce::jmin (numSamples, p.getNumSamples()));
}

void Mixer::process (juce::AudioBuffer<float>& out, const std::array<Strip, numAudioParts>& strips, double bpm)
{
    const int n = out.getNumSamples();
    if (n > blockSize)
        return; // host broke its promise; stay silent rather than allocate

    delaySendBus.clear (0, n);
    reverbSendBus.clear (0, n);
    out.clear();
    const int outCh = out.getNumChannels();

    for (int p = 0; p < numAudioParts; ++p)
    {
        auto& buf = parts[static_cast<size_t> (p)];
        const auto& s = strips[static_cast<size_t> (p)];
        auto& g = gains[static_cast<size_t> (p)];
        auto& fc = cutoffs[static_cast<size_t> (p)];
        auto& filter = filters[static_cast<size_t> (p)];

        g.setTargetValue (s.audible ? juce::Decibels::decibelsToGain (s.levelDb, -60.0f) : 0.0f);
        fc.setTargetValue (200.0f * std::pow (100.0f, juce::jlimit (0.0f, 1.0f, s.tone)));
        const float drive = 1.0f + 9.0f * juce::jlimit (0.0f, 1.0f, s.drive);
        const float driveComp = 1.0f / std::tanh (drive);
        const bool filterOn = s.tone < 0.999f;

        float peak = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float gain = g.getNextValue();
            const float cutoff = fc.getNextValue();
            if (filterOn && (i % 16) == 0)
                filter.setCutoffFrequency (juce::jmin (cutoff, static_cast<float> (sr * 0.45)));

            for (int ch = 0; ch < 2; ++ch)
            {
                float x = buf.getSample (ch, i);
                if (s.drive > 0.001f)
                    x = std::tanh (x * drive) * driveComp;
                if (filterOn)
                    x = filter.processSample (ch, x);
                x *= gain;

                if (outCh == 1)
                    out.addSample (0, i, 0.5f * x);
                else if (ch < outCh)
                    out.addSample (ch, i, x);
                delaySendBus.addSample (ch, i, x * s.delaySend);
                reverbSendBus.addSample (ch, i, x * s.reverbSend);
                peak = juce::jmax (peak, std::abs (x));
            }
        }
        auto& pk = peaks[static_cast<size_t> (p)];
        pk.store (juce::jmax (peak, pk.load (std::memory_order_relaxed) * 0.85f), std::memory_order_relaxed);
    }

    // Dotted-8th ping-pong-ish delay, damped in the loop.
    const int delaySamples = juce::jlimit (1, delayLine.getNumSamples() - 1,
                                           static_cast<int> (sr * 60.0 / juce::jlimit (40.0, 300.0, bpm) * 0.75));
    const int lineLen = delayLine.getNumSamples();
    for (int i = 0; i < n; ++i)
    {
        const int readPos = (delayWrite - delaySamples + lineLen) % lineLen;
        const float dl = delayLine.getSample (0, readPos), dr = delayLine.getSample (1, readPos);
        delayLp[0] += 0.35f * (dl - delayLp[0]);
        delayLp[1] += 0.35f * (dr - delayLp[1]);
        delayLine.setSample (0, delayWrite, delaySendBus.getSample (0, i) + 0.38f * delayLp[1]);
        delayLine.setSample (1, delayWrite, delaySendBus.getSample (1, i) + 0.38f * delayLp[0]);
        delayWrite = (delayWrite + 1) % lineLen;

        if (outCh == 1)
            out.addSample (0, i, 0.5f * (dl + dr));
        else
        {
            out.addSample (0, i, dl);
            out.addSample (1, i, dr);
        }
    }

    reverb.processStereo (reverbSendBus.getWritePointer (0), reverbSendBus.getWritePointer (1), n);
    for (int ch = 0; ch < outCh; ++ch)
        out.addFrom (ch, 0, reverbSendBus, juce::jmin (ch, 1), 0, n, outCh == 1 ? 0.5f : 1.0f);
}

} // namespace bounce
