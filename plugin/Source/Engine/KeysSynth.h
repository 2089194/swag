#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>

namespace bounce
{

/** Polyphonic electric piano for the chords: 2-operator FM (a ratio-1 body whose brightness
    decays after the attack, plus a fast-decaying ratio-14 "tine"), a gentle low-pass, ADSR and
    chorus. Every chord tone stays distinct, so voicings read as chords rather than a wash.

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
        float index = 0.0f, tine = 0.0f, indexDecay = 1.0f, tineDecay = 1.0f;
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
