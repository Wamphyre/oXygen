#include "MainComponent.h"
#include "Theme.h"
#include "../PluginProcessor.h"
#include "../Assets.h"

using namespace oxygen;

MainComponent::MainComponent(OxygenAudioProcessor& p)
    : audioProcessor(p),
      spectrumAnalyzer(p.getAudioBufferQueue())
{
    // Rack
    moduleRack = std::make_unique<ModuleRack>(p);
    addAndMakeVisible(moduleRack.get());
    moduleRack->updateModuleList(); // Fix: Populate the rack on startup
    
    // Spectrum Analyzer (Visualizer)
    addAndMakeVisible(spectrumAnalyzer);
    
    // Meters
    addAndMakeVisible(inputMeterL);
    addAndMakeVisible(inputMeterR);
    addAndMakeVisible(outputMeterL);
    addAndMakeVisible(outputMeterR);

    // Parse SVGs
    auto logoXml = juce::parseXML(oxygen::Assets::oxygenLogoSvg);
    if (logoXml) logoDrawable = juce::Drawable::createFromSVG(*logoXml);
    
    auto iconXml = juce::parseXML(oxygen::Assets::oxygenIconSvg);
    if (iconXml) iconDrawable = juce::Drawable::createFromSVG(*iconXml);
    
    // AI Master Button
    aiMasterButton = std::make_unique<juce::DrawableButton>("AI Master", juce::DrawableButton::ImageAboveTextLabel);
    if (iconDrawable)
    {
        aiMasterButton->setImages(iconDrawable.get());
        // Reduce detailed icon for button use if needed, or use as is
    }
    aiMasterButton->setButtonText("AI MASTER");
    aiMasterButton->setColour(juce::TextButton::textColourOnId, juce::Colours::cyan);
    aiMasterButton->onClick = [this] { audioProcessor.triggerAutoMastering(); };
    addAndMakeVisible(aiMasterButton.get());
    
    setSize(1000, 700); // Increased default size
    startTimerHz(60);   // Smoother 60fps update
}

MainComponent::~MainComponent()
{
    stopTimer();
}

void MainComponent::timerCallback()
{
    inputMeterL.setLevel(audioProcessor.getInputLevel());
    inputMeterR.setLevel(audioProcessor.getInputLevel());
    outputMeterL.setLevel(audioProcessor.getOutputLevel());
    outputMeterR.setLevel(audioProcessor.getOutputLevel());
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    // Header (Top 60px for branding)
    auto headerArea = area.removeFromTop(60);
    
    // AI Button
    if (aiMasterButton.get()) // Fix unique_ptr check
    {
        auto buttonArea = headerArea.removeFromRight(120).reduced(10);
        aiMasterButton->setBounds(buttonArea);
    }
    
    // Spectrum Analyzer (Top 150px - Compact per user request)
    auto visualizerArea = area.removeFromTop(150).reduced(10, 5);
    spectrumAnalyzer.setBounds(visualizerArea);
    
    // Meters Sidebar (Left/Right) - Adjusted to fit new layout
    auto leftSidebar = area.removeFromLeft(50);
    auto rightSidebar = area.removeFromRight(50);
    
    // Segmented Meters (Left)
    inputMeterL.setBounds(leftSidebar.removeFromLeft(20).reduced(2, 0));
    inputMeterR.setBounds(leftSidebar.reduced(2, 0));
    
    // Segmented Meters (Right)
    outputMeterL.setBounds(rightSidebar.removeFromLeft(20).reduced(2, 0));
    outputMeterR.setBounds(rightSidebar.reduced(2, 0));
    
    // Module Rack (Remaining center area)
    moduleRack->setBounds(area.reduced(5));
}

void MainComponent::paint(juce::Graphics& g)
{
    // Modern Dark Gradient Background
    g.fillAll(oxygen::Theme::Colors::Background);
    
    // Subtle header separator or glow
    g.setColour(oxygen::Theme::Colors::OnSurfaceVariant.withAlpha(0.1f));
    g.drawHorizontalLine(60, 0.0f, (float)getWidth());

    // Logo / Branding (SVG)
    if (logoDrawable)
    {
        auto headerArea = getLocalBounds().removeFromTop(60).reduced(20, 10).toFloat();
        auto logoHeight = headerArea.getHeight();
        auto logoWidth = logoHeight * (800.0f / 300.0f);
        
        logoDrawable->drawWithin(g, juce::Rectangle<float>(headerArea.getX(), headerArea.getY(), logoWidth, logoHeight),
                                 juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid, 1.0f);
    }
    else
    {
        g.setColour(oxygen::Theme::Colors::OnSurface);
        g.setFont(oxygen::Theme::Fonts::getHeading().withHeight(32.0f));
        g.drawText("oXygen", getLocalBounds().removeFromTop(60).reduced(20, 0), juce::Justification::left, true);
    }
    
    // Draw Grid for Visualizer (Background)
    auto visBounds = spectrumAnalyzer.getBounds().toFloat();
    if (!visBounds.isEmpty())
    {
        g.setColour(oxygen::Theme::Colors::OnSurfaceVariant.withAlpha(0.1f));
        g.drawRect(visBounds);
        
        // Frequency Lines (Logarithmic approx)
        float freqs[] = { 100.0f, 1000.0f, 10000.0f };
        for (float f : freqs)
        {
            float normX = std::log10(f / 20.0f) / std::log10(20000.0f / 20.0f);
            if (normX >= 0.0f && normX <= 1.0f)
                g.drawVerticalLine((int)(visBounds.getX() + normX * visBounds.getWidth()), visBounds.getY(), visBounds.getBottom());
        }
        
        // dB Lines
        float dbs[] = { -6.0f, -12.0f, -24.0f };
        for (float db : dbs)
        {
            // Assuming simplified linear mapping for now or map to analyzer range
             float normY = std::abs(db) / 60.0f; // Approx range 0 to -60
             if (normY <= 1.0f)
                 g.drawHorizontalLine((int)(visBounds.getY() + normY * visBounds.getHeight()), visBounds.getX(), visBounds.getRight());
        }
    }
}
