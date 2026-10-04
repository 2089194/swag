#include "State/Parameters.h"

#include "bounce/gen/BassGenerator.h"
#include "bounce/gen/ChordRenderer.h"
#include "bounce/gen/DrumGenerator.h"
#include "bounce/gen/MelodyGenerator.h"
#include "bounce/theory/Scale.h"
#include "bounce/theory/Voicing.h"

namespace bounce::params
{

namespace
{
template <typename Enum, typename NameFn>
juce::StringArray enumNames (int count, NameFn&& nameOf)
{
    juce::StringArray s;
    for (int i = 0; i < count; ++i)
        s.add (juce::String (std::string (nameOf (static_cast<Enum> (i)))));
    return s;
}

std::unique_ptr<juce::AudioParameterFloat> percent (const juce::String& id, const juce::String& name, float def)
{
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (0.0f, 1.0f), def,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return juce::String (juce::roundToInt (v * 100.0f)) + "%";
        }));
}

std::unique_ptr<juce::AudioParameterBool> toggle (const juce::String& id, const juce::String& name, bool def)
{
    return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def);
}

std::unique_ptr<juce::AudioParameterChoice> choice (const juce::String& id, const juce::String& name, juce::StringArray items, int def)
{
    return std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, std::move (items), def);
}

const char* partPrefix (AudioPart p)
{
    switch (p)
    {
        case AudioPart::Chords: return "chords";
        case AudioPart::Bass:   return "bass";
        case AudioPart::Melody: return "melody";
        case AudioPart::Drums:  return "drums";
        case AudioPart::NumParts: break;
    }
    return "x";
}

const char* partTitle (AudioPart p)
{
    switch (p)
    {
        case AudioPart::Chords: return "Chords";
        case AudioPart::Bass:   return "808";
        case AudioPart::Melody: return "Melody";
        case AudioPart::Drums:  return "Drums";
        case AudioPart::NumParts: break;
    }
    return "";
}
} // namespace

juce::String drumNoteId (int lane)
{
    return "drumNote" + juce::String (lane);
}

juce::String mixId (AudioPart part, const char* field)
{
    return juce::String (partPrefix (part)) + field;
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    // --- Chords.
    layout.add (choice (key, "Key", { "C", "C#/Db", "D", "D#/Eb", "E", "F", "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B" }, 9));
    layout.add (choice (scale, "Scale", enumNames<theory::ScaleType> (static_cast<int> (theory::ScaleType::NumTypes), theory::scaleTypeName), 1));
    layout.add (choice (bars, "Bars", { "2", "4", "8" }, 1));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { chordCount, 1 }, "Chords", 1, 8, 4));
    layout.add (percent (complexity, "Complexity", 0.65f));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { mood, 1 }, "Mood", NormalisableRange<float> (-1.0f, 1.0f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            if (std::abs (v) < 0.05f) return String ("Neutral");
            return (v < 0 ? String ("Dark ") : String ("Bright ")) + String (roundToInt (std::abs (v) * 100.0f)) + "%";
        })));
    layout.add (percent (borrowed, "Borrowed", 0.35f));
    layout.add (choice (rhythm, "Rhythm", enumNames<gen::ChordRhythm> (static_cast<int> (gen::ChordRhythm::NumRhythms), gen::chordRhythmName), 0));
    layout.add (choice (voicing, "Voicing", enumNames<theory::VoicingStyle> (static_cast<int> (theory::VoicingStyle::NumStyles), theory::voicingStyleName), 1));
    layout.add (percent (humanise, "Humanise", 0.5f));
    layout.add (percent (swing, "Swing", 0.1f));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { octave, 1 }, "Octave", -2, 2, 0));
    layout.add (percent (keysWobble, "Tape Wobble", 0.0f));

    // --- I/O.
    layout.add (toggle (internalSound, "Internal Sound", true));
    layout.add (toggle (midiOut, "MIDI Out", true));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { midiChannel, 1 }, "MIDI Channel", 1, 16, 1));
    layout.add (toggle (preview, "Play", false));
    layout.add (toggle (syncHost, "Sync To Host", false));
    layout.add (choice (tempoView, "Tempo View", { "Normal", "Half-time", "Double-time" }, 0));

    // --- 808.
    layout.add (choice (bassMode, "808 Mode", enumNames<gen::BassMode> (static_cast<int> (gen::BassMode::NumModes), gen::bassModeName), 1));
    layout.add (percent (bassDensity, "808 Density", 0.5f));
    layout.add (percent (bassGlide, "808 Glide", 0.45f));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { bassOctaves, 1 }, "808 Octaves", 1, 2, 2));
    layout.add (toggle (bassLockKick, "808 Lock To Kick", true));
    layout.add (percent (bassLength, "808 Note Length", 0.8f));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { bassLowNote, 1 }, "808 Register", 24, 36, 28,
        AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int)
        {
            return MidiMessage::getMidiNoteName (v, true, true, 5); // FL Studio naming: C5 = 60
        })));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { bassDecay, 1 }, "808 Decay",
                                                       NormalisableRange<float> (0.1f, 4.0f, 0.01f, 0.5f), 1.4f,
                                                       AudioParameterFloatAttributes().withLabel ("s")));
    layout.add (percent (bassPunch, "808 Punch", 0.6f));

    // --- Melody.
    layout.add (percent (melDensity, "Melody Density", 0.5f));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { melOctave, 1 }, "Melody Octave", -1, 1, 0));
    layout.add (choice (melFeel, "Melody Feel", enumNames<gen::MelodyFeel> (static_cast<int> (gen::MelodyFeel::NumFeels), gen::melodyFeelName), 0));
    layout.add (percent (melRepetition, "Repetition", 0.6f));
    layout.add (percent (melCatchiness, "Catchiness", 0.6f));
    layout.add (toggle (melCallResponse, "Call & Response", true));
    layout.add (percent (melPentatonic, "Pentatonic", 0.6f));
    layout.add (toggle (melCounter, "Counter-Melody", false));
    layout.add (percent (counterDensity, "Counter Density", 0.5f));
    layout.add (choice (leadType, "Lead Sound", { "Bell", "Pluck", "Flute" }, 0));

    // --- Drums.
    layout.add (percent (drumKick, "Kick Density", 0.5f));
    layout.add (percent (drumSwing, "Drum Swing", 0.12f));
    layout.add (percent (drumHumanise, "Drum Humanise", 0.4f));
    layout.add (percent (drumBounce, "Bounce", 0.3f));
    layout.add (percent (drumRolls, "Hat Rolls", 0.5f));
    layout.add (toggle (drumRoll16, "Rolls 1/16", true));
    layout.add (toggle (drumRoll16T, "Rolls 1/16T", true));
    layout.add (toggle (drumRoll32, "Rolls 1/32", true));
    layout.add (toggle (drumRoll32T, "Rolls 1/32T", false));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { drumRollPitch, 1 }, "Roll Pitch", -12, 12, 0,
                                                     AudioParameterIntAttributes().withLabel ("st")));
    layout.add (choice (drumRollCurve, "Roll Curve", enumNames<gen::RollCurve> (static_cast<int> (gen::RollCurve::NumCurves), gen::rollCurveName), 0));
    layout.add (percent (drumOpenHat, "Open Hats", 0.4f));
    layout.add (percent (drumPerc, "Percs", 0.4f));
    layout.add (toggle (drumFx, "Crash/FX", true));
    layout.add (toggle (drumPitchNotes, "Roll Pitch To Notes", false));
    layout.add (choice (drumMap, "Drum Map", { "GM / FPC", "Custom" }, 0));
    const auto gm = gen::DrumMap::gm();
    for (int l = 0; l < gen::numDrumLanes; ++l)
        layout.add (std::make_unique<AudioParameterInt> (ParameterID { drumNoteId (l), 1 },
                                                         String (std::string (gen::drumLaneName (static_cast<gen::DrumLane> (l)))) + " Note",
                                                         0, 127, gm.notes[static_cast<size_t> (l)]));

    // --- Mixer.
    const float defaultReverb[] = { 0.25f, 0.0f, 0.3f, 0.08f };
    const float defaultDelay[] = { 0.0f, 0.0f, 0.18f, 0.0f };
    for (int p = 0; p < numAudioParts; ++p)
    {
        const auto part = static_cast<AudioPart> (p);
        const String t (partTitle (part));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { mixId (part, fLevel), 1 }, t + " Level",
                                                           NormalisableRange<float> (-48.0f, 6.0f, 0.1f, 2.0f),
                                                           part == AudioPart::Drums ? -4.0f : -6.0f,
                                                           AudioParameterFloatAttributes().withLabel ("dB")));
        layout.add (toggle (mixId (part, fMute), t + " Mute", false));
        layout.add (toggle (mixId (part, fSolo), t + " Solo", false));
        layout.add (percent (mixId (part, fDrive), t + " Drive", part == AudioPart::Bass ? 0.25f : 0.0f));
        layout.add (percent (mixId (part, fTone), t + " Tone", 1.0f));
        layout.add (percent (mixId (part, fDelay), t + " Delay", defaultDelay[p]));
        layout.add (percent (mixId (part, fReverb), t + " Reverb", defaultReverb[p]));
    }
    return layout;
}

bool isChordHarmonyParam (const juce::String& id)
{
    return id == complexity || id == mood || id == borrowed || id == scale || id == bars || id == chordCount;
}

bool affectsRendering (const juce::String& id)
{
    // Everything that shapes generated notes but not the audio-only mixer / voice settings.
    static const juce::StringArray audioOnly { internalSound, midiOut, preview, syncHost, tempoView, keysWobble, bassDecay, bassPunch, leadType };
    if (audioOnly.contains (id))
        return false;
    for (int p = 0; p < numAudioParts; ++p)
        for (auto* f : { fLevel, fMute, fSolo, fDrive, fTone, fDelay, fReverb })
            if (id == mixId (static_cast<AudioPart> (p), f))
                return false;
    return true;
}

} // namespace bounce::params
