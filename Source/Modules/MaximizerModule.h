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
        float lastThreshold = -100.0f, lastRelease = -1.0f, lastCeiling = -100.0f;
        float currentInputDrive = 1.0f;
        float currentCeilingGain = 1.0f;
        float releaseCoeff = 0.0f;
        float currentGain = 1.0f;
        int lookaheadSamples = 0;
        int delayWritePosition = 0;
        double currentSampleRate = 44100.0;
        juce::AudioBuffer<float> delayBuffer;
        std::vector<float> gainReductionBuffer;
        std::vector<float> previousDrivenSamples;
        
        void updateParameters();
        void resetLimiterState();

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MaximizerModule)
    };

}
