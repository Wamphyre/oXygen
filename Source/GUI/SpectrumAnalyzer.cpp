#include "SpectrumAnalyzer.h"
#include "Theme.h"

namespace oxygen
{
    SpectrumAnalyzer::SpectrumAnalyzer(AudioBufferQueue& inputQueueToUse,
                                       AudioBufferQueue& outputQueueToUse,
                                       std::function<double()> sampleRateProviderToUse)
        : inputAudioQueue(inputQueueToUse),
          outputAudioQueue(outputQueueToUse),
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
        if (inputAudioQueue.getNumReady() < fftSize || outputAudioQueue.getNumReady() < fftSize)
            return;

        inputAudioQueue.pop(inputLeftFifo.data(), inputRightFifo.data(), fftSize);
        outputAudioQueue.pop(outputLeftFifo.data(), outputRightFifo.data(), fftSize);

        updateSpectrumFrame(inputLeftFifo, inputRightFifo, inputFftData);
        updateSpectrumFrame(outputLeftFifo, outputRightFifo, outputFftData);
        updateSmoothedSpectrum(inputFftData, smoothedInputDb);
        updateSmoothedSpectrum(outputFftData, smoothedOutputDb);
        displaySpectraInitialized = true;
        hasInputFrame = true;
        hasOutputFrame = true;
        repaint();
    }

    void SpectrumAnalyzer::updateSpectrumFrame(const std::array<float, fftSize>& leftChannel,
                                               const std::array<float, fftSize>& rightChannel,
                                               std::array<float, fftSize * 2>& magnitudeDestination)
    {
        std::array<float, fftSize * 2> leftFft {};
        std::array<float, fftSize * 2> rightFft {};

        std::copy(leftChannel.begin(), leftChannel.end(), leftFft.begin());
        std::copy(rightChannel.begin(), rightChannel.end(), rightFft.begin());

        window.multiplyWithWindowingTable(leftFft.data(), fftSize);
        window.multiplyWithWindowingTable(rightFft.data(), fftSize);
        forwardFFT.performFrequencyOnlyForwardTransform(leftFft.data());
        forwardFFT.performFrequencyOnlyForwardTransform(rightFft.data());

        for (int bin = 0; bin < fftBins; ++bin)
        {
            const float leftMagnitude = leftFft[(size_t) bin];
            const float rightMagnitude = rightFft[(size_t) bin];
            magnitudeDestination[(size_t) bin] = std::sqrt(0.5f
                                                           * ((leftMagnitude * leftMagnitude)
                                                            + (rightMagnitude * rightMagnitude)));
        }
    }

    void SpectrumAnalyzer::updateSmoothedSpectrum(const std::array<float, fftSize * 2>& fftMagnitudes,
                                                  std::array<float, fftBins>& smoothedDestination)
    {
        constexpr float smoothing = 0.86f;
        for (int bin = 0; bin < fftBins; ++bin)
        {
            const float magnitude = juce::jmax(fftMagnitudes[(size_t) bin], 1.0e-9f);
            const float db = juce::jlimit(-100.0f,
                                          0.0f,
                                          juce::Decibels::gainToDecibels(magnitude)
                                            - juce::Decibels::gainToDecibels((float) fftSize));

            if (!displaySpectraInitialized)
                smoothedDestination[(size_t) bin] = db;
            else
                smoothedDestination[(size_t) bin] = (smoothedDestination[(size_t) bin] * smoothing)
                                                 + (db * (1.0f - smoothing));
        }

    }

    void SpectrumAnalyzer::paint(juce::Graphics& g)
    {
        g.fillAll(juce::Colours::black.withAlpha(0.2f)); // More transparent background for overlay
        
        if (hasInputFrame || hasOutputFrame)
        {
            drawFrame(g);
        }
    }
    
    juce::Path SpectrumAnalyzer::buildSpectrumPath(const std::array<float, fftBins>& magnitudesDb,
                                                   juce::Rectangle<float> bounds,
                                                   float sampleRate) const
    {
        juce::Path spectrumPath;
        bool started = false;
        const float height = bounds.getHeight();
        const float minFreq = 20.0f;
        const float maxFreq = juce::jmax(minFreq * 2.0f, juce::jmin(20000.0f, sampleRate * 0.5f));

        for (int i = 1; i < fftBins; ++i)
        {
            const float freq = (float) i * (sampleRate / (float) fftSize);
            if (freq < minFreq || freq > maxFreq)
                continue;

            const float x = juce::jmap(std::log10(freq),
                                       std::log10(minFreq),
                                       std::log10(maxFreq),
                                       bounds.getX(),
                                       bounds.getRight());

            const float normY = juce::jmap(magnitudesDb[(size_t) i], -100.0f, 0.0f, 0.0f, 1.0f);
            const float y = bounds.getBottom() - (normY * height);

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

        return spectrumPath;
    }

    void SpectrumAnalyzer::drawFrame(juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat();
        const auto width = bounds.getWidth();
        const auto height = bounds.getHeight();
        const float sampleRate = (sampleRateProvider != nullptr) ? (float) sampleRateProvider() : 44100.0f;
        const float minFreq = 20.0f;
        const float maxFreq = juce::jmax(minFreq * 2.0f, juce::jmin(20000.0f, sampleRate * 0.5f));
        
        const auto originalPath = hasInputFrame
                                ? buildSpectrumPath(smoothedInputDb, bounds, sampleRate)
                                : juce::Path {};
        const auto processedPath = hasOutputFrame
                                 ? buildSpectrumPath(smoothedOutputDb, bounds, sampleRate)
                                 : juce::Path {};

        if (!processedPath.isEmpty())
        {
            juce::ColourGradient processedGradient(juce::Colours::cyan.withAlpha(0.95f), bounds.getX(), height * 0.5f,
                                                   juce::Colours::magenta.withAlpha(0.95f), bounds.getRight(), height * 0.5f, false);
            g.setGradientFill(processedGradient);
            g.strokePath(processedPath, juce::PathStrokeType(1.8f));

            auto processedFill = processedPath;
            processedFill.lineTo(bounds.getRight(), bounds.getBottom());
            processedFill.lineTo(bounds.getX(), bounds.getBottom());
            processedFill.closeSubPath();

            juce::ColourGradient fillGradient(juce::Colours::cyan.withAlpha(0.16f), 0, 0,
                                              juce::Colours::transparentBlack, 0, height, false);
            g.setGradientFill(fillGradient);
            g.fillPath(processedFill);
        }

        if (!originalPath.isEmpty())
        {
            g.setColour(juce::Colours::white.withAlpha(0.72f));
            g.strokePath(originalPath, juce::PathStrokeType(1.25f));
        }

        if (hasInputFrame && hasOutputFrame)
        {
            for (int bin = 2; bin < fftBins; bin += 2)
            {
                const float freq = (float) bin * (sampleRate / (float) fftSize);
                if (freq < minFreq || freq > maxFreq)
                    continue;

                const float x = juce::jmap(std::log10(freq),
                                           std::log10(minFreq),
                                           std::log10(maxFreq),
                                           bounds.getX(),
                                           bounds.getRight());
                const float inputDb = smoothedInputDb[(size_t) bin];
                const float outputDb = smoothedOutputDb[(size_t) bin];
                const float deltaDb = outputDb - inputDb;
                if (std::abs(deltaDb) < 0.75f)
                    continue;

                const float inputY = bounds.getBottom() - (juce::jmap(inputDb, -100.0f, 0.0f, 0.0f, 1.0f) * height);
                const float outputY = bounds.getBottom() - (juce::jmap(outputDb, -100.0f, 0.0f, 0.0f, 1.0f) * height);
                const float alpha = juce::jlimit(0.08f, 0.34f, std::abs(deltaDb) / 10.0f);
                g.setColour((deltaDb > 0.0f ? juce::Colours::magenta : juce::Colours::orange).withAlpha(alpha));
                g.drawLine(x, inputY, x, outputY, 1.0f);
            }
        }

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
            const float x = juce::jmap(std::log10(line.freq),
                                       std::log10(minFreq),
                                       std::log10(maxFreq),
                                       bounds.getX(),
                                       bounds.getRight());
            if (x >= bounds.getX() && x <= bounds.getRight())
            {
                g.drawVerticalLine((int) x, bounds.getY(), bounds.getBottom());
                g.drawText(line.label, (int) x + 2, (int) bounds.getBottom() - 12, 30, 12, juce::Justification::left, false);
            }
        }

        const auto legendBounds = bounds.removeFromTop(18.0f);
        g.setColour(juce::Colours::white.withAlpha(0.78f));
        g.drawLine(legendBounds.getX(), legendBounds.getCentreY(),
                   legendBounds.getX() + 16.0f, legendBounds.getCentreY(), 1.2f);
        g.drawText("Original", (int) legendBounds.getX() + 22, (int) legendBounds.getY(), 64, (int) legendBounds.getHeight(),
                   juce::Justification::centredLeft, false);

        g.setColour(juce::Colours::cyan.withAlpha(0.90f));
        g.drawLine(legendBounds.getX() + 92.0f, legendBounds.getCentreY(),
                   legendBounds.getX() + 108.0f, legendBounds.getCentreY(), 1.8f);
        g.drawText("Processed", (int) legendBounds.getX() + 114, (int) legendBounds.getY(), 76, (int) legendBounds.getHeight(),
                   juce::Justification::centredLeft, false);
        
        // Visualize "Hot Zones" (Mud/Resonance areas)
        // 200-500Hz often problematic 'mud' area
        const float mudX1 = juce::jmap(std::log10(200.0f), std::log10(minFreq), std::log10(maxFreq), bounds.getX(), bounds.getRight());
        const float mudX2 = juce::jmap(std::log10(500.0f), std::log10(minFreq), std::log10(maxFreq), bounds.getX(), bounds.getRight());
        
        g.setColour(juce::Colours::orange.withAlpha(0.05f));
        g.fillRect(juce::Rectangle<float>(mudX1, bounds.getY(), mudX2 - mudX1, height));
        
        // High shhh area 4k-8k
        const float harshX1 = juce::jmap(std::log10(4000.0f), std::log10(minFreq), std::log10(maxFreq), bounds.getX(), bounds.getRight());
        const float harshX2 = juce::jmap(std::log10(8000.0f), std::log10(minFreq), std::log10(maxFreq), bounds.getX(), bounds.getRight());
        
        g.setColour(juce::Colours::yellow.withAlpha(0.05f));
        g.fillRect(juce::Rectangle<float>(harshX1, bounds.getY(), harshX2 - harshX1, height));
    }

    void SpectrumAnalyzer::resized()
    {
    }
}
