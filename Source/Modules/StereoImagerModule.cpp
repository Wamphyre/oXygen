#include "StereoImagerModule.h"
#include "../GUI/StereoImagerEditor.h"

namespace oxygen
{

    StereoImagerModule::StereoImagerModule()
        : MasteringModule("Stereo Imager"),
          apvts(*this, nullptr, "Parameters", createParameterLayout())
    {
        crossoverLowMid.setType(juce::dsp::LinkwitzRileyFilter<float>::Type::lowpass);
        crossoverMidHigh.setType(juce::dsp::LinkwitzRileyFilter<float>::Type::lowpass);
        crossoverHigh.setType(juce::dsp::LinkwitzRileyFilter<float>::Type::lowpass);
    }

    StereoImagerModule::~StereoImagerModule()
    {
    }

    juce::AudioProcessorValueTreeState::ParameterLayout StereoImagerModule::createParameterLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        
        // Crossovers
        layout.add(std::make_unique<juce::AudioParameterFloat>("LowMidX", "Low-Mid Xover", 
            juce::NormalisableRange<float>(20.0f, 1000.0f, 1.0f, 0.5f), 200.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("MidHighX", "Mid-High Xover", 
            juce::NormalisableRange<float>(500.0f, 5000.0f, 1.0f, 0.5f), 2000.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("HighX", "High Xover", 
            juce::NormalisableRange<float>(2000.0f, 15000.0f, 1.0f, 0.5f), 8000.0f));

        // Width per band (0.0 = Mono, 1.0 = Normal, 2.0 = Extra Wide)
        layout.add(std::make_unique<juce::AudioParameterFloat>("LowWidth", "Low Width", 
            juce::NormalisableRange<float>(0.0f, 2.0f, 0.01f), 1.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("LowMidWidth", "Low-Mid Width", 
            juce::NormalisableRange<float>(0.0f, 2.0f, 0.01f), 1.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("HighMidWidth", "High-Mid Width", 
            juce::NormalisableRange<float>(0.0f, 2.0f, 0.01f), 1.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("HighWidth", "High Width", 
            juce::NormalisableRange<float>(0.0f, 2.0f, 0.01f), 1.0f));
            
        layout.add(std::make_unique<juce::AudioParameterBool>("Bypass", "Bypass", false));

        return layout;
    }

    void StereoImagerModule::prepareToPlay(double sampleRate, int samplesPerBlock)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = samplesPerBlock;
        spec.numChannels = getTotalNumOutputChannels();

        crossoverLowMid.prepare(spec);
        crossoverMidHigh.prepare(spec);
        crossoverHigh.prepare(spec);
        
        bufferLow.setSize(spec.numChannels, samplesPerBlock);
        bufferLowMid.setSize(spec.numChannels, samplesPerBlock);
        bufferHighMid.setSize(spec.numChannels, samplesPerBlock);
        bufferHigh.setSize(spec.numChannels, samplesPerBlock);
        
        updateParameters();
    }

    void StereoImagerModule::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
    {
        if (apvts.getRawParameterValue("Bypass")->load() > 0.5f)
            return;

        updateParameters();
        
        const int numChannels = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();
        
        if (numChannels < 2) return; // Stereo only
        
        // Ensure buffers
        if (bufferLow.getNumSamples() < numSamples)
        {
            bufferLow.setSize(numChannels, numSamples, true, true, true);
            bufferLowMid.setSize(numChannels, numSamples, true, true, true);
            bufferHighMid.setSize(numChannels, numSamples, true, true, true);
            bufferHigh.setSize(numChannels, numSamples, true, true, true);
        }
        
        // Split Bands
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* inData = buffer.getReadPointer(ch);
            auto* lowData = bufferLow.getWritePointer(ch);
            auto* lowMidData = bufferLowMid.getWritePointer(ch);
            auto* highMidData = bufferHighMid.getWritePointer(ch);
            auto* highData = bufferHigh.getWritePointer(ch);
            
            for (int i = 0; i < numSamples; ++i)
            {
                float input = inData[i];
                float lowPath = 0.0f, highPath = 0.0f;
                float low = 0.0f, lowMid = 0.0f;
                float highMid = 0.0f, high = 0.0f;
                
                crossoverMidHigh.processSample(ch, input, lowPath, highPath);
                crossoverLowMid.processSample(ch, lowPath, low, lowMid);
                crossoverHigh.processSample(ch, highPath, highMid, high);
                
                lowData[i] = low;
                lowMidData[i] = lowMid;
                highMidData[i] = highMid;
                highData[i] = high;
            }
        }
        
        // Process Width per band
        processBand(bufferLow, apvts.getRawParameterValue("LowWidth")->load());
        processBand(bufferLowMid, apvts.getRawParameterValue("LowMidWidth")->load());
        processBand(bufferHighMid, apvts.getRawParameterValue("HighMidWidth")->load());
        processBand(bufferHigh, apvts.getRawParameterValue("HighWidth")->load());
        
        // Sum back
        buffer.clear();
        for (int ch = 0; ch < numChannels; ++ch)
        {
            buffer.addFrom(ch, 0, bufferLow, ch, 0, numSamples);
            buffer.addFrom(ch, 0, bufferLowMid, ch, 0, numSamples);
            buffer.addFrom(ch, 0, bufferHighMid, ch, 0, numSamples);
            buffer.addFrom(ch, 0, bufferHigh, ch, 0, numSamples);
        }
    }
    
    void StereoImagerModule::processBand(juce::AudioBuffer<float>& bandBuffer, float width)
    {
        if (bandBuffer.getNumChannels() != 2) return;
        
        auto* left = bandBuffer.getWritePointer(0);
        auto* right = bandBuffer.getWritePointer(1);
        int numSamples = bandBuffer.getNumSamples();
        
        for (int i = 0; i < numSamples; ++i)
        {
            float l = left[i];
            float r = right[i];
            
            float mid = (l + r) * 0.5f;
            float side = (l - r) * 0.5f;
            
            // Apply width
            side *= width;
            
            left[i] = mid + side;
            right[i] = mid - side;
        }
    }
    
    void StereoImagerModule::updateParameters()
    {
        crossoverLowMid.setCutoffFrequency(apvts.getRawParameterValue("LowMidX")->load());
        crossoverMidHigh.setCutoffFrequency(apvts.getRawParameterValue("MidHighX")->load());
        crossoverHigh.setCutoffFrequency(apvts.getRawParameterValue("HighX")->load());
    }

    juce::AudioProcessorEditor* StereoImagerModule::createEditor()
    {
        return new StereoImagerEditor(*this, apvts);
    }

}
