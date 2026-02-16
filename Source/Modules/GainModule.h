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

        void prepareToPlay(double sampleRate, int samplesPerBlock) override {}
        void releaseResources() override {}

        void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
        {
            if (bypassParam->get())
                return;

            buffer.applyGain(*gainParam);
        }

    private:
        juce::AudioParameterFloat* gainParam;
        juce::AudioParameterBool* bypassParam;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GainModule)
    };

} // namespace oxygen
