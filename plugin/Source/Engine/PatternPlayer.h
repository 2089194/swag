#pragma once

#include "bounce/midi/MidiClip.h"
#include "bounce/util/TripleBuffer.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <bitset>
#include <cstdint>

namespace bounce
{

/** Fixed-size, allocation-free snapshot of one part's loop, built on the message thread and
    handed to the audio thread through a TripleBuffer. */
struct PlaybackPattern
{
    static constexpr int maxNotes = 1024;

    struct Note
    {
        double start = 0.0, end = 0.0; // beats, 0 <= start < end <= lengthBeats
        uint8_t pitch = 60, velocity = 100, channel = 0;
    };

    double lengthBeats = 16.0;
    int numNotes = 0;
    uint32_t version = 0;
    std::array<Note, maxNotes> notes {};

    /** Message thread: fills from a clip (notes beyond maxNotes are dropped). */
    void setFrom (const midi::MidiClip& clip, uint32_t newVersion);
};

/** Turns a looping pattern into sample-accurate MIDI, following the host transport.

    - When the host plays, the loop is locked to the host's PPQ position, so it always starts
      on the bar (beat 0 of the loop = any multiple of the loop length).
    - When the host is stopped and preview is on, an internal clock runs from the loop start.
    - Notes that are already sounding when playback starts or the pattern changes are chased,
      so editing chords while looping doesn't leave holes.
    - Every note-on has a matching note-off (stops, jumps, pattern swaps and disable all flush).
    Real-time safe: no locks, no allocation (the MidiBuffer is pre-sized in prepare()). */
class PatternPlayer
{
public:
    void prepare (double sampleRate);

    struct Transport
    {
        bool hostPlaying = false;
        double hostPpq = 0.0;
        double bpm = 120.0;
        bool previewEnabled = false;
    };

    /** Audio thread. Adds this block's events to `out`. Pass enabled = false to silence. */
    void process (const PlaybackPattern& pattern, bool patternChanged, const Transport& transport,
                  int numSamples, juce::MidiBuffer& out, bool enabled);

    /** Audio thread: current loop position in beats, or -1 when idle. */
    double getLoopPosition() const noexcept { return lastLoopPos; }

    /** Audio thread: releases everything currently held. */
    void allNotesOff (juce::MidiBuffer& out, int sampleOffset);

private:
    double sampleRate = 44100.0;
    bool wasRunning = false;
    double expectedPpq = 0.0;
    double previewPpq = 0.0;
    double lastLoopPos = -1.0;

    std::bitset<16 * 128> held;

    void chase (const PlaybackPattern& p, double loopPos, juce::MidiBuffer& out);
    void emitWindow (const PlaybackPattern& p, double ppqStart, double ppqPerSample, int numSamples, juce::MidiBuffer& out);
};

} // namespace bounce
