#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/Reverb.h"
#include "dsp/LossyCodec.h"
#include "Presets.h"

class SkyverbAudioProcessor : public juce::AudioProcessor
{
public:
    SkyverbAudioProcessor();
    ~SkyverbAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "skyverb"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState apvts;
    sky::PresetManager presets; // must be declared after apvts

private:
    static constexpr int kChunk = 512;

    float param (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    sky::Reverb reverb;
    sky::LossyCodec codec;
    sky::DelayLine dryDelayL, dryDelayR;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSm, amountSm, outSm;

    float wetL[kChunk] {};
    float wetR[kChunk] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SkyverbAudioProcessor)
};
