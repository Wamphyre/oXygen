#pragma once
#include <JuceHeader.h>

namespace oxygen
{
    class AudioBufferQueue
    {
    public:
        void clear()
        {
            abstractFifo.reset();
            leftBuffer.fill(0.0f);
            rightBuffer.fill(0.0f);
        }

        void push(const juce::AudioBuffer<float>& sourceBuffer)
        {
            pushImpl(sourceBuffer);
        }

        void push(const juce::AudioBuffer<double>& sourceBuffer)
        {
            pushImpl(sourceBuffer);
        }

        template <typename SampleType>
        void pushImpl(const juce::AudioBuffer<SampleType>& sourceBuffer)
        {
            const int numSamples = sourceBuffer.getNumSamples();
            if (numSamples <= 0 || sourceBuffer.getNumChannels() <= 0)
                return;

            int start1, size1, start2, size2;
            abstractFifo.prepareToWrite(numSamples, start1, size1, start2, size2);

            if (size1 > 0)
                copyFromBuffer(sourceBuffer, start1, 0, size1);
            if (size2 > 0)
                copyFromBuffer(sourceBuffer, start2, size1, size2);

            abstractFifo.finishedWrite(size1 + size2);
        }
        
        void pop(float* leftOutput, float* rightOutput, int numSamples)
        {
            int start1, size1, start2, size2;
            abstractFifo.prepareToRead(numSamples, start1, size1, start2, size2);
            
            if (size1 > 0)
            {
                juce::FloatVectorOperations::copy(leftOutput, leftBuffer.data() + start1, size1);
                juce::FloatVectorOperations::copy(rightOutput, rightBuffer.data() + start1, size1);
            }
            if (size2 > 0)
            {
                juce::FloatVectorOperations::copy(leftOutput + size1, leftBuffer.data() + start2, size2);
                juce::FloatVectorOperations::copy(rightOutput + size1, rightBuffer.data() + start2, size2);
            }
                
            abstractFifo.finishedRead(size1 + size2);
        }
        
        int getNumReady() const { return abstractFifo.getNumReady(); }

    private:
        template <typename SampleType>
        void copyFromBuffer(const juce::AudioBuffer<SampleType>& sourceBuffer,
                            int destinationStart,
                            int sourceStart,
                            int numSamples)
        {
            const auto* left = sourceBuffer.getReadPointer(0);
            const auto* right = (sourceBuffer.getNumChannels() > 1)
                              ? sourceBuffer.getReadPointer(1)
                              : left;

            for (int sample = 0; sample < numSamples; ++sample)
            {
                leftBuffer[(size_t) (destinationStart + sample)] = (float) left[sourceStart + sample];
                rightBuffer[(size_t) (destinationStart + sample)] = (float) right[sourceStart + sample];
            }
        }

        static constexpr int bufferSize = 16384; // Holds enough samples
        juce::AbstractFifo abstractFifo { bufferSize };
        std::array<float, bufferSize> leftBuffer {};
        std::array<float, bufferSize> rightBuffer {};
    };
}
