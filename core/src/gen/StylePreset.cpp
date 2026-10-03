#include "bounce/gen/StylePreset.h"

#include <cctype>

namespace bounce::gen
{

using theory::ChordFamily;

namespace
{
constexpr int kMajorOffsets[] = { 0, 2, 4, 5, 7, 9, 11 };
constexpr const char* kNumerals[] = { "I", "II", "III", "IV", "V", "VI", "VII" };

std::optional<int> numeralIndex (const std::string& upper)
{
    for (int i = 0; i < 7; ++i)
        if (upper == kNumerals[i])
            return i;
    return std::nullopt;
}

using TransitionTable = std::map<std::string, std::map<std::string, double>>;

TransitionTable defaultMajorTransitions()
{
    return {
        { "I",    { { "IV", 3.0 }, { "vi", 3.0 }, { "V", 1.5 }, { "ii", 1.5 }, { "iii", 1.0 }, { "bVII", 0.6 }, { "bVI", 0.5 }, { "iv", 0.4 } } },
        { "ii",   { { "V", 3.0 }, { "IV", 1.0 }, { "vi", 1.0 }, { "I", 1.0 }, { "iii", 0.8 }, { "bVII", 0.4 } } },
        { "iii",  { { "vi", 3.0 }, { "IV", 2.0 }, { "ii", 1.0 }, { "I", 0.5 } } },
        { "IV",   { { "I", 2.0 }, { "V", 2.0 }, { "vi", 1.5 }, { "iv", 1.2 }, { "iii", 1.2 }, { "ii", 1.0 }, { "bVII", 0.6 } } },
        { "V",    { { "I", 3.0 }, { "vi", 2.5 }, { "IV", 1.5 }, { "iii", 0.6 } } },
        { "vi",   { { "IV", 3.0 }, { "ii", 2.0 }, { "V", 1.5 }, { "iii", 1.5 }, { "I", 1.0 } } },
        { "viio", { { "I", 3.0 }, { "iii", 1.0 }, { "vi", 0.5 } } },
        { "bIII", { { "bVI", 2.0 }, { "IV", 1.5 }, { "bVII", 1.0 }, { "I", 1.0 } } },
        { "iv",   { { "I", 3.0 }, { "bVII", 1.5 }, { "V", 1.0 }, { "bVI", 0.8 } } },
        { "bVI",  { { "bVII", 3.0 }, { "I", 1.5 }, { "IV", 1.0 }, { "iv", 1.0 } } },
        { "bVII", { { "I", 3.0 }, { "IV", 1.5 }, { "bVI", 0.8 }, { "vi", 0.5 } } },
    };
}

TransitionTable defaultMinorTransitions()
{
    return {
        { "i",    { { "bVI", 3.0 }, { "iv", 2.5 }, { "bVII", 2.0 }, { "bIII", 1.5 }, { "v", 1.0 }, { "V", 0.8 }, { "iio", 0.5 }, { "IV", 0.5 } } },
        { "iio",  { { "V", 2.5 }, { "v", 1.5 }, { "i", 1.0 }, { "bIII", 0.6 } } },
        { "bIII", { { "bVI", 2.5 }, { "iv", 2.0 }, { "bVII", 2.0 }, { "i", 1.0 } } },
        { "iv",   { { "i", 2.5 }, { "bVII", 2.0 }, { "v", 1.5 }, { "V", 1.2 }, { "bVI", 1.0 }, { "bIII", 0.8 } } },
        { "v",    { { "i", 2.5 }, { "bVI", 2.5 }, { "iv", 1.2 }, { "bIII", 0.6 } } },
        { "bVI",  { { "bVII", 2.5 }, { "bIII", 2.0 }, { "iv", 1.8 }, { "i", 1.5 }, { "v", 1.0 }, { "V", 0.8 } } },
        { "bVII", { { "i", 2.5 }, { "bIII", 2.5 }, { "bVI", 1.5 }, { "iv", 1.0 } } },
        { "V",    { { "i", 3.0 }, { "bVI", 1.5 } } },
        { "IV",   { { "i", 2.0 }, { "bVI", 1.0 }, { "bVII", 1.0 } } },
        { "bII",  { { "i", 2.0 }, { "V", 1.0 } } },
    };
}

std::map<std::string, double> defaultColourWeights()
{
    return {
        { "maj", 1.0 },  { "maj7", 1.6 },  { "add9", 1.2 },  { "6_9", 0.7 },  { "maj9", 1.0 },
        { "maj7s11", 0.3 }, { "dom7", 0.35 }, { "dom9", 0.25 }, { "sus2", 0.5 }, { "maj7sus2", 0.4 },
        { "maj6", 0.3 },
        { "min", 1.0 },  { "min7", 1.5 },  { "madd9", 0.8 }, { "min9", 1.6 },  { "min11", 0.6 },
        { "min6", 0.2 }, { "minmaj7", 0.1 },
        { "dim", 1.0 },  { "m7b5", 1.2 },  { "aug", 1.0 },
    };
}

/** Canonical token ("ii\u00B0" -> "iio"), or empty (with a warning) when the token is invalid. */
std::string canonicalToken (const std::string& token, std::vector<std::string>* warnings)
{
    if (auto fn = parseHarmonyToken (token))
        return fn->symbol;
    if (warnings)
        warnings->push_back ("unknown harmony token '" + token + "'");
    return {};
}

void mergeTable (TransitionTable& into, const util::Json& json, std::vector<std::string>* warnings)
{
    for (const auto& [from, row] : json.asObject())
    {
        const auto f = canonicalToken (from, warnings);
        if (f.empty())
            continue;
        auto& dst = into[f];
        for (const auto& [to, w] : row.asObject())
            if (const auto t = canonicalToken (to, warnings); ! t.empty())
                dst[t] = w.asNumber();
    }
}

void mergeStartWeights (std::map<std::string, double>& into, const util::Json& json, std::vector<std::string>* warnings)
{
    for (const auto& [k, v] : json.asObject())
        if (const auto t = canonicalToken (k, warnings); ! t.empty())
            into[t] = v.asNumber();
}

util::Json tableToJson (const TransitionTable& t)
{
    util::Json j;
    j.asObject();
    for (const auto& [from, row] : t)
    {
        util::Json r;
        r.asObject();
        for (const auto& [to, w] : row)
            r.set (to, w);
        j.set (from, std::move (r));
    }
    return j;
}

void mergeWeights (std::map<std::string, double>& into, const util::Json& json)
{
    for (const auto& [k, v] : json.asObject())
        into[k] = v.asNumber();
}

util::Json weightsToJson (const std::map<std::string, double>& m)
{
    util::Json j;
    j.asObject();
    for (const auto& [k, v] : m)
        j.set (k, v);
    return j;
}
} // namespace

std::optional<HarmonyFunction> parseHarmonyToken (const std::string& token)
{
    size_t i = 0;
    int accidental = 0;
    if (i < token.size() && token[i] == 'b') { accidental = -1; ++i; }
    else if (i < token.size() && token[i] == '#') { accidental = 1; ++i; }

    std::string letters;
    bool anyUpper = false, anyLower = false;
    while (i < token.size() && (token[i] == 'I' || token[i] == 'V' || token[i] == 'i' || token[i] == 'v'))
    {
        anyUpper |= std::isupper (static_cast<unsigned char> (token[i])) != 0;
        anyLower |= std::islower (static_cast<unsigned char> (token[i])) != 0;
        letters += static_cast<char> (std::toupper (static_cast<unsigned char> (token[i])));
        ++i;
    }

    if (letters.empty() || (anyUpper && anyLower))
        return std::nullopt;

    const auto idx = numeralIndex (letters);
    if (! idx)
        return std::nullopt;

    const std::string rest = token.substr (i);
    ChordFamily family = anyUpper ? ChordFamily::Major : ChordFamily::Minor;
    if (rest == "o" || rest == "dim" || rest == "\xC2\xB0")
        family = ChordFamily::Diminished;
    else if (rest == "+")
        family = ChordFamily::Augmented;
    else if (! rest.empty())
        return std::nullopt;

    return makeHarmonyFunction (kMajorOffsets[*idx] + accidental, family);
}

HarmonyFunction makeHarmonyFunction (int offset, ChordFamily family)
{
    offset = theory::wrapPc (offset);

    int degree = -1;
    std::string accidental;
    for (int d = 0; d < 7; ++d)
        if (kMajorOffsets[d] == offset)
            degree = d;
    if (degree < 0)
    {
        for (int d = 0; d < 7; ++d)
            if (kMajorOffsets[d] == theory::wrapPc (offset + 1))
                degree = d;
        accidental = "b";
    }

    std::string body = kNumerals[degree];
    if (family == ChordFamily::Minor || family == ChordFamily::Diminished)
        for (auto& c : body)
            c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));

    HarmonyFunction fn;
    fn.offset = offset;
    fn.family = family;
    fn.symbol = accidental + body
              + (family == ChordFamily::Diminished ? "o" : family == ChordFamily::Augmented ? "+" : "");
    return fn;
}

theory::Chord functionTriad (const HarmonyFunction& fn, const theory::Key& key)
{
    using theory::ChordQuality;
    ChordQuality q = ChordQuality::Major;
    switch (fn.family)
    {
        case ChordFamily::Major:      q = ChordQuality::Major; break;
        case ChordFamily::Minor:      q = ChordQuality::Minor; break;
        case ChordFamily::Diminished: q = ChordQuality::Diminished; break;
        case ChordFamily::Augmented:  q = ChordQuality::Augmented; break;
        case ChordFamily::Suspended:  q = ChordQuality::Sus2; break;
    }
    return { theory::wrapPc (key.tonic + fn.offset), q, std::nullopt };
}

bool isDiatonicFunction (const HarmonyFunction& fn, const theory::Key& key)
{
    return functionTriad (fn, key).isDiatonicTo (key.heptatonicParent());
}

StylePreset StylePreset::defaults()
{
    StylePreset p;
    p.description = "Lush minor-key bounce: maj7/min9 colours, borrowed bVI/bVII, spread voicings.";
    p.majorTransitions = defaultMajorTransitions();
    p.minorTransitions = defaultMinorTransitions();
    p.majorStartWeights = { { "I", 3.0 }, { "vi", 2.0 }, { "IV", 2.0 }, { "ii", 1.0 } };
    p.minorStartWeights = { { "i", 3.0 }, { "bVI", 2.0 }, { "iv", 1.5 }, { "bIII", 1.0 } };
    p.colourWeights = defaultColourWeights();
    return p;
}

StylePreset StylePreset::fromJson (const util::Json& j, std::vector<std::string>* warnings)
{
    auto p = defaults();
    p.description.clear();

    p.id = j["id"].asString (p.id);
    p.name = j["name"].asString (p.name);
    p.description = j["description"].asString (p.description);

    const auto& tempo = j["tempo"];
    p.bpm = tempo["bpm"].asNumber (p.bpm);
    p.halfTimeFeel = tempo["halfTime"].asBool (p.halfTimeFeel);

    const auto& h = j["harmony"];
    if (h.has ("key"))
    {
        if (auto tonic = theory::parsePitchClass (h["key"].asString()))
            p.defaultKey.tonic = *tonic;
        else if (warnings)
            warnings->push_back ("bad key '" + h["key"].asString() + "'");
    }
    if (h.has ("scale"))
    {
        if (auto s = theory::scaleTypeFromId (h["scale"].asString()))
            p.defaultKey.scale = *s;
        else if (warnings)
            warnings->push_back ("bad scale '" + h["scale"].asString() + "'");
    }
    p.defaultBars = h["bars"].asInt (p.defaultBars);
    p.defaultChordCount = h["chordCount"].asInt (p.defaultChordCount);
    p.defaultComplexity = h["complexity"].asNumber (p.defaultComplexity);
    p.defaultMood = h["mood"].asNumber (p.defaultMood);
    p.borrowedChordAmount = h["borrowed"].asNumber (p.borrowedChordAmount);

    mergeTable (p.majorTransitions, h["majorTransitions"], warnings);
    mergeTable (p.minorTransitions, h["minorTransitions"], warnings);
    mergeStartWeights (p.majorStartWeights, h["majorStart"], warnings);
    mergeStartWeights (p.minorStartWeights, h["minorStart"], warnings);
    mergeWeights (p.colourWeights, h["colours"]);

    for (const auto& [id, w] : h["colours"].asObject())
        if (! theory::qualityFromId (id) && warnings)
            warnings->push_back ("unknown chord quality '" + id + "'");

    const auto& v = j["voicing"];
    if (v.has ("style"))
    {
        if (auto vs = theory::voicingStyleFromId (v["style"].asString()))
            p.voicing = *vs;
        else if (warnings)
            warnings->push_back ("bad voicing style '" + v["style"].asString() + "'");
    }
    p.registerLow = v["low"].asInt (p.registerLow);
    p.registerHigh = v["high"].asInt (p.registerHigh);

    const auto& perf = j["performance"];
    p.chordRhythm = perf["rhythm"].asString (p.chordRhythm);
    if (perf.has ("stabSteps"))
    {
        p.stabSteps.clear();
        for (const auto& s : perf["stabSteps"].asArray())
            p.stabSteps.push_back (s.asInt());
    }
    p.stabGate = perf["stabGate"].asNumber (p.stabGate);
    p.swing = perf["swing"].asNumber (p.swing);
    p.strumMs = perf["strumMs"].asNumber (p.strumMs);
    p.velocity = perf["velocity"].asNumber (p.velocity);
    p.velocityRandom = perf["velocityRandom"].asNumber (p.velocityRandom);
    p.timingRandomMs = perf["timingRandomMs"].asNumber (p.timingRandomMs);

    const auto& bass = j["bass"];
    if (bass.has ("mode"))
    {
        bool found = false;
        for (int m = 0; m < static_cast<int> (BassMode::NumModes); ++m)
            if (bass["mode"].asString() == bassModeName (static_cast<BassMode> (m)))
            {
                p.bassMode = static_cast<BassMode> (m);
                found = true;
            }
        if (! found && warnings)
            warnings->push_back ("bad bass mode '" + bass["mode"].asString() + "'");
    }
    p.bassDensity = bass["density"].asNumber (p.bassDensity);
    p.bassGlide = bass["glide"].asNumber (p.bassGlide);
    p.bassLockToKick = bass["lockToKick"].asBool (p.bassLockToKick);

    const auto& mel = j["melody"];
    p.melodyDensity = mel["density"].asNumber (p.melodyDensity);
    p.melodyPentatonic = mel["pentatonic"].asNumber (p.melodyPentatonic);
    if (mel.has ("feel"))
    {
        bool found = false;
        for (int f = 0; f < static_cast<int> (MelodyFeel::NumFeels); ++f)
            if (mel["feel"].asString() == melodyFeelName (static_cast<MelodyFeel> (f)))
            {
                p.melodyFeel = static_cast<MelodyFeel> (f);
                found = true;
            }
        if (! found && warnings)
            warnings->push_back ("bad melody feel '" + mel["feel"].asString() + "'");
    }

    const auto& dr = j["drums"];
    p.drums = DrumStyle::fromJson (dr, warnings);
    p.drumSwing = dr["swing"].asNumber (p.drumSwing);
    p.rollAmount = dr["rollAmount"].asNumber (p.rollAmount);
    p.percDensity = dr["percDensity"].asNumber (p.percDensity);
    p.openHatAmount = dr["openHatAmount"].asNumber (p.openHatAmount);

    return p;
}

util::Json StylePreset::toJson() const
{
    using util::Json;
    Json j;
    j.set ("id", id);
    j.set ("name", name);
    j.set ("description", description);

    Json tempo;
    tempo.set ("bpm", bpm);
    tempo.set ("halfTime", halfTimeFeel);
    j.set ("tempo", std::move (tempo));

    Json h;
    h.set ("key", theory::pitchClassName (defaultKey.tonic, defaultKey.preferredSpelling()));
    h.set ("scale", std::string (theory::scaleTypeId (defaultKey.scale)));
    h.set ("bars", defaultBars);
    h.set ("chordCount", defaultChordCount);
    h.set ("complexity", defaultComplexity);
    h.set ("mood", defaultMood);
    h.set ("borrowed", borrowedChordAmount);
    h.set ("majorStart", weightsToJson (majorStartWeights));
    h.set ("minorStart", weightsToJson (minorStartWeights));
    h.set ("majorTransitions", tableToJson (majorTransitions));
    h.set ("minorTransitions", tableToJson (minorTransitions));
    h.set ("colours", weightsToJson (colourWeights));
    j.set ("harmony", std::move (h));

    Json v;
    v.set ("style", std::string (theory::voicingStyleId (voicing)));
    v.set ("low", registerLow);
    v.set ("high", registerHigh);
    j.set ("voicing", std::move (v));

    Json perf;
    perf.set ("rhythm", chordRhythm);
    Json steps;
    steps.asArray();
    for (int s : stabSteps)
        steps.push (s);
    perf.set ("stabSteps", std::move (steps));
    perf.set ("stabGate", stabGate);
    perf.set ("swing", swing);
    perf.set ("strumMs", strumMs);
    perf.set ("velocity", velocity);
    perf.set ("velocityRandom", velocityRandom);
    perf.set ("timingRandomMs", timingRandomMs);
    j.set ("performance", std::move (perf));

    Json bass;
    bass.set ("mode", std::string (bassModeName (bassMode)));
    bass.set ("density", bassDensity);
    bass.set ("glide", bassGlide);
    bass.set ("lockToKick", bassLockToKick);
    j.set ("bass", std::move (bass));

    Json mel;
    mel.set ("density", melodyDensity);
    mel.set ("feel", std::string (melodyFeelName (melodyFeel)));
    mel.set ("pentatonic", melodyPentatonic);
    j.set ("melody", std::move (mel));

    Json dr = drums.toJson();
    dr.set ("swing", drumSwing);
    dr.set ("rollAmount", rollAmount);
    dr.set ("percDensity", percDensity);
    dr.set ("openHatAmount", openHatAmount);
    j.set ("drums", std::move (dr));

    return j;
}

} // namespace bounce::gen
