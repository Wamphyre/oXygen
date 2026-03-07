#include "SpectrumAnalyzer.h"
#include "Theme.h"

namespace oxygen
{
    SpectrumAnalyzer::SpectrumAnalyzer(AudioBufferQueue& queueToUse, std::function<double()> sampleRateProviderToUse)
        : audioQueue(queueToUse),
          sampleRateProvider(std::move(sampleRateProviderToUse))
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
        
        const float sampleRate = (sampleRateProvider != nullptr) ? (float) sampleRateProvider() : 44100.0f;
        const float minFreq = 20.0f;
        const float maxFreq = juce::jmax(minFreq * 2.0f, juce::jmin(20000.0f, sampleRate * 0.5f));
        
        for (int i = 1; i < fftSize / 2; ++i)
        {
            float freq = (float)i * (sampleRate / (float)fftSize);
            if (freq < minFreq || freq > maxFreq)
                continue;
            
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

        // Draw Frequency Grid & Labels
        g.setColour(juce::Colours::white.withAlpha(0.2f));
        g.setFont(oxygen::Theme::Fonts::getBody().withHeight(10.0f));
        
        struct GridLine { float freq; const char* label; };
        GridLine lines[] = { 
            {100.0f, "100"},
            {500.0f, "500"},
            {1000.0f, "1k"},
            {5000.0f, "5k"},
            {10000.0f, "10k"}
        };

        for (const auto& line : lines)
        {
            float x = juce::jmap(std::log10(line.freq), std::log10(minFreq), std::log10(maxFreq), 0.0f, width);
            if (x >= 0 && x <= width)
            {
                g.drawVerticalLine((int)x, 0.0f, height);
                g.drawText(line.label, (int)x + 2, (int)height - 12, 30, 12, juce::Justification::left, false);
            }
        }
        
        // Visualize "Hot Zones" (Mud/Resonance areas)
        // 200-500Hz often problematic 'mud' area
        float mudX1 = juce::jmap(std::log10(200.0f), std::log10(minFreq), std::log10(maxFreq), 0.0f, width);
        float mudX2 = juce::jmap(std::log10(500.0f), std::log10(minFreq), std::log10(maxFreq), 0.0f, width);
        
        g.setColour(juce::Colours::orange.withAlpha(0.05f));
        g.fillRect(juce::Rectangle<float>(mudX1, 0, mudX2 - mudX1, height));
        
        // High shhh area 4k-8k
        float harshX1 = juce::jmap(std::log10(4000.0f), std::log10(minFreq), std::log10(maxFreq), 0.0f, width);
        float harshX2 = juce::jmap(std::log10(8000.0f), std::log10(minFreq), std::log10(maxFreq), 0.0f, width);
        
        g.setColour(juce::Colours::yellow.withAlpha(0.05f));
        g.fillRect(juce::Rectangle<float>(harshX1, 0, harshX2 - harshX1, height));
    }

    void SpectrumAnalyzer::resized()
    {
    }
}
