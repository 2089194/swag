#pragma once

#include "State/StyleLibrary.h"

#include "bounce/gen/Arrangement.h"
#include "bounce/gen/BassGenerator.h"
#include "bounce/gen/ChordGenerator.h"
#include "bounce/gen/ChordRenderer.h"
#include "bounce/gen/DrumGenerator.h"
#include "bounce/gen/Idea.h"
#include "bounce/gen/IdeaHistory.h"
#include "bounce/gen/MelodyGenerator.h"
#include "bounce/midi/MidiClip.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <functional>
#include <memory>

namespace bounce
{

/** Patterns handed to the audio thread. Drums go out twice: mapped for MIDI out, and in the
    internal lane/pitch encoding for the built-in sampler. */
enum class PlaybackSlot
{
    Chords,
    Bass,
    Melody,
    Counter,
    DrumsOut,
    DrumsInternal,
    NumSlots
};

inline constexpr int numPlaybackSlots = static_cast<int> (PlaybackSlot::NumSlots);

/** Everything about the current idea that isn't a host parameter: the chord loop, a seed per
    part, melody bar locks, note edits, undo history and idea list, the style, the arrangement,
    and the rendered MIDI for every part.

    Lives on the message thread. Host parameters are polled with a timer (never touched from the
    audio thread); every change re-renders the parts and hands them to the audio thread through
    `publish`. */
class Session : public juce::ChangeBroadcaster,
                private juce::Timer
{
public:
    using Publisher = std::function<void (PlaybackSlot, const midi::MidiClip&)>;

    Session (juce::AudioProcessorValueTreeState& state, Publisher publisher);
    ~Session() override;

    //== Generation ===============================================================
    /** New seeds for every part (the big Generate button). Locked chords/bars are kept. */
    void generate();
    /** Generate plus randomised colour/mood/voicing/rhythm/groove (the dice). */
    void randomise();
    /** Regenerates the chords with a specific seed (reproducing a shared idea). */
    void generateWithSeed (uint64_t seed);
    /** New seed for one part only; clears that part's hand edits. */
    void regeneratePart (gen::Part part);

    //== Per-chord edits ==========================================================
    void toggleLock (int slot);
    void invert (int slot, int delta);
    void shiftOctave (int slot, int delta);
    void setSlotVoicing (int slot, std::optional<theory::VoicingStyle> style);
    void reharmonise (int slot);
    void transpose (int semitones);
    void setSlotLength (int slot, double beats);
    void resetSlotLengths();

    //== Melody bar locks / note edits ============================================
    bool isMelodyBarLocked (int bar) const;
    void toggleMelodyBarLock (int bar);
    void addNote (gen::Part part, const midi::Note& note);
    void removeNote (gen::Part part, const midi::Note& note);
    void toggleDrumHit (gen::DrumLane lane, double beat);
    void clearEdits (gen::Part part);
    bool hasEdits (gen::Part part) const;

    //== From the Key & BPM Lab ==================================================
    /** Replaces the chords with a progression detected from audio (all chords locked). */
    void applyProgression (const gen::Progression& progression);

    //== History ==================================================================
    bool canUndo() const { return history.canUndo(); }
    bool canRedo() const { return history.canRedo(); }
    //== Listening inside Bounce ===================================================
    /** Play / stop Bounce's own loop (independent of FL's transport unless Sync is on). */
    bool isPlaying() const;
    void setPlaying (bool shouldPlay);

    /** Listen to one part on its own: solos it and starts playing. Calling it again for the
        part already being listened to stops playback and clears the solo. */
    void toggleListen (gen::Part part);

    /** True while `part` is soloed on its own and playing. */
    bool isListeningTo (gen::Part part) const;

    void undo();
    void redo();
    const std::deque<gen::IdeaHistory::Entry>& ideas() const { return history.ideas(); }
    void recallIdea (int index);

    //== Styles ===================================================================
    StyleLibrary& styles() { return library; }
    int currentStyleIndex() const { return styleIndex; }
    const gen::StylePreset& currentStyle() const { return library.get (styleIndex); }
    void setStyle (int index);
    juce::StringArray reloadStyles();

    //== Arrangement ==============================================================
    const gen::Arrangement& arrangement() const { return arrangementModel; }
    void setArrangement (const gen::Arrangement& a);

    //== Selection ================================================================
    int getSelectedSlot() const { return selectedSlot; }
    void setSelectedSlot (int slot);

    //== Read access ==============================================================
    const gen::Idea& idea() const { return current; }
    const gen::Progression& progression() const { return current.chords; }
    const midi::MidiClip& clip (gen::Part part) const { return clips[static_cast<size_t> (part)]; }
    const midi::MidiClip& chordClip() const { return clip (gen::Part::Chords); }
    const gen::DrumPattern& drumPattern() const { return drums; }
    uint64_t seed() const { return current.chords.seed; }

    double tempo() const;
    void setHostTempoSource (std::function<double()> source) { hostTempoSource = std::move (source); }

    /** "chords", "808", "melody", "counter", "drums", "all" or "arrangement". */
    std::vector<midi::MidiClip> clipsForExport (const juce::String& part) const;
    juce::String exportFileName (const juce::String& part) const;

    //== State ====================================================================
    /** Thread-safe snapshot (hosts may ask for state from any thread). */
    juce::ValueTree toValueTree() const;
    /** Message thread only. */
    void fromValueTree (const juce::ValueTree& tree);

private:
    juce::AudioProcessorValueTreeState& apvts;
    Publisher publish;
    StyleLibrary library;
    int styleIndex = 0;
    std::unique_ptr<gen::ChordGenerator> generator;

    gen::Idea current;
    gen::IdeaHistory history;
    gen::Arrangement arrangementModel = gen::Arrangement::defaultTemplate();
    std::array<midi::MidiClip, gen::numParts> clips;
    gen::DrumPattern drums;
    midi::MidiClip drumsInternal;
    int selectedSlot = 0;
    uint64_t reharmCounter = 0;
    double hostTempo = 0.0;
    std::function<double()> hostTempoSource;

    // Parameter polling.
    std::vector<std::pair<juce::String, std::atomic<float>*>> watched;
    std::vector<float> lastValues;
    int pendingRegenTicks = -1;
    bool suppressPolling = false;

    mutable juce::SpinLock stateLock;
    juce::ValueTree cachedState;

    void timerCallback() override;
    float raw (const char* id) const;
    int rawInt (const char* id) const;
    void snapshotParams();

    gen::ChordGeneratorParams generatorParams (uint64_t seed) const;
    gen::ChordPerformance performance() const;
    gen::DrumParams drumParams() const;

    void rebuildGenerator();
    void commit (const gen::Idea& idea, bool asNewIdea, const juce::String& label = {});
    void render();
    void syncParamsFromProgression();
    void setParam (const juce::String& id, float plainValue);
    void setHostTempo (double bpm);
    juce::ValueTree buildValueTree() const;
    gen::Idea withNewChords (const gen::Progression& p) const;

    static uint64_t freshSeed();
};

} // namespace bounce
