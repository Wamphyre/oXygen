#pragma once

#include <JuceHeader.h>
#include "../MasteringModule.h"

namespace oxygen
{

    class MaximizerModule : public MasteringModule
    {
    public:
        static constexpr double lookaheadTimeMs = 5.0;
        static constexpr size_t oversamplingFactorExponent = 2;
        enum class Mode
        {
            Transparent = 0,
            Loud,
            Safe
        };

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
        static constexpr double parameterSmoothingMs = 20.0;
        static constexpr size_t maxSupportedChannels = 2;

        float lastThreshold = -100.0f, lastRelease = -1.0f, lastCeiling = -100.0f;
        int lastModeIndex = -1;
        float currentGain = 1.0f;
        int lookaheadSamples = 0;
        int oversampledLookaheadSamples = 0;
        int totalLatencySamples = 0;
        int delayWritePosition = 0;
        int bypassDelayWritePosition = 0;
        double currentSampleRate = 44100.0;
        double currentOversampledRate = 44100.0;
        juce::dsp::Oversampling<float> oversampling
        {
            maxSupportedChannels,
            oversamplingFactorExponent,
            juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
            true,
            true
        };
        juce::LinearSmoothedValue<float> inputDriveSmoothed;
        juce::LinearSmoothedValue<float> ceilingGainSmoothed;
        juce::AudioBuffer<float> delayBuffer;
        juce::AudioBuffer<float> bypassDelayBuffer;
        std::vector<float> gainReductionBuffer;
        std::vector<float> previousDrivenSamples;
        
        void updateParameters();
        void resetLimiterState();
        void processBypassDelay(juce::AudioBuffer<float>& buffer, bool writeToOutput);

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MaximizerModule)
    };

}
