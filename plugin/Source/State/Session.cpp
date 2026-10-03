#include "State/Session.h"

#include "State/Parameters.h"

#include "bounce/midi/MidiFile.h"
#include "bounce/util/Random.h"

#include <algorithm>

namespace bounce
{

using gen::Part;

namespace
{
constexpr uint64_t kFirstLaunchSeed = 0x5EEDB0B0ull;
const juce::Identifier kSessionTag ("Session");

int partIndex (Part p) { return static_cast<int> (p); }
} // namespace

Session::Session (juce::AudioProcessorValueTreeState& state, Publisher publisher)
    : apvts (state), publish (std::move (publisher))
{
    for (auto* p : apvts.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            watched.emplace_back (ranged->getParameterID(), apvts.getRawParameterValue (ranged->getParameterID()));

    rebuildGenerator();
    current.chords = generator->generate (generatorParams (kFirstLaunchSeed));
    current.bassSeed = util::deriveSeed (kFirstLaunchSeed, 1);
    current.melodySeed = util::deriveSeed (kFirstLaunchSeed, 2);
    current.counterSeed = util::deriveSeed (kFirstLaunchSeed, 3);
    current.drumSeed = util::deriveSeed (kFirstLaunchSeed, 4);
    history.reset (current);
    render();
    snapshotParams();
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
float Session::raw (const char* id) const
{
    return apvts.getRawParameterValue (id)->load();
}

int Session::rawInt (const char* id) const
{
    return juce::roundToInt (raw (id));
}

void Session::snapshotParams()
{
    lastValues.resize (watched.size());
    for (size_t i = 0; i < watched.size(); ++i)
        lastValues[i] = watched[i].second->load();
}

void Session::setParam (const juce::String& id, float plainValue)
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
    gen::ChordGeneratorParams p;
    p.key.tonic = theory::wrapPc (rawInt (params::key));
    p.key.scale = static_cast<theory::ScaleType> (juce::jlimit (0, static_cast<int> (theory::ScaleType::NumTypes) - 1, rawInt (params::scale)));
    p.bars = params::barChoices[juce::jlimit (0, 2, rawInt (params::bars))];
    p.chordCount = rawInt (params::chordCount);
    p.complexity = raw (params::complexity);
    p.mood = raw (params::mood);
    p.borrowed = raw (params::borrowed);
    p.seed = seed;
    return p;
}

gen::ChordPerformance Session::performance() const
{
    auto perf = gen::ChordPerformance::fromPreset (currentStyle());
    perf.rhythm = static_cast<gen::ChordRhythm> (juce::jlimit (0, static_cast<int> (gen::ChordRhythm::NumRhythms) - 1, rawInt (params::rhythm)));
    perf.voicing = static_cast<theory::VoicingStyle> (juce::jlimit (0, static_cast<int> (theory::VoicingStyle::NumStyles) - 1, rawInt (params::voicing)));
    perf.humanise = raw (params::humanise);
    perf.swing = raw (params::swing);
    perf.registerLow += 12 * rawInt (params::octave);
    perf.registerHigh += 12 * rawInt (params::octave);
    perf.channel = juce::jlimit (0, 15, rawInt (params::midiChannel) - 1);
    perf.bpm = tempo();
    perf.seed = current.chords.seed;
    return perf;
}

gen::DrumParams Session::drumParams() const
{
    gen::DrumParams d;
    d.bars = current.chords.bars;
    d.bpm = tempo();
    d.kickDensity = raw (params::drumKick);
    d.swing = raw (params::drumSwing);
    d.humanise = raw (params::drumHumanise);
    d.bounce = raw (params::drumBounce);
    d.rollAmount = raw (params::drumRolls);
    d.rollRates = (raw (params::drumRoll16) > 0.5f ? gen::Roll16 : 0) | (raw (params::drumRoll16T) > 0.5f ? gen::Roll16T : 0)
                | (raw (params::drumRoll32) > 0.5f ? gen::Roll32 : 0) | (raw (params::drumRoll32T) > 0.5f ? gen::Roll32T : 0);
    d.rollPitch = raw (params::drumRollPitch);
    d.rollCurve = static_cast<gen::RollCurve> (juce::jlimit (0, static_cast<int> (gen::RollCurve::NumCurves) - 1, rawInt (params::drumRollCurve)));
    d.openHatAmount = raw (params::drumOpenHat);
    d.percDensity = raw (params::drumPerc);
    d.fx = raw (params::drumFx) > 0.5f;
    d.seed = current.drumSeed;
    return d;
}

double Session::tempo() const
{
    return hostTempo > 0.0 ? hostTempo : currentStyle().bpm;
}

void Session::setHostTempo (double bpm)
{
    // Humanise/swing timing is in ms, so re-render when the tempo moves noticeably.
    if (std::abs (bpm - hostTempo) > 0.5)
    {
        hostTempo = bpm;
        render();
        sendChangeMessage();
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

    // Key changes transpose the existing idea instead of throwing it away.
    if (theory::wrapPc (rawInt (params::key)) != current.chords.key.tonic)
    {
        int diff = theory::wrapPc (rawInt (params::key) - current.chords.key.tonic);
        if (diff > 6)
            diff -= 12;
        commit (current.transposed (diff), false);
    }

    bool harmonyChanged = false, renderChanged = false;
    for (size_t i = 0; i < watched.size(); ++i)
    {
        const float v = watched[i].second->load();
        if (i < lastValues.size() && juce::exactlyEqual (v, lastValues[i]))
            continue;
        const auto& id = watched[i].first;
        if (id == params::key)
            continue;
        if (params::isChordHarmonyParam (id))
            harmonyChanged = true;
        else if (params::affectsRendering (id))
            renderChanged = true;
    }
    snapshotParams();

    if (harmonyChanged)
        pendingRegenTicks = 6; // ~200 ms debounce while a knob is being dragged

    if (renderChanged)
    {
        render();
        sendChangeMessage();
    }

    if (pendingRegenTicks > 0 && --pendingRegenTicks == 0)
    {
        pendingRegenTicks = -1;
        // Same seed: turning a knob re-colours *this* idea rather than rolling a new one.
        commit (withNewChords (generator->generate (generatorParams (current.chords.seed), &current.chords)), false);
    }
}

void Session::rebuildGenerator()
{
    generator = std::make_unique<gen::ChordGenerator> (currentStyle());
}

gen::Idea Session::withNewChords (const gen::Progression& p) const
{
    auto idea = current;
    idea.chords = p;
    return idea;
}

void Session::commit (const gen::Idea& idea, bool asNewIdea, const juce::String& label)
{
    current = idea;
    if (asNewIdea)
        history.addIdea (current, label.toStdString());
    else
        history.push (current);

    selectedSlot = juce::jlimit (0, static_cast<int> (current.chords.slots.size()) - 1, selectedSlot);
    render();
    sendChangeMessage();
}

void Session::render()
{
    const auto& prog = current.chords;
    const auto& style = currentStyle();
    const int baseChannel = juce::jlimit (0, 15, rawInt (params::midiChannel) - 1);

    // Chords.
    auto& chords = clips[static_cast<size_t> (Part::Chords)];
    chords = gen::renderChords (prog, performance());
    chords.name = "Chords";
    current.editsFor (Part::Chords).apply (chords);

    // Drums first: the 808 locks to the kick.
    drums = gen::generateDrums (drumParams(), style.drums);
    current.editsFor (Part::Drums).applyToDrums (drums);

    gen::BassParams bp;
    bp.mode = static_cast<gen::BassMode> (juce::jlimit (0, static_cast<int> (gen::BassMode::NumModes) - 1, rawInt (params::bassMode)));
    bp.density = raw (params::bassDensity);
    bp.glide = raw (params::bassGlide);
    bp.octaveRange = rawInt (params::bassOctaves);
    bp.lockToKick = raw (params::bassLockKick) > 0.5f;
    bp.noteLength = raw (params::bassLength);
    bp.lowNote = rawInt (params::bassLowNote);
    bp.channel = (baseChannel + 1) % 16;
    bp.seed = current.bassSeed;
    auto& bass = clips[static_cast<size_t> (Part::Bass)];
    bass = gen::generateBass (prog, drums.kickTimes(), bp);
    current.editsFor (Part::Bass).apply (bass);

    gen::MelodyParams mp;
    const int melShift = 12 * rawInt (params::melOctave);
    mp.density = raw (params::melDensity);
    mp.rangeLow += melShift;
    mp.rangeHigh += melShift;
    mp.feel = static_cast<gen::MelodyFeel> (juce::jlimit (0, static_cast<int> (gen::MelodyFeel::NumFeels) - 1, rawInt (params::melFeel)));
    mp.repetition = raw (params::melRepetition);
    mp.catchiness = raw (params::melCatchiness);
    mp.callResponse = raw (params::melCallResponse) > 0.5f;
    mp.pentatonic = raw (params::melPentatonic);
    mp.channel = (baseChannel + 2) % 16;
    mp.seed = current.melodySeed;
    mp.lockedBars = current.melodyLockedBars;
    mp.lockedNotes = current.melodyLockedNotes;
    auto& melody = clips[static_cast<size_t> (Part::Melody)];
    melody = gen::generateMelody (prog, mp);
    current.editsFor (Part::Melody).apply (melody);

    auto& counter = clips[static_cast<size_t> (Part::Counter)];
    if (raw (params::melCounter) > 0.5f)
    {
        gen::CounterParams cp;
        cp.rangeLow += melShift;
        cp.rangeHigh += melShift;
        cp.density = raw (params::counterDensity);
        cp.channel = (baseChannel + 3) % 16;
        cp.seed = current.counterSeed;
        counter = gen::generateCounterMelody (prog, melody, cp);
        current.editsFor (Part::Counter).apply (counter);
    }
    else
    {
        counter = {};
        counter.name = "Counter";
        counter.lengthBeats = prog.lengthBeats();
    }

    gen::DrumMap map = gen::DrumMap::gm();
    if (rawInt (params::drumMap) == 1)
        for (int l = 0; l < gen::numDrumLanes; ++l)
            map.notes[static_cast<size_t> (l)] = juce::roundToInt (apvts.getRawParameterValue (params::drumNoteId (l))->load());
    clips[static_cast<size_t> (Part::Drums)] = gen::renderDrums (drums, map, 9, raw (params::drumPitchNotes) > 0.5f);
    drumsInternal = gen::renderDrumsInternal (drums, 9);

    if (publish)
    {
        publish (PlaybackSlot::Chords, chords);
        publish (PlaybackSlot::Bass, bass);
        publish (PlaybackSlot::Melody, melody);
        publish (PlaybackSlot::Counter, counter);
        publish (PlaybackSlot::DrumsOut, clips[static_cast<size_t> (Part::Drums)]);
        publish (PlaybackSlot::DrumsInternal, drumsInternal);
    }

    auto snapshot = buildValueTree();
    const juce::SpinLock::ScopedLockType sl (stateLock);
    cachedState = snapshot;
}

void Session::syncParamsFromProgression()
{
    const auto& prog = current.chords;
    suppressPolling = true;
    setParam (params::key, static_cast<float> (prog.key.tonic));
    setParam (params::scale, static_cast<float> (prog.key.scale));
    const auto barIt = std::find (std::begin (params::barChoices), std::end (params::barChoices), prog.bars);
    if (barIt != std::end (params::barChoices))
        setParam (params::bars, static_cast<float> (barIt - std::begin (params::barChoices)));
    setParam (params::chordCount, static_cast<float> (prog.slots.size()));
    snapshotParams();
    pendingRegenTicks = -1;
    suppressPolling = false;
}

//==============================================================================
void Session::generate()
{
    pendingRegenTicks = -1;
    snapshotParams();
    auto idea = current;
    idea.chords = generator->generate (generatorParams (freshSeed()), &current.chords);
    idea.bassSeed = freshSeed();
    idea.melodySeed = freshSeed();
    idea.counterSeed = freshSeed();
    idea.drumSeed = freshSeed();
    for (auto& e : idea.edits)
        e = {};
    commit (idea, true, juce::String (idea.chords.chordNames()));
}

void Session::generateWithSeed (uint64_t seed)
{
    pendingRegenTicks = -1;
    snapshotParams();
    auto idea = current;
    idea.chords = generator->generate (generatorParams (seed), &current.chords);
    idea.bassSeed = util::deriveSeed (seed, 1);
    idea.melodySeed = util::deriveSeed (seed, 2);
    idea.counterSeed = util::deriveSeed (seed, 3);
    idea.drumSeed = util::deriveSeed (seed, 4);
    for (auto& e : idea.edits)
        e = {};
    commit (idea, true, juce::String (idea.chords.chordNames()));
}

void Session::regeneratePart (Part part)
{
    auto idea = current;
    idea.editsFor (part) = {};
    switch (part)
    {
        case Part::Chords:  idea.chords = generator->generate (generatorParams (freshSeed()), &current.chords); break;
        case Part::Bass:    idea.bassSeed = freshSeed(); break;
        case Part::Melody:  idea.melodySeed = freshSeed(); break;
        case Part::Counter: idea.counterSeed = freshSeed(); break;
        case Part::Drums:   idea.drumSeed = freshSeed(); break;
        case Part::NumParts: break;
    }
    commit (idea, part == Part::Chords, juce::String (idea.chords.chordNames()));
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
    setParam (params::bassMode, static_cast<float> (r.nextInt (static_cast<int> (gen::BassMode::NumModes))));
    setParam (params::melFeel, static_cast<float> (r.nextInt (3) == 0 ? 2 : r.nextInt (2)));
    setParam (params::drumRolls, 0.25f + 0.6f * r.nextFloat());
    setParam (params::drumBounce, 0.6f * r.nextFloat());
    suppressPolling = false;
    generate();
}

//==============================================================================
void Session::toggleLock (int slot)
{
    if (slot < 0 || slot >= static_cast<int> (current.chords.slots.size()))
        return;
    auto idea = current;
    auto& s = idea.chords.slots[static_cast<size_t> (slot)];
    s.locked = ! s.locked;
    commit (idea, false);
}

void Session::invert (int slot, int delta)
{
    if (slot < 0 || slot >= static_cast<int> (current.chords.slots.size()))
        return;
    auto idea = current;
    auto& s = idea.chords.slots[static_cast<size_t> (slot)];
    // Start from the inversion voice leading picked, so the first click moves exactly one step.
    if (! s.inversion)
        s.inversion = gen::voiceProgression (current.chords, performance())[static_cast<size_t> (slot)].inversion;
    s.inversion = *s.inversion + delta;
    commit (idea, false);
}

void Session::shiftOctave (int slot, int delta)
{
    if (slot < 0 || slot >= static_cast<int> (current.chords.slots.size()))
        return;
    auto idea = current;
    auto& s = idea.chords.slots[static_cast<size_t> (slot)];
    s.octave = juce::jlimit (-2, 2, s.octave + delta);
    commit (idea, false);
}

void Session::setSlotVoicing (int slot, std::optional<theory::VoicingStyle> style)
{
    if (slot < 0 || slot >= static_cast<int> (current.chords.slots.size()))
        return;
    auto idea = current;
    idea.chords.slots[static_cast<size_t> (slot)].voicing = style;
    idea.chords.slots[static_cast<size_t> (slot)].inversion.reset();
    commit (idea, false);
}

void Session::reharmonise (int slot)
{
    if (slot < 0 || slot >= static_cast<int> (current.chords.slots.size()))
        return;
    commit (withNewChords (generator->reharmonise (current.chords, slot, generatorParams (current.chords.seed), ++reharmCounter)), false);
}

void Session::transpose (int semitones)
{
    commit (current.transposed (semitones), false);
    syncParamsFromProgression();
}

void Session::setSlotLength (int slot, double beats)
{
    auto p = current.chords;
    p.setSlotLength (slot, beats);
    commit (withNewChords (p), false);
}

void Session::resetSlotLengths()
{
    auto p = current.chords;
    p.clearCustomLengths();
    commit (withNewChords (p), false);
}

//==============================================================================
bool Session::isMelodyBarLocked (int bar) const
{
    return bar >= 0 && bar < 32 && ((current.melodyLockedBars >> bar) & 1u);
}

void Session::toggleMelodyBarLock (int bar)
{
    if (bar < 0 || bar >= 32 || bar >= current.chords.bars)
        return;

    auto idea = current;
    const double a = bar * 4.0, b = a + 4.0;
    auto inBar = [a, b] (double t) { return t >= a - 1e-9 && t < b - 1e-9; };
    auto& locked = idea.melodyLockedNotes;
    locked.erase (std::remove_if (locked.begin(), locked.end(), [&] (const midi::Note& n) { return inBar (n.start); }), locked.end());

    if (isMelodyBarLocked (bar))
        idea.melodyLockedBars &= ~(1u << bar);
    else
    {
        // Bake the bar as it sounds now (generated + hand edits) into the lock.
        idea.melodyLockedBars |= 1u << bar;
        for (const auto& n : clip (Part::Melody).notes)
            if (inBar (n.start))
                locked.push_back (n);
        auto& e = idea.editsFor (Part::Melody);
        e.added.erase (std::remove_if (e.added.begin(), e.added.end(), [&] (const midi::Note& n) { return inBar (n.start); }), e.added.end());
        e.removed.erase (std::remove_if (e.removed.begin(), e.removed.end(), [&] (const gen::PartEdits::NoteRef& r)
        {
            return inBar (static_cast<double> (r.tick) / 960.0);
        }), e.removed.end());
    }
    commit (idea, false);
}

void Session::addNote (Part part, const midi::Note& note)
{
    auto idea = current;
    idea.editsFor (part).addNote (note);
    commit (idea, false);
}

void Session::removeNote (Part part, const midi::Note& note)
{
    auto idea = current;
    idea.editsFor (part).removeNote (note);
    commit (idea, false);
}

void Session::toggleDrumHit (gen::DrumLane lane, double beat)
{
    auto idea = current;
    auto& e = idea.editsFor (Part::Drums);
    const auto it = std::find_if (drums.hits.begin(), drums.hits.end(), [&] (const gen::DrumHit& h)
    {
        return h.lane == lane && std::abs (h.start - beat) < 0.09;
    });
    if (it != drums.hits.end())
        e.removeNote ({ static_cast<int> (lane), it->start, it->length, it->velocity, 0 });
    else
        e.addNote ({ static_cast<int> (lane), beat, lane == gen::DrumLane::OpenHat ? 0.5 : 0.25, 100, 0 });
    commit (idea, false);
}

void Session::clearEdits (Part part)
{
    auto idea = current;
    idea.editsFor (part) = {};
    commit (idea, false);
}

bool Session::hasEdits (Part part) const
{
    return ! current.editsFor (part).empty();
}

void Session::applyProgression (const gen::Progression& p)
{
    auto prog = p;
    prog.seed = current.chords.seed;
    prog.styleId = currentStyle().id;
    auto idea = withNewChords (prog);
    idea.editsFor (Part::Chords) = {};
    commit (idea, true, "From Lab: " + juce::String (prog.chordNames()));
    syncParamsFromProgression();
}

//==============================================================================
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
    commit (list[static_cast<size_t> (index)].idea, false);
    syncParamsFromProgression();
}

void Session::setSelectedSlot (int slot)
{
    slot = juce::jlimit (0, static_cast<int> (current.chords.slots.size()) - 1, slot);
    if (slot != selectedSlot)
    {
        selectedSlot = slot;
        sendChangeMessage();
    }
}

void Session::setArrangement (const gen::Arrangement& a)
{
    arrangementModel = a;
    render(); // refreshes the state snapshot
    sendChangeMessage();
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
    setParam (params::bassMode, static_cast<float> (s.bassMode));
    setParam (params::bassDensity, static_cast<float> (s.bassDensity));
    setParam (params::bassGlide, static_cast<float> (s.bassGlide));
    setParam (params::bassLockKick, s.bassLockToKick ? 1.0f : 0.0f);
    setParam (params::melDensity, static_cast<float> (s.melodyDensity));
    setParam (params::melFeel, static_cast<float> (s.melodyFeel));
    setParam (params::melPentatonic, static_cast<float> (s.melodyPentatonic));
    setParam (params::drumSwing, static_cast<float> (s.drumSwing));
    setParam (params::drumRolls, static_cast<float> (s.rollAmount));
    setParam (params::drumPerc, static_cast<float> (s.percDensity));
    setParam (params::drumOpenHat, static_cast<float> (s.openHatAmount));
    suppressPolling = false;

    // Locks belong to the old style's key; a new style starts fresh.
    for (auto& slot : current.chords.slots)
        slot.locked = false;
    current.melodyLockedBars = 0;
    current.melodyLockedNotes.clear();
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
    auto nonEmpty = [] (std::vector<midi::MidiClip> in)
    {
        in.erase (std::remove_if (in.begin(), in.end(), [] (const midi::MidiClip& c) { return c.notes.empty(); }), in.end());
        return in;
    };

    if (part == "all")
        return nonEmpty ({ clips.begin(), clips.end() });
    if (part == "arrangement")
        return nonEmpty (gen::renderArrangement (arrangementModel, clips, current.chords.beatsPerBar));

    for (int p = 0; p < gen::numParts; ++p)
        if (part == juce::String (std::string (gen::partId (static_cast<Part> (p)))))
            return { clips[static_cast<size_t> (p)] };
    return { clips[static_cast<size_t> (partIndex (Part::Chords))] };
}

juce::String Session::exportFileName (const juce::String& part) const
{
    std::vector<std::string> names;
    for (const auto& s : current.chords.slots)
        names.push_back (s.chord.name (current.chords.key.preferredSpelling()));
    return juce::String (midi::makeMidiFileName (names, current.chords.key.name(), tempo(), part.toStdString()));
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
    t.setProperty ("version", 2, nullptr);
    t.setProperty ("style", juce::String (currentStyle().id), nullptr);
    t.setProperty ("idea", juce::String (current.toJson().dump (0)), nullptr);
    t.setProperty ("ideas", juce::String (history.ideasToJson().dump (0)), nullptr);
    t.setProperty ("arrangement", juce::String (arrangementModel.toJson().dump (0)), nullptr);
    t.setProperty ("selected", selectedSlot, nullptr);
    return t;
}

void Session::fromValueTree (const juce::ValueTree& t)
{
    if (! t.hasType (kSessionTag))
        return;

    styleIndex = library.indexOf (t.getProperty ("style").toString().toStdString());
    rebuildGenerator();

    // Version 1 (milestone 1) stored only "progression".
    const auto ideaText = t.hasProperty ("idea") ? t.getProperty ("idea").toString() : t.getProperty ("progression").toString();
    if (auto parsed = util::Json::parse (ideaText.toStdString()); parsed.value)
        if (auto idea = gen::Idea::fromJson (*parsed.value))
            current = *idea;

    if (auto parsed = util::Json::parse (t.getProperty ("ideas").toString().toStdString()); parsed.value)
        history.ideasFromJson (*parsed.value);
    if (auto parsed = util::Json::parse (t.getProperty ("arrangement").toString().toStdString()); parsed.value)
        arrangementModel = gen::Arrangement::fromJson (*parsed.value);

    history.reset (current);
    selectedSlot = juce::jlimit (0, static_cast<int> (current.chords.slots.size()) - 1, static_cast<int> (t.getProperty ("selected", 0)));
    pendingRegenTicks = -1;
    render();
    snapshotParams();
    sendChangeMessage();
}

} // namespace bounce
