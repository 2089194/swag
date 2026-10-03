#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>

namespace bounce
{

/** Mono 808: sine + gentle saturation, pitch "punch" envelope, long decay, legato glide.
    Overlapping notes (how the generator writes slides) glide instead of retriggering.
    Real-time safe: no allocation after prepare(). */
class Bass808
{
public:
    struct Settings
    {
        float decaySeconds = 1.4f; // amp decay
        float glide = 0.4f;        // 0..1 -> glide time 25..250 ms
        float punch = 0.6f;        // pitch envelope depth
    };

    void prepare (double sampleRate);
    void reset();

    /** Adds the 808 (mono, same on both channels) into `buffer` from sample 0..num. */
    void render (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, const Settings& settings);

private:
    double sr = 44100.0;
    double phase = 0.0;
    double currentHz = 55.0, targetHz = 55.0;
    float env = 0.0f, pitchEnv = 0.0f;
    bool gate = false;
    std::array<int, 16> held {};
    int numHeld = 0;
    float velocity = 1.0f;

    void noteOn (int note, float vel, const Settings& s);
    void noteOff (int note);
    void renderSpan (juce::AudioBuffer<float>& buffer, int start, int num, const Settings& s);
};

/** Small polyphonic lead: Bell (2-op FM), Pluck (filtered saw) or Flute (sine + breath + vibrato). */
class LeadSynth
{
public:
    enum class Type
    {
        Bell,
        Pluck,
        Flute,
        NumTypes
    };

    static constexpr int numVoices = 12;

    void prepare (double sampleRate);
    void reset();
    void render (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, Type type);

private:
    struct Voice
    {
        int note = -1, channel = 0;
        uint32_t age = 0;
        float velocity = 0.0f;
        double phase = 0.0, modPhase = 0.0, inc = 0.0;
        double time = 0.0;
        float lp = 0.0f;
        uint32_t noise = 12345u;
        juce::ADSR env;
    };

    double sr = 44100.0;
    uint32_t ageCounter = 0;
    std::array<Voice, numVoices> voices;

    void noteOn (int channel, int note, float velocity, Type type);
    void noteOff (int channel, int note);
    void renderSpan (juce::AudioBuffer<float>& buffer, int start, int num, Type type);
};

} // namespace bounce
