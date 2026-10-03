#include "bounce/midi/MidiFile.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

namespace bounce::midi
{

void MidiClip::sortByTime()
{
    std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b)
    {
        return std::tie (a.start, a.pitch) < std::tie (b.start, b.pitch);
    });
}

namespace
{
struct Event
{
    int64_t tick;
    int order;              // 0 = meta, 1 = note-off, 2 = note-on (offs first at equal ticks)
    std::vector<uint8_t> bytes;
};

void writeU16 (std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back (static_cast<uint8_t> ((v >> 8) & 0xFF));
    out.push_back (static_cast<uint8_t> (v & 0xFF));
}

void writeU32 (std::vector<uint8_t>& out, uint32_t v)
{
    for (int s = 24; s >= 0; s -= 8)
        out.push_back (static_cast<uint8_t> ((v >> s) & 0xFF));
}

void writeVarLen (std::vector<uint8_t>& out, uint32_t v)
{
    uint8_t buf[5];
    int n = 0;
    buf[n++] = static_cast<uint8_t> (v & 0x7F);
    while ((v >>= 7) != 0)
        buf[n++] = static_cast<uint8_t> ((v & 0x7F) | 0x80);
    while (n > 0)
        out.push_back (buf[--n]);
}

Event metaEvent (int64_t tick, uint8_t type, const std::vector<uint8_t>& payload)
{
    Event e { tick, 0, { 0xFF, type } };
    writeVarLen (e.bytes, static_cast<uint32_t> (payload.size()));
    e.bytes.insert (e.bytes.end(), payload.begin(), payload.end());
    return e;
}

Event textMeta (int64_t tick, uint8_t type, const std::string& text)
{
    return metaEvent (tick, type, std::vector<uint8_t> (text.begin(), text.end()));
}

std::vector<Event> tempoEvents (const MidiFileOptions& o)
{
    const auto usPerQuarter = static_cast<uint32_t> (std::lround (60'000'000.0 / std::clamp (o.bpm, 10.0, 999.0)));
    int denPow = 0;
    while ((1 << denPow) < o.timeSigDenominator && denPow < 6)
        ++denPow;

    return {
        metaEvent (0, 0x51, { static_cast<uint8_t> (usPerQuarter >> 16), static_cast<uint8_t> (usPerQuarter >> 8),
                              static_cast<uint8_t> (usPerQuarter) }),
        metaEvent (0, 0x58, { static_cast<uint8_t> (o.timeSigNumerator), static_cast<uint8_t> (denPow), 24, 8 }),
    };
}

std::vector<Event> noteEvents (const MidiClip& clip, int ppq)
{
    const auto toTick = [ppq] (double beats) { return static_cast<int64_t> (std::llround (beats * ppq)); };
    const int64_t clipEnd = toTick (clip.lengthBeats);

    // Quantise to ticks, clamp into the clip, then trim same-key overlaps.
    struct TickNote { int64_t on, off; int pitch, vel, ch; };
    std::vector<TickNote> tn;
    for (const auto& n : clip.notes)
    {
        int64_t on = std::max<int64_t> (0, toTick (n.start));
        int64_t off = std::min (clipEnd, toTick (n.start + n.length));
        if (on >= clipEnd || off <= on)
            continue;
        tn.push_back ({ on, off, std::clamp (n.pitch, 0, 127), std::clamp (n.velocity, 1, 127), std::clamp (n.channel, 0, 15) });
    }
    std::sort (tn.begin(), tn.end(), [] (const TickNote& a, const TickNote& b) { return std::tie (a.on, a.pitch) < std::tie (b.on, b.pitch); });

    std::map<std::pair<int, int>, size_t> lastByKey;
    std::vector<bool> drop (tn.size(), false);
    for (size_t i = 0; i < tn.size(); ++i)
    {
        const auto keyId = std::make_pair (tn[i].ch, tn[i].pitch);
        if (auto it = lastByKey.find (keyId); it != lastByKey.end())
        {
            auto& prev = tn[it->second];
            if (prev.on == tn[i].on)
                drop[it->second] = true; // exact duplicate start: keep the later one
            else if (prev.off > tn[i].on)
                prev.off = tn[i].on;
        }
        lastByKey[keyId] = i;
    }

    std::vector<Event> events;
    for (size_t i = 0; i < tn.size(); ++i)
    {
        if (drop[i])
            continue;
        const auto& n = tn[i];
        events.push_back ({ n.on,  2, { static_cast<uint8_t> (0x90 | n.ch), static_cast<uint8_t> (n.pitch), static_cast<uint8_t> (n.vel) } });
        events.push_back ({ n.off, 1, { static_cast<uint8_t> (0x80 | n.ch), static_cast<uint8_t> (n.pitch), 64 } });
    }
    return events;
}

void appendTrack (std::vector<uint8_t>& out, std::vector<Event> events, int64_t endTick)
{
    std::stable_sort (events.begin(), events.end(), [] (const Event& a, const Event& b)
    {
        return std::tie (a.tick, a.order) < std::tie (b.tick, b.order);
    });

    std::vector<uint8_t> body;
    int64_t now = 0;
    for (const auto& e : events)
    {
        writeVarLen (body, static_cast<uint32_t> (e.tick - now));
        body.insert (body.end(), e.bytes.begin(), e.bytes.end());
        now = e.tick;
    }

    writeVarLen (body, static_cast<uint32_t> (std::max<int64_t> (0, endTick - now)));
    body.insert (body.end(), { 0xFF, 0x2F, 0x00 });

    out.insert (out.end(), { 'M', 'T', 'r', 'k' });
    writeU32 (out, static_cast<uint32_t> (body.size()));
    out.insert (out.end(), body.begin(), body.end());
}
} // namespace

std::vector<uint8_t> writeMidiFile (const std::vector<MidiClip>& clips, const MidiFileOptions& options)
{
    const int ppq = std::clamp (options.ppq, 24, 32767);
    double lengthBeats = 0.0;
    for (const auto& c : clips)
        lengthBeats = std::max (lengthBeats, c.lengthBeats);
    const auto endTick = static_cast<int64_t> (std::llround (lengthBeats * ppq));

    const bool single = clips.size() <= 1;
    std::vector<uint8_t> out { 'M', 'T', 'h', 'd' };
    writeU32 (out, 6);
    writeU16 (out, single ? 0 : 1);
    writeU16 (out, static_cast<uint32_t> (single ? 1 : clips.size() + 1));
    writeU16 (out, static_cast<uint32_t> (ppq));

    if (single)
    {
        auto events = tempoEvents (options);
        if (! clips.empty())
        {
            events.push_back (textMeta (0, 0x03, clips.front().name));
            auto notes = noteEvents (clips.front(), ppq);
            events.insert (events.end(), notes.begin(), notes.end());
        }
        appendTrack (out, std::move (events), endTick);
        return out;
    }

    auto tempoTrack = tempoEvents (options);
    tempoTrack.push_back (textMeta (0, 0x03, "Bounce"));
    appendTrack (out, std::move (tempoTrack), endTick);

    for (const auto& clip : clips)
    {
        std::vector<Event> events { textMeta (0, 0x03, clip.name) };
        auto notes = noteEvents (clip, ppq);
        events.insert (events.end(), notes.begin(), notes.end());
        appendTrack (out, std::move (events), endTick);
    }
    return out;
}

//==============================================================================
std::optional<ParsedMidiFile> readMidiFile (const std::vector<uint8_t>& d)
{
    size_t pos = 0;
    auto need = [&] (size_t n) { return pos + n <= d.size(); };
    auto u16 = [&] { uint32_t v = (uint32_t (d[pos]) << 8) | d[pos + 1]; pos += 2; return v; };
    auto u32 = [&] { uint32_t v = (uint32_t (d[pos]) << 24) | (uint32_t (d[pos + 1]) << 16) | (uint32_t (d[pos + 2]) << 8) | d[pos + 3]; pos += 4; return v; };

    if (! need (14) || std::string (d.begin(), d.begin() + 4) != "MThd")
        return std::nullopt;
    pos = 4;
    const auto headerLen = u32();
    if (headerLen < 6 || ! need (headerLen))
        return std::nullopt;

    ParsedMidiFile f;
    f.format = static_cast<int> (u16());
    const auto numTracks = u16();
    const auto division = u16();
    if (division & 0x8000)
        return std::nullopt; // SMPTE time not supported
    f.ppq = static_cast<int> (division);
    pos = 8 + headerLen;

    for (uint32_t t = 0; t < numTracks; ++t)
    {
        if (! need (8) || std::string (d.begin() + static_cast<long> (pos), d.begin() + static_cast<long> (pos) + 4) != "MTrk")
            return std::nullopt;
        pos += 4;
        const auto len = u32();
        if (! need (len))
            return std::nullopt;
        const size_t end = pos + len;

        MidiClip clip;
        std::map<std::pair<int, int>, std::pair<int64_t, int>> open; // (ch,pitch) -> (tick, vel)
        int64_t tick = 0;
        uint8_t running = 0;

        auto varLen = [&] () -> std::optional<uint32_t>
        {
            uint32_t v = 0;
            for (int i = 0; i < 4; ++i)
            {
                if (pos >= end)
                    return std::nullopt;
                const uint8_t b = d[pos++];
                v = (v << 7) | (b & 0x7F);
                if (! (b & 0x80))
                    return v;
            }
            return std::nullopt;
        };

        while (pos < end)
        {
            const auto delta = varLen();
            if (! delta || pos >= end)
                return std::nullopt;
            tick += *delta;

            uint8_t status = d[pos];
            if (status & 0x80)
                ++pos;
            else if (running)
                status = running;
            else
                return std::nullopt;

            if (status == 0xFF)
            {
                if (pos >= end) return std::nullopt;
                const uint8_t type = d[pos++];
                const auto mlen = varLen();
                if (! mlen || pos + *mlen > end)
                    return std::nullopt;
                if (type == 0x51 && *mlen == 3)
                {
                    const uint32_t us = (uint32_t (d[pos]) << 16) | (uint32_t (d[pos + 1]) << 8) | d[pos + 2];
                    if (us > 0)
                        f.bpm = 60'000'000.0 / us;
                }
                else if (type == 0x03)
                    clip.name.assign (d.begin() + static_cast<long> (pos), d.begin() + static_cast<long> (pos + *mlen));
                else if (type == 0x2F)
                    clip.lengthBeats = static_cast<double> (tick) / f.ppq;
                pos += *mlen;
                continue;
            }
            if (status == 0xF0 || status == 0xF7)
            {
                const auto slen = varLen();
                if (! slen || pos + *slen > end)
                    return std::nullopt;
                pos += *slen;
                continue;
            }

            running = status;
            const int type = status & 0xF0;
            const int ch = status & 0x0F;
            const int dataBytes = (type == 0xC0 || type == 0xD0) ? 1 : 2;
            if (pos + static_cast<size_t> (dataBytes) > end)
                return std::nullopt;
            const int a = d[pos];
            const int b = dataBytes > 1 ? d[pos + 1] : 0;
            pos += static_cast<size_t> (dataBytes);

            const bool on = type == 0x90 && b > 0;
            const bool off = type == 0x80 || (type == 0x90 && b == 0);
            if (on)
                open[{ ch, a }] = { tick, b };
            else if (off)
            {
                if (auto it = open.find ({ ch, a }); it != open.end())
                {
                    Note n;
                    n.pitch = a;
                    n.channel = ch;
                    n.velocity = it->second.second;
                    n.start = static_cast<double> (it->second.first) / f.ppq;
                    n.length = static_cast<double> (tick - it->second.first) / f.ppq;
                    clip.notes.push_back (n);
                    open.erase (it);
                }
            }
        }

        pos = end;
        clip.sortByTime();
        f.tracks.push_back (std::move (clip));
    }
    return f;
}

std::string makeMidiFileName (const std::vector<std::string>& chordNames, const std::string& keyName,
                              double bpm, const std::string& part, size_t maxChords)
{
    auto sanitise = [] (std::string s)
    {
        for (size_t p; (p = s.find ("6/9")) != std::string::npos;)
            s.replace (p, 3, "69"); // Ab6/9 -> Ab69, not a slash chord

        std::string out;
        for (char c : s)
        {
            switch (c)
            {
                case '/':  out += "on"; break;   // C/E -> ConE
                case ' ':  break;
                case '\\': case ':': case '*': case '?': case '"': case '<': case '>': case '|':
                    break;
                default:
                    if (static_cast<unsigned char> (c) >= 0x20 && static_cast<unsigned char> (c) < 0x80)
                        out += c;
            }
        }
        return out;
    };

    std::string chords;
    for (size_t i = 0; i < chordNames.size() && i < maxChords; ++i)
    {
        if (i > 0)
            chords += "-";
        chords += sanitise (chordNames[i]);
    }

    std::string name;
    if (! chords.empty())
        name += chords + "_";
    if (! keyName.empty())
        name += sanitise (keyName) + "_";
    name += std::to_string (static_cast<int> (std::lround (bpm))) + "bpm_" + sanitise (part) + ".mid";
    return name;
}

} // namespace bounce::midi
