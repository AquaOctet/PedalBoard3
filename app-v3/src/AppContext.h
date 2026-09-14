#pragma once

namespace pedalboard3::engine
{
class AudioEngine;
class PluginHost;
} // namespace pedalboard3::engine

namespace pedalboard3::session
{
class AppMappings;
class BankDocument;
class Preferences;
class Session;
class ControlHub;
class EditHistory;
class FeatureGate;
class TransportCommands;
} // namespace pedalboard3::session

namespace pedalboard3::control
{
class OscServer;
} // namespace pedalboard3::control

namespace pedalboard3
{

// Bundle of long-lived subsystem references handed to UI components so
// their constructors stay narrow as the app grows. `App` owns every
// subsystem by unique_ptr and passes references through here.
//
// Keep the struct trivially copyable — it's a reference bundle, not a
// container. MainWindow holds a reference to the struct, not its fields,
// so new members don't break the ctor signature.
struct AppContext
{
    engine::AudioEngine& audioEngine;
    engine::PluginHost& pluginHost;
    session::Preferences& preferences;
    session::Session& session;
    session::BankDocument& bankDocument;
    session::ControlHub& controlHub;
    session::AppMappings& appMappings;
    control::OscServer& oscServer;
    session::EditHistory& editHistory;             // finish pass U8: undoable canvas edits
    session::TransportCommands& transportCommands; // M6 U3: play, stop, tap tempo, MMC
    session::FeatureGate& features;                // optional-features U1: on in Settings and for this board
};

} // namespace pedalboard3
