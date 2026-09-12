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

void TransportCommands::playPause()
{
    transport_.togglePlayPause();
}

void TransportCommands::stop()
{
    transport_.stop();
}

void TransportCommands::returnToZero()
{
    transport_.returnToZero();
}

void TransportCommands::tap()
{
    tapAt(juce::Time::getMillisecondCounterHiRes());
}

void TransportCommands::tapAt(double nowMs)
{
    if (const auto bpm = tapTempo_.tap(nowMs))
        session_.setTempo(*bpm); // into the patch, and on to the transport
}

bool TransportCommands::handleMmc(const juce::MidiMessage& msg)
{
    if (!mmcEnabled_)
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
