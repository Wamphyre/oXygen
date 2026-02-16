#pragma once

#include <JuceHeader.h>

namespace oxygen
{
    class ModuleWrapper : public juce::Component
    {
    public:
        ModuleWrapper(const juce::String& name, std::unique_ptr<juce::AudioProcessorEditor> editor, juce::RangedAudioParameter* bypassParam);
        ~ModuleWrapper() override;

        void paint(juce::Graphics& g) override;
        void resized() override;
        
        bool isCollapsed() const { return collapsed; }
        void setCollapsed(bool shouldCollapse);
        
        // Callbacks
        std::function<void()> onHeightChanged;
        std::function<void()> onMoveUp;
        std::function<void()> onMoveDown;

    private:
        juce::String moduleName;
        std::unique_ptr<juce::AudioProcessorEditor> moduleEditor;
        
        juce::TextButton bypassButton { "B" };
        juce::TextButton collapseButton { "^" };
        juce::TextButton upButton { "U" };
        juce::TextButton downButton { "D" };
        
        std::unique_ptr<juce::ParameterAttachment> bypassAttachment;
        
        bool collapsed = false;
        const int headerHeight = 30;
        int expandedHeight = 230;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModuleWrapper)
    };
}
