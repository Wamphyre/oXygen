#include "MaximizerModule.h"
#include "../GUI/MaximizerEditor.h"

namespace oxygen
{

    MaximizerModule::MaximizerModule()
        : MasteringModule("Maximizer"),
          apvts(*this, nullptr, "Parameters", createParameterLayout())
    {
    }

    MaximizerModule::~MaximizerModule()
    {
    }

    juce::AudioProcessorValueTreeState::ParameterLayout MaximizerModule::createParameterLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        
        layout.add(std::make_unique<juce::AudioParameterFloat>("Threshold", "Threshold", 
            juce::NormalisableRange<float>(-60.0f, 0.0f, 0.1f), -0.1f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("Ceiling", "Ceiling", 
            juce::NormalisableRange<float>(-60.0f, 0.0f, 0.1f), -0.1f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("Release", "Release", 
            juce::NormalisableRange<float>(1.0f, 500.0f, 0.1f, 0.5f), 10.0f));
            
        layout.add(std::make_unique<juce::AudioParameterBool>("Bypass", "Bypass", false));

        return layout;
    }

    void MaximizerModule::prepareToPlay(double sampleRate, int samplesPerBlock)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = samplesPerBlock;
        spec.numChannels = getTotalNumOutputChannels();

        limiter.prepare(spec);
        updateParameters();
    }

    void MaximizerModule::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
    {
        if (apvts.getRawParameterValue("Bypass")->load() > 0.5f)
            return;

        updateParameters();
        
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);

        // 1. Soft Clip / Saturation (Ozone-like "Tube" or "IRC" emulation)
        // Adds harmonics and reduces peakiness before the hard wall limiter
        // This allows for louder perceived volume.
        const int numChannels = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();
        
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i)
            {
                // Simple Tanh Soft Clipper with slight drive
                // data[i] = std::tanh(data[i]); 
                // We can be more sophisticated:
                float x = data[i];
                if (x < -1.5f) x = -1.5f;
                else if (x > 1.5f) x = 1.5f;
                
                // Soft knee
                // Polynomial or Tanh. Tanh is classic.
                data[i] = std::tanh(x); 
            }
        }

        // 2. Hard Limiting
        limiter.process(context);
        
        // 3. Apply Make-up Gain (Ceiling)
        // Optimization: Use vector op
        buffer.applyGain(currentMakeupGain);
    }
    
    void MaximizerModule::updateParameters()
    {
        float thresh = apvts.getRawParameterValue("Threshold")->load();
        float rel = apvts.getRawParameterValue("Release")->load();
        float ceil = apvts.getRawParameterValue("Ceiling")->load();

        if (thresh == lastThreshold && rel == lastRelease && ceil == lastCeiling)
            return;

        lastThreshold = thresh;
        lastRelease = rel;
        lastCeiling = ceil;

        limiter.setThreshold(thresh);
        limiter.setRelease(rel);
        // Note: juce::dsp::Limiter doesn't have a direct "Ceiling" parameter.
        // In a full implementation, we might apply output gain or drive input gain.
    }

    juce::AudioProcessorEditor* MaximizerModule::createEditor()
    {
        return new MaximizerEditor(*this, apvts);
    }

}
