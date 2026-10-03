#pragma once

#include "bounce/midi/MidiClip.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace bounce::MidiExport
{

/** Writes clips to `file` as a Standard MIDI File (format 0 for one clip, 1 for several). */
bool writeFile (const juce::File& file, const std::vector<midi::MidiClip>& clips, double bpm);

/** Writes to <temp>/Bounce/<fileName> for an external drag (FL Studio copies the file on drop).
    Returns an invalid File on failure. */
juce::File writeTempFile (const std::vector<midi::MidiClip>& clips, double bpm, const juce::String& fileName);

/** Default export folder: ~/Documents/Bounce Exports (created on demand). */
juce::File defaultExportFolder();

} // namespace bounce::MidiExport
