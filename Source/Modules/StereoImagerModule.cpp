#include "StereoImagerModule.h"
#include "../GUI/StereoImagerEditor.h"
#include <cmath>

namespace oxygen
{
    namespace
    {
        constexpr float minCrossoverSpacingHz = 20.0f;

        struct CrossoverSet
        {
            float lowMid = 200.0f;
            float midHigh = 2000.0f;
            float high = 8000.0f;
        };

        CrossoverSet sanitizeCrossovers(float sampleRate, float lowMid, float midHigh, float high)
        {
            const float nyquist = juce::jmax(60.0f, sampleRate * 0.45f);
            const float lowMidMax = juce::jmax(20.0f, nyquist - (2.0f * minCrossoverSpacingHz));

            CrossoverSet sanitized;
            sanitized.lowMid = juce::jlimit(20.0f, lowMidMax, lowMid);

            const float midHighMin = juce::jmin(nyquist - minCrossoverSpacingHz, sanitized.lowMid + minCrossoverSpacingHz);
            const float midHighMax = juce::jmax(midHighMin, nyquist - minCrossoverSpacingHz);
            sanitized.midHigh = juce::jlimit(midHighMin, midHighMax, midHigh);

            const float highMin = juce::jmin(nyquist, sanitized.midHigh + minCrossoverSpacingHz);
            sanitized.high = juce::jlimit(highMin, nyquist, high);

            return sanitized;
        }
    }

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

        crossoverLowMid.reset();
        crossoverMidHigh.reset();
        crossoverHigh.reset();
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
        if (numSamples == 0)
            return;
        
        if (numChannels < 2) return; // Stereo only
        
        // Ensure buffers
        if (bufferLow.getNumChannels() != numChannels || bufferLow.getNumSamples() < numSamples)
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

            auto* out = buffer.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i)
            {
                if (!std::isfinite(out[i]))
                    out[i] = 0.0f;
            }
        }
    }
    
    void StereoImagerModule::processBand(juce::AudioBuffer<float>& bandBuffer, float width)
    {
        if (bandBuffer.getNumChannels() != 2) return;

        width = juce::jlimit(0.0f, 2.0f, width);
        float sideScale = (width <= 1.0f)
                        ? width
                        : (1.0f + ((width - 1.0f) * 2.0f));
        
        auto* left = bandBuffer.getWritePointer(0);
        auto* right = bandBuffer.getWritePointer(1);
        int numSamples = bandBuffer.getNumSamples();

        double sumL2 = 0.0;
        double sumR2 = 0.0;
        double sumLR = 0.0;
        double sumMid2 = 0.0;
        double sumSide2 = 0.0;

        for (int i = 0; i < numSamples; ++i)
        {
            const float l = left[i];
            const float r = right[i];
            const float mid = 0.5f * (l + r);
            const float side = 0.5f * (l - r);

            sumL2 += (double) l * l;
            sumR2 += (double) r * r;
            sumLR += (double) l * r;
            sumMid2 += (double) mid * mid;
            sumSide2 += (double) side * side;
        }

        const double correlationDenominator = std::sqrt(sumL2 * sumR2);
        const float bandCorrelation = (correlationDenominator > 1.0e-12)
                                    ? juce::jlimit(-1.0f, 1.0f, (float) (sumLR / correlationDenominator))
                                    : 1.0f;
        const float sideRatio = (sumMid2 > 1.0e-12)
                              ? std::sqrt((float) (sumSide2 / sumMid2))
                              : 0.0f;

        if (sideScale > 1.0f)
        {
            if (bandCorrelation < 0.15f || sideRatio > 0.80f)
                sideScale = juce::jmin(sideScale, 1.10f);
            else if (bandCorrelation < 0.35f || sideRatio > 0.60f)
                sideScale = juce::jmin(sideScale, 1.25f);
        }

        float energyCompensation = 1.0f;
        if (sideScale > 1.0f)
        {
            const double originalEnergy = sumMid2 + sumSide2;
            const double widenedEnergy = sumMid2 + (sumSide2 * (double) sideScale * (double) sideScale);
            if (originalEnergy > 1.0e-12 && widenedEnergy > 1.0e-12)
                energyCompensation = std::sqrt((float) (originalEnergy / widenedEnergy));
        }
        
        for (int i = 0; i < numSamples; ++i)
        {
            float l = left[i];
            float r = right[i];
            
            float mid = (l + r) * 0.5f;
            float side = (l - r) * 0.5f;
            
            // Keep mono collapse linear, but make widening more explicit above 1.0.
            const float scaledMid = mid * energyCompensation;
            const float scaledSide = side * sideScale * energyCompensation;
            
            left[i] = scaledMid + scaledSide;
            right[i] = scaledMid - scaledSide;
        }
    }
    
    void StereoImagerModule::updateParameters()
    {
        const auto crossovers = sanitizeCrossovers((float) getSampleRate(),
                                                   apvts.getRawParameterValue("LowMidX")->load(),
                                                   apvts.getRawParameterValue("MidHighX")->load(),
                                                   apvts.getRawParameterValue("HighX")->load());

        crossoverLowMid.setCutoffFrequency(crossovers.lowMid);
        crossoverMidHigh.setCutoffFrequency(crossovers.midHigh);
        crossoverHigh.setCutoffFrequency(crossovers.high);
    }

    juce::AudioProcessorEditor* StereoImagerModule::createEditor()
    {
        return new StereoImagerEditor(*this, apvts);
    }

}
