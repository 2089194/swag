#pragma once

#include "bounce/gen/DrumGenerator.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace bounce
{

/** One mono one-shot plus the rate it was recorded at. */
struct DrumSample
{
    std::vector<float> data;
    double sampleRate = 48000.0;
    juce::String name;
};

/** The bundled kit. Every sound is synthesised from scratch at startup (sine sweeps, filtered
    noise, 808-style metallic squares), so nothing third-party ships with the plugin. */
namespace DrumKit
{
std::unique_ptr<DrumSample> synthesise (gen::DrumLane lane, double sampleRate = 48000.0);
}

/** Polyphonic one-shot sampler, one sample per drum lane, open hat choked by the closed hat.

    Reads the internal drum encoding (gen::internalDrumNote): lane + pitch offset, so pitched
    hat rolls play at the right pitch whatever the MIDI-out note map is.

    Samples are swapped from the message thread with setSample(); the audio thread picks them up
    via atomics, and replaced samples are kept alive in a release pool for a few seconds before
    being freed, so the audio thread never touches freed memory and never frees anything. */
class DrumSampler
{
public:
    static constexpr int numVoices = 32;

    DrumSampler();
    ~DrumSampler();

    void prepare (double sampleRate);
    void reset();
    void render (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi);

    /** Message thread. nullptr restores the built-in sound. */
    void setSample (gen::DrumLane lane, std::unique_ptr<DrumSample> sample);
    juce::String getSampleName (gen::DrumLane lane) const;
    bool isCustom (gen::DrumLane lane) const;

    /** Message thread: frees retired samples that the audio thread can no longer be using. */
    void collectGarbage();

private:
    struct Voice
    {
        const DrumSample* sample = nullptr;
        int lane = -1;
        double pos = 0.0, rate = 1.0;
        float gain = 0.0f;
        float fade = 1.0f;
        bool choking = false;
    };

    double sr = 44100.0;
    std::array<std::unique_ptr<DrumSample>, gen::numDrumLanes> builtIn;
    std::array<std::unique_ptr<DrumSample>, gen::numDrumLanes> custom; // owned by the message thread
    std::array<std::atomic<const DrumSample*>, gen::numDrumLanes> active;
    std::array<Voice, numVoices> voices;

    struct Retired
    {
        std::unique_ptr<DrumSample> sample;
        juce::uint32 time;
    };
    std::vector<Retired> retired;

    void trigger (int note, float velocity);
};

} // namespace bounce
