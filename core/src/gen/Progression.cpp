#include "bounce/gen/Progression.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace bounce::gen
{

bool Progression::hasCustomLengths() const
{
    if (customLengths.size() != slots.size() || slots.empty())
        return false;
    double sum = 0.0;
    for (double l : customLengths)
    {
        if (l <= 0.0)
            return false;
        sum += l;
    }
    return std::abs (sum - lengthBeats()) < 1e-6;
}

double Progression::slotStart (int i) const
{
    const int n = static_cast<int> (slots.size());
    if (n == 0)
        return 0.0;
    if (hasCustomLengths())
    {
        double t = 0.0;
        for (int k = 0; k < i && k < n; ++k)
            t += customLengths[static_cast<size_t> (k)];
        return i >= n ? lengthBeats() : t;
    }
    return std::round (2.0 * lengthBeats() * i / n) / 2.0;
}

void Progression::setSlotLength (int i, double beats)
{
    const int n = static_cast<int> (slots.size());
    if (i < 0 || i >= n || n < 2)
        return;

    std::vector<double> lengths;
    for (int k = 0; k < n; ++k)
        lengths.push_back (slotLength (k));

    const int donor = i < n - 1 ? i + 1 : i - 1;
    const double pool = lengths[static_cast<size_t> (i)] + lengths[static_cast<size_t> (donor)];
    beats = std::round (beats * 2.0) / 2.0;
    beats = std::clamp (beats, 0.5, pool - 0.5);
    lengths[static_cast<size_t> (i)] = beats;
    lengths[static_cast<size_t> (donor)] = pool - beats;
    customLengths = lengths;
}

double Progression::slotLength (int i) const
{
    return slotStart (i + 1) - slotStart (i);
}

int Progression::slotAt (double beat) const
{
    const int n = static_cast<int> (slots.size());
    if (n == 0)
        return -1;
    const double len = lengthBeats();
    beat = std::fmod (beat, len);
    if (beat < 0.0)
        beat += len;
    for (int i = n - 1; i >= 0; --i)
        if (beat >= slotStart (i))
            return i;
    return 0;
}

std::string Progression::chordNames() const
{
    std::string s;
    for (size_t i = 0; i < slots.size(); ++i)
    {
        if (i > 0)
            s += " - ";
        s += slots[i].chord.name (key.preferredSpelling());
    }
    return s;
}

Progression Progression::transposed (int semitones) const
{
    auto p = *this;
    p.key = key.transposed (semitones);
    for (auto& s : p.slots)
        s.chord = s.chord.transposed (semitones);
    return p;
}

util::Json Progression::toJson() const
{
    using util::Json;
    Json j;
    j.set ("tonic", key.tonic);
    j.set ("scale", std::string (theory::scaleTypeId (key.scale)));
    j.set ("bars", bars);
    j.set ("beatsPerBar", beatsPerBar);
    j.set ("seed", std::to_string (seed)); // string: JSON numbers can't hold 64 bits
    j.set ("style", styleId);
    if (hasCustomLengths())
    {
        Json lens;
        lens.asArray();
        for (double l : customLengths)
            lens.push (l);
        j.set ("lengths", std::move (lens));
    }

    Json arr;
    arr.asArray();
    for (const auto& s : slots)
    {
        Json c;
        c.set ("root", s.chord.root);
        c.set ("quality", std::string (theory::qualityId (s.chord.quality)));
        if (s.chord.bass)
            c.set ("bass", *s.chord.bass);
        c.set ("fn", s.function.symbol);
        c.set ("locked", s.locked);
        if (s.inversion)
            c.set ("inversion", *s.inversion);
        if (s.voicing)
            c.set ("voicing", std::string (theory::voicingStyleId (*s.voicing)));
        if (s.octave != 0)
            c.set ("octave", s.octave);
        arr.push (std::move (c));
    }
    j.set ("chords", std::move (arr));
    return j;
}

std::optional<Progression> Progression::fromJson (const util::Json& j)
{
    Progression p;
    p.key.tonic = theory::wrapPc (j["tonic"].asInt());
    const auto scale = theory::scaleTypeFromId (j["scale"].asString());
    if (! scale)
        return std::nullopt;
    p.key.scale = *scale;
    p.bars = j["bars"].asInt (4);
    p.beatsPerBar = j["beatsPerBar"].asInt (4);
    if (p.bars < 1 || p.bars > 64 || p.beatsPerBar < 1 || p.beatsPerBar > 16)
        return std::nullopt;

    try
    {
        p.seed = std::stoull (j["seed"].asString ("0"));
    }
    catch (...)
    {
        p.seed = 0;
    }
    p.styleId = j["style"].asString();

    for (const auto& c : j["chords"].asArray())
    {
        ChordSlot s;
        s.chord.root = theory::wrapPc (c["root"].asInt());
        const auto q = theory::qualityFromId (c["quality"].asString());
        if (! q)
            return std::nullopt;
        s.chord.quality = *q;
        if (c.has ("bass"))
            s.chord.bass = theory::wrapPc (c["bass"].asInt());

        if (auto fn = parseHarmonyToken (c["fn"].asString()))
        {
            s.function = *fn;
            s.function.borrowed = ! isDiatonicFunction (*fn, p.key);
        }

        s.locked = c["locked"].asBool();
        if (c.has ("inversion"))
            s.inversion = c["inversion"].asInt();
        if (c.has ("voicing"))
            s.voicing = theory::voicingStyleFromId (c["voicing"].asString());
        s.octave = c["octave"].asInt();
        p.slots.push_back (s);
    }

    if (p.slots.empty() || p.slots.size() > 32)
        return std::nullopt;

    for (const auto& l : j["lengths"].asArray())
        p.customLengths.push_back (l.asNumber());
    if (! p.hasCustomLengths())
        p.customLengths.clear();

    return p;
}

} // namespace bounce::gen
