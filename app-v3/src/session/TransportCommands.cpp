#include "TransportCommands.h"

#include "engine/Transport.h"
#include "Mmc.h"
#include "Session.h"

namespace pedalboard3::session
{

TransportCommands::TransportCommands(Session& session, engine::Transport& transport)
    : session_(session), transport_(transport)
{
}

void TransportCommands::setEnabled(bool enabled)
{
    if (enabled == enabled_)
        return;
    enabled_ = enabled;
    if (!enabled_)
        transport_.pause();
}

void TransportCommands::playPause()
{
    if (!enabled_)
        return;
    transport_.togglePlayPause();
}

void TransportCommands::stop()
{
    if (!enabled_)
        return;
    transport_.stop();
}

void TransportCommands::returnToZero()
{
    if (!enabled_)
        return;
    transport_.returnToZero();
}

void TransportCommands::tap()
{
    tapAt(juce::Time::getMillisecondCounterHiRes());
}

void TransportCommands::tapAt(double nowMs)
{
    if (!enabled_)
        return;
    if (const auto bpm = tapTempo_.tap(nowMs))
        session_.setTempo(*bpm); // into the patch, and on to the transport
}

bool TransportCommands::handleMmc(const juce::MidiMessage& msg)
{
    if (!mmcEnabled_ || !enabled_)
        return false;
    switch (mmc::commandOf(msg))
    {
    case mmc::Command::Play:
        transport_.play();
        return true;
    case mmc::Command::Stop:
        transport_.stop();
        return true;
    case mmc::Command::Pause:
        transport_.togglePlayPause();
        return true;
    case mmc::Command::ReturnToZero:
        transport_.returnToZero();
        return true;
    case mmc::Command::None:
        break;
    }
    return false;
}

} // namespace pedalboard3::session
