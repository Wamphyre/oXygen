#pragma once

#include <JuceHeader.h>
#include "../MasteringModule.h"

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

    private:
        // DSP Chain: 16 bands of IIR filters
        // Using a std::vector of filters for flexibility if ProcessorChain is too verbose for 16
        // Or simplified ProcessorChain. Let's use a vector of IIR Filters for loopability.
        
        using Filter = juce::dsp::IIR::Filter<float>;
        std::vector<std::unique_ptr<Filter>> filters;
        
        float lastGains[NumBands];
        
        // Helper to update filters
        void updateFilters();

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EqualizerModule)
    };

}
