#include "ModuleWrapper.h"
#include "Theme.h"
#include "../Modules/MaximizerModule.h"

namespace oxygen
{
    ModuleWrapper::ModuleWrapper(const juce::String& name,
                                 std::unique_ptr<juce::AudioProcessorEditor> editor,
                                 juce::RangedAudioParameter* bypassParam,
                                 juce::AudioProcessor* processor)
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

        if (auto* maximizerModule = dynamic_cast<oxygen::MaximizerModule*>(processor))
        {
            addAndMakeVisible(modeSelector);
            modeSelector.addItem("Transparent", 1);
            modeSelector.addItem("Loud", 2);
            modeSelector.addItem("Safe", 3);
            modeSelector.setJustificationType(juce::Justification::centredLeft);
            modeSelector.setColour(juce::ComboBox::backgroundColourId, oxygen::Theme::Colors::SurfaceVariant.darker(0.15f));
            modeSelector.setColour(juce::ComboBox::outlineColourId, oxygen::Theme::Colors::OnSurfaceVariant.withAlpha(0.28f));
            modeSelector.setColour(juce::ComboBox::textColourId, oxygen::Theme::Colors::OnSurface);
            modeSelector.setColour(juce::ComboBox::arrowColourId, oxygen::Theme::Colors::Primary);
            modeAttachment = std::make_unique<ComboBoxAttachment>(maximizerModule->apvts, "Mode", modeSelector);
        }

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
        g.setColour(oxygen::Theme::Colors::SurfaceVariant);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), oxygen::Theme::Dimens::CornerRadius);

        constexpr int headerButtonWidth = 40;
        constexpr int headerModeWidth = 148;
        const int rightControlsWidth = headerButtonWidth * 4
                                     + (modeAttachment != nullptr ? headerModeWidth + 8 : 0);

        g.setColour(oxygen::Theme::Colors::OnSurface);
        g.setFont(oxygen::Theme::Fonts::getSubheading());
        g.drawText(moduleName,
                   15,
                   5,
                   juce::jmax(80, getWidth() - rightControlsWidth - 28),
                   20,
                   juce::Justification::centredLeft,
                   true);

        g.setColour(oxygen::Theme::Colors::OnSurfaceVariant.withAlpha(0.1f));
        g.drawRoundedRectangle(getLocalBounds().toFloat(), oxygen::Theme::Dimens::CornerRadius, 1.0f);
    }

    void ModuleWrapper::resized()
    {
        auto bounds = getLocalBounds();
        auto headerArea = bounds.removeFromTop(headerHeight);

        collapseButton.setBounds(headerArea.removeFromRight(40).reduced(5));
        downButton.setBounds(headerArea.removeFromRight(40).reduced(5));
        upButton.setBounds(headerArea.removeFromRight(40).reduced(5));
        bypassButton.setBounds(headerArea.removeFromRight(40).reduced(5));

        if (modeAttachment != nullptr)
        {
            auto modeArea = headerArea.removeFromRight(148).reduced(4, 4);
            modeSelector.setBounds(modeArea);
            headerArea.removeFromRight(4);
        }
        
        if (moduleEditor != nullptr && !collapsed)
        {
            moduleEditor->setBounds(bounds);
        }
    }
}
