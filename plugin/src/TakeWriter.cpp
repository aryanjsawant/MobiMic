#include "TakeWriter.h"

TakeWriter::TakeWriter()
{
    history.calloc ((size_t) historySize);
}

TakeWriter::~TakeWriter()
{
    finish();
}

juce::File TakeWriter::getTakesFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("MobiMic").getChildFile ("Takes");
}

void TakeWriter::begin (juce::int64 first)
{
    const juce::ScopedLock sl (lock);

    if (stream != nullptr)
        return;

    auto folder = getTakesFolder();
    folder.createDirectory();
    current = folder.getNonexistentChildFile (
        "MobiMic " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S"), ".wav", false);

    auto out = std::make_unique<juce::FileOutputStream> (current);

    if (out->failedToOpen())
        return;

    stream = std::move (out);
    samplesWritten = 0;
    samplesAtLastHeader = 0;
    ending = false;
    endSample = 0;

    // Samples that arrived between the moment recording started and now are still in the history.
    const auto now = received.load();
    firstSample = juce::jmax (first, now - historySize, (juce::int64) 0);
    writeHeader();

    for (auto pos = firstSample; pos < now;)
    {
        const auto index = pos & (historySize - 1);
        const auto n = (int) juce::jmin (now - pos, historySize - index);
        stream->write (history + index, (size_t) n * sizeof (int16_t));
        samplesWritten += n;
        pos += n;
    }

    open = true;
}

void TakeWriter::setEnd (juce::int64 end)
{
    const juce::ScopedLock sl (lock);

    if (stream == nullptr || ending.load())
        return;

    endSample = juce::jmax (end, firstSample);
    ending = true;

    // Already written past the end: cut the file back.
    if (firstSample + samplesWritten.load() > endSample)
    {
        samplesWritten = endSample - firstSample;
        stream->setPosition (44 + samplesWritten.load() * 2);
        stream->truncate();
    }
}

bool TakeWriter::isComplete() const
{
    const juce::ScopedLock sl (lock);
    return stream != nullptr && ending.load() && received.load() >= endSample;
}

juce::File TakeWriter::finish()
{
    const juce::ScopedLock sl (lock);

    if (stream == nullptr)
        return {};

    open = false;
    ending = false;
    writeHeader();
    stream.reset();

    if (samplesWritten.load() == 0)
    {
        current.deleteFile();
        return {};
    }

    lastTake = current;
    return current;
}

void TakeWriter::write (const int16_t* samples, int n)
{
    const juce::ScopedLock sl (lock);
    const auto position = received.load();

    for (int done = 0; done < n;)
    {
        const auto index = (position + done) & (historySize - 1);
        const auto chunk = (int) juce::jmin ((juce::int64) (n - done), historySize - index);
        std::memcpy (history + index, samples + done, (size_t) chunk * sizeof (int16_t));
        done += chunk;
    }

    if (stream != nullptr)
        append (samples, position, n);

    received.store (position + n, std::memory_order_release);
}

void TakeWriter::append (const int16_t* samples, juce::int64 position, int n)
{
    // Keep only the part of this packet that falls inside [firstSample, endSample).
    auto from = juce::jmax (position, firstSample + samplesWritten.load());
    auto to = position + n;

    if (ending.load())
        to = juce::jmin (to, endSample);

    if (to <= from)
        return;

    stream->write (samples + (from - position), (size_t) (to - from) * sizeof (int16_t)); // WAV and the wire format are both little-endian
    samplesWritten += to - from;

    // Keep the header roughly current so the file is usable even if the host crashes mid-take.
    if (samplesWritten.load() - samplesAtLastHeader >= 48000)
        writeHeader();
}

juce::File TakeWriter::getLastTake() const
{
    const juce::ScopedLock sl (lock);
    return lastTake;
}

void TakeWriter::writeHeader()
{
    const auto dataBytes = (juce::uint32) (samplesWritten.load() * 2);
    const auto end = stream->getPosition();

    stream->setPosition (0);
    stream->write ("RIFF", 4);
    stream->writeInt ((int) (36 + dataBytes));
    stream->write ("WAVEfmt ", 8);
    stream->writeInt (16);          // fmt chunk size
    stream->writeShort (1);         // PCM
    stream->writeShort (1);         // mono
    stream->writeInt (48000);       // sample rate
    stream->writeInt (48000 * 2);   // byte rate
    stream->writeShort (2);         // block align
    stream->writeShort (16);        // bits per sample
    stream->write ("data", 4);
    stream->writeInt ((int) dataBytes);

    if (end > 44)
        stream->setPosition (end);

    stream->flush();
    samplesAtLastHeader = samplesWritten.load();
}
