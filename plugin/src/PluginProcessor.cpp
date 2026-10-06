#include "PluginProcessor.h"
#include "PluginEditor.h"

MobiMicProcessor::MobiMicProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "state", createLayout())
{
    bufferMs = apvts.getRawParameterValue ("buffer");
    gainDb = apvts.getRawParameterValue ("gain");
    autoCapture = apvts.getRawParameterValue ("autoCapture");

    // The server starts on the first tick rather than here, so a host that only
    // instantiates the plugin to scan it never opens a network port.
    startTimerHz (10);
}

MobiMicProcessor::~MobiMicProcessor()
{
    stopTimer();
    server->release (this);
    takes.stop();
}

juce::AudioProcessorValueTreeState::ParameterLayout MobiMicProcessor::createLayout()
{
    using namespace juce;

    return {
        std::make_unique<AudioParameterFloat> (ParameterID { "buffer", 1 }, "Buffer",
                                               NormalisableRange<float> (40.0f, 500.0f, 1.0f), 120.0f,
                                               AudioParameterFloatAttributes().withLabel ("ms")),
        std::make_unique<AudioParameterFloat> (ParameterID { "gain", 1 }, "Gain",
                                               NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
                                               AudioParameterFloatAttributes().withLabel ("dB")),
        std::make_unique<AudioParameterBool> (ParameterID { "autoCapture", 1 }, "Capture while recording", true)
    };
}

void MobiMicProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    scratch.assign ((size_t) juce::jmax (samplesPerBlock, 4096), 0.0f);
    gain.reset (sampleRate, 0.05);
    gain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (gainDb->load()));
}

bool MobiMicProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return in.isDisabled() || in == out;
}

void MobiMicProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (auto* playHead = getPlayHead())
        if (auto position = playHead->getPosition())
            hostRecording.store (position->getIsRecording(), std::memory_order_relaxed);

    // The track's own input is replaced by the phone.
    buffer.clear();

    if (! owner.load (std::memory_order_relaxed) || scratch.empty())
        return;

    gain.setTargetValue (juce::Decibels::decibelsToGain (gainDb->load()));

    const int numSamples = buffer.getNumSamples();
    const int numChannels = getTotalNumOutputChannels();

    for (int offset = 0; offset < numSamples;)
    {
        const int n = juce::jmin (numSamples - offset, (int) scratch.size());
        drift.pull (scratch.data(), n, getSampleRate(), (double) bufferMs->load());
        gain.applyGain (scratch.data(), n);

        for (int ch = 0; ch < numChannels; ++ch)
            buffer.copyFrom (ch, offset, scratch.data(), n);

        offset += n;
    }
}

//==============================================================================
void MobiMicProcessor::phonePcm (const int16_t* samples, int n)
{
    drift.push (samples, n);
    takes.write (samples, n);

    int peak = 0;

    for (int i = 0; i < n; ++i)
        peak = juce::jmax (peak, std::abs ((int) samples[i]));

    const auto level = (float) peak / 32768.0f;
    auto previous = inputPeak.load();

    while (level > previous && ! inputPeak.compare_exchange_weak (previous, level)) {}
}

void MobiMicProcessor::phoneConnectionChanged (bool)
{
    // Start clean on connect; on disconnect, drop the tail so it doesn't count as a glitch.
    drift.requestReset();
}

std::string MobiMicProcessor::phoneStatsJson()
{
    return "{\"buffer_ms\":" + std::to_string ((int) drift.getFillMs())
         + ",\"underruns\":" + std::to_string (drift.getUnderruns())
         + ",\"drops\":" + std::to_string (drift.getDrops())
         + ",\"capturing\":" + (takes.isActive() ? "true" : "false") + "}";
}

void MobiMicProcessor::timerCallback()
{
    if (! server->isRunning())
    {
        std::string error;
        server->start (error);
        serverError = error;
    }

    // Only one plugin instance can have the phone; the first one keeps it until it's removed.
    const bool nowOwner = server->claim (this);

    if (nowOwner && ! owner.load())
        drift.requestReset();

    owner = nowOwner;

    const bool wantCapture = nowOwner && (manualCapture.load() || (autoCapture->load() > 0.5f && hostRecording.load()));

    if (wantCapture && ! takes.isActive())
        takes.start();
    else if (! wantCapture && takes.isActive())
        takes.stop();
}

//==============================================================================
void MobiMicProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void MobiMicProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* MobiMicProcessor::createEditor()
{
    return new MobiMicEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MobiMicProcessor();
}
