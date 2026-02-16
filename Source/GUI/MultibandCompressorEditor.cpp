#include "MultibandCompressorEditor.h"
#include "Theme.h"

namespace oxygen
{
    MultibandCompressorEditor::MultibandCompressorEditor(MultibandCompressorModule& p, juce::AudioProcessorValueTreeState& vts)
        : AudioProcessorEditor(&p), audioProcessor(p), apvts(vts)
    {
        const char* bandNames[] = { "Low", "LowMid", "HighMid", "High" };
        const char* paramNames[] = { "Thresh", "Ratio", "Attack", "Release", "Gain" };

        for (int i = 0; i < 4; ++i)
        {
            auto& band = bands[i];
            juce::String prefix = bandNames[i];

            auto setupSlider = [&](std::unique_ptr<juce::Slider>& slider, std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& att, juce::String paramID)
            {
                slider = std::make_unique<juce::Slider>();
                slider->setSliderStyle(juce::Slider::LinearVertical);
                slider->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
                
                slider->setColour(juce::Slider::thumbColourId, oxygen::Theme::Colors::Primary);
                slider->setColour(juce::Slider::trackColourId, oxygen::Theme::Colors::OnSurfaceVariant.withAlpha(0.2f));
                slider->setColour(juce::Slider::backgroundColourId, oxygen::Theme::Colors::SurfaceVariant.darker(0.2f));
                
                slider->setPopupDisplayEnabled(true, true, nullptr); // Feedback
                
                addAndMakeVisible(slider.get());
                att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, paramID, *slider);
            };

            setupSlider(band.thresh, band.threshAtt, prefix + "Thresh");
            setupSlider(band.ratio, band.ratioAtt, prefix + "Ratio");
            setupSlider(band.attack, band.attackAtt, prefix + "Attack");
            setupSlider(band.release, band.releaseAtt, prefix + "Release");
            setupSlider(band.gain, band.gainAtt, prefix + "Gain");
        }

        // Crossovers
        auto setupXover = [&](std::unique_ptr<juce::Slider>& slider, std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& att, juce::String paramID)
        {
            slider = std::make_unique<juce::Slider>();
            slider->setSliderStyle(juce::Slider::LinearHorizontal);
            slider->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            slider->setColour(juce::Slider::thumbColourId, oxygen::Theme::Colors::Secondary);
            slider->setColour(juce::Slider::trackColourId, oxygen::Theme::Colors::OnSurfaceVariant.withAlpha(0.2f));
            addAndMakeVisible(slider.get());
            att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, paramID, *slider);
        };

        setupXover(lowMidX, lowMidXAtt, "LowMidX");
        setupXover(midHighX, midHighXAtt, "MidHighX");
        setupXover(highX, highXAtt, "HighX");

        setSize(700, 300);
    }

    MultibandCompressorEditor::~MultibandCompressorEditor()
    {
    }

    void MultibandCompressorEditor::paint(juce::Graphics& g)
    {
        g.setColour(oxygen::Theme::Colors::OnSurfaceVariant);
        g.setFont(oxygen::Theme::Fonts::getBody().withHeight(11.0f)); // Smaller font

        auto bounds = getLocalBounds().toFloat();
        auto xoverArea = bounds.removeFromTop(30); // Compact Xover
        
        // Crossover Label
        g.drawText("Xovers", xoverArea.removeFromLeft(50), juce::Justification::centredLeft, false);

        float bandWidth = bounds.getWidth() / 4.0f;
        const char* bandNames[] = { "LOW", "L-MID", "H-MID", "HIGH" };
        const char* controlLabels[] = { "TH", "RAT", "ATK", "REL", "GN" }; // Very short labels

        for (int i = 0; i < 4; ++i)
        {
            auto bandArea = bounds.removeFromLeft(bandWidth);
            
            // Band Background (Alternating slightly for visibility)
            if (i % 2 == 0)
            {
                g.setColour(oxygen::Theme::Colors::SurfaceVariant.withAlpha(0.05f));
                g.fillRect(bandArea);
            }

            // Band Header
            g.setColour(oxygen::Theme::Colors::Secondary);
            g.drawText(bandNames[i], bandArea.removeFromTop(15), juce::Justification::centred, false);
            
            // Separator
            g.setColour(oxygen::Theme::Colors::OnSurfaceVariant.withAlpha(0.1f));
            g.drawVerticalLine((int)bounds.getX(), bandArea.getY(), bandArea.getBottom());

            // Control Labels (Bottom)
            float sliderWidth = bandArea.getWidth() / 5.0f;
            auto labelArea = bandArea.removeFromBottom(12);
            
            g.setColour(oxygen::Theme::Colors::OnSurfaceVariant);
            
            for (int j = 0; j < 5; ++j)
            {
                 g.drawText(controlLabels[j], labelArea.removeFromLeft(sliderWidth), juce::Justification::centred, false);
            }
        }
    }

    void MultibandCompressorEditor::resized()
    {
        auto bounds = getLocalBounds();
        auto xoverArea = bounds.removeFromTop(30);
        
        // Layout Crossovers
        xoverArea.removeFromLeft(50); // Label space
        int xoverWidth = xoverArea.getWidth() / 3;
        
        // Compact Xovers
        lowMidX->setBounds(xoverArea.removeFromLeft(xoverWidth).reduced(2));
        midHighX->setBounds(xoverArea.removeFromLeft(xoverWidth).reduced(2));
        highX->setBounds(xoverArea.reduced(2));

        // Layout Bands
        float bandWidth = bounds.getWidth() / 4.0f;
        
        for (int i = 0; i < 4; ++i)
        {
            auto bandArea = bounds.removeFromLeft((int)bandWidth);
            bandArea.removeFromTop(15); // Header
            bandArea.removeFromBottom(12); // Labels
            
            float sliderWidth = bandArea.getWidth() / 5.0f;
            
            auto& band = bands[i];
            
            // Compact sliders (reduced margins)
            if (band.thresh) band.thresh->setBounds(bandArea.removeFromLeft((int)sliderWidth).reduced(1, 0));
            if (band.ratio) band.ratio->setBounds(bandArea.removeFromLeft((int)sliderWidth).reduced(1, 0));
            if (band.attack) band.attack->setBounds(bandArea.removeFromLeft((int)sliderWidth).reduced(1, 0));
            if (band.release) band.release->setBounds(bandArea.removeFromLeft((int)sliderWidth).reduced(1, 0));
            if (band.gain) band.gain->setBounds(bandArea.reduced(1, 0));
            
            // Enable value popups on hover/drag for feedback
            auto configSlider = [](juce::Slider* s) {
                if(s) s->setPopupDisplayEnabled(true, true, nullptr);
            };
            configSlider(band.thresh.get());
            configSlider(band.ratio.get());
            // ... apply to all (doing in loop or helper would be cleaner but resizing is where we have easy access)
        }
    }
}
