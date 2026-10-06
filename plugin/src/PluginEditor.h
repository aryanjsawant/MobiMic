#pragma once

#include "PluginProcessor.h"

/** The last captured take; drag it out of the plugin window onto a track. */
class TakeChip : public juce::Component
{
public:
    void setTake (const juce::File& newFile, const juce::String& newText);
    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override   { dragging = false; }

private:
    juce::File file;
    juce::String text;
    bool dragging = false;
};

class MobiMicEditor : public juce::AudioProcessorEditor,
                      private juce::Timer
{
public:
    explicit MobiMicEditor (MobiMicProcessor&);
    ~MobiMicEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshAddress();

    MobiMicProcessor& processor;

    juce::Label status, urlLabel, stats, helperLabel;
    juce::TextButton nextAddress { "Other address" }, captureButton { "Capture" }, openFolder { "Open takes folder" };
    juce::ToggleButton autoCapture { "Capture while the DAW records" }, monitor { "Hear the phone live" };
    juce::Slider bufferSlider, gainSlider, offsetSlider;
    juce::Label bufferLabel { {}, "Buffer" }, gainLabel { {}, "Gain" }, offsetLabel { {}, "Offset" };
    TakeChip takeChip;

    juce::AudioProcessorValueTreeState::SliderAttachment bufferAttachment, gainAttachment, offsetAttachment;
    juce::AudioProcessorValueTreeState::ButtonAttachment autoCaptureAttachment, monitorAttachment;

    std::vector<std::string> addresses;
    int addressIndex = 0;
    juce::String url;
    std::vector<std::vector<bool>> qr;
    juce::Rectangle<int> qrArea, meterArea;
    float meterLevel = 0.0f;
    int ticks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MobiMicEditor)
};
