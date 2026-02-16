#pragma once

#include <JuceHeader.h>
#include "../MasteringModule.h"

namespace oxygen
{

    class StereoImagerModule : public MasteringModule
    {
    public:
        StereoImagerModule();
        ~StereoImagerModule() override;

        void prepareToPlay(double sampleRate, int samplesPerBlock) override;
        void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;
        
        bool hasEditor() const override { return true; }
        juce::AudioProcessorEditor* createEditor() override;

        // Parameter layout
        static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
        juce::AudioProcessorValueTreeState apvts;
        juce::RangedAudioParameter* getBypassParameter() const override { return apvts.getParameter("Bypass"); }

    private:
        using Filter = juce::dsp::LinkwitzRileyFilter<float>;
        
        // Crossovers (same structure as Multiband Compressor)
        Filter crossoverLowMid;
        Filter crossoverMidHigh;
        Filter crossoverHigh;
        
        // Temporary buffers for bands
        juce::AudioBuffer<float> bufferLow;
        juce::AudioBuffer<float> bufferLowMid;
        juce::AudioBuffer<float> bufferHighMid;
        juce::AudioBuffer<float> bufferHigh;
        
        void updateParameters();
        void processBand(juce::AudioBuffer<float>& bandBuffer, float width);

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StereoImagerModule)
    };

}
