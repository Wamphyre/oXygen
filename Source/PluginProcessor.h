#pragma once

#include <JuceHeader.h>
#include "AI/InferenceEngine.h"
#include "GUI/AudioBufferQueue.h"
#include <functional>
#include <vector>

//==============================================================================
/**
*/
class OxygenAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    OxygenAudioProcessor();
    ~OxygenAudioProcessor() override;
    
    oxygen::AudioBufferQueue& getAudioBufferQueue() { return audioBufferQueue; }

    float getInputLevel() const { return juce::jmax(inputLevelL.load(), inputLevelR.load()); }
    float getOutputLevel() const { return juce::jmax(outputLevelL.load(), outputLevelR.load()); }
    float getInputLevel(int channel) const;
    float getOutputLevel(int channel) const;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

private:
    std::unique_ptr<juce::AudioProcessorGraph> mainProcessorGraph;
    
    // Visualizer Data Source
    oxygen::AudioBufferQueue audioBufferQueue;

    // AI Engine
    std::unique_ptr<oxygen::InferenceEngine> inferenceEngine;

    void initialiseGraph();
    void updateGraphConnections();
    
public:
    juce::AudioProcessorGraph& getProcessorGraph() { return *mainProcessorGraph; }
    
    // Module Management
    void moveModuleUp(int index);
    void moveModuleDown(int index);
    const std::vector<juce::AudioProcessorGraph::Node::Ptr>& getModuleNodes() const { return moduleNodes; }

    void triggerMasterAssistant();
    void setGraphChangedCallback(std::function<void()> callback);
    oxygen::AssistantGenre getAssistantGenre() const;
    oxygen::ArtisticDirection getArtisticDirection() const;
    void setAssistantGenre(oxygen::AssistantGenre genre);
    void setArtisticDirection(oxygen::ArtisticDirection direction);

private:
    std::vector<juce::AudioProcessorGraph::Node::Ptr> moduleNodes;
    
    juce::AudioProcessorGraph::Node::Ptr audioInputNode;
    juce::AudioProcessorGraph::Node::Ptr audioOutputNode;
    std::atomic<float> inputLevelL { 0.0f };
    std::atomic<float> inputLevelR { 0.0f };
    std::atomic<float> outputLevelL { 0.0f };
    std::atomic<float> outputLevelR { 0.0f };
    std::vector<float> analyzerScratchBuffer;
    juce::AudioBuffer<float> assistantHistoryBuffer;
    mutable juce::SpinLock assistantHistoryLock;
    int assistantHistoryWritePosition = 0;
    int assistantHistoryNumValidSamples = 0;
    int assistantHistoryCapacity = 32768;
    int assistantHistoryDecimationFactor = 1;
    int assistantDownsampleCounter = 0;
    float assistantDownsampleAccumL = 0.0f;
    float assistantDownsampleAccumR = 0.0f;
    double assistantHistorySampleRate = 44100.0;
    oxygen::AssistantGenre assistantGenre = oxygen::AssistantGenre::Universal;
    oxygen::ArtisticDirection artisticDirection = oxygen::ArtisticDirection::Balanced;
    std::function<void()> graphChangedCallback;

    juce::StringArray getCurrentModuleOrder() const;
    void applyModuleOrder(const juce::StringArray& savedOrder);
    void notifyGraphChanged();
    juce::AudioBuffer<float> buildAssistantAnalysisBuffer();

    static constexpr double assistantAnalysisWindowSeconds = 256.0;
    static constexpr double assistantTargetSampleRate = 24000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OxygenAudioProcessor)
};
