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
    
    void drawStereoMeterValue(juce::Graphics& g, float leftLevel, float rightLevel, int x, int y, int w);
    void drawBranding(juce::Graphics& g, juce::Rectangle<float> area);
    juce::String formatLevelText(float level) const;
    
private:
    OxygenAudioProcessor& audioProcessor;
    
    oxygen::SpectrumAnalyzer spectrumAnalyzer;
    oxygen::LevelMeter inputMeterL, inputMeterR;
    oxygen::LevelMeter outputMeterL, outputMeterR;
    
    std::unique_ptr<ModuleRack> moduleRack;
    
    // SVG Assets
    std::unique_ptr<juce::Drawable> iconDrawable;
    
    std::unique_ptr<juce::DrawableButton> masterAssistButton;
    juce::Label genreLabel;
    juce::Label directionLabel;
    juce::ComboBox genreBox;
    juce::ComboBox directionBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
