#pragma once

#include <JuceHeader.h>

namespace oxygen
{
    class LevelMeter : public juce::Component
    {
    public:
        LevelMeter() {}
        
        void setLevel(float newLevel)
        {
            if (std::abs(level - newLevel) > 1e-5f)
            {
                level = newLevel;
                repaint();
            }
        }

        void paint(juce::Graphics& g) override
        {
            auto bounds = getLocalBounds().toFloat();
            auto w = bounds.getWidth();
            auto h = bounds.getHeight();
            
            // Dark Background
            g.setColour(juce::Colours::black.withAlpha(0.8f));
            g.fillRoundedRectangle(bounds, 2.0f);
            
            // dB Scaling (-60 to +6)
            float db = juce::Decibels::gainToDecibels(level);
            if (db < -60.0f) db = -60.0f;
            if (db > 6.0f) db = 6.0f; // Cap at +6 for display
            
            float normLevel = juce::jmap(db, -60.0f, 6.0f, 0.0f, 1.0f);
            float fillHeight = h * normLevel;
            
            // Segmented Drawing
            int numSegments = 30; // More segments for better resolution
            float segmentHeight = h / (float)numSegments;
            
            for (int i = 0; i < numSegments; ++i)
            {
                float segmentTop = h - (i + 1) * segmentHeight;
                // Calculate dB value for this segment
                float segmentNorm = (float)(i + 1) / numSegments;
                float segmentDb = juce::jmap(segmentNorm, 0.0f, 1.0f, -60.0f, 6.0f);

                if (segmentTop + segmentHeight > h - fillHeight)
                {
                    // Calculate color based on segment position
                    juce::Colour c;
                    if (segmentDb < -18.0f) c = juce::Colours::lime.withAlpha(0.8f); // Healthy
                    else if (segmentDb < 0.0f) c = juce::Colours::yellow.withAlpha(0.8f); // Hot
                    else c = juce::Colours::red.withAlpha(0.8f); // Clipping/Limit
                    
                    g.setColour(c);
                    g.fillRect(2.0f, segmentTop + 1.0f, w - 4.0f, segmentHeight - 2.0f);
                }
                else
                {
                    g.setColour(juce::Colours::white.withAlpha(0.1f)); // Dim background
                    g.fillRect(2.0f, segmentTop + 1.0f, w - 4.0f, segmentHeight - 2.0f);
                }
            }
            
            // Border Glow
            g.setColour(juce::Colours::cyan.withAlpha(0.2f));
            g.drawRoundedRectangle(bounds, 2.0f, 1.0f);
        }

    private:
        float level = 0.0f;
        
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LevelMeter)
    };
}
