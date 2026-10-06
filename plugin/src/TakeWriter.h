#pragma once

#include <juce_core/juce_core.h>

/** Writes the phone's stream straight to a WAV file, exactly as received.

    This bypasses the live jitter buffer: the stream arrives over TCP, so every
    sample is present and in order even when the live output had to glitch.

    Positions are counted in "stream samples": the running total of samples
    received from the phone. A take is the range [first, end) of that count, so
    the audio thread can mark the exact start and end of a recording and the
    file is cut to match, even though the file itself is opened a little later.
*/
class TakeWriter
{
public:
    TakeWriter();
    ~TakeWriter();

    static juce::File getTakesFolder();

    /** Total samples received so far. Safe to call from the audio thread. */
    juce::int64 getStreamPosition() const   { return received.load (std::memory_order_acquire); }

    /** Opens a new take starting at stream position `first` (may be slightly in the past or future). */
    void begin (juce::int64 first);

    /** The take stops at stream position `end`; samples up to there are still accepted. */
    void setEnd (juce::int64 end);

    /** True once every sample up to the end position has arrived. */
    bool isComplete() const;

    /** Closes the take. Returns the file, or an invalid File if nothing was written. */
    juce::File finish();

    /** Network thread. */
    void write (const int16_t* samples, int n);

    bool isOpen() const                { return open.load(); }
    bool isActive() const              { return open.load() && ! ending.load(); }
    double getSecondsWritten() const   { return (double) samplesWritten.load() / 48000.0; }
    juce::int64 getSamplesWritten() const { return samplesWritten.load(); }
    juce::File getLastTake() const;

private:
    void append (const int16_t* samples, juce::int64 position, int n);
    void writeHeader();

    static constexpr juce::int64 historySize = 1 << 17; // ~2.7 s kept so a take can start slightly in the past

    mutable juce::CriticalSection lock;
    std::unique_ptr<juce::FileOutputStream> stream;
    juce::File current, lastTake;
    juce::HeapBlock<int16_t> history;
    juce::int64 firstSample = 0, endSample = 0, samplesAtLastHeader = 0;

    std::atomic<juce::int64> received { 0 }, samplesWritten { 0 };
    std::atomic<bool> open { false }, ending { false };
};
