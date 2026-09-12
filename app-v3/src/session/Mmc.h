#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace pedalboard3::session::mmc
{

// The MIDI Machine Control commands the transport follows (M6 U3, plan KD5).
enum class Command
{
    None,
    Play,
    Stop,
    Pause,
    ReturnToZero
};

// Play and Deferred Play play, Stop stops, Pause toggles and Rewind returns to
// zero. Pedalboard2 sent Stop, Play and Pause all to one toggle, so a Stop
// could start its transport. Anything else, MMC or not, is None.
inline Command commandOf(const juce::MidiMessage& msg) noexcept
{
    if (!msg.isMidiMachineControlMessage())
        return Command::None;
    switch (msg.getMidiMachineControlCommand())
    {
    case juce::MidiMessage::mmc_play:
    case juce::MidiMessage::mmc_deferredplay:
        return Command::Play;
    case juce::MidiMessage::mmc_stop:
        return Command::Stop;
    case juce::MidiMessage::mmc_pause:
        return Command::Pause;
    case juce::MidiMessage::mmc_rewind:
        return Command::ReturnToZero;
    default:
        return Command::None;
    }
}

} // namespace pedalboard3::session::mmc
