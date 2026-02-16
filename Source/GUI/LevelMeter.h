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
            if (level != newLevel)
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
            
            // dB Scaling (0 to -60)
            float db = juce::Decibels::gainToDecibels(level);
            if (db < -60.0f) db = -60.0f;
            
            float normLevel = juce::jmap(db, -60.0f, 0.0f, 0.0f, 1.0f);
            float fillHeight = h * normLevel;
            
            auto fillBounds = bounds.withTop(bounds.getBottom() - fillHeight);
            
            // Segmented Drawing
            int numSegments = 20;
            float segmentHeight = h / (float)numSegments;
            
            for (int i = 0; i < numSegments; ++i)
            {
                float segmentTop = h - (i + 1) * segmentHeight;
                if (segmentTop + segmentHeight > h - fillHeight)
                {
                    // Calculate color based on segment position
                    juce::Colour c;
                    if (i < 12) c = juce::Colours::cyan.withAlpha(0.8f);
                    else if (i < 17) c = juce::Colours::yellow.withAlpha(0.8f);
                    else c = juce::Colours::magenta.withAlpha(0.8f);
                    
                    g.setColour(c);
                    g.fillRect(2.0f, segmentTop + 1.0f, w - 4.0f, segmentHeight - 2.0f);
                }
                else
                {
                    g.setColour(juce::Colours::white.withAlpha(0.05f));
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
