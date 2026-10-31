#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "SkyLookAndFeel.h"

class SkyverbAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit SkyverbAudioProcessorEditor (SkyverbAudioProcessor&);
    ~SkyverbAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class SliderRow;
    class Segmented;

    SkyverbAudioProcessor& processor;
    SkyLookAndFeel laf;

    std::vector<std::unique_ptr<SliderRow>> rows;
    std::unique_ptr<Segmented> targetSwitch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SkyverbAudioProcessorEditor)
};
