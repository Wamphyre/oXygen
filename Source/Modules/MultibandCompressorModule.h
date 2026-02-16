#pragma once

#include <JuceHeader.h>
#include "../MasteringModule.h"

namespace oxygen
{

    class MultibandCompressorModule : public MasteringModule
    {
    public:
        MultibandCompressorModule();
        ~MultibandCompressorModule() override;

        void prepareToPlay(double sampleRate, int samplesPerBlock) override;
        void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;
        
        bool hasEditor() const override { return true; }
        juce::AudioProcessorEditor* createEditor() override;

        // Parameter layout
        static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
        juce::AudioProcessorValueTreeState apvts;
        juce::RangedAudioParameter* getBypassParameter() const override { return apvts.getParameter("Bypass"); }

    private:
        // DSP Objects
        // 3 Crossovers to create 4 bands
        // Tree structure:
        //                  Input
        //                    |
        //                  [LP/HP @ MidHigh Freq]
        //                  /   \
        //            LowPath    HighPath
        //              |            |
        // [LP/HP @ LowMid Freq]  [LP/HP @ High Freq]
        //      /      \             /      \
        //    Low    LowMid       HighMid   High
        
        using Filter = juce::dsp::LinkwitzRileyFilter<float>;
        using Compressor = juce::dsp::Compressor<float>;
        
        Filter crossoverLowMid;
        Filter crossoverMidHigh;
        Filter crossoverHigh;
        
        // We need 2 filters for each crossover split per channel? 
        // No, LinkwitzRileyFilter::processSample returns both LP and HP outputs.
        // But LinkwitzRileyFilter maintains state per channel internally if configured correctly?
        // Actually LinkwitzRileyFilter is usually mono or multi-channel handling in process(). 
        // processSample takes a channel index.
        
        Compressor compressorLow;
        Compressor compressorLowMid;
        Compressor compressorHighMid;
        Compressor compressorHigh;
        
        // Temporary buffers for bands
        juce::AudioBuffer<float> bufferLow, bufferLowMid, bufferHighMid, bufferHigh;
        
        // Parameter caching for optimization
        float lastLowMidX = -1.0f, lastMidHighX = -1.0f, lastHighX = -1.0f;
        struct BandParams { float thresh, ratio, attack, release; };
        std::array<BandParams, 4> lastBandParams;
        
        // Helper to update parameters
        void updateParameters();

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultibandCompressorModule)
    };

}
