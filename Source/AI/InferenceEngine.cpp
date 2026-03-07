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
            return juce::jlimit(-2.5f, 2.5f, gainDb);
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

        const float mudExcess = juce::jmax(features.eqDeviationDb[4], features.eqDeviationDb[5], features.eqDeviationDb[6]);
        const float harshExcess = juce::jmax(features.eqDeviationDb[9], features.eqDeviationDb[10], features.eqDeviationDb[11]);
        const float presenceDip = juce::jmax(0.0f, -(features.eqDeviationDb[8] + features.eqDeviationDb[9]) * 0.5f);

        if (features.lowVsMidDb > 4.0f)
        {
            addEqGain(params, 1, -0.8f);
            addEqGain(params, 2, -1.2f);
            addEqGain(params, 3, -1.0f);
            addEqGain(params, 4, -0.6f);
            addEqGain(params, 5, -0.8f);
        }
        else if (features.lowVsMidDb > 2.0f)
        {
            addEqGain(params, 2, -0.6f);
            addEqGain(params, 3, -0.5f);
            addEqGain(params, 5, -0.5f);
        }
        else if (features.lowVsMidDb < -4.0f)
        {
            addEqGain(params, 1, 0.5f);
            addEqGain(params, 2, 1.0f);
            addEqGain(params, 3, 0.8f);
            addEqGain(params, 4, 0.3f);
        }
        else if (features.lowVsMidDb < -2.0f)
        {
            addEqGain(params, 2, 0.5f);
            addEqGain(params, 3, 0.4f);
        }

        if (features.highVsMidDb < -6.0f)
        {
            addEqGain(params, 11, 0.3f);
            addEqGain(params, 12, 0.9f);
            addEqGain(params, 13, 1.1f);
            addEqGain(params, 14, 0.5f);
        }
        else if (features.highVsMidDb < -3.0f)
        {
            addEqGain(params, 12, 0.5f);
            addEqGain(params, 13, 0.8f);
            addEqGain(params, 14, 0.3f);
        }
        else if (features.highVsMidDb > 4.0f)
        {
            addEqGain(params, 10, -0.5f);
            addEqGain(params, 11, -0.6f);
            addEqGain(params, 12, -0.5f);
        }
        else if (features.highVsMidDb > 2.0f)
        {
            addEqGain(params, 11, -0.3f);
            addEqGain(params, 12, -0.3f);
        }

        for (int band = 0; band < eqBandCount; ++band)
        {
            const float centerFreq = eqBandCenters[(size_t) band];
            const float excess = features.eqDeviationDb[(size_t) band];

            if (excess > 2.4f && centerFreq >= 100.0f && centerFreq <= 8000.0f)
                addEqGain(params, band, -juce::jlimit(0.2f, 1.6f, (excess - 2.4f) * 0.45f));
        }

        if (mudExcess > 1.5f)
        {
            addEqGain(params, 5, -0.5f);
            addEqGain(params, 6, -0.3f);
        }

        if (harshExcess > 1.5f)
        {
            addEqGain(params, 9, -0.3f);
            addEqGain(params, 10, -0.5f);
            addEqGain(params, 11, -0.3f);
        }

        if (presenceDip > 1.0f && features.highVsMidDb < 1.0f)
        {
            addEqGain(params, 8, 0.3f);
            addEqGain(params, 9, 0.4f);
        }

        params.lowMidX = (features.lowVsMidDb > 3.0f) ? 140.0f
                        : (features.lowVsMidDb < -3.0f) ? 100.0f
                        : 120.0f;
        params.midHighX = (harshExcess > 2.0f) ? 1200.0f
                         : (presenceDip > 1.5f) ? 1600.0f
                         : 1400.0f;
        params.highX = (features.highVsMidDb > 3.0f) ? 6500.0f
                     : (features.highVsMidDb < -3.0f) ? 5500.0f
                     : 6000.0f;

        const float lowControl = computeBandControl(features.bandCrestDb[0], juce::jmax(0.0f, features.lowVsMidDb - 1.5f) / 5.0f);
        const float lowMidControl = computeBandControl(features.bandCrestDb[1], juce::jmax(0.0f, mudExcess - 1.0f) / 4.0f);
        const float highMidControl = computeBandControl(features.bandCrestDb[2], juce::jmax(0.0f, harshExcess - 1.0f) / 4.0f);
        const float highControl = computeBandControl(features.bandCrestDb[3], juce::jmax(0.0f, features.highVsMidDb - 2.0f) / 6.0f);

        params.lowThresh = -14.0f - (5.0f * lowControl);
        params.lowRatio = 1.20f + (0.55f * lowControl);
        params.lowAttack = 34.0f - (12.0f * lowControl);
        params.lowRelease = 190.0f - (35.0f * lowControl);

        params.lowMidThresh = -13.0f - (4.5f * lowMidControl);
        params.lowMidRatio = 1.20f + (0.45f * lowMidControl);
        params.lowMidAttack = 24.0f - (8.0f * lowMidControl);
        params.lowMidRelease = 155.0f - (20.0f * lowMidControl);

        params.highMidThresh = -12.5f - (4.0f * highMidControl);
        params.highMidRatio = 1.15f + (0.35f * highMidControl);
        params.highMidAttack = 10.0f - (4.0f * highMidControl);
        params.highMidRelease = 125.0f - (18.0f * highMidControl);

        params.highThresh = -11.5f - (3.5f * highControl);
        params.highRatio = 1.10f + (0.30f * highControl);
        params.highAttack = 6.0f - (2.0f * highControl);
        params.highRelease = 105.0f - (12.0f * highControl);

        params.lowWidth = juce::jlimit(0.0f, 0.25f, 0.16f - (features.bandSideRatio[0] * 0.18f));
        params.lowMidWidth = (features.bandCorrelation[1] > 0.90f && features.bandSideRatio[1] < 0.12f) ? 1.04f
                          : (features.bandCorrelation[1] < 0.45f || features.bandSideRatio[1] > 0.45f) ? 0.96f
                          : 1.00f;

        if (features.bandCorrelation[2] > 0.88f && features.bandSideRatio[2] < 0.18f)
            params.highMidWidth = 1.22f;
        else if (features.bandCorrelation[2] < 0.25f || features.bandSideRatio[2] > 0.70f)
            params.highMidWidth = 1.02f;
        else
            params.highMidWidth = 1.10f;

        if (features.bandCorrelation[3] > 0.86f && features.bandSideRatio[3] < 0.18f)
            params.highWidth = 1.38f;
        else if (features.bandCorrelation[3] < 0.20f || features.bandSideRatio[3] > 0.75f)
            params.highWidth = 1.05f;
        else
            params.highWidth = 1.18f;

        params.maximizerRelease = (features.crestDb > 12.0f) ? 100.0f
                                 : (features.crestDb < 8.0f) ? 150.0f
                                 : 120.0f;

        float programRmsBiasDb = 0.0f;

        switch (context.genre)
        {
            case AssistantGenre::Pop:
                addEqGain(params, 8, 0.15f);
                addEqGain(params, 12, 0.25f);
                params.highMidWidth = juce::jmax(params.highMidWidth, 1.14f);
                params.highWidth = juce::jmax(params.highWidth, 1.24f);
                programRmsBiasDb = 0.4f;
                break;

            case AssistantGenre::HipHop:
                addEqGain(params, 2, 0.35f);
                addEqGain(params, 5, -0.15f);
                addEqGain(params, 12, -0.10f);
                params.lowWidth = juce::jmin(params.lowWidth, 0.06f);
                params.highWidth = juce::jmin(params.highWidth, 1.14f);
                params.lowRatio += 0.08f;
                params.lowMidRatio += 0.05f;
                programRmsBiasDb = 0.9f;
                break;

            case AssistantGenre::Electronic:
                addEqGain(params, 2, 0.20f);
                addEqGain(params, 11, 0.20f);
                addEqGain(params, 12, 0.30f);
                params.highMidWidth = juce::jmax(params.highMidWidth, 1.16f);
                params.highWidth = juce::jmax(params.highWidth, 1.30f);
                params.maximizerRelease = juce::jmax(85.0f, params.maximizerRelease - 10.0f);
                programRmsBiasDb = 1.1f;
                break;

            case AssistantGenre::Rock:
                addEqGain(params, 8, 0.25f);
                addEqGain(params, 9, 0.20f);
                addEqGain(params, 11, -0.10f);
                params.highWidth = juce::jmin(params.highWidth, 1.16f);
                params.lowMidRatio += 0.06f;
                params.highMidRatio += 0.05f;
                programRmsBiasDb = 0.3f;
                break;

            case AssistantGenre::Acoustic:
                params.lowRatio = pullTowards(params.lowRatio, 1.10f, 0.75f);
                params.lowMidRatio = pullTowards(params.lowMidRatio, 1.10f, 0.75f);
                params.highMidRatio = pullTowards(params.highMidRatio, 1.08f, 0.75f);
                params.highRatio = pullTowards(params.highRatio, 1.05f, 0.75f);
                params.highMidWidth = juce::jmin(params.highMidWidth, 1.08f);
                params.highWidth = juce::jmin(params.highWidth, 1.10f);
                addEqGain(params, 12, 0.10f);
                programRmsBiasDb = -1.8f;
                break;

            case AssistantGenre::Orchestral:
                params.lowRatio = pullTowards(params.lowRatio, 1.05f, 0.85f);
                params.lowMidRatio = pullTowards(params.lowMidRatio, 1.05f, 0.85f);
                params.highMidRatio = pullTowards(params.highMidRatio, 1.04f, 0.85f);
                params.highRatio = pullTowards(params.highRatio, 1.03f, 0.85f);
                params.highMidWidth = juce::jmin(params.highMidWidth, 1.04f);
                params.highWidth = juce::jmin(params.highWidth, 1.06f);
                params.maximizerRelease = juce::jmax(params.maximizerRelease, 140.0f);
                programRmsBiasDb = -3.5f;
                break;

            case AssistantGenre::Universal:
                break;
        }

        switch (context.direction)
        {
            case ArtisticDirection::Transparent:
                for (auto& gainDb : params.eqBandGains)
                    gainDb *= 0.70f;
                params.lowRatio = pullTowards(params.lowRatio, 1.05f, 0.65f);
                params.lowMidRatio = pullTowards(params.lowMidRatio, 1.05f, 0.65f);
                params.highMidRatio = pullTowards(params.highMidRatio, 1.04f, 0.65f);
                params.highRatio = pullTowards(params.highRatio, 1.03f, 0.65f);
                params.lowWidth = pullTowards(params.lowWidth, 0.10f, 0.5f);
                params.lowMidWidth = pullTowards(params.lowMidWidth, 1.0f, 0.7f);
                params.highMidWidth = pullTowards(params.highMidWidth, 1.03f, 0.6f);
                params.highWidth = pullTowards(params.highWidth, 1.06f, 0.6f);
                params.maximizerRelease = juce::jmax(params.maximizerRelease, 140.0f);
                programRmsBiasDb -= 1.4f;
                break;

            case ArtisticDirection::Warm:
                addEqGain(params, 4, 0.20f);
                addEqGain(params, 5, 0.15f);
                addEqGain(params, 12, -0.25f);
                addEqGain(params, 13, -0.20f);
                params.highMidWidth = juce::jmin(params.highMidWidth, 1.10f);
                params.highWidth = juce::jmin(params.highWidth, 1.14f);
                params.maximizerRelease = juce::jmax(params.maximizerRelease, 130.0f);
                break;

            case ArtisticDirection::Punchy:
                params.lowAttack = juce::jmin(45.0f, params.lowAttack + 6.0f);
                params.lowMidAttack = juce::jmin(32.0f, params.lowMidAttack + 4.0f);
                params.lowRelease = juce::jmin(210.0f, params.lowRelease + 10.0f);
                params.lowMidRelease = juce::jmin(180.0f, params.lowMidRelease + 8.0f);
                params.highMidRatio = juce::jmax(1.12f, params.highMidRatio - 0.04f);
                programRmsBiasDb += 0.2f;
                break;

            case ArtisticDirection::Wide:
                params.highMidWidth = juce::jmax(params.highMidWidth, 1.18f);
                params.highWidth = juce::jmax(params.highWidth, 1.32f);
                params.lowWidth = juce::jmin(params.lowWidth, 0.08f);
                addEqGain(params, 12, 0.15f);
                break;

            case ArtisticDirection::Aggressive:
                addEqGain(params, 9, 0.10f);
                addEqGain(params, 12, 0.20f);
                params.lowRatio += 0.10f;
                params.lowMidRatio += 0.10f;
                params.highMidRatio += 0.08f;
                params.highRatio += 0.06f;
                params.maximizerRelease = juce::jmax(85.0f, params.maximizerRelease - 20.0f);
                programRmsBiasDb += 1.4f;
                break;

            case ArtisticDirection::Balanced:
                break;
        }

        float positiveEqBoostDb = 0.0f;
        for (const float gainDb : params.eqBandGains)
            positiveEqBoostDb += juce::jmax(0.0f, gainDb) * 0.35f;

        const float widthExcursionDb = juce::jmax(0.0f, params.lowMidWidth - 1.0f) * 0.25f
                                     + juce::jmax(0.0f, params.highMidWidth - 1.0f) * 0.65f
                                     + juce::jmax(0.0f, params.highWidth - 1.0f) * 0.90f;
        const float nearCeilingBiasDb = juce::jmax(0.0f, features.truePeakDb + 3.0f) * 0.35f;
        const float proactiveTrimDb = juce::jlimit(0.0f, 1.5f,
                                                   (positiveEqBoostDb * 0.45f)
                                                 + (widthExcursionDb * 0.80f)
                                                 + nearCeilingBiasDb);
        params.outputGain = juce::Decibels::decibelsToGain(-proactiveTrimDb);

        float targetProgramRmsDb = -12.5f;
        if (features.crestDb > 13.0f)
            targetProgramRmsDb = -14.0f;
        else if (features.crestDb < 8.0f)
            targetProgramRmsDb = -11.5f;

        targetProgramRmsDb += programRmsBiasDb;

        float desiredDriveDb = juce::jlimit(0.8f, 6.0f, targetProgramRmsDb - features.gatedRmsDb);
        if (features.gatedRmsDb > -12.0f && features.truePeakDb > -1.5f)
            desiredDriveDb = 0.8f;

        desiredDriveDb = juce::jlimit(0.8f, 6.5f, desiredDriveDb + (proactiveTrimDb * 0.5f));

        const float peakLimitedDriveDb = juce::jlimit(0.8f, 6.5f, (-features.truePeakDb - 1.0f) + 3.0f + (proactiveTrimDb * 0.75f));
        params.maximizerThreshold = -juce::jmin(desiredDriveDb, peakLimitedDriveDb);
        params.maximizerCeiling = -1.0f;

        params.lowWidth = juce::jlimit(0.0f, 0.25f, params.lowWidth);
        params.lowMidWidth = juce::jlimit(0.80f, 1.20f, params.lowMidWidth);
        params.highMidWidth = juce::jlimit(0.95f, 1.35f, params.highMidWidth);
        params.highWidth = juce::jlimit(1.0f, 1.50f, params.highWidth);
        params.lowRatio = juce::jlimit(1.0f, 3.0f, params.lowRatio);
        params.lowMidRatio = juce::jlimit(1.0f, 3.0f, params.lowMidRatio);
        params.highMidRatio = juce::jlimit(1.0f, 2.5f, params.highMidRatio);
        params.highRatio = juce::jlimit(1.0f, 2.0f, params.highRatio);
        params.lowAttack = juce::jlimit(5.0f, 50.0f, params.lowAttack);
        params.lowMidAttack = juce::jlimit(5.0f, 40.0f, params.lowMidAttack);
        params.highMidAttack = juce::jlimit(2.0f, 20.0f, params.highMidAttack);
        params.highAttack = juce::jlimit(1.0f, 12.0f, params.highAttack);
        params.lowRelease = juce::jlimit(80.0f, 240.0f, params.lowRelease);
        params.lowMidRelease = juce::jlimit(70.0f, 220.0f, params.lowMidRelease);
        params.highMidRelease = juce::jlimit(60.0f, 180.0f, params.highMidRelease);
        params.highRelease = juce::jlimit(50.0f, 160.0f, params.highRelease);
        params.maximizerRelease = juce::jlimit(80.0f, 180.0f, params.maximizerRelease);

        return params;
    }
}
