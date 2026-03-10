#include "InferenceEngine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <vector>

// Note: Uncomment these when ONNX Runtime is linked
// #include <onnxruntime_cxx_api.h>

namespace oxygen
{
    namespace
    {
        constexpr int eqBandCount = MasteringParameters::eqBandCount;
        constexpr float assistantEqHardLimitDb = 7.5f;
        constexpr std::array<float, eqBandCount> eqBandCenters = {
            30.0f, 40.0f, 60.0f, 100.0f,
            180.0f, 300.0f, 500.0f, 900.0f,
            1500.0f, 2500.0f, 4000.0f, 6000.0f,
            10000.0f, 15000.0f, 20000.0f
        };

        struct AnalysisFeatures
        {
            float peakDb = -100.0f;
            float truePeakDb = -100.0f;
            float rmsDb = -100.0f;
            float gatedRmsDb = -100.0f;
            float crestDb = 0.0f;
            float lowVsMidDb = 0.0f;
            float highVsMidDb = 0.0f;
            float stereoCorrelation = 1.0f;
            float sideRatio = 0.0f;
            std::array<float, eqBandCount> eqDeviationDb {};
            std::array<float, 4> bandPeakDb {};
            std::array<float, 4> bandRmsDb {};
            std::array<float, 4> bandCrestDb {};
            std::array<float, 4> bandCorrelation {};
            std::array<float, 4> bandSideRatio {};
            bool valid = false;
        };

        float gainToDbSafe(float gain)
        {
            return juce::Decibels::gainToDecibels(juce::jmax(gain, 1.0e-9f));
        }

        float meanSquareToDbSafe(double meanSquare)
        {
            return 10.0f * std::log10((float) juce::jmax(meanSquare, 1.0e-12));
        }

        float cubicInterpolate(float y0, float y1, float y2, float y3, float t)
        {
            const float a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
            const float a1 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            const float a2 = -0.5f * y0 + 0.5f * y2;
            const float a3 = y1;
            return ((a0 * t + a1) * t + a2) * t + a3;
        }

        float approximateTruePeak(const juce::AudioBuffer<float>& recentAudio, int numChannels, int numSamples)
        {
            float peak = 0.0f;

            for (int channel = 0; channel < numChannels; ++channel)
            {
                const auto* data = recentAudio.getReadPointer(channel);
                for (int sample = 0; sample < numSamples; ++sample)
                    peak = juce::jmax(peak, std::abs(data[sample]));

                for (int sample = 1; sample < numSamples - 2; ++sample)
                {
                    const float y0 = data[sample - 1];
                    const float y1 = data[sample];
                    const float y2 = data[sample + 1];
                    const float y3 = data[sample + 2];

                    peak = juce::jmax(peak, std::abs(cubicInterpolate(y0, y1, y2, y3, 0.25f)));
                    peak = juce::jmax(peak, std::abs(cubicInterpolate(y0, y1, y2, y3, 0.50f)));
                    peak = juce::jmax(peak, std::abs(cubicInterpolate(y0, y1, y2, y3, 0.75f)));
                }
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

        float clampEqGain(float gainDb)
        {
            return juce::jlimit(-assistantEqHardLimitDb, assistantEqHardLimitDb, gainDb);
        }

        void addEqGain(MasteringParameters& params, int bandIndex, float deltaDb)
        {
            if (bandIndex < 0 || bandIndex >= eqBandCount)
                return;

            auto& gain = params.eqBandGains[(size_t) bandIndex];
            gain = clampEqGain(gain + deltaDb);
        }

        float computeBandControl(float crestDb, float extraBias)
        {
            return juce::jlimit(0.0f, 1.0f, ((crestDb - 7.0f) / 7.0f) + extraBias);
        }

        float pullTowards(float value, float target, float amount)
        {
            return value + ((target - value) * amount);
        }

        AnalysisFeatures extractFeatures(const juce::AudioBuffer<float>& recentAudio, double sampleRate)
        {
            AnalysisFeatures features;
            features.bandPeakDb.fill(-100.0f);
            features.bandRmsDb.fill(-100.0f);
            features.bandCrestDb.fill(0.0f);
            features.bandCorrelation.fill(1.0f);
            features.bandSideRatio.fill(0.0f);

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
            features.truePeakDb = gainToDbSafe(approximateTruePeak(recentAudio, numChannels, numSamples));
            features.rmsDb = meanSquareToDbSafe(overallMeanSquare);
            features.gatedRmsDb = features.rmsDb;
            features.crestDb = features.truePeakDb - features.rmsDb;

            const double correlationDenominator = std::sqrt(sumL2 * sumR2);
            if (correlationDenominator > 0.0)
                features.stereoCorrelation = juce::jlimit(-1.0f, 1.0f, (float) (sumLR / correlationDenominator));

            if (sumMid2 > 1.0e-12)
                features.sideRatio = std::sqrt((float) (sumSide2 / sumMid2));

            const int loudnessBlockSize = juce::jmax(1024, juce::roundToInt(sampleRate * 0.4));
            const int loudnessStep = juce::jmax(512, loudnessBlockSize / 2);
            std::vector<double> gatedBlocks;

            for (int start = 0; start < numSamples; start += loudnessStep)
            {
                const int blockSize = juce::jmin(loudnessBlockSize, numSamples - start);
                if (blockSize < loudnessBlockSize / 2 && !gatedBlocks.empty())
                    break;

                double blockEnergy = 0.0;
                for (int sample = 0; sample < blockSize; ++sample)
                {
                    const float l = left[start + sample];
                    const float r = right[start + sample];
                    blockEnergy += (double) l * l + (double) r * r;
                }

                const double blockMeanSquare = blockEnergy / (double) (blockSize * numChannels);
                if (meanSquareToDbSafe(blockMeanSquare) > -70.0f)
                    gatedBlocks.push_back(blockMeanSquare);
            }

            if (!gatedBlocks.empty())
            {
                const double ungatedAverage = std::accumulate(gatedBlocks.begin(), gatedBlocks.end(), 0.0) / (double) gatedBlocks.size();
                const float relativeGateDb = meanSquareToDbSafe(ungatedAverage) - 10.0f;
                double gatedAverage = 0.0;
                int gatedCount = 0;

                for (const double blockMeanSquare : gatedBlocks)
                {
                    if (meanSquareToDbSafe(blockMeanSquare) >= relativeGateDb)
                    {
                        gatedAverage += blockMeanSquare;
                        ++gatedCount;
                    }
                }

                if (gatedCount > 0)
                    features.gatedRmsDb = meanSquareToDbSafe(gatedAverage / (double) gatedCount);
            }

            constexpr int fftOrder = 12;
            constexpr int fftSize = 1 << fftOrder;
            constexpr int hopSize = fftSize / 4;
            juce::dsp::FFT fft(fftOrder);
            juce::dsp::WindowingFunction<float> window(fftSize, juce::dsp::WindowingFunction<float>::hann);
            std::array<double, eqBandCount> eqBandEnergy {};
            double lowEnergy = 0.0;
            double midEnergy = 0.0;
            double highEnergy = 0.0;
            const float maxAnalysisFreq = juce::jmin(20000.0f, (float) sampleRate * 0.45f);
            const auto eqBandEdges = makeEqBandEdges(maxAnalysisFreq);
            const auto frameStarts = makeFrameStarts(numSamples, fftSize, hopSize);

            for (const int startSample : frameStarts)
            {
                std::array<float, fftSize * 2> fftData {};

                for (int i = 0; i < fftSize; ++i)
                {
                    const int sampleIndex = juce::jmin(startSample + i, numSamples - 1);
                    fftData[(size_t) i] = 0.5f * (left[sampleIndex] + right[sampleIndex]);
                }

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
                        lowEnergy += energy;
                    else if (freq < 4000.0f)
                        midEnergy += energy;
                    else
                        highEnergy += energy;

                    while (bandIndex + 1 < (size_t) eqBandCount && freq >= eqBandEdges[bandIndex + 1])
                        ++bandIndex;

                    if (freq >= eqBandEdges[bandIndex] && freq < eqBandEdges[bandIndex + 1])
                        eqBandEnergy[bandIndex] += energy;
                }
            }

            const float lowDb = meanSquareToDbSafe(lowEnergy / (double) juce::jmax(1, (int) frameStarts.size()));
            const float midDb = meanSquareToDbSafe(midEnergy / (double) juce::jmax(1, (int) frameStarts.size()));
            const float highDb = meanSquareToDbSafe(highEnergy / (double) juce::jmax(1, (int) frameStarts.size()));
            features.lowVsMidDb = lowDb - midDb;
            features.highVsMidDb = highDb - midDb;

            std::array<float, eqBandCount> bandDb {};
            for (int band = 0; band < eqBandCount; ++band)
                bandDb[(size_t) band] = meanSquareToDbSafe(eqBandEnergy[(size_t) band] / (double) juce::jmax(1, (int) frameStarts.size()));

            auto smoothedBandDb = bandDb;
            for (int pass = 0; pass < 3; ++pass)
            {
                const auto previous = smoothedBandDb;
                for (int band = 0; band < eqBandCount; ++band)
                {
                    if (band == 0)
                        smoothedBandDb[(size_t) band] = 0.75f * previous[(size_t) band] + 0.25f * previous[1];
                    else if (band == eqBandCount - 1)
                        smoothedBandDb[(size_t) band] = 0.75f * previous[(size_t) band] + 0.25f * previous[(size_t) band - 1];
                    else
                        smoothedBandDb[(size_t) band] = 0.2f * previous[(size_t) band - 1]
                                                     + 0.6f * previous[(size_t) band]
                                                     + 0.2f * previous[(size_t) band + 1];
                }
            }

            for (int band = 0; band < eqBandCount; ++band)
                features.eqDeviationDb[(size_t) band] = bandDb[(size_t) band] - smoothedBandDb[(size_t) band];

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

        float scoreGenrePattern(const AnalysisFeatures& features, const GenrePattern& pattern)
        {
            float score = 0.0f;
            score += normalizedDifference(features.lowVsMidDb, pattern.lowVsMidDb, 3.0f) * 1.15f;
            score += normalizedDifference(features.highVsMidDb, pattern.highVsMidDb, 3.0f) * 1.25f;
            score += normalizedDifference(features.crestDb, pattern.crestDb, 5.0f) * 1.10f;
            score += normalizedDifference(features.gatedRmsDb, pattern.gatedRmsDb, 4.5f) * 1.20f;
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
            constexpr std::array<GenrePattern, 10> genrePatterns {{
                { AssistantGenre::Pop,        1.0f,  1.4f,  8.6f, -10.2f, 0.24f, 0.84f, { 0.03f, 0.11f, 0.23f, 0.36f }, { 7.2f, 6.9f, 6.8f, 6.6f } },
                { AssistantGenre::HipHop,     3.2f, -0.2f,  8.0f,  -9.3f, 0.16f, 0.90f, { 0.02f, 0.07f, 0.14f, 0.21f }, { 7.8f, 7.3f, 6.9f, 6.4f } },
                { AssistantGenre::Trap,       3.8f,  0.6f,  7.3f,  -8.6f, 0.22f, 0.86f, { 0.02f, 0.08f, 0.17f, 0.24f }, { 7.4f, 7.0f, 6.5f, 6.0f } },
                { AssistantGenre::Electronic, 2.1f,  2.2f,  7.4f,  -8.8f, 0.34f, 0.70f, { 0.03f, 0.14f, 0.34f, 0.52f }, { 7.0f, 6.6f, 6.3f, 5.9f } },
                { AssistantGenre::Rock,       1.5f,  0.8f, 10.2f, -11.4f, 0.21f, 0.80f, { 0.03f, 0.10f, 0.22f, 0.30f }, { 8.6f, 8.2f, 7.9f, 7.2f } },
                { AssistantGenre::BlackMetal, 0.9f,  2.7f, 11.2f, -10.7f, 0.19f, 0.72f, { 0.02f, 0.09f, 0.20f, 0.28f }, { 9.3f, 8.9f, 8.4f, 7.8f } },
                { AssistantGenre::DeathMetal, 1.8f,  2.0f,  9.3f,  -9.7f, 0.15f, 0.80f, { 0.01f, 0.06f, 0.15f, 0.22f }, { 8.7f, 8.4f, 7.9f, 7.2f } },
                { AssistantGenre::BrutalDeathMetal, 2.5f, 2.4f, 7.6f, -8.5f, 0.12f, 0.85f, { 0.01f, 0.05f, 0.11f, 0.18f }, { 7.3f, 7.1f, 6.6f, 6.1f } },
                { AssistantGenre::Acoustic,  -0.8f,  0.7f, 13.8f, -16.8f, 0.11f, 0.93f, { 0.01f, 0.05f, 0.10f, 0.16f }, { 11.4f, 10.8f, 10.0f, 9.0f } },
                { AssistantGenre::Orchestral,-0.6f,  0.4f, 17.0f, -20.6f, 0.09f, 0.96f, { 0.01f, 0.04f, 0.09f, 0.14f }, { 14.0f, 13.5f, 12.8f, 11.7f } }
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

            if (genre == AssistantGenre::Acoustic || genre == AssistantGenre::Orchestral)
                return hotProgram ? ArtisticDirection::Balanced : ArtisticDirection::Transparent;

            if (hotProgram || overCompressedProgram)
                return ArtisticDirection::Transparent;

            if (genre == AssistantGenre::BlackMetal
                || genre == AssistantGenre::DeathMetal
                || genre == AssistantGenre::BrutalDeathMetal)
                return ArtisticDirection::Aggressive;

            if (tooBright && genre != AssistantGenre::Electronic)
                return ArtisticDirection::Warm;

            if (narrowImage && !wideImage)
                return ArtisticDirection::Wide;

            if (dynamicProgram && (genre == AssistantGenre::Rock
                                || genre == AssistantGenre::HipHop
                                || genre == AssistantGenre::Trap))
                return ArtisticDirection::Punchy;

            if (darkProgram && features.gatedRmsDb < -13.0f)
                return ArtisticDirection::Aggressive;

            return ArtisticDirection::Balanced;
        }

        juce::String genreToString(AssistantGenre genre)
        {
            switch (genre)
            {
                case AssistantGenre::Universal: return "Universal";
                case AssistantGenre::Pop: return "Pop";
                case AssistantGenre::HipHop: return "HipHop";
                case AssistantGenre::Trap: return "Trap";
                case AssistantGenre::Electronic: return "Electronic";
                case AssistantGenre::Rock: return "Rock";
                case AssistantGenre::BlackMetal: return "BlackMetal";
                case AssistantGenre::DeathMetal: return "DeathMetal";
                case AssistantGenre::BrutalDeathMetal: return "BrutalDeathMetal";
                case AssistantGenre::Acoustic: return "Acoustic";
                case AssistantGenre::Orchestral: return "Orchestral";
            }

            return "Universal";
        }

        juce::String directionToString(ArtisticDirection direction)
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

        float computeLoudnessProxyDb(const AnalysisFeatures& features)
        {
            const float lowMaskingPenalty = juce::jmax(0.0f, features.lowVsMidDb - 1.0f) * 0.22f;
            const float darkBalancePenalty = juce::jmax(0.0f, -features.highVsMidDb - 1.2f) * 0.18f;
            const float presenceLift = juce::jmax(0.0f, juce::jmin(features.highVsMidDb, 2.5f)) * 0.10f;
            return features.gatedRmsDb - lowMaskingPenalty - darkBalancePenalty + presenceLift;
        }

        GenreTuning getGenreTuning(AssistantGenre genre)
        {
            switch (genre)
            {
                case AssistantGenre::Pop:
                    return { -9.0f, 0.10f, 0.18f, 0.24f, 0.12f, 0.12f, 1.05f, 1.05f, 0.00f, -0.65f };
                case AssistantGenre::HipHop:
                    return { -8.8f, 0.36f, 0.05f, -0.06f, -0.08f, 0.18f, 1.00f, 1.15f, 0.08f, -0.80f };
                case AssistantGenre::Trap:
                    return { -8.2f, 0.46f, 0.10f, 0.08f, 0.00f, 0.24f, 1.00f, 1.12f, 0.05f, -0.75f };
                case AssistantGenre::Electronic:
                    return { -8.4f, 0.14f, 0.12f, 0.30f, 0.22f, 0.18f, 1.08f, 1.14f, -0.05f, -0.65f };
                case AssistantGenre::Rock:
                    return { -9.6f, 0.12f, 0.24f, 0.04f, -0.04f, 0.24f, 0.98f, 1.05f, 0.14f, -0.80f };
                case AssistantGenre::BlackMetal:
                    return { -10.1f, 0.06f, 0.10f, 0.00f, -0.08f, 0.14f, 0.95f, 1.28f, 0.26f, -0.90f };
                case AssistantGenre::DeathMetal:
                    return { -8.9f, 0.18f, 0.20f, -0.04f, -0.10f, 0.30f, 0.92f, 1.16f, 0.18f, -0.82f };
                case AssistantGenre::BrutalDeathMetal:
                    return { -8.4f, 0.25f, 0.18f, -0.10f, -0.16f, 0.38f, 0.88f, 1.22f, 0.10f, -0.85f };
                case AssistantGenre::Acoustic:
                    return { -13.2f, 0.00f, 0.10f, 0.14f, 0.04f, -0.25f, 1.20f, 0.95f, 0.40f, -1.00f };
                case AssistantGenre::Orchestral:
                    return { -17.0f, 0.00f, 0.06f, 0.10f, 0.02f, -0.40f, 1.25f, 1.00f, 0.65f, -1.30f };
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
            juce::Logger::writeToLog("Master Assistant auto context: genre="
                                     + genreToString(effectiveContext.genre)
                                     + ", direction=" + directionToString(effectiveContext.direction));
        }

        const auto genreTuning = getGenreTuning(effectiveContext.genre);
        const auto directionTuning = getDirectionTuning(effectiveContext.direction);
        const float loudnessProxyDb = computeLoudnessProxyDb(features);
        const float targetProgramLoudnessDb = genreTuning.targetLoudnessDb + directionTuning.loudnessBiasDb;

        const float mudExcess = juce::jmax(features.eqDeviationDb[4], features.eqDeviationDb[5], features.eqDeviationDb[6]);
        const float harshExcess = juce::jmax(features.eqDeviationDb[9], features.eqDeviationDb[10], features.eqDeviationDb[11]);
        const float presenceDip = juce::jmax(0.0f, -(features.eqDeviationDb[8] + features.eqDeviationDb[9]) * 0.5f);

        const float lowOverhang = positiveEvidence(features.lowVsMidDb,
                                                   1.8f - (genreTuning.lowBias * 0.60f) - (directionTuning.warmthBias * 0.20f));
        const float lowDeficit = positiveEvidence(-features.lowVsMidDb,
                                                  1.9f + (genreTuning.lowBias * 0.35f));
        const float topOverhang = positiveEvidence(features.highVsMidDb,
                                                   1.9f - (genreTuning.airBias * 0.20f));
        const float topDeficit = positiveEvidence(-features.highVsMidDb,
                                                  1.6f + (directionTuning.warmthBias * 0.45f));
        const float mudEvidence = positiveEvidence(mudExcess, genreTuning.mudToleranceDb)
                                * juce::jlimit(0.55f, 1.30f, 0.82f + (0.07f * features.lowVsMidDb));
        const float harshEvidence = positiveEvidence(harshExcess, genreTuning.harshToleranceDb)
                                  * juce::jlimit(0.55f, 1.30f, 0.84f + (0.07f * features.highVsMidDb));
        const float presenceOpportunity = positiveEvidence(presenceDip,
                                                           0.85f - (genreTuning.presenceBias * 0.35f))
                                        * juce::jlimit(0.20f, 1.25f,
                                                       1.05f - (0.30f * topOverhang) - (0.25f * harshEvidence));
        const float airOpportunity = positiveEvidence(topDeficit, 0.25f)
                                   * juce::jlimit(0.15f, 1.20f, 1.0f - (0.40f * harshEvidence));
        const float lowPunchOpportunity = positiveEvidence(lowDeficit, 0.30f)
                                        * juce::jlimit(0.15f, 1.20f, 1.0f - (0.55f * mudEvidence));
        const float tonalImbalanceScore = (0.45f * std::abs(features.lowVsMidDb))
                                        + (0.50f * std::abs(features.highVsMidDb));
        const float problemSeverity = (0.80f * mudEvidence)
                                    + (0.80f * harshEvidence)
                                    + (0.35f * lowOverhang)
                                    + (0.30f * topOverhang);
        const float enhancementSeverity = (0.65f * lowPunchOpportunity)
                                        + (0.80f * presenceOpportunity)
                                        + (0.75f * airOpportunity);
        const float eqSeverity = tonalImbalanceScore + problemSeverity + enhancementSeverity;
        const bool mostlyBalanced = eqSeverity < 1.65f;
        const bool overCompressedProgram = features.crestDb < (6.5f - (0.45f * genreTuning.transientTolerance));
        const bool hotProgram = loudnessProxyDb > (targetProgramLoudnessDb - 0.9f)
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

            addEqGain(params, band, juce::jlimit(0.0f, maxBoost, (0.16f + (hole * scale)) * driver));
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
            const float cutDb = juce::jlimit(0.0f, 1.45f, resonanceEvidence * 0.42f * tonalGuard);
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

        params.lowWidth = juce::jlimit(0.0f, 0.25f, 0.16f - (features.bandSideRatio[0] * 0.18f));
        params.lowMidWidth = (features.bandCorrelation[1] > 0.90f && features.bandSideRatio[1] < 0.12f) ? 1.04f
                          : (features.bandCorrelation[1] < 0.45f || features.bandSideRatio[1] > 0.45f) ? 0.96f
                          : 1.00f;
        params.lowMidWidth += 0.04f * widthBias;

        if (features.bandCorrelation[2] > 0.88f && features.bandSideRatio[2] < 0.18f)
            params.highMidWidth = 1.16f;
        else if (features.bandCorrelation[2] < 0.25f || features.bandSideRatio[2] > 0.70f)
            params.highMidWidth = 1.00f;
        else
            params.highMidWidth = 1.08f;
        params.highMidWidth += 0.10f * widthBias;

        if (features.bandCorrelation[3] > 0.86f && features.bandSideRatio[3] < 0.18f)
            params.highWidth = 1.28f;
        else if (features.bandCorrelation[3] < 0.20f || features.bandSideRatio[3] > 0.75f)
            params.highWidth = 1.02f;
        else
            params.highWidth = 1.14f;
        params.highWidth += 0.14f * widthBias;

        if (features.stereoCorrelation < 0.15f || features.sideRatio > 0.70f)
        {
            params.highMidWidth = juce::jmin(params.highMidWidth, 1.08f);
            params.highWidth = juce::jmin(params.highWidth, 1.12f);
        }

        if (hotProgram)
        {
            params.lowMidWidth = juce::jmin(params.lowMidWidth, 1.04f);
            params.highMidWidth = juce::jmin(params.highMidWidth, 1.14f);
            params.highWidth = juce::jmin(params.highWidth, 1.22f);
        }

        params.maximizerRelease = (features.crestDb > 12.0f) ? 98.0f
                                 : (features.crestDb < 8.0f) ? 145.0f
                                 : 118.0f;
        params.maximizerRelease -= 10.0f * juce::jlimit(-0.30f, 0.60f, genreTuning.glueBias + directionTuning.glueBias);

        if (hotProgram || overCompressedProgram)
            params.maximizerRelease = juce::jmax(params.maximizerRelease, 130.0f);

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

        float positiveEqBoostDb = 0.0f;
        for (const float gainDb : params.eqBandGains)
            positiveEqBoostDb += juce::jmax(0.0f, gainDb) * 0.35f;

        const float widthExcursionDb = juce::jmax(0.0f, params.lowMidWidth - 1.0f) * 0.25f
                                     + juce::jmax(0.0f, params.highMidWidth - 1.0f) * 0.65f
                                     + juce::jmax(0.0f, params.highWidth - 1.0f) * 0.90f;
        const float nearCeilingBiasDb = juce::jmax(0.0f, features.truePeakDb - (genreTuning.ceilingDb - 2.2f)) * 0.25f;
        const float loudnessStressDb = juce::jmax(0.0f, loudnessProxyDb - (targetProgramLoudnessDb - 1.2f)) * 0.45f;
        const float trimScale = (hotProgram || overCompressedProgram) ? 1.0f : 0.72f;
        const float proactiveTrimDb = juce::jlimit(0.0f, 3.2f,
                                                   ((positiveEqBoostDb * 0.30f)
                                                 + (widthExcursionDb * 0.45f)
                                                 + nearCeilingBiasDb
                                                 + loudnessStressDb) * trimScale);
        params.outputGain = juce::Decibels::decibelsToGain(-proactiveTrimDb);

        const float loudnessGapDb = targetProgramLoudnessDb - loudnessProxyDb;
        float desiredDriveDb = loudnessGapDb
                             + (proactiveTrimDb * 0.80f)
                             + (glueControl * 0.55f)
                             + (juce::jmax(0.0f, 9.0f - features.crestDb) * 0.08f);

        if (loudnessGapDb < 0.5f && features.truePeakDb > (genreTuning.ceilingDb - 0.8f))
            desiredDriveDb = juce::jmin(desiredDriveDb, 1.6f);
        if (hotProgram)
            desiredDriveDb = juce::jmin(desiredDriveDb, 4.2f);
        if (overCompressedProgram)
            desiredDriveDb = juce::jmin(desiredDriveDb, 3.4f);
        if (features.crestDb > 12.5f)
            desiredDriveDb = juce::jmin(desiredDriveDb, 5.6f);
        if (features.truePeakDb > -0.2f)
            desiredDriveDb = juce::jmin(desiredDriveDb, 3.8f);
        if (effectiveContext.genre == AssistantGenre::BlackMetal)
            desiredDriveDb = juce::jmin(desiredDriveDb, 4.0f);
        if (effectiveContext.genre == AssistantGenre::Acoustic)
            desiredDriveDb = juce::jmin(desiredDriveDb, 2.8f);
        if (effectiveContext.genre == AssistantGenre::Orchestral)
            desiredDriveDb = juce::jmin(desiredDriveDb, 2.2f);

        desiredDriveDb = juce::jlimit(0.6f, 11.5f, desiredDriveDb);

        const float peakLimitedDriveDb = juce::jlimit(1.0f, 11.5f,
                                                      (-features.truePeakDb + std::abs(genreTuning.ceilingDb) - 0.4f)
                                                    + 5.5f
                                                    + proactiveTrimDb);
        params.maximizerThreshold = -juce::jmin(desiredDriveDb, peakLimitedDriveDb);
        params.maximizerCeiling = genreTuning.ceilingDb;
        if (hotProgram || overCompressedProgram)
            params.maximizerCeiling = juce::jmin(params.maximizerCeiling, genreTuning.ceilingDb - 0.10f);

        params.lowWidth = juce::jlimit(0.0f, 0.25f, params.lowWidth);
        params.lowMidWidth = juce::jlimit(0.80f, 1.55f, params.lowMidWidth);
        params.highMidWidth = juce::jlimit(0.95f, 1.80f, params.highMidWidth);
        params.highWidth = juce::jlimit(1.0f, 1.95f, params.highWidth);
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

        return params;
    }
}
