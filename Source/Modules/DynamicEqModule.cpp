#include "DynamicEqModule.h"
#include "../GUI/DynamicEqEditor.h"
#include <cmath>

namespace oxygen
{
    namespace
    {
        constexpr float softKneeWidthDb = 3.5f;
        constexpr float dynamicResponseRatio = 1.35f;
        constexpr std::array<float, DynamicEqModule::NumBands> defaultThresholds { -26.0f, -28.0f, -30.0f, -32.0f };
        constexpr std::array<float, DynamicEqModule::NumBands> attackTimesMs { 16.0f, 11.0f, 4.5f, 2.2f };
        constexpr std::array<float, DynamicEqModule::NumBands> releaseTimesMs { 170.0f, 130.0f, 90.0f, 65.0f };

        float computeTimeCoefficient(double sampleRate, float timeMs)
        {
            if (sampleRate <= 0.0 || timeMs <= 0.0f)
                return 0.0f;

            return std::exp(-1.0f / (0.001f * timeMs * (float) sampleRate));
        }
    }

    DynamicEqModule::DynamicEqModule()
        : MasteringModule("Dynamic EQ"),
          apvts(*this, nullptr, "Parameters", createParameterLayout())
    {
        for (int band = 0; band < NumBands; ++band)
        {
            isolationFilters[(size_t) band] = std::make_unique<Filter>();
            dynamicFilters[(size_t) band] = std::make_unique<Filter>();
            lastThresholds[(size_t) band] = std::numeric_limits<float>::quiet_NaN();
            lastRanges[(size_t) band] = std::numeric_limits<float>::quiet_NaN();
            lastAppliedCuts[(size_t) band] = std::numeric_limits<float>::quiet_NaN();
            runtimeStates[(size_t) band].thresholdDb = defaultThresholds[(size_t) band];
        }
    }

    DynamicEqModule::~DynamicEqModule() = default;

    juce::AudioProcessorValueTreeState::ParameterLayout DynamicEqModule::createParameterLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        layout.add(std::make_unique<juce::AudioParameterBool>("Bypass", "Bypass", false));

        const char* bandNames[] = { "Low", "LowMid", "HighMid", "High" };
        for (int band = 0; band < NumBands; ++band)
        {
            const juce::String prefix = bandNames[band];
            layout.add(std::make_unique<juce::AudioParameterFloat>(
                prefix + "Thresh",
                prefix + " Threshold",
                juce::NormalisableRange<float>(-48.0f, -6.0f, 0.1f),
                defaultThresholds[(size_t) band]));
            layout.add(std::make_unique<juce::AudioParameterFloat>(
                prefix + "Range",
                prefix + " Range",
                juce::NormalisableRange<float>(0.0f, 12.0f, 0.1f),
                0.0f));
        }

        return layout;
    }

    void DynamicEqModule::prepareToPlay(double sampleRate, int samplesPerBlock)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = (juce::uint32) juce::jmax(1, samplesPerBlock);
        spec.numChannels = (juce::uint32) juce::jmax(1, getTotalNumOutputChannels());

        const float maxFreq = (float) sampleRate * 0.45f;
        for (int band = 0; band < NumBands; ++band)
        {
            auto& detectorFilter = isolationFilters[(size_t) band];
            detectorFilter->prepare(spec);
            detectorFilter->reset();

            auto& dynamicFilter = dynamicFilters[(size_t) band];
            dynamicFilter->prepare(spec);
            dynamicFilter->reset();

            const float safeFreq = juce::jlimit(20.0f, maxFreq, CenterFrequencies[(size_t) band]);
            auto detectorCoeffs = juce::dsp::IIR::Coefficients<float>::makeBandPass(sampleRate,
                                                                                     safeFreq,
                                                                                     BandQValues[(size_t) band]);
            auto dynamicCoeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter(sampleRate,
                                                                                      safeFreq,
                                                                                      BandQValues[(size_t) band],
                                                                                      1.0f);
            *detectorFilter->state = *detectorCoeffs;
            *dynamicFilter->state = *dynamicCoeffs;

            bandBuffers[(size_t) band].setSize((int) spec.numChannels, samplesPerBlock, false, false, true);
            runtimeStates[(size_t) band].attackCoeff = computeTimeCoefficient(sampleRate, attackTimesMs[(size_t) band]);
            runtimeStates[(size_t) band].releaseCoeff = computeTimeCoefficient(sampleRate, releaseTimesMs[(size_t) band]);
            runtimeStates[(size_t) band].envelope = 0.0f;
            runtimeStates[(size_t) band].currentGainReductionDb = 0.0f;
        }

        dryInputBuffer.setSize((int) spec.numChannels, samplesPerBlock, false, false, true);
        lastSampleRate = 0.0;
        updateFiltersAndParameters();
    }

    void DynamicEqModule::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
    {
        if (apvts.getRawParameterValue("Bypass")->load() > 0.5f)
            return;

        const int numChannels = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();
        if (numChannels == 0 || numSamples == 0)
            return;

        updateFiltersAndParameters();

        if (dryInputBuffer.getNumChannels() != numChannels || dryInputBuffer.getNumSamples() < numSamples)
            dryInputBuffer.setSize(numChannels, numSamples, false, false, true);

        dryInputBuffer.makeCopyOf(buffer, true);
        const auto sampleRate = getSampleRate();
        const float maxFreq = (float) sampleRate * 0.45f;

        for (int band = 0; band < NumBands; ++band)
        {
            auto& state = runtimeStates[(size_t) band];
            auto& bandBuffer = bandBuffers[(size_t) band];
            if (bandBuffer.getNumChannels() != numChannels || bandBuffer.getNumSamples() < numSamples)
                bandBuffer.setSize(numChannels, numSamples, false, false, true);

            bandBuffer.makeCopyOf(dryInputBuffer, true);

            juce::dsp::AudioBlock<float> block(bandBuffer);
            juce::dsp::ProcessContextReplacing<float> context(block);
            isolationFilters[(size_t) band]->process(context);

            const float targetCutDb = (state.maxCutDb > 0.001f)
                ? analyseDynamicBand(bandBuffer, state)
                : 0.0f;

            if (!std::isfinite(lastAppliedCuts[(size_t) band])
                || std::abs(targetCutDb - lastAppliedCuts[(size_t) band]) > 0.05f)
            {
                const float safeFreq = juce::jlimit(20.0f, maxFreq, CenterFrequencies[(size_t) band]);
                auto dynamicCoeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter(sampleRate,
                                                                                          safeFreq,
                                                                                          BandQValues[(size_t) band],
                                                                                          juce::Decibels::decibelsToGain(-targetCutDb));
                *dynamicFilters[(size_t) band]->state = *dynamicCoeffs;
                lastAppliedCuts[(size_t) band] = targetCutDb;
            }

            if (targetCutDb > 0.001f)
            {
                juce::dsp::AudioBlock<float> mainBlock(buffer);
                juce::dsp::ProcessContextReplacing<float> mainContext(mainBlock);
                dynamicFilters[(size_t) band]->process(mainContext);
            }
        }

        for (int channel = 0; channel < numChannels; ++channel)
        {
            auto* channelData = buffer.getWritePointer(channel);
            for (int sample = 0; sample < numSamples; ++sample)
            {
                if (!std::isfinite(channelData[sample]))
                    channelData[sample] = 0.0f;
            }
        }
    }

    void DynamicEqModule::updateFiltersAndParameters()
    {
        const double sampleRate = getSampleRate();
        if (sampleRate <= 0.0)
            return;

        const bool sampleRateChanged = std::abs(sampleRate - lastSampleRate) > 1.0e-6;
        const float maxFreq = (float) sampleRate * 0.45f;
        const char* bandNames[] = { "Low", "LowMid", "HighMid", "High" };

        for (int band = 0; band < NumBands; ++band)
        {
            if (sampleRateChanged)
            {
                const float safeFreq = juce::jlimit(20.0f, maxFreq, CenterFrequencies[(size_t) band]);
                auto detectorCoeffs = juce::dsp::IIR::Coefficients<float>::makeBandPass(sampleRate,
                                                                                         safeFreq,
                                                                                         BandQValues[(size_t) band]);
                auto dynamicCoeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter(sampleRate,
                                                                                          safeFreq,
                                                                                          BandQValues[(size_t) band],
                                                                                          juce::Decibels::decibelsToGain(
                                                                                              -runtimeStates[(size_t) band].currentGainReductionDb));
                *isolationFilters[(size_t) band]->state = *detectorCoeffs;
                *dynamicFilters[(size_t) band]->state = *dynamicCoeffs;
                lastAppliedCuts[(size_t) band] = runtimeStates[(size_t) band].currentGainReductionDb;
                runtimeStates[(size_t) band].attackCoeff = computeTimeCoefficient(sampleRate, attackTimesMs[(size_t) band]);
                runtimeStates[(size_t) band].releaseCoeff = computeTimeCoefficient(sampleRate, releaseTimesMs[(size_t) band]);
            }

            const juce::String prefix = bandNames[band];
            const float thresholdDb = juce::jlimit(-48.0f, -6.0f,
                                                   apvts.getRawParameterValue(prefix + "Thresh")->load());
            const float rangeDb = juce::jlimit(0.0f, 12.0f,
                                               apvts.getRawParameterValue(prefix + "Range")->load());

            if (sampleRateChanged
                || std::abs(thresholdDb - lastThresholds[(size_t) band]) > 0.001f
                || std::abs(rangeDb - lastRanges[(size_t) band]) > 0.001f)
            {
                runtimeStates[(size_t) band].thresholdDb = thresholdDb;
                runtimeStates[(size_t) band].maxCutDb = rangeDb;
                lastThresholds[(size_t) band] = thresholdDb;
                lastRanges[(size_t) band] = rangeDb;
            }
        }

        lastSampleRate = sampleRate;
    }

    float DynamicEqModule::analyseDynamicBand(const juce::AudioBuffer<float>& bandBuffer, BandRuntimeState& state)
    {
        const int numChannels = bandBuffer.getNumChannels();
        const int numSamples = bandBuffer.getNumSamples();
        if (numChannels == 0 || numSamples == 0 || state.maxCutDb <= 0.001f)
            return 0.0f;

        float peakReductionDb = 0.0f;
        float lastReductionDb = 0.0f;

        for (int sample = 0; sample < numSamples; ++sample)
        {
            float peakDetector = 0.0f;
            double sumSquares = 0.0;
            for (int channel = 0; channel < numChannels; ++channel)
            {
                const float value = bandBuffer.getReadPointer(channel)[sample];
                peakDetector = juce::jmax(peakDetector, std::abs(value));
                sumSquares += (double) value * (double) value;
            }

            const float rmsDetector = std::sqrt((float) (sumSquares / (double) numChannels));
            const float detector = juce::jmax(peakDetector * 0.82f, rmsDetector * 1.20f);

            const float coefficient = (detector > state.envelope) ? state.attackCoeff : state.releaseCoeff;
            state.envelope = coefficient * state.envelope + (1.0f - coefficient) * detector;

            float gainReductionDb = 0.0f;
            if (state.envelope > 0.0f)
            {
                const float envelopeDb = juce::Decibels::gainToDecibels(state.envelope, -160.0f);
                const float overDb = envelopeDb - state.thresholdDb;

                if (2.0f * overDb <= -softKneeWidthDb)
                {
                    gainReductionDb = 0.0f;
                }
                else if (2.0f * std::abs(overDb) < softKneeWidthDb)
                {
                    const float kneeDelta = overDb + (softKneeWidthDb * 0.5f);
                    gainReductionDb = (kneeDelta * kneeDelta) / (2.0f * softKneeWidthDb);
                }
                else if (overDb > 0.0f)
                {
                    gainReductionDb = overDb;
                }
            }

            gainReductionDb = juce::jmin(state.maxCutDb, gainReductionDb * dynamicResponseRatio);
            peakReductionDb = juce::jmax(peakReductionDb, gainReductionDb);
            lastReductionDb = gainReductionDb;
        }

        const float weightedTargetDb = juce::jlimit(0.0f,
                                                    state.maxCutDb,
                                                    (0.56f * lastReductionDb) + (0.44f * peakReductionDb));
        const float blockSmoothing = (weightedTargetDb > state.currentGainReductionDb) ? 0.58f : 0.24f;
        state.currentGainReductionDb += (weightedTargetDb - state.currentGainReductionDb) * blockSmoothing;
        state.currentGainReductionDb = juce::jlimit(0.0f, state.maxCutDb, state.currentGainReductionDb);
        return state.currentGainReductionDb;
    }

    juce::AudioProcessorEditor* DynamicEqModule::createEditor()
    {
        return new DynamicEqEditor(*this, apvts);
    }
}
