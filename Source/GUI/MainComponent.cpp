#include "MainComponent.h"
#include "Theme.h"
#include "../PluginProcessor.h"
#include "../Assets.h"
#include <array>

using namespace oxygen;

namespace
{
    constexpr int headerHeight = 96;

    int comboIdForGenre(oxygen::AssistantGenre genre)
    {
        return 1 + (int) genre;
    }

    int comboIdForDirection(oxygen::ArtisticDirection direction)
    {
        return 1 + (int) direction;
    }
}

MainComponent::MainComponent(OxygenAudioProcessor& p)
    : audioProcessor(p),
      spectrumAnalyzer(p.getAudioBufferQueue(), [&p] { return p.getSampleRate(); })
{
    // Rack
    moduleRack = std::make_unique<ModuleRack>(p);
    addAndMakeVisible(moduleRack.get());
    moduleRack->updateModuleList(); // Fix: Populate the rack on startup
    audioProcessor.setGraphChangedCallback([this]
    {
        if (moduleRack)
            moduleRack->updateModuleList();
    });
    
    // Spectrum Analyzer (Visualizer)
    addAndMakeVisible(spectrumAnalyzer);
    
    // Meters
    addAndMakeVisible(inputMeterL);
    addAndMakeVisible(inputMeterR);
    addAndMakeVisible(outputMeterL);
    addAndMakeVisible(outputMeterR);

    auto iconXml = juce::parseXML(oxygen::Assets::oxygenIconSvg);
    if (iconXml) iconDrawable = juce::Drawable::createFromSVG(*iconXml);
    
    // Assistant Button
    masterAssistButton = std::make_unique<juce::DrawableButton>("Master Assist", juce::DrawableButton::ImageAboveTextLabel);
    if (iconDrawable)
    {
        masterAssistButton->setImages(iconDrawable.get());
    }
    masterAssistButton->setButtonText("MASTER ASSIST");
    masterAssistButton->setColour(juce::TextButton::textColourOnId, juce::Colours::cyan);
    masterAssistButton->onClick = [this] { audioProcessor.triggerMasterAssistant(); };
    addAndMakeVisible(masterAssistButton.get());

    genreLabel.setText("GENRE", juce::dontSendNotification);
    genreLabel.setColour(juce::Label::textColourId, Theme::Colors::OnSurfaceVariant);
    genreLabel.setFont(Theme::Fonts::getBody().withHeight(11.0f));
    addAndMakeVisible(genreLabel);

    genreBox.addItem("Universal", comboIdForGenre(AssistantGenre::Universal));
    genreBox.addItem("Pop", comboIdForGenre(AssistantGenre::Pop));
    genreBox.addItem("Hip Hop", comboIdForGenre(AssistantGenre::HipHop));
    genreBox.addItem("Electronic", comboIdForGenre(AssistantGenre::Electronic));
    genreBox.addItem("Rock", comboIdForGenre(AssistantGenre::Rock));
    genreBox.addItem("Acoustic", comboIdForGenre(AssistantGenre::Acoustic));
    genreBox.addItem("Orchestral", comboIdForGenre(AssistantGenre::Orchestral));
    genreBox.setSelectedId(comboIdForGenre(audioProcessor.getAssistantGenre()), juce::dontSendNotification);
    genreBox.onChange = [this]
    {
        audioProcessor.setAssistantGenre((AssistantGenre) juce::jmax(0, genreBox.getSelectedId() - 1));
    };
    addAndMakeVisible(genreBox);

    directionLabel.setText("DIRECTION", juce::dontSendNotification);
    directionLabel.setColour(juce::Label::textColourId, Theme::Colors::OnSurfaceVariant);
    directionLabel.setFont(Theme::Fonts::getBody().withHeight(11.0f));
    addAndMakeVisible(directionLabel);

    directionBox.addItem("Balanced", comboIdForDirection(ArtisticDirection::Balanced));
    directionBox.addItem("Transparent", comboIdForDirection(ArtisticDirection::Transparent));
    directionBox.addItem("Warm", comboIdForDirection(ArtisticDirection::Warm));
    directionBox.addItem("Punchy", comboIdForDirection(ArtisticDirection::Punchy));
    directionBox.addItem("Wide", comboIdForDirection(ArtisticDirection::Wide));
    directionBox.addItem("Aggressive", comboIdForDirection(ArtisticDirection::Aggressive));
    directionBox.setSelectedId(comboIdForDirection(audioProcessor.getArtisticDirection()), juce::dontSendNotification);
    directionBox.onChange = [this]
    {
        audioProcessor.setArtisticDirection((ArtisticDirection) juce::jmax(0, directionBox.getSelectedId() - 1));
    };
    addAndMakeVisible(directionBox);
    
    setSize(1100, 1000); // Increased default size for better visibility
    startTimerHz(60);   // Smoother 60fps update
}

MainComponent::~MainComponent()
{
    stopTimer();
    audioProcessor.setGraphChangedCallback({});
}

void MainComponent::timerCallback()
{
    inputMeterL.setLevel(audioProcessor.getInputLevel(0));
    inputMeterR.setLevel(audioProcessor.getInputLevel(1));
    outputMeterL.setLevel(audioProcessor.getOutputLevel(0));
    outputMeterR.setLevel(audioProcessor.getOutputLevel(1));
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    // Header
    auto headerArea = area.removeFromTop(headerHeight);
    
    auto controlsArea = headerArea.removeFromRight(540).reduced(6, 8);

    if (masterAssistButton.get())
    {
        auto buttonArea = controlsArea.removeFromRight(180).reduced(5, 2);
        masterAssistButton->setBounds(buttonArea);
    }

    auto directionArea = controlsArea.removeFromRight(170).reduced(6, 2);
    auto genreArea = controlsArea.removeFromRight(160).reduced(6, 2);

    auto genreLabelArea = genreArea.removeFromTop(16);
    genreLabel.setBounds(genreLabelArea);
    genreBox.setBounds(genreArea.reduced(0, 2));

    auto directionLabelArea = directionArea.removeFromTop(16);
    directionLabel.setBounds(directionLabelArea);
    directionBox.setBounds(directionArea.reduced(0, 2));
    
    // Spectrum Analyzer (Top 120px - Slightly more compact)
    auto visualizerArea = area.removeFromTop(120).reduced(10, 5);
    spectrumAnalyzer.setBounds(visualizerArea);
    
    // Meters Sidebar (Left/Right) - Adjusted to fit new layout
    auto leftSidebar = area.removeFromLeft(70);
    auto rightSidebar = area.removeFromRight(70);
    
    // Segmented Meters (Left)
    // Reserve bottom 20px for text
    auto leftMetersArea = leftSidebar.removeFromTop(leftSidebar.getHeight() - 25);
    inputMeterL.setBounds(leftMetersArea.removeFromLeft(35).reduced(2, 0));
    inputMeterR.setBounds(leftMetersArea.reduced(2, 0));
    
    // Segmented Meters (Right)
    auto rightMetersArea = rightSidebar.removeFromTop(rightSidebar.getHeight() - 25);
    outputMeterL.setBounds(rightMetersArea.removeFromLeft(35).reduced(2, 0));
    outputMeterR.setBounds(rightMetersArea.reduced(2, 0));
    
    // Module Rack (Remaining center area)
    moduleRack->setBounds(area.reduced(5));
}

void MainComponent::paint(juce::Graphics& g)
{
    // Modern Dark Gradient Background
    g.fillAll(oxygen::Theme::Colors::Background);
    
    // Subtle header separator or glow
    g.setColour(oxygen::Theme::Colors::OnSurfaceVariant.withAlpha(0.1f));
    g.drawHorizontalLine(headerHeight, 0.0f, (float) getWidth());

    drawBranding(g, getLocalBounds().removeFromTop(headerHeight).reduced(10, 5).toFloat());
    
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

    // Draw Meter Text Values
    drawStereoMeterValue(g,
                         audioProcessor.getInputLevel(0),
                         audioProcessor.getInputLevel(1),
                         inputMeterL.getX(),
                         inputMeterL.getBottom() + 5,
                         inputMeterL.getWidth() * 2 + 10);
    drawStereoMeterValue(g,
                         audioProcessor.getOutputLevel(0),
                         audioProcessor.getOutputLevel(1),
                         outputMeterL.getX(),
                         outputMeterL.getBottom() + 5,
                         outputMeterL.getWidth() * 2 + 10);
}

void MainComponent::drawStereoMeterValue(juce::Graphics& g, float leftLevel, float rightLevel, int x, int y, int w)
{
    g.setColour(oxygen::Theme::Colors::OnSurfaceVariant);
    g.setFont(oxygen::Theme::Fonts::getBody().withHeight(12.0f));
    
    const auto text = "L " + formatLevelText(leftLevel) + "  R " + formatLevelText(rightLevel);
    g.drawText(text, x, y, w, 20, juce::Justification::centred, false);
}

juce::String MainComponent::formatLevelText(float level) const
{
    const float db = juce::Decibels::gainToDecibels(level);
    return (db < -60.0f) ? "-inf" : juce::String(db, 1) + " dB";
}

void MainComponent::drawBranding(juce::Graphics& g, juce::Rectangle<float> area)
{
    auto iconArea = area.removeFromLeft(70.0f).reduced(4.0f);
    auto textArea = area.reduced(4.0f, 0.0f);

    // Neon X icon with soft glow
    const auto iconStroke = juce::jmax(4.0f, iconArea.getWidth() * 0.12f);
    const auto innerStroke = juce::jmax(1.5f, iconArea.getWidth() * 0.035f);
    const auto iconBounds = iconArea.reduced(6.0f);
    const auto x1 = iconBounds.getX();
    const auto y1 = iconBounds.getY();
    const auto x2 = iconBounds.getRight();
    const auto y2 = iconBounds.getBottom();

    g.setColour(juce::Colours::magenta.withAlpha(0.18f));
    g.drawLine(x1, y1, x2, y2, iconStroke + 8.0f);
    g.drawLine(x1, y2, x2, y1, iconStroke + 8.0f);
    g.setColour(juce::Colours::cyan.withAlpha(0.24f));
    g.drawLine(x1, y1, x2, y2, iconStroke + 4.0f);
    g.drawLine(x1, y2, x2, y1, iconStroke + 4.0f);

    juce::ColourGradient iconGradient(juce::Colours::cyan, x1, y1,
                                      juce::Colours::magenta, x2, y2, false);
    g.setGradientFill(iconGradient);
    g.drawLine(x1, y1, x2, y2, iconStroke);
    g.drawLine(x1, y2, x2, y1, iconStroke);

    g.setColour(juce::Colours::cyan.withAlpha(0.5f));
    g.drawLine(iconBounds.getCentreX(), iconBounds.getY(), iconBounds.getCentreX(), iconBounds.getY() + iconBounds.getHeight() * 0.24f, innerStroke);
    g.drawLine(iconBounds.getCentreX(), iconBounds.getBottom() - iconBounds.getHeight() * 0.24f, iconBounds.getCentreX(), iconBounds.getBottom(), innerStroke);
    g.drawLine(iconBounds.getX(), iconBounds.getCentreY(), iconBounds.getX() + iconBounds.getWidth() * 0.24f, iconBounds.getCentreY(), innerStroke);
    g.drawLine(iconBounds.getRight() - iconBounds.getWidth() * 0.24f, iconBounds.getCentreY(), iconBounds.getRight(), iconBounds.getCentreY(), innerStroke);

    // Main title as path so the glow survives JUCE rendering regardless of SVG support
    auto titleFont = oxygen::Theme::Fonts::getHeading().withHeight(area.getHeight() * 0.78f);
    auto titleBaseline = textArea.getY() + textArea.getHeight() * 0.72f;

    juce::GlyphArrangement titleGlyphs;
    titleGlyphs.addLineOfText(titleFont, "oXygen", textArea.getX(), titleBaseline);
    juce::Path titlePath;
    titleGlyphs.createPath(titlePath);

    g.setColour(juce::Colours::magenta.withAlpha(0.18f));
    g.strokePath(titlePath, juce::PathStrokeType(14.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(juce::Colours::cyan.withAlpha(0.24f));
    g.strokePath(titlePath, juce::PathStrokeType(8.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(juce::Colours::white.withAlpha(0.18f));
    g.strokePath(titlePath, juce::PathStrokeType(3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::ColourGradient titleGradient(juce::Colours::white, textArea.getX(), textArea.getY(),
                                       juce::Colours::cyan, textArea.getX() + textArea.getWidth() * 0.35f, titleBaseline, false);
    titleGradient.addColour(0.52, juce::Colours::magenta);
    titleGradient.addColour(1.0, juce::Colours::white);
    g.setGradientFill(titleGradient);
    g.fillPath(titlePath);

    auto subtitleFont = oxygen::Theme::Fonts::getBody().withHeight(area.getHeight() * 0.28f);
    auto subtitleBaseline = textArea.getBottom() - area.getHeight() * 0.02f;
    juce::GlyphArrangement subtitleGlyphs;
    subtitleGlyphs.addLineOfText(subtitleFont, "MASTERING SUITE", textArea.getX() + 4.0f, subtitleBaseline);
    juce::Path subtitlePath;
    subtitleGlyphs.createPath(subtitlePath);

    g.setColour(juce::Colours::cyan.withAlpha(0.16f));
    g.strokePath(subtitlePath, juce::PathStrokeType(6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(juce::Colours::white.withAlpha(0.86f));
    g.fillPath(subtitlePath);
}
