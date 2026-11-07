#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "SkyLookAndFeel.h"

class SkyverbAudioProcessorEditor : public juce::AudioProcessorEditor,
                                    private juce::Timer
{
public:
    explicit SkyverbAudioProcessorEditor (SkyverbAudioProcessor&);
    ~SkyverbAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class SliderRow;
    class Segmented;
    class Btn;

    void timerCallback() override;
    void applyTheme();
    void setDark (bool dark);

    void showPresetMenu();
    void stepPreset (int direction);
    void beginSave();
    void commitSave();
    void cancelSave();
    void refreshPresetLabel();

    SkyverbAudioProcessor& processor;
    SkyLookAndFeel laf;

    float themeMix = 0.0f;    // 0 = light, 1 = dark (animated)
    float themeTarget = 0.0f;
    double lastTick = 0.0;

    std::vector<std::unique_ptr<SliderRow>> rows;
    std::unique_ptr<Segmented> targetSwitch, themeSwitch;
    std::unique_ptr<Btn> prevBtn, nextBtn, nameBtn, saveBtn;
    std::unique_ptr<juce::ParameterAttachment> targetAttachment;
    juce::TextEditor nameEditor;

    bool editingName = false;
    juce::String shownLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SkyverbAudioProcessorEditor)
};
