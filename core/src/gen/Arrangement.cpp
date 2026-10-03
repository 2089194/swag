#include "bounce/gen/Arrangement.h"

#include <algorithm>
#include <cmath>

namespace bounce::gen
{

int Arrangement::totalBars() const
{
    int bars = 0;
    for (const auto& s : sections)
        bars += std::max (0, s.bars);
    return bars;
}

Arrangement Arrangement::defaultTemplate()
{
    Arrangement a;
    Section intro { "Intro", 8, { true, false, true, false, false }, true, false };
    Section hook { "Hook", 16, { true, true, true, true, true }, false, false };
    Section verse { "Verse", 16, { true, true, false, true, true }, false, true };
    Section bridge { "Bridge", 8, { true, false, true, true, false }, false, false };
    Section outro { "Outro", 8, { true, false, true, false, false }, false, false };
    a.sections = { intro, hook, verse, hook, bridge, outro };
    return a;
}

util::Json Arrangement::toJson() const
{
    util::Json arr;
    arr.asArray();
    for (const auto& s : sections)
    {
        util::Json j, parts;
        j.set ("name", s.name);
        j.set ("bars", s.bars);
        for (int p = 0; p < numParts; ++p)
            parts.set (std::string (partId (static_cast<Part> (p))), s.parts[static_cast<size_t> (p)]);
        j.set ("parts", std::move (parts));
        j.set ("filterSweep", s.filterSweep);
        j.set ("dropLastBar", s.dropLastBar);
        arr.push (std::move (j));
    }
    return arr;
}

Arrangement Arrangement::fromJson (const util::Json& j)
{
    Arrangement a;
    for (const auto& item : j.asArray())
    {
        Section s;
        s.name = item["name"].asString ("Section");
        s.bars = std::clamp (item["bars"].asInt (8), 1, 64);
        for (int p = 0; p < numParts; ++p)
            s.parts[static_cast<size_t> (p)] = item["parts"][partId (static_cast<Part> (p))].asBool (true);
        s.filterSweep = item["filterSweep"].asBool();
        s.dropLastBar = item["dropLastBar"].asBool();
        a.sections.push_back (s);
        if (a.sections.size() >= 16)
            break;
    }
    if (a.sections.empty())
        a = defaultTemplate();
    return a;
}

std::vector<midi::MidiClip> renderArrangement (const Arrangement& arr, const std::array<midi::MidiClip, numParts>& loops,
                                               int beatsPerBar)
{
    const double total = arr.totalBars() * static_cast<double> (beatsPerBar);
    std::vector<midi::MidiClip> out (static_cast<size_t> (numParts));
    for (int p = 0; p < numParts; ++p)
    {
        out[static_cast<size_t> (p)].name = loops[static_cast<size_t> (p)].name.empty()
                                                ? std::string (partName (static_cast<Part> (p)))
                                                : loops[static_cast<size_t> (p)].name;
        out[static_cast<size_t> (p)].lengthBeats = total;
    }

    double offset = 0.0;
    for (const auto& s : arr.sections)
    {
        const double secLen = std::max (0, s.bars) * static_cast<double> (beatsPerBar);
        out[0].markers.push_back ({ offset, s.name });

        for (int p = 0; p < numParts; ++p)
        {
            const auto part = static_cast<Part> (p);
            if (! s.parts[static_cast<size_t> (p)] || loops[static_cast<size_t> (p)].notes.empty())
                continue;

            double playable = secLen;
            if (s.dropLastBar && (part == Part::Drums || part == Part::Bass) && s.bars > 1)
                playable -= beatsPerBar;

            const auto tiled = loops[static_cast<size_t> (p)].tiled (playable);
            auto& dst = out[static_cast<size_t> (p)];
            for (auto n : tiled.notes)
            {
                n.start += offset;
                dst.notes.push_back (n);
            }
            for (auto c : tiled.controls)
            {
                c.time += offset;
                dst.controls.push_back (c);
            }

            if (s.filterSweep && part != Part::Drums && part != Part::Bass)
            {
                const int channel = tiled.notes.empty() ? 0 : tiled.notes.front().channel;
                const int steps = std::max (2, s.bars * 4);
                for (int k = 0; k <= steps; ++k)
                {
                    const double t = offset + secLen * k / steps;
                    if (t >= total)
                        break;
                    dst.controls.push_back ({ t, 74, static_cast<int> (std::lround (20.0 + 107.0 * k / steps)), channel });
                }
            }
        }
        if (s.filterSweep)
            out[0].markers.push_back ({ offset, s.name + ": filter sweep" });
        offset += secLen;
    }

    for (auto& c : out)
        c.sortByTime();
    return out;
}

} // namespace bounce::gen
