#include "PluginProcessor.h"
#include "PluginEditor.h"

MobiMicProcessor::MobiMicProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      Thread ("MobiMic control"),
      apvts (*this, nullptr, "state", createLayout())
{
    bufferMs = apvts.getRawParameterValue ("buffer");
    gainDb = apvts.getRawParameterValue ("gain");
    autoCapture = apvts.getRawParameterValue ("autoCapture");
    offsetMs = apvts.getRawParameterValue ("offset");
    monitor = apvts.getRawParameterValue ("monitor");

    // The server starts on the first tick rather than here, so a host that only
    // instantiates the plugin to scan it never opens a network port.
    startTimerHz (10);

    // Loopback only: this is how the Ableton helper says "recording started / stopped".
    if (controlSocket.bindToPort (0, "127.0.0.1"))
        startThread();
}

MobiMicProcessor::~MobiMicProcessor()
{
    stopTimer();
    signalThreadShouldExit();
    controlSocket.shutdown();
    stopThread (2000);
    server->release (this);
    takes.finish();
}

juce::AudioProcessorValueTreeState::ParameterLayout MobiMicProcessor::createLayout()
{
    using namespace juce;

    return {
        std::make_unique<AudioParameterFloat> (ParameterID { "buffer", 1 }, "Buffer",
                                               NormalisableRange<float> (40.0f, 500.0f, 1.0f), 120.0f,
                                               AudioParameterFloatAttributes().withLabel ("ms")),
        std::make_unique<AudioParameterFloat> (ParameterID { "gain", 1 }, "Gain",
                                               NormalisableRange<float> (-24.0f, 36.0f, 0.1f), 0.0f,
                                               AudioParameterFloatAttributes().withLabel ("dB")),
        std::make_unique<AudioParameterBool> (ParameterID { "autoCapture", 1 }, "Capture while recording", true),
        std::make_unique<AudioParameterFloat> (ParameterID { "offset", 1 }, "Take offset",
                                               NormalisableRange<float> (0.0f, 300.0f, 1.0f), 50.0f,
                                               AudioParameterFloatAttributes().withLabel ("ms")),
        std::make_unique<AudioParameterBool> (ParameterID { "monitor", 1 }, "Monitor", true)
    };
}

void MobiMicProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    scratch.assign ((size_t) juce::jmax (samplesPerBlock, 4096), 0.0f);
    gain.reset (sampleRate, 0.05);
    gain.setCurrentAndTargetValue (monitor->load() > 0.5f ? 1.0f : 0.0f);
}

bool MobiMicProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return in.isDisabled() || in == out;
}

void MobiMicProcessor::pushRecordEvent (const RecordEvent& event)
{
    int start1, size1, start2, size2;
    eventFifo.prepareToWrite (1, start1, size1, start2, size2);

    if (size1 > 0)
    {
        events[start1] = event;
        eventFifo.finishedWrite (1);
    }
}

void MobiMicProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numInputs = getTotalNumInputChannels();
    const int numOutputs = getTotalNumOutputChannels();
    const bool isOwner = owner.load (std::memory_order_relaxed);

    // Stamp the exact moment the host starts and stops recording, so the take can be cut to match.
    bool recording = false;
    double beats = std::numeric_limits<double>::quiet_NaN(), bpm = 120.0;

    if (auto* playHead = getPlayHead())
    {
        if (auto position = playHead->getPosition())
        {
            recording = position->getIsRecording() && position->getIsPlaying();

            if (auto ppq = position->getPpqPosition())
                beats = *ppq;

            if (auto tempo = position->getBpm())
                bpm = *tempo;
        }
    }

    if (! isOwner)
        recording = false;

    // A jump while recording (loop recording) ends one take and starts the next.
    const bool jumped = recording && wasRecording && ! std::isnan (beats) && std::abs (beats - expectedBeats) > 0.1;

    if (recording != wasRecording || jumped)
    {
        const auto position = takes.getStreamPosition();

        if (wasRecording)
            pushRecordEvent ({ false, beats, bpm, position });

        if (recording)
            pushRecordEvent ({ true, beats, bpm, position });
    }

    wasRecording = recording;
    expectedBeats = beats + (double) numSamples / getSampleRate() * bpm / 60.0;
    hostRecording.store (recording, std::memory_order_relaxed);

    // Whatever is already on the track (earlier takes) passes through; the live phone is added on top.
    for (int ch = numInputs; ch < numOutputs; ++ch)
        buffer.clear (ch, 0, numSamples);

    if (! isOwner || scratch.empty())
        return;

    gain.setTargetValue (monitor->load() > 0.5f ? 1.0f : 0.0f);

    for (int offset = 0; offset < numSamples;)
    {
        const int n = juce::jmin (numSamples - offset, (int) scratch.size());
        drift.pull (scratch.data(), n, getSampleRate(), (double) bufferMs->load());
        gain.applyGain (scratch.data(), n);

        for (int ch = 0; ch < numOutputs; ++ch)
            buffer.addFrom (ch, offset, scratch.data(), n);

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
         + ",\"capturing\":" + (takes.isActive() ? "true" : "false")
         // The phone applies the gain itself, before reducing the audio to 16 bits, so boosting adds no noise.
         + ",\"gain\":" + std::to_string (juce::Decibels::decibelsToGain (gainDb->load())) + "}";
}

juce::File MobiMicProcessor::getHandoffFolder()
{
    // Tests point this somewhere private so a running Ableton helper never picks up their takes.
    const auto override = juce::SystemStats::getEnvironmentVariable ("MOBIMIC_HANDOFF", {});

    if (override.isNotEmpty())
        return juce::File (override);

    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("MobiMic");
}

void MobiMicProcessor::log (const juce::String& message)
{
    // A short history of what happened to each recording, for troubleshooting.
    const auto file = getHandoffFolder().getChildFile ("log.txt");

    if (file.getSize() > 200 * 1024)
        file.deleteFile();

    file.appendText (juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S  ") + message + "\n");
}

void MobiMicProcessor::closeTake()
{
    const auto samples = takes.getSamplesWritten();
    const auto file = takes.finish();
    const bool handOff = file.existsAsFile() && takeKind == TakeKind::host && ! std::isnan (takeBeats);

    log (juce::String (takeKind == TakeKind::host ? "Host" : "Manual") + " take closed: "
         + juce::String ((double) samples / 48000.0, 2) + " s"
         + (samples == 0 ? " (nothing received from the phone, no file kept)" : ", " + file.getFileName())
         + (handOff ? ", handed to the Ableton helper at beat " + juce::String (takeBeats, 3) : juce::String()));

    // Hand host-recorded takes to the Ableton helper script, which places them on the timeline.
    if (handOff)
    {
        auto* info = new juce::DynamicObject();
        info->setProperty ("file", file.getFullPathName());
        info->setProperty ("start_beats", takeBeats);
        info->setProperty ("samples", samples);
        info->setProperty ("created", juce::Time::currentTimeMillis() / 1000);

        const auto pending = getHandoffFolder().getChildFile ("pending");
        pending.createDirectory();
        pending.getChildFile (juce::Uuid().toString() + ".json").replaceWithText (juce::JSON::toString (juce::var (info)));
    }

    if (takeKind == TakeKind::manual)
        manualCapture = false;

    takeKind = TakeKind::none;
}

void MobiMicProcessor::handleRecordEvent (const RecordEvent& e, const char* source)
{
    if (takeKind == TakeKind::manual)
        return;

    const auto offsetSamples = (juce::int64) (offsetMs->load() * 48.0f);

    if (e.start)
    {
        if (takeKind == TakeKind::host)
            closeTake();

        if (autoCapture->load() < 0.5f)
        {
            log (juce::String (source) + " started recording, but 'Capture while the DAW records' is off");
            return;
        }

        // What arrives now was played a moment ago (phone + network delay), so start that much later in the stream.
        auto first = e.streamPosition + offsetSamples;
        takeBeats = e.beats;

        // Count-in: the host is "recording" before bar 1. Skip that part.
        if (takeBeats < 0.0)
        {
            first += (juce::int64) (-takeBeats * 60.0 / e.bpm * 48000.0);
            takeBeats = 0.0;
        }

        takes.begin (first);
        takeKind = TakeKind::host;
        log (juce::String (source) + " started recording at beat " + juce::String (e.beats, 3)
             + (server->isPhoneConnected() ? "" : " (no phone connected)"));
    }
    else if (takeKind == TakeKind::host && takes.isActive())
    {
        takes.setEnd (e.streamPosition + offsetSamples);
        takeDeadline = juce::Time::getMillisecondCounter() + 1500;
    }
}

void MobiMicProcessor::handleRecordEvents()
{
    // What the host itself reported through the plugin interface.
    int start1, size1, start2, size2;
    eventFifo.prepareToRead (eventFifo.getNumReady(), start1, size1, start2, size2);

    // With the Ableton helper running, it is the one source of truth (it sees Live's record
    // button directly), so the host's own flags are ignored to avoid starting takes twice.
    if (! helperActive)
    {
        for (int i = 0; i < size1; ++i)  handleRecordEvent (events[start1 + i], "Host");
        for (int i = 0; i < size2; ++i)  handleRecordEvent (events[start2 + i], "Host");
    }

    eventFifo.finishedRead (size1 + size2);

    std::vector<RecordEvent> fromHelper;

    {
        const juce::ScopedLock sl (helperLock);
        fromHelper.swap (helperEvents);
    }

    for (auto& e : fromHelper)
        handleRecordEvent (e, "Ableton helper");
}

void MobiMicProcessor::run()
{
    char buffer[512];

    while (! threadShouldExit())
    {
        if (controlSocket.waitUntilReady (true, 200) != 1)
            continue;

        const int n = controlSocket.read (buffer, (int) sizeof (buffer) - 1, false);

        if (n <= 0)
            continue;

        // Stamp the stream position first: this is the moment the helper saw Live's transport change.
        const auto position = takes.getStreamPosition();
        const auto message = juce::JSON::parse (juce::String::fromUTF8 (buffer, n));
        const auto command = message.getProperty ("cmd", {}).toString();

        if (! owner.load() || (command != "start" && command != "stop"))
            continue;

        RecordEvent e;
        e.start = command == "start";
        e.beats = (double) message.getProperty ("beats", 0.0);
        e.bpm = (double) message.getProperty ("bpm", 120.0);
        e.streamPosition = position;

        const juce::ScopedLock sl (helperLock);
        helperEvents.push_back (e);
    }
}

void MobiMicProcessor::writePluginStatus()
{
    // Tells the helper where to send its start/stop messages.
    auto* info = new juce::DynamicObject();
    info->setProperty ("control_port", controlSocket.getBoundPort());
    info->setProperty ("time", juce::Time::currentTimeMillis() / 1000);
    info->setProperty ("phone_connected", server->isPhoneConnected());
    getHandoffFolder().createDirectory();
    getHandoffFolder().getChildFile ("plugin.json").replaceWithText (juce::JSON::toString (juce::var (info)));
}

void MobiMicProcessor::readHelperStatus()
{
    const auto status = juce::JSON::parse (getHandoffFolder().getChildFile ("helper.json"));
    const auto age = juce::Time::currentTimeMillis() / 1000 - (juce::int64) status.getProperty ("time", 0);
    helperActive = status.isObject() && age < 5;
    helperResult = status.getProperty ("result", {}).toString();
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

    if (++ticks % 10 == 1)
    {
        readHelperStatus();

        if (nowOwner)
            writePluginStatus();
    }

    handleRecordEvents();

    // The Capture button.
    if (manualCapture.load() && takeKind == TakeKind::none && nowOwner)
    {
        takes.begin (takes.getStreamPosition());
        takeKind = TakeKind::manual;
    }
    else if (! manualCapture.load() && takeKind == TakeKind::manual && takes.isActive())
    {
        takes.setEnd (takes.getStreamPosition());
        takeDeadline = juce::Time::getMillisecondCounter();
    }
    else if (manualCapture.load() && takeKind == TakeKind::host)
    {
        manualCapture = false;
    }

    // A take that has been told to stop closes once its last samples have arrived.
    if (takeKind != TakeKind::none && takes.isOpen() && ! takes.isActive()
        && (takes.isComplete() || juce::Time::getMillisecondCounter() >= takeDeadline))
        closeTake();
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
