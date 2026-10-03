#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "Engine/PatternPlayer.h"

#include <map>
#include <memory>
#include <vector>

using namespace bounce;

namespace
{
struct Ev
{
    long long sample;
    bool on;
    int note;
};

constexpr double sr = 48000.0;
constexpr double bpm = 150.0;
constexpr int block = 512;
constexpr double samplesPerBeat = sr * 60.0 / bpm;

std::unique_ptr<PlaybackPattern> makePattern (std::vector<midi::Note> notes, double length = 4.0, uint32_t version = 1)
{
    midi::MidiClip clip;
    clip.lengthBeats = length;
    clip.notes = std::move (notes);
    auto p = std::make_unique<PlaybackPattern>();
    p->setFrom (clip, version);
    return p;
}

/** Runs the player from sample `start` for `numBlocks`, collecting events with absolute times. */
void run (PatternPlayer& player, const PlaybackPattern& p, long long& samplePos, int numBlocks,
          std::vector<Ev>& out, bool playing = true, bool preview = false, bool changed = false)
{
    juce::MidiBuffer buf;
    for (int b = 0; b < numBlocks; ++b)
    {
        buf.clear();
        PatternPlayer::Transport t;
        t.hostPlaying = playing;
        t.hostPpq = static_cast<double> (samplePos) / samplesPerBeat;
        t.bpm = bpm;
        t.previewEnabled = preview;
        player.process (p, changed && b == 0, t, block, buf, true);
        for (const auto m : buf)
        {
            const auto msg = m.getMessage();
            if (msg.isNoteOn())
                out.push_back ({ samplePos + m.samplePosition, true, msg.getNoteNumber() });
            else if (msg.isNoteOff())
                out.push_back ({ samplePos + m.samplePosition, false, msg.getNoteNumber() });
        }
        samplePos += block;
    }
}

/** Every on has a matching later off; never two ons for a held key; returns keys still held. */
int checkPairing (const std::vector<Ev>& events)
{
    std::map<int, long long> held;
    for (const auto& e : events)
    {
        if (e.on)
        {
            CHECK_MESSAGE (held.count (e.note) == 0, "double note-on ", e.note, " at ", e.sample);
            held[e.note] = e.sample;
        }
        else
        {
            REQUIRE_MESSAGE (held.count (e.note) == 1, "stray note-off ", e.note, " at ", e.sample);
            CHECK (e.sample >= held[e.note]);
            held.erase (e.note);
        }
    }
    return static_cast<int> (held.size());
}
} // namespace

TEST_CASE ("loop starts on the bar and wraps sample-accurately")
{
    auto p = makePattern ({ { 60, 0.0, 1.0, 100, 0 }, { 64, 2.0, 1.5, 90, 0 } });
    PatternPlayer player;
    player.prepare (sr);

    long long pos = 0;
    std::vector<Ev> ev;
    const int blocks = static_cast<int> (3 * 4 * samplesPerBeat / block) + 2;
    run (player, *p, pos, blocks, ev);

    std::vector<long long> c4Ons;
    for (const auto& e : ev)
        if (e.on && e.note == 60)
            c4Ons.push_back (e.sample);

    REQUIRE (c4Ons.size() >= 3);
    CHECK (c4Ons[0] == 0);
    for (size_t i = 1; i < c4Ons.size(); ++i)
        CHECK (std::llabs (c4Ons[i] - static_cast<long long> (i * 4 * samplesPerBeat)) <= 1);

    CHECK (checkPairing (ev) <= 2); // whatever is sounding at the end of the run
}

TEST_CASE ("stopping the transport releases everything")
{
    auto p = makePattern ({ { 60, 0.0, 4.0, 100, 0 }, { 67, 0.0, 4.0, 100, 0 } });
    PatternPlayer player;
    player.prepare (sr);

    long long pos = 0;
    std::vector<Ev> ev;
    run (player, *p, pos, 10, ev);
    run (player, *p, pos, 1, ev, false);
    CHECK (checkPairing (ev) == 0);
}

TEST_CASE ("starting mid-loop chases notes that are already sounding")
{
    auto p = makePattern ({ { 48, 0.0, 4.0, 100, 0 } });
    PatternPlayer player;
    player.prepare (sr);

    long long pos = static_cast<long long> (1.5 * samplesPerBeat);
    std::vector<Ev> ev;
    run (player, *p, pos, 2, ev);
    REQUIRE_FALSE (ev.empty());
    CHECK (ev.front().on);
    CHECK (ev.front().note == 48);
    CHECK (ev.front().sample == static_cast<long long> (1.5 * samplesPerBeat));
}

TEST_CASE ("swapping the pattern keeps common notes and fixes the rest")
{
    auto a = makePattern ({ { 60, 0.0, 4.0, 100, 0 }, { 64, 0.0, 4.0, 100, 0 } }, 4.0, 1);
    auto b = makePattern ({ { 60, 0.0, 4.0, 100, 0 }, { 63, 0.0, 4.0, 100, 0 } }, 4.0, 2);
    PatternPlayer player;
    player.prepare (sr);

    long long pos = 0;
    std::vector<Ev> ev;
    run (player, *a, pos, 4, ev);
    const auto before = ev.size();
    run (player, *b, pos, 4, ev, true, false, true);

    // After the swap: E off, Eb on, C untouched (no retrigger).
    std::vector<Ev> after (ev.begin() + static_cast<long> (before), ev.end());
    int cEvents = 0, eOff = 0, ebOn = 0;
    for (const auto& e : after)
    {
        cEvents += e.note == 60 ? 1 : 0;
        eOff += (e.note == 64 && ! e.on) ? 1 : 0;
        ebOn += (e.note == 63 && e.on) ? 1 : 0;
    }
    CHECK (cEvents == 0);
    CHECK (eOff == 1);
    CHECK (ebOn == 1);
    run (player, *b, pos, 1, ev, false);
    CHECK (checkPairing (ev) == 0);
}

TEST_CASE ("re-struck notes release before re-attacking")
{
    // Back-to-back stabs of the same key: off and on land on the same sample, off first.
    auto p = makePattern ({ { 60, 0.0, 1.0, 100, 0 }, { 60, 1.0, 1.0, 100, 0 }, { 60, 2.0, 2.0, 100, 0 } });
    PatternPlayer player;
    player.prepare (sr);
    long long pos = 0;
    std::vector<Ev> ev;
    run (player, *p, pos, static_cast<int> (8 * samplesPerBeat / block), ev);
    run (player, *p, pos, 1, ev, false);
    CHECK (checkPairing (ev) == 0);
}

TEST_CASE ("transport jumps flush and chase")
{
    auto p = makePattern ({ { 50, 0.0, 2.0, 100, 0 }, { 55, 2.0, 2.0, 100, 0 } });
    PatternPlayer player;
    player.prepare (sr);
    long long pos = 0;
    std::vector<Ev> ev;
    run (player, *p, pos, 4, ev);
    pos = static_cast<long long> (2.5 * samplesPerBeat); // user clicks into the timeline
    run (player, *p, pos, 4, ev);
    run (player, *p, pos, 1, ev, false);
    CHECK (checkPairing (ev) == 0);

    bool sawChase = false;
    for (const auto& e : ev)
        sawChase |= e.on && e.note == 55 && e.sample == static_cast<long long> (2.5 * samplesPerBeat);
    CHECK (sawChase);
}

TEST_CASE ("preview runs without a host transport")
{
    auto p = makePattern ({ { 60, 0.0, 1.0, 100, 0 } });
    PatternPlayer player;
    player.prepare (sr);
    long long pos = 0;
    std::vector<Ev> ev;
    run (player, *p, pos, 20, ev, false, true);
    REQUIRE_FALSE (ev.empty());
    CHECK (ev.front().on);
    CHECK (ev.front().sample == 0);
}

TEST_CASE ("pattern trims same-key overlaps and keeps offs inside the loop")
{
    midi::MidiClip clip;
    clip.lengthBeats = 4.0;
    clip.notes = { { 60, 0.0, 3.0, 100, 0 }, { 60, 1.0, 5.0, 100, 0 } };
    auto p = std::make_unique<PlaybackPattern>();
    p->setFrom (clip, 1);
    REQUIRE (p->numNotes == 2);
    CHECK (p->notes[0].end < p->notes[1].start);
    CHECK (p->notes[1].end < 4.0);
}
