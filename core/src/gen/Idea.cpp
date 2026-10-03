#include "bounce/gen/Idea.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace bounce::gen
{

namespace
{
constexpr std::array<std::string_view, numParts> names { "Chords", "808", "Melody", "Counter", "Drums" };
constexpr std::array<std::string_view, numParts> ids { "chords", "808", "melody", "counter", "drums" };

util::Json noteToJson (const midi::Note& n)
{
    util::Json j;
    j.set ("p", n.pitch);
    j.set ("s", n.start);
    j.set ("l", n.length);
    j.set ("v", n.velocity);
    j.set ("c", n.channel);
    return j;
}

midi::Note noteFromJson (const util::Json& j)
{
    midi::Note n;
    n.pitch = std::clamp (j["p"].asInt (60), 0, 127);
    n.start = std::max (0.0, j["s"].asNumber());
    n.length = std::max (0.01, j["l"].asNumber (0.25));
    n.velocity = std::clamp (j["v"].asInt (100), 1, 127);
    n.channel = std::clamp (j["c"].asInt (0), 0, 15);
    return n;
}

std::string seedToString (uint64_t s) { return std::to_string (s); }

uint64_t seedFromJson (const util::Json& j, uint64_t fallback)
{
    try
    {
        return j.isString() ? std::stoull (j.asString()) : fallback;
    }
    catch (...)
    {
        return fallback;
    }
}
} // namespace

std::string_view partName (Part p) { return names[static_cast<size_t> (p)]; }
std::string_view partId (Part p)   { return ids[static_cast<size_t> (p)]; }

//==============================================================================
int64_t PartEdits::toTick (double beats)
{
    return static_cast<int64_t> (std::llround (beats * 960.0));
}

void PartEdits::removeNote (const midi::Note& n)
{
    const NoteRef ref { n.pitch, toTick (n.start) };
    const auto it = std::find_if (added.begin(), added.end(), [&] (const midi::Note& a)
    {
        return a.pitch == ref.pitch && toTick (a.start) == ref.tick;
    });
    if (it != added.end())
    {
        added.erase (it);
        return;
    }
    if (std::find (removed.begin(), removed.end(), ref) == removed.end())
        removed.push_back (ref);
}

void PartEdits::addNote (const midi::Note& n)
{
    const NoteRef ref { n.pitch, toTick (n.start) };
    // Re-adding a note that was removed just restores it.
    if (const auto it = std::find (removed.begin(), removed.end(), ref); it != removed.end())
    {
        removed.erase (it);
        return;
    }
    added.push_back (n);
}

void PartEdits::apply (midi::MidiClip& clip) const
{
    if (empty())
        return;
    auto& notes = clip.notes;
    notes.erase (std::remove_if (notes.begin(), notes.end(), [this] (const midi::Note& n)
    {
        return std::find (removed.begin(), removed.end(), NoteRef { n.pitch, toTick (n.start) }) != removed.end();
    }), notes.end());
    for (auto n : added)
    {
        if (n.start >= clip.lengthBeats)
            continue;
        n.length = std::min (n.length, clip.lengthBeats - n.start);
        notes.push_back (n);
    }
    clip.sortByTime();
}

void PartEdits::applyToDrums (DrumPattern& pattern) const
{
    if (empty())
        return;
    auto& hits = pattern.hits;
    hits.erase (std::remove_if (hits.begin(), hits.end(), [this] (const DrumHit& h)
    {
        return std::find (removed.begin(), removed.end(), NoteRef { static_cast<int> (h.lane), toTick (h.start) }) != removed.end();
    }), hits.end());
    for (const auto& n : added)
    {
        if (n.pitch < 0 || n.pitch >= numDrumLanes || n.start >= pattern.lengthBeats())
            continue;
        hits.push_back ({ static_cast<DrumLane> (n.pitch), n.start, std::min (n.length, pattern.lengthBeats() - n.start), n.velocity, 0, false });
    }
    std::stable_sort (hits.begin(), hits.end(), [] (const DrumHit& a, const DrumHit& b) { return a.start < b.start; });
}

util::Json PartEdits::toJson() const
{
    util::Json j, a, r;
    a.asArray();
    r.asArray();
    for (const auto& n : added)
        a.push (noteToJson (n));
    for (const auto& ref : removed)
    {
        util::Json x;
        x.set ("p", ref.pitch);
        x.set ("t", static_cast<double> (ref.tick));
        r.push (std::move (x));
    }
    j.set ("added", std::move (a));
    j.set ("removed", std::move (r));
    return j;
}

PartEdits PartEdits::fromJson (const util::Json& j)
{
    PartEdits e;
    for (const auto& n : j["added"].asArray())
        e.added.push_back (noteFromJson (n));
    for (const auto& r : j["removed"].asArray())
        e.removed.push_back ({ r["p"].asInt(), static_cast<int64_t> (std::llround (r["t"].asNumber())) });
    return e;
}

//==============================================================================
Idea Idea::transposed (int semitones) const
{
    auto out = *this;
    out.chords = chords.transposed (semitones);
    for (auto& n : out.melodyLockedNotes)
        n.pitch = std::clamp (n.pitch + semitones, 0, 127);
    for (int p = 0; p < numParts; ++p)
    {
        if (static_cast<Part> (p) == Part::Drums)
            continue;
        auto& e = out.edits[static_cast<size_t> (p)];
        for (auto& n : e.added)
            n.pitch = std::clamp (n.pitch + semitones, 0, 127);
        for (auto& r : e.removed)
            r.pitch = std::clamp (r.pitch + semitones, 0, 127);
    }
    return out;
}

util::Json Idea::toJson() const
{
    util::Json j;
    j.set ("chords", chords.toJson());
    j.set ("bassSeed", seedToString (bassSeed));
    j.set ("melodySeed", seedToString (melodySeed));
    j.set ("counterSeed", seedToString (counterSeed));
    j.set ("drumSeed", seedToString (drumSeed));
    j.set ("melodyLockedBars", static_cast<double> (melodyLockedBars));
    util::Json locked;
    locked.asArray();
    for (const auto& n : melodyLockedNotes)
        locked.push (noteToJson (n));
    j.set ("melodyLockedNotes", std::move (locked));

    util::Json editsJson;
    editsJson.asObject();
    for (int p = 0; p < numParts; ++p)
        if (! edits[static_cast<size_t> (p)].empty())
            editsJson.set (std::string (partId (static_cast<Part> (p))), edits[static_cast<size_t> (p)].toJson());
    j.set ("edits", std::move (editsJson));
    return j;
}

std::optional<Idea> Idea::fromJson (const util::Json& j)
{
    // Milestone-1 states stored a bare progression: accept that too.
    const auto& chordsJson = j["chords"].isObject() ? j["chords"] : j;
    auto prog = Progression::fromJson (chordsJson);
    if (! prog)
        return std::nullopt;

    Idea idea;
    idea.chords = *prog;
    idea.bassSeed = seedFromJson (j["bassSeed"], prog->seed ^ 0xB455ull);
    idea.melodySeed = seedFromJson (j["melodySeed"], prog->seed ^ 0x3E10ull);
    idea.counterSeed = seedFromJson (j["counterSeed"], prog->seed ^ 0xC0C0ull);
    idea.drumSeed = seedFromJson (j["drumSeed"], prog->seed ^ 0xD5A3ull);
    idea.melodyLockedBars = static_cast<uint32_t> (j["melodyLockedBars"].asNumber());
    for (const auto& n : j["melodyLockedNotes"].asArray())
        idea.melodyLockedNotes.push_back (noteFromJson (n));
    for (int p = 0; p < numParts; ++p)
        idea.edits[static_cast<size_t> (p)] = PartEdits::fromJson (j["edits"][partId (static_cast<Part> (p))]);
    return idea;
}

} // namespace bounce::gen
