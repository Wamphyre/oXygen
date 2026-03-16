#include "DynamicEqEditor.h"
#include "Theme.h"

namespace oxygen
{
    namespace
    {
        constexpr int editorWidth = 700;
        constexpr int editorHeight = 192;
        constexpr int topTitleHeight = 18;
        constexpr int topFreqHeight = 14;
        constexpr int topSpacerHeight = 6;
        constexpr int bottomLabelHeight = 20;
        constexpr int bandInnerMargin = 8;
        constexpr int sliderInnerMargin = 8;
    }

    DynamicEqEditor::DynamicEqEditor(DynamicEqModule& p, juce::AudioProcessorValueTreeState& vts)
        : juce::AudioProcessorEditor(&p), audioProcessor(p), apvts(vts)
    {
        const char* bandNames[] = { "Low", "LowMid", "HighMid", "High" };

        for (int band = 0; band < DynamicEqModule::NumBands; ++band)
        {
            auto& controls = bands[(size_t) band];
            const juce::String prefix = bandNames[band];

            auto setupSlider = [this] (std::unique_ptr<juce::Slider>& slider)
            {
                slider = std::make_unique<juce::Slider>();
                slider->setSliderStyle(juce::Slider::LinearVertical);
                slider->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
                slider->setColour(juce::Slider::thumbColourId, oxygen::Theme::Colors::Primary);
                slider->setColour(juce::Slider::trackColourId, oxygen::Theme::Colors::OnSurfaceVariant.withAlpha(0.2f));
                slider->setColour(juce::Slider::backgroundColourId, oxygen::Theme::Colors::SurfaceVariant.darker(0.2f));
                slider->setPopupDisplayEnabled(true, true, nullptr);
                addAndMakeVisible(slider.get());
            };

            setupSlider(controls.threshold);
            setupSlider(controls.range);

            controls.thresholdAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                apvts, prefix + "Thresh", *controls.threshold);
            controls.rangeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                apvts, prefix + "Range", *controls.range);
        }

        setSize(editorWidth, editorHeight);
    }

    DynamicEqEditor::~DynamicEqEditor() = default;

    void DynamicEqEditor::paint(juce::Graphics& g)
    {
        g.setColour(oxygen::Theme::Colors::OnSurfaceVariant);
        g.setFont(oxygen::Theme::Fonts::getBody().withHeight(11.0f));

        auto bounds = getLocalBounds().toFloat().reduced(0.0f, 2.0f);

        const float bandWidth = bounds.getWidth() / (float) DynamicEqModule::NumBands;
        const char* bandNames[] = { "LOW CLEAN", "BODY", "PRESENCE", "AIR" };
        const char* freqNames[] = { "140 Hz", "380 Hz", "3.2 kHz", "9.6 kHz" };

        for (int band = 0; band < DynamicEqModule::NumBands; ++band)
        {
            auto bandArea = bounds.removeFromLeft(bandWidth).reduced((float) bandInnerMargin, 0.0f);
            auto contentArea = bandArea;

            if (band % 2 == 0)
            {
                g.setColour(oxygen::Theme::Colors::SurfaceVariant.withAlpha(0.05f));
                g.fillRoundedRectangle(contentArea, 6.0f);
            }

            g.setColour(oxygen::Theme::Colors::Secondary);
            g.drawText(bandNames[band],
                       contentArea.removeFromTop((float) topTitleHeight),
                       juce::Justification::centred,
                       false);
            g.setColour(oxygen::Theme::Colors::OnSurfaceVariant.withAlpha(0.9f));
            g.drawText(freqNames[band],
                       contentArea.removeFromTop((float) topFreqHeight),
                       juce::Justification::centred,
                       false);
            contentArea.removeFromTop((float) topSpacerHeight);

            auto labelArea = contentArea.removeFromBottom((float) bottomLabelHeight);
            const float sliderLaneWidth = labelArea.getWidth() * 0.5f;
            g.drawText("TH", labelArea.removeFromLeft(sliderLaneWidth), juce::Justification::centred, false);
            g.drawText("RG", labelArea, juce::Justification::centred, false);
        }
    }

    void DynamicEqEditor::resized()
    {
        auto bounds = getLocalBounds().reduced(0, 2);

        const int bandWidth = bounds.getWidth() / DynamicEqModule::NumBands;
        for (int band = 0; band < DynamicEqModule::NumBands; ++band)
        {
            auto bandArea = bounds.removeFromLeft(bandWidth).reduced(bandInnerMargin, 0);
            auto& controls = bands[(size_t) band];
            bandArea.removeFromTop(topTitleHeight + topFreqHeight + topSpacerHeight);
            bandArea.removeFromBottom(bottomLabelHeight);

            const int sliderWidth = bandArea.getWidth() / 2;

            if (controls.threshold)
                controls.threshold->setBounds(bandArea.removeFromLeft(sliderWidth).reduced(sliderInnerMargin, 0));
            if (controls.range)
                controls.range->setBounds(bandArea.reduced(sliderInnerMargin, 0));
        }
    }
}
