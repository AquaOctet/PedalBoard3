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
// Phase 1.C.6 members: AudioEngine + PluginHost.
// Phase 1.C.2 added: session::Preferences (per-user persistence).
// Phase 1.D.2 added: session::Session (queued patch-switch state machine).
// Phase 1.D.3: Session API expanded to multi-node patch (PluginSlot retired).
// M2 added: session::ControlHub (MIDI/OSC ingest — see ControlWiring.h) and
// control::OscServer (so the port-control UI can rebind it — U5).
//
// Upcoming additions (one field per subsystem as each phase lands):
//   - diagnostics::EventLog      (Phase 1.B+ — RT-safe observability)
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
    session::EditHistory& editHistory; // finish pass U8: undoable canvas edits
};

} // namespace pedalboard3
