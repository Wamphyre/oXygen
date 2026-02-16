#pragma once

#include <JuceHeader.h>

class OxygenAudioProcessor;
namespace oxygen { class ModuleWrapper; }

class ModuleRack : public juce::Viewport
{
public:
    ModuleRack(OxygenAudioProcessor& p);
    ~ModuleRack() override;

    // Method to refresh the rack when modules are added/removed in the processor
    void updateModuleList();
    void resized() override;

private:
    OxygenAudioProcessor& audioProcessor;
    
    // Inner component to hold the stack of editors
    std::unique_ptr<juce::Component> contentComponent;
    juce::OwnedArray<oxygen::ModuleWrapper> moduleWrappers;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModuleRack)
};
