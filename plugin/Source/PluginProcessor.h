#pragma once

#include "Engine/KeysSynth.h"
#include "Engine/PatternPlayer.h"
#include "State/Session.h"

#include "bounce/util/TripleBuffer.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>

namespace bounce
{

class BounceProcessor : public juce::AudioProcessor
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
    double getTailLengthSeconds() const override { return 2.0; }

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
    float getOutputPeak() const noexcept { return keys.getPeakLevel(); }

private:
    juce::AudioProcessorValueTreeState apvts;

    // Message thread -> audio thread hand-over of the current loop.
    util::TripleBuffer<PlaybackPattern> chordPattern;
    uint32_t patternVersion = 0;

    std::unique_ptr<Session> session;

    PatternPlayer chordPlayer;
    KeysSynth keys;
    juce::MidiBuffer generated;

    std::atomic<double> loopPosition { -1.0 };
    std::atomic<double> hostBpm { 0.0 };
    std::atomic<double> fallbackBpm { 140.0 }; // style tempo, used when the host reports none
    std::atomic<bool> hostPlaying { false };

    std::atomic<float>* internalSoundParam = nullptr;
    std::atomic<float>* midiOutParam = nullptr;
    std::atomic<float>* previewParam = nullptr;
    std::atomic<float>* levelParam = nullptr;
    std::atomic<float>* muteParam = nullptr;

    void publishPattern (const midi::MidiClip& clip);

    JUCE_DECLARE_WEAK_REFERENCEABLE (BounceProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BounceProcessor)
};

} // namespace bounce
