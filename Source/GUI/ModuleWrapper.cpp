#include "ModuleWrapper.h"
#include "Theme.h"

namespace oxygen
{
    ModuleWrapper::ModuleWrapper(const juce::String& name, std::unique_ptr<juce::AudioProcessorEditor> editor, juce::RangedAudioParameter* bypassParam)
        : moduleName(name), moduleEditor(std::move(editor))
    {
        addAndMakeVisible(bypassButton);
        bypassButton.setClickingTogglesState(true);
        bypassButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::red.darker());
        
        if (bypassParam != nullptr)
        {
            bypassAttachment = std::make_unique<juce::ParameterAttachment>(*bypassParam, [this](float v) {
                bypassButton.setToggleState(v > 0.5f, juce::dontSendNotification);
            });
            bypassButton.onClick = [this, bypassParam] {
                bypassParam->setValueNotifyingHost(bypassButton.getToggleState() ? 1.0f : 0.0f);
            };
            // Sync initial state
            bypassButton.setToggleState(bypassParam->getValue() > 0.5f, juce::dontSendNotification);
        }

        addAndMakeVisible(collapseButton);
        collapseButton.setClickingTogglesState(true);
        collapseButton.onClick = [this] { setCollapsed(collapseButton.getToggleState()); };

        addAndMakeVisible(upButton);
        upButton.setButtonText(juce::CharPointer_UTF8("\xe2\x86\x91")); // Up arrow
        upButton.onClick = [this] { if (onMoveUp) onMoveUp(); };

        addAndMakeVisible(downButton);
        downButton.setButtonText(juce::CharPointer_UTF8("\xe2\x86\x93")); // Down arrow
        downButton.onClick = [this] { if (onMoveDown) onMoveDown(); };

        if (moduleEditor != nullptr)
        {
            addAndMakeVisible(moduleEditor.get());
            expandedHeight = moduleEditor->getHeight() + headerHeight;
        }
        
        setSize(780, expandedHeight);
    }

    ModuleWrapper::~ModuleWrapper()
    {
    }

    void ModuleWrapper::setCollapsed(bool shouldCollapse)
    {
        collapsed = shouldCollapse;
        collapseButton.setButtonText(collapsed ? "v" : "^");
        
        if (moduleEditor != nullptr)
            moduleEditor->setVisible(!collapsed);
            
        setSize(getWidth(), collapsed ? headerHeight : expandedHeight);
        
        if (onHeightChanged)
            onHeightChanged();
    }

    void ModuleWrapper::paint(juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat();
        auto currentHeaderHeight = (float)headerHeight; // Use member variable headerHeight
        
        // Background    // Material Design 3 Styling
    g.setColour(oxygen::Theme::Colors::SurfaceVariant);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), oxygen::Theme::Dimens::CornerRadius);
    
    // Header background (optional, or just use the surface variant)
    // g.setColour(oxygen::Theme::Colors::SurfaceVariant.brighter(0.05f));
    // g.fillRoundedRectangle(0, 0, getWidth(), 30, oxygen::Theme::Dimens::CornerRadius); // Top rounded only?

    g.setColour(oxygen::Theme::Colors::OnSurface);
    g.setFont(oxygen::Theme::Fonts::getSubheading());
    g.drawText(moduleName, 15, 5, getWidth() - 40, 20, juce::Justification::centredLeft, true);
    
    // Outline (Subtle)
    g.setColour(oxygen::Theme::Colors::OnSurfaceVariant.withAlpha(0.1f));
    g.drawRoundedRectangle(getLocalBounds().toFloat(), oxygen::Theme::Dimens::CornerRadius, 1.0f); // Draw border around the whole component
        
    }

    void ModuleWrapper::resized()
    {
        auto bounds = getLocalBounds();
        auto headerArea = bounds.removeFromTop(headerHeight);
        
        collapseButton.setBounds(headerArea.removeFromRight(30).reduced(5));
        downButton.setBounds(headerArea.removeFromRight(30).reduced(5));
        upButton.setBounds(headerArea.removeFromRight(30).reduced(5));
        bypassButton.setBounds(headerArea.removeFromRight(30).reduced(5));
        
        if (moduleEditor != nullptr && !collapsed)
        {
            moduleEditor->setBounds(bounds);
        }
    }
}
