#pragma once

#include "bounce/midi/MidiClip.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bounce::midi
{

struct MidiFileOptions
{
    double bpm = 140.0;
    int ppq = 960;
    int timeSigNumerator = 4;
    int timeSigDenominator = 4;
};

/** Writes a Standard MIDI File.
    One clip  -> format 0 (single track with tempo): what FL Studio's Piano Roll likes best.
    Several   -> format 1 (tempo track + one named track per clip): drops as separate channels.
    Overlapping notes of the same pitch/channel are trimmed so note-ons/offs always pair up,
    and every track ends exactly at the clip length so loops stay bar-aligned. */
std::vector<uint8_t> writeMidiFile (const std::vector<MidiClip>& clips, const MidiFileOptions& options);

/** Minimal SMF reader (note on/off, tempo, track names). Used by tests and for importing
    the user's own MIDI later on. */
struct ParsedMidiFile
{
    int format = 0;
    int ppq = 960;
    double bpm = 120.0;
    std::vector<MidiClip> tracks;
};

std::optional<ParsedMidiFile> readMidiFile (const std::vector<uint8_t>& data);

/** "Fmaj7-Em9-Am9_Am_150bpm_chords.mid": chord names, key, tempo and part, made safe for
    Windows/macOS file systems. Long progressions are truncated to the first `maxChords`. */
std::string makeMidiFileName (const std::vector<std::string>& chordNames, const std::string& keyName,
                              double bpm, const std::string& part, size_t maxChords = 4);

} // namespace bounce::midi
