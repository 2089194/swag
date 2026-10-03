#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>

namespace bounce
{

/** Milestone-1 audition voice: a soft polyphonic keys/pad (detuned PolyBLEP saws + sine through
    a gentle 12 dB low-pass, ADSR), followed by chorus and reverb. Just enough to hear ideas
    without routing; the full engine (808, lead, drum sampler, per-part FX) is milestone 2.

    Real-time safe: everything is allocated in prepare(); render() never allocates or locks
    (deliberately not juce::Synthesiser, whose render path takes a lock). */
class KeysSynth
{
public:
    static constexpr int numVoices = 16;

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();

    /** Renders `midi` and adds the result to `buffer`. `wobble` (0..1) adds tape wow/flutter,
        a darker tone and a little hiss for the lo-fi sound. */
    void render (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, float wobble);

    /** Any thread: output peak for the UI meter. */
    float getPeakLevel() const noexcept { return peak.load (std::memory_order_relaxed); }

private:
    struct Voice
    {
        int note = -1;
        int channel = 0;
        uint32_t age = 0;
        float velocity = 0.0f;
        double phase[3] {};
        double inc[3] {};
        float lp1 = 0.0f, lp2 = 0.0f, cutoffCoeff = 0.2f;
        float pan = 0.5f;
        juce::ADSR env;

        bool active() const { return env.isActive(); }
    };

    void noteOn (int channel, int note, float velocity);
    void noteOff (int channel, int note);
    void renderVoices (int start, int num, float wobble);

    double sampleRate = 44100.0;
    uint32_t ageCounter = 0;
    std::array<Voice, numVoices> voices;
    juce::AudioBuffer<float> scratch, pitchMod;
    juce::dsp::Chorus<float> chorus;
    double wowPhase = 0.0, flutterPhase = 0.0;
    uint32_t hissState = 0x9E3779B9u;
    float hissLp = 0.0f;
    std::atomic<float> peak { 0.0f };
};

} // namespace bounce
