// Runs the real server, drift buffer and take writer without a plugin host or audio device.
// A fake "host" pulls audio at the given sample rate; fake_phone.py plays the phone.
//
//   MobiMicHeadlessTest <seconds> <hostSampleRate> <bufferMs> [window]
//
// With "window", the take is opened late for a range that began half a second earlier and is
// told to end slightly in the future, the way the plugin cuts a take to the host's record button.

#include <juce_core/juce_core.h>

#include "../src/DriftBuffer.h"
#include "../src/PhoneServer.h"
#include "../src/TakeWriter.h"

#include <iostream>

struct Sink : PhoneServer::Listener
{
    DriftBuffer drift;
    TakeWriter takes;

    void phonePcm (const int16_t* samples, int n) override
    {
        drift.push (samples, n);
        takes.write (samples, n);
    }

    void phoneConnectionChanged (bool) override
    {
        drift.requestReset();
    }

    std::string phoneStatsJson() override
    {
        return "{\"buffer_ms\":" + std::to_string ((int) drift.getFillMs())
             + ",\"underruns\":" + std::to_string (drift.getUnderruns())
             + ",\"drops\":" + std::to_string (drift.getDrops()) + ",\"capturing\":true}";
    }
};

int main (int argc, char** argv)
{
    const double seconds = argc > 1 ? std::atof (argv[1]) : 20.0;
    const double hostRate = argc > 2 ? std::atof (argv[2]) : 48000.0;
    const double bufferMs = argc > 3 ? std::atof (argv[3]) : 120.0;
    const bool window = argc > 4;
    juce::int64 first = 0, end = -1;

    Sink sink;
    PhoneServer server;
    std::string error;

    if (! server.start (error))
    {
        std::cout << "{\"error\":\"" << error << "\"}" << std::endl;
        return 1;
    }

    server.claim (&sink);
    if (! window)
        sink.takes.begin (0);
    std::cout << "{\"port\":" << server.getPort() << "}" << std::endl;

    // Pull in 10 ms blocks, as many as wall-clock time says a real host would have by now.
    const int block = (int) (hostRate / 100.0);
    std::vector<float> out ((size_t) block);
    const auto start = juce::Time::getMillisecondCounterHiRes();
    juce::int64 blocksDone = 0;

    for (;;)
    {
        const auto elapsed = (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0;

        if (elapsed >= seconds)
            break;

        const auto blocksDue = (juce::int64) (elapsed * 100.0);

        for (; blocksDone < blocksDue; ++blocksDone)
            sink.drift.pull (out.data(), block, hostRate, bufferMs);

        if (window && ! sink.takes.isOpen() && end < 0 && elapsed > 5.0 && sink.takes.getStreamPosition() > 48000)
        {
            first = sink.takes.getStreamPosition() - 24000;
            sink.takes.begin (first);
        }
        else if (window && sink.takes.isActive() && elapsed > seconds - 8.0)
        {
            end = sink.takes.getStreamPosition() + 4800;
            sink.takes.setEnd (end);
        }

        juce::Thread::sleep (2);
    }

    if (! window)
    {
        end = sink.takes.getStreamPosition();
        sink.takes.setEnd (end);
    }

    const bool complete = sink.takes.isComplete();
    sink.takes.finish();
    server.release (&sink);
    server.stop();

    std::cout << "{\"underruns\":" << sink.drift.getUnderruns()
              << ",\"drops\":" << sink.drift.getDrops()
              << ",\"first\":" << first << ",\"end\":" << end << ",\"complete\":" << (complete ? "true" : "false")
              << ",\"take\":\"" << sink.takes.getLastTake().getFullPathName().replace ("\\", "/") << "\"}" << std::endl;
    return 0;
}
