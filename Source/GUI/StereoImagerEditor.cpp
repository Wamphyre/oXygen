#include "StereoImagerEditor.h"
#include "Theme.h"

namespace oxygen
{
    StereoImagerEditor::StereoImagerEditor(StereoImagerModule& p, juce::AudioProcessorValueTreeState& vts)
        : AudioProcessorEditor(&p), audioProcessor(p), apvts(vts)
    {
        // Width Sliders
        auto setupSlider = [&](juce::Slider& slider, std::unique_ptr<SliderAttachment>& attachment, const juce::String& paramID)
        {
            addAndMakeVisible(slider);
            slider.setSliderStyle(juce::Slider::LinearVertical);
            slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            
            slider.setColour(juce::Slider::thumbColourId, oxygen::Theme::Colors::Primary);
            slider.setColour(juce::Slider::trackColourId, oxygen::Theme::Colors::OnSurfaceVariant.withAlpha(0.2f));
            slider.setColour(juce::Slider::backgroundColourId, oxygen::Theme::Colors::SurfaceVariant.darker(0.2f));
            
            slider.setPopupDisplayEnabled(true, true, nullptr); // Feedback
            
            attachment = std::make_unique<SliderAttachment>(apvts, paramID, slider);
        };

        setupSlider(lowWidthSlider, lowAttachment, "LowWidth");
        setupSlider(lowMidWidthSlider, lowMidAttachment, "LowMidWidth");
        setupSlider(highMidWidthSlider, highMidAttachment, "HighMidWidth");
        setupSlider(highWidthSlider, highAttachment, "HighWidth");

        // Crossovers (Hidden for now or added if needed? The module has them)
        // Let's rely on default defaults or add them if requested. 
        // The plan mentioned "Audit Multiband/Imager Summing Logic" but for GUI, simple is better.
        // Let's allow Xovers since Multiband has them.
        
        setSize(400, 130);
    }

    StereoImagerEditor::~StereoImagerEditor()
    {
    }

    void StereoImagerEditor::paint(juce::Graphics& g)
    {
        g.setColour(oxygen::Theme::Colors::OnSurfaceVariant);
        g.setFont(oxygen::Theme::Fonts::getBody().withHeight(12.0f));

        auto bounds = getLocalBounds().toFloat();
        float width = bounds.getWidth() / 4.0f;
        
        const char* names[] = { "Low", "LowMid", "HighMid", "High" };
        
        for (int i = 0; i < 4; ++i)
        {
            auto area = bounds.removeFromLeft(width);
            g.drawText(names[i], area.removeFromBottom(20), juce::Justification::centred, false);
            g.drawText("Width", area.removeFromBottom(15), juce::Justification::centred, false);
        }
    }

    void StereoImagerEditor::resized()
    {
        auto bounds = getLocalBounds();
        bounds.removeFromBottom(35); // Labels
        
        int width = bounds.getWidth() / 4;
        
        lowWidthSlider.setBounds(bounds.removeFromLeft(width).reduced(10, 0));
        lowMidWidthSlider.setBounds(bounds.removeFromLeft(width).reduced(10, 0));
        highMidWidthSlider.setBounds(bounds.removeFromLeft(width).reduced(10, 0));
        highWidthSlider.setBounds(bounds.reduced(10, 0));
    }
}
