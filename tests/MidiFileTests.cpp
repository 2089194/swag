#include <doctest/doctest.h>

#include "bounce/midi/MidiFile.h"

#include <string>

using namespace bounce::midi;

namespace
{
MidiClip simpleClip()
{
    MidiClip c;
    c.name = "Chords";
    c.lengthBeats = 8.0;
    c.notes = {
        { 57, 0.0, 4.0, 90, 0 },
        { 60, 0.0, 4.0, 80, 0 },
        { 64, 0.0, 4.0, 85, 0 },
        { 53, 4.0, 4.0, 90, 0 },
        { 57, 4.0, 3.5, 70, 0 },
    };
    return c;
}
} // namespace

TEST_CASE ("format 0 header and tempo")
{
    MidiFileOptions o;
    o.bpm = 150.0;
    const auto bytes = writeMidiFile ({ simpleClip() }, o);
    REQUIRE (bytes.size() > 22);
    CHECK (std::string (bytes.begin(), bytes.begin() + 4) == "MThd");
    CHECK (bytes[9] == 0);   // format 0
    CHECK (bytes[11] == 1);  // one track
    CHECK (((bytes[12] << 8) | bytes[13]) == 960);
    CHECK (std::string (bytes.begin() + 14, bytes.begin() + 18) == "MTrk");
    // Last three bytes are End-of-Track.
    CHECK (bytes[bytes.size() - 3] == 0xFF);
    CHECK (bytes[bytes.size() - 2] == 0x2F);
    CHECK (bytes[bytes.size() - 1] == 0x00);

    const auto parsed = readMidiFile (bytes);
    REQUIRE (parsed);
    CHECK (parsed->bpm == doctest::Approx (150.0).epsilon (0.001));
}

TEST_CASE ("write/read round trip")
{
    const auto clip = simpleClip();
    const auto parsed = readMidiFile (writeMidiFile ({ clip }, {}));
    REQUIRE (parsed);
    REQUIRE (parsed->tracks.size() == 1);
    const auto& t = parsed->tracks[0];
    CHECK (t.name == "Chords");
    CHECK (t.lengthBeats == doctest::Approx (8.0));

    auto expected = clip;
    expected.sortByTime();
    REQUIRE (t.notes.size() == expected.notes.size());
    for (size_t i = 0; i < t.notes.size(); ++i)
    {
        CHECK (t.notes[i].pitch == expected.notes[i].pitch);
        CHECK (t.notes[i].start == doctest::Approx (expected.notes[i].start));
        CHECK (t.notes[i].length == doctest::Approx (expected.notes[i].length));
        CHECK (t.notes[i].velocity == expected.notes[i].velocity);
    }
}

TEST_CASE ("format 1 with several parts")
{
    auto bass = simpleClip();
    bass.name = "808";
    bass.lengthBeats = 16.0;
    bass.notes = { { 33, 0.0, 2.0, 100, 1 }, { 36, 14.0, 4.0, 100, 1 } }; // second note overruns the loop

    const auto bytes = writeMidiFile ({ simpleClip(), bass }, { 140.0, 480, 4, 4 });
    CHECK (bytes[9] == 1);
    CHECK (bytes[11] == 3); // tempo + two parts

    const auto parsed = readMidiFile (bytes);
    REQUIRE (parsed);
    REQUIRE (parsed->tracks.size() == 3);
    CHECK (parsed->tracks[1].name == "Chords");
    CHECK (parsed->tracks[2].name == "808");
    REQUIRE (parsed->tracks[2].notes.size() == 2);
    CHECK (parsed->tracks[2].notes[1].length == doctest::Approx (2.0)); // clipped at the loop end
    CHECK (parsed->tracks[2].notes[0].channel == 1);
    // Every track is padded to the full length so they line up in the DAW.
    for (const auto& t : parsed->tracks)
        CHECK (t.lengthBeats == doctest::Approx (16.0));
}

TEST_CASE ("overlapping same-pitch notes are trimmed so on/off pairs stay valid")
{
    MidiClip c;
    c.lengthBeats = 4.0;
    c.notes = { { 40, 0.0, 2.0, 100, 0 }, { 40, 1.0, 2.0, 100, 0 }, { 40, 1.0, 1.0, 50, 0 } };
    const auto parsed = readMidiFile (writeMidiFile ({ c }, {}));
    REQUIRE (parsed);
    const auto& n = parsed->tracks[0].notes;
    REQUIRE (n.size() == 2);
    CHECK (n[0].start == 0.0);
    CHECK (n[0].length == doctest::Approx (1.0));
    CHECK (n[1].start == doctest::Approx (1.0));
}

TEST_CASE ("reader rejects garbage")
{
    CHECK_FALSE (readMidiFile ({}).has_value());
    CHECK_FALSE (readMidiFile ({ 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, 96, 'X' }).has_value());

    auto bytes = writeMidiFile ({ simpleClip() }, {});
    bytes.resize (bytes.size() - 5); // truncated track
    CHECK_FALSE (readMidiFile (bytes).has_value());
}

TEST_CASE ("file names")
{
    CHECK (makeMidiFileName ({ "Fmaj7", "Em9", "Am9" }, "Am", 150.0, "chords") == "Fmaj7-Em9-Am9_Am_150bpm_chords.mid");
    CHECK (makeMidiFileName ({ "C/E", "Ab6/9" }, "C", 142.6, "chords") == "ConE-Ab69_C_143bpm_chords.mid");
    CHECK (makeMidiFileName ({ "A", "B", "C", "D", "E" }, "F# Dorian", 90, "all") == "A-B-C-D_F#Dorian_90bpm_all.mid");
    CHECK (makeMidiFileName ({}, "", 140, "drums") == "140bpm_drums.mid");
}
