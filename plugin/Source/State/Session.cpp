#include "State/Session.h"

#include "State/Parameters.h"

#include "bounce/midi/MidiFile.h"

#include <algorithm>

namespace bounce
{

namespace
{
constexpr uint64_t kFirstLaunchSeed = 0x5EEDB0B0ull;
const juce::Identifier kSessionTag ("Session");
} // namespace

Session::Session (juce::AudioProcessorValueTreeState& state, Publisher publisher)
    : apvts (state), publish (std::move (publisher))
{
    rebuildGenerator();
    current = generator->generate (generatorParams (kFirstLaunchSeed));
    history.reset (current);
    render();
    lastParams = readParams();
    startTimerHz (30);
}

Session::~Session()
{
    stopTimer();
}

uint64_t Session::freshSeed()
{
    auto& r = juce::Random::getSystemRandom();
    return (static_cast<uint64_t> (static_cast<uint32_t> (r.nextInt())) << 32)
         | static_cast<uint32_t> (r.nextInt());
}

//==============================================================================
Session::ParamSnapshot Session::readParams() const
{
    const auto raw = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    ParamSnapshot s;
    s.key = juce::roundToInt (raw (params::key));
    s.scale = juce::roundToInt (raw (params::scale));
    s.bars = juce::roundToInt (raw (params::bars));
    s.chordCount = juce::roundToInt (raw (params::chordCount));
    s.complexity = raw (params::complexity);
    s.mood = raw (params::mood);
    s.borrowed = raw (params::borrowed);
    s.rhythm = juce::roundToInt (raw (params::rhythm));
    s.voicing = juce::roundToInt (raw (params::voicing));
    s.octave = juce::roundToInt (raw (params::octave));
    s.channel = juce::roundToInt (raw (params::midiChannel));
    s.humanise = raw (params::humanise);
    s.swing = raw (params::swing);
    return s;
}

void Session::setParam (const char* id, float plainValue)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
        p->endChangeGesture();
    }
}

gen::ChordGeneratorParams Session::generatorParams (uint64_t seed) const
{
    const auto s = readParams();
    gen::ChordGeneratorParams p;
    p.key.tonic = theory::wrapPc (s.key);
    p.key.scale = static_cast<theory::ScaleType> (juce::jlimit (0, static_cast<int> (theory::ScaleType::NumTypes) - 1, s.scale));
    p.bars = params::barChoices[juce::jlimit (0, 2, s.bars)];
    p.chordCount = s.chordCount;
    p.complexity = s.complexity;
    p.mood = s.mood;
    p.borrowed = s.borrowed;
    p.seed = seed;
    return p;
}

gen::ChordPerformance Session::performance() const
{
    const auto s = readParams();
    auto perf = gen::ChordPerformance::fromPreset (currentStyle());
    perf.rhythm = static_cast<gen::ChordRhythm> (juce::jlimit (0, static_cast<int> (gen::ChordRhythm::NumRhythms) - 1, s.rhythm));
    perf.voicing = static_cast<theory::VoicingStyle> (juce::jlimit (0, static_cast<int> (theory::VoicingStyle::NumStyles) - 1, s.voicing));
    perf.humanise = s.humanise;
    perf.swing = s.swing;
    perf.registerLow += 12 * s.octave;
    perf.registerHigh += 12 * s.octave;
    perf.channel = juce::jlimit (0, 15, s.channel - 1);
    perf.bpm = tempo();
    perf.seed = current.seed;
    return perf;
}

double Session::tempo() const
{
    return hostTempo > 0.0 ? hostTempo : currentStyle().bpm;
}

void Session::setHostTempo (double bpm)
{
    // Only humanise timing depends on tempo; re-render when it changes noticeably.
    if (std::abs (bpm - hostTempo) > 0.5)
    {
        hostTempo = bpm;
        render();
    }
}

//==============================================================================
void Session::timerCallback()
{
    if (suppressPolling)
        return;

    if (hostTempoSource)
        if (const double bpm = hostTempoSource(); bpm > 0.0)
            setHostTempo (bpm);

    const auto s = readParams();
    const auto& o = lastParams;

    // Key changes transpose the existing idea instead of throwing it away.
    if (theory::wrapPc (s.key) != current.key.tonic)
    {
        int diff = theory::wrapPc (s.key - current.key.tonic);
        if (diff > 6)
            diff -= 12;
        commit (current.transposed (diff), false);
    }

    const auto differs = [] (float a, float b) { return ! juce::exactlyEqual (a, b); };
    const bool harmonyChanged = s.scale != o.scale || s.bars != o.bars || s.chordCount != o.chordCount
                             || differs (s.complexity, o.complexity) || differs (s.mood, o.mood) || differs (s.borrowed, o.borrowed);
    const bool performanceChanged = s.rhythm != o.rhythm || s.voicing != o.voicing || s.octave != o.octave
                                 || s.channel != o.channel || differs (s.humanise, o.humanise) || differs (s.swing, o.swing);
    lastParams = s;

    if (harmonyChanged)
        pendingRegenTicks = 6; // ~200 ms debounce while a knob is being dragged

    if (performanceChanged)
    {
        render();
        sendChangeMessage();
    }

    if (pendingRegenTicks > 0 && --pendingRegenTicks == 0)
    {
        pendingRegenTicks = -1;
        // Same seed: turning a knob re-colours *this* idea rather than rolling a new one.
        commit (generator->generate (generatorParams (current.seed), &current), false);
    }
}

void Session::rebuildGenerator()
{
    generator = std::make_unique<gen::ChordGenerator> (currentStyle());
}

void Session::commit (const gen::Progression& p, bool asNewIdea, const juce::String& label)
{
    current = p;
    if (asNewIdea)
        history.addIdea (current, label.toStdString());
    else
        history.push (current);

    selectedSlot = juce::jlimit (0, static_cast<int> (current.slots.size()) - 1, selectedSlot);
    render();
    sendChangeMessage();
}

void Session::render()
{
    clip = gen::renderChords (current, performance());
    clip.name = "Chords";
    if (publish)
        publish (clip);

    auto snapshot = buildValueTree();
    const juce::SpinLock::ScopedLockType sl (stateLock);
    cachedState = snapshot;
}

void Session::syncParamsFromProgression()
{
    suppressPolling = true;
    setParam (params::key, static_cast<float> (current.key.tonic));
    setParam (params::scale, static_cast<float> (current.key.scale));
    const auto barIt = std::find (std::begin (params::barChoices), std::end (params::barChoices), current.bars);
    if (barIt != std::end (params::barChoices))
        setParam (params::bars, static_cast<float> (barIt - std::begin (params::barChoices)));
    setParam (params::chordCount, static_cast<float> (current.slots.size()));
    lastParams = readParams();
    pendingRegenTicks = -1;
    suppressPolling = false;
}

//==============================================================================
void Session::generate()
{
    generateWithSeed (freshSeed());
}

void Session::generateWithSeed (uint64_t seed)
{
    pendingRegenTicks = -1;
    lastParams = readParams();
    auto p = generator->generate (generatorParams (seed), &current);
    commit (p, true, juce::String (p.chordNames()));
}

void Session::randomise()
{
    auto& r = juce::Random::getSystemRandom();
    suppressPolling = true;
    setParam (params::complexity, 0.3f + 0.65f * r.nextFloat());
    setParam (params::mood, -0.7f + 1.3f * r.nextFloat());
    setParam (params::borrowed, 0.6f * r.nextFloat());
    setParam (params::voicing, static_cast<float> (r.nextInt (static_cast<int> (theory::VoicingStyle::NumStyles))));

    const float roll = r.nextFloat(); // sustain 40%, stabs 30%, half 20%, pulse 10%
    const auto rhythm = roll < 0.4f ? gen::ChordRhythm::Sustain
                      : roll < 0.7f ? gen::ChordRhythm::Stabs
                      : roll < 0.9f ? gen::ChordRhythm::Half
                                    : gen::ChordRhythm::Pulse8;
    setParam (params::rhythm, static_cast<float> (rhythm));
    suppressPolling = false;
    generate();
}

void Session::toggleLock (int slot)
{
    if (slot < 0 || slot >= static_cast<int> (current.slots.size()))
        return;
    auto p = current;
    p.slots[static_cast<size_t> (slot)].locked = ! p.slots[static_cast<size_t> (slot)].locked;
    commit (p, false);
}

void Session::invert (int slot, int delta)
{
    if (slot < 0 || slot >= static_cast<int> (current.slots.size()))
        return;

    auto p = current;
    auto& s = p.slots[static_cast<size_t> (slot)];

    // Start from the inversion voice leading picked, so the first click moves exactly one step.
    if (! s.inversion)
        s.inversion = gen::voiceProgression (current, performance())[static_cast<size_t> (slot)].inversion;

    s.inversion = *s.inversion + delta;
    commit (p, false);
}

void Session::shiftOctave (int slot, int delta)
{
    if (slot < 0 || slot >= static_cast<int> (current.slots.size()))
        return;
    auto p = current;
    auto& s = p.slots[static_cast<size_t> (slot)];
    s.octave = juce::jlimit (-2, 2, s.octave + delta);
    commit (p, false);
}

void Session::setSlotVoicing (int slot, std::optional<theory::VoicingStyle> style)
{
    if (slot < 0 || slot >= static_cast<int> (current.slots.size()))
        return;
    auto p = current;
    p.slots[static_cast<size_t> (slot)].voicing = style;
    p.slots[static_cast<size_t> (slot)].inversion.reset();
    commit (p, false);
}

void Session::reharmonise (int slot)
{
    if (slot < 0 || slot >= static_cast<int> (current.slots.size()))
        return;
    commit (generator->reharmonise (current, slot, generatorParams (current.seed), ++reharmCounter), false);
}

void Session::transpose (int semitones)
{
    commit (current.transposed (semitones), false);
    syncParamsFromProgression();
}

void Session::undo()
{
    if (auto p = history.undo())
    {
        current = *p;
        syncParamsFromProgression();
        render();
        sendChangeMessage();
    }
}

void Session::redo()
{
    if (auto p = history.redo())
    {
        current = *p;
        syncParamsFromProgression();
        render();
        sendChangeMessage();
    }
}

void Session::recallIdea (int index)
{
    const auto& list = history.ideas();
    if (index < 0 || index >= static_cast<int> (list.size()))
        return;
    commit (list[static_cast<size_t> (index)].progression, false);
    syncParamsFromProgression();
}

void Session::setSelectedSlot (int slot)
{
    slot = juce::jlimit (0, static_cast<int> (current.slots.size()) - 1, slot);
    if (slot != selectedSlot)
    {
        selectedSlot = slot;
        sendChangeMessage();
    }
}

//==============================================================================
void Session::setStyle (int index)
{
    styleIndex = juce::jlimit (0, static_cast<int> (library.presets().size()) - 1, index);
    rebuildGenerator();

    const auto& s = currentStyle();
    suppressPolling = true;
    setParam (params::key, static_cast<float> (s.defaultKey.tonic));
    setParam (params::scale, static_cast<float> (s.defaultKey.scale));
    const auto barIt = std::find (std::begin (params::barChoices), std::end (params::barChoices), s.defaultBars);
    setParam (params::bars, static_cast<float> (barIt != std::end (params::barChoices) ? barIt - std::begin (params::barChoices) : 1));
    setParam (params::chordCount, static_cast<float> (juce::jlimit (1, 8, s.defaultChordCount)));
    setParam (params::complexity, static_cast<float> (s.defaultComplexity));
    setParam (params::mood, static_cast<float> (s.defaultMood));
    setParam (params::borrowed, static_cast<float> (s.borrowedChordAmount));
    setParam (params::voicing, static_cast<float> (s.voicing));
    setParam (params::rhythm, static_cast<float> (gen::chordRhythmFromId (s.chordRhythm).value_or (gen::ChordRhythm::Sustain)));
    setParam (params::swing, static_cast<float> (s.swing));
    suppressPolling = false;

    // Locks only make sense within a style's key; a new style starts fresh.
    for (auto& slot : current.slots)
        slot.locked = false;
    generate();
}

juce::StringArray Session::reloadStyles()
{
    const auto id = currentStyle().id;
    auto warnings = library.reload();
    styleIndex = library.indexOf (id);
    rebuildGenerator();
    render();
    sendChangeMessage();
    return warnings;
}

//==============================================================================
std::vector<midi::MidiClip> Session::clipsForExport (const juce::String& part) const
{
    juce::ignoreUnused (part); // milestone 1 has a single part; "all" will add bass/melody/drums
    return { clip };
}

juce::String Session::exportFileName (const juce::String& part) const
{
    std::vector<std::string> names;
    for (const auto& s : current.slots)
        names.push_back (s.chord.name (current.key.preferredSpelling()));
    return juce::String (midi::makeMidiFileName (names, current.key.name(), tempo(), part.toStdString()));
}

//==============================================================================
juce::ValueTree Session::toValueTree() const
{
    const juce::SpinLock::ScopedLockType sl (stateLock);
    return cachedState.createCopy();
}

juce::ValueTree Session::buildValueTree() const
{
    juce::ValueTree t (kSessionTag);
    t.setProperty ("version", 1, nullptr);
    t.setProperty ("style", juce::String (currentStyle().id), nullptr);
    t.setProperty ("progression", juce::String (current.toJson().dump (0)), nullptr);
    t.setProperty ("ideas", juce::String (history.ideasToJson().dump (0)), nullptr);
    t.setProperty ("selected", selectedSlot, nullptr);
    return t;
}

void Session::fromValueTree (const juce::ValueTree& t)
{
    if (! t.hasType (kSessionTag))
        return;

    styleIndex = library.indexOf (t.getProperty ("style").toString().toStdString());
    rebuildGenerator();

    const auto progText = t.getProperty ("progression").toString().toStdString();
    if (auto parsed = util::Json::parse (progText); parsed.value)
        if (auto p = gen::Progression::fromJson (*parsed.value))
            current = *p;

    if (auto parsed = util::Json::parse (t.getProperty ("ideas").toString().toStdString()); parsed.value)
        history.ideasFromJson (*parsed.value);

    history.reset (current);
    selectedSlot = juce::jlimit (0, static_cast<int> (current.slots.size()) - 1, static_cast<int> (t.getProperty ("selected", 0)));
    pendingRegenTicks = -1;
    lastParams = readParams();
    render();
    sendChangeMessage();
}

} // namespace bounce
