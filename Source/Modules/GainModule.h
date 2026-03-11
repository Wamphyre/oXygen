#pragma once

#include <JuceHeader.h>
#include "../MasteringModule.h"

namespace oxygen
{

    class GainModule : public MasteringModule
    {
    public:
        GainModule() : MasteringModule("Gain")
        {
            addParameter(gainParam = new juce::AudioParameterFloat(
                "gain", "Gain", 0.0f, 2.0f, 1.0f));
            addParameter(bypassParam = new juce::AudioParameterBool(
                "bypass", "Bypass", false));
        }

        ~GainModule() override = default;

        juce::RangedAudioParameter* getBypassParameter() const override { return bypassParam; }

        void prepareToPlay(double sampleRate, int samplesPerBlock) override
        {
            juce::ignoreUnused(samplesPerBlock);
            smoothedGain.reset(sampleRate, 0.02);
            smoothedGain.setCurrentAndTargetValue(*gainParam);
        }
        void releaseResources() override {}

        void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
        {
            if (bypassParam->get())
                return;

            smoothedGain.setTargetValue(*gainParam);

            const auto startGain = smoothedGain.getCurrentValue();
            const auto endGain = smoothedGain.skip(buffer.getNumSamples());

            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                buffer.applyGainRamp(channel, 0, buffer.getNumSamples(), startGain, endGain);
        }

    private:
        juce::AudioParameterFloat* gainParam;
        juce::AudioParameterBool* bypassParam;
        juce::LinearSmoothedValue<float> smoothedGain;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GainModule)
    };

} // namespace oxygen
