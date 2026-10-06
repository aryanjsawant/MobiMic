#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "DriftBuffer.h"
#include "PhoneServer.h"
#include "TakeWriter.h"

class MobiMicProcessor : public juce::AudioProcessor,
                         private PhoneServer::Listener,
                         private juce::Timer
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

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void phonePcm (const int16_t*, int) override;
    void phoneConnectionChanged (bool) override;
    std::string phoneStatsJson() override;
    void timerCallback() override;

    juce::SharedResourcePointer<PhoneServer> server;
    DriftBuffer drift;
    TakeWriter takes;

    std::atomic<float>* bufferMs = nullptr;
    std::atomic<float>* gainDb = nullptr;
    std::atomic<float>* autoCapture = nullptr;

    std::atomic<bool> owner { false }, hostRecording { false };
    std::atomic<float> inputPeak { 0.0f };
    juce::String serverError;

    std::vector<float> scratch;
    juce::SmoothedValue<float> gain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MobiMicProcessor)
};
