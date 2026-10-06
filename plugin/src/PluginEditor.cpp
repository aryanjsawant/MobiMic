#include "PluginEditor.h"
#include "NetInfo.h"

#include <qrcodegen.hpp>

namespace
{
    const juce::Colour background { 0xff0f1115 };
    const juce::Colour panel { 0xff23262d };
    const juce::Colour textColour { 0xffe8eaed };
    const juce::Colour dimText { 0xff9aa0a6 };
    const juce::Colour green { 0xff34c759 };
    const juce::Colour red { 0xffd93025 };
}

//==============================================================================
void TakeChip::setTake (const juce::File& newFile, const juce::String& newText)
{
    if (file != newFile || text != newText)
    {
        file = newFile;
        text = newText;
        setMouseCursor (file.existsAsFile() ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void TakeChip::paint (juce::Graphics& g)
{
    g.setColour (panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    g.setColour (file.existsAsFile() ? textColour : dimText);
    g.setFont (13.0f);
    g.drawFittedText (text, getLocalBounds().reduced (10, 2), juce::Justification::centredLeft, 2);
}

void TakeChip::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || ! file.existsAsFile() || e.getDistanceFromDragStart() < 5)
        return;

    dragging = true;
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
}

//==============================================================================
MobiMicEditor::MobiMicEditor (MobiMicProcessor& p)
    : AudioProcessorEditor (p),
      processor (p),
      bufferAttachment (p.apvts, "buffer", bufferSlider),
      gainAttachment (p.apvts, "gain", gainSlider),
      autoCaptureAttachment (p.apvts, "autoCapture", autoCapture)
{
    for (auto* label : { &status, &urlLabel, &stats })
    {
        label->setJustificationType (juce::Justification::centred);
        addAndMakeVisible (label);
    }

    status.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    urlLabel.setFont (juce::FontOptions (14.0f));
    urlLabel.setColour (juce::Label::textColourId, textColour);
    stats.setFont (juce::FontOptions (12.0f));
    stats.setColour (juce::Label::textColourId, dimText);

    for (auto* slider : { &bufferSlider, &gainSlider })
    {
        slider->setSliderStyle (juce::Slider::LinearHorizontal);
        slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
        slider->setColour (juce::Slider::thumbColourId, green);
        slider->setColour (juce::Slider::trackColourId, green.darker (0.4f));
        slider->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (slider);
    }

    bufferSlider.setTextValueSuffix (" ms");
    gainSlider.setTextValueSuffix (" dB");
    bufferSlider.setTooltip ("Bigger = more delay but fewer dropouts on shaky Wi-Fi.");

    for (auto* label : { &bufferLabel, &gainLabel })
    {
        label->setColour (juce::Label::textColourId, dimText);
        addAndMakeVisible (label);
    }

    nextAddress.onClick = [this]
    {
        ++addressIndex;
        url.clear();
        refreshAddress();
    };

    captureButton.setClickingTogglesState (true);
    captureButton.setColour (juce::TextButton::buttonOnColourId, red);
    captureButton.setToggleState (processor.manualCapture.load(), juce::dontSendNotification);
    captureButton.onClick = [this] { processor.manualCapture = captureButton.getToggleState(); };

    openFolder.onClick = []
    {
        auto folder = TakeWriter::getTakesFolder();
        folder.createDirectory();
        folder.startAsProcess();
    };

    autoCapture.setColour (juce::ToggleButton::textColourId, textColour);

    addChildComponent (nextAddress);
    addAndMakeVisible (captureButton);
    addAndMakeVisible (openFolder);
    addAndMakeVisible (autoCapture);
    addAndMakeVisible (takeChip);

    setSize (360, 610);
    refreshAddress();
    timerCallback();
    startTimerHz (15);
}

MobiMicEditor::~MobiMicEditor()
{
    stopTimer();
}

void MobiMicEditor::refreshAddress()
{
    addresses = NetInfo::getLocalAddresses();
    nextAddress.setVisible (addresses.size() > 1);

    juce::String newUrl;
    const int port = processor.getServer().getPort();

    if (! addresses.empty() && port != 0)
    {
        addressIndex %= (int) addresses.size();
        newUrl = "https://" + juce::String (addresses[(size_t) addressIndex]) + ":" + juce::String (port);
    }

    if (newUrl == url)
        return;

    url = newUrl;
    qr.clear();

    if (url.isNotEmpty())
    {
        const auto code = qrcodegen::QrCode::encodeText (url.toRawUTF8(), qrcodegen::QrCode::Ecc::MEDIUM);
        const int size = code.getSize();
        qr.assign ((size_t) size, std::vector<bool> ((size_t) size));

        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
                qr[(size_t) y][(size_t) x] = code.getModule (x, y);
    }

    urlLabel.setText (url.isNotEmpty() ? url : "No network found", juce::dontSendNotification);
    repaint();
}

void MobiMicEditor::timerCallback()
{
    if (++ticks % 30 == 0 || url.isEmpty())
        refreshAddress();

    auto& server = processor.getServer();
    const bool connected = server.isPhoneConnected();

    if (! processor.ownsPhone() && server.isRunning())
    {
        status.setText ("MobiMic is already on another track", juce::dontSendNotification);
        status.setColour (juce::Label::textColourId, dimText);
    }
    else if (! server.isRunning())
    {
        const auto error = processor.getServerError();
        status.setText (error.isNotEmpty() ? error : "Starting...", juce::dontSendNotification);
        status.setColour (juce::Label::textColourId, error.isNotEmpty() ? red : dimText);
    }
    else
    {
        status.setText (connected ? "Phone connected" : "Scan with your phone's camera", juce::dontSendNotification);
        status.setColour (juce::Label::textColourId, connected ? green : textColour);
    }

    const auto& buffer = processor.getBuffer();
    stats.setText (connected ? "buffer " + juce::String ((int) buffer.getFillMs()) + " ms   glitches "
                                   + juce::String (buffer.getUnderruns() + buffer.getDrops())
                             : "Phone and computer must be on the same Wi-Fi, hotspot or USB tether",
                   juce::dontSendNotification);

    meterLevel = juce::jmax (processor.readInputPeak(), meterLevel * 0.8f);
    repaint (meterArea);

    auto& takes = processor.getTakes();

    if (takes.isActive())
    {
        takeChip.setTake ({}, "Capturing take...  " + juce::String (takes.getSecondsWritten(), 1) + " s");
    }
    else
    {
        const auto last = takes.getLastTake();
        takeChip.setTake (last, last.existsAsFile() ? last.getFileName() + "\nDrag me onto a track"
                                                    : "No take yet. Captured takes are gap-free even if the live sound glitched.");
    }

    if (captureButton.getToggleState() != processor.manualCapture.load())
        captureButton.setToggleState (processor.manualCapture.load(), juce::dontSendNotification);

    captureButton.setButtonText (captureButton.getToggleState() ? "Stop capture" : "Capture");
}

void MobiMicEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);

    g.setColour (dimText);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText ("MobiMic", getLocalBounds().removeFromTop (40), juce::Justification::centred);

    // QR code: black on white with a quiet zone, as scanners expect.
    g.setColour (juce::Colours::white);
    g.fillRoundedRectangle (qrArea.toFloat(), 8.0f);

    if (! qr.empty())
    {
        const int modules = (int) qr.size();
        const int cell = (qrArea.getWidth() - 20) / modules;
        const int origin = (qrArea.getWidth() - cell * modules) / 2;
        g.setColour (juce::Colours::black);

        for (int y = 0; y < modules; ++y)
            for (int x = 0; x < modules; ++x)
                if (qr[(size_t) y][(size_t) x])
                    g.fillRect (qrArea.getX() + origin + x * cell, qrArea.getY() + origin + y * cell, cell, cell);
    }

    g.setColour (panel);
    g.fillRoundedRectangle (meterArea.toFloat(), 4.0f);
    g.setColour (green);
    g.fillRoundedRectangle (meterArea.toFloat().withWidth ((float) meterArea.getWidth() * juce::jlimit (0.0f, 1.0f, std::sqrt (meterLevel))), 4.0f);
}

void MobiMicEditor::resized()
{
    auto area = getLocalBounds().reduced (16, 0);
    area.removeFromTop (40);

    status.setBounds (area.removeFromTop (26));
    area.removeFromTop (8);

    qrArea = area.removeFromTop (220).withSizeKeepingCentre (220, 220);
    area.removeFromTop (6);
    urlLabel.setBounds (area.removeFromTop (22));
    nextAddress.setBounds (area.removeFromTop (22).withSizeKeepingCentre (120, 20));
    area.removeFromTop (8);

    meterArea = area.removeFromTop (8);
    area.removeFromTop (4);
    stats.setBounds (area.removeFromTop (18));
    area.removeFromTop (8);

    auto row = area.removeFromTop (26);
    bufferLabel.setBounds (row.removeFromLeft (52));
    bufferSlider.setBounds (row);
    row = area.removeFromTop (26);
    gainLabel.setBounds (row.removeFromLeft (52));
    gainSlider.setBounds (row);
    area.removeFromTop (10);

    autoCapture.setBounds (area.removeFromTop (24));
    area.removeFromTop (6);
    takeChip.setBounds (area.removeFromTop (44));
    area.removeFromTop (8);

    row = area.removeFromTop (28);
    captureButton.setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (0, 0).withTrimmedRight (4));
    openFolder.setBounds (row.withTrimmedLeft (4));
}
