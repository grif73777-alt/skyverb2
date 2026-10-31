#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace sky::ui
{
inline const juce::Colour paper { 0xffffffff };
inline const juce::Colour ink   { 0xff111111 };
inline const juce::Colour track { 0xff161616 };
inline const juce::Colour grey  { 0xff8f8f8f };
inline const juce::Colour hair  { 0xffdedede };

inline juce::Font makeFont (float height, bool bold = false, float kerning = 0.0f)
{
    juce::FontOptions opts (height);
    if (bold)
        opts = opts.withStyle ("Bold");
    return juce::Font (opts).withExtraKerningFactor (kerning);
}
} // namespace sky::ui

// White, strict, flat. Sliders are dark bars with a white indicator line and a
// square white handle.
class SkyLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SkyLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, sky::ui::paper);
    }

    int getSliderThumbRadius (juce::Slider&) override { return 6; }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float, float, juce::Slider::SliderStyle,
                           juce::Slider& slider) override
    {
        const float cy = (float) y + (float) height * 0.5f;
        const float barH = 8.0f;
        const juce::Rectangle<float> bar ((float) x, cy - barH * 0.5f, (float) width, barH);

        // dark bar
        g.setColour (sky::ui::track);
        g.fillRect (bar);

        // thin white line inside the bar up to the handle = value
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.fillRect (juce::Rectangle<float> (bar.getX() + 2.0f, cy - 0.75f, juce::jmax (0.0f, sliderPos - bar.getX() - 2.0f), 1.5f));

        // square handle
        const juce::Rectangle<float> handle (sliderPos - 5.0f, cy - 11.0f, 10.0f, 22.0f);
        const bool active = slider.isMouseOverOrDragging();
        g.setColour (active ? sky::ui::ink : sky::ui::paper);
        g.fillRect (handle);
        g.setColour (sky::ui::track);
        g.drawRect (handle, 2.0f);

        if (active)
        {
            g.setColour (sky::ui::paper);
            g.fillRect (juce::Rectangle<float> (handle.getCentreX() - 0.75f, handle.getY() + 5.0f, 1.5f, handle.getHeight() - 10.0f));
        }
    }
};
