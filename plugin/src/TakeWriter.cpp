#include "TakeWriter.h"

juce::File TakeWriter::getTakesFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("MobiMic").getChildFile ("Takes");
}

void TakeWriter::start()
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
    writeHeader();
    active = true;
}

void TakeWriter::stop()
{
    const juce::ScopedLock sl (lock);

    if (stream == nullptr)
        return;

    active = false;
    writeHeader();
    stream.reset();

    if (samplesWritten.load() == 0)
        current.deleteFile();
    else
        lastTake = current;
}

void TakeWriter::write (const int16_t* samples, int n)
{
    const juce::ScopedLock sl (lock);

    if (stream == nullptr)
        return;

    stream->write (samples, (size_t) n * sizeof (int16_t)); // WAV and the wire format are both little-endian
    samplesWritten += n;

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
