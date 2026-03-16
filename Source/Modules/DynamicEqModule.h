#pragma once

#include <JuceHeader.h>
#include "../MasteringModule.h"
#include <array>
#include <limits>

namespace oxygen
{
    class DynamicEqModule : public MasteringModule
    {
    public:
        static constexpr int NumBands = 4;
        static constexpr std::array<float, NumBands> CenterFrequencies { 140.0f, 380.0f, 3200.0f, 9600.0f };
        static constexpr std::array<float, NumBands> BandQValues { 0.92f, 1.04f, 1.12f, 0.96f };

        DynamicEqModule();
        ~DynamicEqModule() override;

        void prepareToPlay(double sampleRate, int samplesPerBlock) override;
        void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

        bool hasEditor() const override { return true; }
        juce::AudioProcessorEditor* createEditor() override;

        static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
        juce::AudioProcessorValueTreeState apvts;
        juce::RangedAudioParameter* getBypassParameter() const override { return apvts.getParameter("Bypass"); }

    private:
        using MonoFilter = juce::dsp::IIR::Filter<float>;
        using Filter = juce::dsp::ProcessorDuplicator<MonoFilter, juce::dsp::IIR::Coefficients<float>>;

        struct BandRuntimeState
        {
            float thresholdDb = -24.0f;
            float maxCutDb = 0.0f;
            float attackCoeff = 0.0f;
            float releaseCoeff = 0.0f;
            float envelope = 0.0f;
            float currentGainReductionDb = 0.0f;
        };

        std::array<std::unique_ptr<Filter>, NumBands> isolationFilters;
        std::array<std::unique_ptr<Filter>, NumBands> dynamicFilters;
        std::array<juce::AudioBuffer<float>, NumBands> bandBuffers;
        juce::AudioBuffer<float> dryInputBuffer;
        std::array<BandRuntimeState, NumBands> runtimeStates;
        std::array<float, NumBands> lastThresholds;
        std::array<float, NumBands> lastRanges;
        std::array<float, NumBands> lastAppliedCuts;
        double lastSampleRate = 0.0;

        void updateFiltersAndParameters();
        float analyseDynamicBand(const juce::AudioBuffer<float>& bandBuffer, BandRuntimeState& state);

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DynamicEqModule)
    };
}
