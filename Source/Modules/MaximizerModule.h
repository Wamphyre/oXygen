#pragma once

#include <JuceHeader.h>
#include "../MasteringModule.h"

namespace oxygen
{

    class MaximizerModule : public MasteringModule
    {
    public:
        MaximizerModule();
        ~MaximizerModule() override;

        void prepareToPlay(double sampleRate, int samplesPerBlock) override;
        void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;
        
        bool hasEditor() const override { return true; }
        juce::AudioProcessorEditor* createEditor() override;

        // Parameter layout
        static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
        juce::AudioProcessorValueTreeState apvts;
        juce::RangedAudioParameter* getBypassParameter() const override { return apvts.getParameter("Bypass"); }

    private:
        juce::dsp::Limiter<float> limiter;
        
        float lastThreshold = -100.0f, lastRelease = -1.0f, lastCeiling = -100.0f;
        
        void updateParameters();

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MaximizerModule)
    };

}
