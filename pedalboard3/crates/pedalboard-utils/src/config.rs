//! Configuration management for Pedalboard3.

use serde::{Deserialize, Serialize};
use std::path::PathBuf;
use crate::{SampleRate, BufferSize, PedalboardResult};

/// Main application configuration.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AppConfig {
    pub audio: AudioConfig,
    pub midi: MidiConfig,
    pub osc: OscConfig,
    pub gui: GuiConfig,
    pub plugins: PluginConfig,
}

/// Audio system configuration.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AudioConfig {
    pub sample_rate: f64,
    pub buffer_size: usize,
    pub input_channels: u32,
    pub output_channels: u32,
    pub device_name: Option<String>,
}

impl Default for AudioConfig {
    fn default() -> Self {
        Self {
            sample_rate: SampleRate::HIGH_QUALITY.as_f64(),
            buffer_size: BufferSize::BALANCED.as_usize(),
            input_channels: 2,
            output_channels: 2,
            device_name: None,
        }
    }
}

/// MIDI system configuration.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct MidiConfig {
    pub input_devices: Vec<String>,
    pub output_devices: Vec<String>,
    pub learn_mode_enabled: bool,
    pub channel_filter: Option<u8>,
}

impl Default for MidiConfig {
    fn default() -> Self {
        Self {
            input_devices: Vec::new(),
            output_devices: Vec::new(),
            learn_mode_enabled: false,
            channel_filter: None,
        }
    }
}

/// OSC (Open Sound Control) configuration.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct OscConfig {
    pub enabled: bool,
    pub input_port: u16,
    pub output_port: u16,
    pub output_address: String,
}

impl Default for OscConfig {
    fn default() -> Self {
        Self {
            enabled: true,
            input_port: 8000,
            output_port: 8001, 
            output_address: "127.0.0.1".to_string(),
        }
    }
}

/// GUI configuration.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct GuiConfig {
    pub theme: String,
    pub window_width: u32,
    pub window_height: u32,
    pub start_minimized: bool,
    pub show_tray_icon: bool,
}

impl Default for GuiConfig {
    fn default() -> Self {
        Self {
            theme: "default".to_string(),
            window_width: 1024,
            window_height: 768,
            start_minimized: false,
            show_tray_icon: true,
        }
    }
}

/// Plugin system configuration.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PluginConfig {
    pub vst2_paths: Vec<PathBuf>,
    pub vst3_paths: Vec<PathBuf>,
    pub clap_paths: Vec<PathBuf>,
    pub scan_on_startup: bool,
    pub sandbox_plugins: bool,
}

impl Default for PluginConfig {
    fn default() -> Self {
        Self {
            vst2_paths: Vec::new(),
            vst3_paths: Vec::new(),
            clap_paths: Vec::new(),
            scan_on_startup: true,
            sandbox_plugins: true,
        }
    }
}

impl Default for AppConfig {
    fn default() -> Self {
        Self {
            audio: AudioConfig::default(),
            midi: MidiConfig::default(),
            osc: OscConfig::default(),
            gui: GuiConfig::default(),
            plugins: PluginConfig::default(),
        }
    }
}

impl AppConfig {
    /// Load configuration from file.
    pub fn load_from_file<P: AsRef<std::path::Path>>(path: P) -> PedalboardResult<Self> {
        let content = std::fs::read_to_string(path)
            .map_err(|e| crate::PedalboardError::Config { 
                message: format!("Failed to read config file: {}", e) 
            })?;
            
        let config: AppConfig = toml::from_str(&content)
            .map_err(|e| crate::PedalboardError::Config { 
                message: format!("Failed to parse config: {}", e) 
            })?;
            
        Ok(config)
    }
    
    /// Save configuration to file.
    pub fn save_to_file<P: AsRef<std::path::Path>>(&self, path: P) -> PedalboardResult<()> {
        let content = toml::to_string_pretty(self)
            .map_err(|e| crate::PedalboardError::Config { 
                message: format!("Failed to serialize config: {}", e) 
            })?;
            
        std::fs::write(path, content)
            .map_err(|e| crate::PedalboardError::Config { 
                message: format!("Failed to write config file: {}", e) 
            })?;
            
        Ok(())
    }
}