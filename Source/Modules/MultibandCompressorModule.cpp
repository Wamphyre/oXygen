#include "MultibandCompressorModule.h"
#include "../GUI/MultibandCompressorEditor.h"
#include <cmath>
#include <limits>

namespace oxygen
{
    namespace
    {
        constexpr float minCrossoverSpacingHz = 20.0f;

        float computeTimeCoefficient(double sampleRate, float timeMs)
        {
            if (sampleRate <= 0.0 || timeMs <= 0.0f)
                return 0.0f;

            return std::exp(-1.0f / (0.001f * timeMs * (float) sampleRate));
        }

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

    MultibandCompressorModule::MultibandCompressorModule()
        : MasteringModule("Multiband Comp"),
          apvts(*this, nullptr, "Parameters", createParameterLayout())
    {
        crossoverLowMid.setType(juce::dsp::LinkwitzRileyFilter<float>::Type::lowpass);
        crossoverMidHigh.setType(juce::dsp::LinkwitzRileyFilter<float>::Type::lowpass);
        crossoverHigh.setType(juce::dsp::LinkwitzRileyFilter<float>::Type::lowpass);

        for (auto& params : lastBandParams)
            params = { std::numeric_limits<float>::quiet_NaN(),
                       std::numeric_limits<float>::quiet_NaN(),
                       std::numeric_limits<float>::quiet_NaN(),
                       std::numeric_limits<float>::quiet_NaN() };

        for (auto& runtimeState : bandRuntimeStates)
            runtimeState = {};
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
        layout.add(std::make_unique<juce::AudioParameterBool>("Bypass", "Bypass", false));

        return layout;
    }

    void MultibandCompressorModule::prepareToPlay(double sampleRate, int samplesPerBlock)
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
        
        // Prepare buffers
        bufferLow.setSize(spec.numChannels, samplesPerBlock);
        bufferLowMid.setSize(spec.numChannels, samplesPerBlock);
        bufferHighMid.setSize(spec.numChannels, samplesPerBlock);
        bufferHigh.setSize(spec.numChannels, samplesPerBlock);

        for (auto& runtimeState : bandRuntimeStates)
            runtimeState.envelope = 0.0f;

        lastSampleRate = 0.0;
        
        updateParameters();
    }

    void MultibandCompressorModule::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
    {
        juce::ignoreUnused(midiMessages);

        if (apvts.getRawParameterValue("Bypass")->load() > 0.5f)
            return;

        updateParameters();
        
        const int numChannels = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();
        if (numChannels == 0 || numSamples == 0)
            return;
        
        // Ensure buffers are large enough (redundant if prepare called correctly, but safe)
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
        
        // Stereo-linked dynamics keeps the stereo image stable on asymmetric material.
        processStereoLinkedBand(bufferLow, bandRuntimeStates[0]);
        processStereoLinkedBand(bufferLowMid, bandRuntimeStates[1]);
        processStereoLinkedBand(bufferHighMid, bandRuntimeStates[2]);
        processStereoLinkedBand(bufferHigh, bandRuntimeStates[3]);
        
        // Apply Gains and Sum back to Output
        // Note: dsp::Compressor doesn't apply makeup gain automatically in this version?
        // We will apply the gain parameter manually.
        
        buffer.clear();
        
        const float gainLow = juce::Decibels::decibelsToGain(apvts.getRawParameterValue("LowGain")->load());
        const float gainLowMid = juce::Decibels::decibelsToGain(apvts.getRawParameterValue("LowMidGain")->load());
        const float gainHighMid = juce::Decibels::decibelsToGain(apvts.getRawParameterValue("HighMidGain")->load());
        const float gainHigh = juce::Decibels::decibelsToGain(apvts.getRawParameterValue("HighGain")->load());

        for (int ch = 0; ch < numChannels; ++ch)
        {
            buffer.addFrom(ch, 0, bufferLow, ch, 0, numSamples, gainLow);
            buffer.addFrom(ch, 0, bufferLowMid, ch, 0, numSamples, gainLowMid);
            buffer.addFrom(ch, 0, bufferHighMid, ch, 0, numSamples, gainHighMid);
            buffer.addFrom(ch, 0, bufferHigh, ch, 0, numSamples, gainHigh);

            auto* out = buffer.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i)
            {
                if (!std::isfinite(out[i]))
                    out[i] = 0.0f;
            }
        }
    }
    
    void MultibandCompressorModule::updateParameters()
    {
        const auto sampleRate = getSampleRate();
        if (sampleRate <= 0.0)
            return;

        const auto crossovers = sanitizeCrossovers((float) sampleRate,
                                                   apvts.getRawParameterValue("LowMidX")->load(),
                                                   apvts.getRawParameterValue("MidHighX")->load(),
                                                   apvts.getRawParameterValue("HighX")->load());

        if (std::abs(crossovers.lowMid - lastLowMidX) > 0.001f)
        {
            crossoverLowMid.setCutoffFrequency(crossovers.lowMid);
            lastLowMidX = crossovers.lowMid;
        }

        if (std::abs(crossovers.midHigh - lastMidHighX) > 0.001f)
        {
            crossoverMidHigh.setCutoffFrequency(crossovers.midHigh);
            lastMidHighX = crossovers.midHigh;
        }

        if (std::abs(crossovers.high - lastHighX) > 0.001f)
        {
            crossoverHigh.setCutoffFrequency(crossovers.high);
            lastHighX = crossovers.high;
        }
        
        const bool sampleRateChanged = std::abs(sampleRate - lastSampleRate) > 1.0e-6;
        const char* bandNames[] = { "Low", "LowMid", "HighMid", "High" };

        for (int i = 0; i < 4; ++i)
        {
            juce::String prefix = bandNames[i];
            const float t = juce::jlimit(-60.0f, 0.0f, apvts.getRawParameterValue(prefix + "Thresh")->load());
            const float r = juce::jmax(1.0f, apvts.getRawParameterValue(prefix + "Ratio")->load());
            const float a = juce::jlimit(0.1f, 100.0f, apvts.getRawParameterValue(prefix + "Attack")->load());
            const float rl = juce::jlimit(10.0f, 1000.0f, apvts.getRawParameterValue(prefix + "Release")->load());

            if (sampleRateChanged
                || std::abs(t - lastBandParams[i].thresh) > 0.001f
                || std::abs(r - lastBandParams[i].ratio) > 0.001f
                || std::abs(a - lastBandParams[i].attack) > 0.001f
                || std::abs(rl - lastBandParams[i].release) > 0.001f)
            {
                lastBandParams[i] = { t, r, a, rl };
                bandRuntimeStates[i].thresholdGain = juce::Decibels::decibelsToGain(t, -200.0f);
                bandRuntimeStates[i].ratioInverse = 1.0f / r;
                bandRuntimeStates[i].attackCoeff = computeTimeCoefficient(sampleRate, a);
                bandRuntimeStates[i].releaseCoeff = computeTimeCoefficient(sampleRate, rl);
            }
        }

        lastSampleRate = sampleRate;
    }

    void MultibandCompressorModule::processStereoLinkedBand(juce::AudioBuffer<float>& bandBuffer, BandRuntimeState& state)
    {
        const int numChannels = bandBuffer.getNumChannels();
        const int numSamples = bandBuffer.getNumSamples();
        if (numChannels == 0 || numSamples == 0)
            return;

        for (int sample = 0; sample < numSamples; ++sample)
        {
            float detector = 0.0f;
            for (int channel = 0; channel < numChannels; ++channel)
                detector = juce::jmax(detector, std::abs(bandBuffer.getReadPointer(channel)[sample]));

            const float coefficient = (detector > state.envelope) ? state.attackCoeff : state.releaseCoeff;
            state.envelope = coefficient * state.envelope + (1.0f - coefficient) * detector;

            float gain = 1.0f;
            if (state.envelope > state.thresholdGain && state.thresholdGain > 0.0f)
                gain = std::pow(state.envelope / state.thresholdGain, state.ratioInverse - 1.0f);

            if (!std::isfinite(gain))
                gain = 1.0f;

            for (int channel = 0; channel < numChannels; ++channel)
            {
                auto* data = bandBuffer.getWritePointer(channel);
                data[sample] *= gain;
            }
        }
    }

    juce::AudioProcessorEditor* MultibandCompressorModule::createEditor()
    {
        return new MultibandCompressorEditor(*this, apvts);
    }

}
