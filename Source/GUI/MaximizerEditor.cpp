#include "MaximizerEditor.h"
#include "Theme.h"

namespace oxygen
{
    MaximizerEditor::MaximizerEditor(MaximizerModule& p, juce::AudioProcessorValueTreeState& vts)
        : AudioProcessorEditor(&p), audioProcessor(p), apvts(vts)
    {
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

        setupSlider(threshSlider, threshAttachment, "Threshold");
        setupSlider(ceilingSlider, ceilingAttachment, "Ceiling");
        setupSlider(releaseSlider, releaseAttachment, "Release");

        setSize(300, 200);
    }

    MaximizerEditor::~MaximizerEditor()
    {
    }

    void MaximizerEditor::paint(juce::Graphics& g)
    {
        g.setColour(oxygen::Theme::Colors::OnSurfaceVariant);
        g.setFont(oxygen::Theme::Fonts::getBody().withHeight(12.0f));

        auto bounds = getLocalBounds().toFloat();
        float width = bounds.getWidth() / 3.0f;
        
        auto drawLabel = [&](const juce::String& text, int index)
        {
            auto area = bounds.removeFromLeft(width);
            g.drawText(text, area.removeFromBottom(20), juce::Justification::centred, false);
        };
        
        drawLabel("Threshold", 0);
        drawLabel("Ceiling", 1);
        drawLabel("Release", 2);
    }

    void MaximizerEditor::resized()
    {
        auto bounds = getLocalBounds();
        bounds.removeFromBottom(25); // Labels
        
        int width = bounds.getWidth() / 3;
        
        threshSlider.setBounds(bounds.removeFromLeft(width).reduced(15, 0));
        ceilingSlider.setBounds(bounds.removeFromLeft(width).reduced(15, 0));
        releaseSlider.setBounds(bounds.reduced(15, 0));
    }
}
