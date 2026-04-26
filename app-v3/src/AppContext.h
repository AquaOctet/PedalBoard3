#pragma once

namespace pedalboard3::engine
{
class AudioEngine;
class PluginHost;
} // namespace pedalboard3::engine

namespace pedalboard3::session
{
class Preferences;
class PluginSlot;
} // namespace pedalboard3::session

namespace pedalboard3
{

// Bundle of long-lived subsystem references handed to UI components so
// their constructors stay narrow as the app grows. `App` owns every
// subsystem by unique_ptr and passes references through here.
//
// Phase 1.C.6 members: AudioEngine + PluginHost.
// Phase 1.C.2 added: session::Preferences (per-user persistence).
// Phase 1.D.1 added: session::PluginSlot (single-plugin host slot).
//
// Upcoming additions (one field per subsystem as each phase lands):
//   - session::Session           (Phase 1.D.2 — patch-switch state machine)
//   - control::MidiRouter        (Phase 4 — MIDI ingest / dispatch)
//   - control::OscServer         (Phase 4 — OSC ingest / dispatch)
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
    session::PluginSlot& pluginSlot;
};

} // namespace pedalboard3
