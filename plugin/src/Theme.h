#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/** MobiMic's look: soft silver surfaces, white cards, one rose-pink accent.

    The phone page (web/index.html) and the website (docs/) use the same values,
    so change them in all three places together.
*/
namespace theme
{
    const juce::Colour silverTop    { 0xfffbfbfd };
    const juce::Colour silverBottom { 0xffe6e7ec };
    const juce::Colour card         { 0xffffffff };
    const juce::Colour hairline     { 0xffdcdde4 };
    const juce::Colour track        { 0xffe4e5eb };
    const juce::Colour text         { 0xff1b1b1f };
    const juce::Colour textDim      { 0xff6e6e78 };
    const juce::Colour pink         { 0xffec3882 };
    const juce::Colour pinkLight    { 0xffff96be };
    const juce::Colour pinkPale     { 0xffffe6f0 };
    const juce::Colour red          { 0xffe5484d };

    inline juce::Font font (float height, bool semibold = false)
    {
        return juce::Font (juce::FontOptions ("Segoe UI", height, juce::Font::plain)
                               .withStyle (semibold ? "Semibold" : "Regular"));
    }

    inline juce::ColourGradient pinkGradient (juce::Rectangle<float> area)
    {
        return { pink, area.getX(), area.getY(), pinkLight, area.getRight(), area.getBottom(), false };
    }

    /** Silver, with a faint pink glow in the top-right corner. */
    inline void drawBackground (juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        const auto area = bounds.toFloat();
        g.setGradientFill ({ silverTop, 0.0f, area.getY(), silverBottom, 0.0f, area.getBottom(), false });
        g.fillRect (area);

        juce::ColourGradient glow (pinkLight.withAlpha (0.30f), area.getRight() - 20.0f, area.getY() - 30.0f,
                                   pinkLight.withAlpha (0.0f), area.getRight() - 20.0f, area.getY() + 290.0f, true);
        g.setGradientFill (glow);
        g.fillRect (area);
    }

    inline void drawCard (juce::Graphics& g, juce::Rectangle<float> area, float radius = 20.0f)
    {
        juce::Path shape;
        shape.addRoundedRectangle (area, radius);
        juce::DropShadow (juce::Colour (0x16202030), 22, { 0, 8 }).drawForPath (g, shape);
        g.setColour (card);
        g.fillPath (shape);
        g.setColour (hairline.withAlpha (0.7f));
        g.strokePath (shape, juce::PathStrokeType (1.0f));
    }
}

//==============================================================================
/** Sliders, switches and buttons drawn in the MobiMic style.

    A TextButton with the property "primary" set is the filled pink pill; any other
    TextButton is a quiet white one.
*/
class Theme : public juce::LookAndFeel_V4
{
public:
    // The colour scheme covers what MobiMic doesn't draw itself: the app's title bar,
    // its Options menu and the audio settings dialog.
    Theme()
        : LookAndFeel_V4 ({ theme::silverTop, juce::Colour (0xfff3f3f6), theme::card, theme::hairline, theme::text,
                            theme::pink, juce::Colours::white, theme::pink, theme::text })
    {
        setColour (juce::Label::textColourId, theme::text);
        setColour (juce::ToggleButton::textColourId, theme::text);
        setColour (juce::ResizableWindow::backgroundColourId, theme::silverTop);
    }

    int getSliderThumbRadius (juce::Slider&) override   { return 9; }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                           float, float, juce::Slider::SliderStyle, juce::Slider& slider) override
    {
        const auto centreY = (float) y + (float) height * 0.5f;
        const juce::Rectangle<float> rail ((float) x, centreY - 2.0f, (float) width, 4.0f);

        g.setColour (theme::track);
        g.fillRoundedRectangle (rail, 2.0f);

        const auto filled = rail.withRight (sliderPos);
        g.setGradientFill (theme::pinkGradient (rail));
        g.fillRoundedRectangle (filled, 2.0f);

        const auto thumb = juce::Rectangle<float> (18.0f, 18.0f).withCentre ({ sliderPos, centreY });
        juce::Path circle;
        circle.addEllipse (thumb);
        juce::DropShadow (juce::Colour (0x30202030), 6, { 0, 2 }).drawForPath (g, circle);
        g.setColour (juce::Colours::white);
        g.fillPath (circle);
        g.setColour (slider.isMouseOverOrDragging() ? theme::pink : theme::pinkLight);
        g.drawEllipse (thumb.reduced (0.75f), 1.5f);
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool, bool) override
    {
        const auto bounds = button.getLocalBounds().toFloat();
        const auto pill = juce::Rectangle<float> (40.0f, 24.0f).withCentre ({ bounds.getRight() - 20.0f, bounds.getCentreY() });
        const bool on = button.getToggleState();

        if (on)
            g.setGradientFill (theme::pinkGradient (pill));
        else
            g.setColour (theme::track);

        g.fillRoundedRectangle (pill, 12.0f);

        const auto knob = juce::Rectangle<float> (18.0f, 18.0f)
                              .withCentre ({ on ? pill.getRight() - 12.0f : pill.getX() + 12.0f, pill.getCentreY() });
        juce::Path circle;
        circle.addEllipse (knob);
        juce::DropShadow (juce::Colour (0x30202030), 4, { 0, 1 }).drawForPath (g, circle);
        g.setColour (juce::Colours::white);
        g.fillPath (circle);

        g.setColour (theme::text);
        g.setFont (theme::font (15.0f));
        g.drawText (button.getButtonText(), bounds.withTrimmedRight (52.0f), juce::Justification::centredLeft);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&, bool over, bool down) override
    {
        const auto area = button.getLocalBounds().toFloat().reduced (1.0f);
        const auto radius = area.getHeight() * 0.5f;
        const bool primary = (bool) button.getProperties()["primary"];

        if (primary && button.getToggleState())
        {
            g.setColour (theme::text);              // "Stop": a calm dark pill while recording
            g.fillRoundedRectangle (area, radius);
        }
        else if (primary)
        {
            g.setGradientFill (theme::pinkGradient (area));
            g.fillRoundedRectangle (area, radius);
        }
        else
        {
            g.setColour (theme::card.withAlpha (over ? 1.0f : 0.75f));
            g.fillRoundedRectangle (area, radius);
            g.setColour (theme::hairline);
            g.drawRoundedRectangle (area, radius, 1.0f);
        }

        if (down || (over && primary))
        {
            g.setColour (juce::Colours::white.withAlpha (down ? 0.18f : 0.10f));
            g.fillRoundedRectangle (area, radius);
        }
    }

    juce::Font getTextButtonFont (juce::TextButton&, int) override   { return theme::font (15.0f, true); }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool) override
    {
        const bool primary = (bool) button.getProperties()["primary"];
        g.setFont (getTextButtonFont (button, button.getHeight()));
        g.setColour ((primary ? juce::Colours::white : theme::text).withAlpha (button.isEnabled() ? 1.0f : 0.4f));
        g.drawText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred);
    }

    juce::Font getLabelFont (juce::Label& label) override   { return label.getFont(); }
};
