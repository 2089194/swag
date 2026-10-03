#pragma once

#include "Analysis/LabModel.h"
#include "Engine/DrumSampler.h"
#include "Engine/KeysSynth.h"
#include "Engine/Mixer.h"
#include "Engine/PatternPlayer.h"
#include "Engine/Voices.h"
#include "State/Session.h"

#include "bounce/util/TripleBuffer.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <memory>

namespace bounce
{

class BounceProcessor : public juce::AudioProcessor,
                        private juce::Timer
{
public:
    BounceProcessor();
    ~BounceProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getState() { return apvts; }
    Session& getSession() { return *session; }

    //== For the UI (lock-free reads) ===========================================
    double getLoopPosition() const noexcept { return loopPosition.load (std::memory_order_relaxed); }
    double getHostBpm() const noexcept { return hostBpm.load (std::memory_order_relaxed); }
    bool isHostPlaying() const noexcept { return hostPlaying.load (std::memory_order_relaxed); }
    float getPartPeak (AudioPart p) const noexcept { return mixer.getPeak (p); }

    //== Drum samples (message thread) ==========================================
    /** Loads a WAV/AIFF/FLAC/MP3 one-shot (first 10 s, mixed to mono) for a lane. */
    bool loadDrumSample (gen::DrumLane lane, const juce::File& file);
    void resetDrumSample (gen::DrumLane lane);
    juce::String getDrumSampleName (gen::DrumLane lane) const { return drums.getSampleName (lane); }
    bool hasCustomDrumSample (gen::DrumLane lane) const { return drums.isCustom (lane); }

    //== Audio capture for the Key & BPM Lab ====================================
    /** Starts recording the plugin's audio input (if the host feeds one). Message thread. */
    void startCapture();
    /** Stops recording and returns the mono capture (up to 60 s). Message thread. */
    juce::AudioBuffer<float> stopCapture();
    bool isCapturing() const noexcept { return capturing.load(); }
    double getCaptureSeconds() const noexcept;
    double getSampleRateForCapture() const noexcept { return currentSampleRate; }

    juce::AudioFormatManager& getFormatManager() { return formats; }
    LabModel& getLab() { return lab; }

private:
    juce::AudioProcessorValueTreeState apvts;
    juce::AudioFormatManager formats;
    LabModel lab { formats };

    // Message thread -> audio thread hand-over, one per playback slot.
    std::array<util::TripleBuffer<PlaybackPattern>, numPlaybackSlots> patterns;
    std::array<uint32_t, numPlaybackSlots> patternVersions {};

    std::unique_ptr<Session> session;

    std::array<PatternPlayer, numPlaybackSlots> players;
    std::array<juce::MidiBuffer, numPlaybackSlots> partMidi;
    juce::MidiBuffer keysMidi, leadMidi, midiOutBuffer;

    KeysSynth keys;
    Bass808 bass808;
    LeadSynth lead;
    DrumSampler drums;
    Mixer mixer;

    std::atomic<double> loopPosition { -1.0 };
    std::atomic<double> hostBpm { 0.0 };
    std::atomic<double> fallbackBpm { 140.0 };
    std::atomic<bool> hostPlaying { false };
    double currentSampleRate = 44100.0;

    // Capture ring (mono), allocated in prepareToPlay.
    juce::AudioBuffer<float> captureBuffer;
    std::atomic<bool> capturing { false };
    std::atomic<int> captureWrite { 0 };

    juce::StringArray drumSamplePaths;

    /** Parameter atomics cached at construction: the audio thread never looks parameters up by
        name (that would build strings, i.e. allocate). */
    struct ParamPointers
    {
        std::atomic<float>* preview = nullptr;
        std::atomic<float>* internalSound = nullptr;
        std::atomic<float>* midiOut = nullptr;
        std::atomic<float>* keysWobble = nullptr;
        std::atomic<float>* bassDecay = nullptr;
        std::atomic<float>* bassGlide = nullptr;
        std::atomic<float>* bassPunch = nullptr;
        std::atomic<float>* leadType = nullptr;
        enum Field { Level, Mute, Solo, Drive, Tone, Delay, Reverb, NumFields };
        std::array<std::array<std::atomic<float>*, NumFields>, numAudioParts> mix {};
    };
    ParamPointers pp;

    void publishPattern (PlaybackSlot slot, const midi::MidiClip& clip);
    void timerCallback() override { drums.collectGarbage(); }

    JUCE_DECLARE_WEAK_REFERENCEABLE (BounceProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BounceProcessor)
};

} // namespace bounce
