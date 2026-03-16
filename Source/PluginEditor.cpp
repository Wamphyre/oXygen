#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
OxygenAudioProcessorEditor::OxygenAudioProcessorEditor (OxygenAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    mainComponent = std::make_unique<MainComponent>(audioProcessor);
    addAndMakeVisible(mainComponent.get());

    setSize(mainComponent->getWidth(), mainComponent->getHeight());
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
