// Runs the real plugin against a fake host that presses record and stop, with no DAW.
// Checks that the plugin cuts a take to the recording and leaves a note for the Ableton helper.
//
//   set MOBIMIC_HANDOFF=<empty temp folder>     (so a running Ableton helper never sees the note)
//   MobiMicHostRecordTest [helper]
//
// With "helper" the fake host never reports recording; the test script plays the Ableton
// helper instead and sends the start/stop messages itself.
//
// Prints {"port":N}, waits for fake_phone.py, records for 5 seconds, prints what the host did.

#include "../src/PluginProcessor.h"

#include <iostream>

namespace
{
    struct FakePlayHead : juce::AudioPlayHead
    {
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setIsPlaying (true);
            info.setIsRecording (recording.load());
            info.setPpqPosition (beats.load());
            info.setBpm (120.0);
            return info;
        }

        std::atomic<bool> recording { false };
        std::atomic<double> beats { 0.0 };
    };

    /** 10 ms blocks at 48 kHz and 120 bpm, paced by the wall clock like a real audio device. */
    struct FakeHost : juce::Thread
    {
        FakeHost (MobiMicProcessor& p, FakePlayHead& h) : Thread ("fake host"), processor (p), playHead (h) {}

        void run() override
        {
            juce::AudioBuffer<float> buffer (2, 480);
            juce::MidiBuffer midi;
            const auto start = juce::Time::getMillisecondCounterHiRes();
            juce::int64 done = 0;
            bool wasRecording = false;

            while (! threadShouldExit())
            {
                const auto due = (juce::int64) ((juce::Time::getMillisecondCounterHiRes() - start) / 10.0);

                for (; done < due; ++done)
                {
                    const bool recording = wantRecording.load();

                    if (recording && ! wasRecording)
                        startBeats = playHead.beats.load();

                    if (recording)
                        ++recordedBlocks;

                    wasRecording = recording;
                    playHead.recording = recording;
                    buffer.clear();
                    processor.processBlock (buffer, midi);
                    playHead.beats = playHead.beats.load() + 0.02;
                }

                sleep (2);
            }
        }

        MobiMicProcessor& processor;
        FakePlayHead& playHead;
        std::atomic<bool> wantRecording { false };
        std::atomic<double> startBeats { -1.0 };
        std::atomic<int> recordedBlocks { 0 };
    };

    void pump (int ms)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
    }
}

int main (int argc, char**)
{
    const bool helperDriven = argc > 1;

    juce::ScopedJuceInitialiser_GUI gui;
    MobiMicProcessor processor;
    FakePlayHead playHead;
    processor.setPlayHead (&playHead);
    processor.setPlayConfigDetails (2, 2, 48000.0, 480);
    processor.prepareToPlay (48000.0, 480);
    pump (600); // first timer tick starts the server

    std::cout << "{\"port\":" << processor.getServer().getPort() << "}" << std::endl;

    FakeHost host (processor, playHead);
    host.startThread();

    for (int i = 0; i < 150 && ! processor.getServer().isPhoneConnected(); ++i)
        pump (100);

    const bool connected = processor.getServer().isPhoneConnected();

    pump (2000);
    host.wantRecording = ! helperDriven;
    pump (5000);
    host.wantRecording = false;
    pump (2500); // the take closes once its last samples arrive

    host.stopThread (2000);

    std::cout << "{\"connected\":" << (connected ? "true" : "false")
              << ",\"start_beats\":" << host.startBeats.load()
              << ",\"recorded_samples\":" << host.recordedBlocks.load() * 480 << "}" << std::endl;
    return 0;
}
