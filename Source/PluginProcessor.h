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
    OxygenAudioProcessor();
    ~OxygenAudioProcessor() override;
    
    oxygen::AudioBufferQueue& getInputAudioBufferQueue() { return inputAudioBufferQueue; }
    oxygen::AudioBufferQueue& getOutputAudioBufferQueue() { return outputAudioBufferQueue; }

    float getInputLevel() const { return juce::jmax(inputLevelL.load(), inputLevelR.load()); }
    float getOutputLevel() const { return juce::jmax(outputLevelL.load(), outputLevelR.load()); }
    float getInputLevel(int channel) const;
    float getOutputLevel(int channel) const;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    bool supportsDoublePrecisionProcessing() const override { return true; }

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;

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
    oxygen::AudioBufferQueue inputAudioBufferQueue;
    oxygen::AudioBufferQueue outputAudioBufferQueue;

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

    bool triggerMasterAssistant();
    bool createMasterAssistantSuggestion(oxygen::MasteringParameters& params);
    bool applyMasterAssistantSuggestion(const oxygen::MasteringParameters& params);
    void resetAssistantAnalysisCapture();
    bool hasAssistantCapturedSignal() const;
    void setGraphChangedCallback(std::function<void()> callback);

private:
    std::vector<juce::AudioProcessorGraph::Node::Ptr> moduleNodes;
    
    juce::AudioProcessorGraph::Node::Ptr audioInputNode;
    juce::AudioProcessorGraph::Node::Ptr audioOutputNode;
    std::atomic<float> inputLevelL { 0.0f };
    std::atomic<float> inputLevelR { 0.0f };
    std::atomic<float> outputLevelL { 0.0f };
    std::atomic<float> outputLevelR { 0.0f };
    juce::AudioBuffer<float> assistantHistoryBuffer;
    mutable juce::SpinLock assistantHistoryLock;
    int assistantHistoryWritePosition = 0;
    int assistantHistoryNumValidSamples = 0;
    int assistantHistoryCapacity = 32768;
    float assistantCapturePeak = 0.0f;
    double assistantHistorySampleRate = 44100.0;
    std::function<void()> graphChangedCallback;

    juce::StringArray getCurrentModuleOrder() const;
    void applyModuleOrder(const juce::StringArray& savedOrder);
    void notifyGraphChanged();
    void applyMasteringParameters(const oxygen::MasteringParameters& params);
    juce::AudioBuffer<float> buildAssistantAnalysisBuffer();

    static constexpr double assistantAnalysisWindowSeconds = 60.0;
    static constexpr float assistantMinSignalDb = -55.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OxygenAudioProcessor)
};
