#include "EqualizerEditor.h"
#include "Theme.h"

namespace oxygen
{
    EqualizerEditor::EqualizerEditor(EqualizerModule& p, juce::AudioProcessorValueTreeState& vts)
        : AudioProcessorEditor(&p), audioProcessor(p), apvts(vts)
    {
        for (int i = 0; i < NumBands; ++i)
        {
            auto slider = std::make_unique<juce::Slider>();
            slider->setSliderStyle(juce::Slider::LinearVertical);
            slider->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            
            // Theme colors
            slider->setColour(juce::Slider::thumbColourId, oxygen::Theme::Colors::Primary);
            slider->setColour(juce::Slider::trackColourId, oxygen::Theme::Colors::OnSurfaceVariant.withAlpha(0.2f));
            slider->setColour(juce::Slider::backgroundColourId, oxygen::Theme::Colors::SurfaceVariant.darker(0.2f));
            
            slider->setPopupDisplayEnabled(true, true, nullptr); // Feedback on value change

            addAndMakeVisible(slider.get());
            
            gainAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                apvts, "Gain" + juce::String(i), *slider);
                
            gainSliders[i] = std::move(slider);
        }
        
        setSize(600, 200);
    }

    EqualizerEditor::~EqualizerEditor()
    {
    }

    void EqualizerEditor::paint(juce::Graphics& g)
    {
        // Background handled by module wrapper mostly, but we can clear transparency
        // g.fillAll(juce::Colours::transparentBlack);
        
        g.setColour(oxygen::Theme::Colors::OnSurfaceVariant);
        g.setFont(oxygen::Theme::Fonts::getBody().withHeight(10.0f));
        
        auto bounds = getLocalBounds().toFloat();
        bounds.removeFromTop(30); // Skip header
        
        float sliderWidth = bounds.getWidth() / (float)NumBands;
        
        for (int i = 0; i < NumBands; ++i)
        {
            float x = i * sliderWidth;
            
            // Draw Freq Label at bottom
            juce::String freqStr;
            if (EqualizerModule::Frequencies[i] >= 1000.0f)
                freqStr = juce::String(EqualizerModule::Frequencies[i] / 1000.0f, 1) + "k";
            else
                freqStr = juce::String((int)EqualizerModule::Frequencies[i]);
                
            g.drawText(freqStr, x, bounds.getBottom() - 20, sliderWidth, 20, juce::Justification::centred, false);
        }
    }

    void EqualizerEditor::resized()
    {
        auto bounds = getLocalBounds();
        auto header = bounds.removeFromTop(30);
        
        bounds.removeFromBottom(20); // Space for labels
        
        float sliderWidth = bounds.getWidth() / (float)NumBands;
        
        for (int i = 0; i < NumBands; ++i)
        {
            if (auto* slider = gainSliders[i].get())
            {
                slider->setBounds(bounds.removeFromLeft(sliderWidth).reduced(2, 0));
            }
        }
    }
}
