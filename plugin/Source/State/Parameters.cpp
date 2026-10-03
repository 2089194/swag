#include "State/Parameters.h"

#include "bounce/gen/ChordRenderer.h"
#include "bounce/theory/Scale.h"
#include "bounce/theory/Voicing.h"

namespace bounce::params
{

namespace
{
juce::StringArray keyNames()
{
    return { "C", "C#/Db", "D", "D#/Eb", "E", "F", "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B" };
}

juce::StringArray scaleNames()
{
    juce::StringArray s;
    for (int i = 0; i < static_cast<int> (theory::ScaleType::NumTypes); ++i)
        s.add (juce::String (std::string (theory::scaleTypeName (static_cast<theory::ScaleType> (i)))));
    return s;
}

juce::StringArray rhythmNames()
{
    juce::StringArray s;
    for (int i = 0; i < static_cast<int> (gen::ChordRhythm::NumRhythms); ++i)
        s.add (juce::String (std::string (gen::chordRhythmName (static_cast<gen::ChordRhythm> (i)))));
    return s;
}

juce::StringArray voicingNames()
{
    juce::StringArray s;
    for (int i = 0; i < static_cast<int> (theory::VoicingStyle::NumStyles); ++i)
        s.add (juce::String (std::string (theory::voicingStyleName (static_cast<theory::VoicingStyle> (i)))));
    return s;
}

std::unique_ptr<juce::AudioParameterFloat> percent (const char* id, const char* name, float def)
{
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (0.0f, 1.0f), def,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return juce::String (juce::roundToInt (v * 100.0f)) + "%";
        }));
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { key, 1 }, "Key", keyNames(), 9));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { scale, 1 }, "Scale", scaleNames(), 1));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { bars, 1 }, "Bars", StringArray { "2", "4", "8" }, 1));
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
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { rhythm, 1 }, "Rhythm", rhythmNames(), 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { voicing, 1 }, "Voicing", voicingNames(), 1));
    layout.add (percent (humanise, "Humanise", 0.5f));
    layout.add (percent (swing, "Swing", 0.1f));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { octave, 1 }, "Octave", -2, 2, 0));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { chordsLevel, 1 }, "Chords Level", NormalisableRange<float> (-48.0f, 6.0f, 0.1f, 2.0f), -6.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { chordsMute, 1 }, "Chords Mute", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { internalSound, 1 }, "Internal Sound", true));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { midiOut, 1 }, "MIDI Out", true));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { midiChannel, 1 }, "MIDI Channel", 1, 16, 1));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { preview, 1 }, "Preview", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { tempoView, 1 }, "Tempo View",
                                                        StringArray { "Normal", "Half-time", "Double-time" }, 0));
    return layout;
}

bool isPerformanceParam (const juce::String& id)
{
    return id == rhythm || id == voicing || id == humanise || id == swing || id == octave || id == midiChannel;
}

bool isHarmonyParam (const juce::String& id)
{
    return id == complexity || id == mood || id == borrowed || id == scale || id == bars || id == chordCount;
}

} // namespace bounce::params
