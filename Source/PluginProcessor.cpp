#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Modules/GainModule.h"
#include "Modules/EqualizerModule.h"
#include "Modules/MultibandCompressorModule.h"
#include "Modules/MaximizerModule.h"
#include "Modules/StereoImagerModule.h"

using namespace oxygen;

OxygenAudioProcessor::OxygenAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                     .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                       ),
       mainProcessorGraph(std::make_unique<juce::AudioProcessorGraph>()),
       inferenceEngine(std::make_unique<oxygen::InferenceEngine>())
#endif
{
    initialiseGraph();
}

OxygenAudioProcessor::~OxygenAudioProcessor()
{
}

const juce::String OxygenAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool OxygenAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool OxygenAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool OxygenAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double OxygenAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int OxygenAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int OxygenAudioProcessor::getCurrentProgram()
{
    return 0;
}

void OxygenAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String OxygenAudioProcessor::getProgramName (int index)
{
    return {};
}

void OxygenAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

void OxygenAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    mainProcessorGraph->setPlayConfigDetails(getMainBusNumInputChannels(),
                                             getMainBusNumOutputChannels(),
                                             sampleRate, samplesPerBlock);
    mainProcessorGraph->prepareToPlay(sampleRate, samplesPerBlock);
    
    // Refresh connections to match the new layout
    updateGraphConnections();
}

void OxygenAudioProcessor::releaseResources()
{
    mainProcessorGraph->releaseResources();
}

bool OxygenAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}

void OxygenAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // Capture Input Level (RMS simplified to Peak for now)
    inputLevel.store(buffer.getMagnitude(0, buffer.getNumSamples()));

    // Process audio through the graph
    mainProcessorGraph->processBlock(buffer, midiMessages);
    
    // Capture Output Level
    outputLevel.store(buffer.getMagnitude(0, buffer.getNumSamples()));

    // Send to visualizer (Mono sum or Left channel)
    if (buffer.getNumChannels() > 0)
    {
        audioBufferQueue.push(buffer.getReadPointer(0), buffer.getNumSamples());
    }
}

void OxygenAudioProcessor::initialiseGraph()
{
    mainProcessorGraph->clear();
    moduleNodes.clear();
    
    // Create AudioGraphIOProcessors for input and output
    audioInputNode  = mainProcessorGraph->addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(juce::AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode));
    audioOutputNode = mainProcessorGraph->addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
    
    // Add Modules (Initial Order)
    moduleNodes.push_back(mainProcessorGraph->addNode(std::make_unique<oxygen::EqualizerModule>()));
    moduleNodes.push_back(mainProcessorGraph->addNode(std::make_unique<oxygen::MultibandCompressorModule>()));
    moduleNodes.push_back(mainProcessorGraph->addNode(std::make_unique<oxygen::StereoImagerModule>()));
    moduleNodes.push_back(mainProcessorGraph->addNode(std::make_unique<oxygen::GainModule>()));
    moduleNodes.push_back(mainProcessorGraph->addNode(std::make_unique<oxygen::MaximizerModule>()));

    updateGraphConnections();
}

void OxygenAudioProcessor::updateGraphConnections()
{
    // Clear all existing connections properly
    while (!mainProcessorGraph->getConnections().empty())
        mainProcessorGraph->removeConnection(mainProcessorGraph->getConnections().front());
    
    int numInputChannels = getMainBusNumInputChannels();
    int numOutputChannels = getMainBusNumOutputChannels();

    if (moduleNodes.empty())
    {
        for (int channel = 0; channel < numInputChannels; ++channel)
        {
            if (channel < numOutputChannels)
                mainProcessorGraph->addConnection({ { audioInputNode->nodeID, channel }, { audioOutputNode->nodeID, channel } });
        }
        return;
    }

    // Connect Input -> First Module
    // Safety check for empty graph processing
    if (numInputChannels == 0 || numOutputChannels == 0) return;

    for (int channel = 0; channel < numInputChannels; ++channel)
    {
         if (channel < 2) // Limit to stereo max for now if modules support it
            mainProcessorGraph->addConnection({ { audioInputNode->nodeID, channel }, { moduleNodes[0]->nodeID, channel } });
    }

    // Connect Modules in sequence
    for (size_t i = 1; i < moduleNodes.size(); ++i)
    {
        for (int channel = 0; channel < 2; ++channel) // Assuming modules are internal stereo/mono adaptive
        {
            // Connect both channels if possible. Even if mono, modules usually have 2 inputs in JUCE Graph unless restricted.
            // But we should check numInputChannels too? No, internal chain can be stereo even if input is mono potentially.
            mainProcessorGraph->addConnection({ { moduleNodes[i-1]->nodeID, channel }, { moduleNodes[i]->nodeID, channel } });
        }
    }

    // Connect Last Module -> Output
    for (int channel = 0; channel < numOutputChannels; ++channel)
    {
        if (channel < 2)
            mainProcessorGraph->addConnection({ { moduleNodes.back()->nodeID, channel }, { audioOutputNode->nodeID, channel } });
    }
}

void OxygenAudioProcessor::moveModuleUp(int index)
{
    if (index > 0 && index < (int)moduleNodes.size())
    {
        std::swap(moduleNodes[index], moduleNodes[index - 1]);
        updateGraphConnections();
    }
}

void OxygenAudioProcessor::moveModuleDown(int index)
{
    if (index >= 0 && index < (int)moduleNodes.size() - 1)
    {
        std::swap(moduleNodes[index], moduleNodes[index + 1]);
        updateGraphConnections();
    }
}

void OxygenAudioProcessor::triggerAutoMastering()
{
    // 1. "AI" Analysis Mock (In a real plugin, we would analyze the buffer history)
    // For now, we apply a "Golden Master" curve and settings (Ozone-like "Modern" target)

    // A. Set Equalizer to "Smiley Face" / Clarity Curve
    // Boost Subs (60Hz), Cut Mud (300Hz), Boost Air (10kHz+)
    for (auto& node : mainProcessorGraph->getNodes())
    {
        if (auto* processor = node->getProcessor())
        {
            if (auto* eqModule = dynamic_cast<oxygen::EqualizerModule*>(processor))
            {
                // Reset first
                for (auto* param : eqModule->getParameters())
                    if (auto* p = dynamic_cast<juce::RangedAudioParameter*>(param))
                        if (p->paramID.startsWith("Gain")) p->setValueNotifyingHost(p->convertTo0to1(0.0f));

                // Apply Curve (Indices based on 15 bands: 30, 40, 60, 100, 180, 300, 500, 900, 1.5k, 2.5k, 4k, 6k, 10k, 15k, 20k)
                // 60Hz (idx 2) +1.5dB
                // 300Hz (idx 5) -1.0dB
                // 10kHz (idx 12) +1.5dB
                // 15kHz (idx 13) +2.0dB
                
                auto setGain = [&](int idx, float db) {
                    if (idx < eqModule->getParameters().size()) {
                        if (auto* p = dynamic_cast<juce::RangedAudioParameter*>(eqModule->getParameters()[idx + 1])) // +1 for Bypass
                             p->setValueNotifyingHost(p->convertTo0to1(db));
                    }
                };

                // Note: Need ensure parameter indices align. 
                // Parameter layout: Bypass, Gain0, Gain1... 
                // So GainN is at index N+1.
                
                // Boost Lows (Kick/Bass body)
                if (auto* p = eqModule->apvts.getParameter("Gain2")) p->setValueNotifyingHost(p->convertTo0to1(1.5f)); // 60Hz
                
                // Cut Mud (Boxiness)
                if (auto* p = eqModule->apvts.getParameter("Gain5")) p->setValueNotifyingHost(p->convertTo0to1(-1.5f)); // 300Hz
                
                // Boost Presence/Air (Vocals/Cymbals)
                if (auto* p = eqModule->apvts.getParameter("Gain12")) p->setValueNotifyingHost(p->convertTo0to1(1.5f)); // 10kHz
                if (auto* p = eqModule->apvts.getParameter("Gain13")) p->setValueNotifyingHost(p->convertTo0to1(2.0f)); // 15kHz
            }
            
            // B. Set Stereo Imager (Mono Lows, Wide Highs)
            else if (auto* imagerModule = dynamic_cast<oxygen::StereoImagerModule*>(processor))
            {
                // Lows Mono (< 200Hz)
                if (auto* p = imagerModule->apvts.getParameter("LowWidth")) p->setValueNotifyingHost(p->convertTo0to1(0.2f)); // Nearly mono
                
                // LowMids Natural
                if (auto* p = imagerModule->apvts.getParameter("LowMidWidth")) p->setValueNotifyingHost(p->convertTo0to1(1.0f));
                
                // HighMids Wide
                if (auto* p = imagerModule->apvts.getParameter("HighMidWidth")) p->setValueNotifyingHost(p->convertTo0to1(1.2f));
                
                // Highs Very Wide
                if (auto* p = imagerModule->apvts.getParameter("HighWidth")) p->setValueNotifyingHost(p->convertTo0to1(1.4f));
            }
            
            // C. Set Maximizer (Loud but safe)
            else if (auto* maxModule = dynamic_cast<oxygen::MaximizerModule*>(processor))
            {
                // Target -9 LUFS approx implies -4dB to -6dB Threshold usually for pop
                // But safer to start conservative
                if (auto* p = maxModule->apvts.getParameter("Threshold")) p->setValueNotifyingHost(p->convertTo0to1(-3.0f));
                if (auto* p = maxModule->apvts.getParameter("Ceiling")) p->setValueNotifyingHost(p->convertTo0to1(-0.1f));
                if (auto* p = maxModule->apvts.getParameter("Release")) p->setValueNotifyingHost(p->convertTo0to1(100.0f)); // Fast/Modern
            }
        }
    }
}

bool OxygenAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* OxygenAudioProcessor::createEditor()
{
    return new OxygenAudioProcessorEditor (*this);
}

void OxygenAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void OxygenAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
}

// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OxygenAudioProcessor();
}
