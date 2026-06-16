#include "MidiControlSurface.h"

namespace pedalboard3::control
{

void MidiControlSurface::handleIncomingMidi(const juce::MidiMessage& msg)
{
    if (learning_)
    {
        // Capture the first learnable message; ignore anything else (note-off,
        // clock, ...) so a key release after the captured press is a no-op.
        if (auto src = sourceFromMessage(msg))
        {
            learning_ = false;
            auto cb = std::move(learnCb_);
            learnCb_ = nullptr;
            if (cb)
                cb(*src);
        }
        return; // while armed, never route
    }

    const auto updates = router_.process(msg);
    if (sink_)
        for (const auto& u : updates)
            sink_(u.target, u.value);
}

void MidiControlSurface::armLearn(LearnCallback onLearned)
{
    learnCb_ = std::move(onLearned);
    learning_ = true;
}

void MidiControlSurface::cancelLearn() noexcept
{
    learning_ = false;
    learnCb_ = nullptr;
}

} // namespace pedalboard3::control
