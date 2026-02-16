#pragma once

#include <JuceHeader.h>
#include "../Modules/EqualizerModule.h"

namespace oxygen
{

    class EqualizerEditor : public juce::AudioProcessorEditor
    {
    public:
        EqualizerEditor(EqualizerModule&, juce::AudioProcessorValueTreeState&);
        ~EqualizerEditor() override;

        void paint(juce::Graphics&) override;
        void resized() override;

    private:
        EqualizerModule& audioProcessor;
        juce::AudioProcessorValueTreeState& apvts;

        // UI Controls for 16 Bands
        static constexpr int NumBands = EqualizerModule::NumBands;
        
        std::array<std::unique_ptr<juce::Slider>, NumBands> gainSliders;
        std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, NumBands> gainAttachments;
        
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EqualizerEditor)
    };

}
