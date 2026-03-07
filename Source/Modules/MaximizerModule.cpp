#include "MaximizerModule.h"
#include "../GUI/MaximizerEditor.h"
#include <cmath>

namespace oxygen
{
    namespace
    {
        constexpr float lookaheadTimeMs = 5.0f;

        float computeReleaseCoefficient(double sampleRate, float releaseMs)
        {
            if (sampleRate <= 0.0 || releaseMs <= 0.0f)
                return 0.0f;

            return std::exp(-1.0f / (0.001f * releaseMs * (float) sampleRate));
        }

        float estimateInterpolatedPeak(float previousSample, float currentSample)
        {
            const float quarter = (0.75f * previousSample) + (0.25f * currentSample);
            const float midpoint = 0.5f * (previousSample + currentSample);
            const float threeQuarter = (0.25f * previousSample) + (0.75f * currentSample);

            return juce::jmax(std::abs(currentSample),
                              std::abs(quarter),
                              std::abs(midpoint),
                              std::abs(threeQuarter));
        }
    }


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
        juce::ignoreUnused(samplesPerBlock);
        currentSampleRate = sampleRate;
        lookaheadSamples = juce::jmax(1, juce::roundToInt((sampleRate * lookaheadTimeMs) / 1000.0));
        const int bufferSize = lookaheadSamples + 1;
        delayBuffer.setSize(getTotalNumOutputChannels(), bufferSize);
        gainReductionBuffer.assign((size_t) bufferSize, 1.0f);
        previousDrivenSamples.assign((size_t) getTotalNumOutputChannels(), 0.0f);
        resetLimiterState();
        updateParameters();
    }

    void MaximizerModule::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
    {
        if (apvts.getRawParameterValue("Bypass")->load() > 0.5f)
            return;

        updateParameters();
        
        const int numChannels = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();
        if (numChannels == 0 || numSamples == 0)
            return;

        if (delayBuffer.getNumChannels() != numChannels || delayBuffer.getNumSamples() != lookaheadSamples + 1)
        {
            delayBuffer.setSize(numChannels, lookaheadSamples + 1);
            gainReductionBuffer.assign((size_t) (lookaheadSamples + 1), 1.0f);
            previousDrivenSamples.assign((size_t) numChannels, 0.0f);
            resetLimiterState();
        }

        for (int sample = 0; sample < numSamples; ++sample)
        {
            float detectorPeak = 0.0f;
            for (int channel = 0; channel < numChannels; ++channel)
            {
                const float drivenSample = buffer.getSample(channel, sample) * currentInputDrive;
                delayBuffer.setSample(channel, delayWritePosition, drivenSample);
                detectorPeak = juce::jmax(detectorPeak,
                                          estimateInterpolatedPeak(previousDrivenSamples[(size_t) channel], drivenSample));
                previousDrivenSamples[(size_t) channel] = drivenSample;
            }

            const float targetGain = (detectorPeak > currentCeilingGain && detectorPeak > 0.0f)
                                   ? (currentCeilingGain / detectorPeak)
                                   : 1.0f;
            gainReductionBuffer[(size_t) delayWritePosition] = juce::jlimit(0.0f, 1.0f, targetGain);

            const int readPosition = (delayWritePosition + 1) % delayBuffer.getNumSamples();

            float windowGain = 1.0f;
            for (const float bufferedGain : gainReductionBuffer)
                windowGain = juce::jmin(windowGain, bufferedGain);

            if (windowGain < currentGain)
                currentGain = windowGain;
            else
                currentGain = windowGain + (currentGain - windowGain) * releaseCoeff;

            for (int channel = 0; channel < numChannels; ++channel)
            {
                float outputSample = delayBuffer.getSample(channel, readPosition) * currentGain;
                if (!std::isfinite(outputSample))
                    outputSample = 0.0f;

                buffer.setSample(channel, sample, outputSample);
            }

            delayWritePosition = readPosition;
        }

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i)
            {
                if (!std::isfinite(data[i]))
                    data[i] = 0.0f;
            }
        }
    }
    
    void MaximizerModule::updateParameters()
    {
        const float thresh = juce::jlimit(-60.0f, 0.0f, apvts.getRawParameterValue("Threshold")->load());
        const float rel = juce::jlimit(1.0f, 500.0f, apvts.getRawParameterValue("Release")->load());
        const float ceil = juce::jlimit(-60.0f, 0.0f, apvts.getRawParameterValue("Ceiling")->load());

        if (std::abs(thresh - lastThreshold) <= 0.001f
            && std::abs(rel - lastRelease) <= 0.001f
            && std::abs(ceil - lastCeiling) <= 0.001f)
            return;

        lastThreshold = thresh;
        lastRelease = rel;
        lastCeiling = ceil;

        currentInputDrive = juce::Decibels::decibelsToGain(-thresh);
        currentCeilingGain = juce::Decibels::decibelsToGain(ceil);
        releaseCoeff = computeReleaseCoefficient(currentSampleRate, rel);

        if (!std::isfinite(currentInputDrive))
            currentInputDrive = 1.0f;
        if (!std::isfinite(currentCeilingGain))
            currentCeilingGain = 1.0f;
        if (!std::isfinite(releaseCoeff))
            releaseCoeff = 0.0f;
    }

    void MaximizerModule::resetLimiterState()
    {
        delayBuffer.clear();
        std::fill(gainReductionBuffer.begin(), gainReductionBuffer.end(), 1.0f);
        std::fill(previousDrivenSamples.begin(), previousDrivenSamples.end(), 0.0f);
        currentGain = 1.0f;
        delayWritePosition = 0;
    }

    juce::AudioProcessorEditor* MaximizerModule::createEditor()
    {
        return new MaximizerEditor(*this, apvts);
    }

}
