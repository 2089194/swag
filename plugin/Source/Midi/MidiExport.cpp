#include "Midi/MidiExport.h"

#include "bounce/midi/MidiFile.h"

namespace bounce::MidiExport
{

bool writeFile (const juce::File& file, const std::vector<midi::MidiClip>& clips, double bpm)
{
    midi::MidiFileOptions options;
    options.bpm = bpm;
    const auto bytes = midi::writeMidiFile (clips, options);

    // Write next to the target and move into place, so a half-written file is never dropped.
    const juce::TemporaryFile tmp (file);
    if (! tmp.getFile().replaceWithData (bytes.data(), bytes.size()))
        return false;
    return tmp.overwriteTargetFileWithTemporary();
}

juce::File writeTempFile (const std::vector<midi::MidiClip>& clips, double bpm, const juce::String& fileName)
{
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Bounce");
    if (! dir.createDirectory())
        return {};

    // Keep the temp folder from growing forever: drop files older than a day.
    for (const auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.mid"))
        if (f.getLastModificationTime() < juce::Time::getCurrentTime() - juce::RelativeTime::days (1))
            f.deleteFile();

    const auto file = dir.getChildFile (juce::File::createLegalFileName (fileName));
    return writeFile (file, clips, bpm) ? file : juce::File();
}

juce::File defaultExportFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Bounce Exports");
}

} // namespace bounce::MidiExport
