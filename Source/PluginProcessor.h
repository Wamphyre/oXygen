#pragma once

#include <JuceHeader.h>
#include "AI/InferenceEngine.h"
#include "GUI/AudioBufferQueue.h"

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

    float getInputLevel() const { return inputLevel.load(); }
    float getOutputLevel() const { return outputLevel.load(); }

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
    std::atomic<float> inputLevel { 0.0f };
    std::atomic<float> outputLevel { 0.0f };

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

    void triggerAutoMastering();

private:
    std::vector<juce::AudioProcessorGraph::Node::Ptr> moduleNodes;
    
    juce::AudioProcessorGraph::Node::Ptr audioInputNode;
    juce::AudioProcessorGraph::Node::Ptr audioOutputNode;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OxygenAudioProcessor)
};
