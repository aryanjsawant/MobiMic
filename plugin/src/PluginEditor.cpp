#include "PluginEditor.h"
#include "NetInfo.h"
#include "BinaryData.h"

#include <qrcodegen.hpp>

namespace
{
    constexpr int windowWidth = 380, windowHeight = 600, margin = 24;

    const juce::String dot = juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  "));
}

//==============================================================================
void TakeChip::setContent (const juce::File& takeFile, const juce::String& newTitle, const juce::String& newHint, bool isRecording)
{
    if (file == takeFile && title == newTitle && hint == newHint && recording == isRecording)
        return;

    file = takeFile;
    title = newTitle;
    hint = newHint;
    recording = isRecording;
    setMouseCursor (file.existsAsFile() ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

void TakeChip::setPulse (float newPulse)
{
    if (recording && std::abs (newPulse - pulse) > 0.02f)
    {
        pulse = newPulse;
        repaint();
    }
}

void TakeChip::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat().reduced (14.0f, 0.0f);
    const auto badge = area.removeFromLeft (36.0f).withSizeKeepingCentre (36.0f, 36.0f);
    const bool hasTake = file.existsAsFile();

    g.setColour (recording || hasTake ? theme::pinkPale : theme::track.withAlpha (0.6f));
    g.fillEllipse (badge);

    if (recording)
    {
        g.setColour (theme::pink.withAlpha (0.45f + 0.55f * pulse));
        g.fillEllipse (badge.withSizeKeepingCentre (12.0f, 12.0f));
    }
    else
    {
        // A tiny waveform.
        g.setColour (hasTake ? theme::pink : theme::textDim.withAlpha (0.6f));
        const float heights[] = { 7.0f, 14.0f, 10.0f, 16.0f, 8.0f };

        for (int i = 0; i < 5; ++i)
            g.fillRoundedRectangle (juce::Rectangle<float> (2.0f, heights[i])
                                        .withCentre ({ badge.getCentreX() + (float) (i - 2) * 4.0f, badge.getCentreY() }), 1.0f);
    }

    area.removeFromLeft (12.0f);
    const auto text = area.withSizeKeepingCentre (area.getWidth(), 38.0f);

    g.setColour (theme::text);
    g.setFont (theme::font (15.0f, true));
    g.drawText (title, text.withHeight (20.0f), juce::Justification::centredLeft);

    g.setColour (theme::textDim);
    g.setFont (theme::font (13.0f));
    g.drawText (hint, text.withTrimmedTop (20.0f), juce::Justification::centredLeft);
}

void TakeChip::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || ! file.existsAsFile() || e.getDistanceFromDragStart() < 5)
        return;

    dragging = true;
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
}

//==============================================================================
void HeaderButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    const auto area = getLocalBounds().toFloat().reduced (1.0f);
    const auto centre = area.getCentre();

    g.setColour (theme::card.withAlpha (over ? 1.0f : 0.7f));
    g.fillEllipse (area);
    g.setColour (theme::hairline);
    g.drawEllipse (area, 1.0f);
    g.setColour (down ? theme::pink : theme::text);

    if (showClose)
    {
        g.drawLine (centre.x - 5.0f, centre.y - 5.0f, centre.x + 5.0f, centre.y + 5.0f, 1.8f);
        g.drawLine (centre.x - 5.0f, centre.y + 5.0f, centre.x + 5.0f, centre.y - 5.0f, 1.8f);
        return;
    }

    // Three little sliders.
    for (int i = -1; i <= 1; ++i)
    {
        const float y = centre.y + (float) i * 5.0f;
        g.drawLine (centre.x - 7.0f, y, centre.x + 7.0f, y, 1.5f);
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ centre.x + (i == 0 ? -3.0f : 3.0f), y }));
    }
}

void LinkButton::paintButton (juce::Graphics& g, bool over, bool)
{
    g.setColour (over ? theme::pink.darker (0.25f) : theme::pink);
    g.setFont (theme::font (14.0f, true));
    g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred);
}

//==============================================================================
MobiMicEditor::MobiMicEditor (MobiMicProcessor& p)
    : AudioProcessorEditor (p),
      processor (p),
      standalone (MobiMicProcessor::isStandalone()),
      inAbleton (juce::PluginHostType().isAbletonLive()),
      gainAttachment (p.apvts, "gain", gainRow.slider),
      bufferAttachment (p.apvts, "buffer", bufferRow.slider),
      offsetAttachment (p.apvts, "offset", offsetRow.slider),
      monitorAttachment (p.apvts, "monitor", monitorToggle),
      autoCaptureAttachment (p.apvts, "autoCapture", autoCaptureToggle)
{
    setLookAndFeel (&look);
    setOpaque (true);
    icon = juce::ImageCache::getFromMemory (BinaryData::icon_png, BinaryData::icon_pngSize);

    if (juce::JUCEApplicationBase::isStandaloneApp())
    {
        // The app's own window frame, menus and audio dialog follow the same look.
        // Deliberately never deleted: it has to outlive every window.
        static auto* appLook = new Theme();
        juce::LookAndFeel::setDefaultLookAndFeel (appLook);
    }

    settingsButton.onClick = [this] { setSettingsVisible (! showSettings); };
    addAndMakeVisible (settingsButton);

    urlLabel.setFont (theme::font (15.0f, true));
    urlLabel.setJustificationType (juce::Justification::centred);
    addChildComponent (urlLabel);

    otherAddress.onClick = [this]
    {
        ++addressIndex;
        url.clear();
        refreshAddress();
    };

    toggleQr.onClick = [this]
    {
        showQr = ! showQr;
        updateVisibility();
    };

    addChildComponent (otherAddress);
    addChildComponent (toggleQr);
    addChildComponent (takeChip);

    captureButton.getProperties().set ("primary", true);
    captureButton.setClickingTogglesState (true);
    captureButton.onClick = [this] { processor.manualCapture = captureButton.getToggleState(); };

    folderButton.onClick = []
    {
        auto folder = TakeWriter::getTakesFolder();
        folder.createDirectory();
        folder.startAsProcess();
    };

    doneButton.getProperties().set ("primary", true);
    doneButton.onClick = [this] { setSettingsVisible (false); };

    addChildComponent (captureButton);
    addChildComponent (folderButton);
    addChildComponent (doneButton);

    setUpRow (gainRow, "Gain", "Raise it if takes are quiet. Red bars mean it's too loud.");
    setUpRow (bufferRow, "Buffer", "Higher rides out Wi-Fi hiccups, with more delay when listening live.");
    setUpRow (offsetRow, "Offset", "Shifts takes earlier to cancel the phone's delay.");
    addChildComponent (monitorToggle);
    addChildComponent (autoCaptureToggle);

    setSize (windowWidth, windowHeight);
    refreshAddress();
    refreshTexts();
    updateVisibility();
    startTimerHz (30);
}

MobiMicEditor::~MobiMicEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void MobiMicEditor::setUpRow (SliderRow& row, const juce::String& name, const juce::String& caption)
{
    row.name.setText (name, juce::dontSendNotification);
    row.name.setFont (theme::font (15.0f));
    row.value.setFont (theme::font (14.0f, true));
    row.value.setJustificationType (juce::Justification::centredRight);
    row.caption.setText (caption, juce::dontSendNotification);
    row.caption.setFont (theme::font (12.0f));
    row.caption.setColour (juce::Label::textColourId, theme::textDim);
    for (auto* label : { &row.name, &row.value, &row.caption })
        label->setBorderSize ({});

    row.slider.setSliderStyle (juce::Slider::LinearHorizontal);
    row.slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);

    for (auto* c : std::initializer_list<juce::Component*> { &row.name, &row.value, &row.caption, &row.slider })
        addChildComponent (c);
}

void MobiMicEditor::layOutRow (SliderRow& row, juce::Rectangle<int> area)
{
    auto top = area.removeFromTop (22);
    row.value.setBounds (top.removeFromRight (90));
    row.name.setBounds (top);
    row.slider.setBounds (area.removeFromTop (22));
    row.caption.setBounds (area.removeFromTop (16));
}

void MobiMicEditor::setSettingsVisible (bool shouldShow)
{
    showSettings = shouldShow;
    updateVisibility();
}

bool MobiMicEditor::showingQr() const
{
    return ! connected || showQr;
}

void MobiMicEditor::parentHierarchyChanged()
{
    if (! juce::JUCEApplicationBase::isStandaloneApp())
        return;

    if (auto* window = dynamic_cast<juce::DocumentWindow*> (getTopLevelComponent()))
    {
        window->setBackgroundColour (theme::silverTop);
        window->setIcon (icon);
    }
}

//==============================================================================
void MobiMicEditor::updateVisibility()
{
    const bool main = ! showSettings;
    const bool qrMode = showingQr();

    urlLabel.setVisible (main && qrMode);
    otherAddress.setVisible (main && qrMode && ! connected && addresses.size() > 1);
    toggleQr.setVisible (main && connected);
    toggleQr.setButtonText (showQr ? "Hide QR code" : "Show QR code");
    takeChip.setVisible (main);
    captureButton.setVisible (main);
    folderButton.setVisible (main);
    doneButton.setVisible (showSettings);

    for (auto* row : { &gainRow, &bufferRow, &offsetRow })
    {
        const bool visible = showSettings && (row != &offsetRow || ! standalone);

        for (auto* c : std::initializer_list<juce::Component*> { &row->name, &row->value, &row->caption, &row->slider })
            c->setVisible (visible);
    }

    monitorToggle.setVisible (showSettings);
    autoCaptureToggle.setVisible (showSettings && ! standalone);
    settingsButton.showClose = showSettings;

    resized();
    repaint();
}

void MobiMicEditor::resized()
{
    const int contentWidth = getWidth() - 2 * margin;

    settingsButton.setBounds (getWidth() - margin - 34, 18, 34, 34);
    pillArea = { 0, 66, getWidth(), 28 };
    stageArea = { margin, 110, contentWidth, 296 };
    takeArea = { margin, stageArea.getBottom() + 14, contentWidth, 64 };
    footnoteArea = { margin, takeArea.getBottom() + 8, contentWidth, 18 };

    auto buttons = juce::Rectangle<int> (margin, getHeight() - margin - 46, contentWidth, 46);
    settingsArea = { margin, 110, contentWidth, buttons.getY() - 16 - 110 };

    // Stage: either the QR code with its address, or the live waveform.
    qrArea = juce::Rectangle<int> (190, 190).withCentre ({ stageArea.getCentreX(), stageArea.getY() + 22 + 95 });
    captionArea = { stageArea.getX(), qrArea.getBottom() + 8, stageArea.getWidth(), 18 };
    urlLabel.setBounds (stageArea.getX(), captionArea.getBottom() + 2, stageArea.getWidth(), 22);

    const auto linkSlot = juce::Rectangle<int> (180, 22).withCentre ({ stageArea.getCentreX(), stageArea.getBottom() - 22 });
    otherAddress.setBounds (linkSlot);
    toggleQr.setBounds (linkSlot);

    waveArea = { stageArea.getX() + 26, stageArea.getY() + 52, stageArea.getWidth() - 52, 140 };
    statsArea = { stageArea.getX(), waveArea.getBottom() + 16, stageArea.getWidth(), 18 };

    takeChip.setBounds (takeArea);

    doneButton.setBounds (buttons);
    captureButton.setBounds (buttons.removeFromLeft ((contentWidth - 10) * 58 / 100));
    buttons.removeFromLeft (10);
    folderButton.setBounds (buttons);

    // Settings.
    auto rows = settingsArea.reduced (20, 0).withTrimmedTop (56);
    layOutRow (gainRow, rows.removeFromTop (66));
    layOutRow (bufferRow, rows.removeFromTop (66));

    if (! standalone)
        layOutRow (offsetRow, rows.removeFromTop (66));

    rows.removeFromTop (4);
    monitorToggle.setBounds (rows.removeFromTop (42));
    autoCaptureToggle.setBounds (rows.removeFromTop (42));

    rebuildBackdrop();
}

void MobiMicEditor::rebuildBackdrop()
{
    if (getWidth() <= 0 || getHeight() <= 0)
        return;

    // The background and card shadows never move, so they are drawn once (at 2x for sharp
    // displays) and reused while the waveform animates on top.
    constexpr float scale = 2.0f;
    backdrop = juce::Image (juce::Image::RGB, (int) ((float) getWidth() * scale), (int) ((float) getHeight() * scale), false);
    juce::Graphics g (backdrop);
    g.addTransform (juce::AffineTransform::scale (scale));
    theme::drawBackground (g, getLocalBounds());

    if (showSettings)
    {
        theme::drawCard (g, settingsArea.toFloat());
    }
    else
    {
        theme::drawCard (g, stageArea.toFloat());
        theme::drawCard (g, takeArea.toFloat(), 18.0f);
    }

    const auto primary = (showSettings ? doneButton : captureButton).getBounds().toFloat().reduced (6.0f, 1.0f);
    juce::Path pill;
    pill.addRoundedRectangle (primary, primary.getHeight() * 0.5f);
    juce::DropShadow ((backdropShowsStop ? theme::text : theme::pink).withAlpha (0.26f), 16, { 0, 7 }).drawForPath (g, pill);
}

//==============================================================================
void MobiMicEditor::refreshAddress()
{
    const auto previousCount = addresses.size();
    addresses = NetInfo::getLocalAddresses();

    juce::String newUrl;
    const int port = processor.getServer().getPort();

    if (! addresses.empty() && port != 0)
    {
        addressIndex %= (int) addresses.size();
        newUrl = "https://" + juce::String (addresses[(size_t) addressIndex]) + ":" + juce::String (port);
    }

    if (newUrl != url)
    {
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

        urlLabel.setText (url.isNotEmpty() ? url.fromFirstOccurrenceOf ("https://", false, false) : "No network found",
                          juce::dontSendNotification);
        repaint();
    }

    if (previousCount != addresses.size())
        updateVisibility();
}

void MobiMicEditor::refreshTexts()
{
    auto& server = processor.getServer();
    juce::String newStatus, newFootnote;
    bool error = false, newFootnoteOn = false;

    connected = server.isRunning() && processor.ownsPhone() && server.isPhoneConnected();

    if (! server.isRunning())
    {
        const auto message = processor.getServerError();
        error = message.isNotEmpty();
        newStatus = error ? message : "Starting...";
    }
    else if (! processor.ownsPhone())
    {
        newStatus = "In use on another track";
    }
    else
    {
        newStatus = connected ? "Phone connected" : "Waiting for phone";
    }

    const auto& buffer = processor.getBuffer();
    const int glitches = buffer.getUnderruns() + buffer.getDrops();
    const auto newStats = juce::String ((int) buffer.getFillMs()) + " ms buffer" + dot
                        + juce::String (glitches) + (glitches == 1 ? " glitch" : " glitches");

    // The take.
    auto& takes = processor.getTakes();
    const bool helper = ! standalone && processor.isHelperActive();

    if (takes.isActive())
    {
        takeChip.setContent ({}, "Recording  " + juce::String (takes.getSecondsWritten(), 1) + " s",
                             "Saving exactly what the phone sends", true);
    }
    else if (const auto last = takes.getLastTake(); last.existsAsFile())
    {
        const auto result = processor.getHelperResult();
        takeChip.setContent (last, last.getFileNameWithoutExtension(),
                             standalone ? "Drag me into any app"
                                        : (helper && result.isNotEmpty() ? result : juce::String ("Drag me onto a track")),
                             false);
    }
    else
    {
        takeChip.setContent ({}, "No takes yet",
                             standalone ? "Press Record to capture one"
                                        : (helper ? "Press record in Ableton and it lands on the track"
                                                  : "Record in your DAW, then drag the take in"),
                             false);
    }

    if (standalone)
    {
        newFootnote = "Takes are saved in Documents > MobiMic > Takes";
    }
    else if (inAbleton || helper)
    {
        newFootnoteOn = helper;
        newFootnote = helper ? "Ableton helper on" : "Ableton helper off: takes need dragging in";
    }

    const bool capturing = processor.manualCapture.load();

    if (captureButton.getToggleState() != capturing)
        captureButton.setToggleState (capturing, juce::dontSendNotification);

    if (backdropShowsStop != capturing)
    {
        backdropShowsStop = capturing;
        rebuildBackdrop();
        repaint();
    }

    const juce::String captureText = capturing ? "Stop" : (standalone ? "Record" : "Capture");

    if (captureButton.getButtonText() != captureText)
        captureButton.setButtonText (captureText);

    if (newStatus != statusText || error != statusIsError || newStats != statsText
        || newFootnote != footnote || newFootnoteOn != footnoteOn)
    {
        statusText = newStatus;
        statusIsError = error;
        statsText = newStats;
        footnote = newFootnote;
        footnoteOn = newFootnoteOn;
        repaint();
    }
}

void MobiMicEditor::timerCallback()
{
    ++ticks;
    pulse = 0.5f + 0.5f * std::sin ((float) ticks * 0.13f);

    if (ticks % 60 == 0 || url.isEmpty())
        refreshAddress();

    const bool wasConnected = connected;
    refreshTexts();

    if (connected != wasConnected)
    {
        if (! connected)
            showQr = false;

        updateVisibility();
    }

    if (showSettings)
    {
        const auto gain = gainRow.slider.getValue();
        gainRow.value.setText ((gain > 0.05 ? "+" : "") + juce::String (gain, 1) + " dB", juce::dontSendNotification);
        bufferRow.value.setText (juce::String ((int) bufferRow.slider.getValue()) + " ms", juce::dontSendNotification);
        offsetRow.value.setText (juce::String ((int) offsetRow.slider.getValue()) + " ms", juce::dontSendNotification);
        return;
    }

    takeChip.setPulse (pulse);

    if (connected)
    {
        repaint (pillArea);

        if (! showingQr())
            repaint (waveArea);
    }
}

//==============================================================================
void MobiMicEditor::paint (juce::Graphics& g)
{
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (backdrop, getLocalBounds().toFloat());

    g.drawImage (icon, juce::Rectangle<float> ((float) margin, 19.0f, 32.0f, 32.0f));
    g.setColour (theme::text);
    g.setFont (theme::font (20.0f, true));
    g.drawText ("MobiMic", margin + 42, 19, 180, 32, juce::Justification::centredLeft);

    drawStatusPill (g);

    if (showSettings)
    {
        g.setColour (theme::text);
        g.setFont (theme::font (18.0f, true));
        g.drawText ("Settings", settingsArea.reduced (20, 0).withHeight (56), juce::Justification::centredLeft);

        g.setColour (theme::textDim);
        g.setFont (theme::font (12.0f));
        g.drawFittedText (juce::String (standalone ? "To choose speakers or headphones, use Options in the title bar.\n" : "")
                              + "MobiMic " MOBIMIC_VERSION,
                          settingsArea.reduced (20, 14).removeFromBottom (34), juce::Justification::centredBottom, 2);
        return;
    }

    if (showingQr())
    {
        drawQr (g);
        g.setColour (theme::textDim);
        g.setFont (theme::font (13.5f));
        g.drawText (qr.empty() ? "Connect this computer to a network first" : "Scan with your phone's camera",
                    captionArea, juce::Justification::centred);
    }
    else
    {
        drawWaveform (g);
        g.setColour (theme::textDim);
        g.setFont (theme::font (13.5f));
        g.drawText (statsText, statsArea, juce::Justification::centred);
    }

    if (footnote.isNotEmpty())
    {
        g.setColour (footnoteOn ? theme::pink : theme::textDim);
        g.setFont (theme::font (12.5f, footnoteOn));
        g.drawText (footnote, footnoteArea, juce::Justification::centred);
    }
}

void MobiMicEditor::drawStatusPill (juce::Graphics& g)
{
    const auto font = theme::font (14.0f, true);
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (font, statusText, 0.0f, 0.0f);
    const float textWidth = glyphs.getBoundingBox (0, -1, true).getWidth();

    const auto pill = juce::Rectangle<float> (textWidth + 46.0f, (float) pillArea.getHeight())
                          .withCentre (pillArea.toFloat().getCentre());

    g.setColour (theme::card.withAlpha (0.85f));
    g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
    g.setColour (theme::hairline);
    g.drawRoundedRectangle (pill, pill.getHeight() * 0.5f, 1.0f);

    const juce::Point<float> centre (pill.getX() + 17.0f, pill.getCentreY());

    if (connected)
    {
        g.setColour (theme::pink.withAlpha (0.22f * pulse));
        g.fillEllipse (juce::Rectangle<float> (16.0f, 16.0f).withCentre (centre));
    }

    g.setColour (statusIsError ? theme::red : (connected ? theme::pink : juce::Colour (0xffb9bac4)));
    g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (centre));

    g.setColour (theme::text);
    g.setFont (font);
    g.drawText (statusText, pill.withTrimmedLeft (30.0f).withTrimmedRight (12.0f), juce::Justification::centredLeft);
}

void MobiMicEditor::drawQr (juce::Graphics& g)
{
    if (qr.empty())
        return;

    // Dark squares straight on the white card, which doubles as the quiet zone scanners need.
    const int modules = (int) qr.size();
    const int cell = qrArea.getWidth() / modules;
    const int originX = qrArea.getCentreX() - cell * modules / 2;
    const int originY = qrArea.getCentreY() - cell * modules / 2;
    g.setColour (theme::text);

    for (int y = 0; y < modules; ++y)
        for (int x = 0; x < modules; ++x)
            if (qr[(size_t) y][(size_t) x])
                g.fillRect (originX + x * cell, originY + y * cell, cell, cell);
}

void MobiMicEditor::drawWaveform (juce::Graphics& g)
{
    constexpr float barWidth = 3.0f, step = 6.0f;
    float bars[128];
    const int numBars = juce::jmin (128, (int) ((float) waveArea.getWidth() / step) + 2);
    const float filled = processor.getLevelBars (bars, numBars, 4);

    const juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (waveArea);

    const auto area = waveArea.toFloat();

    for (int i = 0; i < numBars; ++i)
    {
        // The newest bar enters at the right edge and everything slides left as it fills.
        const float x = area.getRight() - barWidth - (float) (numBars - 1 - i) * step - filled * step;
        if (x < area.getX())
            continue;

        const float age = (float) i / (float) (numBars - 1);
        const float level = bars[i];
        const float barHeight = juce::jmax (3.0f, std::pow (level, 0.6f) * area.getHeight());

        g.setColour ((level >= 0.99f ? theme::red : theme::pinkLight.interpolatedWith (theme::pink, age))
                         .withAlpha (0.12f + 0.88f * age));
        g.fillRoundedRectangle (x, area.getCentreY() - barHeight * 0.5f, barWidth, barHeight, 1.5f);
    }
}
