#pragma once

#include "bounce/analysis/AudioAnalysis.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <vector>

namespace bounce
{

/** Key & BPM Lab state. Decoding and analysis run on a background thread; results are posted
    back to the message thread, where everything else (UI, edits) happens. Never touches the
    audio thread. */
class LabModel : public juce::ChangeBroadcaster,
                 private juce::Thread
{
public:
    enum class Status
    {
        Empty,
        Working,
        Ready,
        Failed
    };

    explicit LabModel (juce::AudioFormatManager& formats);
    ~LabModel() override;

    /** Decodes and analyses a file (WAV/AIFF/FLAC/OGG/MP3), first 10 minutes. */
    void analyseFile (const juce::File& file);
    /** Analyses audio captured from the plugin input. */
    void analyseBuffer (juce::AudioBuffer<float> mono, double sampleRate, const juce::String& name);
    void cancel();

    Status getStatus() const { return status; }
    double getProgress() const { return progress.load(); }
    juce::String getStatusText() const { return statusText; }
    juce::String getSourceName() const { return sourceName; }
    const analysis::AnalysisResult& getResult() const { return result; }
    /** ~600 min/max peaks for the waveform overview. */
    const std::vector<std::pair<float, float>>& getOverview() const { return overview; }

    //== User choices on top of the analysis =====================================
    int getKeyChoice() const { return keyChoice; }        // 0..2 = candidates, 3 = relative of #0
    void setKeyChoice (int c);
    theory::Key getChosenKey() const;

    int getTempoChoice() const { return tempoChoice; }    // 0 normal, 1 half, 2 double
    void setTempoChoice (int c);
    double getChosenBpm() const;

    double getSpeedSemitones() const { return speedSemitones; }
    void setSpeedSemitones (double st);
    analysis::SpeedChange getSpeedChange() const;

    /** Editable chord lane. */
    void setChord (int index, const theory::Chord& chord);
    void deleteChord (int index); // merges into its neighbour

    /** Progression for the generator from `bars` bars starting at loop bar `startBar`
        (counted from the first detected downbeat). */
    gen::Progression makeProgression (int bars, int startBar) const;

private:
    juce::AudioFormatManager& formats;

    // Job input (set before startThread, read by the worker).
    juce::File pendingFile;
    juce::AudioBuffer<float> pendingBuffer;
    double pendingRate = 0.0;

    std::atomic<double> progress { 0.0 };
    Status status = Status::Empty;
    juce::String statusText { "Drop a WAV / MP3 / FLAC here, or capture the plugin input." };
    juce::String sourceName;
    analysis::AnalysisResult result;
    std::vector<std::pair<float, float>> overview;
    int keyChoice = 0, tempoChoice = 0;
    double speedSemitones = 0.0;

    void run() override;
    void startJob();

    JUCE_DECLARE_WEAK_REFERENCEABLE (LabModel)
};

} // namespace bounce
