#include "Analysis/LabModel.h"

namespace bounce
{

LabModel::LabModel (juce::AudioFormatManager& f) : juce::Thread ("Bounce Lab analysis"), formats (f) {}

LabModel::~LabModel()
{
    stopThread (4000);
}

void LabModel::startJob()
{
    stopThread (4000);
    progress = 0.0;
    status = Status::Working;
    statusText = "Analysing...";
    sendChangeMessage();
    startThread (juce::Thread::Priority::low);
}

void LabModel::analyseFile (const juce::File& file)
{
    stopThread (4000);
    pendingFile = file;
    pendingBuffer.setSize (0, 0);
    sourceName = file.getFileName();
    startJob();
}

void LabModel::analyseBuffer (juce::AudioBuffer<float> mono, double sampleRate, const juce::String& name)
{
    stopThread (4000);
    pendingFile = juce::File();
    pendingBuffer = std::move (mono);
    pendingRate = sampleRate;
    sourceName = name;
    startJob();
}

void LabModel::cancel()
{
    stopThread (4000);
    if (status == Status::Working)
    {
        status = Status::Empty;
        statusText = "Cancelled.";
        sendChangeMessage();
    }
}

void LabModel::run()
{
    std::vector<float> mono;
    double rate = pendingRate;
    juce::String error;

    if (pendingFile != juce::File())
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (pendingFile));
        if (reader == nullptr || reader->sampleRate <= 0.0)
            error = "Couldn't read " + pendingFile.getFileName() + " (unsupported format?)";
        else
        {
            rate = reader->sampleRate;
            const auto total = std::min<juce::int64> (reader->lengthInSamples, static_cast<juce::int64> (rate * 600.0));
            mono.reserve (static_cast<size_t> (total));
            const int chunk = 65536;
            juce::AudioBuffer<float> tmp (static_cast<int> (reader->numChannels), chunk);
            for (juce::int64 pos = 0; pos < total && ! threadShouldExit(); pos += chunk)
            {
                const int n = static_cast<int> (std::min<juce::int64> (chunk, total - pos));
                reader->read (&tmp, 0, n, pos, true, true);
                for (int i = 0; i < n; ++i)
                {
                    float s = 0.0f;
                    for (int ch = 0; ch < tmp.getNumChannels(); ++ch)
                        s += tmp.getSample (ch, i);
                    mono.push_back (s / static_cast<float> (juce::jmax (1, tmp.getNumChannels())));
                }
                progress = 0.1 * static_cast<double> (pos) / static_cast<double> (std::max<juce::int64> (1, total));
            }
        }
    }
    else
    {
        mono.assign (pendingBuffer.getReadPointer (0), pendingBuffer.getReadPointer (0) + pendingBuffer.getNumSamples());
    }

    if (threadShouldExit())
        return;

    if (error.isEmpty() && mono.size() < static_cast<size_t> (rate * 2.0))
        error = "Need at least 2 seconds of audio.";

    analysis::AnalysisResult r;
    std::vector<std::pair<float, float>> peaks;
    if (error.isEmpty())
    {
        r = analysis::analyse (mono.data(), mono.size(), rate, {}, [this] (double p)
        {
            progress = 0.1 + 0.9 * p;
            return ! threadShouldExit();
        });
        if (threadShouldExit())
            return;

        const size_t bins = 600;
        const size_t per = std::max<size_t> (1, mono.size() / bins);
        for (size_t b = 0; b * per < mono.size() && peaks.size() < bins; ++b)
        {
            float lo = 0.0f, hi = 0.0f;
            for (size_t i = b * per; i < std::min (mono.size(), (b + 1) * per); ++i)
            {
                lo = std::min (lo, mono[i]);
                hi = std::max (hi, mono[i]);
            }
            peaks.emplace_back (lo, hi);
        }
    }

    juce::MessageManager::callAsync ([safe = juce::WeakReference<LabModel> (this), r = std::move (r), peaks = std::move (peaks), error]() mutable
    {
        if (safe == nullptr)
            return;
        auto& m = *safe;
        if (error.isNotEmpty())
        {
            m.status = Status::Failed;
            m.statusText = error;
        }
        else
        {
            m.result = std::move (r);
            m.overview = std::move (peaks);
            m.status = Status::Ready;
            m.keyChoice = 0;
            m.tempoChoice = 0;
            m.speedSemitones = 0.0;
            m.statusText = "Done: " + juce::String (m.result.chords.size()) + " chord changes in "
                         + juce::String (m.result.durationSeconds, 1) + " s.";
        }
        m.progress = 1.0;
        m.sendChangeMessage();
    });
}

//==============================================================================
void LabModel::setKeyChoice (int c)
{
    keyChoice = juce::jlimit (0, 3, c);
    sendChangeMessage();
}

theory::Key LabModel::getChosenKey() const
{
    if (result.keys.empty())
        return {};
    if (keyChoice == 3)
        return result.keys.front().key.relative();
    return result.keys[static_cast<size_t> (juce::jlimit (0, static_cast<int> (result.keys.size()) - 1, keyChoice))].key;
}

void LabModel::setTempoChoice (int c)
{
    tempoChoice = juce::jlimit (0, 2, c);
    sendChangeMessage();
}

double LabModel::getChosenBpm() const
{
    const double bpm = result.tempo.bpm;
    return tempoChoice == 1 ? bpm / 2.0 : tempoChoice == 2 ? bpm * 2.0 : bpm;
}

void LabModel::setSpeedSemitones (double st)
{
    speedSemitones = juce::jlimit (-12.0, 12.0, st);
    sendChangeMessage();
}

analysis::SpeedChange LabModel::getSpeedChange() const
{
    return analysis::speedBySemitones (getChosenKey(), getChosenBpm(), speedSemitones);
}

void LabModel::setChord (int index, const theory::Chord& chord)
{
    if (index < 0 || index >= static_cast<int> (result.chords.size()))
        return;
    result.chords[static_cast<size_t> (index)].chord = chord;
    sendChangeMessage();
}

void LabModel::deleteChord (int index)
{
    auto& c = result.chords;
    if (index < 0 || index >= static_cast<int> (c.size()) || c.size() < 2)
        return;
    if (index > 0)
        c[static_cast<size_t> (index - 1)].end = c[static_cast<size_t> (index)].end;
    else
        c[1].start = c[0].start;
    c.erase (c.begin() + index);
    sendChangeMessage();
}

gen::Progression LabModel::makeProgression (int bars, int startBar) const
{
    // Work in the analysed tempo: the chord times are in the original audio.
    const double bpm = result.tempo.bpm > 0.0 ? getChosenBpm() : 120.0;
    const double start = result.tempo.firstBeat + startBar * 4.0 * 60.0 / bpm;

    // Speed helper: transpose the chords along with the key.
    const auto speed = getSpeedChange();
    const int shift = static_cast<int> (std::lround (speed.semitones));
    auto prog = analysis::progressionFromDetected (result.chords, getChosenKey(), bpm, start, bars);
    return shift != 0 ? prog.transposed (shift) : prog;
}

} // namespace bounce
