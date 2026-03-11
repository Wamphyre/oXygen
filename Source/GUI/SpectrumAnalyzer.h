#pragma once
#include <JuceHeader.h>
#include "AudioBufferQueue.h"
#include <functional>

namespace oxygen
{
    class SpectrumAnalyzer : public juce::Component,
                             public juce::Timer
    {
    public:
        SpectrumAnalyzer(AudioBufferQueue& inputQueueToUse,
                         AudioBufferQueue& outputQueueToUse,
                         std::function<double()> sampleRateProviderToUse);
        ~SpectrumAnalyzer() override;

        void paint(juce::Graphics& g) override;
        void resized() override;
        void timerCallback() override;

    private:
        static constexpr int fftOrder = 11; // 2048 points
        static constexpr int fftSize = 1 << fftOrder;
        static constexpr int fftBins = fftSize / 2;
        
        juce::dsp::FFT forwardFFT { fftOrder };
        juce::dsp::WindowingFunction<float> window { fftSize, juce::dsp::WindowingFunction<float>::hann };
        
        AudioBufferQueue& inputAudioQueue;
        AudioBufferQueue& outputAudioQueue;
        std::function<double()> sampleRateProvider;
        std::array<float, fftSize> inputLeftFifo {};
        std::array<float, fftSize> inputRightFifo {};
        std::array<float, fftSize> outputLeftFifo {};
        std::array<float, fftSize> outputRightFifo {};
        std::array<float, fftSize * 2> inputFftData {};
        std::array<float, fftSize * 2> outputFftData {};
        std::array<float, fftBins> smoothedInputDb {};
        std::array<float, fftBins> smoothedOutputDb {};
        bool hasInputFrame = false;
        bool hasOutputFrame = false;
        bool displaySpectraInitialized = false;
        
        void drawFrame(juce::Graphics& g);
        void updateSpectrumFrame(const std::array<float, fftSize>& leftChannel,
                                 const std::array<float, fftSize>& rightChannel,
                                 std::array<float, fftSize * 2>& magnitudeDestination);
        void updateSmoothedSpectrum(const std::array<float, fftSize * 2>& fftMagnitudes,
                                    std::array<float, fftBins>& smoothedDestination);
        juce::Path buildSpectrumPath(const std::array<float, fftBins>& magnitudesDb,
                                     juce::Rectangle<float> bounds,
                                     float sampleRate) const;
        
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumAnalyzer)
    };
}
