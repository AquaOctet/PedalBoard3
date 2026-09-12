#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <cstdint>

namespace pedalboard3::engine::processors
{

// The shared frame of the built-in processors M6 adds (plan U5, KD8, KD11):
//   - State is XML inside the node's state blob: <PB3_PROCESSOR type version>
//     with every parameter (normalised, as p_<id>), plus whatever writeState
//     adds. setStateInformation restores the parameters, then calls readState.
//   - The AudioProcessor boilerplate a built-in doesn't use (programs, no
//     editor, no MIDI unless a subclass says so).
//   - handOff: the audio thread asks for work only the message thread may do
//     (open a file, close a take). It sets bits and triggers an async update;
//     messageThreadWork receives them. Nothing allocates or locks on the
//     audio thread.
class ProcessorBase : public juce::AudioProcessor, private juce::AsyncUpdater
{
public:
    ProcessorBase(const BusesProperties& buses, juce::String typeId);
    ~ProcessorBase() override;

    const juce::String& getTypeId() const noexcept { return typeId_; }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

protected:
    // Extra state beyond the parameters (a file path, a folder).
    virtual void writeState(juce::XmlElement&) const {}
    virtual void readState(const juce::XmlElement&) {}

    // Any thread: ask for messageThreadWork(bits) on the message thread.
    void handOff(std::uint32_t bits) noexcept;
    virtual void messageThreadWork(std::uint32_t bits) { juce::ignoreUnused(bits); }

private:
    void handleAsyncUpdate() override;

    juce::String typeId_;
    std::atomic<std::uint32_t> pending_{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProcessorBase)
};

} // namespace pedalboard3::engine::processors
