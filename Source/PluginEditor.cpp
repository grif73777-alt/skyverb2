#include "PluginEditor.h"

namespace
{
constexpr int kWidth = 780;
constexpr int kHeight = 440;
constexpr int kMargin = 36;
constexpr int kRowH = 32;
constexpr int kLabelW = 96;
constexpr int kValueW = 78;

constexpr int kLeftX = kMargin;
constexpr int kLeftW = 404;
constexpr int kRightX = 496;
constexpr int kRightW = kWidth - kMargin - kRightX;

constexpr int kSectionY = 90;
constexpr int kRowsTop = 116;
constexpr int kMasterY = kRowsTop + 3 * kRowH + 26;
constexpr int kMasterRowsTop = kMasterY + 26;
} // namespace

//==============================================================================
// One line: LABEL  [dark slider]  value
class SkyverbAudioProcessorEditor::SliderRow : public juce::Component
{
public:
    SliderRow (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID, const juce::String& labelText)
        : label (labelText.toUpperCase())
    {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        slider.setScrollWheelEnabled (true);
        addAndMakeVisible (slider);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramID, slider);
        slider.onValueChange = [this] { repaint(); };
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromLeft (kLabelW);
        r.removeFromRight (kValueW);
        slider.setBounds (r);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        g.setColour (sky::ui::grey);
        g.setFont (sky::ui::makeFont (10.5f, false, 0.16f));
        g.drawText (label, r.removeFromLeft (kLabelW), juce::Justification::centredLeft, false);

        g.setColour (sky::ui::ink);
        g.setFont (sky::ui::makeFont (12.0f));
        g.drawText (slider.getTextFromValue (slider.getValue()), getLocalBounds().removeFromRight (kValueW),
                    juce::Justification::centredRight, false);
    }

private:
    juce::String label;
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

//==============================================================================
// Two-state selector, drawn as flat boxes.
class SkyverbAudioProcessorEditor::Segmented : public juce::Component
{
public:
    Segmented (juce::RangedAudioParameter& p, juce::StringArray opts)
        : options (std::move (opts)),
          attachment (p, [this] (float v) { index = juce::roundToInt (v); repaint(); }, nullptr)
    {
        attachment.sendInitialUpdate();
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paint (juce::Graphics& g) override
    {
        const int n = options.size();
        const float w = (float) getWidth() / (float) n;
        for (int i = 0; i < n; ++i)
        {
            const juce::Rectangle<float> cell ((float) i * w, 0.0f, w, (float) getHeight());
            const bool on = i == index;
            g.setColour (on ? sky::ui::track : sky::ui::paper);
            g.fillRect (cell);
            g.setColour (sky::ui::track);
            g.drawRect (cell, 1.5f);
            g.setColour (on ? sky::ui::paper : sky::ui::ink);
            g.setFont (sky::ui::makeFont (10.5f, on, 0.2f));
            g.drawText (options[i], cell.toNearestInt(), juce::Justification::centred, false);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = juce::jlimit (0, options.size() - 1, (int) ((float) e.x / (float) getWidth() * (float) options.size()));
        attachment.setValueAsCompleteGesture ((float) i);
    }

private:
    juce::StringArray options;
    int index = 0;
    juce::ParameterAttachment attachment;
};

//==============================================================================
SkyverbAudioProcessorEditor::SkyverbAudioProcessorEditor (SkyverbAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&laf);

    auto& s = processor.apvts;
    auto add = [&] (const char* id, const char* label)
    {
        rows.push_back (std::make_unique<SliderRow> (s, id, label));
        addAndMakeVisible (*rows.back());
    };

    // reverb (0-8)
    add ("size", "Size");
    add ("decay", "Decay");
    add ("predelay", "Pre-delay");
    add ("damping", "Damping");
    add ("diffusion", "Diffusion");
    add ("modulation", "Modulation");
    add ("lowcut", "Low cut");
    add ("highcut", "High cut");
    add ("width", "Width");
    // lossy (9-10)
    add ("lossy_bitrate", "Bitrate");
    add ("lossy_amount", "Amount");
    // master (11-12)
    add ("mix", "Mix");
    add ("output", "Output");

    targetSwitch = std::make_unique<Segmented> (*s.getParameter ("lossy_target"), juce::StringArray { "WET ONLY", "ALL" });
    addAndMakeVisible (*targetSwitch);

    setSize (kWidth, kHeight);
    setResizable (false, false);
}

SkyverbAudioProcessorEditor::~SkyverbAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void SkyverbAudioProcessorEditor::resized()
{
    for (int i = 0; i < 9; ++i)
        rows[(size_t) i]->setBounds (kLeftX, kRowsTop + i * kRowH, kLeftW, kRowH);

    rows[9]->setBounds (kRightX, kRowsTop, kRightW, kRowH);
    rows[10]->setBounds (kRightX, kRowsTop + kRowH, kRightW, kRowH);
    targetSwitch->setBounds (kRightX + kLabelW, kRowsTop + 2 * kRowH + 5, 150, 22);

    rows[11]->setBounds (kRightX, kMasterRowsTop, kRightW, kRowH);
    rows[12]->setBounds (kRightX, kMasterRowsTop + kRowH, kRightW, kRowH);
}

void SkyverbAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (sky::ui::paper);

    // title
    g.setColour (sky::ui::ink);
    g.setFont (sky::ui::makeFont (26.0f, true, 0.30f));
    g.drawText ("SKYVERB", kMargin, 22, 320, 34, juce::Justification::centredLeft, false);

    g.setColour (sky::ui::grey);
    g.setFont (sky::ui::makeFont (10.5f, false, 0.22f));
    g.drawText ("REVERB + LOSSY CODEC", kWidth - kMargin - 300, 32, 300, 16, juce::Justification::centredRight, false);

    // heavy rule under the header
    g.setColour (sky::ui::ink);
    g.fillRect (kMargin, 68, kWidth - 2 * kMargin, 2);

    auto section = [&] (const juce::String& name, int x, int y, int w)
    {
        g.setColour (sky::ui::ink);
        g.setFont (sky::ui::makeFont (11.0f, true, 0.30f));
        g.drawText (name, x, y, 120, 16, juce::Justification::centredLeft, false);
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (sky::ui::makeFont (11.0f, true, 0.30f), name, 0.0f, 0.0f);
        const int tw = (int) std::ceil (glyphs.getBoundingBox (0, -1, true).getWidth()) + 12;
        g.setColour (sky::ui::hair);
        g.fillRect (x + tw, y + 8, w - tw, 1);
    };

    section ("REVERB", kLeftX, kSectionY, kLeftW);
    section ("LOSSY", kRightX, kSectionY, kRightW);
    section ("MASTER", kRightX, kMasterY, kRightW);

    // "TARGET" caption for the switch row
    g.setColour (sky::ui::grey);
    g.setFont (sky::ui::makeFont (10.5f, false, 0.16f));
    g.drawText ("APPLY TO", kRightX, kRowsTop + 2 * kRowH, kLabelW, kRowH, juce::Justification::centredLeft, false);

    // column separator
    g.setColour (sky::ui::hair);
    g.fillRect (kLeftX + kLeftW + 28, kSectionY, 1, kHeight - kSectionY - 40);

    // footer
    g.setColour (sky::ui::grey);
    g.setFont (sky::ui::makeFont (9.5f, false, 0.18f));
    g.drawText ("LATENCY " + juce::String (sky::LossyCodec::kLatency) + " SAMPLES  /  REPORTED TO HOST",
                kMargin, kHeight - 30, 400, 14, juce::Justification::centredLeft, false);
}
