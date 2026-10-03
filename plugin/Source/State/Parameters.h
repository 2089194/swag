#pragma once

#include "Engine/Mixer.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

namespace bounce::params
{

// Parameter IDs. Never rename these: they are stored in user projects.

// Chords (milestone 1).
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
inline constexpr const char* keysWobble = "keysWobble";

// Global I/O.
inline constexpr const char* internalSound = "internalSound";
inline constexpr const char* midiOut     = "midiOut";
inline constexpr const char* midiChannel = "midiChannel"; // chords; 808 = +1, melody = +2, counter = +3, drums = 10
inline constexpr const char* preview     = "preview";
inline constexpr const char* tempoView   = "tempoView";

// 808 / bass.
inline constexpr const char* bassMode     = "bassMode";
inline constexpr const char* bassDensity  = "bassDensity";
inline constexpr const char* bassGlide    = "bassGlide";
inline constexpr const char* bassOctaves  = "bassOctaves";
inline constexpr const char* bassLockKick = "bassLockKick";
inline constexpr const char* bassLength   = "bassLength";
inline constexpr const char* bassLowNote  = "bassLowNote";
inline constexpr const char* bassDecay    = "bassDecay";
inline constexpr const char* bassPunch    = "bassPunch";

// Melody / counter.
inline constexpr const char* melDensity    = "melDensity";
inline constexpr const char* melOctave     = "melOctave";
inline constexpr const char* melFeel       = "melFeel";
inline constexpr const char* melRepetition = "melRepetition";
inline constexpr const char* melCatchiness = "melCatchiness";
inline constexpr const char* melCallResponse = "melCallResponse";
inline constexpr const char* melPentatonic = "melPentatonic";
inline constexpr const char* melCounter    = "melCounter";
inline constexpr const char* counterDensity = "counterDensity";
inline constexpr const char* leadType      = "leadType";

// Drums.
inline constexpr const char* drumKick     = "drumKick";
inline constexpr const char* drumSwing    = "drumSwing";
inline constexpr const char* drumHumanise = "drumHumanise";
inline constexpr const char* drumBounce   = "drumBounce";
inline constexpr const char* drumRolls    = "drumRolls";
inline constexpr const char* drumRoll16   = "drumRoll16";
inline constexpr const char* drumRoll16T  = "drumRoll16T";
inline constexpr const char* drumRoll32   = "drumRoll32";
inline constexpr const char* drumRoll32T  = "drumRoll32T";
inline constexpr const char* drumRollPitch = "drumRollPitch";
inline constexpr const char* drumRollCurve = "drumRollCurve";
inline constexpr const char* drumOpenHat  = "drumOpenHat";
inline constexpr const char* drumPerc     = "drumPerc";
inline constexpr const char* drumFx       = "drumFx";
inline constexpr const char* drumPitchNotes = "drumPitchNotes";
inline constexpr const char* drumMap      = "drumMap"; // GM/FPC or Custom
/** Custom per-lane notes: "drumNote0".."drumNote6". */
juce::String drumNoteId (int lane);

// Mixer strips: "<part><Field>", e.g. "chordsLevel", "bassDrive", "drumsReverb".
juce::String mixId (AudioPart part, const char* field);
inline constexpr const char* fLevel  = "Level";
inline constexpr const char* fMute   = "Mute";
inline constexpr const char* fSolo   = "Solo";
inline constexpr const char* fDrive  = "Drive";
inline constexpr const char* fTone   = "Tone";
inline constexpr const char* fDelay  = "Delay";
inline constexpr const char* fReverb = "Reverb";

/** Bar-count choices exposed by the "bars" parameter. */
inline constexpr int barChoices[] = { 2, 4, 8 };

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

/** Parameter groups, used by Session to decide what to recompute. */
bool isChordHarmonyParam (const juce::String& id);  // regenerate chords (same seed)
bool affectsRendering (const juce::String& id);     // re-render parts (no new seeds)

} // namespace bounce::params
