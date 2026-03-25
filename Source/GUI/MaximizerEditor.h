#pragma once

#include <JuceHeader.h>
#include "../Modules/MaximizerModule.h"

namespace oxygen
{

    class MaximizerEditor : public juce::AudioProcessorEditor
    {
    public:
        MaximizerEditor(MaximizerModule&, juce::AudioProcessorValueTreeState&);
        ~MaximizerEditor() override;

        void paint(juce::Graphics&) override;
        void resized() override;

    private:
        MaximizerModule& audioProcessor;
        juce::AudioProcessorValueTreeState& apvts;

        juce::Slider threshSlider;
        juce::Slider releaseSlider;
        juce::Slider ceilingSlider;
        
        juce::Label threshLabel;
        using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
        std::unique_ptr<SliderAttachment> threshAttachment;
        std::unique_ptr<SliderAttachment> releaseAttachment;
        std::unique_ptr<SliderAttachment> ceilingAttachment;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MaximizerEditor)
    };

}
