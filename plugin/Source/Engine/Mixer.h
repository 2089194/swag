#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <array>

namespace bounce
{

/** Audio parts of the engine (Melody and Counter share the lead voice). */
enum class AudioPart
{
    Chords,
    Bass,
    Melody,
    Drums,
    NumParts
};

inline constexpr int numAudioParts = static_cast<int> (AudioPart::NumParts);

/** Per-part strip (drive -> low-pass -> level) plus shared tempo-synced delay and reverb sends.
    Everything is allocated in prepare(); process() is real-time safe. */
class Mixer
{
public:
    struct Strip
    {
        float levelDb = -6.0f;
        bool audible = true;   // mute/solo already resolved
        float drive = 0.0f;    // 0..1
        float tone = 1.0f;     // 0..1 -> 200 Hz .. 20 kHz low-pass (1 = open)
        float delaySend = 0.0f;
        float reverbSend = 0.0f;
    };

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    juce::AudioBuffer<float>& partBuffer (AudioPart p) { return parts[static_cast<size_t> (p)]; }

    /** Clears every part buffer (call at the top of the block). */
    void beginBlock (int numSamples);

    /** Mixes the part buffers into `out` (stereo or mono). */
    void process (juce::AudioBuffer<float>& out, const std::array<Strip, numAudioParts>& strips, double bpm);

    float getPeak (AudioPart p) const noexcept { return peaks[static_cast<size_t> (p)].load (std::memory_order_relaxed); }

private:
    double sr = 44100.0;
    int blockSize = 512;
    std::array<juce::AudioBuffer<float>, numAudioParts> parts;
    std::array<juce::dsp::StateVariableTPTFilter<float>, numAudioParts> filters;
    std::array<juce::SmoothedValue<float>, numAudioParts> gains, cutoffs;
    juce::AudioBuffer<float> delaySendBus, reverbSendBus, delayLine;
    int delayWrite = 0;
    float delayLp[2] {};
    juce::Reverb reverb;
    std::array<std::atomic<float>, numAudioParts> peaks {};
};

} // namespace bounce
