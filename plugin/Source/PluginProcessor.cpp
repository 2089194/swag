#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "State/Parameters.h"

namespace bounce
{

namespace
{
const juce::Identifier kStateTag ("BounceState");
const juce::Identifier kDrumSamplesTag ("DrumSamples");

AudioPart audioPartFor (PlaybackSlot s)
{
    switch (s)
    {
        case PlaybackSlot::Chords:  return AudioPart::Chords;
        case PlaybackSlot::Bass:    return AudioPart::Bass;
        case PlaybackSlot::Melody:
        case PlaybackSlot::Counter: return AudioPart::Melody;
        case PlaybackSlot::DrumsOut:
        case PlaybackSlot::DrumsInternal:
        case PlaybackSlot::NumSlots: break;
    }
    return AudioPart::Drums;
}
} // namespace

BounceProcessor::BounceProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", params::createLayout())
{
    formats.registerBasicFormats();

    pp.preview = apvts.getRawParameterValue (params::preview);
    pp.syncHost = apvts.getRawParameterValue (params::syncHost);
    pp.internalSound = apvts.getRawParameterValue (params::internalSound);
    pp.midiOut = apvts.getRawParameterValue (params::midiOut);
    pp.keysWobble = apvts.getRawParameterValue (params::keysWobble);
    pp.bassDecay = apvts.getRawParameterValue (params::bassDecay);
    pp.bassGlide = apvts.getRawParameterValue (params::bassGlide);
    pp.bassPunch = apvts.getRawParameterValue (params::bassPunch);
    pp.leadType = apvts.getRawParameterValue (params::leadType);
    const char* fields[] = { params::fLevel, params::fMute, params::fSolo, params::fDrive, params::fTone, params::fDelay, params::fReverb };
    for (int p = 0; p < numAudioParts; ++p)
        for (int f = 0; f < ParamPointers::NumFields; ++f)
            pp.mix[static_cast<size_t> (p)][static_cast<size_t> (f)] = apvts.getRawParameterValue (params::mixId (static_cast<AudioPart> (p), fields[f]));
    for (int l = 0; l < gen::numDrumLanes; ++l)
        drumSamplePaths.add ({});

    session = std::make_unique<Session> (apvts, [this] (PlaybackSlot slot, const midi::MidiClip& clip) { publishPattern (slot, clip); });
    session->setHostTempoSource ([this] { return hostBpm.load (std::memory_order_relaxed); });
    startTimer (2000);
}

BounceProcessor::~BounceProcessor()
{
    stopTimer();
}

void BounceProcessor::publishPattern (PlaybackSlot slot, const midi::MidiClip& clip)
{
    // Single producer: only ever called on the message thread by Session.
    const auto i = static_cast<size_t> (slot);
    patterns[i].write().setFrom (clip, ++patternVersions[i]);
    patterns[i].publish();
    fallbackBpm.store (session != nullptr ? session->tempo() : 140.0, std::memory_order_relaxed);
}

//==============================================================================
void BounceProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    for (auto& p : players)
        p.prepare (sampleRate);
    for (auto& m : partMidi)
        m.ensureSize (4096);
    keysMidi.ensureSize (8192);
    leadMidi.ensureSize (8192);
    midiOutBuffer.ensureSize (16384);

    keys.prepare (sampleRate, samplesPerBlock, 2);
    bass808.prepare (sampleRate);
    lead.prepare (sampleRate);
    drums.prepare (sampleRate);
    mixer.prepare (sampleRate, samplesPerBlock);

    if (captureBuffer.getNumSamples() != static_cast<int> (sampleRate * 60.0))
        captureBuffer.setSize (1, static_cast<int> (sampleRate * 60.0), false, true, false);
}

void BounceProcessor::releaseResources()
{
    keys.reset();
    bass808.reset();
    lead.reset();
    drums.reset();
    mixer.reset();
}

bool BounceProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    const auto in = layouts.getMainInputChannelSet();
    return in.isDisabled() || in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
}

void BounceProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    // --- Lab capture from the (optional) input bus, before the buffer is reused for output.
    if (capturing.load (std::memory_order_relaxed) && getTotalNumInputChannels() > 0)
    {
        const int inCh = juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels());
        int w = captureWrite.load (std::memory_order_relaxed);
        const int cap = captureBuffer.getNumSamples();
        auto* dst = captureBuffer.getWritePointer (0);
        for (int i = 0; i < numSamples && w < cap; ++i, ++w)
        {
            float s = 0.0f;
            for (int ch = 0; ch < inCh; ++ch)
                s += buffer.getSample (ch, i);
            dst[w] = s / static_cast<float> (inCh);
        }
        captureWrite.store (w, std::memory_order_relaxed);
    }

    // --- Transport.
    PatternPlayer::Transport transport;
    transport.bpm = fallbackBpm.load (std::memory_order_relaxed);
    double reportedBpm = 0.0;
    if (auto* ph = getPlayHead())
    {
        if (const auto pos = ph->getPosition())
        {
            if (const auto bpm = pos->getBpm(); bpm && *bpm > 0.0)
                transport.bpm = reportedBpm = *bpm;
            if (const auto ppq = pos->getPpqPosition())
            {
                transport.hostPpq = *ppq;
                // Bounce only follows FL's play button when Sync is on. Otherwise it stays silent
                // while you play the notes you dragged into FL, and sounds only when you press Play.
                transport.hostPlaying = pos->getIsPlaying() && pp.syncHost->load() > 0.5f;
            }
        }
    }
    transport.previewEnabled = pp.preview->load() > 0.5f;

    // --- Mute / solo per audio part.
    auto mixValue = [this] (int part, ParamPointers::Field f) { return pp.mix[static_cast<size_t> (part)][static_cast<size_t> (f)]->load(); };
    std::array<bool, numAudioParts> enabled {};
    bool anySolo = false;
    for (int p = 0; p < numAudioParts; ++p)
        anySolo |= mixValue (p, ParamPointers::Solo) > 0.5f;
    for (int p = 0; p < numAudioParts; ++p)
        enabled[static_cast<size_t> (p)] = mixValue (p, ParamPointers::Mute) < 0.5f && (! anySolo || mixValue (p, ParamPointers::Solo) > 0.5f);

    // --- Patterns -> MIDI.
    for (int s = 0; s < numPlaybackSlots; ++s)
    {
        const auto i = static_cast<size_t> (s);
        const bool changed = patterns[i].acquire();
        partMidi[i].clear();
        players[i].process (patterns[i].read(), changed, transport, numSamples, partMidi[i],
                            enabled[static_cast<size_t> (audioPartFor (static_cast<PlaybackSlot> (s)))]);
    }

    // --- Voices.
    mixer.beginBlock (numSamples);
    if (pp.internalSound->load() > 0.5f)
    {
        keysMidi.clear();
        keysMidi.addEvents (partMidi[static_cast<size_t> (PlaybackSlot::Chords)], 0, numSamples, 0);
        keysMidi.addEvents (midiMessages, 0, numSamples, 0); // what the user plays reaches the keys too
        keys.render (mixer.partBuffer (AudioPart::Chords), keysMidi, pp.keysWobble->load());

        Bass808::Settings bs;
        bs.decaySeconds = pp.bassDecay->load();
        bs.glide = pp.bassGlide->load();
        bs.punch = pp.bassPunch->load();
        bass808.render (mixer.partBuffer (AudioPart::Bass), partMidi[static_cast<size_t> (PlaybackSlot::Bass)], bs);

        leadMidi.clear();
        leadMidi.addEvents (partMidi[static_cast<size_t> (PlaybackSlot::Melody)], 0, numSamples, 0);
        leadMidi.addEvents (partMidi[static_cast<size_t> (PlaybackSlot::Counter)], 0, numSamples, 0);
        const auto type = static_cast<LeadSynth::Type> (juce::jlimit (0, 2, juce::roundToInt (pp.leadType->load())));
        lead.render (mixer.partBuffer (AudioPart::Melody), leadMidi, type);

        drums.render (mixer.partBuffer (AudioPart::Drums), partMidi[static_cast<size_t> (PlaybackSlot::DrumsInternal)]);
    }

    std::array<Mixer::Strip, numAudioParts> strips;
    for (int p = 0; p < numAudioParts; ++p)
    {
        auto& s = strips[static_cast<size_t> (p)];
        s.levelDb = mixValue (p, ParamPointers::Level);
        s.audible = enabled[static_cast<size_t> (p)];
        s.drive = mixValue (p, ParamPointers::Drive);
        s.tone = mixValue (p, ParamPointers::Tone);
        s.delaySend = mixValue (p, ParamPointers::Delay);
        s.reverbSend = mixValue (p, ParamPointers::Reverb);
    }
    mixer.process (buffer, strips, transport.bpm);

    // --- MIDI out: every part (drums with the user's note map) plus what was played in.
    midiOutBuffer.clear();
    if (pp.midiOut->load() > 0.5f)
    {
        for (auto slot : { PlaybackSlot::Chords, PlaybackSlot::Bass, PlaybackSlot::Melody, PlaybackSlot::Counter, PlaybackSlot::DrumsOut })
            midiOutBuffer.addEvents (partMidi[static_cast<size_t> (slot)], 0, numSamples, 0);
        midiOutBuffer.addEvents (midiMessages, 0, numSamples, 0);
    }
    midiMessages.swapWith (midiOutBuffer);

    loopPosition.store (players[static_cast<size_t> (PlaybackSlot::Chords)].getLoopPosition(), std::memory_order_relaxed);
    hostBpm.store (reportedBpm, std::memory_order_relaxed);
    hostPlaying.store (transport.hostPlaying, std::memory_order_relaxed);
}

//==============================================================================
bool BounceProcessor::loadDrumSample (gen::DrumLane lane, const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->sampleRate <= 0.0)
        return false;

    const auto length = static_cast<int> (std::min<juce::int64> (reader->lengthInSamples, static_cast<juce::int64> (reader->sampleRate * 10.0)));
    if (length <= 1)
        return false;

    juce::AudioBuffer<float> tmp (static_cast<int> (reader->numChannels), length);
    reader->read (&tmp, 0, length, 0, true, true);

    auto sample = std::make_unique<DrumSample>();
    sample->sampleRate = reader->sampleRate;
    sample->name = file.getFileNameWithoutExtension();
    sample->data.resize (static_cast<size_t> (length));
    for (int i = 0; i < length; ++i)
    {
        float s = 0.0f;
        for (int ch = 0; ch < tmp.getNumChannels(); ++ch)
            s += tmp.getSample (ch, i);
        sample->data[static_cast<size_t> (i)] = s / static_cast<float> (juce::jmax (1, tmp.getNumChannels()));
    }

    drums.setSample (lane, std::move (sample));
    drumSamplePaths.set (static_cast<int> (lane), file.getFullPathName());
    return true;
}

void BounceProcessor::resetDrumSample (gen::DrumLane lane)
{
    drums.setSample (lane, nullptr);
    drumSamplePaths.set (static_cast<int> (lane), {});
}

void BounceProcessor::startCapture()
{
    captureWrite.store (0);
    capturing.store (true);
}

juce::AudioBuffer<float> BounceProcessor::stopCapture()
{
    capturing.store (false);
    const int n = captureWrite.load();
    juce::AudioBuffer<float> out (1, juce::jmax (0, n));
    if (n > 0)
        out.copyFrom (0, 0, captureBuffer, 0, 0, n);
    return out;
}

double BounceProcessor::getCaptureSeconds() const noexcept
{
    return currentSampleRate > 0.0 ? captureWrite.load() / currentSampleRate : 0.0;
}

//==============================================================================
juce::AudioProcessorEditor* BounceProcessor::createEditor()
{
    return new BounceEditor (*this);
}

void BounceProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root (kStateTag);
    root.setProperty ("pluginVersion", JucePlugin_VersionString, nullptr);
    root.appendChild (apvts.copyState(), nullptr);
    root.appendChild (session->toValueTree(), nullptr);

    juce::ValueTree samples (kDrumSamplesTag);
    for (int l = 0; l < gen::numDrumLanes; ++l)
        samples.setProperty ("lane" + juce::String (l), drumSamplePaths[l], nullptr);
    root.appendChild (samples, nullptr);

    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, destData);
}

void BounceProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    const auto root = juce::ValueTree::fromXml (*xml);
    if (! root.hasType (kStateTag))
        return;

    const auto paramsTree = root.getChildWithName (apvts.state.getType()).createCopy();
    const auto sessionTree = root.getChildWithName ("Session").createCopy();
    const auto samplesTree = root.getChildWithName (kDrumSamplesTag).createCopy();

    auto apply = [this, paramsTree, sessionTree, samplesTree]
    {
        if (paramsTree.isValid())
            apvts.replaceState (paramsTree);
        // A reopened project never starts playing by itself.
        if (auto* play = apvts.getParameter (params::preview))
            play->setValueNotifyingHost (0.0f);
        session->fromValueTree (sessionTree);
        for (int l = 0; l < gen::numDrumLanes; ++l)
        {
            const auto path = samplesTree.getProperty ("lane" + juce::String (l)).toString();
            const auto lane = static_cast<gen::DrumLane> (l);
            if (path.isNotEmpty() && juce::File (path).existsAsFile())
                loadDrumSample (lane, juce::File (path));
            else if (hasCustomDrumSample (lane))
                resetDrumSample (lane);
        }
    };

    // Session lives on the message thread; a few hosts restore state from elsewhere.
    if (juce::MessageManager::existsAndIsCurrentThread())
        apply();
    else
        juce::MessageManager::callAsync ([safe = juce::WeakReference<BounceProcessor> (this), apply]
        {
            if (safe != nullptr)
                apply();
        });
}

} // namespace bounce

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new bounce::BounceProcessor();
}
