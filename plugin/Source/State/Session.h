#pragma once

#include "State/StyleLibrary.h"

#include "bounce/gen/ChordGenerator.h"
#include "bounce/gen/ChordRenderer.h"
#include "bounce/gen/IdeaHistory.h"
#include "bounce/midi/MidiClip.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <memory>

namespace bounce
{

/** Everything about the current idea that isn't a host parameter: the progression, its edits,
    undo history and idea list, the active style, and the rendered MIDI.

    Lives on the message thread. Host-automatable parameters are polled with a timer (never
    touched from the audio thread), and every change re-renders the clip and hands it to the
    audio thread through `publish`. */
class Session : public juce::ChangeBroadcaster,
                private juce::Timer
{
public:
    using Publisher = std::function<void (const midi::MidiClip&)>;

    Session (juce::AudioProcessorValueTreeState& state, Publisher publisher);
    ~Session() override;

    //== Generation ===============================================================
    /** New seed, same settings (the big Generate button). */
    void generate();
    /** New seed plus randomised colour/mood/voicing/rhythm (the dice). */
    void randomise();
    /** Regenerates with a specific seed (for reproducing an idea someone shared). */
    void generateWithSeed (uint64_t seed);

    //== Per-chord edits ==========================================================
    void toggleLock (int slot);
    void invert (int slot, int delta);
    void shiftOctave (int slot, int delta);
    void setSlotVoicing (int slot, std::optional<theory::VoicingStyle> style);
    void reharmonise (int slot);
    void transpose (int semitones);

    //== History ==================================================================
    bool canUndo() const { return history.canUndo(); }
    bool canRedo() const { return history.canRedo(); }
    void undo();
    void redo();
    const std::deque<gen::IdeaHistory::Idea>& ideas() const { return history.ideas(); }
    void recallIdea (int index);

    //== Styles ===================================================================
    StyleLibrary& styles() { return library; }
    int currentStyleIndex() const { return styleIndex; }
    const gen::StylePreset& currentStyle() const { return library.get (styleIndex); }
    /** Switches style, applies its defaults to the parameters and generates. */
    void setStyle (int index);
    /** Re-reads style JSON files; returns warnings. Keeps the current progression. */
    juce::StringArray reloadStyles();

    //== Selection (shared by wheel, strip and piano roll) ========================
    int getSelectedSlot() const { return selectedSlot; }
    void setSelectedSlot (int slot);

    //== Read access ==============================================================
    const gen::Progression& progression() const { return current; }
    const midi::MidiClip& chordClip() const { return clip; }
    uint64_t seed() const { return current.seed; }

    /** Tempo used for rendering/export: host tempo when known, else the style's. */
    double tempo() const;
    /** Polled on the message thread; returns 0 when the host hasn't reported a tempo. */
    void setHostTempoSource (std::function<double()> source) { hostTempoSource = std::move (source); }

    /** Clips + suggested file name for drag & drop / export. part: "chords" or "all". */
    std::vector<midi::MidiClip> clipsForExport (const juce::String& part) const;
    juce::String exportFileName (const juce::String& part) const;

    //== State ====================================================================
    /** Thread-safe: returns a copy of the state as of the last change (hosts may ask for state
        from any thread, so it is snapshotted on the message thread after every edit). */
    juce::ValueTree toValueTree() const;
    /** Message thread only. */
    void fromValueTree (const juce::ValueTree& tree);

private:
    juce::AudioProcessorValueTreeState& apvts;
    Publisher publish;
    StyleLibrary library;
    int styleIndex = 0;
    std::unique_ptr<gen::ChordGenerator> generator;

    gen::Progression current;
    gen::IdeaHistory history;
    midi::MidiClip clip;
    int selectedSlot = 0;
    uint64_t reharmCounter = 0;
    double hostTempo = 0.0;
    std::function<double()> hostTempoSource;
    void setHostTempo (double bpm);

    // Parameter polling.
    struct ParamSnapshot
    {
        int key = -1, scale = -1, bars = -1, chordCount = -1;
        float complexity = -1, mood = -2, borrowed = -1;
        int rhythm = -1, voicing = -1, octave = 99, channel = -1;
        float humanise = -1, swing = -1;
    };
    ParamSnapshot lastParams;
    int pendingRegenTicks = -1;
    bool suppressPolling = false;

    void timerCallback() override;
    ParamSnapshot readParams() const;

    gen::ChordGeneratorParams generatorParams (uint64_t seed) const;
    gen::ChordPerformance performance() const;

    void rebuildGenerator();
    void commit (const gen::Progression& p, bool asNewIdea, const juce::String& label = {});
    void render();
    void syncParamsFromProgression();
    void setParam (const char* id, float plainValue);

    mutable juce::SpinLock stateLock;
    juce::ValueTree cachedState;
    juce::ValueTree buildValueTree() const;

    static uint64_t freshSeed();
};

} // namespace bounce
