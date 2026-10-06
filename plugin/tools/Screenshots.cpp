// Renders the plugin window to PNG files off-screen, for the README.
// Nothing is shown on screen and no audio device is opened.
//
//   MobiMicScreenshots <output folder>
//
// Prints {"port":N} once the server is up, then waits for a phone
// (tests/fake_phone.py) to connect before taking the "connected" shots.

#include "../src/PluginEditor.h"
#include "../src/PluginProcessor.h"

#include <iostream>

namespace
{
    void pump (int ms)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
    }

    void save (juce::Component& editor, const juce::File& folder, const juce::String& name)
    {
        pump (200);
        const auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 2.0f);
        const auto file = folder.getChildFile (name);
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat().writeImageToStream (image, out);
    }

    /** Stands in for the host's audio thread so the buffer behaves as it would in a DAW. */
    struct FakeHost : juce::Thread
    {
        explicit FakeHost (MobiMicProcessor& p) : Thread ("fake host"), processor (p) {}

        void run() override
        {
            juce::AudioBuffer<float> buffer (2, 480);
            juce::MidiBuffer midi;
            const auto start = juce::Time::getMillisecondCounterHiRes();
            juce::int64 done = 0;

            while (! threadShouldExit())
            {
                const auto due = (juce::int64) ((juce::Time::getMillisecondCounterHiRes() - start) / 10.0);

                for (; done < due; ++done)
                    processor.processBlock (buffer, midi);

                sleep (2);
            }
        }

        MobiMicProcessor& processor;
    };
}

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::cout << "usage: MobiMicScreenshots <output folder>" << std::endl;
        return 1;
    }

    const auto folder = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
    folder.createDirectory();

    juce::ScopedJuceInitialiser_GUI gui;
    MobiMicProcessor processor;
    processor.setPlayConfigDetails (2, 2, 48000.0, 480);
    processor.prepareToPlay (48000.0, 480);
    pump (600); // first timer tick starts the server

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    save (*editor, folder, "plugin-waiting.png");

    std::cout << "{\"port\":" << processor.getServer().getPort() << "}" << std::endl;

    FakeHost host (processor);
    host.startThread();

    for (int i = 0; i < 150 && ! processor.getServer().isPhoneConnected(); ++i)
        pump (100);

    if (processor.getServer().isPhoneConnected())
    {
        pump (2500);
        save (*editor, folder, "plugin-connected.png");

        processor.manualCapture = true;
        pump (3200);
        save (*editor, folder, "plugin-capturing.png");

        processor.manualCapture = false;
        pump (500);
        save (*editor, folder, "plugin-take.png");
        processor.getTakes().getLastTake().deleteFile();
    }

    host.stopThread (2000);
    editor.reset();
    return 0;
}
