#include <JuceHeader.h>
#include "ModuleRack.h"
#include "SpectrumAnalyzer.h"
#include "LevelMeter.h"

class OxygenAudioProcessor;

class MainComponent : public juce::Component,
                      public juce::Timer
{
public:
    MainComponent(OxygenAudioProcessor& p);
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;
    
private:
    OxygenAudioProcessor& audioProcessor;
    
    oxygen::SpectrumAnalyzer spectrumAnalyzer;
    oxygen::LevelMeter inputMeterL, inputMeterR;
    oxygen::LevelMeter outputMeterL, outputMeterR;
    
    std::unique_ptr<ModuleRack> moduleRack;
    
    // SVG Assets
    std::unique_ptr<juce::Drawable> logoDrawable;
    std::unique_ptr<juce::Drawable> iconDrawable;
    
    std::unique_ptr<juce::DrawableButton> aiMasterButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
