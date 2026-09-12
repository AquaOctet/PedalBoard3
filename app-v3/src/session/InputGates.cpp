#include "InputGates.h"

#include "control/MidiInputManager.h"
#include "control/OscServer.h"

namespace pedalboard3::session
{

InputGates::InputGates(juce::ChangeBroadcaster& features,
                       Availability available,
                       control::MidiInputManager& midi,
                       control::OscServer& osc,
                       OscConfig oscConfig)
    : features_(features), available_(std::move(available)), midi_(midi), osc_(osc), oscConfig_(std::move(oscConfig))
{
    features_.addChangeListener(this);
}

InputGates::~InputGates()
{
    features_.removeChangeListener(this);
}

void InputGates::apply()
{
    midi_.setEnabled(available_(Feature::Midi));

    const bool wantOsc = available_(Feature::Osc);
    if (wantOsc && !osc_.isConnected())
    {
        const auto [port, group] = oscConfig_();
        oscError_ = osc_.rebind(port, group);
        // Non-fatal: the rest of the control layer works without OSC, and OSC
        // Settings can retry with another port.
        if (oscError_.isNotEmpty())
            juce::Logger::writeToLog("Pedalboard 3: OSC listener warning - " + oscError_);
    }
    else if (!wantOsc)
    {
        if (osc_.isConnected())
            osc_.disconnect();
        oscError_.clear();
    }
}

} // namespace pedalboard3::session
