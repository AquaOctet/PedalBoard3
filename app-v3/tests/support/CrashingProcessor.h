#pragma once

// A test double that faults on purpose, for the crash guard's tests (M5 U5):
// on block number `crashOnBlock` (0-based) processBlock scribbles on the buffer,
// then writes through a null pointer. Every other block is unity gain.

#include "TestGainProcessor.h"

#include <atomic>
#include <cstdint>

namespace pedalboard3::testing
{

// Read through a volatile, so the compiler can't see that it's null.
inline volatile std::uintptr_t crashAddress = 0;

inline void writeThroughNull() noexcept
{
    *reinterpret_cast<volatile int*>(crashAddress) = 1;
}

class CrashingProcessor : public TestGainProcessor
{
public:
    explicit CrashingProcessor(int crashOn = 0) : TestGainProcessor(1.0f), crashOnBlock(crashOn) {}

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        if (blocks.fetch_add(1) == crashOnBlock)
        {
            buffer.applyGain(3.0f); // scribble first, so a pass-through has to undo it
            writeThroughNull();
        }
        TestGainProcessor::processBlock(buffer, midi);
    }

    std::atomic<int> blocks{0};
    const int crashOnBlock;
};

} // namespace pedalboard3::testing
