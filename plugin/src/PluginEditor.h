#pragma once

#include "PluginProcessor.h"
#include "Theme.h"

/** The latest take. Drag it out of the window onto a track (or into any other app). */
class TakeChip : public juce::Component
{
public:
    void setContent (const juce::File& takeFile, const juce::String& newTitle, const juce::String& newHint, bool isRecording);
    void setPulse (float newPulse);

    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override   { dragging = false; }

private:
    juce::File file;
    juce::String title, hint;
    bool recording = false, dragging = false;
    float pulse = 1.0f;
};

/** The round button in the header: opens settings, or closes them. */
class HeaderButton : public juce::Button
{
public:
    HeaderButton() : Button ({}) {}

    bool showClose = false;
    void paintButton (juce::Graphics&, bool over, bool down) override;
};

/** A quiet pink text link. */
class LinkButton : public juce::Button
{
public:
    explicit LinkButton (const juce::String& text) : Button (text) {}
    void paintButton (juce::Graphics&, bool over, bool down) override;
};

//==============================================================================
class MobiMicEditor : public juce::AudioProcessorEditor,
                      private juce::Timer
{
public:
    explicit MobiMicEditor (MobiMicProcessor&);
    ~MobiMicEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override;

    /** For the screenshot tool. */
    void setSettingsVisible (bool);

private:
    /** A setting with a name, its current value, and a slider underneath. */
    struct SliderRow
    {
        juce::Label name, value, caption;
        juce::Slider slider;
    };

    void timerCallback() override;
    void refreshAddress();
    void refreshTexts();
    void updateVisibility();
    void rebuildBackdrop();
    void setUpRow (SliderRow&, const juce::String& name, const juce::String& caption);
    void layOutRow (SliderRow&, juce::Rectangle<int>);
    bool showingQr() const;

    void drawStatusPill (juce::Graphics&);
    void drawQr (juce::Graphics&);
    void drawWaveform (juce::Graphics&);

    MobiMicProcessor& processor;
    Theme look;
    const bool standalone;
    const bool inAbleton;
    juce::Image icon, backdrop;

    HeaderButton settingsButton;
    juce::Label urlLabel;
    LinkButton otherAddress { "Use another address" }, toggleQr { "Show QR code" };
    TakeChip takeChip;
    juce::TextButton captureButton, folderButton { "Takes folder" }, doneButton { "Done" };

    SliderRow gainRow, bufferRow, offsetRow;
    juce::ToggleButton monitorToggle { "Hear the phone live" }, autoCaptureToggle { "Capture while the DAW records" };

    juce::AudioProcessorValueTreeState::SliderAttachment gainAttachment, bufferAttachment, offsetAttachment;
    juce::AudioProcessorValueTreeState::ButtonAttachment monitorAttachment, autoCaptureAttachment;

    // What is on screen right now.
    bool showSettings = false, showQr = false, connected = false, statusIsError = false;
    juce::String statusText, statsText, footnote;
    bool footnoteOn = false, backdropShowsStop = false;
    float pulse = 0.0f;
    int ticks = 0;

    std::vector<std::string> addresses;
    int addressIndex = 0;
    juce::String url;
    std::vector<std::vector<bool>> qr;

    juce::Rectangle<int> pillArea, stageArea, takeArea, footnoteArea, settingsArea, qrArea, waveArea, statsArea, captionArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MobiMicEditor)
};
