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
        SpectrumAnalyzer(AudioBufferQueue& queueToUse, std::function<double()> sampleRateProviderToUse);
        ~SpectrumAnalyzer() override;

        void paint(juce::Graphics& g) override;
        void resized() override;
        void timerCallback() override;

    private:
        static constexpr int fftOrder = 11; // 2048 points
        static constexpr int fftSize = 1 << fftOrder;
        
        juce::dsp::FFT forwardFFT { fftOrder };
        juce::dsp::WindowingFunction<float> window { fftSize, juce::dsp::WindowingFunction<float>::hann };
        
        AudioBufferQueue& audioQueue;
        std::function<double()> sampleRateProvider;
        std::array<float, fftSize * 2> fifo;
        std::array<float, fftSize * 2> fftData;
        
        bool nextBlockReady = false;
        
        void drawFrame(juce::Graphics& g);
        
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumAnalyzer)
    };
}
