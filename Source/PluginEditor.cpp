#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::Rectangle<int> getInitialEditorBounds(int preferredWidth, int preferredHeight)
    {
        if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        {
            const auto availableArea = display->userArea.reduced(40, 40);
            return { juce::jmax(1, juce::jmin(preferredWidth, availableArea.getWidth())),
                     juce::jmax(1, juce::jmin(preferredHeight, availableArea.getHeight())) };
        }

        return { preferredWidth, preferredHeight };
    }
}

//==============================================================================
OxygenAudioProcessorEditor::OxygenAudioProcessorEditor (OxygenAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    mainComponent = std::make_unique<MainComponent>(audioProcessor);
    addAndMakeVisible(mainComponent.get());

    const auto preferredWidth = mainComponent->getWidth();
    const auto preferredHeight = mainComponent->getHeight();
    const auto initialBounds = getInitialEditorBounds(preferredWidth, preferredHeight);
    const int minWidth = juce::jmin(initialBounds.getWidth(), juce::jmin(preferredWidth, 900));
    const int minHeight = juce::jmin(initialBounds.getHeight(), juce::jmin(preferredHeight, 720));

    setResizable(true, false);
    setResizeLimits(minWidth, minHeight, preferredWidth, preferredHeight);
    setSize(initialBounds.getWidth(), initialBounds.getHeight());
}

OxygenAudioProcessorEditor::~OxygenAudioProcessorEditor()
{
    mainComponent = nullptr;
}

//==============================================================================
void OxygenAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void OxygenAudioProcessorEditor::resized()
{
    if (mainComponent)
        mainComponent->setBounds(getLocalBounds());
}
