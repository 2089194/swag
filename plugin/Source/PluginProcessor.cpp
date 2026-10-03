#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "State/Parameters.h"

namespace bounce
{

namespace
{
const juce::Identifier kStateTag ("BounceState");
} // namespace

BounceProcessor::BounceProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", params::createLayout())
{
    internalSoundParam = apvts.getRawParameterValue (params::internalSound);
    midiOutParam = apvts.getRawParameterValue (params::midiOut);
    previewParam = apvts.getRawParameterValue (params::preview);
    levelParam = apvts.getRawParameterValue (params::chordsLevel);
    muteParam = apvts.getRawParameterValue (params::chordsMute);

    session = std::make_unique<Session> (apvts, [this] (const midi::MidiClip& clip) { publishPattern (clip); });
    session->setHostTempoSource ([this] { return hostBpm.load (std::memory_order_relaxed); });
}

BounceProcessor::~BounceProcessor() = default;

void BounceProcessor::publishPattern (const midi::MidiClip& clip)
{
    // Single producer: only ever called on the message thread by Session.
    chordPattern.write().setFrom (clip, ++patternVersion);
    chordPattern.publish();
    fallbackBpm.store (session != nullptr ? session->tempo() : 140.0, std::memory_order_relaxed);
}

//==============================================================================
void BounceProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    chordPlayer.prepare (sampleRate);
    keys.prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels());

    // Room for a dense block of generated + incoming events without reallocating.
    generated.ensureSize (8192);
}

void BounceProcessor::releaseResources()
{
    keys.reset();
}

bool BounceProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    return layouts.getMainInputChannelSet().isDisabled();
}

void BounceProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();

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
                transport.hostPlaying = pos->getIsPlaying();
            }
        }
    }
    transport.previewEnabled = previewParam->load() > 0.5f;

    const bool changed = chordPattern.acquire();
    const bool muted = muteParam->load() > 0.5f;

    generated.clear();
    chordPlayer.process (chordPattern.read(), changed, transport, numSamples, generated, ! muted);

    // Whatever the user plays in also reaches the keys (and MIDI out), handy for auditioning.
    generated.addEvents (midiMessages, 0, numSamples, 0);

    if (internalSoundParam->load() > 0.5f)
        keys.render (buffer, generated, juce::Decibels::decibelsToGain (levelParam->load(), -48.0f));

    if (midiOutParam->load() > 0.5f)
        midiMessages.swapWith (generated);
    else
        midiMessages.clear();

    loopPosition.store (chordPlayer.getLoopPosition(), std::memory_order_relaxed);
    hostBpm.store (reportedBpm, std::memory_order_relaxed);
    hostPlaying.store (transport.hostPlaying, std::memory_order_relaxed);
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

    const auto paramsTree = root.getChildWithName (apvts.state.getType());
    const auto sessionTree = root.getChildWithName ("Session").createCopy();

    auto apply = [this, paramsTree, sessionTree]
    {
        if (paramsTree.isValid())
            apvts.replaceState (paramsTree.createCopy());
        session->fromValueTree (sessionTree);
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
