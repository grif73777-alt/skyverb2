#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
using Param  = juce::AudioParameterFloat;
using Attrs  = juce::AudioParameterFloatAttributes;

juce::String fmtPercent (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; }

juce::String fmtMs (float v, int) { return juce::String (juce::roundToInt (v)) + " ms"; }

juce::String fmtSeconds (float v, int) { return juce::String (v, v < 10.0f ? 2 : 1) + " s"; }

juce::String fmtHz (float v, int)
{
    return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
}

juce::String fmtDb (float v, int) { return juce::String (v, 1) + " dB"; }

juce::String fmtKbps (float v, int) { return juce::String (juce::roundToInt (v)) + " kbps"; }

float parseNumber (const juce::String& s) { return s.retainCharacters ("0123456789.-").getFloatValue(); }

std::unique_ptr<Param> makeParam (const char* id, const char* name, juce::NormalisableRange<float> range, float def,
                                  juce::String (*fmt) (float, int), float kiloScale = 1.0f)
{
    return std::make_unique<Param> (juce::ParameterID { id, 1 }, name, range, def,
                                    Attrs()
                                        .withStringFromValueFunction ([fmt] (float v, int len) { return fmt (v, len); })
                                        .withValueFromStringFunction ([kiloScale] (const juce::String& s)
                                        {
                                            const auto lower = s.toLowerCase();
                                            return parseNumber (s) * (lower.contains ("k") && kiloScale > 1.0f ? kiloScale : 1.0f);
                                        }));
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout SkyverbAudioProcessor::createLayout()
{
    using Range = juce::NormalisableRange<float>;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // ---- reverb
    layout.add (makeParam ("size",       "Size",       Range (0.0f, 100.0f, 0.1f), 50.0f, fmtPercent));
    layout.add (makeParam ("decay",      "Decay",      Range (0.1f, 30.0f, 0.01f, 0.35f), 2.5f, fmtSeconds));
    layout.add (makeParam ("predelay",   "Pre-delay",  Range (0.0f, 250.0f, 0.1f, 0.6f), 15.0f, fmtMs));
    layout.add (makeParam ("damping",    "Damping",    Range (0.0f, 100.0f, 0.1f), 40.0f, fmtPercent));
    layout.add (makeParam ("diffusion",  "Diffusion",  Range (0.0f, 100.0f, 0.1f), 70.0f, fmtPercent));
    layout.add (makeParam ("modulation", "Modulation", Range (0.0f, 100.0f, 0.1f), 30.0f, fmtPercent));
    layout.add (makeParam ("lowcut",     "Low cut",    Range (20.0f, 1000.0f, 1.0f, 0.4f), 100.0f, fmtHz));
    layout.add (makeParam ("highcut",    "High cut",   Range (1000.0f, 20000.0f, 1.0f, 0.4f), 14000.0f, fmtHz, 1000.0f));
    layout.add (makeParam ("width",      "Width",      Range (0.0f, 150.0f, 0.1f), 100.0f, fmtPercent));

    // ---- lossy
    layout.add (makeParam ("lossy_bitrate", "Lossy Bitrate", Range (8.0f, 192.0f, 1.0f, 0.55f), 96.0f, fmtKbps));
    layout.add (makeParam ("lossy_amount",  "Lossy Amount",  Range (0.0f, 100.0f, 0.1f), 0.0f, fmtPercent));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "lossy_target", 1 }, "Lossy Target",
                                                              juce::StringArray { "Wet", "All" }, 0));

    // ---- master
    layout.add (makeParam ("mix",    "Mix",    Range (0.0f, 100.0f, 0.1f), 30.0f, fmtPercent));
    layout.add (makeParam ("output", "Output", Range (-24.0f, 12.0f, 0.1f), 0.0f, fmtDb));

    return layout;
}

SkyverbAudioProcessor::SkyverbAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SKYVERB_STATE", createLayout())
{
    setLatencySamples (sky::LossyCodec::kLatency);
}

bool SkyverbAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

double SkyverbAudioProcessor::getTailLengthSeconds() const
{
    return (double) param ("decay") + (double) param ("predelay") * 0.001 + 0.2;
}

void SkyverbAudioProcessor::prepareToPlay (double sampleRate, int)
{
    reverb.prepare (sampleRate);
    codec.prepare (sampleRate);
    dryDelayL.prepare (sky::LossyCodec::kLatency + 16);
    dryDelayR.prepare (sky::LossyCodec::kLatency + 16);

    mixSm.reset (sampleRate, 0.03);
    amountSm.reset (sampleRate, 0.03);
    outSm.reset (sampleRate, 0.03);
    mixSm.setCurrentAndTargetValue (param ("mix") * 0.01f);
    amountSm.setCurrentAndTargetValue (param ("lossy_amount") * 0.01f);
    outSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (param ("output")));

    setLatencySamples (sky::LossyCodec::kLatency);
}

void SkyverbAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (buffer.getNumChannels() < 2)
        return;

    sky::ReverbParams rp;
    rp.size = param ("size") * 0.01f;
    rp.decay = param ("decay");
    rp.predelayMs = param ("predelay");
    rp.damping = param ("damping") * 0.01f;
    rp.diffusion = param ("diffusion") * 0.01f;
    rp.modulation = param ("modulation") * 0.01f;
    rp.lowCutHz = param ("lowcut");
    rp.highCutHz = param ("highcut");
    rp.width = param ("width") * 0.01f;
    reverb.setParams (rp);

    codec.setBitrate (param ("lossy_bitrate"));

    mixSm.setTargetValue (param ("mix") * 0.01f);
    amountSm.setTargetValue (param ("lossy_amount") * 0.01f);
    outSm.setTargetValue (juce::Decibels::decibelsToGain (param ("output")));
    const bool lossyOnWetOnly = param ("lossy_target") < 0.5f;

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    const int numSamples = buffer.getNumSamples();

    for (int start = 0; start < numSamples; start += kChunk)
    {
        const int n = juce::jmin (kChunk, numSamples - start);
        float* cl = L + start;
        float* cr = R + start;

        reverb.process (cl, cr, wetL, wetR, n);

        for (int i = 0; i < n; ++i)
        {
            const float m = mixSm.getNextValue();
            const float am = amountSm.getNextValue();
            const float og = outSm.getNextValue();

            // equal-power dry/wet
            const float dryG = std::cos (m * juce::MathConstants<float>::halfPi);
            const float wetG = std::sin (m * juce::MathConstants<float>::halfPi);

            const float dL = cl[i], dR = cr[i];
            float coded[2], plain[2], outS[2];

            if (lossyOnWetOnly)
            {
                // codec sees only the reverb; dry is delayed by the codec latency
                codec.processSample (wetL[i], wetR[i], coded[0], coded[1], plain[0], plain[1]);

                const float dryDelL = dryDelayL.readInt (sky::LossyCodec::kLatency);
                const float dryDelR = dryDelayR.readInt (sky::LossyCodec::kLatency);
                dryDelayL.push (dL);
                dryDelayR.push (dR);

                outS[0] = dryDelL * dryG + (plain[0] + am * (coded[0] - plain[0])) * wetG;
                outS[1] = dryDelR * dryG + (plain[1] + am * (coded[1] - plain[1])) * wetG;
            }
            else
            {
                // codec sees the complete mix (dry + reverb)
                const float inL = dL * dryG + wetL[i] * wetG;
                const float inR = dR * dryG + wetR[i] * wetG;
                codec.processSample (inL, inR, coded[0], coded[1], plain[0], plain[1]);

                outS[0] = plain[0] + am * (coded[0] - plain[0]);
                outS[1] = plain[1] + am * (coded[1] - plain[1]);
            }

            cl[i] = outS[0] * og;
            cr[i] = outS[1] * og;
        }
    }
}

juce::AudioProcessorEditor* SkyverbAudioProcessor::createEditor()
{
    return new SkyverbAudioProcessorEditor (*this);
}

void SkyverbAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SkyverbAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SkyverbAudioProcessor();
}
