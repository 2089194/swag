#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace bounce::params
{

// Parameter IDs. Never rename these: they are stored in user projects.
inline constexpr const char* key        = "key";
inline constexpr const char* scale      = "scale";
inline constexpr const char* bars       = "bars";
inline constexpr const char* chordCount = "chordCount";
inline constexpr const char* complexity = "complexity";
inline constexpr const char* mood       = "mood";
inline constexpr const char* borrowed   = "borrowed";
inline constexpr const char* rhythm     = "rhythm";
inline constexpr const char* voicing    = "voicing";
inline constexpr const char* humanise   = "humanise";
inline constexpr const char* swing      = "swing";
inline constexpr const char* octave     = "octave";

inline constexpr const char* chordsLevel = "chordsLevel";
inline constexpr const char* chordsMute  = "chordsMute";
inline constexpr const char* internalSound = "internalSound";
inline constexpr const char* midiOut     = "midiOut";
inline constexpr const char* midiChannel = "midiChannel";
inline constexpr const char* preview     = "preview";
inline constexpr const char* tempoView   = "tempoView";

/** Bar-count choices exposed by the "bars" parameter. */
inline constexpr int barChoices[] = { 2, 4, 8 };

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

/** Parameters that only change how the current progression is played (re-render, no regen). */
bool isPerformanceParam (const juce::String& id);

/** Parameters that reshape the progression with the same seed (debounced regenerate). */
bool isHarmonyParam (const juce::String& id);

} // namespace bounce::params
