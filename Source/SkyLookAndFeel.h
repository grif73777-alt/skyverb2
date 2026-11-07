#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace sky::ui
{
// Everything the interface paints with. Light and dark are exact inversions of each other.
struct Theme
{
    juce::Colour paper; // background
    juce::Colour ink;   // text
    juce::Colour bar;   // slider bars, borders, filled boxes
    juce::Colour grey;  // secondary text
    juce::Colour hair;  // hairlines
};

inline Theme lightTheme()
{
    return { juce::Colour (0xffffffff), juce::Colour (0xff111111), juce::Colour (0xff161616),
             juce::Colour (0xff8f8f8f), juce::Colour (0xffdedede) };
}

inline Theme darkTheme()
{
    return { juce::Colour (0xff080808), juce::Colour (0xfff2f2f2), juce::Colour (0xfff2f2f2),
             juce::Colour (0xff7d7d7d), juce::Colour (0xff2a2a2a) };
}

inline Theme mixThemes (const Theme& a, const Theme& b, float t)
{
    return { a.paper.interpolatedWith (b.paper, t), a.ink.interpolatedWith (b.ink, t), a.bar.interpolatedWith (b.bar, t),
             a.grey.interpolatedWith (b.grey, t),   a.hair.interpolatedWith (b.hair, t) };
}

inline juce::Font makeFont (float height, bool bold = false, float kerning = 0.0f)
{
    juce::FontOptions opts (height);
    if (bold)
        opts = opts.withStyle ("Bold");
    return juce::Font (opts).withExtraKerningFactor (kerning);
}
} // namespace sky::ui

// Flat, strict. Sliders are bars with a thin indicator line and a square handle.
// The animated state (displayed position and hover amount) comes from the slider's
// property set: "disp" (0..1) and "hover" (0..1), written by the editor's timer.
class SkyLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SkyLookAndFeel() { setTheme (sky::ui::lightTheme()); }

    sky::ui::Theme theme;

    void setTheme (const sky::ui::Theme& t)
    {
        theme = t;
        setColour (juce::ResizableWindow::backgroundColourId, t.paper);

        setColour (juce::PopupMenu::backgroundColourId, t.paper);
        setColour (juce::PopupMenu::textColourId, t.ink);
        setColour (juce::PopupMenu::headerTextColourId, t.grey);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, t.bar);
        setColour (juce::PopupMenu::highlightedTextColourId, t.paper);
    }

    int getSliderThumbRadius (juce::Slider&) override { return 5; }

    // The slider component spans the whole bar; the mouse range is inset by half a handle
    // so the square handle never sticks out at either end.
    juce::Slider::SliderLayout getSliderLayout (juce::Slider& slider) override
    {
        juce::Slider::SliderLayout layout;
        layout.sliderBounds = slider.getLocalBounds().reduced (5, 0);
        return layout;
    }

    juce::Font getPopupMenuFont() override { return sky::ui::makeFont (12.5f); }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                           float, float, float, juce::Slider::SliderStyle,
                           juce::Slider& slider) override
    {
        const float disp  = juce::jlimit (0.0f, 1.0f, (float) (double) slider.getProperties().getWithDefault ("disp", 0.0));
        const float hover = juce::jlimit (0.0f, 1.0f, (float) (double) slider.getProperties().getWithDefault ("hover", 0.0));

        const float cy = (float) y + (float) height * 0.5f;
        const float barH = 8.0f + 2.0f * hover;
        const juce::Rectangle<float> bar ((float) x - 5.0f, cy - barH * 0.5f, (float) width + 10.0f, barH);
        const float hx = (float) x + disp * (float) width;

        // bar
        g.setColour (theme.bar);
        g.fillRect (bar);

        // value line inside the bar, up to the handle
        g.setColour (theme.paper.withAlpha (0.9f));
        g.fillRect (juce::Rectangle<float> (bar.getX() + 2.0f, cy - 0.75f, juce::jmax (0.0f, hx - bar.getX() - 2.0f), 1.5f));

        // square handle, fills in on hover
        const juce::Rectangle<float> handle (hx - 5.0f, cy - 11.0f - 2.0f * hover, 10.0f, 22.0f + 4.0f * hover);
        g.setColour (theme.paper.interpolatedWith (theme.ink, hover));
        g.fillRect (handle);
        g.setColour (theme.bar);
        g.drawRect (handle, 2.0f);

        if (hover > 0.02f)
        {
            g.setColour (theme.paper.withAlpha (hover));
            g.fillRect (juce::Rectangle<float> (handle.getCentreX() - 0.75f, handle.getY() + 5.0f, 1.5f, handle.getHeight() - 10.0f));
        }
    }
};
