#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Modules/GainModule.h"
#include "Modules/EqualizerModule.h"
#include "Modules/MultibandCompressorModule.h"
#include "Modules/MaximizerModule.h"
#include "Modules/StereoImagerModule.h"
#include <algorithm>
#include <cmath>

namespace
{
    constexpr auto pluginStateType = "OXYGEN_STATE";
    constexpr auto moduleOrderStateType = "MODULE_ORDER";
    constexpr auto moduleStatesType = "MODULE_STATES";
    constexpr auto moduleStateType = "MODULE";

    void storeParameters(juce::ValueTree& moduleState, juce::AudioProcessor& processor)
    {
        for (auto* parameter : processor.getParameters())
        {
            if (auto* parameterWithId = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter))
                moduleState.setProperty(parameterWithId->paramID, parameterWithId->getValue(), nullptr);
        }
    }

    void restoreParameters(const juce::ValueTree& moduleState, juce::AudioProcessor& processor)
    {
        for (auto* parameter : processor.getParameters())
        {
            if (auto* parameterWithId = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter))
            {
                if (moduleState.hasProperty(parameterWithId->paramID))
                    parameterWithId->setValueNotifyingHost((float) moduleState.getProperty(parameterWithId->paramID));
            }
        }
    }

    float getChannelMagnitude(const juce::AudioBuffer<float>& buffer, int channel)
    {
        if (channel < 0 || channel >= buffer.getNumChannels() || buffer.getNumSamples() == 0)
            return 0.0f;

        return buffer.getMagnitude(channel, 0, buffer.getNumSamples());
    }

    float scaleFromDefault(float defaultsValue, float targetValue, float amount)
    {
        return defaultsValue + ((targetValue - defaultsValue) * amount);
    }
}

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

    {
        const juce::SpinLock::ScopedLockType historyLock(assistantHistoryLock);
        assistantHistorySampleRate = sampleRate;
        assistantHistoryCapacity = juce::jmax(4096, juce::roundToInt(assistantHistorySampleRate * assistantAnalysisWindowSeconds));
        assistantHistoryBuffer.setSize(2, assistantHistoryCapacity);
        assistantHistoryBuffer.clear();
        assistantHistoryWritePosition = 0;
        assistantHistoryNumValidSamples = 0;
        assistantCapturePeak = 0.0f;
    }
    
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

    if (buffer.getNumChannels() > 0)
    {
        const juce::SpinLock::ScopedTryLockType historyLock(assistantHistoryLock);
        if (historyLock.isLocked())
        {
            const auto* left = buffer.getReadPointer(0);
            const auto* right = (buffer.getNumChannels() > 1) ? buffer.getReadPointer(1) : left;

            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            {
                const float storedL = left[sample];
                const float storedR = right[sample];

                assistantHistoryBuffer.setSample(0, assistantHistoryWritePosition, storedL);
                assistantHistoryBuffer.setSample(1, assistantHistoryWritePosition, storedR);
                assistantCapturePeak = juce::jmax(assistantCapturePeak, std::abs(storedL), std::abs(storedR));
                assistantHistoryWritePosition = (assistantHistoryWritePosition + 1) % assistantHistoryCapacity;
                assistantHistoryNumValidSamples = juce::jmin(assistantHistoryNumValidSamples + 1,
                                                             assistantHistoryCapacity);
            }
        }
    }

    const float inL = getChannelMagnitude(buffer, 0);
    const float inR = (buffer.getNumChannels() > 1) ? getChannelMagnitude(buffer, 1) : inL;
    inputLevelL.store(inL);
    inputLevelR.store(inR);

    // Process audio through the graph
    mainProcessorGraph->processBlock(buffer, midiMessages);
    
    const float outL = getChannelMagnitude(buffer, 0);
    const float outR = (buffer.getNumChannels() > 1) ? getChannelMagnitude(buffer, 1) : outL;
    outputLevelL.store(outL);
    outputLevelR.store(outR);

    // Feed the analyser with a mono sum of the processed output.
    if (buffer.getNumChannels() > 0)
    {
        analyzerScratchBuffer.resize((size_t) buffer.getNumSamples());

        if (buffer.getNumChannels() == 1)
        {
            juce::FloatVectorOperations::copy(analyzerScratchBuffer.data(),
                                              buffer.getReadPointer(0),
                                              buffer.getNumSamples());
        }
        else
        {
            const auto* left = buffer.getReadPointer(0);
            const auto* right = buffer.getReadPointer(1);

            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                analyzerScratchBuffer[(size_t) sample] = 0.5f * (left[sample] + right[sample]);
        }

        audioBufferQueue.push(analyzerScratchBuffer.data(), buffer.getNumSamples());
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
    notifyGraphChanged();
}

void OxygenAudioProcessor::updateGraphConnections()
{
    // Clear all existing connections properly
    while (!mainProcessorGraph->getConnections().empty())
        mainProcessorGraph->removeConnection(mainProcessorGraph->getConnections().front());
    
    int numInputChannels = getMainBusNumInputChannels();
    int numOutputChannels = getMainBusNumOutputChannels();
    const int numChannelsToProcess = juce::jmin(juce::jmin(numInputChannels, numOutputChannels), 2);

    auto addConnectionIfLegal = [this](juce::AudioProcessorGraph::NodeID srcNode, int srcChannel,
                                       juce::AudioProcessorGraph::NodeID dstNode, int dstChannel)
    {
        const juce::AudioProcessorGraph::Connection connection { { srcNode, srcChannel }, { dstNode, dstChannel } };
        if (mainProcessorGraph->isConnectionLegal(connection))
            mainProcessorGraph->addConnection(connection);
    };

    if (moduleNodes.empty())
    {
        for (int channel = 0; channel < numChannelsToProcess; ++channel)
            addConnectionIfLegal(audioInputNode->nodeID, channel, audioOutputNode->nodeID, channel);
        return;
    }

    // Connect Input -> First Module
    // Safety check for empty graph processing
    if (numChannelsToProcess == 0)
        return;

    for (int channel = 0; channel < numChannelsToProcess; ++channel)
        addConnectionIfLegal(audioInputNode->nodeID, channel, moduleNodes[0]->nodeID, channel);

    // Connect Modules in sequence
    for (size_t i = 1; i < moduleNodes.size(); ++i)
    {
        for (int channel = 0; channel < numChannelsToProcess; ++channel)
            addConnectionIfLegal(moduleNodes[i - 1]->nodeID, channel, moduleNodes[i]->nodeID, channel);
    }

    // Connect Last Module -> Output
    for (int channel = 0; channel < numChannelsToProcess; ++channel)
        addConnectionIfLegal(moduleNodes.back()->nodeID, channel, audioOutputNode->nodeID, channel);
}

void OxygenAudioProcessor::moveModuleUp(int index)
{
    if (index > 0 && index < (int)moduleNodes.size())
    {
        std::swap(moduleNodes[index], moduleNodes[index - 1]);
        updateGraphConnections();
        notifyGraphChanged();
    }
}

void OxygenAudioProcessor::moveModuleDown(int index)
{
    if (index >= 0 && index < (int)moduleNodes.size() - 1)
    {
        std::swap(moduleNodes[index], moduleNodes[index + 1]);
        updateGraphConnections();
        notifyGraphChanged();
    }
}

bool OxygenAudioProcessor::triggerMasterAssistant()
{
    oxygen::MasteringParameters params;
    if (!createMasterAssistantSuggestion(params))
        return false;

    return applyMasterAssistantSuggestion(params, getAssistantIntensity());
}

bool OxygenAudioProcessor::createMasterAssistantSuggestion(oxygen::MasteringParameters& params)
{
    const float minSignalGain = juce::Decibels::decibelsToGain(assistantMinSignalDb);
    float capturedPeak = 0.0f;
    {
        const juce::SpinLock::ScopedLockType historyLock(assistantHistoryLock);
        capturedPeak = assistantCapturePeak;
    }

    if (capturedPeak < minSignalGain)
        return false;

    auto analysisBuffer = buildAssistantAnalysisBuffer();
    if (analysisBuffer.getNumSamples() < 1024)
        return false;

    params = (inferenceEngine != nullptr)
           ? inferenceEngine->predict(analysisBuffer, assistantHistorySampleRate)
           : oxygen::MasteringParameters {};

    return true;
}

bool OxygenAudioProcessor::applyMasterAssistantSuggestion(const oxygen::MasteringParameters& suggestion,
                                                          AssistantIntensity intensity)
{
    auto params = suggestion;
    setAssistantIntensity(intensity);
    applyAssistantIntensity(params, intensity);
    applyMasteringParameters(params);

    return true;
}

void OxygenAudioProcessor::applyMasteringParameters(const oxygen::MasteringParameters& params)
{
    for (auto& node : mainProcessorGraph->getNodes())
    {
        if (auto* processor = node->getProcessor())
        {
            auto setParameter = [] (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float value)
            {
                if (auto* parameter = apvts.getParameter(id))
                    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
            };

            if (auto* eqModule = dynamic_cast<oxygen::EqualizerModule*>(processor))
            {
                for (auto* param : eqModule->getParameters())
                {
                    auto* parameterWithId = dynamic_cast<juce::AudioProcessorParameterWithID*>(param);
                    auto* rangedParameter = dynamic_cast<juce::RangedAudioParameter*>(param);

                    if (parameterWithId != nullptr && rangedParameter != nullptr)
                        if (parameterWithId->paramID.startsWith("Gain"))
                            rangedParameter->setValueNotifyingHost(rangedParameter->convertTo0to1(0.0f));
                }

                for (int band = 0; band < oxygen::MasteringParameters::eqBandCount; ++band)
                    setParameter(eqModule->apvts, "Gain" + juce::String(band), params.eqBandGains[(size_t) band]);
            }
            else if (auto* compModule = dynamic_cast<oxygen::MultibandCompressorModule*>(processor))
            {
                setParameter(compModule->apvts, "LowMidX", params.lowMidX);
                setParameter(compModule->apvts, "MidHighX", params.midHighX);
                setParameter(compModule->apvts, "HighX", params.highX);

                setParameter(compModule->apvts, "LowThresh", params.lowThresh);
                setParameter(compModule->apvts, "LowRatio", params.lowRatio);
                setParameter(compModule->apvts, "LowAttack", params.lowAttack);
                setParameter(compModule->apvts, "LowRelease", params.lowRelease);
                setParameter(compModule->apvts, "LowGain", 0.0f);

                setParameter(compModule->apvts, "LowMidThresh", params.lowMidThresh);
                setParameter(compModule->apvts, "LowMidRatio", params.lowMidRatio);
                setParameter(compModule->apvts, "LowMidAttack", params.lowMidAttack);
                setParameter(compModule->apvts, "LowMidRelease", params.lowMidRelease);
                setParameter(compModule->apvts, "LowMidGain", 0.0f);

                setParameter(compModule->apvts, "HighMidThresh", params.highMidThresh);
                setParameter(compModule->apvts, "HighMidRatio", params.highMidRatio);
                setParameter(compModule->apvts, "HighMidAttack", params.highMidAttack);
                setParameter(compModule->apvts, "HighMidRelease", params.highMidRelease);
                setParameter(compModule->apvts, "HighMidGain", 0.0f);

                setParameter(compModule->apvts, "HighThresh", params.highThresh);
                setParameter(compModule->apvts, "HighRatio", params.highRatio);
                setParameter(compModule->apvts, "HighAttack", params.highAttack);
                setParameter(compModule->apvts, "HighRelease", params.highRelease);
                setParameter(compModule->apvts, "HighGain", 0.0f);
            }
            else if (auto* imagerModule = dynamic_cast<oxygen::StereoImagerModule*>(processor))
            {
                setParameter(imagerModule->apvts, "LowWidth", params.lowWidth);
                setParameter(imagerModule->apvts, "LowMidWidth", params.lowMidWidth);
                setParameter(imagerModule->apvts, "HighMidWidth", params.highMidWidth);
                setParameter(imagerModule->apvts, "HighWidth", params.highWidth);
            }
            else if (auto* gainModule = dynamic_cast<oxygen::GainModule*>(processor))
            {
                if (auto* gainParameter = dynamic_cast<juce::RangedAudioParameter*>(gainModule->getParameters()[0]))
                    gainParameter->setValueNotifyingHost(gainParameter->convertTo0to1(params.outputGain));
            }
            else if (auto* maxModule = dynamic_cast<oxygen::MaximizerModule*>(processor))
            {
                setParameter(maxModule->apvts, "Threshold", params.maximizerThreshold);
                setParameter(maxModule->apvts, "Ceiling", params.maximizerCeiling);
                setParameter(maxModule->apvts, "Release", params.maximizerRelease);
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
    juce::ValueTree root(pluginStateType);
    root.setProperty("version", 2, nullptr);
    root.setProperty("assistantIntensity", assistantIntensityIndex.load(), nullptr);

    juce::ValueTree moduleOrder(moduleOrderStateType);
    for (const auto& name : getCurrentModuleOrder())
    {
        juce::ValueTree child(moduleStateType);
        child.setProperty("name", name, nullptr);
        moduleOrder.addChild(child, -1, nullptr);
    }

    juce::ValueTree moduleStates(moduleStatesType);
    for (const auto& node : moduleNodes)
    {
        if (auto* processor = node->getProcessor())
        {
            juce::ValueTree moduleState(moduleStateType);
            moduleState.setProperty("name", processor->getName(), nullptr);
            storeParameters(moduleState, *processor);
            moduleStates.addChild(moduleState, -1, nullptr);
        }
    }

    root.addChild(moduleOrder, -1, nullptr);
    root.addChild(moduleStates, -1, nullptr);

    if (auto xml = root.createXml())
        copyXmlToBinary(*xml, destData);
}

void OxygenAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState == nullptr)
        return;

    const auto root = juce::ValueTree::fromXml(*xmlState);
    if (!root.isValid() || !root.hasType(pluginStateType))
        return;

    if (root.hasProperty("assistantIntensity"))
        setAssistantIntensity(static_cast<AssistantIntensity>((int) root.getProperty("assistantIntensity")));

    if (auto orderState = root.getChildWithName(moduleOrderStateType); orderState.isValid())
    {
        juce::StringArray savedOrder;
        for (const auto& child : orderState)
        {
            if (child.hasProperty("name"))
                savedOrder.add(child["name"].toString());
        }

        applyModuleOrder(savedOrder);
    }

    if (auto states = root.getChildWithName(moduleStatesType); states.isValid())
    {
        for (const auto& moduleState : states)
        {
            const auto moduleName = moduleState["name"].toString();
            auto it = std::find_if(moduleNodes.begin(), moduleNodes.end(),
                                   [&moduleName](const juce::AudioProcessorGraph::Node::Ptr& node)
                                   {
                                       return node != nullptr
                                           && node->getProcessor() != nullptr
                                           && node->getProcessor()->getName() == moduleName;
                                   });

            if (it != moduleNodes.end())
                if (auto* processor = (*it)->getProcessor())
                    restoreParameters(moduleState, *processor);
        }
    }
}

float OxygenAudioProcessor::getInputLevel(int channel) const
{
    return (channel <= 0) ? inputLevelL.load() : inputLevelR.load();
}

float OxygenAudioProcessor::getOutputLevel(int channel) const
{
    return (channel <= 0) ? outputLevelL.load() : outputLevelR.load();
}

void OxygenAudioProcessor::resetAssistantAnalysisCapture()
{
    const juce::SpinLock::ScopedLockType historyLock(assistantHistoryLock);
    assistantHistoryBuffer.clear();
    assistantHistoryWritePosition = 0;
    assistantHistoryNumValidSamples = 0;
    assistantCapturePeak = 0.0f;
}

bool OxygenAudioProcessor::hasAssistantCapturedSignal() const
{
    const juce::SpinLock::ScopedLockType historyLock(assistantHistoryLock);
    return assistantCapturePeak >= juce::Decibels::decibelsToGain(assistantMinSignalDb);
}

void OxygenAudioProcessor::setAssistantIntensity(AssistantIntensity intensity)
{
    const int index = juce::jlimit(0, 2, static_cast<int>(intensity));
    assistantIntensityIndex.store(index);
}

OxygenAudioProcessor::AssistantIntensity OxygenAudioProcessor::getAssistantIntensity() const
{
    const int index = juce::jlimit(0, 2, assistantIntensityIndex.load());
    return static_cast<AssistantIntensity>(index);
}

void OxygenAudioProcessor::applyAssistantIntensity(oxygen::MasteringParameters& params, AssistantIntensity intensity) const
{
    const oxygen::MasteringParameters defaults {};

    float amount = 1.0f;
    switch (intensity)
    {
        case AssistantIntensity::Soft: amount = 0.72f; break;
        case AssistantIntensity::Standard: amount = 1.0f; break;
        case AssistantIntensity::Hard: amount = 1.28f; break;
    }

    for (int band = 0; band < oxygen::MasteringParameters::eqBandCount; ++band)
    {
        const float scaled = scaleFromDefault(defaults.eqBandGains[(size_t) band],
                                              params.eqBandGains[(size_t) band],
                                              amount);
        params.eqBandGains[(size_t) band] = juce::jlimit(-12.0f, 12.0f, scaled);
    }

    params.lowMidX = juce::jlimit(20.0f, 1000.0f, scaleFromDefault(defaults.lowMidX, params.lowMidX, amount));
    params.midHighX = juce::jlimit(500.0f, 5000.0f, scaleFromDefault(defaults.midHighX, params.midHighX, amount));
    params.highX = juce::jlimit(2000.0f, 15000.0f, scaleFromDefault(defaults.highX, params.highX, amount));

    params.lowThresh = juce::jlimit(-60.0f, 0.0f, scaleFromDefault(defaults.lowThresh, params.lowThresh, amount));
    params.lowRatio = juce::jlimit(1.0f, 20.0f, scaleFromDefault(defaults.lowRatio, params.lowRatio, amount));
    params.lowAttack = juce::jlimit(0.1f, 100.0f, scaleFromDefault(defaults.lowAttack, params.lowAttack, amount));
    params.lowRelease = juce::jlimit(10.0f, 1000.0f, scaleFromDefault(defaults.lowRelease, params.lowRelease, amount));

    params.lowMidThresh = juce::jlimit(-60.0f, 0.0f, scaleFromDefault(defaults.lowMidThresh, params.lowMidThresh, amount));
    params.lowMidRatio = juce::jlimit(1.0f, 20.0f, scaleFromDefault(defaults.lowMidRatio, params.lowMidRatio, amount));
    params.lowMidAttack = juce::jlimit(0.1f, 100.0f, scaleFromDefault(defaults.lowMidAttack, params.lowMidAttack, amount));
    params.lowMidRelease = juce::jlimit(10.0f, 1000.0f, scaleFromDefault(defaults.lowMidRelease, params.lowMidRelease, amount));

    params.highMidThresh = juce::jlimit(-60.0f, 0.0f, scaleFromDefault(defaults.highMidThresh, params.highMidThresh, amount));
    params.highMidRatio = juce::jlimit(1.0f, 20.0f, scaleFromDefault(defaults.highMidRatio, params.highMidRatio, amount));
    params.highMidAttack = juce::jlimit(0.1f, 100.0f, scaleFromDefault(defaults.highMidAttack, params.highMidAttack, amount));
    params.highMidRelease = juce::jlimit(10.0f, 1000.0f, scaleFromDefault(defaults.highMidRelease, params.highMidRelease, amount));

    params.highThresh = juce::jlimit(-60.0f, 0.0f, scaleFromDefault(defaults.highThresh, params.highThresh, amount));
    params.highRatio = juce::jlimit(1.0f, 20.0f, scaleFromDefault(defaults.highRatio, params.highRatio, amount));
    params.highAttack = juce::jlimit(0.1f, 100.0f, scaleFromDefault(defaults.highAttack, params.highAttack, amount));
    params.highRelease = juce::jlimit(10.0f, 1000.0f, scaleFromDefault(defaults.highRelease, params.highRelease, amount));

    params.lowWidth = juce::jlimit(0.0f, 2.0f, scaleFromDefault(defaults.lowWidth, params.lowWidth, amount));
    params.lowMidWidth = juce::jlimit(0.0f, 2.0f, scaleFromDefault(defaults.lowMidWidth, params.lowMidWidth, amount));
    params.highMidWidth = juce::jlimit(0.0f, 2.0f, scaleFromDefault(defaults.highMidWidth, params.highMidWidth, amount));
    params.highWidth = juce::jlimit(0.0f, 2.0f, scaleFromDefault(defaults.highWidth, params.highWidth, amount));

    params.outputGain = juce::jlimit(0.0f, 2.0f, scaleFromDefault(defaults.outputGain, params.outputGain, amount));
    params.maximizerThreshold = juce::jlimit(-60.0f, 0.0f, scaleFromDefault(defaults.maximizerThreshold, params.maximizerThreshold, amount));
    params.maximizerCeiling = juce::jlimit(-60.0f, 0.0f, scaleFromDefault(defaults.maximizerCeiling, params.maximizerCeiling, amount));
    params.maximizerRelease = juce::jlimit(1.0f, 500.0f, scaleFromDefault(defaults.maximizerRelease, params.maximizerRelease, amount));
}

void OxygenAudioProcessor::setGraphChangedCallback(std::function<void()> callback)
{
    graphChangedCallback = std::move(callback);
}

juce::StringArray OxygenAudioProcessor::getCurrentModuleOrder() const
{
    juce::StringArray order;
    for (const auto& node : moduleNodes)
    {
        if (node != nullptr)
            if (auto* processor = node->getProcessor())
                order.add(processor->getName());
    }

    return order;
}

void OxygenAudioProcessor::applyModuleOrder(const juce::StringArray& savedOrder)
{
    if (savedOrder.isEmpty())
        return;

    std::vector<juce::AudioProcessorGraph::Node::Ptr> remaining(moduleNodes.begin(), moduleNodes.end());
    std::vector<juce::AudioProcessorGraph::Node::Ptr> reordered;
    reordered.reserve(moduleNodes.size());

    for (const auto& name : savedOrder)
    {
        auto it = std::find_if(remaining.begin(), remaining.end(),
                               [&name](const juce::AudioProcessorGraph::Node::Ptr& node)
                               {
                                   return node != nullptr
                                       && node->getProcessor() != nullptr
                                       && node->getProcessor()->getName() == name;
                               });

        if (it != remaining.end())
        {
            reordered.push_back(*it);
            remaining.erase(it);
        }
    }

    reordered.insert(reordered.end(), remaining.begin(), remaining.end());
    moduleNodes = std::move(reordered);
    updateGraphConnections();
    notifyGraphChanged();
}

void OxygenAudioProcessor::notifyGraphChanged()
{
    if (graphChangedCallback)
        graphChangedCallback();
}

juce::AudioBuffer<float> OxygenAudioProcessor::buildAssistantAnalysisBuffer()
{
    juce::AudioBuffer<float> analysisBuffer;
    const juce::SpinLock::ScopedLockType historyLock(assistantHistoryLock);

    if (assistantHistoryNumValidSamples <= 0)
        return analysisBuffer;

    analysisBuffer.setSize(2, assistantHistoryNumValidSamples);
    const int startPosition = (assistantHistoryWritePosition - assistantHistoryNumValidSamples + assistantHistoryCapacity)
                            % assistantHistoryCapacity;

    for (int channel = 0; channel < 2; ++channel)
    {
        if (startPosition + assistantHistoryNumValidSamples <= assistantHistoryCapacity)
        {
            analysisBuffer.copyFrom(channel, 0,
                                    assistantHistoryBuffer, channel, startPosition,
                                    assistantHistoryNumValidSamples);
        }
        else
        {
            const int firstBlockSize = assistantHistoryCapacity - startPosition;
            const int secondBlockSize = assistantHistoryNumValidSamples - firstBlockSize;

            analysisBuffer.copyFrom(channel, 0,
                                    assistantHistoryBuffer, channel, startPosition,
                                    firstBlockSize);
            analysisBuffer.copyFrom(channel, firstBlockSize,
                                    assistantHistoryBuffer, channel, 0,
                                    secondBlockSize);
        }
    }

    return analysisBuffer;
}

// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OxygenAudioProcessor();
}
