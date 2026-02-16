#pragma once

#include <JuceHeader.h>
#include "../Modules/MultibandCompressorModule.h"

namespace oxygen
{
    class MultibandCompressorEditor : public juce::AudioProcessorEditor
    {
    public:
        MultibandCompressorEditor(MultibandCompressorModule&, juce::AudioProcessorValueTreeState&);
        ~MultibandCompressorEditor() override;

        void paint(juce::Graphics&) override;
        void resized() override;

    private:
        MultibandCompressorModule& audioProcessor;
        juce::AudioProcessorValueTreeState& apvts;

        struct BandControls
        {
            std::unique_ptr<juce::Slider> thresh, ratio, attack, release, gain;
            std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> threshAtt, ratioAtt, attackAtt, releaseAtt, gainAtt;
        };

        std::array<BandControls, 4> bands;
        
        // Crossovers
        std::unique_ptr<juce::Slider> lowMidX, midHighX, highX;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lowMidXAtt, midHighXAtt, highXAtt;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultibandCompressorEditor)
    };
}
