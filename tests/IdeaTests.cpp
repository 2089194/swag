#include <doctest/doctest.h>

#include "bounce/gen/Arrangement.h"
#include "bounce/gen/ChordGenerator.h"
#include "bounce/gen/Idea.h"
#include "bounce/midi/MidiFile.h"

#include <algorithm>

using namespace bounce;
using namespace bounce::gen;

TEST_CASE ("part edits: remove generated, add, restore")
{
    midi::MidiClip clip;
    clip.lengthBeats = 4.0;
    clip.notes = { { 60, 0.0, 1.0, 100, 0 }, { 62, 1.0, 1.0, 100, 0 } };

    PartEdits e;
    e.removeNote (clip.notes[0]);
    e.addNote ({ 65, 2.0, 1.0, 90, 0 });
    auto edited = clip;
    e.apply (edited);
    REQUIRE (edited.notes.size() == 2);
    CHECK (edited.notes[0].pitch == 62);
    CHECK (edited.notes[1].pitch == 65);

    e.removeNote ({ 65, 2.0, 1.0, 90, 0 }); // removing an added note just drops it
    e.addNote (clip.notes[0]);              // re-adding a removed note restores it
    CHECK (e.empty());

    const auto back = PartEdits::fromJson (PartEdits { { { 70, 0.5, 0.5, 80, 2 } }, { { 61, 960 } } }.toJson());
    CHECK (back.added.size() == 1);
    CHECK (back.removed.front().tick == 960);
}

TEST_CASE ("drum edits address lanes")
{
    DrumPattern pat;
    pat.bars = 1;
    pat.hits = { { DrumLane::Kick, 0.0, 0.5, 100, 0, false }, { DrumLane::Snare, 2.0, 0.5, 100, 0, false } };
    PartEdits e;
    e.removed.push_back ({ static_cast<int> (DrumLane::Snare), PartEdits::toTick (2.0) });
    e.added.push_back ({ static_cast<int> (DrumLane::Rim), 3.5, 0.25, 70, 0 });
    e.applyToDrums (pat);
    REQUIRE (pat.hits.size() == 2);
    CHECK (pat.hits[1].lane == DrumLane::Rim);
}

TEST_CASE ("idea JSON round trip and M1 compatibility")
{
    ChordGenerator gen (StylePreset::defaults());
    Idea idea;
    idea.chords = gen.generate ({});
    idea.bassSeed = 123456789012345ull;
    idea.melodyLockedBars = 0b1010;
    idea.melodyLockedNotes = { { 72, 4.5, 0.5, 90, 2 } };
    idea.editsFor (Part::Bass).addNote ({ 40, 1.0, 0.5, 100, 1 });

    const auto back = Idea::fromJson (*util::Json::parse (idea.toJson().dump()).value);
    REQUIRE (back);
    CHECK (*back == idea);

    // A milestone-1 state stored just the progression.
    const auto legacy = Idea::fromJson (idea.chords.toJson());
    REQUIRE (legacy);
    CHECK (legacy->chords == idea.chords);

    const auto up = idea.transposed (2);
    CHECK (up.melodyLockedNotes[0].pitch == 74);
    CHECK (up.editsFor (Part::Bass).added[0].pitch == 42);
}

TEST_CASE ("arrangement tiles loops, mutes parts and adds markers")
{
    std::array<midi::MidiClip, numParts> loops;
    for (int p = 0; p < numParts; ++p)
    {
        loops[static_cast<size_t> (p)].lengthBeats = 16.0;
        loops[static_cast<size_t> (p)].notes = { { 60, 0.0, 1.0, 100, p }, { 62, 8.0, 1.0, 100, p } };
    }

    const auto arr = Arrangement::defaultTemplate();
    const auto clips = renderArrangement (arr, loops);
    REQUIRE (clips.size() == static_cast<size_t> (numParts));
    const double total = arr.totalBars() * 4.0;
    CHECK (total == 72.0 * 4.0);
    for (const auto& c : clips)
    {
        CHECK (c.lengthBeats == total);
        for (const auto& n : c.notes)
            CHECK (n.start < total);
    }

    // Intro (8 bars) has no drums; the first hook (bars 8-24) does.
    const auto& drums = clips[static_cast<size_t> (Part::Drums)];
    CHECK (std::none_of (drums.notes.begin(), drums.notes.end(), [] (const midi::Note& n) { return n.start < 32.0; }));
    CHECK (std::any_of (drums.notes.begin(), drums.notes.end(), [] (const midi::Note& n) { return n.start >= 32.0 && n.start < 96.0; }));

    // Verse drops drums + 808 in its last bar (bars 39..40 => beats 156..160).
    CHECK (std::none_of (drums.notes.begin(), drums.notes.end(), [] (const midi::Note& n) { return n.start >= 156.0 && n.start < 160.0; }));

    CHECK (clips[0].markers.size() >= arr.sections.size());
    CHECK (clips[0].markers.front().text == "Intro");
    const auto& chords = clips[static_cast<size_t> (Part::Chords)];
    CHECK (std::any_of (chords.controls.begin(), chords.controls.end(), [] (const midi::ControlChange& c) { return c.controller == 74; }));

    // Writes and reads back as a valid multi-track MIDI file with markers.
    const auto parsed = midi::readMidiFile (midi::writeMidiFile (clips, {}));
    REQUIRE (parsed);
    CHECK (parsed->tracks.size() == static_cast<size_t> (numParts) + 1);
    CHECK (parsed->tracks[1].markers.front().text == "Intro");

    const auto json = Arrangement::fromJson (arr.toJson());
    CHECK (json == arr);
}
