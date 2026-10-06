#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "DriftBuffer.h"
#include "PhoneServer.h"
#include "TakeWriter.h"

class MobiMicProcessor : public juce::AudioProcessor,
                         private PhoneServer::Listener,
                         private juce::Timer,
                         private juce::Thread
{
public:
    MobiMicProcessor();
    ~MobiMicProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                         { return true; }

    const juce::String getName() const override             { return "MobiMic"; }
    bool acceptsMidi() const override                       { return false; }
    bool producesMidi() const override                      { return false; }
    double getTailLengthSeconds() const override            { return 0.0; }

    int getNumPrograms() override                           { return 1; }
    int getCurrentProgram() override                        { return 0; }
    void setCurrentProgram (int) override                   {}
    const juce::String getProgramName (int) override        { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==============================================================================
    // For the editor (message thread).
    juce::AudioProcessorValueTreeState apvts;
    std::atomic<bool> manualCapture { false };

    PhoneServer& getServer()                { return *server; }
    const DriftBuffer& getBuffer() const    { return drift; }
    TakeWriter& getTakes()                  { return takes; }
    bool ownsPhone() const                  { return owner.load(); }
    juce::String getServerError() const     { return serverError; }
    float readInputPeak()                   { return inputPeak.exchange (0.0f); }

    /** True while the Ableton helper script is running (it places finished takes on the timeline). */
    bool isHelperActive() const             { return helperActive; }
    juce::String getHelperResult() const    { return helperResult; }
    static juce::File getHandoffFolder();

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void phonePcm (const int16_t*, int) override;
    void phoneConnectionChanged (bool) override;
    std::string phoneStatsJson() override;
    void timerCallback() override;

    /** A recording started or stopped in the host, stamped on the audio thread. */
    struct RecordEvent
    {
        bool start = false;
        double beats = 0.0;         // host timeline position; NaN if the host doesn't report one
        double bpm = 120.0;
        juce::int64 streamPosition = 0;
    };

    enum class TakeKind { none, host, manual };

    void pushRecordEvent (const RecordEvent&);
    void handleRecordEvents();
    void handleRecordEvent (const RecordEvent&, const char* source);
    void run() override;
    void writePluginStatus();
    void closeTake();
    static void log (const juce::String& message);
    void readHelperStatus();

    juce::SharedResourcePointer<PhoneServer> server;
    DriftBuffer drift;
    TakeWriter takes;

    std::atomic<float>* bufferMs = nullptr;
    std::atomic<float>* gainDb = nullptr;
    std::atomic<float>* autoCapture = nullptr;
    std::atomic<float>* offsetMs = nullptr;
    std::atomic<float>* monitor = nullptr;

    std::atomic<bool> owner { false }, hostRecording { false };
    std::atomic<float> inputPeak { 0.0f };
    juce::String serverError;

    std::vector<float> scratch;
    juce::SmoothedValue<float> gain;    // fades live monitoring in and out

    // Audio thread -> message thread.
    juce::AbstractFifo eventFifo { 32 };
    RecordEvent events[32];
    bool wasRecording = false;
    double expectedBeats = 0.0;

    // The Ableton helper tells us when recording starts and stops, over a local UDP socket.
    juce::DatagramSocket controlSocket { false };
    juce::CriticalSection helperLock;
    std::vector<RecordEvent> helperEvents;

    // Message thread.
    TakeKind takeKind = TakeKind::none;
    double takeBeats = 0.0;
    juce::uint32 takeDeadline = 0;
    bool helperActive = false;
    juce::String helperResult;
    int ticks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MobiMicProcessor)
};
