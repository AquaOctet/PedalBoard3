//! Application event system

use pedalboard_audio::AudioConfig;
use std::path::PathBuf;
use uuid::Uuid;

/// Application events
#[derive(Debug, Clone)]
pub enum AppEvent {
    // Application lifecycle
    ApplicationReady,
    ApplicationStarted,
    ApplicationStopped,
    Shutdown,
    Error { message: String },

    // Audio events
    AudioConfigChanged(AudioConfig),
    
    // Plugin events
    PluginLoaded { plugin_id: Uuid, instance_id: Uuid },
    PluginUnloaded { instance_id: Uuid },
    PluginParameterChanged { instance_id: Uuid, parameter: u32, value: f32 },
    PluginBypassed { instance_id: Uuid, bypassed: bool },
    
    // Preset events
    PresetLoaded { name: String },
    PresetSaved { name: String },
    PresetDeleted { name: String },
    
    // Session events
    SessionLoaded { path: PathBuf },
    SessionSaved { path: PathBuf },
    SessionCreated { name: String },
}