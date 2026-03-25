#include "MaximizerModule.h"
#include "../GUI/MaximizerEditor.h"
#include <array>
#include <cmath>

namespace oxygen
{
    namespace
    {
        struct MaximizerModeTuning
        {
            float clipHeadroomOffsetDb = 0.0f;
            float clipOnsetFraction = 0.38f;
            float releaseScale = 1.0f;
            float adaptiveReleaseMinScale = 0.45f;
            float detectorBias = 1.0f;
            float finalSafetyOnset = 0.995f;
        };

        MaximizerModeTuning getModeTuning(int modeIndex)
        {
            switch (static_cast<MaximizerModule::Mode>(modeIndex))
            {
                case MaximizerModule::Mode::Loud:
                    return { -0.18f, 0.24f, 0.84f, 0.30f, 1.0f, 0.985f };

                case MaximizerModule::Mode::Safe:
                    return { 0.40f, 0.58f, 1.22f, 0.68f, 1.02f, 0.998f };

                case MaximizerModule::Mode::Transparent:
                default:
                    return {};
            }
        }

        float decibelsToGainSafe(float db)
        {
            const float gain = juce::Decibels::decibelsToGain(db);
            return std::isfinite(gain) ? gain : 1.0f;
        }

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

        float computeAdaptiveReleaseCoefficient(double sampleRate,
                                               float releaseMs,
                                               float currentGain,
                                               float windowGain,
                                               float minimumScale)
        {
            const float gainReduction = juce::jlimit(0.0f, 1.0f, 1.0f - juce::jmin(currentGain, windowGain));
            const float releaseScale = juce::jmap(gainReduction, 0.0f, 1.0f, 1.0f, minimumScale);
            return computeReleaseCoefficient(sampleRate, juce::jmax(1.0f, releaseMs * releaseScale));
        }

        float computeHybridClipHeadroomDb(float driveDb, float releaseMs)
        {
            const float driveHeadroomDb = juce::jmap(juce::jlimit(0.0f, 12.0f, driveDb),
                                                     0.0f, 12.0f,
                                                     1.8f, 0.7f);
            const float releaseTrimDb = juce::jmap(juce::jlimit(40.0f, 220.0f, releaseMs),
                                                   40.0f, 220.0f,
                                                   -0.12f, 0.18f);
            return juce::jlimit(0.5f, 2.2f, driveHeadroomDb + releaseTrimDb);
        }

        float softSaturateToCeiling(float sample, float onset, float ceiling)
        {
            const float safeCeiling = juce::jmax(1.0e-5f, ceiling);
            const float safeOnset = juce::jlimit(0.0f, safeCeiling - 1.0e-6f, onset);
            const float absSample = std::abs(sample);

            if (absSample <= safeOnset || safeCeiling <= safeOnset)
                return sample;

            const float normalized = (absSample - safeOnset) / juce::jmax(1.0e-6f, safeCeiling - safeOnset);
            const float saturated = safeOnset + ((safeCeiling - safeOnset) * std::tanh(normalized));
            return std::copysign(juce::jmin(saturated, safeCeiling), sample);
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
        layout.add(std::make_unique<juce::AudioParameterChoice>("Mode", "Mode",
            juce::StringArray { "Transparent", "Loud", "Safe" }, 0));
            
        layout.add(std::make_unique<juce::AudioParameterBool>("Bypass", "Bypass", false));

        return layout;
    }

    void MaximizerModule::prepareToPlay(double sampleRate, int samplesPerBlock)
    {
        currentSampleRate = sampleRate;
        oversampling.reset();
        oversampling.initProcessing((size_t) juce::jmax(1, samplesPerBlock));

        const auto oversamplingFactor = (double) oversampling.getOversamplingFactor();
        currentOversampledRate = currentSampleRate * oversamplingFactor;
        lookaheadSamples = juce::jmax(1, juce::roundToInt((currentSampleRate * lookaheadTimeMs) / 1000.0));
        oversampledLookaheadSamples = juce::jmax(1, juce::roundToInt((currentOversampledRate * lookaheadTimeMs) / 1000.0));

        const int bufferSize = oversampledLookaheadSamples + 1;
        delayBuffer.setSize(getTotalNumOutputChannels(), bufferSize);
        gainReductionBuffer.assign((size_t) bufferSize, 1.0f);
        previousDrivenSamples.assign((size_t) getTotalNumOutputChannels(), 0.0f);

        inputDriveSmoothed.reset(currentOversampledRate, parameterSmoothingMs * 0.001);
        ceilingGainSmoothed.reset(currentOversampledRate, parameterSmoothingMs * 0.001);

        resetLimiterState();
        updateParameters();

        inputDriveSmoothed.setCurrentAndTargetValue(inputDriveSmoothed.getTargetValue());
        ceilingGainSmoothed.setCurrentAndTargetValue(ceilingGainSmoothed.getTargetValue());

        const int oversamplingLatency = juce::roundToInt(oversampling.getLatencyInSamples());
        totalLatencySamples = lookaheadSamples + oversamplingLatency;
        bypassDelayBuffer.setSize(getTotalNumOutputChannels(), totalLatencySamples + 1);
        bypassDelayBuffer.clear();
        bypassDelayWritePosition = 0;
        setLatencySamples(totalLatencySamples);
    }

    void MaximizerModule::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
    {
        updateParameters();

        const int numChannels = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();
        if (numChannels == 0 || numSamples == 0)
            return;

        const bool bypassed = apvts.getRawParameterValue("Bypass")->load() > 0.5f;

        if (delayBuffer.getNumChannels() != numChannels || delayBuffer.getNumSamples() != oversampledLookaheadSamples + 1)
        {
            delayBuffer.setSize(numChannels, oversampledLookaheadSamples + 1);
            gainReductionBuffer.assign((size_t) (oversampledLookaheadSamples + 1), 1.0f);
            previousDrivenSamples.assign((size_t) numChannels, 0.0f);
            resetLimiterState();
        }

        if (bypassDelayBuffer.getNumChannels() != numChannels || bypassDelayBuffer.getNumSamples() != totalLatencySamples + 1)
        {
            bypassDelayBuffer.setSize(numChannels, totalLatencySamples + 1);
            bypassDelayBuffer.clear();
            bypassDelayWritePosition = 0;
        }

        processBypassDelay(buffer, bypassed);

        if (bypassed)
            return;

        juce::dsp::AudioBlock<float> outputBlock(buffer);
        auto oversampledBlock = oversampling.processSamplesUp(outputBlock);
        const int oversampledSamples = (int) oversampledBlock.getNumSamples();
        const auto modeTuning = getModeTuning(lastModeIndex);
        const float effectiveRelease = juce::jmax(1.0f, lastRelease * modeTuning.releaseScale);

        for (int sample = 0; sample < oversampledSamples; ++sample)
        {
            float detectorPeak = 0.0f;
            const float inputDrive = inputDriveSmoothed.getNextValue();
            const float ceilingGain = ceilingGainSmoothed.getNextValue();
            const float driveDb = juce::jmax(0.0f,
                                             juce::Decibels::gainToDecibels(juce::jmax(inputDrive, 1.0e-6f)));
            const float hybridClipHeadroomDb = juce::jlimit(0.35f, 2.6f,
                                                            computeHybridClipHeadroomDb(driveDb, effectiveRelease)
                                                              + modeTuning.clipHeadroomOffsetDb);
            const float hybridStageCeiling = ceilingGain * decibelsToGainSafe(hybridClipHeadroomDb);
            const float hybridClipOnset = juce::jmin(hybridStageCeiling - 1.0e-6f,
                                                     ceilingGain * decibelsToGainSafe(hybridClipHeadroomDb * modeTuning.clipOnsetFraction));

            for (int channel = 0; channel < numChannels; ++channel)
            {
                auto* oversampledChannel = oversampledBlock.getChannelPointer((size_t) channel);
                const float drivenSample = oversampledChannel[sample] * inputDrive;
                const float shapedSample = softSaturateToCeiling(drivenSample,
                                                                 hybridClipOnset,
                                                                 hybridStageCeiling);
                delayBuffer.setSample(channel, delayWritePosition, shapedSample);
                detectorPeak = juce::jmax(detectorPeak,
                                          estimateInterpolatedPeak(previousDrivenSamples[(size_t) channel], shapedSample));
                previousDrivenSamples[(size_t) channel] = shapedSample;
            }

            detectorPeak *= modeTuning.detectorBias;

            const float targetGain = (detectorPeak > ceilingGain && detectorPeak > 0.0f)
                                   ? (ceilingGain / detectorPeak)
                                   : 1.0f;
            gainReductionBuffer[(size_t) delayWritePosition] = juce::jlimit(0.0f, 1.0f, targetGain);

            const int readPosition = (delayWritePosition + 1) % delayBuffer.getNumSamples();

            float windowGain = 1.0f;
            for (const float bufferedGain : gainReductionBuffer)
                windowGain = juce::jmin(windowGain, bufferedGain);

            if (windowGain < currentGain)
                currentGain = windowGain;
            else
                currentGain = windowGain + (currentGain - windowGain)
                                          * computeAdaptiveReleaseCoefficient(currentOversampledRate,
                                                                              effectiveRelease,
                                                                              currentGain,
                                                                              windowGain,
                                                                              modeTuning.adaptiveReleaseMinScale);

            for (int channel = 0; channel < numChannels; ++channel)
            {
                float outputSample = delayBuffer.getSample(channel, readPosition) * currentGain;
                outputSample = softSaturateToCeiling(outputSample,
                                                    ceilingGain * modeTuning.finalSafetyOnset,
                                                    ceilingGain);
                if (!std::isfinite(outputSample))
                    outputSample = 0.0f;

                auto* oversampledChannel = oversampledBlock.getChannelPointer((size_t) channel);
                oversampledChannel[sample] = outputSample;
            }

            delayWritePosition = readPosition;
        }

        oversampling.processSamplesDown(outputBlock);

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
        const int mode = juce::jlimit(0, 2, juce::roundToInt(apvts.getRawParameterValue("Mode")->load()));

        if (std::abs(thresh - lastThreshold) <= 0.001f
            && std::abs(rel - lastRelease) <= 0.001f
            && std::abs(ceil - lastCeiling) <= 0.001f
            && mode == lastModeIndex)
            return;
	
        lastThreshold = thresh;
        lastRelease = rel;
        lastCeiling = ceil;
        lastModeIndex = mode;

        inputDriveSmoothed.setTargetValue(juce::Decibels::decibelsToGain(-thresh));
        ceilingGainSmoothed.setTargetValue(juce::Decibels::decibelsToGain(ceil));
        if (!std::isfinite(inputDriveSmoothed.getTargetValue()))
            inputDriveSmoothed.setCurrentAndTargetValue(1.0f);
        if (!std::isfinite(ceilingGainSmoothed.getTargetValue()))
            ceilingGainSmoothed.setCurrentAndTargetValue(1.0f);
    }

    void MaximizerModule::resetLimiterState()
    {
        oversampling.reset();
        delayBuffer.clear();
        std::fill(gainReductionBuffer.begin(), gainReductionBuffer.end(), 1.0f);
        std::fill(previousDrivenSamples.begin(), previousDrivenSamples.end(), 0.0f);
        currentGain = 1.0f;
        delayWritePosition = 0;
    }

    void MaximizerModule::processBypassDelay(juce::AudioBuffer<float>& buffer, bool writeToOutput)
    {
        const int numChannels = juce::jmin(buffer.getNumChannels(), bypassDelayBuffer.getNumChannels());
        const int numSamples = buffer.getNumSamples();
        if (numChannels == 0 || numSamples == 0 || bypassDelayBuffer.getNumSamples() <= 1)
            return;

        std::array<const float*, maxSupportedChannels> inputPointers {};
        std::array<float*, maxSupportedChannels> outputPointers {};
        std::array<float*, maxSupportedChannels> delayPointers {};

        for (int channel = 0; channel < numChannels; ++channel)
        {
            inputPointers[(size_t) channel] = buffer.getReadPointer(channel);
            outputPointers[(size_t) channel] = writeToOutput ? buffer.getWritePointer(channel) : nullptr;
            delayPointers[(size_t) channel] = bypassDelayBuffer.getWritePointer(channel);
        }

        const int delayBufferSize = bypassDelayBuffer.getNumSamples();
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const int readPosition = (bypassDelayWritePosition + 1) % delayBufferSize;

            for (int channel = 0; channel < numChannels; ++channel)
            {
                auto* delayData = delayPointers[(size_t) channel];
                delayData[bypassDelayWritePosition] = inputPointers[(size_t) channel][sample];

                if (writeToOutput)
                    outputPointers[(size_t) channel][sample] = delayData[readPosition];
            }

            bypassDelayWritePosition = readPosition;
        }
    }

    juce::AudioProcessorEditor* MaximizerModule::createEditor()
    {
        return new MaximizerEditor(*this, apvts);
    }

}
