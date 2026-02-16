#include "EqualizerModule.h"
#include "../GUI/EqualizerEditor.h"

namespace oxygen
{

    EqualizerModule::EqualizerModule()
        : MasteringModule("Graphic EQ"),
          apvts(*this, nullptr, "Parameters", createParameterLayout())
    {
        for (int i = 0; i < NumBands; ++i)
        {
            filters.push_back(std::make_unique<Filter>());
            lastGains[i] = 0.0f;
        }
    }

    EqualizerModule::~EqualizerModule()
    {
    }

    juce::AudioProcessorValueTreeState::ParameterLayout EqualizerModule::createParameterLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        
        layout.add(std::make_unique<juce::AudioParameterBool>("Bypass", "Bypass", false));

        for (int i = 0; i < NumBands; ++i)
        {
            juce::String freqStr;
            if (Frequencies[i] >= 1000.0f)
                freqStr = juce::String(Frequencies[i] / 1000.0f, 1) + "k";
            else
                freqStr = juce::String((int)Frequencies[i]);

            layout.add(std::make_unique<juce::AudioParameterFloat>(
                "Gain" + juce::String(i), 
                freqStr + "Hz", 
                juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 
                0.0f));
        }
            
        return layout;
    }

    void EqualizerModule::prepareToPlay(double sampleRate, int samplesPerBlock)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = samplesPerBlock;
        spec.numChannels = getTotalNumOutputChannels();

        for (auto& filter : filters)
        {
            filter->prepare(spec);
            filter->reset();
        }
        updateFilters();
    }

    void EqualizerModule::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
    {
        if (apvts.getRawParameterValue("Bypass")->load() > 0.5f)
            return;

        updateFilters();
        
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        
        // Process serially
        for (auto& filter : filters)
            filter->process(context);
    }
    
    void EqualizerModule::updateFilters()
    {
        for (int i = 0; i < NumBands; ++i)
        {
            float gainDb = apvts.getRawParameterValue("Gain" + juce::String(i))->load();
            
            // Optimization: Only update if changed (epsilon check)
            if (std::abs(gainDb - lastGains[i]) > 0.01f)
            {
                lastGains[i] = gainDb;
                float gain = juce::Decibels::decibelsToGain(gainDb);
                float q = 1.41f; // Standard Octave Q
                
                *filters[i]->coefficients = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(getSampleRate(), Frequencies[i], q, gain);
            }
        }
    }

    juce::AudioProcessorEditor* EqualizerModule::createEditor()
    {
        return new EqualizerEditor(*this, apvts);
    }

}
