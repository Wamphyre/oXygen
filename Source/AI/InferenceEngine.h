#pragma once

#include <JuceHeader.h>
#include <array>
#include <memory>
#include <vector>

// Forward declaration to avoid including heavy ONNX headers in header
namespace Ort { class Session; class Env; }

namespace oxygen
{
    enum class AssistantGenre
    {
        Universal = 0,
        Pop,
        HipHop,
        Trap,
        Electronic,
        Rock,
        BlackMetal,
        DeathMetal,
        BrutalDeathMetal,
        Acoustic,
        Orchestral
    };

    enum class ArtisticDirection
    {
        Balanced = 0,
        Transparent,
        Warm,
        Punchy,
        Wide,
        Aggressive
    };

    struct AssistantContext
    {
        AssistantGenre genre = AssistantGenre::Universal;
        ArtisticDirection direction = ArtisticDirection::Balanced;
    };

    struct MasteringParameters
    {
        static constexpr int eqBandCount = 15;
        static constexpr int dynamicEqBandCount = 4;

        std::array<float, eqBandCount> eqBandGains {};
        std::array<float, dynamicEqBandCount> dynamicEqThresholds { -26.0f, -28.0f, -30.0f, -32.0f };
        std::array<float, dynamicEqBandCount> dynamicEqRanges {};

        float lowMidX = 120.0f;
        float midHighX = 1400.0f;
        float highX = 6000.0f;

        float lowThresh = -18.0f;
        float lowRatio = 1.5f;
        float lowAttack = 30.0f;
        float lowRelease = 180.0f;

        float lowMidThresh = -16.0f;
        float lowMidRatio = 1.35f;
        float lowMidAttack = 20.0f;
        float lowMidRelease = 150.0f;

        float highMidThresh = -14.0f;
        float highMidRatio = 1.25f;
        float highMidAttack = 8.0f;
        float highMidRelease = 120.0f;

        float highThresh = -12.0f;
        float highRatio = 1.2f;
        float highAttack = 5.0f;
        float highRelease = 100.0f;

        float lowWidth = 0.10f;
        float lowMidWidth = 0.95f;
        float highMidWidth = 1.18f;
        float highWidth = 1.35f;

        float outputGain = 1.0f;
        float maximizerThreshold = -2.0f;
        float maximizerCeiling = -1.0f;
        float maximizerRelease = 120.0f;
        int maximizerMode = 0;

        bool usedAnalysis = false;
    };

    class InferenceEngine
    {
    public:
        InferenceEngine();
        ~InferenceEngine();

        bool loadModel(const juce::File& modelFile);
        
        // Analyze audio features and return suggested mastering parameters
        MasteringParameters predict(const std::vector<float>& audioFeatures);
        MasteringParameters predict(const juce::AudioBuffer<float>& recentAudio, double sampleRate);
        MasteringParameters predict(const juce::AudioBuffer<float>& recentAudio,
                                    double sampleRate,
                                    const AssistantContext& context);
        MasteringParameters matchReference(const juce::AudioBuffer<float>& sourceAudio,
                                           double sourceSampleRate,
                                           const juce::AudioBuffer<float>& referenceAudio,
                                           double referenceSampleRate);

        bool isModelLoaded() const { return modelLoaded; }

    private:
        struct Impl;
        std::unique_ptr<Impl> pImpl;
        
        bool modelLoaded = false;
        
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InferenceEngine)
    };
}
