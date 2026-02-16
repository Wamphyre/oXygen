#pragma once

#include <JuceHeader.h>
#include <vector>
#include <memory>

// Forward declaration to avoid including heavy ONNX headers in header
namespace Ort { class Session; class Env; }

namespace oxygen
{
    struct MasteringParameters
    {
        float targetLufs = -14.0f;
        float eqLowGain = 0.0f;
        float eqMidGain = 0.0f;
        float eqHighGain = 0.0f;
        float stereoWidth = 1.0f;
    };

    class InferenceEngine
    {
    public:
        InferenceEngine();
        ~InferenceEngine();

        bool loadModel(const juce::File& modelFile);
        
        // Analyze audio features and return suggested mastering parameters
        MasteringParameters predict(const std::vector<float>& audioFeatures);

        bool isModelLoaded() const { return modelLoaded; }

    private:
        struct Impl;
        std::unique_ptr<Impl> pImpl;
        
        bool modelLoaded = false;
        
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InferenceEngine)
    };
}
