#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "GUI/MainComponent.h"

class OxygenAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    OxygenAudioProcessorEditor (OxygenAudioProcessor&);
    ~OxygenAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    OxygenAudioProcessor& audioProcessor;
    std::unique_ptr<MainComponent> mainComponent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OxygenAudioProcessorEditor)
};
