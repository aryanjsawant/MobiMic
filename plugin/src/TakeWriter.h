#pragma once

#include <juce_core/juce_core.h>

/** Writes the phone's stream straight to a WAV file, exactly as received.

    This bypasses the live jitter buffer: the stream arrives over TCP, so every
    sample is present and in order even when the live output had to glitch.
*/
class TakeWriter
{
public:
    ~TakeWriter() { stop(); }

    static juce::File getTakesFolder();

    void start();
    void stop();

    /** Network thread. */
    void write (const int16_t* samples, int n);

    bool isActive() const              { return active.load(); }
    double getSecondsWritten() const   { return (double) samplesWritten.load() / 48000.0; }
    juce::File getLastTake() const;

private:
    void writeHeader();

    mutable juce::CriticalSection lock;
    std::unique_ptr<juce::FileOutputStream> stream;
    juce::File current, lastTake;
    std::atomic<bool> active { false };
    std::atomic<juce::int64> samplesWritten { 0 };
    juce::int64 samplesAtLastHeader = 0;
};
