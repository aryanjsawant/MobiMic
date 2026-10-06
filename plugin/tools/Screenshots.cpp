// Renders the plugin window to PNG files off-screen, for the README and website.
// Nothing is shown on screen and no audio device is opened.
//
//   MobiMicScreenshots <output folder> [name prefix, default "plugin"]
//
// Set MOBIMIC_STANDALONE=1 to render the app's layout instead of the plugin's.
// Prints {"port":N} once the server is up, then waits for a phone to connect
// before taking the "connected" shots. Prints {"shot":"name"} after each image.

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
        pump (250);
        const auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 2.0f);
        const auto file = folder.getChildFile (name + ".png");
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat().writeImageToStream (image, out);
        std::cout << "{\"shot\":\"" << name << "\"}" << std::endl;
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
                {
                    buffer.clear();
                    processor.processBlock (buffer, midi);
                }

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
        std::cout << "usage: MobiMicScreenshots <output folder> [name prefix]" << std::endl;
        return 1;
    }

    const auto folder = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
    const juce::String prefix (argc > 2 ? argv[2] : "plugin");
    folder.createDirectory();

    juce::ScopedJuceInitialiser_GUI gui;
    MobiMicProcessor processor;
    processor.setPlayConfigDetails (2, 2, 48000.0, 480);
    processor.prepareToPlay (48000.0, 480);
    pump (1300); // first timer ticks start the server and read the helper's status

    std::unique_ptr<juce::AudioProcessorEditor> window (processor.createEditor());
    auto& editor = dynamic_cast<MobiMicEditor&> (*window);

    save (editor, folder, prefix + "-waiting");

    editor.setSettingsVisible (true);
    save (editor, folder, prefix + "-settings");
    editor.setSettingsVisible (false);

    std::cout << "{\"port\":" << processor.getServer().getPort() << "}" << std::endl;

    FakeHost host (processor);
    host.startThread();

    for (int i = 0; i < 150 && ! processor.getServer().isPhoneConnected(); ++i)
        pump (100);

    if (processor.getServer().isPhoneConnected())
    {
        pump (3000);
        save (editor, folder, prefix + "-connected");

        processor.manualCapture = true;
        pump (3200);
        save (editor, folder, prefix + "-capturing");

        processor.manualCapture = false;
        pump (1500); // gives the script time to report a result, as the Ableton helper would
        save (editor, folder, prefix + "-take");
        processor.getTakes().getLastTake().deleteFile();
    }

    host.stopThread (2000);
    window.reset();
    return 0;
}
