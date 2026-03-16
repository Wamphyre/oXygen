#pragma once

#include <JuceHeader.h>
#include "../Modules/DynamicEqModule.h"
#include <array>

namespace oxygen
{
    class DynamicEqEditor : public juce::AudioProcessorEditor
    {
    public:
        DynamicEqEditor(DynamicEqModule&, juce::AudioProcessorValueTreeState&);
        ~DynamicEqEditor() override;

        void paint(juce::Graphics&) override;
        void resized() override;

    private:
        DynamicEqModule& audioProcessor;
        juce::AudioProcessorValueTreeState& apvts;

        struct BandControls
        {
            std::unique_ptr<juce::Slider> threshold;
            std::unique_ptr<juce::Slider> range;
            std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> thresholdAttachment;
            std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> rangeAttachment;
        };

        std::array<BandControls, DynamicEqModule::NumBands> bands;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DynamicEqEditor)
    };
}
