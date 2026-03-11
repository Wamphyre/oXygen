#pragma once

#include <JuceHeader.h>
#include "../MasteringModule.h"
#include <array>

namespace oxygen
{

    class EqualizerModule : public MasteringModule
    {
    public:
        EqualizerModule();
        ~EqualizerModule() override;

        void prepareToPlay(double sampleRate, int samplesPerBlock) override;
        void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;
        
        bool hasEditor() const override { return true; }
        juce::AudioProcessorEditor* createEditor() override;

        // Parameter layout
        static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
        juce::AudioProcessorValueTreeState apvts;
        juce::RangedAudioParameter* getBypassParameter() const override { return apvts.getParameter("Bypass"); }
        
        // 15 Bands: 30, 40, 60, 100, 180, 300, 500, 900, 1.5k, 2.5k, 4k, 6k, 10k, 15k, 20k
        static constexpr int NumBands = 15;
        static constexpr float Frequencies[NumBands] = { 
            30.0f, 40.0f, 60.0f, 100.0f, 
            180.0f, 300.0f, 500.0f, 900.0f, 
            1500.0f, 2500.0f, 4000.0f, 6000.0f, 
            10000.0f, 15000.0f, 20000.0f 
        };
        static constexpr std::array<float, NumBands> BandQValues = {
            0.78f, 0.84f, 0.92f, 1.02f,
            1.10f, 1.16f, 1.22f, 1.28f,
            1.28f, 1.22f, 1.14f, 1.05f,
            0.95f, 0.84f, 0.76f
        };

    private:
        static constexpr double parameterSmoothingSeconds = 0.035;
        // One peak filter per EQ band, duplicated across all active channels.
        
        using MonoFilter = juce::dsp::IIR::Filter<float>;
        using Filter = juce::dsp::ProcessorDuplicator<MonoFilter, juce::dsp::IIR::Coefficients<float>>;
        std::vector<std::unique_ptr<Filter>> filters;
        std::array<juce::LinearSmoothedValue<float>, NumBands> smoothedGains;
        
        float lastGains[NumBands];
        double lastSampleRate = 0.0;
        
        // Helper to update filters
        void updateFilters(int numSamples);

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EqualizerModule)
    };

}
