#include <doctest/doctest.h>

#include "bounce/theory/Chord.h"
#include "bounce/theory/Scale.h"

using namespace bounce::theory;

TEST_CASE ("pitch class names and parsing")
{
    CHECK (pitchClassName (1, Spelling::Sharps) == "C#");
    CHECK (pitchClassName (1, Spelling::Flats) == "Db");
    CHECK (pitchClassName (-1, Spelling::Sharps) == "B");
    CHECK (parsePitchClass ("Bb") == 10);
    CHECK (parsePitchClass ("f#") == 6);
    CHECK (parsePitchClass ("Cb") == 11);
    CHECK (parsePitchClass ("E#") == 5);
    CHECK_FALSE (parsePitchClass ("H").has_value());
    CHECK_FALSE (parsePitchClass ("").has_value());
    CHECK (midiNoteName (60, Spelling::Sharps) == "C4");
    CHECK (midiNoteName (60, Spelling::Sharps, 0) == "C5"); // FL Studio naming
}

TEST_CASE ("scales")
{
    const Key cMajor { 0, ScaleType::Major };
    CHECK (cMajor.pitchClasses() == std::vector<int> { 0, 2, 4, 5, 7, 9, 11 });
    CHECK (cMajor.contains (4));
    CHECK_FALSE (cMajor.contains (3));

    const Key aMinor { 9, ScaleType::NaturalMinor };
    CHECK (aMinor.mask() == cMajor.mask());
    CHECK (aMinor.relative() == cMajor);
    CHECK (cMajor.relative() == aMinor);
    CHECK (aMinor.name() == "Am");
    CHECK (Key { 5, ScaleType::Major }.preferredSpelling() == Spelling::Flats);
    CHECK (Key { 2, ScaleType::NaturalMinor }.preferredSpelling() == Spelling::Flats); // Dm: Bb
    CHECK (Key { 4, ScaleType::NaturalMinor }.preferredSpelling() == Spelling::Sharps); // Em: F#
    CHECK (Key { 10, ScaleType::Major }.name() == "Bb");
    CHECK (Key { 6, ScaleType::Dorian }.name() == "F# Dorian");

    CHECK (Key { 9, ScaleType::MinorPentatonic }.pitchClasses() == std::vector<int> { 9, 0, 2, 4, 7 });
    CHECK (Key { 9, ScaleType::MinorPentatonic }.heptatonicParent() == aMinor);
    CHECK (aMinor.degreeOf (4) == 4);
    CHECK_FALSE (aMinor.degreeOf (1).has_value());

    for (int i = 0; i < static_cast<int> (ScaleType::NumTypes); ++i)
    {
        const auto t = static_cast<ScaleType> (i);
        CHECK (scaleTypeFromId (scaleTypeId (t)) == t);
    }
}

TEST_CASE ("chord spelling")
{
    CHECK (Chord { 5, ChordQuality::Major7, {} }.name (Spelling::Flats) == "Fmaj7");
    CHECK (Chord { 4, ChordQuality::Minor9, {} }.name (Spelling::Sharps) == "Em9");
    CHECK (Chord { 8, ChordQuality::SixNine, {} }.name (Spelling::Flats) == "Ab6/9");
    CHECK (Chord { 0, ChordQuality::Major, 4 }.name (Spelling::Sharps) == "C/E");

    CHECK (Chord { 9, ChordQuality::Minor9, {} }.pitchClasses() == std::vector<int> { 9, 0, 4, 7, 11 });
    CHECK (Chord { 5, ChordQuality::Major7, {} }.pitchClasses() == std::vector<int> { 5, 9, 0, 4 });
    CHECK (Chord { 0, ChordQuality::Major, 4 }.pitchClasses() == std::vector<int> { 0, 4, 7 }); // bass already a chord tone
    CHECK (Chord { 0, ChordQuality::Major, 2 }.pitchClasses() == std::vector<int> { 0, 4, 7, 2 });

    CHECK (Chord { 10, ChordQuality::Dominant7, {} }.transposed (3) == Chord { 1, ChordQuality::Dominant7, {} });
}

TEST_CASE ("chord parsing round-trips every quality")
{
    for (int i = 0; i < static_cast<int> (ChordQuality::NumQualities); ++i)
    {
        const auto q = static_cast<ChordQuality> (i);
        for (int root = 0; root < 12; ++root)
        {
            const Chord c { root, q, {} };
            for (auto sp : { Spelling::Sharps, Spelling::Flats })
            {
                const auto parsed = parseChord (c.name (sp));
                REQUIRE_MESSAGE (parsed.has_value(), c.name (sp));
                CHECK (*parsed == c);
            }
        }
        CHECK (qualityFromId (qualityId (q)) == q);
        CHECK (qualityFromIntervalSet (qualityIntervals (q)) == q);
    }

    CHECK (parseChord ("C/E") == Chord { 0, ChordQuality::Major, 4 });
    CHECK (parseChord ("Bbm7/Ab") == Chord { 10, ChordQuality::Minor7, 8 });
    CHECK (parseChord ("Cmin7") == Chord { 0, ChordQuality::Minor7, {} });
    CHECK (parseChord ("F69") == Chord { 5, ChordQuality::SixNine, {} });
    CHECK_FALSE (parseChord ("Xmaj7").has_value());
    CHECK_FALSE (parseChord ("Cfoo").has_value());
}

TEST_CASE ("diatonic chords stack scale thirds")
{
    const Key c { 0, ScaleType::Major };
    CHECK (diatonicChord (c, 0, 4) == Chord { 0, ChordQuality::Major7, {} });
    CHECK (diatonicChord (c, 1, 4) == Chord { 2, ChordQuality::Minor7, {} });
    CHECK (diatonicChord (c, 4, 4) == Chord { 7, ChordQuality::Dominant7, {} });
    CHECK (diatonicChord (c, 6, 4) == Chord { 11, ChordQuality::HalfDiminished7, {} });
    CHECK (diatonicChord (c, 3, 5) == Chord { 5, ChordQuality::Major9, {} });
    CHECK (diatonicChord (c, 5, 5) == Chord { 9, ChordQuality::Minor9, {} });
    // iii9 would be a m7b9: not in the table, so it falls back to m7.
    CHECK (diatonicChord (c, 2, 5) == Chord { 4, ChordQuality::Minor7, {} });

    const Key am { 9, ScaleType::NaturalMinor };
    CHECK (diatonicChord (am, 0, 3) == Chord { 9, ChordQuality::Minor, {} });
    CHECK (diatonicChord (am, 5, 4) == Chord { 5, ChordQuality::Major7, {} });

    const Key aHarm { 9, ScaleType::HarmonicMinor };
    CHECK (diatonicChord (aHarm, 4, 3) == Chord { 4, ChordQuality::Major, {} });

    // Every diatonic chord in every key/mode really is diatonic.
    for (int i = 0; i < static_cast<int> (ScaleType::NumTypes); ++i)
        for (int tonic = 0; tonic < 12; ++tonic)
            for (int d = 0; d < 7; ++d)
                for (int tones = 3; tones <= 6; ++tones)
                {
                    const Key k { tonic, static_cast<ScaleType> (i) };
                    CHECK (diatonicChord (k, d, tones).isDiatonicTo (k.heptatonicParent()));
                }
}

TEST_CASE ("roman numerals")
{
    const Key c { 0, ScaleType::Major };
    CHECK (romanNumeral ({ 0, ChordQuality::Major7, {} }, c) == "Imaj7");
    CHECK (romanNumeral ({ 9, ChordQuality::Minor9, {} }, c) == "vi9");
    CHECK (romanNumeral ({ 5, ChordQuality::Minor, {} }, c) == "iv");
    CHECK (romanNumeral ({ 8, ChordQuality::Major, {} }, c) == "bVI");
    CHECK (romanNumeral ({ 10, ChordQuality::Add9, {} }, c) == "bVIIadd9");
    CHECK (romanNumeral ({ 11, ChordQuality::HalfDiminished7, {} }, c) == "vii\xC3\xB8" "7");
    CHECK (romanNumeral ({ 7, ChordQuality::Dominant7, {} }, c) == "V7");
    CHECK (romanNumeral ({ 0, ChordQuality::Major, 4 }, c) == "I/E");

    const Key am { 9, ScaleType::NaturalMinor };
    CHECK (romanNumeral ({ 9, ChordQuality::Minor9, {} }, am) == "i9");
    CHECK (romanNumeral ({ 5, ChordQuality::Major7, {} }, am) == "bVImaj7");
    CHECK (romanNumeral ({ 7, ChordQuality::Major, {} }, am) == "bVII");
    CHECK (romanNumeral ({ 0, ChordQuality::Major, {} }, am) == "bIII");
    CHECK (romanNumeral ({ 2, ChordQuality::MinorAdd9, {} }, am) == "ivadd9");
    CHECK (romanNumeral ({ 11, ChordQuality::Diminished, {} }, am) == "ii\xC2\xB0");
}
