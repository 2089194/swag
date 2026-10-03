#include <doctest/doctest.h>

#include "Engine/DrumSampler.h"
#include "Engine/KeysSynth.h"
#include "Engine/Mixer.h"
#include "Engine/Voices.h"

#include <cmath>

using namespace bounce;

namespace
{
constexpr double sr = 48000.0;
constexpr int block = 256;

float peakOf (const juce::AudioBuffer<float>& b)
{
    float p = 0.0f;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float v = b.getSample (ch, i);
            REQUIRE (std::isfinite (v));
            p = std::max (p, std::abs (v));
        }
    return p;
}

/** Zero crossings per second of channel 0 ~ 2 x frequency. */
double estimateHz (const juce::AudioBuffer<float>& b)
{
    int crossings = 0;
    for (int i = 1; i < b.getNumSamples(); ++i)
        crossings += (b.getSample (0, i - 1) < 0.0f) != (b.getSample (0, i) < 0.0f) ? 1 : 0;
    return crossings / 2.0 / (b.getNumSamples() / sr);
}
} // namespace

TEST_CASE ("808: sounds, glides on overlap, releases")
{
    Bass808 bass;
    bass.prepare (sr);
    Bass808::Settings s;
    s.punch = 0.0f;
    s.glide = 0.2f;

    juce::AudioBuffer<float> buf (2, 4800); // 100 ms
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (2, 33, 1.0f), 0); // A1 = 55 Hz
    buf.clear();
    bass.render (buf, midi, s);
    CHECK (peakOf (buf) > 0.1f);
    CHECK (estimateHz (buf) == doctest::Approx (55.0).epsilon (0.1));

    // Overlapping note: legato glide up an octave, no retrigger.
    midi.clear();
    midi.addEvent (juce::MidiMessage::noteOn (2, 45, 1.0f), 0);
    midi.addEvent (juce::MidiMessage::noteOff (2, 33), 100);
    for (int i = 0; i < 4; ++i)
    {
        buf.clear();
        bass.render (buf, midi, s);
        midi.clear();
    }
    CHECK (estimateHz (buf) == doctest::Approx (110.0).epsilon (0.1));

    midi.addEvent (juce::MidiMessage::noteOff (2, 45), 0);
    for (int i = 0; i < 20; ++i)
    {
        buf.clear();
        bass.render (buf, midi, s);
        midi.clear();
    }
    CHECK (peakOf (buf) < 0.001f);
}

TEST_CASE ("lead: every type sounds and stays bounded")
{
    for (int t = 0; t < static_cast<int> (LeadSynth::Type::NumTypes); ++t)
    {
        LeadSynth lead;
        lead.prepare (sr);
        juce::AudioBuffer<float> buf (2, 9600);
        juce::MidiBuffer midi;
        for (int n : { 72, 76, 79 })
            midi.addEvent (juce::MidiMessage::noteOn (3, n, 0.9f), 0);
        buf.clear();
        lead.render (buf, midi, static_cast<LeadSynth::Type> (t));
        const float p = peakOf (buf);
        CHECK (p > 0.02f);
        CHECK (p < 1.5f);
    }
}

TEST_CASE ("keys: wobble keeps output finite")
{
    KeysSynth keys;
    keys.prepare (sr, 4096, 2);
    juce::AudioBuffer<float> buf (2, 4096);
    juce::MidiBuffer midi;
    for (int n : { 57, 60, 64, 67 })
        midi.addEvent (juce::MidiMessage::noteOn (1, n, 0.8f), 0);
    for (float wobble : { 0.0f, 1.0f })
    {
        buf.clear();
        keys.render (buf, midi, wobble);
        CHECK (peakOf (buf) > 0.01f);
    }
}

TEST_CASE ("drum kit: every lane is synthesised; sampler plays the internal encoding")
{
    for (int l = 0; l < gen::numDrumLanes; ++l)
    {
        const auto s = DrumKit::synthesise (static_cast<gen::DrumLane> (l));
        REQUIRE (s != nullptr);
        CHECK (s->data.size() > 1000);
        float p = 0.0f;
        for (float v : s->data)
            p = std::max (p, std::abs (v));
        CHECK (p == doctest::Approx (0.9f).epsilon (0.01));
    }

    DrumSampler sampler;
    sampler.prepare (sr);
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (10, gen::internalDrumNote (gen::DrumLane::Kick, 0), 1.0f), 0);
    buf.clear();
    sampler.render (buf, midi);
    CHECK (peakOf (buf) > 0.05f);

    // Custom sample swap from the "message thread" while voices may hold the old one.
    auto custom = std::make_unique<DrumSample>();
    custom->data.assign (4800, 0.5f);
    custom->name = "test";
    sampler.setSample (gen::DrumLane::Kick, std::move (custom));
    CHECK (sampler.isCustom (gen::DrumLane::Kick));
    CHECK (sampler.getSampleName (gen::DrumLane::Kick) == "test");
    midi.clear();
    midi.addEvent (juce::MidiMessage::noteOn (10, gen::internalDrumNote (gen::DrumLane::Kick, 0), 1.0f), 0);
    buf.clear();
    sampler.render (buf, midi);
    CHECK (peakOf (buf) > 0.2f);
    sampler.setSample (gen::DrumLane::Kick, nullptr);
    CHECK_FALSE (sampler.isCustom (gen::DrumLane::Kick));
    sampler.collectGarbage(); // retired sample is kept for a while, then freed
}

TEST_CASE ("mixer: mute silences, sends add tails, output finite")
{
    Mixer mixer;
    mixer.prepare (sr, block);
    std::array<Mixer::Strip, numAudioParts> strips {};
    for (auto& s : strips)
    {
        s.levelDb = 0.0f;
        s.reverbSend = 0.5f;
        s.delaySend = 0.5f;
        s.drive = 0.5f;
        s.tone = 0.5f;
    }

    juce::AudioBuffer<float> out (2, block);
    mixer.beginBlock (block);
    for (int i = 0; i < block; ++i)
        mixer.partBuffer (AudioPart::Chords).setSample (0, i, std::sin (static_cast<float> (i) * 0.1f));
    mixer.process (out, strips, 140.0);
    CHECK (peakOf (out) > 0.01f);

    for (auto& s : strips)
    {
        s.audible = false;
        s.reverbSend = s.delaySend = 0.0f;
    }
    for (int b = 0; b < 1500; ++b) // let gains ramp down and tails die (~8 s)
    {
        mixer.beginBlock (block);
        mixer.process (out, strips, 140.0);
    }
    CHECK (peakOf (out) < 0.01f);
}
