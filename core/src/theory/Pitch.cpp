#include "bounce/theory/Pitch.h"

#include <array>
#include <cctype>

namespace bounce::theory
{

namespace
{
constexpr std::array<const char*, 12> sharpNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
constexpr std::array<const char*, 12> flatNames  { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
} // namespace

std::string pitchClassName (PitchClass pc, Spelling spelling)
{
    const auto i = static_cast<size_t> (wrapPc (pc));
    return spelling == Spelling::Sharps ? sharpNames[i] : flatNames[i];
}

std::optional<PitchClass> parsePitchClass (std::string_view text)
{
    if (text.empty())
        return std::nullopt;

    int pc = 0;
    switch (std::toupper (static_cast<unsigned char> (text[0])))
    {
        case 'C': pc = 0;  break;
        case 'D': pc = 2;  break;
        case 'E': pc = 4;  break;
        case 'F': pc = 5;  break;
        case 'G': pc = 7;  break;
        case 'A': pc = 9;  break;
        case 'B': pc = 11; break;
        default:  return std::nullopt;
    }

    for (size_t i = 1; i < text.size(); ++i)
    {
        const char c = text[i];
        if (c == '#')
            ++pc;
        else if (c == 'b')
            --pc;
        else
            return std::nullopt;
    }

    return wrapPc (pc);
}

std::string midiNoteName (int midiNote, Spelling spelling, int octaveOffset)
{
    const int octave = (midiNote < 0 ? (midiNote - 11) / 12 : midiNote / 12) + octaveOffset;
    return pitchClassName (midiNote, spelling) + std::to_string (octave);
}

} // namespace bounce::theory
