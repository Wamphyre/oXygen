#include "EqualizerModule.h"
#include "../GUI/EqualizerEditor.h"
#include <cmath>
#include <limits>

namespace oxygen
{

    EqualizerModule::EqualizerModule()
        : MasteringModule("Graphic EQ"),
          apvts(*this, nullptr, "Parameters", createParameterLayout())
    {
        for (int i = 0; i < NumBands; ++i)
        {
            filters.push_back(std::make_unique<Filter>());
            lastGains[i] = std::numeric_limits<float>::quiet_NaN();
            smoothedGains[(size_t) i].setCurrentAndTargetValue(0.0f);
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

        for (int i = 0; i < NumBands; ++i)
        {
            smoothedGains[(size_t) i].reset(sampleRate, parameterSmoothingSeconds);
            smoothedGains[(size_t) i].setCurrentAndTargetValue(apvts.getRawParameterValue("Gain" + juce::String(i))->load());
        }

        lastSampleRate = 0.0;
        updateFilters(samplesPerBlock);
    }

    void EqualizerModule::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
    {
        if (apvts.getRawParameterValue("Bypass")->load() > 0.5f)
            return;

        updateFilters(buffer.getNumSamples());
        
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        
        // Process serially
        for (auto& filter : filters)
            filter->process(context);
    }
    
    void EqualizerModule::updateFilters(int numSamples)
    {
        const auto currentSampleRate = getSampleRate();
        if (currentSampleRate <= 0.0)
            return;

        const bool sampleRateChanged = std::abs(currentSampleRate - lastSampleRate) > 1.0e-6;
        const float maxFreq = (float) currentSampleRate * 0.45f;
        if (maxFreq <= 20.0f)
            return;

        for (int i = 0; i < NumBands; ++i)
        {
            auto& smoothedGain = smoothedGains[(size_t) i];
            smoothedGain.setTargetValue(apvts.getRawParameterValue("Gain" + juce::String(i))->load());
            const float gainDb = (numSamples > 0)
                               ? smoothedGain.skip(numSamples)
                               : smoothedGain.getCurrentValue();
            
            // Optimization: Only update if changed (epsilon check)
            if (sampleRateChanged || !std::isfinite(lastGains[i]) || std::abs(gainDb - lastGains[i]) > 0.0025f)
            {
                lastGains[i] = gainDb;
                const float gain = std::isfinite(gainDb) ? juce::Decibels::decibelsToGain(gainDb) : 1.0f;
                const float q = BandQValues[(size_t) i];
                const float safeFreq = juce::jlimit(20.0f, maxFreq, Frequencies[i]);

                auto coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter(currentSampleRate, safeFreq, q, gain);
                *filters[i]->state = *coeffs;
            }
        }

        lastSampleRate = currentSampleRate;
    }

    juce::AudioProcessorEditor* EqualizerModule::createEditor()
    {
        return new EqualizerEditor(*this, apvts);
    }

}
