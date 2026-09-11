#include "MidiRouter.h"

namespace pedalboard3::control
{

namespace
{

// How a matching message reads as a button for latch purposes.
enum class Press
{
    Down, // edge-capable source reporting "pressed"
    Up,   // edge-capable source reporting "released"
    Pulse // source with no release (Program Change / Pitch Bend): toggle each msg
};

Press readPress(MidiSourceKind kind, const juce::MidiMessage& msg) noexcept
{
    switch (kind)
    {
    case MidiSourceKind::ControlChange:
        return msg.getControllerValue() >= 64 ? Press::Down : Press::Up;
    case MidiSourceKind::Note:
        return msg.isNoteOn() ? Press::Down : Press::Up;
    case MidiSourceKind::ProgramChange:
    case MidiSourceKind::PitchBend:
        break;
    }
    return Press::Pulse;
}

} // namespace

void MidiRouter::setMappings(std::vector<MidiMapping> mappings)
{
    mappings_ = std::move(mappings);
    latch_.assign(mappings_.size(), LatchState{});
}

void MidiRouter::reset() noexcept
{
    for (auto& s : latch_)
        s = LatchState{};
}

std::vector<MidiRouter::Update> MidiRouter::process(const juce::MidiMessage& msg)
{
    std::vector<Update> updates;

    for (size_t i = 0; i < mappings_.size(); ++i)
    {
        const auto& m = mappings_[i];
        if (!matches(m.source, msg))
            continue;

        if (m.latch)
        {
            auto& st = latch_[i];
            const Press press = readPress(m.source.kind, msg);

            bool toggled = false;
            if (press == Press::Down)
            {
                if (!st.buttonHeld) // rising edge only — don't re-toggle while held
                {
                    st.buttonHeld = true;
                    st.outputOn = !st.outputOn;
                    toggled = true;
                }
            }
            else if (press == Press::Up)
            {
                st.buttonHeld = false; // release: no output, just arm the next press
            }
            else // Pulse: no release semantics, toggle on each message
            {
                st.outputOn = !st.outputOn;
                toggled = true;
            }

            if (toggled)
                updates.push_back({m.target, applyRange(m, st.outputOn ? 1.0f : 0.0f), true}); // a toggle is a press
            continue;
        }

        if (const auto raw = rawNormalized(m.source.kind, msg))
            updates.push_back({m.target, applyRange(m, *raw), readPress(m.source.kind, msg) != Press::Up});
    }

    return updates;
}

} // namespace pedalboard3::control
