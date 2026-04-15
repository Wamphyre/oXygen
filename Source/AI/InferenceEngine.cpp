#include "InferenceEngine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <tuple>
#include <vector>

// Note: Uncomment these when ONNX Runtime is linked
// #include <onnxruntime_cxx_api.h>

namespace oxygen
{
    namespace
    {
        constexpr int eqBandCount = MasteringParameters::eqBandCount;
        constexpr int dynamicEqBandCount = MasteringParameters::dynamicEqBandCount;
        constexpr float assistantEqHardLimitDb = 7.5f;
        constexpr std::array<float, eqBandCount> eqBandCenters = {
            30.0f, 40.0f, 60.0f, 100.0f,
            180.0f, 300.0f, 500.0f, 900.0f,
            1500.0f, 2500.0f, 4000.0f, 6000.0f,
            10000.0f, 15000.0f, 20000.0f
        };
        constexpr std::array<int, dynamicEqBandCount> dynamicEqMacroBands { 0, 1, 2, 3 };
        constexpr int transparentMaximizerMode = 0;
        constexpr int loudMaximizerMode = 1;
        constexpr int safeMaximizerMode = 2;

        struct AnalysisFeatures
        {
            float peakDb = -100.0f;
            float truePeakDb = -100.0f;
            float rmsDb = -100.0f;
            float gatedRmsDb = -100.0f;
            float integratedLufs = -100.0f;
            float shortTermLufs = -100.0f;
            float crestDb = 0.0f;
            float transientMotionDb = 0.0f;
            float lowVsMidDb = 0.0f;
            float highVsMidDb = 0.0f;
            float stereoCorrelation = 1.0f;
            float sideRatio = 0.0f;
            std::array<float, eqBandCount> eqBandProfileDb {};
            std::array<float, eqBandCount> eqDeviationDb {};
            std::array<float, eqBandCount> eqExcessPersistence {};
            std::array<float, eqBandCount> eqDeficitPersistence {};
            std::array<float, 4> bandPeakDb {};
            std::array<float, 4> bandRmsDb {};
            std::array<float, 4> bandCrestDb {};
            std::array<float, 4> bandCorrelation {};
            std::array<float, 4> bandSideRatio {};
            std::array<float, 4> bandTransientMotionDb {};
            bool valid = false;
        };

        struct ReferenceMatchProfile
        {
            float tonalConfidence = 1.0f;
            float glueConfidence = 1.0f;
            float transientConfidence = 1.0f;
            float widthConfidence = 1.0f;
            float loudnessConfidence = 1.0f;
            float overallConfidence = 1.0f;
            float toneWeight = 1.0f;
            float glueWeight = 1.0f;
            float transientWeight = 1.0f;
            float widthWeight = 1.0f;
            float loudnessWeight = 1.0f;
            float lowEndSafety = 1.0f;
        };

        float gainToDbSafe(float gain)
        {
            return juce::Decibels::gainToDecibels(juce::jmax(gain, 1.0e-9f));
        }

        float meanSquareToDbSafe(double meanSquare)
        {
            return 10.0f * std::log10((float) juce::jmax(meanSquare, 1.0e-12));
        }

        float loudnessMeanSquareToLufs(double meanSquare)
        {
            return -0.691f + meanSquareToDbSafe(meanSquare);
        }

        std::pair<float, float> measureProgramLoudness(const juce::AudioBuffer<float>& recentAudio,
                                                       int numChannels,
                                                       int numSamples,
                                                       double sampleRate)
        {
            using Filter = juce::dsp::IIR::Filter<double>;
            using Coefficients = juce::dsp::IIR::Coefficients<double>;

            std::array<Filter, 2> highPassFilters;
            std::array<Filter, 2> highShelfFilters;

            const auto highPassCoefficients = Coefficients::makeHighPass(sampleRate, 38.0, 0.5);
            const auto highShelfCoefficients = Coefficients::makeHighShelf(sampleRate,
                                                                           1500.0,
                                                                           0.70710678,
                                                                           juce::Decibels::decibelsToGain(4.0));

            std::vector<double> weightedEnergy((size_t) numSamples, 0.0);

            for (int channel = 0; channel < numChannels; ++channel)
            {
                highPassFilters[(size_t) channel].coefficients = highPassCoefficients;
                highShelfFilters[(size_t) channel].coefficients = highShelfCoefficients;
                highPassFilters[(size_t) channel].reset();
                highShelfFilters[(size_t) channel].reset();

                const auto* input = recentAudio.getReadPointer(channel);
                for (int sample = 0; sample < numSamples; ++sample)
                {
                    const double highPassed = highPassFilters[(size_t) channel].processSample((double) input[sample]);
                    const double weightedSample = highShelfFilters[(size_t) channel].processSample(highPassed);
                    weightedEnergy[(size_t) sample] += weightedSample * weightedSample;
                }
            }

            std::vector<double> prefixEnergy((size_t) numSamples + 1, 0.0);
            for (int sample = 0; sample < numSamples; ++sample)
                prefixEnergy[(size_t) sample + 1] = prefixEnergy[(size_t) sample] + weightedEnergy[(size_t) sample];

            const int integratedWindow = juce::jmax(1, juce::roundToInt(sampleRate * 0.4));
            const int integratedStep = juce::jmax(1, juce::roundToInt(sampleRate * 0.1));
            const int shortTermWindow = juce::jmax(integratedWindow, juce::roundToInt(sampleRate * 3.0));

            std::vector<double> gatedMeanSquares;
            float shortTermLufs = -100.0f;

            for (int start = 0; start < numSamples; start += integratedStep)
            {
                const int end = juce::jmin(numSamples, start + integratedWindow);
                const int blockSize = end - start;
                if (blockSize <= 0)
                    break;

                const double blockEnergy = prefixEnergy[(size_t) end] - prefixEnergy[(size_t) start];
                const double blockMeanSquare = blockEnergy / (double) blockSize;
                const float blockLufs = loudnessMeanSquareToLufs(blockMeanSquare);

                if (blockLufs > -70.0f)
                    gatedMeanSquares.push_back(blockMeanSquare);

                const int shortTermEnd = juce::jmin(numSamples, start + shortTermWindow);
                const int shortTermSize = shortTermEnd - start;
                if (shortTermSize > 0)
                {
                    const double shortTermEnergy = prefixEnergy[(size_t) shortTermEnd] - prefixEnergy[(size_t) start];
                    shortTermLufs = juce::jmax(shortTermLufs,
                                               loudnessMeanSquareToLufs(shortTermEnergy / (double) shortTermSize));
                }
            }

            float integratedLufs = -100.0f;
            if (!gatedMeanSquares.empty())
            {
                const double ungatedMean = std::accumulate(gatedMeanSquares.begin(), gatedMeanSquares.end(), 0.0)
                                         / (double) gatedMeanSquares.size();
                const float relativeGateLufs = loudnessMeanSquareToLufs(ungatedMean) - 10.0f;

                double integratedMean = 0.0;
                int integratedCount = 0;

                for (const double meanSquare : gatedMeanSquares)
                {
                    if (loudnessMeanSquareToLufs(meanSquare) >= relativeGateLufs)
                    {
                        integratedMean += meanSquare;
                        ++integratedCount;
                    }
                }

                if (integratedCount > 0)
                    integratedLufs = loudnessMeanSquareToLufs(integratedMean / (double) integratedCount);
                else
                    integratedLufs = loudnessMeanSquareToLufs(ungatedMean);
            }

            return { integratedLufs, shortTermLufs };
        }

        float measureStrictTruePeak(const juce::AudioBuffer<float>& recentAudio, int numChannels, int numSamples)
        {
            constexpr int oversamplingFactor = 4;
            float peak = 0.0f;
            std::vector<float> oversampled((size_t) numSamples * oversamplingFactor + 32, 0.0f);

            for (int channel = 0; channel < numChannels; ++channel)
            {
                juce::WindowedSincInterpolator interpolator;
                interpolator.reset();

                const auto* input = recentAudio.getReadPointer(channel);
                for (int sample = 0; sample < numSamples; ++sample)
                    peak = juce::jmax(peak, std::abs(input[sample]));

                const int oversampledSamples = numSamples * oversamplingFactor;
                juce::FloatVectorOperations::clear(oversampled.data(), oversampledSamples);
                interpolator.process(1.0 / (double) oversamplingFactor,
                                     input,
                                     oversampled.data(),
                                     oversampledSamples,
                                     numSamples,
                                     0);

                for (int sample = 0; sample < oversampledSamples; ++sample)
                    peak = juce::jmax(peak, std::abs(oversampled[(size_t) sample]));
            }

            return peak;
        }

        std::array<float, eqBandCount + 1> makeEqBandEdges(float maxFreq)
        {
            std::array<float, eqBandCount + 1> edges {};
            edges[0] = 20.0f;

            for (int i = 1; i < eqBandCount; ++i)
                edges[(size_t) i] = std::sqrt(eqBandCenters[(size_t) (i - 1)] * eqBandCenters[(size_t) i]);

            edges[(size_t) eqBandCount] = juce::jmax(edges[(size_t) eqBandCount - 1] + 1.0f, maxFreq);
            return edges;
        }

        std::vector<int> makeFrameStarts(int numSamples, int fftSize, int hopSize)
        {
            std::vector<int> starts;

            if (numSamples <= fftSize)
            {
                starts.push_back(0);
                return starts;
            }

            for (int start = 0; start <= numSamples - fftSize; start += hopSize)
                starts.push_back(start);

            const int lastStart = numSamples - fftSize;
            if (starts.empty() || starts.back() != lastStart)
                starts.push_back(lastStart);

            return starts;
        }

        std::array<float, eqBandCount> smoothBandProfile(std::array<float, eqBandCount> profile)
        {
            for (int pass = 0; pass < 3; ++pass)
            {
                const auto previous = profile;
                for (int band = 0; band < eqBandCount; ++band)
                {
                    if (band == 0)
                        profile[(size_t) band] = 0.75f * previous[(size_t) band] + 0.25f * previous[1];
                    else if (band == eqBandCount - 1)
                        profile[(size_t) band] = 0.75f * previous[(size_t) band] + 0.25f * previous[(size_t) band - 1];
                    else
                        profile[(size_t) band] = 0.2f * previous[(size_t) band - 1]
                                               + 0.6f * previous[(size_t) band]
                                               + 0.2f * previous[(size_t) band + 1];
                }
            }

            return profile;
        }

        std::array<float, eqBandCount> smoothEqCurve(const std::array<float, eqBandCount>& curve)
        {
            auto smoothed = curve;

            for (int pass = 0; pass < 2; ++pass)
            {
                const auto previous = smoothed;
                for (int band = 0; band < eqBandCount; ++band)
                {
                    if (band == 0)
                        smoothed[(size_t) band] = (0.78f * previous[(size_t) band]) + (0.22f * previous[1]);
                    else if (band == eqBandCount - 1)
                        smoothed[(size_t) band] = (0.78f * previous[(size_t) band]) + (0.22f * previous[(size_t) band - 1]);
                    else
                        smoothed[(size_t) band] = (0.18f * previous[(size_t) band - 1])
                                                + (0.64f * previous[(size_t) band])
                                                + (0.18f * previous[(size_t) band + 1]);
                }
            }

            return smoothed;
        }

        float clampEqGain(float gainDb)
        {
            return juce::jlimit(-assistantEqHardLimitDb, assistantEqHardLimitDb, gainDb);
        }

        float clampDynamicEqThreshold(float thresholdDb)
        {
            return juce::jlimit(-48.0f, -6.0f, thresholdDb);
        }

        float clampDynamicEqRange(float rangeDb)
        {
            return juce::jlimit(0.0f, 12.0f, rangeDb);
        }

        void addEqGain(MasteringParameters& params, int bandIndex, float deltaDb)
        {
            if (bandIndex < 0 || bandIndex >= eqBandCount)
                return;

            auto& gain = params.eqBandGains[(size_t) bandIndex];
            gain = clampEqGain(gain + deltaDb);
        }

        void setDynamicEqBand(MasteringParameters& params, int bandIndex, float thresholdDb, float rangeDb)
        {
            if (bandIndex < 0 || bandIndex >= dynamicEqBandCount)
                return;

            params.dynamicEqThresholds[(size_t) bandIndex] = clampDynamicEqThreshold(thresholdDb);
            params.dynamicEqRanges[(size_t) bandIndex] = clampDynamicEqRange(rangeDb);
        }

        float deriveDynamicEqThreshold(float bandRmsDb, float rangeDb, float transientMotionDb, float offsetDb)
        {
            const float transientOffsetDb = juce::jlimit(-1.2f, 1.6f, 1.0f - (0.32f * transientMotionDb));
            return clampDynamicEqThreshold(bandRmsDb + 5.6f - (0.62f * rangeDb) + transientOffsetDb + offsetDb);
        }

        float computeBandControl(float crestDb, float extraBias)
        {
            return juce::jlimit(0.0f, 1.0f, ((crestDb - 7.0f) / 7.0f) + extraBias);
        }

        float pullTowards(float value, float target, float amount)
        {
            return value + ((target - value) * amount);
        }

        int mapEqBandToMacroBand(int band)
        {
            if (band <= 3)
                return 0;

            if (band <= 7)
                return 1;

            if (band <= 11)
                return 2;

            return 3;
        }

        float getProgramLoudnessDb(const AnalysisFeatures& features)
        {
            return (features.integratedLufs > -99.0f)
                 ? features.integratedLufs
                 : features.gatedRmsDb;
        }

        float computeWeightedPositiveEqBoostDb(const std::array<float, eqBandCount>& eqBandGains)
        {
            float weightedBoost = 0.0f;
            float weightTotal = 0.0f;

            for (int band = 0; band < eqBandCount; ++band)
            {
                const float bandWeight = (band >= 4 && band <= 11) ? 1.0f
                                       : (band >= 2 && band <= 12) ? 0.86f
                                       : 0.72f;
                weightedBoost += juce::jmax(0.0f, eqBandGains[(size_t) band]) * bandWeight;
                weightTotal += bandWeight;
            }

            return (weightTotal > 0.0f) ? (weightedBoost / weightTotal) : 0.0f;
        }

        float computeCompressionDensityScore(const MasteringParameters& params)
        {
            const float lowDensity = juce::jmax(0.0f, params.lowRatio - 1.55f)
                                   + juce::jmax(0.0f, -params.lowThresh - 16.0f) * 0.12f;
            const float lowMidDensity = juce::jmax(0.0f, params.lowMidRatio - 1.45f)
                                      + juce::jmax(0.0f, -params.lowMidThresh - 15.0f) * 0.12f;
            const float highMidDensity = juce::jmax(0.0f, params.highMidRatio - 1.35f)
                                       + juce::jmax(0.0f, -params.highMidThresh - 14.0f) * 0.10f;
            const float highDensity = juce::jmax(0.0f, params.highRatio - 1.30f)
                                    + juce::jmax(0.0f, -params.highThresh - 13.0f) * 0.10f;

            return (lowDensity * 0.30f)
                 + (lowMidDensity * 0.28f)
                 + (highMidDensity * 0.24f)
                 + (highDensity * 0.18f);
        }

        float estimateReferenceSaturationRisk(const MasteringParameters& params,
                                              const AnalysisFeatures& sourceFeatures,
                                              float sourceProgramLoudnessDb,
                                              float targetProgramLoudnessDb)
        {
            const float outputTrimDb = juce::Decibels::gainToDecibels(juce::jmax(params.outputGain, 1.0e-4f));
            const float positiveEqBoostDb = computeWeightedPositiveEqBoostDb(params.eqBandGains);
            const float compressionDensity = computeCompressionDensityScore(params);
            const float widthLift = (juce::jmax(0.0f, params.lowMidWidth - 1.04f) * 0.24f)
                                  + (juce::jmax(0.0f, params.highMidWidth - 1.10f) * 0.62f)
                                  + (juce::jmax(0.0f, params.highWidth - 1.18f) * 0.86f);
            const float limiterDriveDb = juce::jmax(0.0f, -params.maximizerThreshold);
            const float hybridLimiterReliefDb = juce::jlimit(0.0f, 1.35f,
                                                             (juce::jmax(0.0f, limiterDriveDb - 2.4f) * 0.10f)
                                                           + (juce::jmax(0.0f, params.maximizerRelease - 100.0f) * 0.004f)
                                                           + (juce::jmax(0.0f, -params.maximizerCeiling - 0.55f) * 0.35f));

            const float predictedExcitationDb = (positiveEqBoostDb * 0.88f)
                                              + (juce::jmax(0.0f, outputTrimDb) * 1.05f)
                                              + (compressionDensity * 0.60f)
                                              + widthLift
                                              + (juce::jmax(0.0f, limiterDriveDb - 7.2f) * 0.34f)
                                              - (hybridLimiterReliefDb * 0.36f);
            const float predictedPreLimiterPeakDb = sourceFeatures.truePeakDb + (predictedExcitationDb * 0.50f);
            const float limiterStressDb = juce::jmax(0.0f, predictedPreLimiterPeakDb - (params.maximizerCeiling + 0.24f))
                                        + (juce::jmax(0.0f, limiterDriveDb - 8.6f) * 0.48f)
                                        - (hybridLimiterReliefDb * 0.62f);

            const float predictedProgramLoudnessDb = sourceProgramLoudnessDb
                                                   + (juce::jmax(0.0f, targetProgramLoudnessDb - sourceProgramLoudnessDb) * 0.94f)
                                                   + (positiveEqBoostDb * 0.16f)
                                                   + (compressionDensity * 0.24f);
            const float loudnessOvershootDb = juce::jmax(0.0f, predictedProgramLoudnessDb - (targetProgramLoudnessDb + 0.55f));
            const float boostStressDb = juce::jmax(0.0f, positiveEqBoostDb - 4.8f) * 0.46f;

            return juce::jmax(0.0f, limiterStressDb + (loudnessOvershootDb * 0.82f) + boostStressDb);
        }

        void softenReferenceParametersForSafety(MasteringParameters& params, float saturationRisk)
        {
            const float softenAmount = juce::jlimit(0.0f, 0.42f, saturationRisk / 6.5f);
            if (softenAmount <= 0.0f)
                return;

            for (auto& gainDb : params.eqBandGains)
            {
                if (gainDb > 0.0f)
                    gainDb *= (1.0f - (0.64f * softenAmount));
                else
                    gainDb *= (1.0f - (0.20f * softenAmount));

                gainDb = clampEqGain(gainDb);
            }

            params.eqBandGains = smoothEqCurve(params.eqBandGains);
            for (auto& gainDb : params.eqBandGains)
                gainDb = clampEqGain(gainDb);

            params.lowThresh += 2.2f * softenAmount;
            params.lowMidThresh += 2.0f * softenAmount;
            params.highMidThresh += 1.8f * softenAmount;
            params.highThresh += 1.6f * softenAmount;

            params.lowRatio = pullTowards(params.lowRatio, 1.10f, 0.42f * softenAmount);
            params.lowMidRatio = pullTowards(params.lowMidRatio, 1.08f, 0.40f * softenAmount);
            params.highMidRatio = pullTowards(params.highMidRatio, 1.06f, 0.38f * softenAmount);
            params.highRatio = pullTowards(params.highRatio, 1.04f, 0.36f * softenAmount);

            params.lowWidth = juce::jmin(params.lowWidth, 0.18f - (0.05f * softenAmount));
            params.lowMidWidth = pullTowards(params.lowMidWidth, 1.04f, 0.30f * softenAmount);
            params.highMidWidth = pullTowards(params.highMidWidth, 1.08f, 0.38f * softenAmount);
            params.highWidth = pullTowards(params.highWidth, 1.12f, 0.42f * softenAmount);

            float outputTrimDb = juce::Decibels::gainToDecibels(juce::jmax(params.outputGain, 1.0e-4f));
            outputTrimDb -= 1.4f * softenAmount;
            params.outputGain = juce::Decibels::decibelsToGain(outputTrimDb);

            params.maximizerThreshold = juce::jmin(-0.6f, params.maximizerThreshold + (1.8f * softenAmount));
            params.maximizerCeiling = juce::jmin(params.maximizerCeiling, -0.50f - (0.16f * softenAmount));
            params.maximizerRelease = juce::jmin(180.0f, params.maximizerRelease + (14.0f * softenAmount));

            juce::Logger::writeToLog("Reference safety softening applied: risk="
                                     + juce::String(saturationRisk, 2)
                                     + ", soften=" + juce::String(softenAmount, 2));
        }

        AnalysisFeatures extractFeatures(const juce::AudioBuffer<float>& recentAudio, double sampleRate)
        {
            AnalysisFeatures features;
            features.bandPeakDb.fill(-100.0f);
            features.bandRmsDb.fill(-100.0f);
            features.bandCrestDb.fill(0.0f);
            features.bandCorrelation.fill(1.0f);
            features.bandSideRatio.fill(0.0f);
            features.bandTransientMotionDb.fill(0.0f);

            const int numChannels = juce::jmin(recentAudio.getNumChannels(), 2);
            const int numSamples = recentAudio.getNumSamples();
            if (numChannels == 0 || numSamples < 1024 || sampleRate <= 0.0)
                return features;

            const auto* left = recentAudio.getReadPointer(0);
            const auto* right = (numChannels > 1) ? recentAudio.getReadPointer(1) : left;

            double sumL2 = 0.0;
            double sumR2 = 0.0;
            double sumLR = 0.0;
            double sumMid2 = 0.0;
            double sumSide2 = 0.0;
            float peak = 0.0f;

            for (int sample = 0; sample < numSamples; ++sample)
            {
                const float l = left[sample];
                const float r = right[sample];
                const float mid = 0.5f * (l + r);
                const float side = 0.5f * (l - r);

                peak = juce::jmax(peak, std::abs(l), std::abs(r));
                sumL2 += (double) l * l;
                sumR2 += (double) r * r;
                sumLR += (double) l * r;
                sumMid2 += (double) mid * mid;
                sumSide2 += (double) side * side;
            }

            const double overallMeanSquare = (sumL2 + sumR2) / (double) (numSamples * numChannels);
            features.peakDb = gainToDbSafe(peak);
            features.truePeakDb = gainToDbSafe(measureStrictTruePeak(recentAudio, numChannels, numSamples));
            features.rmsDb = meanSquareToDbSafe(overallMeanSquare);
            features.gatedRmsDb = features.rmsDb;
            features.crestDb = features.truePeakDb - features.rmsDb;
            std::tie(features.integratedLufs, features.shortTermLufs)
                = measureProgramLoudness(recentAudio, numChannels, numSamples, sampleRate);
            if (features.integratedLufs > -99.0f)
                features.gatedRmsDb = features.integratedLufs;

            const double correlationDenominator = std::sqrt(sumL2 * sumR2);
            if (correlationDenominator > 0.0)
                features.stereoCorrelation = juce::jlimit(-1.0f, 1.0f, (float) (sumLR / correlationDenominator));

            if (sumMid2 > 1.0e-12)
                features.sideRatio = std::sqrt((float) (sumSide2 / sumMid2));

            constexpr int fftOrder = 12;
            constexpr int fftSize = 1 << fftOrder;
            constexpr int hopSize = fftSize / 4;
            juce::dsp::FFT fft(fftOrder);
            juce::dsp::WindowingFunction<float> window(fftSize, juce::dsp::WindowingFunction<float>::hann);
            std::array<double, eqBandCount> eqBandEnergy {};
            std::vector<std::array<float, eqBandCount>> frameBandProfiles;
            std::vector<float> frameWeights;
            std::array<double, 4> bandTransientMotionSum {};
            std::array<double, 4> bandTransientMotionWeightSum {};
            std::array<float, 4> previousMacroBandDb {};
            float previousMotionWeight = 0.0f;
            bool hasPreviousMotionFrame = false;
            double lowEnergy = 0.0;
            double midEnergy = 0.0;
            double highEnergy = 0.0;
            double spectralWeightSum = 0.0;
            const float maxAnalysisFreq = juce::jmin(20000.0f, (float) sampleRate * 0.45f);
            const auto eqBandEdges = makeEqBandEdges(maxAnalysisFreq);
            const auto frameStarts = makeFrameStarts(numSamples, fftSize, hopSize);
            frameBandProfiles.reserve(frameStarts.size());
            frameWeights.reserve(frameStarts.size());

            for (const int startSample : frameStarts)
            {
                std::array<float, fftSize * 2> fftData {};
                std::array<double, eqBandCount> frameBandEnergy {};
                std::array<double, 4> frameMacroBandEnergy {};
                double frameMeanSquare = 0.0;

                for (int i = 0; i < fftSize; ++i)
                {
                    const int sampleIndex = juce::jmin(startSample + i, numSamples - 1);
                    const float mono = 0.5f * (left[sampleIndex] + right[sampleIndex]);
                    fftData[(size_t) i] = mono;
                    frameMeanSquare += (double) mono * mono;
                }

                const float frameRmsDb = meanSquareToDbSafe(frameMeanSquare / (double) fftSize);
                const float frameWeight = juce::jlimit(0.0f, 1.0f,
                                                       (frameRmsDb - (features.gatedRmsDb - 18.0f)) / 18.0f);
                if (frameWeight <= 0.01f)
                    continue;

                window.multiplyWithWindowingTable(fftData.data(), fftSize);
                fft.performFrequencyOnlyForwardTransform(fftData.data());

                size_t bandIndex = 0;
                for (int bin = 1; bin < fftSize / 2; ++bin)
                {
                    const float freq = (float) bin * ((float) sampleRate / (float) fftSize);
                    if (freq < 20.0f || freq > maxAnalysisFreq)
                        continue;

                    const double energy = (double) fftData[(size_t) bin] * fftData[(size_t) bin];

                    if (freq < 120.0f)
                    {
                        lowEnergy += energy * frameWeight;
                        frameMacroBandEnergy[0] += energy;
                    }
                    else if (freq < 1400.0f)
                    {
                        frameMacroBandEnergy[1] += energy;
                    }
                    else if (freq < 6000.0f)
                    {
                        frameMacroBandEnergy[2] += energy;
                    }
                    else
                    {
                        frameMacroBandEnergy[3] += energy;
                    }

                    if (freq >= 120.0f && freq < 4000.0f)
                        midEnergy += energy * frameWeight;
                    else if (freq >= 4000.0f)
                        highEnergy += energy * frameWeight;

                    while (bandIndex + 1 < (size_t) eqBandCount && freq >= eqBandEdges[bandIndex + 1])
                        ++bandIndex;

                    if (freq >= eqBandEdges[bandIndex] && freq < eqBandEdges[bandIndex + 1])
                    {
                        eqBandEnergy[bandIndex] += energy * frameWeight;
                        frameBandEnergy[bandIndex] += energy;
                    }
                }

                spectralWeightSum += frameWeight;

                std::array<float, eqBandCount> frameBandDb {};
                for (int band = 0; band < eqBandCount; ++band)
                    frameBandDb[(size_t) band] = meanSquareToDbSafe(frameBandEnergy[(size_t) band]);

                std::array<float, 4> frameMacroBandDb {};
                for (int band = 0; band < 4; ++band)
                    frameMacroBandDb[(size_t) band] = meanSquareToDbSafe(frameMacroBandEnergy[(size_t) band]);

                if (hasPreviousMotionFrame)
                {
                    const float motionWeight = std::sqrt(juce::jmax(0.0f, frameWeight * previousMotionWeight));
                    for (int band = 0; band < 4; ++band)
                    {
                        const float motionDb = juce::jmax(0.0f,
                            frameMacroBandDb[(size_t) band] - previousMacroBandDb[(size_t) band]);
                        bandTransientMotionSum[(size_t) band] += (double) motionDb * motionWeight;
                        bandTransientMotionWeightSum[(size_t) band] += motionWeight;
                    }
                }

                previousMacroBandDb = frameMacroBandDb;
                previousMotionWeight = frameWeight;
                hasPreviousMotionFrame = true;

                frameBandProfiles.push_back(frameBandDb);
                frameWeights.push_back(frameWeight);
            }

            const double normalizationWeight = juce::jmax(1.0, spectralWeightSum);
            const float lowDb = meanSquareToDbSafe(lowEnergy / normalizationWeight);
            const float midDb = meanSquareToDbSafe(midEnergy / normalizationWeight);
            const float highDb = meanSquareToDbSafe(highEnergy / normalizationWeight);
            features.lowVsMidDb = lowDb - midDb;
            features.highVsMidDb = highDb - midDb;

            std::array<float, eqBandCount> bandDb {};
            for (int band = 0; band < eqBandCount; ++band)
                bandDb[(size_t) band] = meanSquareToDbSafe(eqBandEnergy[(size_t) band] / normalizationWeight);

            const auto smoothedBandDb = smoothBandProfile(bandDb);
            features.eqBandProfileDb = smoothedBandDb;

            float transientMotionWeightedSum = 0.0f;
            float transientMotionWeightTotal = 0.0f;
            for (int band = 0; band < 4; ++band)
            {
                const float motionDb = (bandTransientMotionWeightSum[(size_t) band] > 0.0)
                    ? (float) (bandTransientMotionSum[(size_t) band] / bandTransientMotionWeightSum[(size_t) band])
                    : 0.0f;
                features.bandTransientMotionDb[(size_t) band] = juce::jlimit(0.0f, 6.0f, motionDb);

                const float bandWeight = (band == 0) ? 0.18f
                                       : (band == 1) ? 0.24f
                                       : (band == 2) ? 0.32f
                                                     : 0.26f;
                transientMotionWeightedSum += features.bandTransientMotionDb[(size_t) band] * bandWeight;
                transientMotionWeightTotal += bandWeight;
            }

            if (transientMotionWeightTotal > 0.0f)
                features.transientMotionDb = transientMotionWeightedSum / transientMotionWeightTotal;

            for (int band = 0; band < eqBandCount; ++band)
                features.eqDeviationDb[(size_t) band] = bandDb[(size_t) band] - smoothedBandDb[(size_t) band];

            double persistenceWeightSum = 0.0;
            for (size_t frameIndex = 0; frameIndex < frameBandProfiles.size(); ++frameIndex)
            {
                const float weight = frameWeights[frameIndex];
                if (weight <= 0.0f)
                    continue;

                const auto smoothedFrame = smoothBandProfile(frameBandProfiles[frameIndex]);
                for (int band = 0; band < eqBandCount; ++band)
                {
                    const float deviation = frameBandProfiles[frameIndex][(size_t) band] - smoothedFrame[(size_t) band];
                    if (deviation > 0.55f)
                    {
                        features.eqExcessPersistence[(size_t) band] += weight
                            * juce::jlimit(0.0f, 1.0f, (deviation - 0.55f) / 2.4f);
                    }
                    else if (deviation < -0.55f)
                    {
                        features.eqDeficitPersistence[(size_t) band] += weight
                            * juce::jlimit(0.0f, 1.0f, (-deviation - 0.55f) / 2.4f);
                    }
                }

                persistenceWeightSum += weight;
            }

            if (persistenceWeightSum > 0.0)
            {
                for (int band = 0; band < eqBandCount; ++band)
                {
                    features.eqExcessPersistence[(size_t) band] = juce::jlimit(0.0f, 1.0f,
                        (float) (features.eqExcessPersistence[(size_t) band] / persistenceWeightSum));
                    features.eqDeficitPersistence[(size_t) band] = juce::jlimit(0.0f, 1.0f,
                        (float) (features.eqDeficitPersistence[(size_t) band] / persistenceWeightSum));
                }
            }

            juce::dsp::LinkwitzRileyFilter<float> crossoverLowMid;
            juce::dsp::LinkwitzRileyFilter<float> crossoverMidHigh;
            juce::dsp::LinkwitzRileyFilter<float> crossoverHigh;
            juce::dsp::ProcessSpec analysisSpec;
            analysisSpec.sampleRate = sampleRate;
            analysisSpec.maximumBlockSize = 1;
            analysisSpec.numChannels = 2;

            crossoverLowMid.prepare(analysisSpec);
            crossoverMidHigh.prepare(analysisSpec);
            crossoverHigh.prepare(analysisSpec);
            crossoverLowMid.reset();
            crossoverMidHigh.reset();
            crossoverHigh.reset();
            crossoverLowMid.setCutoffFrequency(120.0f);
            crossoverMidHigh.setCutoffFrequency(1400.0f);
            crossoverHigh.setCutoffFrequency(6000.0f);

            std::array<double, 4> bandSumL2 {};
            std::array<double, 4> bandSumR2 {};
            std::array<double, 4> bandSumLR {};
            std::array<double, 4> bandSumMid2 {};
            std::array<double, 4> bandSumSide2 {};
            std::array<float, 4> bandPeak {};

            for (int sample = 0; sample < numSamples; ++sample)
            {
                float splitBandL[4] {};
                float splitBandR[4] {};

                for (int channel = 0; channel < numChannels; ++channel)
                {
                    const float input = (channel == 0) ? left[sample] : right[sample];
                    float lowPath = 0.0f;
                    float highPath = 0.0f;
                    float low = 0.0f;
                    float lowMid = 0.0f;
                    float highMid = 0.0f;
                    float high = 0.0f;

                    crossoverMidHigh.processSample(channel, input, lowPath, highPath);
                    crossoverLowMid.processSample(channel, lowPath, low, lowMid);
                    crossoverHigh.processSample(channel, highPath, highMid, high);

                    float* bandStorage = (channel == 0) ? splitBandL : splitBandR;
                    bandStorage[0] = low;
                    bandStorage[1] = lowMid;
                    bandStorage[2] = highMid;
                    bandStorage[3] = high;
                }

                if (numChannels == 1)
                    std::copy(std::begin(splitBandL), std::end(splitBandL), std::begin(splitBandR));

                for (int band = 0; band < 4; ++band)
                {
                    const float l = splitBandL[band];
                    const float r = splitBandR[band];
                    const float mid = 0.5f * (l + r);
                    const float side = 0.5f * (l - r);

                    bandPeak[(size_t) band] = juce::jmax(bandPeak[(size_t) band], std::abs(l), std::abs(r));
                    bandSumL2[(size_t) band] += (double) l * l;
                    bandSumR2[(size_t) band] += (double) r * r;
                    bandSumLR[(size_t) band] += (double) l * r;
                    bandSumMid2[(size_t) band] += (double) mid * mid;
                    bandSumSide2[(size_t) band] += (double) side * side;
                }
            }

            for (int band = 0; band < 4; ++band)
            {
                const double bandMeanSquare = (bandSumL2[(size_t) band] + bandSumR2[(size_t) band])
                                            / (double) (numSamples * juce::jmax(1, numChannels));
                features.bandPeakDb[(size_t) band] = gainToDbSafe(bandPeak[(size_t) band]);
                features.bandRmsDb[(size_t) band] = meanSquareToDbSafe(bandMeanSquare);
                features.bandCrestDb[(size_t) band] = features.bandPeakDb[(size_t) band] - features.bandRmsDb[(size_t) band];

                const double bandCorrelationDenominator = std::sqrt(bandSumL2[(size_t) band] * bandSumR2[(size_t) band]);
                if (bandCorrelationDenominator > 0.0)
                {
                    features.bandCorrelation[(size_t) band] = juce::jlimit(-1.0f, 1.0f,
                        (float) (bandSumLR[(size_t) band] / bandCorrelationDenominator));
                }

                if (bandSumMid2[(size_t) band] > 1.0e-12)
                    features.bandSideRatio[(size_t) band] = std::sqrt((float) (bandSumSide2[(size_t) band] / bandSumMid2[(size_t) band]));
            }

            features.valid = true;
            return features;
        }

        struct GenrePattern
        {
            AssistantGenre genre = AssistantGenre::Universal;
            float lowVsMidDb = 0.0f;
            float highVsMidDb = 0.0f;
            float crestDb = 9.0f;
            float gatedRmsDb = -12.0f;
            float sideRatio = 0.2f;
            float stereoCorrelation = 0.8f;
            std::array<float, 4> bandSideRatio {};
            std::array<float, 4> bandCrestDb {};
        };

        float normalizedDifference(float value, float target, float scale)
        {
            return std::abs(value - target) / juce::jmax(scale, 1.0e-4f);
        }

        float confidenceFromPenalty(float penalty, float scale)
        {
            return juce::jlimit(0.0f, 1.0f, 1.0f - (penalty / juce::jmax(scale, 1.0e-4f)));
        }

        std::array<float, eqBandCount> normalizeEqProfile(const std::array<float, eqBandCount>& profile)
        {
            std::array<float, eqBandCount> normalized = profile;
            float weightedSum = 0.0f;
            float weightTotal = 0.0f;

            for (int band = 0; band < eqBandCount; ++band)
            {
                const float weight = (band <= 1 || band >= eqBandCount - 2) ? 0.72f
                                   : (band >= 4 && band <= 11) ? 1.0f
                                   : 0.84f;
                weightedSum += profile[(size_t) band] * weight;
                weightTotal += weight;
            }

            const float average = (weightTotal > 0.0f) ? (weightedSum / weightTotal) : 0.0f;
            for (auto& value : normalized)
                value -= average;

            return smoothBandProfile(normalized);
        }

        float computeEqProfileDistance(const std::array<float, eqBandCount>& a,
                                       const std::array<float, eqBandCount>& b)
        {
            float weightedDistance = 0.0f;
            float weightTotal = 0.0f;

            for (int band = 0; band < eqBandCount; ++band)
            {
                const float weight = (band <= 1 || band >= eqBandCount - 2) ? 0.68f
                                   : (band >= 3 && band <= 11) ? 1.0f
                                   : 0.82f;
                weightedDistance += std::abs(a[(size_t) band] - b[(size_t) band]) * weight;
                weightTotal += weight;
            }

            return (weightTotal > 0.0f) ? (weightedDistance / weightTotal) : 0.0f;
        }

        template <size_t Size>
        float computeAverageBandPenalty(const std::array<float, Size>& a,
                                        const std::array<float, Size>& b,
                                        float scale)
        {
            float penalty = 0.0f;
            for (size_t index = 0; index < Size; ++index)
                penalty += normalizedDifference(a[index], b[index], scale);

            return penalty / (float) Size;
        }

        ReferenceMatchProfile evaluateReferenceMatch(const AnalysisFeatures& sourceFeatures,
                                                     const AnalysisFeatures& referenceFeatures)
        {
            ReferenceMatchProfile profile;

            const auto sourceNormalizedEq = normalizeEqProfile(sourceFeatures.eqBandProfileDb);
            const auto referenceNormalizedEq = normalizeEqProfile(referenceFeatures.eqBandProfileDb);
            const float programLoudnessGap = std::abs(getProgramLoudnessDb(sourceFeatures)
                                                    - getProgramLoudnessDb(referenceFeatures));
            const float eqDistance = computeEqProfileDistance(sourceNormalizedEq, referenceNormalizedEq);
            const float bandCrestPenalty = computeAverageBandPenalty(sourceFeatures.bandCrestDb,
                                                                     referenceFeatures.bandCrestDb,
                                                                     4.8f);
            const float bandWidthPenalty = computeAverageBandPenalty(sourceFeatures.bandSideRatio,
                                                                     referenceFeatures.bandSideRatio,
                                                                     0.24f);
            const float transientPenalty = (normalizedDifference(sourceFeatures.transientMotionDb,
                                                                 referenceFeatures.transientMotionDb,
                                                                 0.95f) * 0.34f)
                                         + (computeAverageBandPenalty(sourceFeatures.bandTransientMotionDb,
                                                                      referenceFeatures.bandTransientMotionDb,
                                                                      1.10f) * 0.66f);

            const float tonalPenalty = (normalizedDifference(sourceFeatures.lowVsMidDb,
                                                             referenceFeatures.lowVsMidDb,
                                                             3.6f) * 0.22f)
                                     + (normalizedDifference(sourceFeatures.highVsMidDb,
                                                             referenceFeatures.highVsMidDb,
                                                             3.0f) * 0.24f)
                                     + ((eqDistance / 4.2f) * 0.46f)
                                     + (transientPenalty * 0.08f);

            const float gluePenalty = (normalizedDifference(sourceFeatures.crestDb,
                                                            referenceFeatures.crestDb,
                                                            5.4f) * 0.44f)
                                     + (bandCrestPenalty * 0.28f)
                                     + (normalizedDifference(sourceFeatures.shortTermLufs,
                                                             referenceFeatures.shortTermLufs,
                                                             4.8f) * 0.16f)
                                     + (transientPenalty * 0.12f);

            const float widthPenalty = (normalizedDifference(sourceFeatures.sideRatio,
                                                             referenceFeatures.sideRatio,
                                                             0.22f) * 0.28f)
                                      + (normalizedDifference(sourceFeatures.stereoCorrelation,
                                                              referenceFeatures.stereoCorrelation,
                                                              0.30f) * 0.22f)
                                      + (bandWidthPenalty * 0.34f)
                                      + (normalizedDifference(sourceFeatures.bandSideRatio[0],
                                                              referenceFeatures.bandSideRatio[0],
                                                              0.10f) * 0.16f)
                                      + (normalizedDifference(sourceFeatures.bandTransientMotionDb[3],
                                                              referenceFeatures.bandTransientMotionDb[3],
                                                              1.15f) * 0.08f);

            const float loudnessPenalty = (normalizedDifference(getProgramLoudnessDb(sourceFeatures),
                                                                getProgramLoudnessDb(referenceFeatures),
                                                                5.0f) * 0.64f)
                                         + (normalizedDifference(sourceFeatures.truePeakDb,
                                                                 referenceFeatures.truePeakDb,
                                                                 1.20f) * 0.36f);

            const float lowEndMismatch = (normalizedDifference(sourceFeatures.lowVsMidDb,
                                                               referenceFeatures.lowVsMidDb,
                                                               4.2f) * 0.62f)
                                       + (normalizedDifference(sourceFeatures.bandSideRatio[0],
                                                               referenceFeatures.bandSideRatio[0],
                                                               0.09f) * 0.38f);

            profile.tonalConfidence = confidenceFromPenalty(tonalPenalty, 1.35f);
            profile.glueConfidence = confidenceFromPenalty(gluePenalty, 1.35f);
            profile.transientConfidence = confidenceFromPenalty(transientPenalty, 1.32f);
            profile.widthConfidence = confidenceFromPenalty(widthPenalty, 1.30f);
            profile.loudnessConfidence = confidenceFromPenalty(loudnessPenalty, 1.25f);
            profile.lowEndSafety = juce::jlimit(0.22f, 1.0f, confidenceFromPenalty(lowEndMismatch, 1.18f));

            profile.overallConfidence = juce::jlimit(0.0f, 1.0f,
                (profile.tonalConfidence * 0.36f)
              + (profile.glueConfidence * 0.24f)
              + (profile.transientConfidence * 0.14f)
              + (profile.widthConfidence * 0.12f)
              + (profile.loudnessConfidence * 0.14f));

            const float overallBlend = juce::jlimit(0.60f, 1.14f, 0.64f + (0.58f * profile.overallConfidence));
            profile.toneWeight = juce::jlimit(0.58f, 1.24f,
                (0.60f + (0.88f * ((profile.tonalConfidence * 0.68f)
                                 + (profile.transientConfidence * 0.14f)
                                 + (profile.overallConfidence * 0.18f))))
                    * overallBlend);
            profile.glueWeight = juce::jlimit(0.34f, 1.16f,
                (0.40f + (0.84f * ((profile.glueConfidence * 0.58f)
                                 + (profile.transientConfidence * 0.24f)
                                 + (profile.overallConfidence * 0.18f))))
                    * overallBlend);
            profile.transientWeight = juce::jlimit(0.42f, 1.24f,
                (0.48f + (0.86f * ((profile.transientConfidence * 0.72f)
                                 + (profile.glueConfidence * 0.12f)
                                 + (profile.overallConfidence * 0.16f))))
                    * juce::jlimit(0.68f, 1.12f, overallBlend * (0.86f + (0.18f * profile.transientConfidence))));
            profile.widthWeight = juce::jlimit(0.22f, 1.06f,
                ((0.28f + (0.78f * ((profile.widthConfidence * 0.66f)
                                  + (profile.transientConfidence * 0.10f)
                                  + (profile.overallConfidence * 0.24f))))
                    * overallBlend * juce::jlimit(0.72f, 1.0f, 0.72f + (0.28f * profile.lowEndSafety))));
            profile.loudnessWeight = juce::jlimit(0.34f, 1.18f,
                (0.40f + (0.82f * ((profile.loudnessConfidence * 0.54f)
                                 + (profile.overallConfidence * 0.32f)
                                 + (profile.transientConfidence * 0.14f))))
                    * juce::jlimit(0.66f, 1.10f,
                                   (0.72f + (0.44f * profile.overallConfidence))
                                     * (0.82f + (0.18f * profile.transientConfidence))));

            if (programLoudnessGap > 5.5f && profile.overallConfidence < 0.46f)
                profile.loudnessWeight = juce::jmin(profile.loudnessWeight, 0.86f);

            return profile;
        }

        float scoreGenrePattern(const AnalysisFeatures& features, const GenrePattern& pattern)
        {
            float score = 0.0f;
            score += normalizedDifference(features.lowVsMidDb, pattern.lowVsMidDb, 3.0f) * 1.15f;
            score += normalizedDifference(features.highVsMidDb, pattern.highVsMidDb, 3.0f) * 1.25f;
            score += normalizedDifference(features.crestDb, pattern.crestDb, 5.0f) * 1.10f;
            score += normalizedDifference(features.integratedLufs, pattern.gatedRmsDb, 4.0f) * 1.20f;
            score += normalizedDifference(features.sideRatio, pattern.sideRatio, 0.22f) * 0.95f;
            score += normalizedDifference(features.stereoCorrelation, pattern.stereoCorrelation, 0.30f) * 0.90f;

            for (int band = 0; band < 4; ++band)
            {
                score += normalizedDifference(features.bandSideRatio[(size_t) band],
                                              pattern.bandSideRatio[(size_t) band],
                                              0.24f) * 0.18f;
                score += normalizedDifference(features.bandCrestDb[(size_t) band],
                                              pattern.bandCrestDb[(size_t) band],
                                              6.0f) * 0.14f;
            }

            return score;
        }

        AssistantGenre detectGenreHeuristically(const AnalysisFeatures& features)
        {
            constexpr std::array<GenrePattern, 26> genrePatterns {{
                { AssistantGenre::Pop,        1.0f,  1.4f,  8.6f, -10.2f, 0.24f, 0.84f, { 0.03f, 0.11f, 0.23f, 0.36f }, { 7.2f, 6.9f, 6.8f, 6.6f } },
                { AssistantGenre::PopModern,  1.4f,  2.2f,  7.2f,  -8.6f, 0.26f, 0.80f, { 0.04f, 0.14f, 0.28f, 0.42f }, { 6.2f, 6.0f, 5.8f, 5.5f } },
                { AssistantGenre::PopAcoustic, 0.4f, 0.8f, 10.4f, -12.4f, 0.16f, 0.88f, { 0.02f, 0.08f, 0.16f, 0.24f }, { 9.2f, 8.8f, 8.4f, 8.0f } },
                { AssistantGenre::HipHop,     3.2f, -0.2f,  8.0f,  -9.3f, 0.16f, 0.90f, { 0.02f, 0.07f, 0.14f, 0.21f }, { 7.8f, 7.3f, 6.9f, 6.4f } },
                { AssistantGenre::HipHopLoFi, 1.8f, -1.5f, 10.5f, -11.0f, 0.10f, 0.92f, { 0.01f, 0.04f, 0.08f, 0.12f }, { 9.0f, 8.5f, 8.2f, 7.8f } },
                { AssistantGenre::HipHopBoomBap, 3.8f, 0.2f, 9.2f, -8.8f, 0.14f, 0.88f, { 0.02f, 0.06f, 0.12f, 0.18f }, { 8.5f, 8.0f, 7.5f, 7.0f } },
                { AssistantGenre::Trap,       3.8f,  0.6f,  7.3f,  -8.6f, 0.22f, 0.86f, { 0.02f, 0.08f, 0.17f, 0.24f }, { 7.4f, 7.0f, 6.5f, 6.0f } },
                { AssistantGenre::TrapDrill,  4.2f,  1.2f,  6.8f,  -8.0f, 0.20f, 0.84f, { 0.02f, 0.08f, 0.18f, 0.26f }, { 6.8f, 6.4f, 6.0f, 5.6f } },
                { AssistantGenre::Electronic, 2.1f,  2.2f,  7.4f,  -8.8f, 0.34f, 0.70f, { 0.03f, 0.14f, 0.34f, 0.52f }, { 7.0f, 6.6f, 6.3f, 5.9f } },
                { AssistantGenre::ElectronicClub, 3.2f, 2.8f, 6.4f, -7.8f, 0.42f, 0.62f, { 0.04f, 0.18f, 0.42f, 0.60f }, { 6.2f, 5.8f, 5.5f, 5.2f } },
                { AssistantGenre::ElectronicAmbient, 1.2f, 1.5f, 11.2f, -14.0f, 0.48f, 0.55f, { 0.04f, 0.16f, 0.38f, 0.56f }, { 9.8f, 9.4f, 8.8f, 8.2f } },
                { AssistantGenre::Rock,       1.5f,  0.8f, 10.2f, -11.4f, 0.21f, 0.80f, { 0.03f, 0.10f, 0.22f, 0.30f }, { 8.6f, 8.2f, 7.9f, 7.2f } },
                { AssistantGenre::RockIndie,  1.2f,  0.4f, 11.5f, -12.5f, 0.18f, 0.84f, { 0.02f, 0.08f, 0.18f, 0.26f }, { 9.6f, 9.2f, 8.8f, 8.2f } },
                { AssistantGenre::RockHard,   1.8f,  1.4f,  8.8f,  -9.8f, 0.24f, 0.76f, { 0.03f, 0.12f, 0.24f, 0.34f }, { 7.8f, 7.4f, 7.0f, 6.5f } },
                { AssistantGenre::BlackMetal,        1.2f,  2.0f, 10.5f, -10.2f, 0.28f, 0.58f, { 0.03f, 0.12f, 0.26f, 0.38f }, { 8.5f, 8.0f, 7.4f, 6.8f } },
                { AssistantGenre::BlackMetalRaw,     0.4f,  3.6f, 12.8f, -12.0f, 0.12f, 0.82f, { 0.01f, 0.05f, 0.14f, 0.22f }, { 10.8f, 10.2f, 9.6f, 8.8f } },
                { AssistantGenre::BlackMetalExtreme, 2.2f,  1.8f,  8.8f,  -8.8f, 0.14f, 0.78f, { 0.01f, 0.04f, 0.10f, 0.18f }, { 7.0f, 6.8f, 6.4f, 5.8f } },
                { AssistantGenre::DeathMetal, 1.8f,  2.0f,  9.3f,  -9.7f, 0.15f, 0.80f, { 0.01f, 0.06f, 0.15f, 0.22f }, { 8.7f, 8.4f, 7.9f, 7.2f } },
                { AssistantGenre::TechDeath,  1.4f,  2.4f,  8.5f,  -9.0f, 0.16f, 0.78f, { 0.01f, 0.06f, 0.16f, 0.24f }, { 8.2f, 7.8f, 7.4f, 6.8f } },
                { AssistantGenre::Melodeath,  1.6f,  1.8f,  9.0f,  -9.5f, 0.18f, 0.75f, { 0.02f, 0.08f, 0.18f, 0.26f }, { 8.5f, 8.0f, 7.6f, 7.0f } },
                { AssistantGenre::BrutalDeathMetal, 2.5f, 2.4f, 7.6f, -8.5f, 0.12f, 0.85f, { 0.01f, 0.05f, 0.11f, 0.18f }, { 7.3f, 7.1f, 6.6f, 6.1f } },
                { AssistantGenre::Acoustic,  -0.8f,  0.7f, 13.8f, -16.8f, 0.11f, 0.93f, { 0.01f, 0.05f, 0.10f, 0.16f }, { 11.4f, 10.8f, 10.0f, 9.0f } },
                { AssistantGenre::AcousticSolo, -1.2f, 0.4f, 15.5f, -18.5f, 0.08f, 0.95f, { 0.01f, 0.04f, 0.08f, 0.12f }, { 12.8f, 12.0f, 11.2f, 10.0f } },
                { AssistantGenre::Orchestral,-0.6f,  0.4f, 17.0f, -20.6f, 0.09f, 0.96f, { 0.01f, 0.04f, 0.09f, 0.14f }, { 14.0f, 13.5f, 12.8f, 11.7f } },
                { AssistantGenre::Cinematic,  1.4f,  0.8f, 14.5f, -15.0f, 0.14f, 0.92f, { 0.02f, 0.06f, 0.14f, 0.22f }, { 12.0f, 11.5f, 10.8f, 10.0f } },
                { AssistantGenre::Chamber,   -1.0f,  0.2f, 18.0f, -22.0f, 0.07f, 0.97f, { 0.01f, 0.03f, 0.07f, 0.12f }, { 15.0f, 14.5f, 13.8f, 12.5f } }
            }};

            float bestScore = 1.0e9f;
            float secondBestScore = 1.0e9f;
            AssistantGenre bestGenre = AssistantGenre::Universal;

            for (const auto& pattern : genrePatterns)
            {
                const float score = scoreGenrePattern(features, pattern);
                if (score < bestScore)
                {
                    secondBestScore = bestScore;
                    bestScore = score;
                    bestGenre = pattern.genre;
                }
                else if (score < secondBestScore)
                {
                    secondBestScore = score;
                }
            }

            const bool lowConfidence = bestScore > 5.6f;
            const bool ambiguous = (secondBestScore - bestScore) < 0.28f;
            if (lowConfidence || ambiguous)
                return AssistantGenre::Universal;

            return bestGenre;
        }

        ArtisticDirection detectDirectionHeuristically(const AnalysisFeatures& features, AssistantGenre genre)
        {
            const bool overCompressedProgram = features.crestDb < 6.5f;
            const bool hotProgram = features.gatedRmsDb > -10.5f || features.truePeakDb > -0.8f;
            const bool tooBright = features.highVsMidDb > 2.8f;
            const bool darkProgram = features.highVsMidDb < -2.8f;
            const bool narrowImage = features.sideRatio < 0.15f && features.stereoCorrelation > 0.86f;
            const bool wideImage = features.sideRatio > 0.42f
                                 || features.bandSideRatio[3] > 0.70f
                                 || features.stereoCorrelation < 0.25f;
            const bool dynamicProgram = features.crestDb > 12.5f;

            if (genre == AssistantGenre::Acoustic || genre == AssistantGenre::AcousticSolo
                || genre == AssistantGenre::Orchestral || genre == AssistantGenre::Chamber)
                return hotProgram ? ArtisticDirection::Balanced : ArtisticDirection::Transparent;

            if (hotProgram || overCompressedProgram)
                return ArtisticDirection::Transparent;

            if (genre == AssistantGenre::BlackMetal
                || genre == AssistantGenre::BlackMetalRaw
                || genre == AssistantGenre::BlackMetalExtreme
                || genre == AssistantGenre::DeathMetal
                || genre == AssistantGenre::TechDeath
                || genre == AssistantGenre::Melodeath
                || genre == AssistantGenre::BrutalDeathMetal
                || genre == AssistantGenre::TrapDrill
                || genre == AssistantGenre::Cinematic)
                return ArtisticDirection::Aggressive;

            if (tooBright && genre != AssistantGenre::Electronic && genre != AssistantGenre::ElectronicClub)
                return ArtisticDirection::Warm;

            if (narrowImage && !wideImage)
                return ArtisticDirection::Wide;

            if (genre == AssistantGenre::ElectronicAmbient)
                return ArtisticDirection::Wide;

            if (dynamicProgram && (genre == AssistantGenre::Rock
                                || genre == AssistantGenre::RockHard
                                || genre == AssistantGenre::HipHop
                                || genre == AssistantGenre::HipHopBoomBap
                                || genre == AssistantGenre::Trap))
                return ArtisticDirection::Punchy;

            if (darkProgram && features.gatedRmsDb < -13.0f)
                return ArtisticDirection::Aggressive;

            return ArtisticDirection::Balanced;
        }

        const char* genreToString(AssistantGenre genre)
        {
            switch (genre)
            {
                case AssistantGenre::Universal: return "Universal";
                case AssistantGenre::Pop: return "Pop";
                case AssistantGenre::PopModern: return "PopModern";
                case AssistantGenre::PopAcoustic: return "PopAcoustic";
                case AssistantGenre::HipHop: return "HipHop";
                case AssistantGenre::HipHopLoFi: return "HipHopLoFi";
                case AssistantGenre::HipHopBoomBap: return "HipHopBoomBap";
                case AssistantGenre::Trap: return "Trap";
                case AssistantGenre::TrapDrill: return "TrapDrill";
                case AssistantGenre::Electronic: return "Electronic";
                case AssistantGenre::ElectronicClub: return "ElectronicClub";
                case AssistantGenre::ElectronicAmbient: return "ElectronicAmbient";
                case AssistantGenre::Rock: return "Rock";
                case AssistantGenre::RockIndie: return "RockIndie";
                case AssistantGenre::RockHard: return "RockHard";
                case AssistantGenre::BlackMetal: return "BlackMetal";
                case AssistantGenre::BlackMetalRaw: return "BlackMetalRaw";
                case AssistantGenre::BlackMetalExtreme: return "BlackMetalExtreme";
                case AssistantGenre::DeathMetal: return "DeathMetal";
                case AssistantGenre::TechDeath: return "TechDeath";
                case AssistantGenre::Melodeath: return "Melodeath";
                case AssistantGenre::BrutalDeathMetal: return "BrutalDeathMetal";
                case AssistantGenre::Acoustic: return "Acoustic";
                case AssistantGenre::AcousticSolo: return "AcousticSolo";
                case AssistantGenre::Orchestral: return "Orchestral";
                case AssistantGenre::Cinematic: return "Cinematic";
                case AssistantGenre::Chamber: return "Chamber";
            }

            return "Universal";
        }

        const char* directionToString(ArtisticDirection direction)
        {
            switch (direction)
            {
                case ArtisticDirection::Balanced: return "Balanced";
                case ArtisticDirection::Transparent: return "Transparent";
                case ArtisticDirection::Warm: return "Warm";
                case ArtisticDirection::Punchy: return "Punchy";
                case ArtisticDirection::Wide: return "Wide";
                case ArtisticDirection::Aggressive: return "Aggressive";
            }

            return "Balanced";
        }

        AssistantContext resolveAssistantContext(const AnalysisFeatures& features,
                                                 const AssistantContext& requested,
                                                 bool& usedAutoDetection)
        {
            usedAutoDetection = (requested.genre == AssistantGenre::Universal
                              && requested.direction == ArtisticDirection::Balanced);
            if (!usedAutoDetection)
                return requested;

            AssistantContext detected;
            detected.genre = detectGenreHeuristically(features);
            detected.direction = detectDirectionHeuristically(features, detected.genre);
            return detected;
        }

        struct GenreTuning
        {
            float targetLoudnessDb = -9.8f;
            float lowBias = 0.0f;
            float presenceBias = 0.0f;
            float airBias = 0.0f;
            float widthBias = 0.0f;
            float glueBias = 0.0f;
            float mudToleranceDb = 1.10f;
            float harshToleranceDb = 1.10f;
            float transientTolerance = 0.0f;
            float ceilingDb = -0.75f;
        };

        struct DirectionTuning
        {
            float eqScale = 1.0f;
            float loudnessBiasDb = 0.0f;
            float widthBias = 0.0f;
            float glueBias = 0.0f;
            float airBias = 0.0f;
            float warmthBias = 0.0f;
        };

        float positiveEvidence(float value, float threshold)
        {
            return juce::jmax(0.0f, value - threshold);
        }

        int pickMaximizerMode(float transparentScore, float loudScore, float safeScore)
        {
            int mode = transparentMaximizerMode;
            float bestScore = transparentScore;

            if (loudScore > bestScore)
            {
                mode = loudMaximizerMode;
                bestScore = loudScore;
            }

            if (safeScore > bestScore)
                mode = safeMaximizerMode;

            return mode;
        }

        int chooseAssistantMaximizerMode(const AnalysisFeatures& features,
                                         const AssistantContext& context,
                                         float targetProgramLoudnessDb,
                                         float loudnessProxyDb,
                                         float maximizerDriveDb,
                                         float maximizerReleaseMs,
                                         bool hotProgram,
                                         bool overCompressedProgram,
                                         float dynamicPotential,
                                         float headroomPotential,
                                         float problemPotential)
        {
            float transparentScore = 0.35f;
            float loudScore = 0.10f;
            float safeScore = 0.12f;

            const float loudnessGapDb = targetProgramLoudnessDb - loudnessProxyDb;

            loudScore += positiveEvidence(loudnessGapDb, 1.2f) * 0.46f;
            loudScore += headroomPotential * 0.65f;
            loudScore += positiveEvidence(maximizerDriveDb, 4.0f) * 0.24f;
            loudScore += juce::jmax(0.0f, 10.8f - features.crestDb) * 0.18f;
            loudScore += juce::jmax(0.0f, 2.2f - features.transientMotionDb) * 0.20f;

            transparentScore += juce::jmax(0.0f, features.crestDb - 8.8f) * 0.10f;
            transparentScore += juce::jmax(0.0f, features.transientMotionDb - 1.9f) * 0.16f;
            transparentScore += dynamicPotential * 0.25f;
            transparentScore += positiveEvidence(maximizerReleaseMs, 126.0f) * 0.008f;
            transparentScore -= juce::jmax(0.0f, 9.8f - features.crestDb) * 0.10f;

            safeScore += hotProgram ? 0.75f : 0.0f;
            safeScore += overCompressedProgram ? 0.90f : 0.0f;
            safeScore += juce::jmax(0.0f, features.truePeakDb + 0.45f) * 0.90f;
            safeScore += juce::jmax(0.0f, features.crestDb - 12.2f) * 0.18f;
            safeScore += juce::jmax(0.0f, features.transientMotionDb - 2.6f) * 0.28f;
            safeScore += problemPotential * 0.24f;
            safeScore += juce::jmax(0.0f, features.sideRatio - 0.64f) * 0.55f;
            safeScore += positiveEvidence(maximizerReleaseMs, 138.0f) * 0.014f;

            switch (context.direction)
            {
                case ArtisticDirection::Transparent:
                    transparentScore += 0.95f;
                    loudScore -= 0.45f;
                    break;

                case ArtisticDirection::Balanced:
                    transparentScore += 0.35f;
                    break;

                case ArtisticDirection::Warm:
                    transparentScore += 0.20f;
                    safeScore += 0.10f;
                    break;

                case ArtisticDirection::Punchy:
                    loudScore += 0.45f;
                    break;

                case ArtisticDirection::Aggressive:
                    loudScore += 0.85f;
                    transparentScore -= 0.18f;
                    break;

                case ArtisticDirection::Wide:
                    safeScore += 0.12f;
                    break;
            }

            if (hotProgram)
            {
                loudScore -= 0.35f;
                transparentScore -= 0.15f;
            }

            if (overCompressedProgram)
            {
                loudScore -= 0.80f;
                transparentScore -= 0.35f;
            }

            if (features.truePeakDb > -0.35f)
                loudScore -= 0.45f;

            if (problemPotential > 0.55f)
                loudScore -= (problemPotential - 0.55f) * 0.45f;

            return pickMaximizerMode(transparentScore, loudScore, safeScore);
        }

        int chooseReferenceMaximizerMode(const AnalysisFeatures& sourceFeatures,
                                         const AnalysisFeatures& referenceFeatures,
                                         const ReferenceMatchProfile& referenceMatch,
                                         float saturationRisk,
                                         float sourceProgramLoudnessDb,
                                         float referenceProgramLoudnessDb,
                                         float maximizerDriveDb,
                                         float maximizerReleaseMs)
        {
            float transparentScore = 0.40f;
            float loudScore = 0.10f;
            float safeScore = 0.12f;

            const float loudnessGapDb = referenceProgramLoudnessDb - sourceProgramLoudnessDb;

            loudScore += positiveEvidence(referenceProgramLoudnessDb, -10.4f) * 0.55f;
            loudScore += positiveEvidence(loudnessGapDb, 0.75f) * 0.28f;
            loudScore += positiveEvidence(maximizerDriveDb, 4.0f) * 0.24f;
            loudScore += juce::jmax(0.0f, 10.2f - referenceFeatures.crestDb) * 0.18f;
            loudScore += juce::jmax(0.0f, 2.0f - referenceFeatures.transientMotionDb) * 0.22f;
            loudScore += referenceMatch.loudnessWeight * 0.32f;
            loudScore -= juce::jmax(0.0f, saturationRisk - 0.40f) * 0.35f;
            loudScore -= juce::jmax(0.0f, referenceFeatures.truePeakDb + 0.35f) * 0.55f;

            transparentScore += juce::jmax(0.0f, referenceFeatures.crestDb - 8.8f) * 0.10f;
            transparentScore += juce::jmax(0.0f, referenceFeatures.transientMotionDb - 1.9f) * 0.18f;
            transparentScore += juce::jmax(0.0f, -referenceProgramLoudnessDb - 11.2f) * 0.15f;
            transparentScore += positiveEvidence(maximizerReleaseMs, 126.0f) * 0.008f;
            if (referenceMatch.overallConfidence < 0.25f)
                transparentScore += 0.25f;

            safeScore += juce::jmax(0.0f, saturationRisk - 0.25f) * 0.95f;
            safeScore += juce::jmax(0.0f, sourceFeatures.truePeakDb + 0.40f) * 0.70f;
            safeScore += juce::jmax(0.0f, referenceFeatures.crestDb - 12.0f) * 0.22f;
            safeScore += juce::jmax(0.0f, referenceFeatures.transientMotionDb - 2.5f) * 0.30f;
            safeScore += juce::jmax(0.0f, referenceFeatures.sideRatio - 0.68f) * 0.55f;
            safeScore += positiveEvidence(maximizerReleaseMs, 138.0f) * 0.014f;
            if (referenceMatch.lowEndSafety < 0.28f)
                safeScore += 0.35f;
            if (referenceMatch.overallConfidence < 0.18f)
                safeScore += 0.18f;

            return pickMaximizerMode(transparentScore, loudScore, safeScore);
        }

        float computeLoudnessProxyDb(const AnalysisFeatures& features)
        {
            const float programLoudness = (features.integratedLufs > -99.0f)
                                        ? features.integratedLufs
                                        : features.gatedRmsDb;
            const float lowMaskingPenalty = juce::jmax(0.0f, features.lowVsMidDb - 1.0f) * 0.22f;
            const float darkBalancePenalty = juce::jmax(0.0f, -features.highVsMidDb - 1.2f) * 0.18f;
            const float presenceLift = juce::jmax(0.0f, juce::jmin(features.highVsMidDb, 2.5f)) * 0.10f;
            return programLoudness - lowMaskingPenalty - darkBalancePenalty + presenceLift;
        }

        GenreTuning getGenreTuning(AssistantGenre genre)
        {
            switch (genre)
            {
                case AssistantGenre::Pop:
                    return { -9.0f, 0.10f, 0.18f, 0.24f, 0.12f, 0.12f, 1.05f, 1.05f, 0.00f, -0.65f };
                case AssistantGenre::PopModern:
                    return { -8.0f, 0.14f, 0.22f, 0.28f, 0.15f, 0.16f, 0.95f, 1.00f, 0.02f, -0.55f };
                case AssistantGenre::PopAcoustic:
                    return { -10.5f, 0.05f, 0.12f, 0.18f, 0.08f, 0.04f, 1.15f, 1.08f, 0.10f, -0.85f };
                case AssistantGenre::HipHop:
                    return { -8.8f, 0.36f, 0.05f, -0.06f, -0.08f, 0.18f, 1.00f, 1.15f, 0.08f, -0.80f };
                case AssistantGenre::HipHopLoFi:
                    return { -10.0f, 0.22f, -0.05f, -0.15f, -0.10f, 0.14f, 1.10f, 1.25f, 0.12f, -0.90f };
                case AssistantGenre::HipHopBoomBap:
                    return { -8.5f, 0.42f, 0.08f, -0.04f, -0.05f, 0.22f, 0.95f, 1.10f, 0.15f, -0.75f };
                case AssistantGenre::Trap:
                    return { -8.2f, 0.46f, 0.10f, 0.08f, 0.00f, 0.24f, 1.00f, 1.12f, 0.05f, -0.75f };
                case AssistantGenre::TrapDrill:
                    return { -7.8f, 0.55f, 0.15f, 0.12f, 0.02f, 0.28f, 0.92f, 1.08f, 0.02f, -0.60f };
                case AssistantGenre::Electronic:
                    return { -8.4f, 0.14f, 0.12f, 0.30f, 0.22f, 0.18f, 1.08f, 1.14f, -0.05f, -0.65f };
                case AssistantGenre::ElectronicClub:
                    return { -7.6f, 0.22f, 0.16f, 0.35f, 0.28f, 0.24f, 1.02f, 1.08f, -0.08f, -0.50f };
                case AssistantGenre::ElectronicAmbient:
                    return { -11.0f, 0.10f, 0.08f, 0.22f, 0.35f, 0.05f, 1.15f, 1.12f, 0.05f, -0.85f };
                case AssistantGenre::Rock:
                    return { -9.6f, 0.12f, 0.24f, 0.04f, -0.04f, 0.24f, 0.98f, 1.05f, 0.14f, -0.80f };
                case AssistantGenre::RockIndie:
                    return { -10.2f, 0.08f, 0.20f, 0.06f, -0.02f, 0.15f, 1.05f, 1.10f, 0.18f, -0.85f };
                case AssistantGenre::RockHard:
                    return { -8.8f, 0.18f, 0.28f, 0.02f, -0.06f, 0.30f, 0.92f, 1.00f, 0.12f, -0.75f };
                case AssistantGenre::BlackMetal:
                    return { -10.1f, 0.06f, 0.10f, 0.00f, -0.08f, 0.14f, 0.95f, 1.28f, 0.26f, -0.90f };
                case AssistantGenre::BlackMetalRaw:
                    return { -12.0f, 0.02f, 0.04f, 0.06f, -0.12f, 0.06f, 0.85f, 1.45f, 0.38f, -1.10f };
                case AssistantGenre::BlackMetalExtreme:
                    return { -8.5f,  0.14f, 0.16f, -0.08f, -0.14f, 0.28f, 0.82f, 1.32f, 0.14f, -0.75f };
                case AssistantGenre::DeathMetal:
                    return { -8.9f, 0.18f, 0.20f, -0.04f, -0.10f, 0.30f, 0.92f, 1.16f, 0.18f, -0.82f };
                case AssistantGenre::TechDeath:
                    return { -8.4f, 0.15f, 0.24f, -0.02f, -0.08f, 0.32f, 0.95f, 1.10f, 0.20f, -0.70f };
                case AssistantGenre::Melodeath:
                    return { -9.2f, 0.14f, 0.26f, 0.08f, -0.04f, 0.25f, 1.02f, 1.12f, 0.15f, -0.78f };
                case AssistantGenre::BrutalDeathMetal:
                    return { -8.4f, 0.25f, 0.18f, -0.10f, -0.16f, 0.38f, 0.88f, 1.22f, 0.10f, -0.85f };
                case AssistantGenre::Acoustic:
                    return { -13.2f, 0.00f, 0.10f, 0.14f, 0.04f, -0.25f, 1.20f, 0.95f, 0.40f, -1.00f };
                case AssistantGenre::AcousticSolo:
                    return { -14.5f, 0.00f, 0.12f, 0.16f, 0.08f, -0.35f, 1.25f, 0.90f, 0.50f, -1.25f };
                case AssistantGenre::Orchestral:
                    return { -17.0f, 0.00f, 0.06f, 0.10f, 0.02f, -0.40f, 1.25f, 1.00f, 0.65f, -1.30f };
                case AssistantGenre::Cinematic:
                    return { -15.0f, 0.15f, 0.08f, 0.12f, 0.10f, -0.15f, 1.10f, 1.05f, 0.30f, -0.85f };
                case AssistantGenre::Chamber:
                    return { -18.5f, 0.00f, 0.04f, 0.10f, 0.00f, -0.50f, 1.30f, 0.90f, 0.80f, -1.50f };
                case AssistantGenre::Universal:
                    break;
            }

            return {};
        }

        DirectionTuning getDirectionTuning(ArtisticDirection direction)
        {
            switch (direction)
            {
                case ArtisticDirection::Transparent:
                    return { 0.82f, -1.0f, -0.05f, -0.18f, -0.05f, 0.00f };
                case ArtisticDirection::Warm:
                    return { 0.96f, -0.35f, -0.05f, 0.00f, -0.15f, 0.18f };
                case ArtisticDirection::Punchy:
                    return { 1.08f, 0.25f, -0.02f, 0.10f, 0.04f, 0.00f };
                case ArtisticDirection::Wide:
                    return { 1.04f, 0.00f, 0.18f, -0.05f, 0.10f, -0.05f };
                case ArtisticDirection::Aggressive:
                    return { 1.16f, 0.55f, 0.05f, 0.15f, 0.14f, -0.02f };
                case ArtisticDirection::Balanced:
                    break;
            }

            return {};
        }
    }

    // PIMPL struct to hold ONNX objects
    struct InferenceEngine::Impl
    {
        // Ort::Env env;
        // std::unique_ptr<Ort::Session> session;
        Impl() {} // : env(ORT_LOGGING_LEVEL_WARNING, "OxygenAI") {}
    };

    InferenceEngine::InferenceEngine() : pImpl(std::make_unique<Impl>())
    {
    }

    InferenceEngine::~InferenceEngine() = default;

    bool InferenceEngine::loadModel(const juce::File& modelFile)
    {
        if (!modelFile.existsAsFile())
            return false;

        try
        {
            // Ort::SessionOptions sessionOptions;
            // sessionOptions.SetIntraOpNumThreads(1);
            // sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_BASIC);

            // pImpl->session = std::make_unique<Ort::Session>(pImpl->env, modelFile.getFullPathName().toWideCharPointer(), sessionOptions);

            modelLoaded = true;
            return true;
        }
        catch (const std::exception& e)
        {
            juce::Logger::writeToLog("Error loading ONNX model: " + juce::String(e.what()));
            return false;
        }
    }

    MasteringParameters InferenceEngine::predict(const std::vector<float>& audioFeatures)
    {
        MasteringParameters params;

        if (!audioFeatures.empty())
        {
            const float loudness = audioFeatures[0];
            if (loudness < -20.0f)
                params.maximizerThreshold = -3.0f;
        }

        return params;
    }

    MasteringParameters InferenceEngine::predict(const juce::AudioBuffer<float>& recentAudio, double sampleRate)
    {
        return predict(recentAudio, sampleRate, {});
    }

    MasteringParameters InferenceEngine::predict(const juce::AudioBuffer<float>& recentAudio,
                                                 double sampleRate,
                                                 const AssistantContext& context)
    {
        MasteringParameters params;
        const auto features = extractFeatures(recentAudio, sampleRate);
        if (!features.valid)
            return params;

        params.usedAnalysis = true;
        bool usedAutoDetection = false;
        const auto effectiveContext = resolveAssistantContext(features, context, usedAutoDetection);
        if (usedAutoDetection)
        {
            juce::Logger::writeToLog(juce::String("Master Assistant auto context: genre=")
                                     + genreToString(effectiveContext.genre)
                                     + ", direction=" + directionToString(effectiveContext.direction));
        }

        const auto genreTuning = getGenreTuning(effectiveContext.genre);
        const auto directionTuning = getDirectionTuning(effectiveContext.direction);
        const float loudnessProxyDb = computeLoudnessProxyDb(features);
        const float targetProgramLoudnessDb = genreTuning.targetLoudnessDb + directionTuning.loudnessBiasDb;
        const auto maxBandRange = [] (const auto& values, int startBand, int endBand)
        {
            float value = 0.0f;
            for (int band = startBand; band <= endBand; ++band)
                value = juce::jmax(value, values[(size_t) band]);
            return value;
        };

        const float mudExcess = juce::jmax(features.eqDeviationDb[4], features.eqDeviationDb[5], features.eqDeviationDb[6]);
        const float harshExcess = juce::jmax(features.eqDeviationDb[9], features.eqDeviationDb[10], features.eqDeviationDb[11]);
        const float presenceDip = juce::jmax(0.0f, -(features.eqDeviationDb[8] + features.eqDeviationDb[9]) * 0.5f);
        const float mudPersistence = maxBandRange(features.eqExcessPersistence, 4, 6);
        const float harshPersistence = maxBandRange(features.eqExcessPersistence, 9, 11);
        const float lowLiftPersistence = maxBandRange(features.eqDeficitPersistence, 1, 3);
        const float presencePersistence = maxBandRange(features.eqDeficitPersistence, 8, 9);
        const float airPersistence = maxBandRange(features.eqDeficitPersistence, 12, 13);

        const float lowOverhang = positiveEvidence(features.lowVsMidDb,
                                                   1.8f - (genreTuning.lowBias * 0.60f) - (directionTuning.warmthBias * 0.20f));
        const float lowDeficit = positiveEvidence(-features.lowVsMidDb,
                                                  1.9f + (genreTuning.lowBias * 0.35f));
        const float topOverhang = positiveEvidence(features.highVsMidDb,
                                                   1.9f - (genreTuning.airBias * 0.20f));
        const float topDeficit = positiveEvidence(-features.highVsMidDb,
                                                  1.6f + (directionTuning.warmthBias * 0.45f));
        const float mudEvidence = positiveEvidence(mudExcess, genreTuning.mudToleranceDb)
                                * juce::jlimit(0.35f, 1.45f,
                                               0.62f + (0.07f * features.lowVsMidDb) + (0.75f * mudPersistence));
        const float harshEvidence = positiveEvidence(harshExcess, genreTuning.harshToleranceDb)
                                  * juce::jlimit(0.35f, 1.45f,
                                                 0.64f + (0.07f * features.highVsMidDb) + (0.75f * harshPersistence));
        const float presenceOpportunity = positiveEvidence(presenceDip,
                                                           0.85f - (genreTuning.presenceBias * 0.35f))
                                        * juce::jlimit(0.20f, 1.25f,
                                                       0.88f + (0.65f * presencePersistence)
                                                     - (0.30f * topOverhang) - (0.25f * harshEvidence));
        const float airOpportunity = positiveEvidence(topDeficit, 0.25f)
                                   * juce::jlimit(0.15f, 1.25f,
                                                  0.88f + (0.70f * airPersistence) - (0.40f * harshEvidence));
        const float lowPunchOpportunity = positiveEvidence(lowDeficit, 0.30f)
                                        * juce::jlimit(0.15f, 1.25f,
                                                       0.88f + (0.70f * lowLiftPersistence) - (0.55f * mudEvidence));
        const float tonalImbalanceScore = (0.45f * std::abs(features.lowVsMidDb))
                                        + (0.50f * std::abs(features.highVsMidDb));
        const float problemSeverity = (0.80f * mudEvidence)
                                    + (0.80f * harshEvidence)
                                    + (0.35f * lowOverhang)
                                    + (0.30f * topOverhang);
        const float enhancementSeverity = (0.65f * lowPunchOpportunity)
                                        + (0.80f * presenceOpportunity)
                                        + (0.75f * airOpportunity);
        const float persistenceSeverity = (0.70f * mudPersistence)
                                        + (0.70f * harshPersistence)
                                        + (0.55f * lowLiftPersistence)
                                        + (0.60f * presencePersistence)
                                        + (0.60f * airPersistence);
        const float eqSeverity = tonalImbalanceScore + problemSeverity + enhancementSeverity + persistenceSeverity;
        const bool mostlyBalanced = eqSeverity < 1.65f;
        const bool overCompressedProgram = features.crestDb < (6.5f - (0.45f * genreTuning.transientTolerance));
        const bool hotProgram = loudnessProxyDb > (targetProgramLoudnessDb - 0.9f)
                             || features.shortTermLufs > (targetProgramLoudnessDb + 0.7f)
                             || features.truePeakDb > (genreTuning.ceilingDb + 0.35f);
        const bool needsGlueControl = features.crestDb > (8.8f + genreTuning.transientTolerance)
                                   && features.truePeakDb > -1.8f;
        const float dynamicPotential = juce::jlimit(0.0f, 1.0f,
                                                    (features.crestDb - (7.4f + genreTuning.transientTolerance)) / 7.0f);
        const float headroomPotential = juce::jlimit(0.0f, 1.0f, (-features.truePeakDb - 0.8f) / 5.5f);
        const float loudnessGapPotential = juce::jlimit(0.0f, 1.0f,
                                                        (targetProgramLoudnessDb - loudnessProxyDb) / 7.0f);
        const float problemPotential = juce::jlimit(0.0f, 1.0f, problemSeverity / 4.5f);
        const float enhancementPotential = juce::jlimit(0.0f, 1.0f, enhancementSeverity / 4.5f);

        float impactStrength = 0.92f
                             + (0.28f * problemPotential)
                             + (0.24f * enhancementPotential)
                             + (0.34f * loudnessGapPotential)
                             + (0.22f * headroomPotential)
                             + (0.12f * dynamicPotential);
        if (hotProgram)
            impactStrength *= 0.86f;
        if (overCompressedProgram)
            impactStrength *= 0.84f;
        impactStrength = juce::jlimit(0.82f, 1.75f, impactStrength);

        const float lowTrim = juce::jlimit(0.0f, 2.0f, (0.35f * lowOverhang) + (0.30f * mudEvidence));
        if (lowTrim > 0.0f)
        {
            addEqGain(params, 1, -0.18f * lowTrim);
            addEqGain(params, 2, -0.32f * lowTrim);
            addEqGain(params, 3, -0.28f * lowTrim);
        }

        const float mudTrim = juce::jlimit(0.0f, 2.4f, 0.40f + (0.75f * mudEvidence));
        if (mudEvidence > 0.0f)
        {
            addEqGain(params, 4, -0.18f * mudTrim);
            addEqGain(params, 5, -0.34f * mudTrim);
            addEqGain(params, 6, -0.24f * mudTrim);
        }

        const float topTrim = juce::jlimit(0.0f, 1.8f, (0.30f * topOverhang) + (0.28f * harshEvidence));
        if (topTrim > 0.0f)
        {
            addEqGain(params, 10, -0.18f * topTrim);
            addEqGain(params, 11, -0.24f * topTrim);
            addEqGain(params, 12, -0.16f * topTrim);
        }

        const float harshTrim = juce::jlimit(0.0f, 2.2f, 0.35f + (0.70f * harshEvidence));
        if (harshEvidence > 0.0f)
        {
            addEqGain(params, 9, -0.18f * harshTrim);
            addEqGain(params, 10, -0.30f * harshTrim);
            addEqGain(params, 11, -0.22f * harshTrim);
        }

        const float lowLift = juce::jlimit(0.0f, 1.8f,
                                           (0.30f * lowPunchOpportunity) + (0.18f * genreTuning.lowBias));
        if (lowLift > 0.05f)
        {
            addEqGain(params, 1, 0.10f * lowLift);
            addEqGain(params, 2, 0.28f * lowLift);
            addEqGain(params, 3, 0.22f * lowLift);
        }

        const float presenceLift = juce::jlimit(0.0f, 1.8f,
                                                (0.32f * presenceOpportunity)
                                              + (0.16f * genreTuning.presenceBias)
                                              - (0.10f * harshEvidence));
        if (presenceLift > 0.05f)
        {
            addEqGain(params, 8, 0.26f * presenceLift);
            addEqGain(params, 9, 0.20f * presenceLift);
        }

        const float airLift = juce::jlimit(0.0f, 2.0f,
                                           (0.30f * airOpportunity)
                                         + (0.16f * (genreTuning.airBias + directionTuning.airBias))
                                         - (0.12f * harshEvidence)
                                         - (0.08f * topOverhang));
        if (airLift > 0.05f)
        {
            addEqGain(params, 12, 0.22f * airLift);
            addEqGain(params, 13, 0.26f * airLift);
            addEqGain(params, 14, 0.10f * airLift);
        }

        auto addOpportunityBoost = [&features, &params] (int band, float driver, float deficitThreshold, float scale, float maxBoost)
        {
            if (driver <= 0.0f)
                return;

            const float hole = positiveEvidence(-features.eqDeviationDb[(size_t) band], deficitThreshold);
            if (hole <= 0.0f)
                return;

            const float persistenceScale = 0.40f + (1.15f * features.eqDeficitPersistence[(size_t) band]);
            addEqGain(params, band, juce::jlimit(0.0f, maxBoost,
                                                 (0.12f + (hole * scale)) * driver * persistenceScale));
        };

        for (int band = 3; band <= 11; ++band)
        {
            const float centerFreq = eqBandCenters[(size_t) band];
            const float excess = features.eqDeviationDb[(size_t) band];
            const float localThreshold = (centerFreq < 800.0f)
                                       ? (genreTuning.mudToleranceDb + 0.55f)
                                       : (genreTuning.harshToleranceDb + 0.55f);
            const float resonanceEvidence = positiveEvidence(excess, localThreshold);
            if (resonanceEvidence <= 0.0f)
                continue;

            const float tonalGuard = (centerFreq < 800.0f)
                                   ? juce::jlimit(0.55f, 1.25f, 0.82f + (0.07f * features.lowVsMidDb))
                                   : juce::jlimit(0.55f, 1.25f, 0.84f + (0.07f * features.highVsMidDb));
            const float persistenceScale = 0.38f + (1.25f * features.eqExcessPersistence[(size_t) band]);
            const float cutDb = juce::jlimit(0.0f, 1.65f,
                                             resonanceEvidence * 0.42f * tonalGuard * persistenceScale);
            addEqGain(params, band, -cutDb);
        }

        addOpportunityBoost(2, lowLift + (0.15f * impactStrength), 0.55f, 0.22f, 0.95f);
        addOpportunityBoost(3, lowLift + (0.12f * impactStrength), 0.45f, 0.18f, 0.80f);
        addOpportunityBoost(8, presenceLift + (0.10f * impactStrength), 0.45f, 0.20f, 0.85f);
        addOpportunityBoost(9, presenceLift + (0.08f * impactStrength), 0.40f, 0.16f, 0.70f);
        addOpportunityBoost(12, airLift + (0.12f * impactStrength), 0.55f, 0.18f, 0.85f);
        addOpportunityBoost(13, airLift + (0.10f * impactStrength), 0.55f, 0.16f, 0.90f);

        const float colorStrength = juce::jlimit(0.0f, 1.0f, 0.35f + (0.25f * impactStrength));
        if (genreTuning.lowBias > 0.12f && lowPunchOpportunity > 0.05f)
            addEqGain(params, 2, 0.16f * genreTuning.lowBias * colorStrength);
        if (genreTuning.presenceBias > 0.12f && presenceOpportunity > 0.05f)
            addEqGain(params, 8, 0.20f * genreTuning.presenceBias * colorStrength);
        if (genreTuning.airBias > 0.12f && airOpportunity > 0.05f)
            addEqGain(params, 12, 0.24f * genreTuning.airBias * colorStrength);
        if (directionTuning.warmthBias > 0.08f && topOverhang < 0.8f)
        {
            addEqGain(params, 4, 0.16f * directionTuning.warmthBias * colorStrength);
            addEqGain(params, 12, -0.14f * directionTuning.warmthBias * colorStrength);
            addEqGain(params, 13, -0.10f * directionTuning.warmthBias * colorStrength);
        }

        params.lowMidX = 120.0f + juce::jlimit(-20.0f, 20.0f, (lowOverhang - lowDeficit) * 10.0f);
        params.midHighX = 1400.0f + juce::jlimit(-260.0f, 240.0f, (presenceOpportunity - harshEvidence) * 110.0f);
        params.highX = 6000.0f + juce::jlimit(-600.0f, 700.0f, (airOpportunity - topOverhang) * 260.0f);

        const float lowControl = computeBandControl(features.bandCrestDb[0],
                                                    juce::jlimit(0.0f, 0.60f,
                                                                 (0.14f * lowOverhang)
                                                               + (0.10f * lowPunchOpportunity)
                                                               + (0.08f * genreTuning.glueBias)));
        const float lowMidControl = computeBandControl(features.bandCrestDb[1],
                                                       juce::jlimit(0.0f, 0.65f,
                                                                    (0.18f * mudEvidence)
                                                                  + (0.08f * genreTuning.glueBias)));
        const float highMidControl = computeBandControl(features.bandCrestDb[2],
                                                        juce::jlimit(0.0f, 0.60f,
                                                                     (0.18f * harshEvidence)
                                                                   + (0.08f * presenceOpportunity)
                                                                   + (0.06f * genreTuning.glueBias)));
        const float highControl = computeBandControl(features.bandCrestDb[3],
                                                     juce::jlimit(0.0f, 0.55f,
                                                                  (0.12f * topOverhang)
                                                                + (0.14f * airOpportunity)));

        params.lowThresh = -13.2f - (4.2f * lowControl);
        params.lowRatio = 1.18f + (0.46f * lowControl);
        params.lowAttack = 34.0f - (11.0f * lowControl);
        params.lowRelease = 186.0f - (26.0f * lowControl);

        params.lowMidThresh = -12.8f - (4.0f * lowMidControl);
        params.lowMidRatio = 1.18f + (0.42f * lowMidControl);
        params.lowMidAttack = 24.0f - (7.0f * lowMidControl);
        params.lowMidRelease = 152.0f - (18.0f * lowMidControl);

        params.highMidThresh = -12.2f - (3.7f * highMidControl);
        params.highMidRatio = 1.14f + (0.34f * highMidControl);
        params.highMidAttack = 10.5f - (3.5f * highMidControl);
        params.highMidRelease = 122.0f - (16.0f * highMidControl);

        params.highThresh = -11.3f - (3.3f * highControl);
        params.highRatio = 1.10f + (0.28f * highControl);
        params.highAttack = 6.0f - (1.8f * highControl);
        params.highRelease = 102.0f - (11.0f * highControl);

        float glueControl = juce::jlimit(0.0f, 1.35f,
                                         (positiveEvidence(features.crestDb, 8.6f + genreTuning.transientTolerance) / 4.8f)
                                       + (juce::jmax(0.0f, targetProgramLoudnessDb - loudnessProxyDb) / 7.0f)
                                       + genreTuning.glueBias
                                       + directionTuning.glueBias);
        if (hotProgram)
            glueControl *= 0.78f;
        if (overCompressedProgram)
            glueControl *= 0.50f;

        if (needsGlueControl || glueControl > 0.15f)
        {
            params.lowThresh -= 1.3f * glueControl;
            params.lowMidThresh -= 1.1f * glueControl;
            params.highMidThresh -= 0.95f * glueControl;
            params.highThresh -= 0.80f * glueControl;

            params.lowRatio += 0.14f + (0.22f * glueControl);
            params.lowMidRatio += 0.12f + (0.20f * glueControl);
            params.highMidRatio += 0.10f + (0.16f * glueControl);
            params.highRatio += 0.06f + (0.12f * glueControl);

            params.lowRelease -= 12.0f * glueControl;
            params.lowMidRelease -= 10.0f * glueControl;
            params.highMidRelease -= 8.0f * glueControl;
            params.highRelease -= 6.0f * glueControl;
        }

        const float recoveryAmount = juce::jlimit(0.0f, 1.0f,
                                                  juce::jmax(0.0f, (6.8f - features.crestDb) / 2.4f)
                                                + juce::jmax(0.0f, (loudnessProxyDb - targetProgramLoudnessDb - 0.4f) / 2.8f));
        if (recoveryAmount > 0.0f)
        {
            params.lowRatio = pullTowards(params.lowRatio, 1.16f, 0.55f * recoveryAmount);
            params.lowMidRatio = pullTowards(params.lowMidRatio, 1.14f, 0.55f * recoveryAmount);
            params.highMidRatio = pullTowards(params.highMidRatio, 1.10f, 0.60f * recoveryAmount);
            params.highRatio = pullTowards(params.highRatio, 1.06f, 0.60f * recoveryAmount);

            params.lowThresh = pullTowards(params.lowThresh, -11.2f, 0.45f * recoveryAmount);
            params.lowMidThresh = pullTowards(params.lowMidThresh, -10.8f, 0.45f * recoveryAmount);
            params.highMidThresh = pullTowards(params.highMidThresh, -10.2f, 0.50f * recoveryAmount);
            params.highThresh = pullTowards(params.highThresh, -9.8f, 0.50f * recoveryAmount);

            params.lowAttack = juce::jmin(46.0f, params.lowAttack + (8.0f * recoveryAmount));
            params.lowMidAttack = juce::jmin(34.0f, params.lowMidAttack + (6.0f * recoveryAmount));
            params.highMidAttack = juce::jmin(15.0f, params.highMidAttack + (3.0f * recoveryAmount));
            params.highAttack = juce::jmin(10.0f, params.highAttack + (2.0f * recoveryAmount));
        }

        const float compressorImpact = juce::jlimit(0.0f, 1.9f, impactStrength + (0.18f * glueControl));
        params.lowThresh -= 1.35f * compressorImpact;
        params.lowMidThresh -= 1.15f * compressorImpact;
        params.highMidThresh -= 0.95f * compressorImpact;
        params.highThresh -= 0.80f * compressorImpact;

        params.lowRatio += 0.22f * compressorImpact;
        params.lowMidRatio += 0.20f * compressorImpact;
        params.highMidRatio += 0.16f * compressorImpact;
        params.highRatio += 0.12f * compressorImpact;

        const float widthBias = (genreTuning.widthBias + directionTuning.widthBias) * (hotProgram ? 0.45f : 1.0f);

        params.lowWidth = juce::jlimit(0.0f, 0.22f, 0.14f - (features.bandSideRatio[0] * 0.16f));
        params.lowMidWidth = (features.bandCorrelation[1] > 0.90f && features.bandSideRatio[1] < 0.12f) ? 1.02f
                          : (features.bandCorrelation[1] < 0.45f || features.bandSideRatio[1] > 0.45f) ? 0.97f
                          : 0.99f;
        params.lowMidWidth += 0.03f * widthBias;

        if (features.bandCorrelation[2] > 0.88f && features.bandSideRatio[2] < 0.18f)
            params.highMidWidth = 1.10f;
        else if (features.bandCorrelation[2] < 0.25f || features.bandSideRatio[2] > 0.70f)
            params.highMidWidth = 1.00f;
        else
            params.highMidWidth = 1.05f;
        params.highMidWidth += 0.07f * widthBias;

        if (features.bandCorrelation[3] > 0.86f && features.bandSideRatio[3] < 0.18f)
            params.highWidth = 1.18f;
        else if (features.bandCorrelation[3] < 0.20f || features.bandSideRatio[3] > 0.75f)
            params.highWidth = 1.01f;
        else
            params.highWidth = 1.10f;
        params.highWidth += 0.10f * widthBias;

        if (features.stereoCorrelation < 0.15f || features.sideRatio > 0.70f)
        {
            params.highMidWidth = juce::jmin(params.highMidWidth, 1.06f);
            params.highWidth = juce::jmin(params.highWidth, 1.10f);
        }

        if (hotProgram)
        {
            params.lowMidWidth = juce::jmin(params.lowMidWidth, 1.02f);
            params.highMidWidth = juce::jmin(params.highMidWidth, 1.10f);
            params.highWidth = juce::jmin(params.highWidth, 1.16f);
        }

        params.maximizerRelease = (features.crestDb > 12.0f) ? 108.0f
                                 : (features.crestDb < 8.0f) ? 138.0f
                                 : 122.0f;
        params.maximizerRelease -= 10.0f * juce::jlimit(-0.30f, 0.60f, genreTuning.glueBias + directionTuning.glueBias);

        if (hotProgram || overCompressedProgram)
            params.maximizerRelease = juce::jmax(params.maximizerRelease, 126.0f);

        if (effectiveContext.direction == ArtisticDirection::Punchy)
        {
            params.lowAttack = juce::jmin(45.0f, params.lowAttack + 5.0f);
            params.lowMidAttack = juce::jmin(32.0f, params.lowMidAttack + 4.0f);
            params.lowRelease = juce::jmin(210.0f, params.lowRelease + 10.0f);
            params.lowMidRelease = juce::jmin(180.0f, params.lowMidRelease + 8.0f);
            params.highMidRatio = juce::jmax(1.12f, params.highMidRatio - 0.04f);
        }
        else if (effectiveContext.direction == ArtisticDirection::Transparent)
        {
            params.lowRatio = pullTowards(params.lowRatio, 1.08f, 0.45f);
            params.lowMidRatio = pullTowards(params.lowMidRatio, 1.08f, 0.45f);
            params.highMidRatio = pullTowards(params.highMidRatio, 1.06f, 0.45f);
            params.highRatio = pullTowards(params.highRatio, 1.04f, 0.45f);
            params.maximizerRelease = juce::jmax(params.maximizerRelease, 140.0f);
        }

        float eqIntensity = juce::jmap(juce::jlimit(0.0f, 7.0f, eqSeverity), 0.0f, 7.0f, 0.95f, 1.95f);
        if (mostlyBalanced)
            eqIntensity = juce::jmin(eqIntensity, 1.08f);

        eqIntensity *= directionTuning.eqScale;
        eqIntensity *= (0.92f + (0.28f * impactStrength));

        if (hotProgram)
            eqIntensity *= 0.92f;
        if (overCompressedProgram)
            eqIntensity *= 0.90f;

        eqIntensity = juce::jlimit(0.95f, 2.65f, eqIntensity);

        for (auto& gainDb : params.eqBandGains)
        {
            gainDb = juce::jlimit(-assistantEqHardLimitDb, assistantEqHardLimitDb, gainDb * eqIntensity);
            if (std::abs(gainDb) < 0.01f)
                gainDb = 0.0f;
        }

        const float dynamicEqScale = juce::jlimit(0.72f, 1.48f,
                                                  impactStrength
                                                    * (hotProgram ? 0.94f : 1.0f)
                                                    * (overCompressedProgram ? 0.88f : 1.0f)
                                                    * (0.96f + (0.20f * problemPotential)));
        const float lowResonancePersistence = maxBandRange(features.eqExcessPersistence, 2, 4);
        const float bodyResonancePersistence = maxBandRange(features.eqExcessPersistence, 5, 7);
        const float presenceResonancePersistence = maxBandRange(features.eqExcessPersistence, 9, 10);
        const float airResonancePersistence = maxBandRange(features.eqExcessPersistence, 11, 13);

        const float lowDynamicRange = juce::jlimit(0.0f, 7.4f,
            ((0.55f * lowOverhang) + (0.42f * mudEvidence) + (2.35f * lowResonancePersistence) - (0.18f * lowPunchOpportunity))
                * dynamicEqScale);
        const float bodyDynamicRange = juce::jlimit(0.0f, 6.8f,
            ((0.88f * mudEvidence) + (2.75f * bodyResonancePersistence) + (0.18f * lowOverhang))
                * dynamicEqScale);
        const float presenceDynamicRange = juce::jlimit(0.0f, 8.0f,
            ((0.82f * harshEvidence) + (3.05f * presenceResonancePersistence) + (0.22f * topOverhang))
                * dynamicEqScale);
        const float airDynamicRange = juce::jlimit(0.0f, 6.2f,
            ((0.52f * harshEvidence) + (0.42f * topOverhang) + (2.45f * airResonancePersistence) - (0.15f * airOpportunity))
                * dynamicEqScale);

        setDynamicEqBand(params, 0,
                         deriveDynamicEqThreshold(features.bandRmsDb[0], lowDynamicRange, features.bandTransientMotionDb[0],
                                                  hotProgram ? 0.5f : 0.0f),
                         lowDynamicRange);
        setDynamicEqBand(params, 1,
                         deriveDynamicEqThreshold(features.bandRmsDb[1], bodyDynamicRange, features.bandTransientMotionDb[1],
                                                  hotProgram ? 0.3f : -0.2f),
                         bodyDynamicRange);
        setDynamicEqBand(params, 2,
                         deriveDynamicEqThreshold(features.bandRmsDb[2], presenceDynamicRange, features.bandTransientMotionDb[2],
                                                  hotProgram ? 0.2f : -0.4f),
                         presenceDynamicRange);
        setDynamicEqBand(params, 3,
                         deriveDynamicEqThreshold(features.bandRmsDb[3], airDynamicRange, features.bandTransientMotionDb[3],
                                                  -0.4f),
                         airDynamicRange);

        float positiveEqBoostDb = 0.0f;
        for (const float gainDb : params.eqBandGains)
            positiveEqBoostDb += juce::jmax(0.0f, gainDb) * 0.35f;

        const float widthExcursionDb = juce::jmax(0.0f, params.lowMidWidth - 1.0f) * 0.25f
                                     + juce::jmax(0.0f, params.highMidWidth - 1.0f) * 0.65f
                                     + juce::jmax(0.0f, params.highWidth - 1.0f) * 0.90f;
        const float nearCeilingBiasDb = juce::jmax(0.0f, features.truePeakDb - (genreTuning.ceilingDb - 2.2f)) * 0.25f;
        const float loudnessStressDb = juce::jmax(0.0f, loudnessProxyDb - (targetProgramLoudnessDb - 1.2f)) * 0.45f;
        const float trimScale = (hotProgram || overCompressedProgram) ? 1.0f : 0.72f;
        const float proactiveTrimDb = juce::jlimit(0.0f, 2.8f,
                                                   ((positiveEqBoostDb * 0.28f)
                                                 + (widthExcursionDb * 0.38f)
                                                 + nearCeilingBiasDb
                                                 + (loudnessStressDb * 0.40f)) * trimScale);
        params.outputGain = juce::Decibels::decibelsToGain(-proactiveTrimDb);

        const float loudnessGapDb = targetProgramLoudnessDb - loudnessProxyDb;
        float desiredDriveDb = loudnessGapDb
                             + (proactiveTrimDb * 0.72f)
                             + (glueControl * 0.44f)
                             + (juce::jmax(0.0f, 9.2f - features.crestDb) * 0.07f);

        if (loudnessGapDb < 0.5f && features.truePeakDb > (genreTuning.ceilingDb - 0.8f))
            desiredDriveDb = juce::jmin(desiredDriveDb, 1.4f);
        if (hotProgram)
            desiredDriveDb = juce::jmin(desiredDriveDb, 3.8f);
        if (overCompressedProgram)
            desiredDriveDb = juce::jmin(desiredDriveDb, 3.1f);
        if (features.crestDb > 12.5f)
            desiredDriveDb = juce::jmin(desiredDriveDb, 5.2f);
        if (features.truePeakDb > -0.2f)
            desiredDriveDb = juce::jmin(desiredDriveDb, 3.4f);
        if (effectiveContext.genre == AssistantGenre::BlackMetal
            || effectiveContext.genre == AssistantGenre::BlackMetalRaw
            || effectiveContext.genre == AssistantGenre::BlackMetalExtreme
            || effectiveContext.genre == AssistantGenre::DeathMetal
            || effectiveContext.genre == AssistantGenre::TechDeath
            || effectiveContext.genre == AssistantGenre::Melodeath
            || effectiveContext.genre == AssistantGenre::BrutalDeathMetal
            || effectiveContext.genre == AssistantGenre::TrapDrill)
            desiredDriveDb = juce::jmin(desiredDriveDb, 3.8f);
        if (effectiveContext.genre == AssistantGenre::Acoustic
            || effectiveContext.genre == AssistantGenre::AcousticSolo
            || effectiveContext.genre == AssistantGenre::Chamber)
            desiredDriveDb = juce::jmin(desiredDriveDb, 2.5f);
        if (effectiveContext.genre == AssistantGenre::Orchestral
            || effectiveContext.genre == AssistantGenre::Cinematic)
            desiredDriveDb = juce::jmin(desiredDriveDb, 2.0f);

        desiredDriveDb = juce::jlimit(0.6f, 10.8f, desiredDriveDb);

        const float peakLimitedDriveDb = juce::jlimit(1.0f, 10.8f,
                                                      (-features.truePeakDb + std::abs(genreTuning.ceilingDb) - 0.35f)
                                                    + 4.9f
                                                    + proactiveTrimDb);
        params.maximizerThreshold = -juce::jmin(desiredDriveDb, peakLimitedDriveDb);
        params.maximizerCeiling = genreTuning.ceilingDb;
        if (hotProgram || overCompressedProgram)
            params.maximizerCeiling = juce::jmin(params.maximizerCeiling, genreTuning.ceilingDb - 0.10f);

        params.lowWidth = juce::jlimit(0.0f, 0.22f, params.lowWidth);
        params.lowMidWidth = juce::jlimit(0.85f, 1.42f, params.lowMidWidth);
        params.highMidWidth = juce::jlimit(0.95f, 1.58f, params.highMidWidth);
        params.highWidth = juce::jlimit(1.0f, 1.68f, params.highWidth);
        params.lowRatio = juce::jlimit(1.0f, 6.4f, params.lowRatio);
        params.lowMidRatio = juce::jlimit(1.0f, 6.0f, params.lowMidRatio);
        params.highMidRatio = juce::jlimit(1.0f, 5.2f, params.highMidRatio);
        params.highRatio = juce::jlimit(1.0f, 4.6f, params.highRatio);
        params.lowAttack = juce::jlimit(5.0f, 50.0f, params.lowAttack);
        params.lowMidAttack = juce::jlimit(5.0f, 40.0f, params.lowMidAttack);
        params.highMidAttack = juce::jlimit(2.0f, 20.0f, params.highMidAttack);
        params.highAttack = juce::jlimit(1.0f, 12.0f, params.highAttack);
        params.lowRelease = juce::jlimit(80.0f, 240.0f, params.lowRelease);
        params.lowMidRelease = juce::jlimit(70.0f, 220.0f, params.lowMidRelease);
        params.highMidRelease = juce::jlimit(60.0f, 180.0f, params.highMidRelease);
        params.highRelease = juce::jlimit(50.0f, 160.0f, params.highRelease);
        params.maximizerRelease = juce::jlimit(60.0f, 180.0f, params.maximizerRelease);
        params.maximizerMode = chooseAssistantMaximizerMode(features,
                                                            effectiveContext,
                                                            targetProgramLoudnessDb,
                                                            loudnessProxyDb,
                                                            -params.maximizerThreshold,
                                                            params.maximizerRelease,
                                                            hotProgram,
                                                            overCompressedProgram,
                                                            dynamicPotential,
                                                            headroomPotential,
                                                            problemPotential);

        return params;
    }

    MasteringParameters InferenceEngine::matchReference(const juce::AudioBuffer<float>& sourceAudio,
                                                        double sourceSampleRate,
                                                        const juce::AudioBuffer<float>& referenceAudio,
                                                        double referenceSampleRate)
    {
        auto params = predict(sourceAudio, sourceSampleRate);

        const auto sourceFeatures = extractFeatures(sourceAudio, sourceSampleRate);
        const auto referenceFeatures = extractFeatures(referenceAudio, referenceSampleRate);
        if (!sourceFeatures.valid || !referenceFeatures.valid)
            return params;

        params.usedAnalysis = true;
        const auto referenceMatch = evaluateReferenceMatch(sourceFeatures, referenceFeatures);
        const float assertiveBlend = juce::jlimit(0.98f, 1.36f,
                                                  1.02f + (referenceMatch.overallConfidence * 0.34f));
        const float sourceProgramLoudnessDb = getProgramLoudnessDb(sourceFeatures);
        const float referenceProgramLoudnessDb = getProgramLoudnessDb(referenceFeatures);
        juce::Logger::writeToLog("Reference match confidence: overall="
                                 + juce::String(referenceMatch.overallConfidence, 2)
                                 + ", tone=" + juce::String(referenceMatch.toneWeight, 2)
                                 + ", glue=" + juce::String(referenceMatch.glueWeight, 2)
                                 + ", transient=" + juce::String(referenceMatch.transientWeight, 2)
                                 + ", width=" + juce::String(referenceMatch.widthWeight, 2)
                                 + ", loudness=" + juce::String(referenceMatch.loudnessWeight, 2));

        const auto sourceNormalizedEq = normalizeEqProfile(sourceFeatures.eqBandProfileDb);
        const auto referenceNormalizedEq = normalizeEqProfile(referenceFeatures.eqBandProfileDb);
        std::array<float, eqBandCount> referenceDeltaDb {};
        for (int band = 0; band < eqBandCount; ++band)
        {
            const float normalizedDeltaDb = referenceNormalizedEq[(size_t) band]
                                          - sourceNormalizedEq[(size_t) band];
            const float relativeProfileDeltaDb = (referenceFeatures.eqBandProfileDb[(size_t) band] - referenceProgramLoudnessDb)
                                               - (sourceFeatures.eqBandProfileDb[(size_t) band] - sourceProgramLoudnessDb);
            referenceDeltaDb[(size_t) band] = juce::jlimit(-7.6f, 7.6f,
                                                           (normalizedDeltaDb * 0.18f)
                                                         + (relativeProfileDeltaDb * 0.82f));
        }

        referenceDeltaDb = smoothEqCurve(referenceDeltaDb);

        for (int band = 0; band < eqBandCount; ++band)
        {
            const int macroBand = mapEqBandToMacroBand(band);
            const float edgeScale = (band == 0 || band == eqBandCount - 1) ? 0.55f
                                  : (band == 1 || band == eqBandCount - 2) ? 0.72f
                                  : 1.0f;
            const float tonalShiftDb = juce::jlimit(-7.2f, 7.2f,
                                                    referenceDeltaDb[(size_t) band] * 1.02f * edgeScale
                                                        * referenceMatch.toneWeight * assertiveBlend);
            const float localShapeShiftDb = juce::jlimit(-2.8f, 2.8f,
                (referenceFeatures.eqDeviationDb[(size_t) band] - sourceFeatures.eqDeviationDb[(size_t) band])
                    * 0.38f * edgeScale * referenceMatch.toneWeight * assertiveBlend);
            const float transientGap = referenceFeatures.bandTransientMotionDb[(size_t) macroBand]
                                     - sourceFeatures.bandTransientMotionDb[(size_t) macroBand];
            const float transientScale = (macroBand == 0) ? 0.14f
                                       : (macroBand == 1) ? 0.22f
                                       : (macroBand == 2) ? 0.44f
                                                          : 0.36f;
            const float transientShapeShiftDb = juce::jlimit(-1.35f, 1.35f,
                transientGap * transientScale * edgeScale * referenceMatch.transientWeight
                    * juce::jlimit(0.82f, 1.24f, assertiveBlend));

            float persistenceShiftDb = 0.0f;
            const float excessGap = sourceFeatures.eqExcessPersistence[(size_t) band]
                                  - referenceFeatures.eqExcessPersistence[(size_t) band];
            if (excessGap > 0.08f)
                persistenceShiftDb -= juce::jlimit(0.0f, 2.05f,
                                                   excessGap * 2.75f * referenceMatch.toneWeight * assertiveBlend);

            const float deficitGap = sourceFeatures.eqDeficitPersistence[(size_t) band]
                                   - referenceFeatures.eqDeficitPersistence[(size_t) band];
            if (deficitGap > 0.08f)
                persistenceShiftDb += juce::jlimit(0.0f, 1.85f,
                                                   deficitGap * 2.45f * referenceMatch.toneWeight * assertiveBlend);

            params.eqBandGains[(size_t) band] = clampEqGain(params.eqBandGains[(size_t) band]
                                                          + tonalShiftDb
                                                          + localShapeShiftDb
                                                          + transientShapeShiftDb
                                                          + persistenceShiftDb);
        }

        params.eqBandGains = smoothEqCurve(params.eqBandGains);
        for (auto& gainDb : params.eqBandGains)
        {
            gainDb = clampEqGain(gainDb);
            if (std::abs(gainDb) < 0.01f)
                gainDb = 0.0f;
        }

        const auto maxPersistence = [] (const auto& values, int startBand, int endBand)
        {
            float value = 0.0f;
            for (int band = startBand; band <= endBand; ++band)
                value = juce::jmax(value, values[(size_t) band]);
            return value;
        };

        const std::array<float, dynamicEqBandCount> sourceDynamicProblem {
            maxPersistence(sourceFeatures.eqExcessPersistence, 2, 4)
                + (positiveEvidence(sourceFeatures.lowVsMidDb - referenceFeatures.lowVsMidDb, 0.18f) * 0.22f),
            maxPersistence(sourceFeatures.eqExcessPersistence, 5, 7)
                + (positiveEvidence(sourceFeatures.eqDeviationDb[5] - referenceFeatures.eqDeviationDb[5], 0.25f) * 0.22f),
            maxPersistence(sourceFeatures.eqExcessPersistence, 9, 10)
                + (positiveEvidence(sourceFeatures.eqDeviationDb[10] - referenceFeatures.eqDeviationDb[10], 0.20f) * 0.28f),
            maxPersistence(sourceFeatures.eqExcessPersistence, 11, 13)
                + (positiveEvidence(sourceFeatures.highVsMidDb - referenceFeatures.highVsMidDb, 0.15f) * 0.18f)
        };
        const std::array<float, dynamicEqBandCount> referenceDynamicProblem {
            maxPersistence(referenceFeatures.eqExcessPersistence, 2, 4),
            maxPersistence(referenceFeatures.eqExcessPersistence, 5, 7),
            maxPersistence(referenceFeatures.eqExcessPersistence, 9, 10),
            maxPersistence(referenceFeatures.eqExcessPersistence, 11, 13)
        };

        for (int band = 0; band < dynamicEqBandCount; ++band)
        {
            const int macroBand = dynamicEqMacroBands[(size_t) band];
            const float problemDelta = juce::jlimit(-1.0f, 1.6f,
                                                    sourceDynamicProblem[(size_t) band]
                                                      - referenceDynamicProblem[(size_t) band]);
            const float transientDelta = juce::jlimit(-2.0f, 2.0f,
                                                      referenceFeatures.bandTransientMotionDb[(size_t) macroBand]
                                                        - sourceFeatures.bandTransientMotionDb[(size_t) macroBand]);
            const float rangeDb = juce::jlimit(0.0f, 9.6f,
                params.dynamicEqRanges[(size_t) band]
                    + (problemDelta * 2.8f * referenceMatch.toneWeight * assertiveBlend)
                    - (juce::jmax(0.0f, transientDelta) * 0.18f * referenceMatch.transientWeight));
            const float targetBandRmsDb = pullTowards(sourceFeatures.bandRmsDb[(size_t) macroBand],
                                                      referenceFeatures.bandRmsDb[(size_t) macroBand],
                                                      0.16f);
            const float targetTransientDb = pullTowards(sourceFeatures.bandTransientMotionDb[(size_t) macroBand],
                                                        referenceFeatures.bandTransientMotionDb[(size_t) macroBand],
                                                        0.55f);
            const float thresholdBiasDb = (band <= 1) ? 0.15f : -0.35f;

            setDynamicEqBand(params, band,
                             deriveDynamicEqThreshold(targetBandRmsDb, rangeDb, targetTransientDb, thresholdBiasDb),
                             rangeDb);
        }

        const float safeReferenceCrest = juce::jmax(referenceFeatures.crestDb, 5.8f);
        const float compressionGuard = juce::jlimit(0.52f, 1.28f,
                                                    juce::jlimit(0.66f, 1.08f, (safeReferenceCrest - 4.5f) / 3.6f)
                                                        * referenceMatch.glueWeight * assertiveBlend);

        auto matchBandCompression = [&compressionGuard, &referenceMatch, assertiveBlend] (float sourceBandCrest,
                                                                                           float referenceBandCrest,
                                                                                           float sourceBandRms,
                                                                                           float referenceBandRms,
                                                                                           float sourceBandTransient,
                                                                                           float referenceBandTransient,
                                                                                           float& thresh,
                                                                                           float& ratio,
                                                                                           float& attack,
                                                                                           float& release,
                                                                                           float minAttack,
                                                                                           float maxAttack,
                                                                                           float minRelease,
                                                                                           float maxRelease,
                                                                                           float transientAttackScale,
                                                                                           float transientReleaseScale)
        {
            const float safeBandReferenceCrest = juce::jmax(referenceBandCrest, 5.4f);
            float densityNeed = ((sourceBandCrest - safeBandReferenceCrest) * 0.28f)
                              + ((referenceBandRms - sourceBandRms) * 0.11f);
            densityNeed *= compressionGuard;

            if (densityNeed > 0.0f)
            {
                thresh -= juce::jlimit(0.0f, 6.2f, densityNeed * 1.85f);
                ratio += juce::jlimit(0.0f, 1.70f, densityNeed * 0.30f);
                attack = juce::jlimit(minAttack, maxAttack, attack - (densityNeed * 3.6f));
                release = juce::jlimit(minRelease, maxRelease, release - (densityNeed * 10.5f));
            }
            else
            {
                const float opennessNeed = juce::jlimit(0.0f, 1.6f, -densityNeed);
                thresh += opennessNeed * 1.40f;
                ratio = pullTowards(ratio, 1.06f, 0.34f * opennessNeed);
                attack = juce::jlimit(minAttack, maxAttack, attack + (opennessNeed * 2.6f));
                release = juce::jlimit(minRelease, maxRelease, release + (opennessNeed * 7.2f));
            }

            const float transientNeed = juce::jlimit(-2.2f, 2.2f,
                (referenceBandTransient - sourceBandTransient)
                    * referenceMatch.transientWeight
                    * juce::jlimit(0.84f, 1.24f, assertiveBlend));

            if (transientNeed > 0.0f)
            {
                thresh += juce::jlimit(0.0f, 1.8f, transientNeed * 0.72f);
                ratio = pullTowards(ratio, 1.06f, juce::jlimit(0.0f, 0.34f, transientNeed * 0.16f));
                attack = juce::jlimit(minAttack, maxAttack, attack + (transientNeed * transientAttackScale));
                release = juce::jlimit(minRelease, maxRelease, release - (transientNeed * transientReleaseScale));
            }
            else if (transientNeed < 0.0f)
            {
                const float sustainNeed = -transientNeed;
                thresh -= juce::jlimit(0.0f, 1.6f, sustainNeed * 0.66f);
                ratio += juce::jlimit(0.0f, 0.38f, sustainNeed * 0.18f);
                attack = juce::jlimit(minAttack, maxAttack, attack - (sustainNeed * transientAttackScale * 0.60f));
                release = juce::jlimit(minRelease, maxRelease, release + (sustainNeed * transientReleaseScale * 0.74f));
            }
        };

        matchBandCompression(sourceFeatures.bandCrestDb[0], referenceFeatures.bandCrestDb[0],
                             sourceFeatures.bandRmsDb[0], referenceFeatures.bandRmsDb[0],
                             sourceFeatures.bandTransientMotionDb[0], referenceFeatures.bandTransientMotionDb[0],
                             params.lowThresh, params.lowRatio, params.lowAttack, params.lowRelease,
                             5.0f, 50.0f, 80.0f, 240.0f, 4.6f, 11.0f);
        matchBandCompression(sourceFeatures.bandCrestDb[1], referenceFeatures.bandCrestDb[1],
                             sourceFeatures.bandRmsDb[1], referenceFeatures.bandRmsDb[1],
                             sourceFeatures.bandTransientMotionDb[1], referenceFeatures.bandTransientMotionDb[1],
                             params.lowMidThresh, params.lowMidRatio, params.lowMidAttack, params.lowMidRelease,
                             5.0f, 40.0f, 70.0f, 220.0f, 4.0f, 9.0f);
        matchBandCompression(sourceFeatures.bandCrestDb[2], referenceFeatures.bandCrestDb[2],
                             sourceFeatures.bandRmsDb[2], referenceFeatures.bandRmsDb[2],
                             sourceFeatures.bandTransientMotionDb[2], referenceFeatures.bandTransientMotionDb[2],
                             params.highMidThresh, params.highMidRatio, params.highMidAttack, params.highMidRelease,
                             2.0f, 20.0f, 60.0f, 180.0f, 2.2f, 7.4f);
        matchBandCompression(sourceFeatures.bandCrestDb[3], referenceFeatures.bandCrestDb[3],
                             sourceFeatures.bandRmsDb[3], referenceFeatures.bandRmsDb[3],
                             sourceFeatures.bandTransientMotionDb[3], referenceFeatures.bandTransientMotionDb[3],
                             params.highThresh, params.highRatio, params.highAttack, params.highRelease,
                             1.0f, 12.0f, 50.0f, 160.0f, 1.5f, 5.8f);

        auto matchWidth = [&referenceMatch] (float currentWidth,
                                             float sourceSideRatio,
                                             float referenceSideRatio,
                                             float minWidth,
                                             float maxWidth,
                                             float scale)
        {
            const float widthShift = juce::jlimit(-0.36f, 0.36f,
                                                  (referenceSideRatio - sourceSideRatio) * scale * referenceMatch.widthWeight);
            return juce::jlimit(minWidth, maxWidth, currentWidth + widthShift);
        };

        params.lowWidth = matchWidth(params.lowWidth,
                                     sourceFeatures.bandSideRatio[0],
                                     referenceFeatures.bandSideRatio[0],
                                     0.0f, 0.22f, 0.30f);
        params.lowMidWidth = matchWidth(params.lowMidWidth,
                                        sourceFeatures.bandSideRatio[1],
                                        referenceFeatures.bandSideRatio[1],
                                        0.85f, 1.42f, 0.42f);
        params.highMidWidth = matchWidth(params.highMidWidth,
                                         sourceFeatures.bandSideRatio[2],
                                         referenceFeatures.bandSideRatio[2],
                                         0.95f, 1.58f, 0.58f);
        params.highWidth = matchWidth(params.highWidth,
                                      sourceFeatures.bandSideRatio[3],
                                      referenceFeatures.bandSideRatio[3],
                                      1.0f, 1.68f, 0.68f);

        const float protectedLowWidthMax = juce::jlimit(0.05f, 0.22f,
                                                        0.06f
                                                          + (sourceFeatures.bandSideRatio[0] * 0.10f)
                                                          + (0.06f * referenceMatch.lowEndSafety));
        params.lowWidth = juce::jmin(params.lowWidth, protectedLowWidthMax);

        if (referenceMatch.lowEndSafety < 0.28f)
            params.lowMidWidth = juce::jmin(params.lowMidWidth, 1.10f);

        if (referenceFeatures.stereoCorrelation < 0.12f || referenceFeatures.sideRatio > 0.76f)
        {
            params.highMidWidth = juce::jmin(params.highMidWidth, 1.12f);
            params.highWidth = juce::jmin(params.highWidth, 1.18f);
        }

        const float safeReferenceProgramLoudnessDb = juce::jlimit(-16.0f, -6.8f,
                                                                  referenceProgramLoudnessDb);
        const float loudnessDeltaDb = juce::jlimit(-4.5f, 8.2f,
                                                   (safeReferenceProgramLoudnessDb - sourceProgramLoudnessDb)
                                                     * referenceMatch.loudnessWeight * assertiveBlend);
        const float referenceTruePeakTargetDb = (referenceFeatures.truePeakDb > -0.25f)
                                              ? -0.45f
                                              : juce::jlimit(-1.1f, -0.25f,
                                                             referenceFeatures.truePeakDb - 0.10f);

        params.maximizerThreshold = juce::jlimit(-12.8f, -0.6f,
                                                 params.maximizerThreshold - (loudnessDeltaDb * 0.98f));
        params.maximizerCeiling = juce::jlimit(-1.1f, -0.25f,
                                               pullTowards(params.maximizerCeiling,
                                                           referenceTruePeakTargetDb,
                                                           0.38f + (0.78f * referenceMatch.loudnessWeight)));
        params.maximizerRelease = juce::jlimit(60.0f, 180.0f,
                                               params.maximizerRelease
                                                 - juce::jlimit(-24.0f, 24.0f,
                                                                (sourceFeatures.crestDb - safeReferenceCrest)
                                                                    * 5.0f * referenceMatch.glueWeight * assertiveBlend));

        const float overallTransientGap = juce::jlimit(-2.5f, 2.5f,
                                                       referenceFeatures.transientMotionDb
                                                         - sourceFeatures.transientMotionDb);
        if (overallTransientGap > 0.0f)
        {
            params.maximizerThreshold = juce::jlimit(-12.8f, -0.6f,
                                                     params.maximizerThreshold + (overallTransientGap * 0.34f));
            params.maximizerRelease = juce::jlimit(60.0f, 180.0f,
                                                   params.maximizerRelease + (overallTransientGap * 5.6f));
        }
        else if (overallTransientGap < 0.0f)
        {
            const float sustainGap = -overallTransientGap;
            params.maximizerThreshold = juce::jlimit(-12.8f, -0.6f,
                                                     params.maximizerThreshold - (sustainGap * 0.22f));
            params.maximizerRelease = juce::jlimit(60.0f, 180.0f,
                                                   params.maximizerRelease - (sustainGap * 3.6f));
        }

        float outputTrimDb = juce::Decibels::gainToDecibels(juce::jmax(params.outputGain, 1.0e-4f));
        outputTrimDb = juce::jlimit(-4.0f, 2.4f, outputTrimDb + (loudnessDeltaDb * 0.24f));
        params.outputGain = juce::Decibels::decibelsToGain(outputTrimDb);

        if (referenceMatch.overallConfidence < 0.22f)
        {
            params.lowRatio = pullTowards(params.lowRatio, 1.22f, 0.16f);
            params.lowMidRatio = pullTowards(params.lowMidRatio, 1.18f, 0.16f);
            params.highMidRatio = pullTowards(params.highMidRatio, 1.12f, 0.18f);
            params.highRatio = pullTowards(params.highRatio, 1.08f, 0.18f);
            params.highMidWidth = juce::jmin(params.highMidWidth, 1.22f);
            params.highWidth = juce::jmin(params.highWidth, 1.30f);
        }

        const float saturationRisk = estimateReferenceSaturationRisk(params,
                                                                     sourceFeatures,
                                                                     sourceProgramLoudnessDb,
                                                                     safeReferenceProgramLoudnessDb);
        if (saturationRisk > 0.0f)
            softenReferenceParametersForSafety(params, saturationRisk);

        params.lowRatio = juce::jlimit(1.0f, 6.4f, params.lowRatio);
        params.lowMidRatio = juce::jlimit(1.0f, 6.0f, params.lowMidRatio);
        params.highMidRatio = juce::jlimit(1.0f, 5.2f, params.highMidRatio);
        params.highRatio = juce::jlimit(1.0f, 4.6f, params.highRatio);
        params.lowThresh = juce::jlimit(-24.0f, -6.0f, params.lowThresh);
        params.lowMidThresh = juce::jlimit(-24.0f, -6.0f, params.lowMidThresh);
        params.highMidThresh = juce::jlimit(-24.0f, -6.0f, params.highMidThresh);
        params.highThresh = juce::jlimit(-24.0f, -6.0f, params.highThresh);
        params.lowWidth = juce::jlimit(0.0f, 0.22f, params.lowWidth);
        params.lowMidWidth = juce::jlimit(0.85f, 1.42f, params.lowMidWidth);
        params.highMidWidth = juce::jlimit(0.95f, 1.58f, params.highMidWidth);
        params.highWidth = juce::jlimit(1.0f, 1.68f, params.highWidth);
        params.maximizerThreshold = juce::jlimit(-12.8f, -0.6f, params.maximizerThreshold);
        params.maximizerCeiling = juce::jlimit(-1.1f, -0.25f, params.maximizerCeiling);
        params.maximizerRelease = juce::jlimit(60.0f, 180.0f, params.maximizerRelease);
        params.maximizerMode = chooseReferenceMaximizerMode(sourceFeatures,
                                                            referenceFeatures,
                                                            referenceMatch,
                                                            saturationRisk,
                                                            sourceProgramLoudnessDb,
                                                            safeReferenceProgramLoudnessDb,
                                                            -params.maximizerThreshold,
                                                            params.maximizerRelease);

        return params;
    }
}
