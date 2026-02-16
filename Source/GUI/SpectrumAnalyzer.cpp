#include "SpectrumAnalyzer.h"

namespace oxygen
{
    SpectrumAnalyzer::SpectrumAnalyzer(AudioBufferQueue& queueToUse)
        : audioQueue(queueToUse)
    {
        startTimerHz(30); // 30 FPS update
    }

    SpectrumAnalyzer::~SpectrumAnalyzer()
    {
        stopTimer();
    }

    void SpectrumAnalyzer::timerCallback()
    {
        if (audioQueue.getNumReady() >= fftSize)
        {
            juce::FloatVectorOperations::clear(fifo.data(), fftSize);
            audioQueue.pop(fifo.data(), fftSize);
            
            // Apply windowing and FFT
            std::copy(fifo.begin(), fifo.begin() + fftSize, fftData.begin());
            window.multiplyWithWindowingTable(fftData.data(), fftSize);
            forwardFFT.performFrequencyOnlyForwardTransform(fftData.data());
            
            nextBlockReady = true;
            repaint();
        }
    }

    void SpectrumAnalyzer::paint(juce::Graphics& g)
    {
        g.fillAll(juce::Colours::black.withAlpha(0.2f)); // More transparent background for overlay
        
        if (nextBlockReady)
        {
            drawFrame(g);
        }
    }
    
    void SpectrumAnalyzer::drawFrame(juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat();
        auto width = bounds.getWidth();
        auto height = bounds.getHeight();
        
        // Premium Neon Cyan Gradient
        juce::ColourGradient spectrumGradient(juce::Colours::cyan.withAlpha(0.9f), 0, height * 0.5f,
                                            juce::Colours::magenta.withAlpha(0.9f), width, height * 0.5f, false);
        g.setGradientFill(spectrumGradient);

        juce::Path spectrumPath;
        bool started = false;
        
        const float sampleRate = 44100.0f; // Simplified, ideally get from processor
        const float minFreq = 20.0f;
        const float maxFreq = 20000.0f;
        
        for (int i = 1; i < fftSize / 2; ++i)
        {
            float freq = (float)i * (sampleRate / (float)fftSize);
            
            // Logarithmic mapping for X
            float x = juce::jmap(std::log10(freq), std::log10(minFreq), std::log10(maxFreq), 0.0f, width);
            
            float magnitude = fftData[i];
            float db = juce::Decibels::gainToDecibels(magnitude) - juce::Decibels::gainToDecibels((float)fftSize); 
            
            if (db < -100.0f) db = -100.0f;
            
            float normY = juce::jmap(db, -100.0f, 0.0f, 0.0f, 1.0f);
            float y = height - (normY * height);
            
            if (x >= 0 && x <= width)
            {
                if (!started)
                {
                    spectrumPath.startNewSubPath(x, y);
                    started = true;
                }
                else
                {
                    spectrumPath.lineTo(x, y);
                }
            }
        }
        
        g.strokePath(spectrumPath, juce::PathStrokeType(1.5f));
        
        // Fill with subtle transparency
        spectrumPath.lineTo(width, height);
        spectrumPath.lineTo(0, height);
        spectrumPath.closeSubPath();
        
        juce::ColourGradient fillGradient(juce::Colours::cyan.withAlpha(0.2f), 0, 0,
                                         juce::Colours::transparentBlack, 0, height, false);
        g.setGradientFill(fillGradient);
        g.fillPath(spectrumPath);

        // Draw basic grid
        g.setColour(juce::Colours::white.withAlpha(0.05f));
        float freqs[] = { 100, 1000, 5000, 10000 };
        for (float f : freqs)
        {
            float x = juce::jmap(std::log10(f), std::log10(minFreq), std::log10(maxFreq), 0.0f, width);
            g.drawVerticalLine((int)x, 0.0f, height);
        }
    }

    void SpectrumAnalyzer::resized()
    {
    }
}
