#pragma once

#include <JuceHeader.h>

namespace oxygen
{

    class MasteringModule : public juce::AudioProcessor
    {
    public:
        MasteringModule(const juce::String& name)
            : AudioProcessor(BusesProperties()
                .withInput("Input", juce::AudioChannelSet::stereo(), true)
                .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
              moduleName(name)
        {
        }

        ~MasteringModule() override = default;

        const juce::String getName() const override { return moduleName; }

        void prepareToPlay(double sampleRate, int samplesPerBlock) override
        {
            juce::ignoreUnused(sampleRate, samplesPerBlock);
        }
        void releaseResources() override {}

        bool isBusesLayoutSupported(const BusesLayout& layouts) const override
        {
            const auto inputLayout = layouts.getMainInputChannelSet();
            const auto outputLayout = layouts.getMainOutputChannelSet();

            if (inputLayout != outputLayout)
                return false;

            return outputLayout == juce::AudioChannelSet::mono()
                || outputLayout == juce::AudioChannelSet::stereo();
        }

        // Subclasses must implement processBlock
        void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override = 0;

        juce::RangedAudioParameter* getBypassParameter() const override { return nullptr; }

        // Editor support defaults (can be overridden)
        bool hasEditor() const override { return false; }
        juce::AudioProcessorEditor* createEditor() override { return nullptr; }

        // Standard boilerplate for VST3
        bool acceptsMidi() const override { return false; }
        bool producesMidi() const override { return false; }
        bool isMidiEffect() const override { return false; }
        double getTailLengthSeconds() const override { return 0.0; }

        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram(int) override {}
        const juce::String getProgramName(int) override { return {}; }
        void changeProgramName(int, const juce::String&) override {}

        void getStateInformation(juce::MemoryBlock&) override {}
        void setStateInformation(const void*, int) override {}

    private:
        juce::String moduleName;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasteringModule)
    };

} // namespace oxygen
