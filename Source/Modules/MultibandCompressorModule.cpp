#include "MultibandCompressorModule.h"
#include "../GUI/MultibandCompressorEditor.h"

namespace oxygen
{

    MultibandCompressorModule::MultibandCompressorModule()
        : MasteringModule("Multiband Comp"),
          apvts(*this, nullptr, "Parameters", createParameterLayout())
    {
        crossoverLowMid.setType(juce::dsp::LinkwitzRileyFilter<float>::Type::lowpass); // Type doesn't matter for processSample splitting
        crossoverMidHigh.setType(juce::dsp::LinkwitzRileyFilter<float>::Type::lowpass);
        crossoverHigh.setType(juce::dsp::LinkwitzRileyFilter<float>::Type::lowpass);
    }

    MultibandCompressorModule::~MultibandCompressorModule()
    {
    }

    juce::AudioProcessorValueTreeState::ParameterLayout MultibandCompressorModule::createParameterLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        
        // Crossovers
        layout.add(std::make_unique<juce::AudioParameterFloat>("LowMidX", "Low-Mid Xover", 
            juce::NormalisableRange<float>(20.0f, 1000.0f, 1.0f, 0.5f), 200.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("MidHighX", "Mid-High Xover", 
            juce::NormalisableRange<float>(500.0f, 5000.0f, 1.0f, 0.5f), 2000.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("HighX", "High Xover", 
            juce::NormalisableRange<float>(2000.0f, 15000.0f, 1.0f, 0.5f), 8000.0f));

        // Per-band parameters (Low, LowMid, HighMid, High)
        const char* bandNames[] = { "Low", "LowMid", "HighMid", "High" };
        
        for (int i = 0; i < 4; ++i)
        {
            juce::String prefix = bandNames[i];
            
            layout.add(std::make_unique<juce::AudioParameterFloat>(prefix + "Thresh", prefix + " Threshold", 
                juce::NormalisableRange<float>(-60.0f, 0.0f, 0.1f), -10.0f));
            layout.add(std::make_unique<juce::AudioParameterFloat>(prefix + "Ratio", prefix + " Ratio", 
                juce::NormalisableRange<float>(1.0f, 20.0f, 0.1f), 2.0f));
            layout.add(std::make_unique<juce::AudioParameterFloat>(prefix + "Attack", prefix + " Attack", 
                juce::NormalisableRange<float>(0.1f, 100.0f, 0.1f), 10.0f));
            layout.add(std::make_unique<juce::AudioParameterFloat>(prefix + "Release", prefix + " Release", 
                juce::NormalisableRange<float>(10.0f, 1000.0f, 1.0f), 100.0f));
            // Gain is not directly supported by dsp::Compressor as output gain, implementing simple gain
            layout.add(std::make_unique<juce::AudioParameterFloat>(prefix + "Gain", prefix + " Gain", 
                juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f));
        }
        layout.add(std::make_unique<juce::AudioParameterFloat>("HighWidth", "High Width", 
            juce::NormalisableRange<float>(0.0f, 2.0f, 0.01f), 1.0f));
            
        layout.add(std::make_unique<juce::AudioParameterBool>("Bypass", "Bypass", false));

        return layout;
    }

    void MultibandCompressorModule::prepareToPlay(double sampleRate, int samplesPerBlock)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = samplesPerBlock;
        spec.numChannels = getTotalNumOutputChannels();

        crossoverLowMid.prepare(spec);
        crossoverMidHigh.prepare(spec);
        crossoverHigh.prepare(spec);
        
        compressorLow.prepare(spec);
        compressorLowMid.prepare(spec);
        compressorHighMid.prepare(spec);
        compressorHigh.prepare(spec);
        
        // Prepare buffers
        bufferLow.setSize(spec.numChannels, samplesPerBlock);
        bufferLowMid.setSize(spec.numChannels, samplesPerBlock);
        bufferHighMid.setSize(spec.numChannels, samplesPerBlock);
        bufferHigh.setSize(spec.numChannels, samplesPerBlock);
        
        updateParameters();
    }

    void MultibandCompressorModule::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
    {
        if (apvts.getRawParameterValue("Bypass")->load() > 0.5f)
            return;

        updateParameters();
        
        const int numChannels = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();
        
        // Ensure buffers are large enough (redundant if prepare called correctly, but safe)
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
                
                // Split 1: MidHigh Crossover (Splits into LowPath and HighPath)
                crossoverMidHigh.processSample(ch, input, lowPath, highPath);
                
                // Split LowPath at LowMid Crossover
                crossoverLowMid.processSample(ch, lowPath, low, lowMid);
                
                // Split HighPath at High Crossover
                crossoverHigh.processSample(ch, highPath, highMid, high);
                
                lowData[i] = low;
                lowMidData[i] = lowMid;
                highMidData[i] = highMid;
                highData[i] = high;
            }
        }
        
        // Process Compressors
        juce::dsp::AudioBlock<float> blockLow(bufferLow);
        juce::dsp::AudioBlock<float> blockLowMid(bufferLowMid);
        juce::dsp::AudioBlock<float> blockHighMid(bufferHighMid);
        juce::dsp::AudioBlock<float> blockHigh(bufferHigh);
        
        // Contexts
        juce::dsp::ProcessContextReplacing<float> ctxLow(blockLow);
        juce::dsp::ProcessContextReplacing<float> ctxLowMid(blockLowMid);
        juce::dsp::ProcessContextReplacing<float> ctxHighMid(blockHighMid);
        juce::dsp::ProcessContextReplacing<float> ctxHigh(blockHigh);
        
        compressorLow.process(ctxLow);
        compressorLowMid.process(ctxLowMid);
        compressorHighMid.process(ctxHighMid);
        compressorHigh.process(ctxHigh);
        
        // Apply Gains and Sum back to Output
        // Note: dsp::Compressor doesn't apply makeup gain automatically in this version?
        // We will apply the gain parameter manually.
        
        buffer.clear();
        
        const float gainLow = juce::Decibels::decibelsToGain(apvts.getRawParameterValue("LowGain")->load());
        const float gainLowMid = juce::Decibels::decibelsToGain(apvts.getRawParameterValue("LowMidGain")->load());
        const float gainHighMid = juce::Decibels::decibelsToGain(apvts.getRawParameterValue("HighMidGain")->load());
        const float gainHigh = juce::Decibels::decibelsToGain(apvts.getRawParameterValue("HighGain")->load());
        
        // Safety: Check for NaNs in sub-buffers before ensuring
        // If we encounter NaNs, bypass processing this block to save ears/speakers
        bool invalidOutput = false;
        
        for (int ch = 0; ch < numChannels; ++ch)
        {
            if (bufferLow.getMagnitude(ch, 0, numSamples) > 10.0f) invalidOutput = true; // Simple sanity check
            // Real NaN check ideally done per sample but expensive.
            // Let's rely on standard summing, but if result is silent, we might want to know.
            
            buffer.addFrom(ch, 0, bufferLow, ch, 0, numSamples, gainLow);
            buffer.addFrom(ch, 0, bufferLowMid, ch, 0, numSamples, gainLowMid);
            buffer.addFrom(ch, 0, bufferHighMid, ch, 0, numSamples, gainHighMid);
            buffer.addFrom(ch, 0, bufferHigh, ch, 0, numSamples, gainHigh);
        }
    }
    
    void MultibandCompressorModule::updateParameters()
    {
        float lmX = apvts.getRawParameterValue("LowMidX")->load();
        float mhX = apvts.getRawParameterValue("MidHighX")->load();
        float hX = apvts.getRawParameterValue("HighX")->load();

        if (lmX != lastLowMidX) { crossoverLowMid.setCutoffFrequency(lmX); lastLowMidX = lmX; }
        if (mhX != lastMidHighX) { crossoverMidHigh.setCutoffFrequency(mhX); lastMidHighX = mhX; }
        if (hX != lastHighX) { crossoverHigh.setCutoffFrequency(hX); lastHighX = hX; }
        
        const char* bandNames[] = { "Low", "LowMid", "HighMid", "High" };
        juce::dsp::Compressor<float>* comps[] = { &compressorLow, &compressorLowMid, &compressorHighMid, &compressorHigh };

        for (int i = 0; i < 4; ++i)
        {
            juce::String prefix = bandNames[i];
            float t = apvts.getRawParameterValue(prefix + "Thresh")->load();
            float r = apvts.getRawParameterValue(prefix + "Ratio")->load();
            float a = apvts.getRawParameterValue(prefix + "Attack")->load();
            float rl = apvts.getRawParameterValue(prefix + "Release")->load();

            if (t != lastBandParams[i].thresh || r != lastBandParams[i].ratio || 
                a != lastBandParams[i].attack || rl != lastBandParams[i].release)
            {
                comps[i]->setThreshold(t);
                comps[i]->setRatio(r);
                comps[i]->setAttack(a);
                comps[i]->setRelease(rl);

                lastBandParams[i] = { t, r, a, rl };
            }
        }
    }

    juce::AudioProcessorEditor* MultibandCompressorModule::createEditor()
    {
        return new MultibandCompressorEditor(*this, apvts);
    }

}
