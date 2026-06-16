#include "MidiMapping.h"

namespace pedalboard3::control
{

namespace
{

// 0 = omni: a source channel of 0 accepts any message channel.
bool channelAccepts(int sourceChannel, const juce::MidiMessage& msg) noexcept
{
    if (sourceChannel == 0)
        return true;
    return msg.getChannel() == sourceChannel;
}

} // namespace

bool matches(const MidiSource& source, const juce::MidiMessage& msg) noexcept
{
    if (!channelAccepts(source.channel, msg))
        return false;

    switch (source.kind)
    {
    case MidiSourceKind::ControlChange:
        return msg.isController() && msg.getControllerNumber() == source.controller;
    case MidiSourceKind::Note:
        // Both note-on and note-off for the target note qualify; the transform
        // decides what each means (level vs. latch press).
        return (msg.isNoteOn() || msg.isNoteOff()) && msg.getNoteNumber() == source.controller;
    case MidiSourceKind::ProgramChange:
        return msg.isProgramChange();
    case MidiSourceKind::PitchBend:
        return msg.isPitchWheel();
    }
    return false;
}

std::optional<float> rawNormalized(MidiSourceKind kind, const juce::MidiMessage& msg) noexcept
{
    switch (kind)
    {
    case MidiSourceKind::ControlChange:
        if (!msg.isController())
            return std::nullopt;
        return static_cast<float>(msg.getControllerValue()) / 127.0f;
    case MidiSourceKind::Note:
        if (msg.isNoteOn())
            return static_cast<float>(msg.getVelocity()) / 127.0f;
        if (msg.isNoteOff())
            return 0.0f;
        return std::nullopt;
    case MidiSourceKind::ProgramChange:
        if (!msg.isProgramChange())
            return std::nullopt;
        return static_cast<float>(msg.getProgramChangeNumber()) / 127.0f;
    case MidiSourceKind::PitchBend:
        if (!msg.isPitchWheel())
            return std::nullopt;
        return static_cast<float>(msg.getPitchWheelValue()) / 16383.0f;
    }
    return std::nullopt;
}

float applyRange(const MidiMapping& mapping, float normalizedSource) noexcept
{
    float s = juce::jlimit(0.0f, 1.0f, normalizedSource);
    if (mapping.inverted)
        s = 1.0f - s;
    return mapping.rangeMin + s * (mapping.rangeMax - mapping.rangeMin);
}

std::optional<MidiSource> sourceFromMessage(const juce::MidiMessage& msg) noexcept
{
    MidiSource s;
    s.channel = msg.getChannel(); // 1..16 for channel messages, 0 otherwise

    if (msg.isController())
    {
        s.kind = MidiSourceKind::ControlChange;
        s.controller = msg.getControllerNumber();
        return s;
    }
    if (msg.isNoteOn())
    {
        s.kind = MidiSourceKind::Note;
        s.controller = msg.getNoteNumber();
        return s;
    }
    if (msg.isProgramChange())
    {
        s.kind = MidiSourceKind::ProgramChange;
        return s;
    }
    if (msg.isPitchWheel())
    {
        s.kind = MidiSourceKind::PitchBend;
        return s;
    }
    return std::nullopt;
}

} // namespace pedalboard3::control
