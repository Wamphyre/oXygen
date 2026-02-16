#pragma once
#include <JuceHeader.h>

namespace oxygen
{
    class AudioBufferQueue
    {
    public:
        void push(const float* data, int numSamples)
        {
            int start1, size1, start2, size2;
            abstractFifo.prepareToWrite(numSamples, start1, size1, start2, size2);
            
            if (size1 > 0)
                juce::FloatVectorOperations::copy(buffer.data() + start1, data, size1);
            if (size2 > 0)
                juce::FloatVectorOperations::copy(buffer.data() + start2, data + size1, size2);
                
            abstractFifo.finishedWrite(size1 + size2);
        }
        
        void pop(float* outputData, int numSamples)
        {
            int start1, size1, start2, size2;
            abstractFifo.prepareToRead(numSamples, start1, size1, start2, size2);
            
            if (size1 > 0)
                juce::FloatVectorOperations::copy(outputData, buffer.data() + start1, size1);
            if (size2 > 0)
                juce::FloatVectorOperations::copy(outputData + size1, buffer.data() + start2, size2);
                
            abstractFifo.finishedRead(size1 + size2);
        }
        
        int getNumReady() const { return abstractFifo.getNumReady(); }

    private:
        static constexpr int bufferSize = 16384; // Holds enough samples
        juce::AbstractFifo abstractFifo { bufferSize };
        std::array<float, bufferSize> buffer;
    };
}
