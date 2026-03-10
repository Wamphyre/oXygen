#include <JuceHeader.h>
#include "ModuleRack.h"
#include "SpectrumAnalyzer.h"
#include "LevelMeter.h"
#include "../AI/InferenceEngine.h"
#include <optional>

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
    void startAssistantListening();
    void cancelAssistantListening();
    void completeAssistantListening();
    void showAssistantApplyConfirmation();
    void closeAssistantListenWindow();
    
private:
    enum class AssistantUiState
    {
        idle = 0,
        listening
    };

    OxygenAudioProcessor& audioProcessor;
    
    oxygen::SpectrumAnalyzer spectrumAnalyzer;
    oxygen::LevelMeter inputMeterL, inputMeterR;
    oxygen::LevelMeter outputMeterL, outputMeterR;
    
    std::unique_ptr<ModuleRack> moduleRack;
    
    // SVG Assets
    std::unique_ptr<juce::Drawable> iconDrawable;
    
    std::unique_ptr<juce::DrawableButton> masterAssistButton;
    juce::Label assistantIntensityLabel;
    juce::ComboBox assistantIntensityBox;
    AssistantUiState assistantUiState = AssistantUiState::idle;
    double assistantListenProgress = 0.0;
    double assistantListenStartMs = 0.0;
    double assistantListenDurationMs = 60000.0;
    std::optional<oxygen::MasteringParameters> pendingAssistantParameters;
    juce::Component::SafePointer<juce::AlertWindow> assistantListenWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
