#include "Engine/PatternPlayer.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

namespace bounce
{

void PlaybackPattern::setFrom (const midi::MidiClip& clip, uint32_t newVersion)
{
    lengthBeats = std::max (0.25, clip.lengthBeats);
    version = newVersion;

    std::vector<Note> tmp;
    tmp.reserve (clip.notes.size());
    for (const auto& n : clip.notes)
    {
        Note p;
        p.start = std::clamp (n.start, 0.0, lengthBeats);
        // Keep note-offs strictly inside the loop so they fire before the wrap.
        p.end = std::clamp (n.start + n.length, p.start, lengthBeats - 1.0e-6);
        if (p.end <= p.start || p.start >= lengthBeats)
            continue;
        p.pitch = static_cast<uint8_t> (std::clamp (n.pitch, 0, 127));
        p.velocity = static_cast<uint8_t> (std::clamp (n.velocity, 1, 127));
        p.channel = static_cast<uint8_t> (std::clamp (n.channel, 0, 15));
        tmp.push_back (p);
    }

    std::sort (tmp.begin(), tmp.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });

    // Same key overlapping itself would make note-offs ambiguous: trim the earlier note.
    std::map<int, size_t> last;
    for (size_t i = 0; i < tmp.size(); ++i)
    {
        const int keyId = tmp[i].channel * 128 + tmp[i].pitch;
        if (auto it = last.find (keyId); it != last.end() && tmp[it->second].end > tmp[i].start)
            tmp[it->second].end = std::max (tmp[it->second].start + 1.0e-4, tmp[i].start - 1.0e-4);
        last[keyId] = i;
    }

    numNotes = static_cast<int> (std::min (tmp.size(), notes.size()));
    std::copy_n (tmp.begin(), numNotes, notes.begin());
}

//==============================================================================
void PatternPlayer::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    wasRunning = false;
    previewPpq = 0.0;
    lastLoopPos = -1.0;
    held.reset();
}

void PatternPlayer::allNotesOff (juce::MidiBuffer& out, int sampleOffset)
{
    if (held.none())
        return;
    for (int i = 0; i < 16 * 128; ++i)
    {
        if (! held.test (static_cast<size_t> (i)))
            continue;
        out.addEvent (juce::MidiMessage::noteOff (i / 128 + 1, i % 128), sampleOffset);
    }
    held.reset();
}

void PatternPlayer::chase (const PlaybackPattern& p, double loopPos, juce::MidiBuffer& out)
{
    std::bitset<16 * 128> shouldHold;
    std::array<uint8_t, 16 * 128> velocity {};
    for (int i = 0; i < p.numNotes; ++i)
    {
        const auto& n = p.notes[static_cast<size_t> (i)];
        if (n.start < loopPos && loopPos < n.end)
        {
            const size_t k = static_cast<size_t> (n.channel) * 128 + n.pitch;
            shouldHold.set (k);
            velocity[k] = n.velocity;
        }
    }

    for (size_t k = 0; k < shouldHold.size(); ++k)
    {
        const int ch = static_cast<int> (k / 128) + 1, pitch = static_cast<int> (k % 128);
        if (held.test (k) && ! shouldHold.test (k))
            out.addEvent (juce::MidiMessage::noteOff (ch, pitch), 0);
    }
    for (size_t k = 0; k < shouldHold.size(); ++k)
    {
        const int ch = static_cast<int> (k / 128) + 1, pitch = static_cast<int> (k % 128);
        if (shouldHold.test (k) && ! held.test (k))
            out.addEvent (juce::MidiMessage::noteOn (ch, pitch, velocity[k]), 0);
    }
    held = shouldHold;
}

void PatternPlayer::emitWindow (const PlaybackPattern& p, double a, double ppqPerSample, int numSamples,
                                juce::MidiBuffer& out)
{
    const double len = p.lengthBeats;
    const double b = a + numSamples * ppqPerSample;
    const auto toSample = [&] (double ppq)
    {
        return juce::jlimit (0, numSamples - 1, static_cast<int> ((ppq - a) / ppqPerSample));
    };

    const auto firstLoop = static_cast<long long> (std::floor (a / len)) - 1;
    const auto lastLoop = static_cast<long long> (std::floor (b / len));

    // Pass 1: note-offs, pass 2: note-ons, so a re-struck key releases before it re-attacks.
    for (int pass = 0; pass < 2; ++pass)
    {
        for (auto loop = firstLoop; loop <= lastLoop; ++loop)
        {
            const double base = static_cast<double> (loop) * len;
            for (int i = 0; i < p.numNotes; ++i)
            {
                const auto& n = p.notes[static_cast<size_t> (i)];
                const size_t k = static_cast<size_t> (n.channel) * 128 + n.pitch;

                if (pass == 0)
                {
                    const double off = base + n.end;
                    if (off >= a && off < b && held.test (k))
                    {
                        out.addEvent (juce::MidiMessage::noteOff (n.channel + 1, n.pitch), toSample (off));
                        held.reset (k);
                    }
                }
                else
                {
                    const double on = base + n.start;
                    if (on >= a && on < b)
                    {
                        const int pos = toSample (on);
                        if (held.test (k))
                            out.addEvent (juce::MidiMessage::noteOff (n.channel + 1, n.pitch), pos);
                        out.addEvent (juce::MidiMessage::noteOn (n.channel + 1, n.pitch, n.velocity), pos);
                        held.set (k);
                    }
                }
            }
        }
    }
}

void PatternPlayer::process (const PlaybackPattern& pattern, bool patternChanged, const Transport& t,
                             int numSamples, juce::MidiBuffer& out, bool enabled)
{
    const bool running = enabled && (t.hostPlaying || t.previewEnabled) && numSamples > 0;

    if (! running)
    {
        allNotesOff (out, 0);
        wasRunning = false;
        lastLoopPos = -1.0;
        if (! t.previewEnabled)
            previewPpq = 0.0;
        return;
    }

    const double bpm = juce::jlimit (20.0, 999.0, t.bpm);
    const double ppqPerSample = bpm / 60.0 / sampleRate;
    const double ppqStart = t.hostPlaying ? t.hostPpq : previewPpq;
    const double len = pattern.lengthBeats;

    double loopPos = std::fmod (ppqStart, len);
    if (loopPos < 0.0)
        loopPos += len;

    const bool jumped = wasRunning && std::abs (ppqStart - expectedPpq) > std::max (ppqPerSample * 8.0, 1.0e-3);
    if (! wasRunning || jumped || patternChanged)
        chase (pattern, loopPos, out);

    emitWindow (pattern, ppqStart, ppqPerSample, numSamples, out);

    expectedPpq = ppqStart + numSamples * ppqPerSample;
    if (! t.hostPlaying)
        previewPpq = expectedPpq;
    else
        previewPpq = 0.0; // a later preview starts from the top of the loop

    wasRunning = true;
    lastLoopPos = loopPos;
}

} // namespace bounce
