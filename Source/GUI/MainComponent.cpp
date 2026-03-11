#include "MainComponent.h"
#include "Theme.h"
#include "../PluginProcessor.h"
#include "../Assets.h"
#include <memory>

using namespace oxygen;

namespace
{
    constexpr int headerHeight = 96;
    constexpr int meterTextHeight = 30;
}

MainComponent::MainComponent(OxygenAudioProcessor& p)
    : audioProcessor(p),
      spectrumAnalyzer(p.getInputAudioBufferQueue(),
                       p.getOutputAudioBufferQueue(),
                       [&p] { return p.getSampleRate(); })
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
    masterAssistButton->onClick = [this]
    {
        startAssistantListening();
    };
    addAndMakeVisible(masterAssistButton.get());
    
    setSize(1100, 1000); // Increased default size for better visibility
    startTimerHz(60);   // Smoother 60fps update
}

MainComponent::~MainComponent()
{
    cancelAssistantListening();
    stopTimer();
    audioProcessor.setGraphChangedCallback({});
}

void MainComponent::timerCallback()
{
    inputMeterL.setLevel(audioProcessor.getInputLevel(0));
    inputMeterR.setLevel(audioProcessor.getInputLevel(1));
    outputMeterL.setLevel(audioProcessor.getOutputLevel(0));
    outputMeterR.setLevel(audioProcessor.getOutputLevel(1));

    if (assistantUiState == AssistantUiState::listening)
    {
        const auto nowMs = juce::Time::getMillisecondCounterHiRes();
        const auto elapsedMs = nowMs - assistantListenStartMs;
        assistantListenProgress = juce::jlimit(0.0, 1.0, elapsedMs / assistantListenDurationMs);

        if (assistantListenWindow != nullptr)
            assistantListenWindow->repaint();

        if (assistantListenProgress >= 1.0)
            completeAssistantListening();
    }
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    // Header
    auto headerArea = area.removeFromTop(headerHeight);
    
    if (masterAssistButton.get() != nullptr)
    {
        auto buttonArea = headerArea.removeFromRight(190).reduced(6, 10);
        masterAssistButton->setBounds(buttonArea);
    }
    
    // Spectrum Analyzer (Top 120px - Slightly more compact)
    auto visualizerArea = area.removeFromTop(120).reduced(10, 5);
    spectrumAnalyzer.setBounds(visualizerArea);
    
    // Meters Sidebar (Left/Right) - Adjusted to fit new layout
    auto leftSidebar = area.removeFromLeft(70);
    auto rightSidebar = area.removeFromRight(70);
    
    // Segmented Meters (Left)
    auto leftMetersArea = leftSidebar.removeFromTop(leftSidebar.getHeight() - meterTextHeight);
    inputMeterL.setBounds(leftMetersArea.removeFromLeft(35).reduced(2, 0));
    inputMeterR.setBounds(leftMetersArea.reduced(2, 0));
    
    // Segmented Meters (Right)
    auto rightMetersArea = rightSidebar.removeFromTop(rightSidebar.getHeight() - meterTextHeight);
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
                         { inputMeterL.getX(),
                           inputMeterL.getBottom() + 4,
                           inputMeterR.getRight() - inputMeterL.getX(),
                           meterTextHeight - 4 });
    drawStereoMeterValue(g,
                         audioProcessor.getOutputLevel(0),
                         audioProcessor.getOutputLevel(1),
                         { outputMeterL.getX(),
                           outputMeterL.getBottom() + 4,
                           outputMeterR.getRight() - outputMeterL.getX(),
                           meterTextHeight - 4 });
}

void MainComponent::drawStereoMeterValue(juce::Graphics& g,
                                         float leftLevel,
                                         float rightLevel,
                                         juce::Rectangle<int> bounds)
{
    g.setColour(oxygen::Theme::Colors::OnSurfaceVariant);
    g.setFont(oxygen::Theme::Fonts::getBody().withHeight(10.0f));

    auto topLine = bounds.removeFromTop(bounds.getHeight() / 2);
    g.drawText("L " + formatLevelText(leftLevel), topLine, juce::Justification::centred, false);
    g.drawText("R " + formatLevelText(rightLevel), bounds, juce::Justification::centred, false);
}

juce::String MainComponent::formatLevelText(float level) const
{
    const float db = juce::Decibels::gainToDecibels(level);
    return (db < -60.0f) ? "-inf" : juce::String(db, 1) + " dB";
}

void MainComponent::startAssistantListening()
{
    if (assistantUiState != AssistantUiState::idle)
        return;

    closeAssistantListenWindow();

    audioProcessor.resetAssistantAnalysisCapture();
    assistantUiState = AssistantUiState::listening;
    assistantListenProgress = 0.0;
    assistantListenStartMs = juce::Time::getMillisecondCounterHiRes();

    if (masterAssistButton != nullptr)
    {
        masterAssistButton->setButtonText("LISTENING...");
        masterAssistButton->setEnabled(false);
    }

    auto* listenWindow = new juce::AlertWindow("Master Assistant Listening",
                                               "The plugin is listening to your mix.\n"
                                               "Volume may increase after processing.\n"
                                               "If no signal is detected, play the loudest section of your mix.",
                                               juce::AlertWindow::InfoIcon);
    listenWindow->addProgressBarComponent(assistantListenProgress);
    listenWindow->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    listenWindow->setAlwaysOnTop(true);
    listenWindow->centreAroundComponent(this, 520, 210);

    juce::Component::SafePointer<MainComponent> safeThis(this);
    listenWindow->enterModalState(true,
                                  juce::ModalCallbackFunction::create([safeThis](int result)
                                  {
                                      if (safeThis == nullptr)
                                          return;

                                      if (safeThis->assistantUiState == AssistantUiState::listening && result == 0)
                                          safeThis->cancelAssistantListening();
                                  }),
                                  true);
    assistantListenWindow = listenWindow;
}

void MainComponent::cancelAssistantListening()
{
    assistantUiState = AssistantUiState::idle;
    assistantListenProgress = 0.0;
    assistantListenStartMs = 0.0;

    if (masterAssistButton != nullptr)
    {
        masterAssistButton->setButtonText("MASTER ASSIST");
        masterAssistButton->setEnabled(true);
    }
    pendingAssistantParameters.reset();

    closeAssistantListenWindow();
}

void MainComponent::completeAssistantListening()
{
    if (assistantUiState != AssistantUiState::listening)
        return;

    assistantUiState = AssistantUiState::idle;

    if (masterAssistButton != nullptr)
    {
        masterAssistButton->setButtonText("MASTER ASSIST");
        masterAssistButton->setEnabled(true);
    }

    closeAssistantListenWindow();

    if (!audioProcessor.hasAssistantCapturedSignal())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               "Master Assistant",
                                               "No audio signal was detected.\n"
                                               "Play the loudest section of your mix and try again.");
        return;
    }

    oxygen::MasteringParameters suggestion;
    if (!audioProcessor.createMasterAssistantSuggestion(suggestion))
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               "Master Assistant",
                                               "Unable to build the mastering suggestion.\n"
                                               "Try listening again with a stronger signal.");
        return;
    }

    pendingAssistantParameters = suggestion;

    showAssistantApplyConfirmation();
}

void MainComponent::showAssistantApplyConfirmation()
{
    if (!pendingAssistantParameters.has_value())
        return;

    auto* confirmationWindow = new juce::AlertWindow("Master Assistant",
                                                     "Analysis complete.\n"
                                                     "Apply the recommended mastering settings to the modules?",
                                                     juce::AlertWindow::QuestionIcon);
    confirmationWindow->addButton("Apply", 1);
    confirmationWindow->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    confirmationWindow->setAlwaysOnTop(true);
    confirmationWindow->centreAroundComponent(this, 400, 190);

    juce::Component::SafePointer<MainComponent> safeThis(this);
    confirmationWindow->enterModalState(true,
                                        juce::ModalCallbackFunction::create([safeThis](int result)
                                        {
                                            if (safeThis == nullptr)
                                                return;

                                            if (result == 1 && safeThis->pendingAssistantParameters.has_value())
                                            {
                                                if (!safeThis->audioProcessor.applyMasterAssistantSuggestion(*safeThis->pendingAssistantParameters))
                                                {
                                                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                                                           "Master Assistant",
                                                                                           "Unable to apply settings.\n"
                                                                                           "No valid audio profile is available.");
                                                }
                                            }

                                            safeThis->pendingAssistantParameters.reset();
                                        }),
                                        true);
}

void MainComponent::closeAssistantListenWindow()
{
    auto* listenWindow = assistantListenWindow.getComponent();
    assistantListenWindow = nullptr;

    if (listenWindow == nullptr)
        return;

    if (listenWindow->isCurrentlyModal())
        listenWindow->exitModalState(0);
    else
        delete listenWindow;
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
