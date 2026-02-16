#pragma once

#include <JuceHeader.h>
#include "../Modules/StereoImagerModule.h"

namespace oxygen
{

    class StereoImagerEditor : public juce::AudioProcessorEditor
    {
    public:
        StereoImagerEditor(StereoImagerModule&, juce::AudioProcessorValueTreeState&);
        ~StereoImagerEditor() override;

        void paint(juce::Graphics&) override;
        void resized() override;

    private:
        StereoImagerModule& audioProcessor;
        juce::AudioProcessorValueTreeState& apvts;

        juce::Slider lowWidthSlider;
        juce::Slider lowMidWidthSlider;
        juce::Slider highMidWidthSlider;
        juce::Slider highWidthSlider;

        using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
        std::unique_ptr<SliderAttachment> lowAttachment;
        std::unique_ptr<SliderAttachment> lowMidAttachment;
        std::unique_ptr<SliderAttachment> highMidAttachment;
        std::unique_ptr<SliderAttachment> highAttachment;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StereoImagerEditor)
    };

}
